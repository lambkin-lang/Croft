#include "croft/scene.h"
#include "croft/editor_brackets.h"
#include "croft/editor_commands.h"
#include "croft/editor_document.h"
#include "croft/editor_folding.h"
#include "croft/editor_line_cache.h"
#include "croft/editor_search.h"
#include "croft/editor_status.h"
#include "croft/editor_syntax.h"
#include "croft/editor_whitespace.h"
#include "croft/host_ui.h"
#include "croft/host_render.h"
#include <sapling/txn.h>
#include <sapling/text.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum {
    CROFT_KEY_RELEASE = 0,
    CROFT_KEY_ESCAPE = 256,
    CROFT_KEY_TAB = 258,
    CROFT_KEY_BACKSPACE = 259,
    CROFT_KEY_DELETE = 261,
    CROFT_KEY_RIGHT = 262,
    CROFT_KEY_LEFT = 263,
    CROFT_KEY_DOWN = 264,
    CROFT_KEY_UP = 265,
    CROFT_KEY_HOME = 268,
    CROFT_KEY_END = 269,
    CROFT_KEY_ENTER = 257,
    CROFT_KEY_F = 70,
    CROFT_KEY_G = 71,
    CROFT_KEY_LEFT_BRACKET = 91,
    CROFT_KEY_RIGHT_BRACKET = 93,
    CROFT_KEY_Y = 89,
    CROFT_KEY_Z = 90,
    CROFT_KEY_KP_ENTER_OLD = 284,
    CROFT_KEY_KP_ENTER = 335
};

typedef struct text_editor_layout {
    float gutter_width;
    float gutter_font_size;
    float text_inset_x;
    float text_inset_y;
    float content_width;
    float content_height;
    float status_height;
    float status_font_size;
} text_editor_layout;

typedef struct text_editor_visual_row {
    uint32_t model_line_number;
    uint32_t line_start_offset;
    uint32_t line_end_offset;
    uint32_t start_offset;
    uint32_t end_offset;
    uint32_t line_start_byte;
    uint32_t start_byte;
    uint32_t end_byte;
    uint8_t is_first_in_line;
    uint8_t is_last_in_line;
    uint8_t is_folded_header;
} text_editor_visual_row;

typedef struct text_editor_decoration_buffer {
    croft_text_editor_decoration items[128];
    uint32_t count;
} text_editor_decoration_buffer;

enum {
    TEXT_EDITOR_TEXT_INSET_X = 12,
    TEXT_EDITOR_TEXT_INSET_Y = 8,
    TEXT_EDITOR_LINE_SLACK_Y = 6,
    TEXT_EDITOR_COMPOSITION_MAX_PREVIEW_BYTES = 256
};

static int32_t text_editor_prepare_layout(text_editor_node* te,
                                          float node_width,
                                          float node_height,
                                          text_editor_layout* out_layout);
static void text_editor_ensure_cursor_visible(text_editor_node* te);
static void text_editor_resolve_font_metrics(text_editor_node* te);
static float text_editor_row_top(const text_editor_node* te, uint32_t visible_line_number);
static float text_editor_row_baseline(const text_editor_node* te, uint32_t visible_line_number);
static void text_editor_clear_composition_state(text_editor_node* te);
static void text_editor_normalize_decorations(text_editor_node* te);
static float text_editor_visual_row_prefix_width(const text_editor_node* te,
                                                 const text_editor_visual_row* row,
                                                 const char* line_text,
                                                 uint32_t offset);

static void text_editor_invalidate_visual_layout(text_editor_node* te) {
    if (!te) {
        return;
    }
    te->visual_layout_dirty = 1u;
}

static croft_text_editor_caret_affinity text_editor_normalize_caret_affinity(
    croft_text_editor_caret_affinity affinity) {
    if (affinity == CROFT_TEXT_EDITOR_CARET_AFFINITY_TRAILING) {
        return CROFT_TEXT_EDITOR_CARET_AFFINITY_TRAILING;
    }
    return CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING;
}

static int text_editor_caret_affinity_prefers_leading(croft_text_editor_caret_affinity affinity) {
    return text_editor_normalize_caret_affinity(affinity) == CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING;
}

static croft_text_editor_caret_affinity text_editor_affinity_for_row_offset(
    const text_editor_visual_row* row,
    uint32_t offset) {
    if (!row) {
        return CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING;
    }
    if (!row->is_last_in_line && offset == row->end_offset) {
        return CROFT_TEXT_EDITOR_CARET_AFFINITY_TRAILING;
    }
    return CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING;
}

static void text_editor_resolve_font_metrics(text_editor_node* te) {
    float ascender;
    float descender;
    float glyph_height;
    float natural_line_height;
    float vertical_padding;

    if (!te) {
        return;
    }
    if (te->font_metrics_valid) {
        return;
    }

    memset(&te->font_probe, 0, sizeof(te->font_probe));
    if (host_render_probe_font(te->font_size,
                               CROFT_EDITOR_FONT_PROBE_SAMPLE,
                               (uint32_t)strlen(CROFT_EDITOR_FONT_PROBE_SAMPLE),
                               &te->font_probe) == 0) {
        te->font_metrics_valid = 1u;
    } else {
        te->font_probe.point_size = te->font_size;
    }

    ascender = te->font_probe.ascender > 0.0f ? te->font_probe.ascender : te->font_size * 0.8f;
    descender = te->font_probe.descender > 0.0f ? te->font_probe.descender : te->font_size * 0.2f;
    glyph_height = ascender + descender;
    if (glyph_height <= 0.0f) {
        glyph_height = te->font_size;
    }
    natural_line_height = te->font_probe.line_height > 0.0f ? te->font_probe.line_height : glyph_height;
    if (natural_line_height < glyph_height) {
        natural_line_height = glyph_height;
    }
    if (te->font_probe.ascender <= 0.0f) {
        te->font_probe.ascender = ascender;
    }
    if (te->font_probe.descender <= 0.0f) {
        te->font_probe.descender = descender;
    }
    if (te->font_probe.line_height <= 0.0f) {
        te->font_probe.line_height = natural_line_height;
    }

    te->line_height = natural_line_height + (float)TEXT_EDITOR_LINE_SLACK_Y;
    if (te->line_height < te->font_size + 6.0f) {
        te->line_height = te->font_size + 6.0f;
    }

    vertical_padding = te->line_height - glyph_height;
    if (vertical_padding < 0.0f) {
        vertical_padding = 0.0f;
    }
    te->baseline_offset = (vertical_padding * 0.5f) + ascender;
    if (te->baseline_offset < ascender) {
        te->baseline_offset = ascender;
    }
}

static float text_editor_row_top(const text_editor_node* te, uint32_t visible_line_number) {
    if (!te || visible_line_number == 0u) {
        return 0.0f;
    }
    return ((float)(visible_line_number - 1u) * te->line_height);
}

static float text_editor_row_baseline(const text_editor_node* te, uint32_t visible_line_number) {
    return text_editor_row_top(te, visible_line_number) + (te ? te->baseline_offset : 0.0f);
}

static int text_editor_has_active_composition(const text_editor_node* te) {
    return te && te->composition_utf8 && te->composition_len > 0u;
}

static int text_editor_find_fold_header_index(const text_editor_node* te,
                                              uint32_t line_number,
                                              uint32_t* out_index);

static croft_text_editor_profile_snapshot* text_editor_profile_stats_mut(const text_editor_node* te) {
    return te ? &((text_editor_node*)te)->profile_stats : NULL;
}

static int text_editor_profile_enabled(const text_editor_node* te) {
    return te && te->profiling_enabled != 0u;
}

static uint64_t text_editor_profile_now_usec(void) {
    double now = host_ui_get_time();
    if (now <= 0.0) {
        return 0u;
    }
    return (uint64_t)(now * 1000000.0);
}

static uint64_t text_editor_profile_begin(const text_editor_node* te) {
    return text_editor_profile_enabled(te) ? text_editor_profile_now_usec() : 0u;
}

static void text_editor_profile_note(croft_text_editor_profile_snapshot* stats,
                                     uint64_t* calls,
                                     uint64_t* total_usec,
                                     uint64_t start_usec) {
    uint64_t end_usec;

    if (!stats || !calls || !total_usec || start_usec == 0u) {
        return;
    }

    end_usec = text_editor_profile_now_usec();
    (*calls)++;
    if (end_usec >= start_usec) {
        *total_usec += end_usec - start_usec;
    }
}

static float text_editor_measure_text(const text_editor_node* te,
                                      const char* text,
                                      uint32_t len,
                                      float font_size) {
    croft_text_editor_profile_snapshot* stats = text_editor_profile_stats_mut(te);
    uint64_t start_usec = text_editor_profile_begin(te);
    float width = host_render_measure_text(text, len, font_size);

    if (stats && start_usec != 0u) {
        uint64_t end_usec = text_editor_profile_now_usec();
        stats->measure_text_calls++;
        stats->measure_text_total_bytes += len;
        if (end_usec >= start_usec) {
            stats->measure_text_total_usec += end_usec - start_usec;
        }
    }

    return width;
}

static float text_editor_status_font_size(const text_editor_node* te) {
    float font_size = te ? (te->font_size * 0.8f) : 12.0f;
    if (font_size < 12.0f) {
        font_size = 12.0f;
    }
    if (font_size > 14.0f) {
        font_size = 14.0f;
    }
    return font_size;
}

static float text_editor_gutter_font_size(const text_editor_node* te) {
    float font_size = te ? (te->font_size * 0.82f) : 12.0f;
    if (font_size < 11.0f) {
        font_size = 11.0f;
    }
    if (te && font_size > te->font_size - 1.0f) {
        font_size = te->font_size - 1.0f;
    }
    return font_size;
}

static void text_editor_compute_layout(const text_editor_node* te,
                                       float node_width,
                                       float node_height,
                                       text_editor_layout* out_layout) {
    croft_text_editor_profile_snapshot* stats = text_editor_profile_stats_mut(te);
    uint64_t start_usec = text_editor_profile_begin(te);
    uint32_t line_count = 1u;
    uint32_t digits;
    char line_label[16];
    int line_label_len;
    float gutter_font_size;
    float status_font_size;
    float gutter_label_width = 0.0f;

    if (!out_layout) {
        return;
    }

    text_editor_resolve_font_metrics((text_editor_node*)te);

    if (te) {
        line_count = croft_editor_text_model_line_count(&te->text_model);
        if (line_count == 0u) {
            line_count = 1u;
        }
    }

    digits = croft_editor_line_number_digits(line_count);
    line_label_len = snprintf(line_label, sizeof(line_label), "%u", line_count);
    gutter_font_size = text_editor_gutter_font_size(te);
    status_font_size = text_editor_status_font_size(te);
    if (line_label_len > 0) {
        gutter_label_width = text_editor_measure_text(te,
                                                      line_label,
                                                      (uint32_t)line_label_len,
                                                      gutter_font_size);
    } else {
        gutter_label_width = (float)digits * gutter_font_size * 0.6f;
    }

    out_layout->gutter_font_size = gutter_font_size;
    out_layout->status_font_size = status_font_size;
    out_layout->text_inset_x = (float)TEXT_EDITOR_TEXT_INSET_X;
    out_layout->text_inset_y = (float)TEXT_EDITOR_TEXT_INSET_Y;
    out_layout->status_height = status_font_size + 10.0f;
    if (out_layout->status_height < 24.0f) {
        out_layout->status_height = 24.0f;
    }
    out_layout->content_height = node_height - out_layout->status_height;
    if (out_layout->content_height < te->line_height) {
        out_layout->content_height = te->line_height;
    }
    out_layout->gutter_width = gutter_label_width + 28.0f;
    if (out_layout->gutter_width < 42.0f) {
        out_layout->gutter_width = 42.0f;
    }
    out_layout->content_width = node_width - out_layout->gutter_width;
    if (out_layout->content_width < 0.0f) {
        out_layout->content_width = 0.0f;
    }

    if (stats) {
        text_editor_profile_note(stats,
                                 &stats->layout_calls,
                                 &stats->layout_total_usec,
                                 start_usec);
    }
}

static int32_t text_editor_reserve_visual_rows(text_editor_node* te, uint32_t capacity) {
    text_editor_visual_row* resized;

    if (!te) {
        return CROFT_EDITOR_ERR_INVALID;
    }
    if (capacity <= te->visual_row_capacity) {
        return CROFT_EDITOR_OK;
    }

    resized = (text_editor_visual_row*)realloc(te->visual_rows,
                                               sizeof(text_editor_visual_row) * (size_t)capacity);
    if (!resized) {
        return CROFT_EDITOR_ERR_OOM;
    }

    te->visual_rows = resized;
    te->visual_row_capacity = capacity;
    return CROFT_EDITOR_OK;
}

static int32_t text_editor_reserve_line_row_maps(text_editor_node* te, uint32_t line_count) {
    uint32_t* first_rows;
    uint32_t* row_counts;

    if (!te) {
        return CROFT_EDITOR_ERR_INVALID;
    }
    if (line_count <= te->line_visible_row_capacity) {
        if (line_count > 0u) {
            memset(te->line_first_visible_rows, 0, sizeof(uint32_t) * (size_t)line_count);
            memset(te->line_visible_row_counts, 0, sizeof(uint32_t) * (size_t)line_count);
        }
        return CROFT_EDITOR_OK;
    }

    first_rows = (uint32_t*)malloc(sizeof(uint32_t) * (size_t)line_count);
    if (!first_rows) {
        return CROFT_EDITOR_ERR_OOM;
    }
    row_counts = (uint32_t*)malloc(sizeof(uint32_t) * (size_t)line_count);
    if (!row_counts) {
        free(first_rows);
        return CROFT_EDITOR_ERR_OOM;
    }

    free(te->line_first_visible_rows);
    free(te->line_visible_row_counts);
    te->line_first_visible_rows = first_rows;
    te->line_visible_row_counts = row_counts;
    te->line_visible_row_capacity = line_count;
    memset(te->line_first_visible_rows, 0, sizeof(uint32_t) * (size_t)line_count);
    memset(te->line_visible_row_counts, 0, sizeof(uint32_t) * (size_t)line_count);
    return CROFT_EDITOR_OK;
}

static int32_t text_editor_reserve_decorations(text_editor_node* te, uint32_t capacity) {
    croft_text_editor_decoration* resized;

    if (!te) {
        return CROFT_EDITOR_ERR_INVALID;
    }
    if (capacity <= te->decoration_capacity) {
        return CROFT_EDITOR_OK;
    }

    resized = (croft_text_editor_decoration*)realloc(
        te->decorations,
        sizeof(croft_text_editor_decoration) * (size_t)capacity);
    if (!resized) {
        return CROFT_EDITOR_ERR_OOM;
    }

    te->decorations = resized;
    te->decoration_capacity = capacity;
    return CROFT_EDITOR_OK;
}

static int32_t text_editor_append_visual_row(text_editor_node* te,
                                             uint32_t model_line_number,
                                             uint32_t line_start_offset,
                                             uint32_t line_end_offset,
                                             uint32_t line_start_byte,
                                             uint32_t start_offset,
                                             uint32_t end_offset,
                                             uint32_t is_first_in_line,
                                             uint32_t is_last_in_line,
                                             uint32_t is_folded_header) {
    text_editor_visual_row* row;

    if (!te) {
        return CROFT_EDITOR_ERR_INVALID;
    }
    if (te->visual_row_count == te->visual_row_capacity
            && text_editor_reserve_visual_rows(
                   te,
                   te->visual_row_capacity > 0u ? te->visual_row_capacity * 2u : 16u)
                != CROFT_EDITOR_OK) {
        return CROFT_EDITOR_ERR_OOM;
    }

    row = &te->visual_rows[te->visual_row_count++];
    row->model_line_number = model_line_number;
    row->line_start_offset = line_start_offset;
    row->line_end_offset = line_end_offset;
    row->start_offset = start_offset;
    row->end_offset = end_offset;
    row->line_start_byte = line_start_byte;
    row->start_byte = croft_editor_text_model_byte_offset_at(&te->text_model, start_offset) - line_start_byte;
    row->end_byte = croft_editor_text_model_byte_offset_at(&te->text_model, end_offset) - line_start_byte;
    row->is_first_in_line = (uint8_t)(is_first_in_line ? 1u : 0u);
    row->is_last_in_line = (uint8_t)(is_last_in_line ? 1u : 0u);
    row->is_folded_header = (uint8_t)(is_folded_header ? 1u : 0u);
    return CROFT_EDITOR_OK;
}

static uint32_t text_editor_wrap_segment_end(const text_editor_node* te,
                                             const char* line_text,
                                             uint32_t line_start_byte,
                                             uint32_t start_offset,
                                             uint32_t line_end_offset,
                                             float wrap_width) {
    uint32_t low;
    uint32_t high;
    uint32_t best;
    uint32_t start_byte;

    if (!te || !line_text || line_end_offset <= start_offset) {
        return line_end_offset;
    }

    start_byte = croft_editor_text_model_byte_offset_at(&te->text_model, start_offset) - line_start_byte;
    low = start_offset + 1u;
    high = line_end_offset;
    best = low;
    while (low <= high) {
        uint32_t mid = low + ((high - low) / 2u);
        uint32_t end_byte = croft_editor_text_model_byte_offset_at(&te->text_model, mid) - line_start_byte;
        float width = text_editor_measure_text(te,
                                               line_text + start_byte,
                                               end_byte - start_byte,
                                               te->font_size);

        if (width <= wrap_width || mid == start_offset + 1u) {
            best = mid;
            low = mid + 1u;
        } else {
            if (mid == 0u) {
                break;
            }
            high = mid - 1u;
        }
    }
    if (best <= start_offset) {
        best = start_offset + 1u;
    }
    if (best > line_end_offset) {
        best = line_end_offset;
    }
    return best;
}

static int32_t text_editor_rebuild_visual_layout(text_editor_node* te,
                                                 const text_editor_layout* layout) {
    uint32_t line_count;
    uint32_t line_number;
    float wrap_width;

    if (!te || !layout) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    line_count = croft_editor_text_model_line_count(&te->text_model);
    if (text_editor_reserve_line_row_maps(te, line_count) != CROFT_EDITOR_OK) {
        return CROFT_EDITOR_ERR_OOM;
    }

    te->visual_row_count = 0u;
    wrap_width = layout->content_width - layout->text_inset_x - 12.0f;
    if (wrap_width < te->font_size * 0.75f) {
        wrap_width = te->font_size * 0.75f;
    }

    if (line_count == 0u) {
        if (text_editor_append_visual_row(te, 1u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 0u)
                != CROFT_EDITOR_OK) {
            return CROFT_EDITOR_ERR_OOM;
        }
        te->visual_layout_width = layout->content_width;
        te->visual_layout_dirty = 0u;
        return CROFT_EDITOR_OK;
    }

    for (line_number = 1u; line_number <= line_count;) {
        uint32_t line_start_offset;
        uint32_t line_end_offset;
        uint32_t line_start_byte;
        uint32_t first_row = te->visual_row_count + 1u;
        uint32_t fold_index = 0u;
        uint32_t is_folded = (uint32_t)text_editor_find_fold_header_index(te, line_number, &fold_index);
        uint32_t search_offset;
        uint32_t end_byte;
        uint32_t line_len_bytes = 0u;
        const char* line_text =
            croft_editor_text_model_line_utf8(&te->text_model, line_number, &line_len_bytes);

        line_start_offset = croft_editor_text_model_line_start_offset(&te->text_model, line_number);
        line_end_offset = croft_editor_text_model_line_end_offset(&te->text_model, line_number);
        line_start_byte = croft_editor_text_model_byte_offset_at(&te->text_model, line_start_offset);
        end_byte = croft_editor_text_model_byte_offset_at(&te->text_model, line_end_offset) - line_start_byte;

        if (!te->wrap_enabled || line_end_offset <= line_start_offset) {
            if (text_editor_append_visual_row(te,
                                              line_number,
                                              line_start_offset,
                                              line_end_offset,
                                              line_start_byte,
                                              line_start_offset,
                                              line_end_offset,
                                              1u,
                                              1u,
                                              is_folded) != CROFT_EDITOR_OK) {
                return CROFT_EDITOR_ERR_OOM;
            }
        } else {
            search_offset = line_start_offset;
            while (search_offset < line_end_offset) {
                uint32_t start_byte =
                    croft_editor_text_model_byte_offset_at(&te->text_model, search_offset) - line_start_byte;
                uint32_t segment_end = line_end_offset;
                float segment_width = text_editor_measure_text(te,
                                                               line_text + start_byte,
                                                               end_byte - start_byte,
                                                               te->font_size);

                if (segment_width > wrap_width) {
                    segment_end = text_editor_wrap_segment_end(te,
                                                               line_text,
                                                               line_start_byte,
                                                               search_offset,
                                                               line_end_offset,
                                                               wrap_width);
                }
                if (text_editor_append_visual_row(te,
                                                  line_number,
                                                  line_start_offset,
                                                  line_end_offset,
                                                  line_start_byte,
                                                  search_offset,
                                                  segment_end,
                                                  search_offset == line_start_offset,
                                                  segment_end >= line_end_offset,
                                                  is_folded && segment_end >= line_end_offset)
                        != CROFT_EDITOR_OK) {
                    return CROFT_EDITOR_ERR_OOM;
                }
                search_offset = segment_end;
            }
        }

        te->line_first_visible_rows[line_number - 1u] = first_row;
        te->line_visible_row_counts[line_number - 1u] = te->visual_row_count - (first_row - 1u);

        if (is_folded) {
            line_number = te->folded_regions[fold_index].end_line_number + 1u;
        } else {
            line_number++;
        }
    }

    te->visual_layout_width = layout->content_width;
    te->visual_layout_dirty = 0u;
    return CROFT_EDITOR_OK;
}

static int32_t text_editor_prepare_layout(text_editor_node* te,
                                          float node_width,
                                          float node_height,
                                          text_editor_layout* out_layout) {
    if (!te || !out_layout) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    text_editor_compute_layout(te, node_width, node_height, out_layout);
    if (te->visual_layout_dirty
            || te->visual_rows == NULL
            || te->visual_layout_width != out_layout->content_width) {
        return text_editor_rebuild_visual_layout(te, out_layout);
    }
    return CROFT_EDITOR_OK;
}

static const text_editor_visual_row* text_editor_visual_row_at_visible_line(const text_editor_node* te,
                                                                            uint32_t visible_line_number) {
    if (!te || !te->visual_rows || visible_line_number == 0u || visible_line_number > te->visual_row_count) {
        return NULL;
    }
    return &te->visual_rows[visible_line_number - 1u];
}

static uint32_t text_editor_visual_row_number_for_offset(const text_editor_node* te,
                                                         uint32_t offset,
                                                         int prefer_leading_edge) {
    croft_editor_position position;
    uint32_t first_row;
    uint32_t row_count;
    uint32_t index;

    if (!te || te->visual_row_count == 0u) {
        return 1u;
    }

    if (croft_editor_text_model_line_count(&te->text_model) == 0u) {
        return 1u;
    }

    if (offset > croft_editor_text_model_codepoint_length(&te->text_model)) {
        offset = croft_editor_text_model_codepoint_length(&te->text_model);
    }

    position = croft_editor_text_model_get_position_at(&te->text_model, offset);
    if (position.line_number == 0u || position.line_number > croft_editor_text_model_line_count(&te->text_model)) {
        return te->visual_row_count;
    }

    first_row = te->line_first_visible_rows[position.line_number - 1u];
    row_count = te->line_visible_row_counts[position.line_number - 1u];
    if (first_row == 0u || row_count == 0u) {
        return 0u;
    }

    for (index = 0u; index < row_count; index++) {
        const text_editor_visual_row* row = &te->visual_rows[(first_row - 1u) + index];

        if (prefer_leading_edge) {
            if (!row->is_last_in_line && offset == row->end_offset) {
                continue;
            }
        }
        if (offset >= row->start_offset && offset <= row->end_offset) {
            return first_row + index;
        }
    }

    return first_row + row_count - 1u;
}

static uint32_t text_editor_visual_row_number_for_affinity(
    const text_editor_node* te,
    uint32_t offset,
    croft_text_editor_caret_affinity affinity) {
    return text_editor_visual_row_number_for_offset(te,
                                                    offset,
                                                    text_editor_caret_affinity_prefers_leading(affinity));
}

static uint32_t text_editor_pick_offset_in_row_for_x(const text_editor_node* te,
                                                     const text_editor_visual_row* row,
                                                     const char* line_text,
                                                     float target_x,
                                                     croft_text_editor_caret_affinity* out_affinity) {
    uint32_t best_offset;
    uint32_t offset;
    float best_distance;
    croft_text_editor_caret_affinity best_affinity;

    if (!row) {
        if (out_affinity) {
            *out_affinity = CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING;
        }
        return 0u;
    }

    best_offset = row->start_offset;
    best_distance = target_x < 0.0f ? -target_x : target_x;
    best_affinity = text_editor_affinity_for_row_offset(row, best_offset);
    for (offset = row->start_offset; offset <= row->end_offset; offset++) {
        float width = line_text
            ? text_editor_visual_row_prefix_width(te, row, line_text, offset)
            : 0.0f;
        float distance = width >= target_x ? (width - target_x) : (target_x - width);

        if (offset == row->start_offset || distance <= best_distance) {
            best_distance = distance;
            best_offset = offset;
            best_affinity = text_editor_affinity_for_row_offset(row, offset);
        }
    }

    if (out_affinity) {
        *out_affinity = best_affinity;
    }
    return best_offset;
}

static uint32_t text_editor_current_line_number(const text_editor_node* te) {
    if (!te) {
        return 1u;
    }
    if (te->selection.position_line_number == 0u) {
        return 1u;
    }
    return te->selection.position_line_number;
}

static void text_editor_visible_line_window(const text_editor_node* te,
                                            const text_editor_layout* layout,
                                            uint32_t* out_first_visible_line,
                                            uint32_t* out_last_visible_line) {
    uint32_t first_visible_line = 1u;
    uint32_t last_visible_line = 1u;
    float doc_top;
    uint32_t visible_lines_per_view;

    if (te && layout && te->line_height > 0.0f) {
        doc_top = -te->scroll_y - layout->text_inset_y;
        if (doc_top > 0.0f) {
            first_visible_line = (uint32_t)(doc_top / te->line_height) + 1u;
        }
        visible_lines_per_view = (uint32_t)(layout->content_height / te->line_height) + 3u;
        last_visible_line = first_visible_line + visible_lines_per_view;
    }

    if (out_first_visible_line) {
        *out_first_visible_line = first_visible_line;
    }
    if (out_last_visible_line) {
        *out_last_visible_line = last_visible_line;
    }
}

static croft_editor_status_snapshot text_editor_status_snapshot_from_node(const text_editor_node* te) {
    croft_editor_status_snapshot snapshot;

    snapshot.line_number = text_editor_current_line_number(te);
    snapshot.column = te ? te->selection.position_column : 1u;
    snapshot.line_count = te ? croft_editor_text_model_line_count(&te->text_model) : 1u;
    if (snapshot.line_count == 0u) {
        snapshot.line_count = 1u;
    }
    snapshot.is_dirty = (te && te->document) ? croft_editor_document_is_dirty(te->document) : 0;
    return snapshot;
}

static void text_editor_refresh_syntax_language(text_editor_node* te) {
    const char* path = NULL;

    if (!te) {
        return;
    }

    if (te->document) {
        path = croft_editor_document_path(te->document);
    }
    te->syntax_language = croft_editor_syntax_language_from_path(path);
}

static uint32_t text_editor_syntax_color(croft_editor_syntax_token_kind kind, uint32_t default_color) {
    switch (kind) {
        case CROFT_EDITOR_SYNTAX_TOKEN_COMMENT:
            return 0x3F6212FF;
        case CROFT_EDITOR_SYNTAX_TOKEN_PROPERTY:
            return 0x1F5F99FF;
        case CROFT_EDITOR_SYNTAX_TOKEN_STRING:
            return 0x9A3412FF;
        case CROFT_EDITOR_SYNTAX_TOKEN_NUMBER:
            return 0x0F766EFF;
        case CROFT_EDITOR_SYNTAX_TOKEN_KEYWORD:
            return 0x7C3AEDFF;
        case CROFT_EDITOR_SYNTAX_TOKEN_TYPE:
            return 0x1D4ED8FF;
        case CROFT_EDITOR_SYNTAX_TOKEN_PUNCTUATION:
            return 0x52606DFF;
        case CROFT_EDITOR_SYNTAX_TOKEN_INVALID:
            return 0xC2410CFF;
        default:
            return default_color;
    }
}

static float text_editor_visual_row_prefix_width(const text_editor_node* te,
                                                 const text_editor_visual_row* row,
                                                 const char* line_text,
                                                 uint32_t offset) {
    uint32_t target_byte;

    if (!te || !row || !line_text) {
        return 0.0f;
    }

    if (offset < row->start_offset) {
        offset = row->start_offset;
    }
    if (offset > row->end_offset) {
        offset = row->end_offset;
    }

    target_byte = croft_editor_text_model_byte_offset_at(&te->text_model, offset) - row->line_start_byte;
    if (target_byte < row->start_byte) {
        target_byte = row->start_byte;
    }

    return text_editor_measure_text(te,
                                    line_text + row->start_byte,
                                    target_byte - row->start_byte,
                                    te->font_size);
}

static void text_editor_draw_visual_row_text_segment(const text_editor_node* te,
                                                     const text_editor_layout* layout,
                                                     const text_editor_visual_row* row,
                                                     const char* line_text,
                                                     uint32_t segment_start_offset,
                                                     uint32_t segment_end_offset,
                                                     float current_y,
                                                     uint32_t color_rgba) {
    uint32_t segment_start_byte;
    uint32_t segment_end_byte;
    float x;

    if (!te || !layout || !row || !line_text || segment_end_offset <= segment_start_offset) {
        return;
    }

    if (segment_start_offset < row->start_offset) {
        segment_start_offset = row->start_offset;
    }
    if (segment_end_offset > row->end_offset) {
        segment_end_offset = row->end_offset;
    }

    segment_start_byte =
        croft_editor_text_model_byte_offset_at(&te->text_model, segment_start_offset) - row->line_start_byte;
    segment_end_byte =
        croft_editor_text_model_byte_offset_at(&te->text_model, segment_end_offset) - row->line_start_byte;
    if (segment_end_byte <= segment_start_byte) {
        return;
    }

    x = layout->text_inset_x
        + text_editor_visual_row_prefix_width(te, row, line_text, segment_start_offset);
    host_render_draw_text(x,
                          current_y,
                          line_text + segment_start_byte,
                          segment_end_byte - segment_start_byte,
                          te->font_size,
                          color_rgba);
}

static void text_editor_draw_visual_row_text(const text_editor_node* te,
                                             const text_editor_layout* layout,
                                             const text_editor_visual_row* row,
                                             const char* line_text,
                                             float current_y,
                                             uint32_t default_color) {
    const croft_editor_syntax_token* tokens = NULL;
    uint32_t token_count = 0u;
    uint32_t token_index = 0u;
    uint32_t draw_offset;

    if (!te || !layout || !row || !line_text || row->end_offset <= row->start_offset) {
        return;
    }

    draw_offset = row->start_offset;
    if (croft_editor_line_cache_tokens_for_line(&((text_editor_node*)te)->line_cache,
                                                &te->text_model,
                                                row->model_line_number,
                                                &tokens,
                                                &token_count) != CROFT_EDITOR_OK
            || token_count == 0u) {
        text_editor_draw_visual_row_text_segment(te,
                                                 layout,
                                                 row,
                                                 line_text,
                                                 row->start_offset,
                                                 row->end_offset,
                                                 current_y,
                                                 default_color);
        return;
    }

    while (token_index < token_count && draw_offset < row->end_offset) {
        const croft_editor_syntax_token* token = &tokens[token_index++];
        uint32_t overlap_start;
        uint32_t overlap_end;

        if (token->end_offset <= row->start_offset) {
            continue;
        }
        if (token->start_offset >= row->end_offset) {
            break;
        }

        overlap_start = token->start_offset > row->start_offset ? token->start_offset : row->start_offset;
        overlap_end = token->end_offset < row->end_offset ? token->end_offset : row->end_offset;
        if (draw_offset < overlap_start) {
            text_editor_draw_visual_row_text_segment(te,
                                                     layout,
                                                     row,
                                                     line_text,
                                                     draw_offset,
                                                     overlap_start,
                                                     current_y,
                                                     default_color);
        }
        if (overlap_end > overlap_start) {
            text_editor_draw_visual_row_text_segment(te,
                                                     layout,
                                                     row,
                                                     line_text,
                                                     overlap_start,
                                                     overlap_end,
                                                     current_y,
                                                     text_editor_syntax_color(token->kind, default_color));
            draw_offset = overlap_end;
        }
    }

    if (draw_offset < row->end_offset) {
        text_editor_draw_visual_row_text_segment(te,
                                                 layout,
                                                 row,
                                                 line_text,
                                                 draw_offset,
                                                 row->end_offset,
                                                 current_y,
                                                 default_color);
    }
}

static uint32_t text_editor_clamp_u32(uint32_t value, uint32_t min_value, uint32_t max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void text_editor_ensure_cursor_visible(text_editor_node* te);

static void text_editor_clear_folds(text_editor_node* te) {
    if (!te) {
        return;
    }
    te->folded_region_count = 0u;
    text_editor_invalidate_visual_layout(te);
}

static int text_editor_find_fold_header_index(const text_editor_node* te,
                                              uint32_t line_number,
                                              uint32_t* out_index) {
    uint32_t index;

    if (!te) {
        return 0;
    }

    for (index = 0u; index < te->folded_region_count; index++) {
        if (te->folded_regions[index].start_line_number == line_number) {
            if (out_index) {
                *out_index = index;
            }
            return 1;
        }
    }

    return 0;
}

static int text_editor_find_fold_containing_index(const text_editor_node* te,
                                                  uint32_t line_number,
                                                  uint32_t* out_index) {
    uint32_t index;

    if (!te) {
        return 0;
    }

    for (index = 0u; index < te->folded_region_count; index++) {
        if (line_number > te->folded_regions[index].start_line_number
                && line_number <= te->folded_regions[index].end_line_number) {
            if (out_index) {
                *out_index = index;
            }
            return 1;
        }
    }

    return 0;
}

static void text_editor_remove_fold_at_index(text_editor_node* te, uint32_t index) {
    uint32_t next;

    if (!te || index >= te->folded_region_count) {
        return;
    }

    for (next = index + 1u; next < te->folded_region_count; next++) {
        te->folded_regions[next - 1u] = te->folded_regions[next];
    }
    te->folded_region_count--;
    text_editor_invalidate_visual_layout(te);
}

static int text_editor_fold_region_from_model(const text_editor_node* te,
                                              uint32_t line_number,
                                              croft_editor_fold_region* out_region) {
    if (!te || !out_region) {
        return 0;
    }
    return croft_editor_line_cache_fold_region_for_line(&((text_editor_node*)te)->line_cache,
                                                        &te->text_model,
                                                        line_number,
                                                        out_region) == CROFT_EDITOR_OK;
}

static int text_editor_line_is_hidden(const text_editor_node* te, uint32_t line_number) {
    return text_editor_find_fold_containing_index(te, line_number, NULL);
}

static uint32_t text_editor_visible_line_count(const text_editor_node* te) {
    croft_text_editor_profile_snapshot* stats = text_editor_profile_stats_mut(te);
    uint64_t start_usec = text_editor_profile_begin(te);
    uint32_t visible_count = 1u;

    if (!te) {
        return 1u;
    }
    if (text_editor_prepare_layout((text_editor_node*)te, te->base.sx, te->base.sy, &(text_editor_layout){0})
            == CROFT_EDITOR_OK
            && te->visual_row_count > 0u) {
        visible_count = te->visual_row_count;
    }

    if (stats) {
        stats->visible_line_count_steps += visible_count;
        text_editor_profile_note(stats,
                                 &stats->visible_line_count_calls,
                                 &stats->visible_line_count_total_usec,
                                 start_usec);
    }
    return visible_count;
}

static uint32_t text_editor_visible_line_number_for_model_line(const text_editor_node* te,
                                                               uint32_t model_line_number) {
    croft_text_editor_profile_snapshot* stats = text_editor_profile_stats_mut(te);
    uint64_t start_usec = text_editor_profile_begin(te);
    uint32_t visible_line_number = 0u;

    if (!te || model_line_number == 0u) {
        return 0u;
    }
    if (text_editor_prepare_layout((text_editor_node*)te, te->base.sx, te->base.sy, &(text_editor_layout){0})
            == CROFT_EDITOR_OK
            && model_line_number <= te->line_visible_row_capacity) {
        visible_line_number = te->line_first_visible_rows[model_line_number - 1u];
    }

    if (stats) {
        stats->visible_line_lookup_steps += visible_line_number > 0u ? 1u : 0u;
        text_editor_profile_note(stats,
                                 &stats->visible_line_lookup_calls,
                                 &stats->visible_line_lookup_total_usec,
                                 start_usec);
    }
    return 0u;
}

static uint32_t text_editor_model_line_number_for_visible_line(const text_editor_node* te,
                                                               uint32_t visible_line_number) {
    croft_text_editor_profile_snapshot* stats = text_editor_profile_stats_mut(te);
    uint64_t start_usec = text_editor_profile_begin(te);
    const text_editor_visual_row* row = NULL;
    uint32_t line_number = 1u;

    if (!te || visible_line_number == 0u) {
        return 1u;
    }
    if (text_editor_prepare_layout((text_editor_node*)te, te->base.sx, te->base.sy, &(text_editor_layout){0})
            == CROFT_EDITOR_OK) {
        if (visible_line_number > te->visual_row_count) {
            visible_line_number = te->visual_row_count;
        }
        row = text_editor_visual_row_at_visible_line(te, visible_line_number);
        if (row) {
            line_number = row->model_line_number;
        }
    }

    if (stats) {
        stats->model_line_lookup_steps += row ? 1u : 0u;
        text_editor_profile_note(stats,
                                 &stats->model_line_lookup_calls,
                                 &stats->model_line_lookup_total_usec,
                                 start_usec);
    }
    return line_number;
}

static int32_t text_editor_fold_line(text_editor_node* te, uint32_t line_number) {
    croft_editor_fold_region region = {0};
    uint32_t insert_index;

    if (!te) {
        return -1;
    }
    if (text_editor_find_fold_header_index(te, line_number, NULL)
            || text_editor_line_is_hidden(te, line_number)
            || !text_editor_fold_region_from_model(te, line_number, &region)) {
        return 0;
    }
    if (te->folded_region_count >= (sizeof(te->folded_regions) / sizeof(te->folded_regions[0]))) {
        return -1;
    }

    insert_index = te->folded_region_count;
    while (insert_index > 0u
            && te->folded_regions[insert_index - 1u].start_line_number > region.start_line_number) {
        te->folded_regions[insert_index] = te->folded_regions[insert_index - 1u];
        insert_index--;
    }
    te->folded_regions[insert_index].start_line_number = region.start_line_number;
    te->folded_regions[insert_index].end_line_number = region.end_line_number;
    te->folded_region_count++;
    text_editor_invalidate_visual_layout(te);
    text_editor_ensure_cursor_visible(te);
    return 0;
}

static int32_t text_editor_unfold_line(text_editor_node* te, uint32_t line_number) {
    uint32_t fold_index = 0u;

    if (!te) {
        return -1;
    }
    if (text_editor_find_fold_header_index(te, line_number, &fold_index)
            || text_editor_find_fold_containing_index(te, line_number, &fold_index)) {
        text_editor_remove_fold_at_index(te, fold_index);
    }
    text_editor_ensure_cursor_visible(te);
    return 0;
}

static int32_t text_editor_toggle_fold_line(text_editor_node* te, uint32_t line_number) {
    if (!te) {
        return -1;
    }
    if (text_editor_find_fold_header_index(te, line_number, NULL)) {
        return text_editor_unfold_line(te, line_number);
    }
    return text_editor_fold_line(te, line_number);
}

static void text_editor_normalize_decorations(text_editor_node* te) {
    uint32_t max_offset;
    uint32_t read_index;
    uint32_t write_index = 0u;

    if (!te || te->decoration_count == 0u || !te->decorations) {
        return;
    }

    max_offset = croft_editor_text_model_codepoint_length(&te->text_model);
    for (read_index = 0u; read_index < te->decoration_count; read_index++) {
        croft_text_editor_decoration decoration = te->decorations[read_index];

        if (decoration.end_offset < decoration.start_offset) {
            uint32_t tmp = decoration.start_offset;
            decoration.start_offset = decoration.end_offset;
            decoration.end_offset = tmp;
        }
        if (decoration.start_offset > max_offset) {
            decoration.start_offset = max_offset;
        }
        if (decoration.end_offset > max_offset) {
            decoration.end_offset = max_offset;
        }
        if (decoration.end_offset <= decoration.start_offset) {
            continue;
        }
        if (decoration.style != CROFT_TEXT_EDITOR_DECORATION_STYLE_UNDERLINE) {
            decoration.style = CROFT_TEXT_EDITOR_DECORATION_STYLE_BACKGROUND;
        }
        te->decorations[write_index++] = decoration;
    }

    te->decoration_count = write_index;
}

static void text_editor_sync_selection(text_editor_node *te) {
    uint32_t max_offset = croft_editor_text_model_codepoint_length(&te->text_model);
    uint32_t fold_index = 0u;

    if (te->sel_start > max_offset) {
        te->sel_start = max_offset;
    }
    if (te->sel_end > max_offset) {
        te->sel_end = max_offset;
    }

    te->selection = croft_editor_selection_from_offsets(&te->text_model, te->sel_start, te->sel_end);
    te->caret_affinity = text_editor_normalize_caret_affinity(te->caret_affinity);
    text_editor_normalize_decorations(te);
    while (text_editor_find_fold_containing_index(te, te->selection.position_line_number, &fold_index)) {
        text_editor_remove_fold_at_index(te, fold_index);
    }
}

static Text* text_editor_live_text(text_editor_node* te) {
    if (!te) {
        return NULL;
    }

    if (te->document) {
        te->env = croft_editor_document_env(te->document);
        te->text_tree = croft_editor_document_text(te->document);
    }

    return te->text_tree;
}

static void text_editor_refresh_cache(text_editor_node *te) {
    te->utf8_cache = te->text_model.utf8;
    te->utf8_len = croft_editor_text_model_length(&te->text_model);
    text_editor_invalidate_visual_layout(te);
    text_editor_sync_selection(te);
}

static void text_editor_sync_cache(text_editor_node *te) {
    croft_editor_text_model next_model;
    croft_editor_tab_settings tab_settings;
    int32_t sync_rc = CROFT_EDITOR_OK;

    croft_editor_text_model_init(&next_model);
    croft_editor_tab_settings_default(&tab_settings);
    te->text_tree = text_editor_live_text(te);
    text_editor_refresh_syntax_language(te);
    text_editor_clear_folds(te);
    text_editor_clear_composition_state(te);

    if (!te->text_tree) {
        sync_rc = croft_editor_line_cache_sync(&te->line_cache,
                                               &te->text_model,
                                               &next_model,
                                               te->syntax_language,
                                               &tab_settings);
        if (sync_rc != CROFT_EDITOR_OK) {
            croft_editor_text_model_dispose(&next_model);
            return;
        }
        croft_editor_text_model_dispose(&te->text_model);
        te->text_model = next_model;
        te->utf8_cache = NULL;
        te->utf8_len = 0;
        te->sel_start = 0;
        te->sel_end = 0;
        te->caret_affinity = CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING;
        te->preferred_column = 0;
        te->preferred_visual_x = 0.0f;
        te->selection = croft_editor_selection_create(
            croft_editor_position_create(1, 1),
            croft_editor_position_create(1, 1)
        );
        text_editor_normalize_decorations(te);
        text_editor_invalidate_visual_layout(te);
        return;
    }

    {
        size_t utf8_len_out = 0;
        char* new_cache = NULL;

        if (text_utf8_length((Text*)te->text_tree, &utf8_len_out) != 0) {
            return;
        }

        new_cache = (char*)malloc(utf8_len_out + 1u);
        if (!new_cache) {
            return;
        }
        if (text_to_utf8((Text*)te->text_tree,
                         (uint8_t*)new_cache,
                         utf8_len_out + 1u,
                         &utf8_len_out) != 0) {
            free(new_cache);
            croft_editor_text_model_dispose(&next_model);
            return;
        }
        new_cache[utf8_len_out] = '\0';
        if (croft_editor_text_model_set_text(&next_model, new_cache, utf8_len_out) == CROFT_EDITOR_OK) {
            sync_rc = croft_editor_line_cache_sync(&te->line_cache,
                                                   &te->text_model,
                                                   &next_model,
                                                   te->syntax_language,
                                                   &tab_settings);
        } else {
            sync_rc = CROFT_EDITOR_ERR_OOM;
        }
        if (sync_rc == CROFT_EDITOR_OK) {
            croft_editor_text_model_dispose(&te->text_model);
            te->text_model = next_model;
            text_editor_refresh_cache(te);
        } else {
            croft_editor_text_model_dispose(&next_model);
        }
        free(new_cache);
    }
}

static void text_editor_set_selection_with_affinity(text_editor_node* te,
                                                    uint32_t anchor_offset,
                                                    uint32_t active_offset,
                                                    croft_text_editor_caret_affinity affinity,
                                                    int reset_preferred_column) {
    te->sel_start = anchor_offset;
    te->sel_end = active_offset;
    te->caret_affinity = text_editor_normalize_caret_affinity(affinity);
    if (reset_preferred_column) {
        te->preferred_column = 0;
        te->preferred_visual_x = 0.0f;
    }
    text_editor_sync_selection(te);
}

static void text_editor_set_selection(text_editor_node* te,
                                      uint32_t anchor_offset,
                                      uint32_t active_offset,
                                      int reset_preferred_column) {
    text_editor_set_selection_with_affinity(te,
                                            anchor_offset,
                                            active_offset,
                                            CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING,
                                            reset_preferred_column);
}

static void text_editor_collapse_selection_with_affinity(text_editor_node* te,
                                                         uint32_t offset,
                                                         croft_text_editor_caret_affinity affinity,
                                                         int preserve_preferred_column) {
    te->sel_start = offset;
    te->sel_end = offset;
    te->caret_affinity = text_editor_normalize_caret_affinity(affinity);
    if (!preserve_preferred_column) {
        te->preferred_column = 0;
        te->preferred_visual_x = 0.0f;
    }
    text_editor_sync_selection(te);
}

static void text_editor_collapse_selection(text_editor_node* te,
                                           uint32_t offset,
                                           int preserve_preferred_column) {
    text_editor_collapse_selection_with_affinity(te,
                                                 offset,
                                                 CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING,
                                                 preserve_preferred_column);
}

static void text_editor_selection_bounds(const text_editor_node* te,
                                         uint32_t* out_min,
                                         uint32_t* out_max) {
    uint32_t start = te->sel_start;
    uint32_t end = te->sel_end;
    if (start <= end) {
        if (out_min) {
            *out_min = start;
        }
        if (out_max) {
            *out_max = end;
        }
    } else {
        if (out_min) {
            *out_min = end;
        }
        if (out_max) {
            *out_max = start;
        }
    }
}

static void text_editor_delete_range(SapTxnCtx* txn,
                                     text_editor_node* te,
                                     uint32_t start_offset,
                                     uint32_t end_offset) {
    uint32_t count = end_offset - start_offset;
    uint32_t i;

    for (i = 0; i < count; i++) {
        text_delete(txn, te->text_tree, start_offset, NULL);
    }
}

static void text_editor_break_coalescing(text_editor_node* te) {
    if (te && te->document) {
        croft_editor_document_break_coalescing(te->document);
    }
}

static int32_t text_editor_reserve_composition_buffer(text_editor_node* te, uint32_t capacity) {
    char* resized;

    if (!te) {
        return CROFT_EDITOR_ERR_INVALID;
    }
    if (capacity <= te->composition_capacity) {
        return CROFT_EDITOR_OK;
    }

    resized = (char*)realloc(te->composition_utf8, (size_t)capacity);
    if (!resized) {
        return CROFT_EDITOR_ERR_OOM;
    }

    te->composition_utf8 = resized;
    te->composition_capacity = capacity;
    return CROFT_EDITOR_OK;
}

static void text_editor_clear_composition_state(text_editor_node* te) {
    if (!te) {
        return;
    }

    te->composition_len = 0u;
    te->composition_selection_start = 0u;
    te->composition_selection_end = 0u;
    if (te->composition_utf8 && te->composition_capacity > 0u) {
        te->composition_utf8[0] = '\0';
    }
}

static int text_editor_utf8_codepoint_count(const uint8_t* utf8,
                                            size_t utf8_len,
                                            uint32_t* count_out) {
    size_t off = 0u;
    uint32_t count = 0u;

    if (!count_out) {
        return -1;
    }

    while (utf8 && off < utf8_len) {
        unsigned char ch = utf8[off];
        if ((ch & 0x80u) == 0u) off += 1u;
        else if ((ch & 0xE0u) == 0xC0u) off += 2u;
        else if ((ch & 0xF0u) == 0xE0u) off += 3u;
        else if ((ch & 0xF8u) == 0xF0u) off += 4u;
        else return -1;
        if (off > utf8_len) {
            return -1;
        }
        count++;
    }

    *count_out = count;
    return 0;
}

static int text_editor_utf8_decode_codepoint(const uint8_t* utf8,
                                             size_t utf8_len,
                                             size_t* consumed_out,
                                             uint32_t* codepoint_out)
{
    uint8_t b0;

    if (!utf8 || utf8_len == 0u || !consumed_out || !codepoint_out) {
        return -1;
    }

    b0 = utf8[0];
    if ((b0 & 0x80u) == 0u) {
        *consumed_out = 1u;
        *codepoint_out = (uint32_t)b0;
        return 0;
    }
    if ((b0 & 0xE0u) == 0xC0u) {
        if (utf8_len < 2u || (utf8[1] & 0xC0u) != 0x80u) {
            return -1;
        }
        *consumed_out = 2u;
        *codepoint_out = ((uint32_t)(b0 & 0x1Fu) << 6) | (uint32_t)(utf8[1] & 0x3Fu);
        return 0;
    }
    if ((b0 & 0xF0u) == 0xE0u) {
        if (utf8_len < 3u
                || (utf8[1] & 0xC0u) != 0x80u
                || (utf8[2] & 0xC0u) != 0x80u) {
            return -1;
        }
        *consumed_out = 3u;
        *codepoint_out = ((uint32_t)(b0 & 0x0Fu) << 12)
            | ((uint32_t)(utf8[1] & 0x3Fu) << 6)
            | (uint32_t)(utf8[2] & 0x3Fu);
        return 0;
    }
    if ((b0 & 0xF8u) == 0xF0u) {
        if (utf8_len < 4u
                || (utf8[1] & 0xC0u) != 0x80u
                || (utf8[2] & 0xC0u) != 0x80u
                || (utf8[3] & 0xC0u) != 0x80u) {
            return -1;
        }
        *consumed_out = 4u;
        *codepoint_out = ((uint32_t)(b0 & 0x07u) << 18)
            | ((uint32_t)(utf8[1] & 0x3Fu) << 12)
            | ((uint32_t)(utf8[2] & 0x3Fu) << 6)
            | (uint32_t)(utf8[3] & 0x3Fu);
        return 0;
    }

    return -1;
}

static int text_editor_utf8_encode_codepoint(uint32_t codepoint, char out_utf8[4], uint32_t* out_len)
{
    if (!out_utf8 || !out_len) {
        return -1;
    }
    if (codepoint <= 0x7Fu) {
        out_utf8[0] = (char)codepoint;
        *out_len = 1u;
        return 0;
    }
    if (codepoint <= 0x7FFu) {
        out_utf8[0] = (char)(0xC0u | ((codepoint >> 6) & 0x1Fu));
        out_utf8[1] = (char)(0x80u | (codepoint & 0x3Fu));
        *out_len = 2u;
        return 0;
    }
    if (codepoint <= 0xFFFFu && !(codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
        out_utf8[0] = (char)(0xE0u | ((codepoint >> 12) & 0x0Fu));
        out_utf8[1] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        out_utf8[2] = (char)(0x80u | (codepoint & 0x3Fu));
        *out_len = 3u;
        return 0;
    }
    if (codepoint <= 0x10FFFFu) {
        out_utf8[0] = (char)(0xF0u | ((codepoint >> 18) & 0x07u));
        out_utf8[1] = (char)(0x80u | ((codepoint >> 12) & 0x3Fu));
        out_utf8[2] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        out_utf8[3] = (char)(0x80u | (codepoint & 0x3Fu));
        *out_len = 4u;
        return 0;
    }
    return -1;
}

static int text_editor_replace_range_utf8(text_editor_node* te,
                                          uint32_t start_offset,
                                          uint32_t end_offset,
                                          const uint8_t* utf8,
                                          size_t utf8_len,
                                          croft_editor_document_edit_kind edit_kind)
{
    if (!te) {
        return -1;
    }

    if (te->document) {
        return croft_editor_document_replace_range_with_utf8(te->document,
                                                             start_offset,
                                                             end_offset,
                                                             utf8,
                                                             utf8_len,
                                                             edit_kind) == 0
            ? 0
            : -1;
    }

    {
        SapTxnCtx* txn = sap_txn_begin(te->env, NULL, 0);
        uint32_t insert_offset = start_offset;
        size_t consumed_total = 0u;

        if (!txn) {
            return -1;
        }

        text_editor_delete_range(txn, te, start_offset, end_offset);
        while (utf8 && consumed_total < utf8_len) {
            size_t consumed = 0u;
            uint32_t codepoint = 0u;

            if (text_editor_utf8_decode_codepoint(utf8 + consumed_total,
                                                  utf8_len - consumed_total,
                                                  &consumed,
                                                  &codepoint) != 0
                    || text_insert(txn, te->text_tree, insert_offset, codepoint) != 0) {
                sap_txn_abort(txn);
                return -1;
            }
            consumed_total += consumed;
            insert_offset++;
        }

        sap_txn_commit(txn);
    }

    return 0;
}

static int32_t text_editor_apply_tab_edit(text_editor_node* te, int outdent)
{
    croft_editor_tab_settings settings;
    croft_editor_tab_edit edit = {0};
    croft_editor_document_edit_kind edit_kind;
    int32_t rc = 0;

    if (!te) {
        return -1;
    }

    croft_editor_tab_settings_default(&settings);
    if (!croft_editor_command_build_tab_edit(&te->text_model,
                                             te->sel_start,
                                             te->sel_end,
                                             &settings,
                                             outdent,
                                             &edit)) {
        return 0;
    }

    edit_kind = (edit.replace_start_offset == edit.replace_end_offset)
        ? CROFT_EDITOR_EDIT_INSERT
        : CROFT_EDITOR_EDIT_REPLACE_ALL;
    if (te->document && edit_kind == CROFT_EDITOR_EDIT_REPLACE_ALL) {
        text_editor_break_coalescing(te);
    }
    if (text_editor_replace_range_utf8(te,
                                       edit.replace_start_offset,
                                       edit.replace_end_offset,
                                       (const uint8_t*)edit.replacement_utf8,
                                       edit.replacement_utf8_len,
                                       edit_kind) != 0) {
        rc = -1;
    } else {
        text_editor_set_selection(te, edit.next_anchor_offset, edit.next_active_offset, 0);
        text_editor_sync_cache(te);
    }

    croft_editor_tab_edit_dispose(&edit);
    return rc;
}

enum {
    TEXT_EDITOR_SEARCH_FIELD_FIND = 0u,
    TEXT_EDITOR_SEARCH_FIELD_REPLACE = 1u
};

static char* text_editor_search_query_buffer(text_editor_node* te, uint32_t field)
{
    if (!te) {
        return NULL;
    }
    return field == TEXT_EDITOR_SEARCH_FIELD_REPLACE ? te->replace_query : te->find_query;
}

static const char* text_editor_search_query_buffer_const(const text_editor_node* te, uint32_t field)
{
    if (!te) {
        return NULL;
    }
    return field == TEXT_EDITOR_SEARCH_FIELD_REPLACE ? te->replace_query : te->find_query;
}

static uint32_t* text_editor_search_query_length_mut(text_editor_node* te, uint32_t field)
{
    if (!te) {
        return NULL;
    }
    return field == TEXT_EDITOR_SEARCH_FIELD_REPLACE ? &te->replace_query_len : &te->find_query_len;
}

static uint32_t text_editor_search_query_length(const text_editor_node* te, uint32_t field)
{
    if (!te) {
        return 0u;
    }
    return field == TEXT_EDITOR_SEARCH_FIELD_REPLACE ? te->replace_query_len : te->find_query_len;
}

static void text_editor_search_query_clear(text_editor_node* te, uint32_t field)
{
    char* buffer = text_editor_search_query_buffer(te, field);
    uint32_t* length = text_editor_search_query_length_mut(te, field);

    if (!buffer || !length) {
        return;
    }

    buffer[0] = '\0';
    *length = 0u;
}

static void text_editor_find_query_clear(text_editor_node* te)
{
    text_editor_search_query_clear(te, TEXT_EDITOR_SEARCH_FIELD_FIND);
}

static int text_editor_find_query_is_single_line(const char* utf8, uint32_t utf8_len)
{
    uint32_t index;

    if (!utf8 || utf8_len == 0u) {
        return 0;
    }

    for (index = 0u; index < utf8_len; index++) {
        if (utf8[index] == '\n' || utf8[index] == '\r') {
            return 0;
        }
    }
    return 1;
}

static int text_editor_find_set_query_utf8(text_editor_node* te,
                                           const char* utf8,
                                           uint32_t utf8_len)
{
    char* buffer = text_editor_search_query_buffer(te, TEXT_EDITOR_SEARCH_FIELD_FIND);
    uint32_t* length = text_editor_search_query_length_mut(te, TEXT_EDITOR_SEARCH_FIELD_FIND);

    if (!buffer || !length || (!utf8 && utf8_len > 0u) || utf8_len >= sizeof(te->find_query)) {
        return 0;
    }

    if (utf8_len > 0u && !text_editor_find_query_is_single_line(utf8, utf8_len)) {
        return 0;
    }

    if (utf8_len > 0u) {
        memcpy(buffer, utf8, utf8_len);
    }
    buffer[utf8_len] = '\0';
    *length = utf8_len;
    return 1;
}

static int text_editor_replace_set_query_utf8(text_editor_node* te,
                                              const char* utf8,
                                              uint32_t utf8_len)
{
    char* buffer = text_editor_search_query_buffer(te, TEXT_EDITOR_SEARCH_FIELD_REPLACE);
    uint32_t* length = text_editor_search_query_length_mut(te, TEXT_EDITOR_SEARCH_FIELD_REPLACE);

    if (!buffer || !length || (!utf8 && utf8_len > 0u) || utf8_len >= sizeof(te->replace_query)) {
        return 0;
    }
    if (utf8_len > 0u && !text_editor_find_query_is_single_line(utf8, utf8_len)) {
        return 0;
    }

    if (utf8_len > 0u) {
        memcpy(buffer, utf8, utf8_len);
    }
    buffer[utf8_len] = '\0';
    *length = utf8_len;
    return 1;
}

static int text_editor_copy_selection_into_find_query(text_editor_node* te)
{
    uint32_t selection_min = 0u;
    uint32_t selection_max = 0u;
    uint32_t start_byte;
    uint32_t end_byte;
    uint32_t len;

    if (!te) {
        return 0;
    }

    text_editor_selection_bounds(te, &selection_min, &selection_max);
    if (selection_min == selection_max) {
        return 0;
    }

    start_byte = croft_editor_text_model_byte_offset_at(&te->text_model, selection_min);
    end_byte = croft_editor_text_model_byte_offset_at(&te->text_model, selection_max);
    len = end_byte - start_byte;
    if (len == 0u || len >= sizeof(te->find_query)) {
        return 0;
    }
    if (!text_editor_find_query_is_single_line(te->text_model.utf8 + start_byte, len)) {
        return 0;
    }

    return text_editor_find_set_query_utf8(te, te->text_model.utf8 + start_byte, len);
}

static int text_editor_find_has_query(const text_editor_node* te)
{
    return te && te->find_query_len > 0u;
}

static uint32_t text_editor_active_search_field(const text_editor_node* te)
{
    if (!te) {
        return TEXT_EDITOR_SEARCH_FIELD_FIND;
    }
    if (te->replace_active && te->search_focus_field == TEXT_EDITOR_SEARCH_FIELD_REPLACE) {
        return TEXT_EDITOR_SEARCH_FIELD_REPLACE;
    }
    return TEXT_EDITOR_SEARCH_FIELD_FIND;
}

static int text_editor_selection_matches_query(const text_editor_node* te)
{
    uint32_t selection_min = 0u;
    uint32_t selection_max = 0u;
    uint32_t start_byte;
    uint32_t end_byte;
    uint32_t len;

    if (!te || !text_editor_find_has_query(te)) {
        return 0;
    }

    text_editor_selection_bounds(te, &selection_min, &selection_max);
    if (selection_min == selection_max) {
        return 0;
    }

    start_byte = croft_editor_text_model_byte_offset_at(&te->text_model, selection_min);
    end_byte = croft_editor_text_model_byte_offset_at(&te->text_model, selection_max);
    len = end_byte - start_byte;
    if (len != te->find_query_len) {
        return 0;
    }

    return memcmp(te->text_model.utf8 + start_byte, te->find_query, len) == 0;
}

static void text_editor_ensure_cursor_visible(text_editor_node* te)
{
    croft_text_editor_profile_snapshot* stats = text_editor_profile_stats_mut(te);
    uint64_t start_usec = text_editor_profile_begin(te);
    text_editor_layout layout;
    const text_editor_visual_row* row;
    uint32_t visible_line_number;
    float line_top;
    float visible_top;
    float visible_bottom;
    float cursor_doc_x;
    float visible_left;
    float visible_right;
    uint32_t line_len_bytes = 0u;
    const char* line_text = NULL;

    if (!te) {
        return;
    }

    if (text_editor_prepare_layout(te, te->base.sx, te->base.sy, &layout) != CROFT_EDITOR_OK) {
        return;
    }

    visible_line_number = text_editor_visual_row_number_for_affinity(te,
                                                                     te->sel_end,
                                                                     te->caret_affinity);
    if (visible_line_number == 0u) {
        visible_line_number = 1u;
    }
    line_top = text_editor_row_top(te, visible_line_number);
    visible_top = -te->scroll_y;
    visible_bottom = visible_top + (layout.content_height - layout.text_inset_y - te->line_height);
    if (line_top < visible_top) {
        te->scroll_y = -line_top;
    } else if (line_top > visible_bottom) {
        te->scroll_y = -(line_top - (layout.content_height - layout.text_inset_y - te->line_height));
    }
    if (te->scroll_y > 0.0f) {
        te->scroll_y = 0.0f;
    }

    row = text_editor_visual_row_at_visible_line(te, visible_line_number);
    if (row && croft_editor_text_model_line_count(&te->text_model) > 0u) {
        line_text = croft_editor_text_model_line_utf8(&te->text_model,
                                                      row->model_line_number,
                                                      &line_len_bytes);
    }

    if (te->wrap_enabled) {
        te->scroll_x = 0.0f;
    } else if (row && line_text) {
        cursor_doc_x =
            layout.text_inset_x + text_editor_visual_row_prefix_width(te, row, line_text, te->sel_end);
        visible_left = -te->scroll_x;
        visible_right = visible_left + layout.content_width;
        if (cursor_doc_x < visible_left + layout.text_inset_x) {
            te->scroll_x = -(cursor_doc_x - layout.text_inset_x);
        } else if (cursor_doc_x > visible_right - 24.0f) {
            te->scroll_x = -(cursor_doc_x - (layout.content_width - 24.0f));
        }
    }
    if (te->scroll_x > 0.0f) {
        te->scroll_x = 0.0f;
    }

    if (stats) {
        text_editor_profile_note(stats,
                                 &stats->ensure_cursor_visible_calls,
                                 &stats->ensure_cursor_visible_total_usec,
                                 start_usec);
    }
}

static int32_t text_editor_apply_search_match(text_editor_node* te,
                                              const croft_editor_search_match* match)
{
    if (!te || !match) {
        return -1;
    }

    text_editor_set_selection_with_affinity(te,
                                            match->start_offset,
                                            match->end_offset,
                                            CROFT_TEXT_EDITOR_CARET_AFFINITY_TRAILING,
                                            1);
    text_editor_ensure_cursor_visible(te);
    return 0;
}

static int32_t text_editor_find_next_from(text_editor_node* te,
                                          uint32_t start_offset,
                                          int allow_wrap)
{
    croft_editor_search_match match = {0};

    if (!te || !text_editor_find_has_query(te)) {
        return -1;
    }

    if (croft_editor_search_next(&te->text_model,
                                 te->find_query,
                                 te->find_query_len,
                                 start_offset,
                                 &match) == CROFT_EDITOR_OK) {
        return text_editor_apply_search_match(te, &match);
    }

    if (allow_wrap
            && croft_editor_search_next(&te->text_model,
                                        te->find_query,
                                        te->find_query_len,
                                        0u,
                                        &match) == CROFT_EDITOR_OK) {
        return text_editor_apply_search_match(te, &match);
    }

    return -1;
}

static int32_t text_editor_find_previous_before(text_editor_node* te,
                                                uint32_t before_offset,
                                                int allow_wrap)
{
    croft_editor_search_match match = {0};

    if (!te || !text_editor_find_has_query(te)) {
        return -1;
    }

    if (croft_editor_search_previous(&te->text_model,
                                     te->find_query,
                                     te->find_query_len,
                                     before_offset,
                                     &match) == CROFT_EDITOR_OK) {
        return text_editor_apply_search_match(te, &match);
    }

    if (allow_wrap
            && croft_editor_search_previous(&te->text_model,
                                            te->find_query,
                                            te->find_query_len,
                                            croft_editor_text_model_codepoint_length(&te->text_model),
                                            &match) == CROFT_EDITOR_OK) {
        return text_editor_apply_search_match(te, &match);
    }

    return -1;
}

static void text_editor_find_refresh_from_cursor(text_editor_node* te)
{
    uint32_t selection_min = 0u;

    if (!te || !text_editor_find_has_query(te)) {
        return;
    }

    text_editor_selection_bounds(te, &selection_min, NULL);
    text_editor_find_next_from(te, selection_min, 1);
}

static int text_editor_count_matches_before_offset(const text_editor_node* te,
                                                   uint32_t before_offset,
                                                   uint32_t* out_count)
{
    croft_editor_search_match match = {0};
    uint32_t count = 0u;
    uint32_t search_from = 0u;

    if (!te || !out_count || !text_editor_find_has_query(te)) {
        return -1;
    }

    while (croft_editor_search_next(&te->text_model,
                                    te->find_query,
                                    te->find_query_len,
                                    search_from,
                                    &match) == CROFT_EDITOR_OK) {
        if (match.start_offset >= before_offset) {
            break;
        }
        count++;
        search_from = match.end_offset;
    }

    *out_count = count;
    return 0;
}

static void text_editor_find_match_stats(const text_editor_node* te,
                                         uint32_t* out_current_index,
                                         uint32_t* out_total_count)
{
    uint32_t total_count = 0u;
    uint32_t current_index = 0u;
    uint32_t selection_min = 0u;

    if (out_current_index) {
        *out_current_index = 0u;
    }
    if (out_total_count) {
        *out_total_count = 0u;
    }
    if (!te || !text_editor_find_has_query(te)) {
        return;
    }

    if (croft_editor_search_count_matches(&te->text_model,
                                          te->find_query,
                                          te->find_query_len,
                                          &total_count) != CROFT_EDITOR_OK) {
        return;
    }
    if (text_editor_selection_matches_query(te)) {
        text_editor_selection_bounds(te, &selection_min, NULL);
        if (text_editor_count_matches_before_offset(te, selection_min, &current_index) == 0) {
            current_index++;
        }
    }

    if (out_current_index) {
        *out_current_index = current_index;
    }
    if (out_total_count) {
        *out_total_count = total_count;
    }
}

static int text_editor_search_query_backspace(text_editor_node* te, uint32_t field)
{
    char* buffer = text_editor_search_query_buffer(te, field);
    uint32_t* length = text_editor_search_query_length_mut(te, field);
    uint32_t len;

    if (!buffer || !length || *length == 0u) {
        return 0;
    }

    len = *length;
    while (len > 0u) {
        len--;
        if ((((unsigned char)buffer[len]) & 0xC0u) != 0x80u) {
            buffer[len] = '\0';
            *length = len;
            return 1;
        }
    }

    text_editor_search_query_clear(te, field);
    return 1;
}

static int text_editor_find_query_backspace(text_editor_node* te)
{
    return text_editor_search_query_backspace(te, TEXT_EDITOR_SEARCH_FIELD_FIND);
}

static int text_editor_replace_query_backspace(text_editor_node* te)
{
    return text_editor_search_query_backspace(te, TEXT_EDITOR_SEARCH_FIELD_REPLACE);
}

static int text_editor_search_query_append_codepoint(text_editor_node* te,
                                                     uint32_t field,
                                                     uint32_t codepoint)
{
    char* buffer = text_editor_search_query_buffer(te, field);
    uint32_t* length = text_editor_search_query_length_mut(te, field);
    char encoded[4];
    uint32_t encoded_len = 0u;

    if (!buffer || !length || codepoint < 0x20u || codepoint == 0x7Fu) {
        return 0;
    }
    if (text_editor_utf8_encode_codepoint(codepoint, encoded, &encoded_len) != 0) {
        return 0;
    }
    if (*length + encoded_len >= sizeof(te->find_query)) {
        return 0;
    }

    memcpy(buffer + *length, encoded, encoded_len);
    *length += encoded_len;
    buffer[*length] = '\0';
    return 1;
}

static int text_editor_find_query_append_codepoint(text_editor_node* te, uint32_t codepoint)
{
    return text_editor_search_query_append_codepoint(te, TEXT_EDITOR_SEARCH_FIELD_FIND, codepoint);
}

static int text_editor_replace_query_append_codepoint(text_editor_node* te, uint32_t codepoint)
{
    return text_editor_search_query_append_codepoint(te, TEXT_EDITOR_SEARCH_FIELD_REPLACE, codepoint);
}

static int32_t text_editor_replace_selection_with_search_query(text_editor_node* te,
                                                               uint32_t start_offset,
                                                               uint32_t end_offset,
                                                               croft_editor_document_edit_kind edit_kind,
                                                               uint32_t* out_replacement_end)
{
    const char* replacement = text_editor_search_query_buffer_const(te, TEXT_EDITOR_SEARCH_FIELD_REPLACE);
    uint32_t replacement_len = text_editor_search_query_length(te, TEXT_EDITOR_SEARCH_FIELD_REPLACE);
    uint32_t replacement_codepoints = 0u;

    if (!te || !replacement || !out_replacement_end) {
        return -1;
    }
    if (text_editor_utf8_codepoint_count((const uint8_t*)replacement,
                                         replacement_len,
                                         &replacement_codepoints) != 0) {
        return -1;
    }
    if (text_editor_replace_range_utf8(te,
                                       start_offset,
                                       end_offset,
                                       (const uint8_t*)replacement,
                                       replacement_len,
                                       edit_kind) != 0) {
        return -1;
    }

    text_editor_sync_cache(te);
    *out_replacement_end = start_offset + replacement_codepoints;
    text_editor_set_selection_with_affinity(te,
                                            start_offset,
                                            *out_replacement_end,
                                            CROFT_TEXT_EDITOR_CARET_AFFINITY_TRAILING,
                                            1);
    text_editor_ensure_cursor_visible(te);
    return 0;
}

static int32_t text_editor_replace_next_internal(text_editor_node* te)
{
    croft_editor_search_match match = {0};
    uint32_t selection_min = 0u;
    uint32_t selection_max = 0u;
    uint32_t replacement_end = 0u;

    if (!te || !text_editor_find_has_query(te)) {
        return -1;
    }

    text_editor_selection_bounds(te, &selection_min, &selection_max);
    if (text_editor_selection_matches_query(te)) {
        match.start_offset = selection_min;
        match.end_offset = selection_max;
    } else if (croft_editor_search_next(&te->text_model,
                                        te->find_query,
                                        te->find_query_len,
                                        selection_min,
                                        &match) != CROFT_EDITOR_OK
            && croft_editor_search_next(&te->text_model,
                                        te->find_query,
                                        te->find_query_len,
                                        0u,
                                        &match) != CROFT_EDITOR_OK) {
        return -1;
    }

    text_editor_break_coalescing(te);
    if (text_editor_replace_selection_with_search_query(te,
                                                        match.start_offset,
                                                        match.end_offset,
                                                        CROFT_EDITOR_EDIT_INSERT,
                                                        &replacement_end) != 0) {
        return -1;
    }

    if (text_editor_find_next_from(te, replacement_end, 1) != 0) {
        text_editor_set_selection_with_affinity(te,
                                                match.start_offset,
                                                replacement_end,
                                                CROFT_TEXT_EDITOR_CARET_AFFINITY_TRAILING,
                                                1);
        text_editor_ensure_cursor_visible(te);
    }
    return 0;
}

static int32_t text_editor_replace_all_internal(text_editor_node* te)
{
    croft_editor_search_match first_match = {0};
    char* replaced_utf8 = NULL;
    size_t replaced_len = 0u;
    uint32_t match_count = 0u;
    uint32_t replacement_codepoints = 0u;
    int32_t rc;

    if (!te || !text_editor_find_has_query(te)) {
        return -1;
    }
    if (croft_editor_search_next(&te->text_model,
                                 te->find_query,
                                 te->find_query_len,
                                 0u,
                                 &first_match) != CROFT_EDITOR_OK) {
        return -1;
    }

    rc = croft_editor_search_replace_all_utf8(&te->text_model,
                                              te->find_query,
                                              te->find_query_len,
                                              text_editor_search_query_buffer_const(
                                                  te,
                                                  TEXT_EDITOR_SEARCH_FIELD_REPLACE),
                                              text_editor_search_query_length(
                                                  te,
                                                  TEXT_EDITOR_SEARCH_FIELD_REPLACE),
                                              &replaced_utf8,
                                              &replaced_len,
                                              &match_count);
    if (rc != CROFT_EDITOR_OK || match_count == 0u || !replaced_utf8) {
        free(replaced_utf8);
        return -1;
    }

    if (text_editor_utf8_codepoint_count((const uint8_t*)text_editor_search_query_buffer_const(
                                             te,
                                             TEXT_EDITOR_SEARCH_FIELD_REPLACE),
                                         text_editor_search_query_length(
                                             te,
                                             TEXT_EDITOR_SEARCH_FIELD_REPLACE),
                                         &replacement_codepoints) != 0) {
        free(replaced_utf8);
        return -1;
    }

    text_editor_break_coalescing(te);
    rc = text_editor_replace_range_utf8(te,
                                        0u,
                                        croft_editor_text_model_codepoint_length(&te->text_model),
                                        (const uint8_t*)replaced_utf8,
                                        replaced_len,
                                        CROFT_EDITOR_EDIT_REPLACE_ALL);
    if (rc == 0) {
        text_editor_sync_cache(te);
        text_editor_set_selection_with_affinity(te,
                                                first_match.start_offset,
                                                first_match.start_offset + replacement_codepoints,
                                                CROFT_TEXT_EDITOR_CARET_AFFINITY_TRAILING,
                                                1);
        text_editor_ensure_cursor_visible(te);
    }

    free(replaced_utf8);
    return rc;
}

static void text_editor_visual_row_range_bounds(const text_editor_node* te,
                                                const text_editor_layout* layout,
                                                const text_editor_visual_row* row,
                                                const char* line_text,
                                                uint32_t start_offset,
                                                uint32_t end_offset,
                                                float* out_x1,
                                                float* out_x2)
{
    if (!te || !layout || !row || !line_text || !out_x1 || !out_x2) {
        return;
    }

    *out_x1 = layout->text_inset_x
        + text_editor_visual_row_prefix_width(te, row, line_text, start_offset);
    *out_x2 = layout->text_inset_x
        + text_editor_visual_row_prefix_width(te, row, line_text, end_offset);
}

static void text_editor_decoration_buffer_append(text_editor_decoration_buffer* buffer,
                                                 uint32_t start_offset,
                                                 uint32_t end_offset,
                                                 croft_text_editor_decoration_style style,
                                                 uint32_t color_rgba) {
    croft_text_editor_decoration* decoration;

    if (!buffer || buffer->count >= (sizeof(buffer->items) / sizeof(buffer->items[0]))
            || end_offset <= start_offset) {
        return;
    }

    decoration = &buffer->items[buffer->count++];
    decoration->start_offset = start_offset;
    decoration->end_offset = end_offset;
    decoration->style = (uint8_t)style;
    decoration->color_rgba = color_rgba;
}

static void text_editor_draw_range_highlight(const text_editor_node* te,
                                             const text_editor_layout* layout,
                                             uint32_t start_offset,
                                             uint32_t end_offset,
                                             uint32_t color_rgba)
{
    uint32_t visible_line_number;

    if (!te || !layout || start_offset >= end_offset) {
        return;
    }

    for (visible_line_number = 1u; visible_line_number <= te->visual_row_count; visible_line_number++) {
        const text_editor_visual_row* row =
            text_editor_visual_row_at_visible_line(te, visible_line_number);

        if (row
                && start_offset < row->end_offset
                && end_offset > row->start_offset
                && row->model_line_number <= croft_editor_text_model_line_count(&te->text_model)) {
            uint32_t highlight_start = start_offset > row->start_offset ? start_offset : row->start_offset;
            uint32_t highlight_end = end_offset < row->end_offset ? end_offset : row->end_offset;
            uint32_t line_len_bytes = 0u;
            const char* line_text =
                croft_editor_text_model_line_utf8(&te->text_model, row->model_line_number, &line_len_bytes);
            float line_top = text_editor_row_top(te, visible_line_number);
            float current_y = text_editor_row_baseline(te, visible_line_number);
            float x1 = 0.0f;
            float x2 = 0.0f;

            if (line_top + te->line_height + te->scroll_y < 0.0f
                    || line_top + te->scroll_y > layout->content_height) {
                continue;
            }
            text_editor_visual_row_range_bounds(te,
                                                layout,
                                                row,
                                                line_text,
                                                highlight_start,
                                                highlight_end,
                                                &x1,
                                                &x2);
            host_render_draw_rect(x1, line_top, x2 - x1, te->line_height, color_rgba);
        }
    }
}

static void text_editor_draw_range_underline(const text_editor_node* te,
                                             const text_editor_layout* layout,
                                             uint32_t start_offset,
                                             uint32_t end_offset,
                                             uint32_t color_rgba) {
    uint32_t visible_line_number;

    if (!te || !layout || start_offset >= end_offset) {
        return;
    }

    for (visible_line_number = 1u; visible_line_number <= te->visual_row_count; visible_line_number++) {
        const text_editor_visual_row* row =
            text_editor_visual_row_at_visible_line(te, visible_line_number);

        if (row
                && start_offset < row->end_offset
                && end_offset > row->start_offset
                && row->model_line_number <= croft_editor_text_model_line_count(&te->text_model)) {
            uint32_t underline_start = start_offset > row->start_offset ? start_offset : row->start_offset;
            uint32_t underline_end = end_offset < row->end_offset ? end_offset : row->end_offset;
            uint32_t line_len_bytes = 0u;
            const char* line_text =
                croft_editor_text_model_line_utf8(&te->text_model, row->model_line_number, &line_len_bytes);
            float line_top = text_editor_row_top(te, visible_line_number);
            float baseline_y = text_editor_row_baseline(te, visible_line_number);
            float x1 = 0.0f;
            float x2 = 0.0f;
            float underline_y;

            if (line_top + te->line_height + te->scroll_y < 0.0f
                    || line_top + te->scroll_y > layout->content_height) {
                continue;
            }
            text_editor_visual_row_range_bounds(te,
                                                layout,
                                                row,
                                                line_text,
                                                underline_start,
                                                underline_end,
                                                &x1,
                                                &x2);
            underline_y = baseline_y + te->font_probe.descender + 1.0f;
            if (underline_y > line_top + te->line_height - 2.0f) {
                underline_y = line_top + te->line_height - 2.0f;
            }
            if (x2 <= x1) {
                x2 = x1 + 1.0f;
            }
            host_render_draw_rect(x1, underline_y, x2 - x1, 1.5f, color_rgba);
        }
    }
}

static void text_editor_draw_decoration_list(const text_editor_node* te,
                                             const text_editor_layout* layout,
                                             const croft_text_editor_decoration* decorations,
                                             uint32_t decoration_count,
                                             croft_text_editor_decoration_style style) {
    uint32_t index;

    if (!te || !layout || decoration_count == 0u || !decorations) {
        return;
    }

    for (index = 0u; index < decoration_count; index++) {
        const croft_text_editor_decoration* decoration = &decorations[index];

        if ((croft_text_editor_decoration_style)decoration->style != style) {
            continue;
        }
        if (style == CROFT_TEXT_EDITOR_DECORATION_STYLE_UNDERLINE) {
            text_editor_draw_range_underline(te,
                                             layout,
                                             decoration->start_offset,
                                             decoration->end_offset,
                                             decoration->color_rgba);
        } else {
            text_editor_draw_range_highlight(te,
                                             layout,
                                             decoration->start_offset,
                                             decoration->end_offset,
                                             decoration->color_rgba);
        }
    }
}

static void text_editor_draw_decorations(const text_editor_node* te,
                                         const text_editor_layout* layout,
                                         croft_text_editor_decoration_style style) {
    text_editor_draw_decoration_list(te,
                                     layout,
                                     te ? te->decorations : NULL,
                                     te ? te->decoration_count : 0u,
                                     style);
}

static void text_editor_draw_whitespace_marker(const text_editor_node* te,
                                               float x1,
                                               float x2,
                                               float current_y,
                                               croft_editor_visible_whitespace_kind kind)
{
    float line_top;
    float width = x2 - x1;
    float marker_y;
    uint32_t color = 0x99A3AFCC;

    if (!te || width <= 0.0f) {
        return;
    }

    line_top = current_y - te->baseline_offset;
    marker_y = line_top + (te->line_height * 0.5f);
    if (kind == CROFT_EDITOR_VISIBLE_WHITESPACE_SPACE) {
        float dot_x = x1 + (width * 0.5f) - 1.0f;
        host_render_draw_rect(dot_x, marker_y - 1.0f, 2.0f, 2.0f, color);
        return;
    }

    {
        float line_x1 = x1 + 2.0f;
        float line_x2 = x2 - 3.0f;
        if (line_x2 <= line_x1) {
            line_x1 = x1 + 1.0f;
            line_x2 = x2 - 1.0f;
        }
        if (line_x2 <= line_x1) {
            return;
        }

        host_render_draw_rect(line_x1, marker_y, line_x2 - line_x1, 1.0f, color);
        host_render_draw_rect(line_x2 - 2.0f, marker_y - 1.0f, 2.0f, 1.0f, color);
        host_render_draw_rect(line_x2 - 1.0f, marker_y + 1.0f, 1.0f, 1.0f, color);
        host_render_draw_rect(line_x1, marker_y - 1.5f, 1.0f, 3.0f, color);
    }
}

static void text_editor_draw_indent_guides_for_line(const text_editor_node* te,
                                                    const text_editor_layout* layout,
                                                    uint32_t line_number,
                                                    float current_y)
{
    croft_editor_tab_settings settings;
    croft_editor_whitespace_line line = {0};
    croft_editor_visible_whitespace marker = {0};
    uint32_t line_start_byte;
    uint32_t line_len_bytes = 0u;
    const char* line_text;
    uint32_t search_offset;

    if (!te || !layout) {
        return;
    }

    croft_editor_tab_settings_default(&settings);
    if (croft_editor_whitespace_describe_line(&te->text_model, line_number, &settings, &line)
            != CROFT_EDITOR_OK
            || line.indent_guide_count == 0u) {
        return;
    }

    line_start_byte = croft_editor_text_model_byte_offset_at(&te->text_model, line.line_start_offset);
    line_text = croft_editor_text_model_line_utf8(&te->text_model, line_number, &line_len_bytes);
    search_offset = line.line_start_offset;
    while (croft_editor_whitespace_find_in_line(&te->text_model,
                                                line_number,
                                                &settings,
                                                search_offset,
                                                &marker) == CROFT_EDITOR_OK) {
        uint32_t visual_end = marker.visual_column + marker.visual_width - 1u;

        if (visual_end > line.leading_indent_columns) {
            break;
        }
        if ((visual_end % settings.tab_size) == 0u) {
            text_editor_visual_row row = {0};
            float x1 = 0.0f;
            float x2 = 0.0f;
            float width;
            float unit_width;
            float guide_x;

            row.model_line_number = line_number;
            row.line_start_offset = line.line_start_offset;
            row.line_end_offset = line.line_end_offset;
            row.start_offset = line.line_start_offset;
            row.end_offset = line.line_end_offset;
            row.line_start_byte = line_start_byte;
            row.start_byte = 0u;
            row.end_byte = line_len_bytes;
            row.is_first_in_line = 1u;
            row.is_last_in_line = 1u;
            text_editor_visual_row_range_bounds(te,
                                                layout,
                                                &row,
                                                line_text,
                                                marker.offset,
                                                marker.offset + 1u,
                                                &x1,
                                                &x2);
            width = x2 - x1;
            if (width > 0.0f) {
                unit_width = width / (float)marker.visual_width;
                guide_x = x2 - (unit_width * 0.5f) - 0.5f;
                host_render_draw_rect(guide_x,
                                      current_y - te->baseline_offset + 1.0f,
                                      1.0f,
                                      te->line_height - 2.0f,
                                      0xD2D9E2D8);
            }
        }

        search_offset = marker.offset + 1u;
    }
}

static void text_editor_draw_whitespace_markers_for_row(const text_editor_node* te,
                                                        const text_editor_layout* layout,
                                                        const text_editor_visual_row* row,
                                                        float current_y)
{
    croft_editor_tab_settings settings;
    croft_editor_visible_whitespace marker = {0};
    uint32_t line_len_bytes = 0u;
    const char* line_text;
    uint32_t search_offset;

    if (!te || !layout || !row) {
        return;
    }

    croft_editor_tab_settings_default(&settings);
    line_text = croft_editor_text_model_line_utf8(&te->text_model, row->model_line_number, &line_len_bytes);
    search_offset = row->start_offset;
    while (croft_editor_whitespace_find_in_line(&te->text_model,
                                                row->model_line_number,
                                                &settings,
                                                search_offset,
                                                &marker) == CROFT_EDITOR_OK) {
        float x1 = 0.0f;
        float x2 = 0.0f;

        if (marker.offset >= row->end_offset) {
            break;
        }
        if (marker.offset < row->start_offset) {
            search_offset = marker.offset + 1u;
            continue;
        }

        text_editor_visual_row_range_bounds(te,
                                            layout,
                                            row,
                                            line_text,
                                            marker.offset,
                                            marker.offset + 1u,
                                            &x1,
                                            &x2);
        text_editor_draw_whitespace_marker(te, x1, x2, current_y, marker.kind);
        search_offset = marker.offset + 1u;
    }
}

static void text_editor_collect_bracket_pair_decorations(text_editor_node* te,
                                                         text_editor_decoration_buffer* decorations)
{
    croft_text_editor_profile_snapshot* stats = text_editor_profile_stats_mut(te);
    uint64_t start_usec = text_editor_profile_begin(te);
    croft_editor_bracket_match match = {0};
    uint32_t selection_min = 0u;
    uint32_t selection_max = 0u;

    if (!te || !decorations) {
        return;
    }

    text_editor_selection_bounds(te, &selection_min, &selection_max);
    if (selection_min != selection_max
            || croft_editor_bracket_match_near_offset(&te->text_model,
                                                      te->sel_end,
                                                      &match) != CROFT_EDITOR_OK) {
        return;
    }

    text_editor_decoration_buffer_append(decorations,
                                         match.open_offset,
                                         match.open_offset + 1u,
                                         CROFT_TEXT_EDITOR_DECORATION_STYLE_BACKGROUND,
                                         0xC8E0FFCC);
    text_editor_decoration_buffer_append(decorations,
                                         match.close_offset,
                                         match.close_offset + 1u,
                                         CROFT_TEXT_EDITOR_DECORATION_STYLE_BACKGROUND,
                                         0xC8E0FFCC);

    if (stats) {
        text_editor_profile_note(stats,
                                 &stats->bracket_draw_calls,
                                 &stats->bracket_draw_total_usec,
                                 start_usec);
    }
}

static void text_editor_collect_search_decorations(text_editor_node* te,
                                                   text_editor_decoration_buffer* decorations)
{
    croft_text_editor_profile_snapshot* stats = text_editor_profile_stats_mut(te);
    uint64_t start_usec = text_editor_profile_begin(te);
    croft_editor_search_match match = {0};
    uint32_t selection_min = 0u;
    uint32_t selection_max = 0u;
    const char* needle = NULL;
    uint32_t needle_len = 0u;
    uint32_t color_rgba = 0u;
    uint32_t search_from = 0u;

    if (!te || !decorations) {
        return;
    }

    text_editor_selection_bounds(te, &selection_min, &selection_max);
    if (te->find_active && text_editor_find_has_query(te)
            && text_editor_find_query_is_single_line(te->find_query, te->find_query_len)) {
        needle = te->find_query;
        needle_len = te->find_query_len;
        color_rgba = 0xF2D98DBB;
    } else if (selection_min != selection_max) {
        uint32_t start_byte = croft_editor_text_model_byte_offset_at(&te->text_model, selection_min);
        uint32_t end_byte = croft_editor_text_model_byte_offset_at(&te->text_model, selection_max);
        needle = te->text_model.utf8 + start_byte;
        needle_len = end_byte - start_byte;
        if (!text_editor_find_query_is_single_line(needle, needle_len)
                || needle_len >= sizeof(te->find_query)) {
            return;
        }
        color_rgba = 0xDCE6F5CC;
    }

    if (!needle || needle_len == 0u) {
        return;
    }

    while (croft_editor_search_next(&te->text_model, needle, needle_len, search_from, &match)
            == CROFT_EDITOR_OK) {
        if (!(match.start_offset == selection_min && match.end_offset == selection_max)) {
            text_editor_decoration_buffer_append(decorations,
                                                 match.start_offset,
                                                 match.end_offset,
                                                 CROFT_TEXT_EDITOR_DECORATION_STYLE_BACKGROUND,
                                                 color_rgba);
        }
        if (match.start_offset == match.end_offset) {
            break;
        }
        search_from = match.end_offset;
        if (search_from >= croft_editor_text_model_codepoint_length(&te->text_model)) {
            break;
        }
    }

    if (stats) {
        text_editor_profile_note(stats,
                                 &stats->search_draw_calls,
                                 &stats->search_draw_total_usec,
                                 start_usec);
    }
}

static void text_editor_draw_search_overlay_field(float x,
                                                  float y,
                                                  float width,
                                                  const char* label,
                                                  const char* value,
                                                  uint32_t value_len,
                                                  int focused,
                                                  int empty)
{
    uint32_t border_color = focused ? 0x3F7EE8FF : 0xD6DCE4FF;
    uint32_t fill_color = focused ? 0xF7FAFFFF : 0xFFFFFFFF;
    uint32_t text_color = empty ? 0x7A8694FF : 0x1E2A38FF;

    host_render_draw_rect(x, y, width, 32.0f, fill_color);
    host_render_draw_rect(x, y, width, 1.0f, border_color);
    host_render_draw_rect(x, y + 31.0f, width, 1.0f, border_color);
    host_render_draw_rect(x, y, 1.0f, 32.0f, border_color);
    host_render_draw_rect(x + width - 1.0f, y, 1.0f, 32.0f, border_color);
    host_render_draw_text(x + 8.0f, y + 11.0f, label, (uint32_t)strlen(label), 10.5f, 0x667484FF);
    host_render_draw_text(x + 60.0f, y + 22.0f, value, value_len, 13.0f, text_color);
}

static void text_editor_draw_find_overlay(const text_editor_node* te,
                                          const text_editor_layout* layout,
                                          float node_width)
{
    char status_text[48];
    const char* find_text;
    const char* replace_text;
    uint32_t find_len;
    uint32_t replace_len;
    uint32_t current_index = 0u;
    uint32_t total_count = 0u;
    float box_width = 292.0f;
    float box_height;
    float x = node_width - box_width - 12.0f;
    float y = 12.0f;
    float field_x = x + 10.0f;
    float field_width = box_width - 20.0f;
    float status_y;

    (void)layout;

    if (!te || !te->find_active) {
        return;
    }

    box_height = te->replace_active ? 98.0f : 58.0f;
    find_text = (te->find_query_len > 0u) ? te->find_query : "Find";
    replace_text = (te->replace_query_len > 0u) ? te->replace_query : "Replace";
    find_len = (uint32_t)strlen(find_text);
    replace_len = (uint32_t)strlen(replace_text);
    text_editor_find_match_stats(te, &current_index, &total_count);
    if (!text_editor_find_has_query(te)) {
        snprintf(status_text, sizeof(status_text), "Type to search");
    } else if (total_count == 0u) {
        snprintf(status_text, sizeof(status_text), "No results");
    } else if (current_index > 0u) {
        snprintf(status_text, sizeof(status_text), "%u of %u", current_index, total_count);
    } else {
        snprintf(status_text, sizeof(status_text), "%u results", total_count);
    }
    status_y = y + box_height - 8.0f;

    host_render_draw_rect(x, y, box_width, box_height, 0xFFFFFFFF);
    host_render_draw_rect(x, y, box_width, 1.0f, 0xD6DCE4FF);
    host_render_draw_rect(x, y + box_height - 1.0f, box_width, 1.0f, 0xD6DCE4FF);
    host_render_draw_rect(x, y, 1.0f, box_height, 0xD6DCE4FF);
    host_render_draw_rect(x + box_width - 1.0f, y, 1.0f, box_height, 0xD6DCE4FF);

    text_editor_draw_search_overlay_field(field_x,
                                          y + 8.0f,
                                          field_width,
                                          "Find",
                                          find_text,
                                          find_len,
                                          text_editor_active_search_field(te) == TEXT_EDITOR_SEARCH_FIELD_FIND,
                                          te->find_query_len == 0u);
    if (te->replace_active) {
        text_editor_draw_search_overlay_field(field_x,
                                              y + 46.0f,
                                              field_width,
                                              "Replace",
                                              replace_text,
                                              replace_len,
                                              text_editor_active_search_field(te)
                                                  == TEXT_EDITOR_SEARCH_FIELD_REPLACE,
                                              te->replace_query_len == 0u);
    }

    host_render_draw_text(x + 12.0f,
                          status_y,
                          status_text,
                          (uint32_t)strlen(status_text),
                          11.0f,
                          0x667484FF);
}

static void text_editor_draw_composition_overlay(const text_editor_node* te,
                                                 const text_editor_layout* layout)
{
    uint32_t anchor_offset = 0u;
    uint32_t row_number;
    const text_editor_visual_row* row;
    uint32_t line_len_bytes = 0u;
    const char* line_text = NULL;
    uint32_t preview_len;
    float x;
    float baseline_y;
    float line_top;
    float text_width;
    float box_width;

    if (!text_editor_has_active_composition(te) || !layout) {
        return;
    }

    text_editor_selection_bounds(te, &anchor_offset, NULL);
    row_number = text_editor_visual_row_number_for_affinity(te,
                                                            anchor_offset,
                                                            te->sel_start == te->sel_end
                                                                ? te->caret_affinity
                                                                : CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING);
    row = text_editor_visual_row_at_visible_line(te, row_number);
    if (!row) {
        return;
    }

    if (row->model_line_number <= croft_editor_text_model_line_count(&te->text_model)) {
        line_text = croft_editor_text_model_line_utf8(&te->text_model,
                                                      row->model_line_number,
                                                      &line_len_bytes);
    }
    if (!line_text) {
        return;
    }

    preview_len = te->composition_len;
    if (preview_len > TEXT_EDITOR_COMPOSITION_MAX_PREVIEW_BYTES) {
        preview_len = TEXT_EDITOR_COMPOSITION_MAX_PREVIEW_BYTES;
    }

    x = layout->text_inset_x + text_editor_visual_row_prefix_width(te, row, line_text, anchor_offset);
    baseline_y = text_editor_row_baseline(te, row_number);
    line_top = baseline_y - te->baseline_offset;
    text_width = text_editor_measure_text(te, te->composition_utf8, preview_len, te->font_size);
    box_width = text_width + 6.0f;
    if (box_width < 10.0f) {
        box_width = 10.0f;
    }

    host_render_draw_rect(x - 2.0f,
                          line_top + 1.0f,
                          box_width,
                          te->line_height - 2.0f,
                          0xE1F0FFFF);
    host_render_draw_text(x,
                          baseline_y,
                          te->composition_utf8,
                          preview_len,
                          te->font_size,
                          0x153A5BFF);
    host_render_draw_rect(x - 1.0f,
                          line_top + te->line_height - 2.0f,
                          box_width - 2.0f,
                          1.0f,
                          0x2B73D9FF);
    host_render_draw_rect(x + text_width,
                          line_top + 2.0f,
                          1.5f,
                          te->line_height - 4.0f,
                          0x1B3655FF);
}

static void text_editor_draw(scene_node *n, render_ctx *rc) {
    text_editor_node *te = (text_editor_node *)n;
    croft_text_editor_profile_snapshot* stats = text_editor_profile_stats_mut(te);
    uint64_t start_usec = text_editor_profile_begin(te);
    text_editor_layout layout;
    croft_editor_status_snapshot status_snapshot;
    char status_text[96];
    uint32_t line_count;
    uint32_t selection_min = 0;
    uint32_t selection_max = 0;
    uint32_t cursor_offset = te->sel_end;
    uint32_t current_line = text_editor_current_line_number(te);
    uint32_t first_visible_line = 1u;
    uint32_t last_visible_line = 1u;
    uint32_t cursor_row_number = 0u;
    text_editor_decoration_buffer builtin_decorations = {0};

    if (text_editor_prepare_layout(te, n->sx, n->sy, &layout) != CROFT_EDITOR_OK) {
        return;
    }
    text_editor_visible_line_window(te, &layout, &first_visible_line, &last_visible_line);
    status_snapshot = text_editor_status_snapshot_from_node(te);
    if (croft_editor_status_format(&status_snapshot, status_text, sizeof(status_text)) != 0) {
        status_text[0] = '\0';
    }

    host_render_draw_rect(0, 0, n->sx, n->sy, rc->bg_color);
    host_render_draw_rect(0, 0, layout.gutter_width, layout.content_height, 0xEEF1F4FF);
    host_render_draw_rect(0, layout.content_height, n->sx, layout.status_height, 0xE8ECF1FF);
    host_render_draw_rect(layout.gutter_width - 1.0f,
                          0.0f,
                          1.0f,
                          layout.content_height,
                          0x00000020);
    host_render_draw_rect(0.0f,
                          layout.content_height,
                          n->sx,
                          1.0f,
                          0x00000020);

    host_render_save();
    host_render_clip_rect(layout.gutter_width, 0, layout.content_width, layout.content_height);
    host_render_translate(layout.gutter_width + te->scroll_x,
                          layout.text_inset_y + te->scroll_y);

    text_editor_selection_bounds(te, &selection_min, &selection_max);
    line_count = croft_editor_text_model_line_count(&te->text_model);
    if (selection_min == selection_max) {
        cursor_row_number = text_editor_visual_row_number_for_affinity(te,
                                                                       cursor_offset,
                                                                       te->caret_affinity);
    }
    text_editor_collect_search_decorations(te, &builtin_decorations);
    text_editor_collect_bracket_pair_decorations(te, &builtin_decorations);

    {
        uint32_t visible_line_number;

        for (visible_line_number = first_visible_line;
             visible_line_number <= last_visible_line && visible_line_number <= te->visual_row_count;
             visible_line_number++) {
            const text_editor_visual_row* row =
                text_editor_visual_row_at_visible_line(te, visible_line_number);
            float current_y;

            if (stats) {
                stats->background_pass_lines++;
            }
            current_y = text_editor_row_baseline(te, visible_line_number);

            if (row && row->model_line_number == current_line) {
                host_render_draw_rect(-te->scroll_x,
                                      current_y - te->baseline_offset,
                                      layout.content_width,
                                      te->line_height,
                                      0xE2ECF8FF);
            }
            host_render_draw_rect(-te->scroll_x, current_y, layout.content_width, 1.0f, 0x00000012);
            if (row && row->is_first_in_line && row->model_line_number <= line_count) {
                text_editor_draw_indent_guides_for_line(te,
                                                        &layout,
                                                        row->model_line_number,
                                                        current_y);
            }
        }
    }

    text_editor_draw_decorations(te, &layout, CROFT_TEXT_EDITOR_DECORATION_STYLE_BACKGROUND);
    text_editor_draw_decoration_list(te,
                                     &layout,
                                     builtin_decorations.items,
                                     builtin_decorations.count,
                                     CROFT_TEXT_EDITOR_DECORATION_STYLE_BACKGROUND);

    {
        uint32_t visible_line_number;

        for (visible_line_number = first_visible_line;
             visible_line_number <= last_visible_line && visible_line_number <= te->visual_row_count;
             visible_line_number++) {
            const text_editor_visual_row* row =
                text_editor_visual_row_at_visible_line(te, visible_line_number);
            uint32_t line_len_bytes = 0u;
            const char* line_text = NULL;
            float current_y;

            if (stats) {
                stats->text_pass_lines++;
            }
            current_y = text_editor_row_baseline(te, visible_line_number);
            if (row && row->model_line_number <= line_count) {
                line_text = croft_editor_text_model_line_utf8(&te->text_model,
                                                              row->model_line_number,
                                      &line_len_bytes);
            }

            if (row
                    && line_text
                    && selection_min != selection_max
                    && selection_min < row->end_offset
                    && selection_max > row->start_offset) {
                uint32_t highlight_start =
                    selection_min > row->start_offset ? selection_min : row->start_offset;
                uint32_t highlight_end =
                    selection_max < row->end_offset ? selection_max : row->end_offset;
                float x1 = 0.0f;
                float x2 = 0.0f;

                text_editor_visual_row_range_bounds(te,
                                                    &layout,
                                                    row,
                                                    line_text,
                                                    highlight_start,
                                                    highlight_end,
                                                    &x1,
                                                    &x2);
                host_render_draw_rect(x1,
                                      current_y - te->baseline_offset,
                                      x2 - x1,
                                      te->line_height,
                                      0x5B9FE0CC);
            }

            if (row && line_text) {
                text_editor_draw_whitespace_markers_for_row(te, &layout, row, current_y);
            }

            if (row
                    && line_text
                    && selection_min == selection_max
                    && !text_editor_has_active_composition(te)
                    && visible_line_number == cursor_row_number) {
                int millis = (int)(rc->time * 1000.0);

                if ((millis / 500) % 2 == 0) {
                    float cursor_x = layout.text_inset_x
                        + text_editor_visual_row_prefix_width(te, row, line_text, cursor_offset);
                    host_render_draw_rect(cursor_x,
                                          current_y - te->baseline_offset,
                                          2.0f,
                                          te->line_height,
                                          0x000000FF);
                }
            }

            if (row && line_text && row->end_offset > row->start_offset) {
                text_editor_draw_visual_row_text(te,
                                                 &layout,
                                                 row,
                                                 line_text,
                                                 current_y,
                                                 rc->fg_color);
            }
            if (row && row->is_folded_header && row->is_last_in_line && line_text) {
                float line_width = text_editor_measure_text(te,
                                                            line_text + row->start_byte,
                                                            row->end_byte - row->start_byte,
                                                            te->font_size);
                float fold_x = layout.text_inset_x + line_width + 12.0f;

                if (fold_x > layout.content_width - 28.0f) {
                    fold_x = layout.content_width - 28.0f;
                }
                host_render_draw_text(fold_x, current_y, "...", 3u, te->font_size - 1.0f, 0x7D8794FF);
            }
        }
    }
    text_editor_draw_decorations(te, &layout, CROFT_TEXT_EDITOR_DECORATION_STYLE_UNDERLINE);
    text_editor_draw_decoration_list(te,
                                     &layout,
                                     builtin_decorations.items,
                                     builtin_decorations.count,
                                     CROFT_TEXT_EDITOR_DECORATION_STYLE_UNDERLINE);
    text_editor_draw_composition_overlay(te, &layout);
    host_render_restore();

    host_render_save();
    host_render_clip_rect(0, 0, layout.gutter_width, layout.content_height);
    host_render_translate(0.0f, layout.text_inset_y + te->scroll_y);
    {
        uint32_t visible_line_number;

        for (visible_line_number = first_visible_line;
             visible_line_number <= last_visible_line && visible_line_number <= te->visual_row_count;
             visible_line_number++) {
            const text_editor_visual_row* row =
                text_editor_visual_row_at_visible_line(te, visible_line_number);
            char line_label[16];
            int line_label_len;
            float current_y;
            float label_width;
            uint32_t line_number;
            uint32_t color;
            croft_editor_fold_region region = {0};
            uint32_t fold_index = 0u;
            int is_folded;
            int is_foldable;

            if (!row || !row->is_first_in_line) {
                continue;
            }

            line_number = row->model_line_number;
            color = (line_number == current_line) ? 0x17385EFF : 0x798493FF;
            is_folded = text_editor_find_fold_header_index(te, line_number, &fold_index);
            is_foldable = is_folded || text_editor_fold_region_from_model(te, line_number, &region);

            if (stats) {
                stats->gutter_pass_lines++;
            }
            current_y = text_editor_row_baseline(te, visible_line_number);

            if (is_foldable) {
                host_render_draw_text(6.0f,
                                      current_y,
                                      is_folded ? ">" : "v",
                                      1u,
                                      layout.gutter_font_size,
                                      0x6B7684FF);
            }

            line_label_len = snprintf(line_label, sizeof(line_label), "%u", line_number);
            label_width = (line_label_len > 0)
                ? text_editor_measure_text(te,
                                           line_label,
                                           (uint32_t)line_label_len,
                                           layout.gutter_font_size)
                : 0.0f;
            if (line_label_len > 0) {
                host_render_draw_text(layout.gutter_width - 8.0f - label_width,
                                      current_y,
                                      line_label,
                                      (uint32_t)line_label_len,
                                      layout.gutter_font_size,
                                      color);
            }
        }
    }
    host_render_restore();

    if (status_text[0] != '\0') {
        uint32_t status_len = (uint32_t)strlen(status_text);
        host_render_draw_text(12.0f,
                              layout.content_height + layout.status_font_size + 4.0f,
                              status_text,
                              status_len,
                              layout.status_font_size,
                              0x2C3A4AFF);
    }

    text_editor_draw_find_overlay(te, &layout, n->sx);

    if (stats) {
        text_editor_profile_note(stats,
                                 &stats->draw_calls,
                                 &stats->draw_total_usec,
                                 start_usec);
    }
}

static void text_editor_hit_test(scene_node *n, float x, float y, hit_result *out) {
    out->node = n;
    out->local_x = x;
    out->local_y = y;
}

static void text_editor_update_accessibility(scene_node *n) {
    // Phase 4 will map this to ROLE_TEXT_AREA
}

static uint32_t text_editor_hit_index_at_point(text_editor_node *te,
                                               float lx,
                                               float ly,
                                               croft_text_editor_caret_affinity* out_affinity) {
    croft_text_editor_profile_snapshot* stats = text_editor_profile_stats_mut(te);
    uint64_t start_usec = text_editor_profile_begin(te);
    text_editor_layout layout;
    const text_editor_visual_row* row;
    uint32_t visible_line_number;
    uint32_t line_len_bytes = 0u;
    const char* line_text = NULL;
    uint64_t offsets_scanned = 0u;
    float doc_x;
    float doc_y;
    uint32_t hit_offset;

    if (text_editor_prepare_layout(te, te->base.sx, te->base.sy, &layout) != CROFT_EDITOR_OK) {
        return 0u;
    }
    doc_x = lx - layout.gutter_width - te->scroll_x - layout.text_inset_x;
    doc_y = ly - layout.text_inset_y - te->scroll_y;

    if (doc_y < 0.0f) {
        return 0u;
    }

    visible_line_number = (uint32_t)(doc_y / te->line_height) + 1u;
    visible_line_number = text_editor_clamp_u32(visible_line_number,
                                                1u,
                                                text_editor_visible_line_count(te));
    row = text_editor_visual_row_at_visible_line(te, visible_line_number);
    if (!row) {
        return 0u;
    }
    if (row->model_line_number <= croft_editor_text_model_line_count(&te->text_model)) {
        line_text = croft_editor_text_model_line_utf8(&te->text_model,
                                                      row->model_line_number,
                                                      &line_len_bytes);
    }

    offsets_scanned = (uint64_t)(row->end_offset - row->start_offset + 1u);
    hit_offset = text_editor_pick_offset_in_row_for_x(te, row, line_text, doc_x, out_affinity);

    if (stats) {
        stats->hit_index_offsets_scanned += offsets_scanned;
        text_editor_profile_note(stats,
                                 &stats->hit_index_calls,
                                 &stats->hit_index_total_usec,
                                 start_usec);
    }
    return hit_offset;
}

static uint32_t text_editor_hit_index(text_editor_node *te, float lx, float ly) {
    return text_editor_hit_index_at_point(te, lx, ly, NULL);
}

static void text_editor_move_wrapped_vertical(text_editor_node* te,
                                              int direction,
                                              int selecting) {
    text_editor_layout layout;
    uint32_t current_row_number;
    uint32_t target_row_number;
    const text_editor_visual_row* current_row;
    const text_editor_visual_row* target_row;
    const char* current_line_text = NULL;
    const char* target_line_text = NULL;
    uint32_t line_len_bytes = 0u;
    uint32_t best_offset;
    croft_text_editor_caret_affinity target_affinity = CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING;

    if (!te || text_editor_prepare_layout(te, te->base.sx, te->base.sy, &layout) != CROFT_EDITOR_OK) {
        return;
    }

    current_row_number = text_editor_visual_row_number_for_affinity(te,
                                                                    te->sel_end,
                                                                    te->caret_affinity);
    if (current_row_number == 0u) {
        current_row_number = 1u;
    }
    target_row_number = current_row_number;
    if (direction < 0 && target_row_number > 1u) {
        target_row_number--;
    } else if (direction > 0 && target_row_number < te->visual_row_count) {
        target_row_number++;
    }

    current_row = text_editor_visual_row_at_visible_line(te, current_row_number);
    target_row = text_editor_visual_row_at_visible_line(te, target_row_number);
    if (!current_row || !target_row) {
        return;
    }

    if (current_row->model_line_number <= croft_editor_text_model_line_count(&te->text_model)) {
        current_line_text = croft_editor_text_model_line_utf8(&te->text_model,
                                                              current_row->model_line_number,
                                                              &line_len_bytes);
    }
    if (te->preferred_column == 0u) {
        croft_editor_position position = croft_editor_text_model_get_position_at(&te->text_model, te->sel_end);
        te->preferred_column = position.column;
        te->preferred_visual_x = current_line_text
            ? text_editor_visual_row_prefix_width(te, current_row, current_line_text, te->sel_end)
            : 0.0f;
    }

    if (target_row->model_line_number <= croft_editor_text_model_line_count(&te->text_model)) {
        target_line_text = croft_editor_text_model_line_utf8(&te->text_model,
                                                             target_row->model_line_number,
                                                             &line_len_bytes);
    }

    best_offset = text_editor_pick_offset_in_row_for_x(te,
                                                       target_row,
                                                       target_line_text,
                                                       te->preferred_visual_x,
                                                       &target_affinity);

    if (selecting) {
        te->sel_end = best_offset;
    } else {
        te->sel_start = best_offset;
        te->sel_end = best_offset;
    }
    te->caret_affinity = target_affinity;
    text_editor_sync_selection(te);
}

static void text_editor_mouse_event(scene_node *n, int action, float local_x, float local_y) {
    text_editor_node *te = (text_editor_node *)n;
    text_editor_layout layout;

    if (text_editor_prepare_layout(te, n->sx, n->sy, &layout) != CROFT_EDITOR_OK) {
        return;
    }
    if (local_y >= layout.content_height) {
        if (action == 0 || action == 2) {
            te->is_selecting = 0;
        }
        return;
    }
    if (local_x < layout.gutter_width && action == 1) {
        uint32_t visible_line_number;
        uint32_t line_number;
        const text_editor_visual_row* row;
        croft_editor_fold_region region = {0};

        visible_line_number = text_editor_clamp_u32((uint32_t)((local_y - layout.text_inset_y - te->scroll_y)
                                                                / te->line_height)
                                                        + 1u,
                                                    1u,
                                                    text_editor_visible_line_count(te));
        row = text_editor_visual_row_at_visible_line(te, visible_line_number);
        line_number = row ? row->model_line_number : 1u;
        if (local_x <= 18.0f
                && (text_editor_find_fold_header_index(te, line_number, NULL)
                    || text_editor_fold_region_from_model(te, line_number, &region))) {
            text_editor_toggle_fold_line(te, line_number);
            te->is_selecting = 0;
            return;
        }
    }
    if (local_x < layout.gutter_width) {
        local_x = layout.gutter_width;
    }

    if (action == 1) { // Down
        uint32_t hit_index;
        croft_text_editor_caret_affinity hit_affinity = CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING;

        hit_index = text_editor_hit_index_at_point(te, local_x, local_y, &hit_affinity);
        text_editor_break_coalescing(te);
        te->is_selecting = 1;
        text_editor_collapse_selection_with_affinity(te, hit_index, hit_affinity, 0);
    } else if (action == 3 && te->is_selecting) { // Drag
        uint32_t hit_index;
        croft_text_editor_caret_affinity hit_affinity = CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING;

        hit_index = text_editor_hit_index_at_point(te, local_x, local_y, &hit_affinity);
        text_editor_break_coalescing(te);
        text_editor_set_selection_with_affinity(te, te->sel_start, hit_index, hit_affinity, 1);
    } else if (action == 0 || action == 2) { // Up
        te->is_selecting = 0;
    }
}

static void text_editor_char_event(scene_node *n, uint32_t codepoint) {
    text_editor_node *te = (text_editor_node *)n;
    Text* text_tree = text_editor_live_text(te);
    SapTxnCtx *txn;
    uint32_t selection_min = 0;
    uint32_t selection_max = 0;

    if (te->find_active) {
        if (codepoint == '\n' || codepoint == '\r') {
            return;
        }
        if (text_editor_active_search_field(te) == TEXT_EDITOR_SEARCH_FIELD_REPLACE) {
            (void)text_editor_replace_query_append_codepoint(te, codepoint);
        } else if (text_editor_find_query_append_codepoint(te, codepoint)) {
            text_editor_find_refresh_from_cursor(te);
        }
        return;
    }

    if (codepoint == (uint32_t)'\t') {
        return;
    }

    text_editor_clear_composition_state(te);

    if (!text_tree || !te->env) {
        return;
    }

    text_editor_selection_bounds(te, &selection_min, &selection_max);
    if (te->document) {
        if (croft_editor_document_replace_range_with_codepoint(te->document,
                                                               selection_min,
                                                               selection_max,
                                                               codepoint,
                                                               CROFT_EDITOR_EDIT_INSERT) != 0) {
            return;
        }

        text_editor_collapse_selection(te, selection_min + 1u, 0);
        text_editor_sync_cache(te);
        return;
    }

    txn = sap_txn_begin(te->env, NULL, 0);
    if (!txn) {
        return;
    }

    if (selection_min != selection_max) {
        text_editor_delete_range(txn, te, selection_min, selection_max);
        te->sel_start = selection_min;
        te->sel_end = selection_min;
    }

    if (text_insert(txn, text_tree, te->sel_end, codepoint) != 0) {
        sap_txn_abort(txn);
        return;
    }
    sap_txn_commit(txn);

    te->sel_end++;
    te->sel_start = te->sel_end;
    te->preferred_column = 0;
    text_editor_sync_cache(te);
}

static void text_editor_key_event(scene_node *n, int key, int action) {
    text_editor_node *te = (text_editor_node *)n;
    int command_mode;
    int selecting;
    int word_part_mode;
    int word_mode;

    if (action == CROFT_KEY_RELEASE) {
        return;
    }

    command_mode = (te->modifiers & (CROFT_UI_MOD_SUPER | CROFT_UI_MOD_CONTROL)) != 0u;
    selecting = (te->modifiers & CROFT_UI_MOD_SHIFT) != 0u;
    word_part_mode = (te->modifiers & CROFT_UI_MOD_ALT) != 0u
        && (te->modifiers & CROFT_UI_MOD_CONTROL) != 0u;
    word_mode = !word_part_mode
        && (te->modifiers & (CROFT_UI_MOD_ALT | CROFT_UI_MOD_CONTROL)) != 0u;

    if (te->find_active) {
        if (key == CROFT_KEY_ESCAPE) {
            text_editor_node_find_close(te);
            return;
        }
        if (command_mode && key == CROFT_KEY_F) {
            if ((te->modifiers & CROFT_UI_MOD_ALT) != 0u) {
                text_editor_node_replace_activate(te);
            } else {
                text_editor_node_find_activate(te);
            }
            return;
        }
        if (key == CROFT_KEY_TAB && te->replace_active) {
            te->search_focus_field = te->search_focus_field == TEXT_EDITOR_SEARCH_FIELD_REPLACE
                ? TEXT_EDITOR_SEARCH_FIELD_FIND
                : TEXT_EDITOR_SEARCH_FIELD_REPLACE;
            return;
        }
        if (command_mode && (key == CROFT_KEY_ENTER
                || key == CROFT_KEY_KP_ENTER_OLD
                || key == CROFT_KEY_KP_ENTER)) {
            if (te->replace_active) {
                (void)text_editor_node_replace_all(te);
            }
            return;
        }
        if (command_mode && key == CROFT_KEY_G) {
            if (selecting) {
                text_editor_node_find_previous(te);
            } else {
                text_editor_node_find_next(te);
            }
            return;
        }
        if (key == CROFT_KEY_ENTER
                || key == CROFT_KEY_KP_ENTER_OLD
                || key == CROFT_KEY_KP_ENTER) {
            if (te->replace_active
                    && text_editor_active_search_field(te) == TEXT_EDITOR_SEARCH_FIELD_REPLACE) {
                (void)text_editor_node_replace_next(te);
            } else if (selecting) {
                text_editor_node_find_previous(te);
            } else {
                text_editor_node_find_next(te);
            }
            return;
        }
        if (key == CROFT_KEY_BACKSPACE || key == CROFT_KEY_DELETE) {
            if (text_editor_active_search_field(te) == TEXT_EDITOR_SEARCH_FIELD_REPLACE) {
                (void)text_editor_replace_query_backspace(te);
            } else if (text_editor_find_query_backspace(te)) {
                if (text_editor_find_has_query(te)) {
                    text_editor_find_refresh_from_cursor(te);
                }
            }
            return;
        }
        return;
    }

    if (command_mode
            && (te->modifiers & CROFT_UI_MOD_ALT) != 0u
            && key == CROFT_KEY_F) {
        text_editor_node_replace_activate(te);
        return;
    }
    if (command_mode && key == CROFT_KEY_F) {
        text_editor_node_find_activate(te);
        return;
    }
    if (command_mode && key == CROFT_KEY_G) {
        if (selecting) {
            text_editor_node_find_previous(te);
        } else {
            text_editor_node_find_next(te);
        }
        return;
    }
    if ((te->modifiers & CROFT_UI_MOD_ALT) != 0u
            && (te->modifiers & (CROFT_UI_MOD_CONTROL | CROFT_UI_MOD_SUPER)) == 0u
            && key == CROFT_KEY_Z) {
        text_editor_node_set_wrap_enabled(te, te->wrap_enabled == 0u);
        return;
    }
    if (command_mode
            && (te->modifiers & CROFT_UI_MOD_ALT) != 0u
            && key == CROFT_KEY_LEFT_BRACKET) {
        text_editor_node_fold(te);
        return;
    }
    if (command_mode
            && (te->modifiers & CROFT_UI_MOD_ALT) != 0u
            && key == CROFT_KEY_RIGHT_BRACKET) {
        text_editor_node_unfold(te);
        return;
    }
    if (command_mode && key == CROFT_KEY_LEFT_BRACKET) {
        text_editor_node_outdent(te);
        return;
    }
    if (command_mode && key == CROFT_KEY_RIGHT_BRACKET) {
        text_editor_node_indent(te);
        return;
    }
    if (key == CROFT_KEY_TAB) {
        if (selecting) {
            text_editor_node_outdent(te);
        } else {
            text_editor_node_indent(te);
        }
        return;
    }

    if (te->document && command_mode) {
        if (key == CROFT_KEY_Z) {
            int32_t rc = selecting
                ? croft_editor_document_redo(te->document)
                : croft_editor_document_undo(te->document);
            if (rc == 0) {
                text_editor_sync_cache(te);
            }
            return;
        }
        if (key == CROFT_KEY_Y) {
            if (croft_editor_document_redo(te->document) == 0) {
                text_editor_sync_cache(te);
            }
            return;
        }
    }

    if (key == CROFT_KEY_BACKSPACE || key == CROFT_KEY_DELETE) {
        uint32_t delete_start = 0;
        uint32_t delete_end = 0;
        int has_delete = 0;

        if (word_part_mode) {
            has_delete = (key == CROFT_KEY_BACKSPACE)
                ? croft_editor_command_delete_word_part_left_range(&te->text_model,
                                                                   te->sel_start,
                                                                   te->sel_end,
                                                                   &delete_start,
                                                                   &delete_end)
                : croft_editor_command_delete_word_part_right_range(&te->text_model,
                                                                    te->sel_start,
                                                                    te->sel_end,
                                                                    &delete_start,
                                                                    &delete_end);
        } else if (word_mode) {
            has_delete = (key == CROFT_KEY_BACKSPACE)
                ? croft_editor_command_delete_word_left_range(&te->text_model,
                                                              te->sel_start,
                                                              te->sel_end,
                                                              &delete_start,
                                                              &delete_end)
                : croft_editor_command_delete_word_right_range(&te->text_model,
                                                               te->sel_start,
                                                               te->sel_end,
                                                               &delete_start,
                                                               &delete_end);
        } else {
            has_delete = (key == CROFT_KEY_BACKSPACE)
                ? croft_editor_command_delete_left_range(&te->text_model,
                                                         te->sel_start,
                                                         te->sel_end,
                                                         &delete_start,
                                                         &delete_end)
                : croft_editor_command_delete_right_range(&te->text_model,
                                                          te->sel_start,
                                                          te->sel_end,
                                                          &delete_start,
                                                          &delete_end);
        }

        if (has_delete) {
            if (te->document) {
                if (croft_editor_document_delete_range(te->document,
                                                       delete_start,
                                                       delete_end,
                                                       key == CROFT_KEY_BACKSPACE
                                                           ? CROFT_EDITOR_EDIT_DELETE_BACKWARD
                                                           : CROFT_EDITOR_EDIT_DELETE_FORWARD) == 0) {
                    text_editor_collapse_selection(te, delete_start, 0);
                }
            } else {
                SapTxnCtx *txn = sap_txn_begin(te->env, NULL, 0);
                if (!txn) {
                    return;
                }
                text_editor_delete_range(txn, te, delete_start, delete_end);
                text_editor_collapse_selection(te, delete_start, 0);
                sap_txn_commit(txn);
            }
        }
        text_editor_sync_cache(te);
    } else if (key == CROFT_KEY_LEFT) {
        text_editor_break_coalescing(te);
        if (word_part_mode) {
            croft_editor_command_move_word_part_left(&te->text_model,
                                                     &te->sel_start,
                                                     &te->sel_end,
                                                     &te->preferred_column,
                                                     selecting);
        } else if (word_mode) {
            croft_editor_command_move_word_left(&te->text_model,
                                                &te->sel_start,
                                                &te->sel_end,
                                                &te->preferred_column,
                                                selecting);
        } else {
            croft_editor_command_move_left(&te->text_model,
                                           &te->sel_start,
                                           &te->sel_end,
                                           &te->preferred_column,
                                           selecting);
        }
        if (te->preferred_column == 0u) {
            te->preferred_visual_x = 0.0f;
        }
        te->caret_affinity = CROFT_TEXT_EDITOR_CARET_AFFINITY_TRAILING;
        text_editor_sync_selection(te);
    } else if (key == CROFT_KEY_RIGHT) {
        text_editor_break_coalescing(te);
        if (word_part_mode) {
            croft_editor_command_move_word_part_right(&te->text_model,
                                                      &te->sel_start,
                                                      &te->sel_end,
                                                      &te->preferred_column,
                                                      selecting);
        } else if (word_mode) {
            croft_editor_command_move_word_right(&te->text_model,
                                                 &te->sel_start,
                                                 &te->sel_end,
                                                 &te->preferred_column,
                                                 selecting);
        } else {
            croft_editor_command_move_right(&te->text_model,
                                            &te->sel_start,
                                            &te->sel_end,
                                            &te->preferred_column,
                                            selecting);
        }
        if (te->preferred_column == 0u) {
            te->preferred_visual_x = 0.0f;
        }
        te->caret_affinity = CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING;
        text_editor_sync_selection(te);
    } else if (key == CROFT_KEY_UP || key == CROFT_KEY_DOWN) {
        text_editor_break_coalescing(te);
        if (te->wrap_enabled) {
            text_editor_move_wrapped_vertical(te, key == CROFT_KEY_UP ? -1 : 1, selecting);
        } else {
            if (key == CROFT_KEY_UP) {
                croft_editor_command_move_up(&te->text_model,
                                             &te->sel_start,
                                             &te->sel_end,
                                             &te->preferred_column,
                                             selecting);
            } else {
                croft_editor_command_move_down(&te->text_model,
                                               &te->sel_start,
                                               &te->sel_end,
                                               &te->preferred_column,
                                               selecting);
            }
            te->caret_affinity = CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING;
            text_editor_sync_selection(te);
        }
    } else if (key == CROFT_KEY_HOME) {
        text_editor_break_coalescing(te);
        croft_editor_command_move_home(&te->text_model,
                                       &te->sel_start,
                                       &te->sel_end,
                                       &te->preferred_column,
                                       selecting);
        if (te->preferred_column == 0u) {
            te->preferred_visual_x = 0.0f;
        }
        te->caret_affinity = CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING;
        text_editor_sync_selection(te);
    } else if (key == CROFT_KEY_END) {
        text_editor_break_coalescing(te);
        croft_editor_command_move_end(&te->text_model,
                                      &te->sel_start,
                                      &te->sel_end,
                                      &te->preferred_column,
                                      selecting);
        if (te->preferred_column == 0u) {
            te->preferred_visual_x = 0.0f;
        }
        te->caret_affinity = CROFT_TEXT_EDITOR_CARET_AFFINITY_TRAILING;
        text_editor_sync_selection(te);
    } else if (key == CROFT_KEY_ENTER
            || key == CROFT_KEY_KP_ENTER_OLD
            || key == CROFT_KEY_KP_ENTER) {
        text_editor_char_event(n, '\n');
    }
}

static scene_node_vtbl text_editor_vtbl = {
    .draw = text_editor_draw,
    .hit_test = text_editor_hit_test,
    .update_accessibility = text_editor_update_accessibility,
    .transform_coords = NULL,
    .on_mouse_event = text_editor_mouse_event,
    .on_key_event = text_editor_key_event,
    .on_char_event = text_editor_char_event
};

void text_editor_node_init(text_editor_node *n, struct SapEnv *env, float x, float y, float sx, float sy, struct Text *text_tree) {
    scene_node_init(&n->base, &text_editor_vtbl, x, y, sx, sy);
    n->base.flags |= 1; // Mark as focusable/container
    n->env = env;
    n->text_tree = text_tree;
    n->document = NULL;
    n->scroll_x = 0;
    n->scroll_y = 0;
    n->utf8_cache = NULL;
    n->utf8_len = 0;
    n->font_size = 15.0f;
    n->line_height = 21.0f;
    n->sel_start = 0;
    n->sel_end = 0;
    n->caret_affinity = CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING;
    n->preferred_column = 0;
    n->preferred_visual_x = 0.0f;
    n->modifiers = 0u;
    n->is_selecting = 0;
    n->find_active = 0;
    n->replace_active = 0;
    n->search_focus_field = TEXT_EDITOR_SEARCH_FIELD_FIND;
    n->find_query[0] = '\0';
    n->find_query_len = 0u;
    n->replace_query[0] = '\0';
    n->replace_query_len = 0u;
    n->folded_region_count = 0u;
    n->wrap_enabled = 0u;
    n->visual_layout_dirty = 1u;
    n->visual_layout_width = 0.0f;
    n->visual_rows = NULL;
    n->visual_row_count = 0u;
    n->visual_row_capacity = 0u;
    n->line_first_visible_rows = NULL;
    n->line_visible_row_counts = NULL;
    n->line_visible_row_capacity = 0u;
    memset(&n->font_probe, 0, sizeof(n->font_probe));
    n->font_metrics_valid = 0u;
    n->baseline_offset = 15.0f;
    n->composition_utf8 = NULL;
    n->composition_len = 0u;
    n->composition_capacity = 0u;
    n->composition_selection_start = 0u;
    n->composition_selection_end = 0u;
    n->decorations = NULL;
    n->decoration_count = 0u;
    n->decoration_capacity = 0u;
    n->profiling_enabled = 0u;
    memset(&n->profile_stats, 0, sizeof(n->profile_stats));
    croft_editor_text_model_init(&n->text_model);
    croft_editor_line_cache_init(&n->line_cache);
    n->selection = croft_editor_selection_create(
        croft_editor_position_create(1, 1),
        croft_editor_position_create(1, 1)
    );
    n->syntax_language = CROFT_EDITOR_SYNTAX_LANGUAGE_PLAIN_TEXT;

    text_editor_resolve_font_metrics(n);
    
    text_editor_sync_cache(n);
    
    croft_scene_a11y_node_config cfg = {
        .x = x, .y = y, .width = sx, .height = sy,
        .label = "Code Editor"
    };
    n->base.a11y_handle = croft_scene_a11y_create_node(CROFT_SCENE_A11Y_ROLE_TEXT, &cfg);
}

void text_editor_node_bind_document(text_editor_node *n, struct croft_editor_document *document) {
    if (!n) {
        return;
    }

    n->document = document;
    n->env = document ? croft_editor_document_env(document) : n->env;
    n->text_tree = document ? croft_editor_document_text(document) : n->text_tree;
    text_editor_sync_cache(n);
}

void text_editor_node_reset_profile(text_editor_node *n) {
    uint32_t enabled;

    if (!n) {
        return;
    }

    enabled = n->profiling_enabled;
    memset(&n->profile_stats, 0, sizeof(n->profile_stats));
    n->profile_stats.enabled = enabled;
}

void text_editor_node_set_profiling(text_editor_node *n, int enabled) {
    if (!n) {
        return;
    }

    n->profiling_enabled = enabled ? 1u : 0u;
    text_editor_node_reset_profile(n);
}

void text_editor_node_get_profile(const text_editor_node *n,
                                  croft_text_editor_profile_snapshot *out_snapshot) {
    if (!out_snapshot) {
        return;
    }

    memset(out_snapshot, 0, sizeof(*out_snapshot));
    if (!n) {
        return;
    }

    *out_snapshot = n->profile_stats;
    out_snapshot->enabled = n->profiling_enabled;
}

void text_editor_node_get_metrics(const text_editor_node *n,
                                  croft_text_editor_metrics_snapshot *out_snapshot) {
    if (!out_snapshot) {
        return;
    }

    memset(out_snapshot, 0, sizeof(*out_snapshot));
    if (!n) {
        return;
    }

    text_editor_resolve_font_metrics((text_editor_node*)n);
    out_snapshot->font_metrics_valid = n->font_metrics_valid;
    out_snapshot->font_size = n->font_size;
    out_snapshot->font_line_height = n->font_probe.line_height;
    out_snapshot->ascender = n->font_probe.ascender;
    out_snapshot->descender = n->font_probe.descender;
    out_snapshot->leading = n->font_probe.leading;
    out_snapshot->line_height = n->line_height;
    out_snapshot->baseline_offset = n->baseline_offset;
    out_snapshot->text_inset_x = (float)TEXT_EDITOR_TEXT_INSET_X;
    out_snapshot->text_inset_y = (float)TEXT_EDITOR_TEXT_INSET_Y;
}

void text_editor_node_set_caret_affinity(text_editor_node *n,
                                         croft_text_editor_caret_affinity affinity) {
    if (!n) {
        return;
    }
    n->caret_affinity = text_editor_normalize_caret_affinity(affinity);
}

croft_text_editor_caret_affinity text_editor_node_get_caret_affinity(const text_editor_node *n) {
    if (!n) {
        return CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING;
    }
    return text_editor_normalize_caret_affinity(n->caret_affinity);
}

int32_t text_editor_node_set_decorations(text_editor_node *n,
                                         const croft_text_editor_decoration *decorations,
                                         uint32_t decoration_count) {
    if (!n || (!decorations && decoration_count > 0u)) {
        return CROFT_EDITOR_ERR_INVALID;
    }
    if (decoration_count > 0u) {
        int32_t rc = text_editor_reserve_decorations(n, decoration_count);
        if (rc != CROFT_EDITOR_OK) {
            return rc;
        }
        memcpy(n->decorations,
               decorations,
               sizeof(croft_text_editor_decoration) * (size_t)decoration_count);
    }
    n->decoration_count = decoration_count;
    text_editor_normalize_decorations(n);
    return CROFT_EDITOR_OK;
}

void text_editor_node_clear_decorations(text_editor_node *n) {
    if (!n) {
        return;
    }
    n->decoration_count = 0u;
}

uint32_t text_editor_node_decoration_count(const text_editor_node *n) {
    return n ? n->decoration_count : 0u;
}

int32_t text_editor_node_get_decoration(const text_editor_node *n,
                                        uint32_t index,
                                        croft_text_editor_decoration *out_decoration) {
    if (!n || !out_decoration || index >= n->decoration_count || !n->decorations) {
        return CROFT_EDITOR_ERR_INVALID;
    }
    *out_decoration = n->decorations[index];
    return CROFT_EDITOR_OK;
}

void text_editor_node_set_text(text_editor_node *n, struct Text *text_tree) {
    n->text_tree = text_tree;
    text_editor_sync_cache(n);
}

void text_editor_node_set_modifiers(text_editor_node *n, uint32_t modifiers) {
    if (!n) {
        return;
    }
    n->modifiers = modifiers;
}

void text_editor_node_set_wrap_enabled(text_editor_node *n, int enabled) {
    if (!n) {
        return;
    }
    n->wrap_enabled = enabled ? 1u : 0u;
    n->preferred_column = 0u;
    n->preferred_visual_x = 0.0f;
    if (n->wrap_enabled) {
        n->scroll_x = 0.0f;
    }
    text_editor_invalidate_visual_layout(n);
    text_editor_ensure_cursor_visible(n);
}

int text_editor_node_is_wrap_enabled(const text_editor_node *n) {
    return n ? (n->wrap_enabled != 0u) : 0;
}

void text_editor_node_select_all(text_editor_node *n) {
    if (!n) {
        return;
    }
    text_editor_set_selection_with_affinity(n,
                                            0u,
                                            croft_editor_text_model_codepoint_length(&n->text_model),
                                            CROFT_TEXT_EDITOR_CARET_AFFINITY_TRAILING,
                                            1);
}

int32_t text_editor_node_select_word_at_offset(text_editor_node *n, uint32_t offset) {
    croft_editor_position position;
    croft_editor_range range;
    uint32_t start_offset;
    uint32_t end_offset;

    if (!n) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    offset = text_editor_clamp_u32(offset,
                                   0u,
                                   croft_editor_text_model_codepoint_length(&n->text_model));
    position = croft_editor_text_model_get_position_at(&n->text_model, offset);
    if (!croft_editor_text_model_get_word_range_at(&n->text_model, position, NULL, &range)) {
        return CROFT_EDITOR_ERR_INVALID;
    }

    start_offset = croft_editor_text_model_get_offset_at(&n->text_model,
                                                         range.start_line_number,
                                                         range.start_column);
    end_offset = croft_editor_text_model_get_offset_at(&n->text_model,
                                                       range.end_line_number,
                                                       range.end_column);
    text_editor_break_coalescing(n);
    text_editor_set_selection_with_affinity(n,
                                            start_offset,
                                            end_offset,
                                            CROFT_TEXT_EDITOR_CARET_AFFINITY_TRAILING,
                                            1);
    return CROFT_EDITOR_OK;
}

int text_editor_node_is_find_active(const text_editor_node *n) {
    return n ? n->find_active : 0;
}

int text_editor_node_is_replace_active(const text_editor_node *n) {
    return n ? n->replace_active : 0;
}

int text_editor_node_has_composition(const text_editor_node *n) {
    return text_editor_has_active_composition(n);
}

int32_t text_editor_node_set_composition_utf8(text_editor_node *n,
                                              const uint8_t *utf8,
                                              size_t utf8_len,
                                              uint32_t selection_start,
                                              uint32_t selection_end) {
    if (!n) {
        return -1;
    }
    if (!utf8 || utf8_len == 0u) {
        text_editor_clear_composition_state(n);
        return 0;
    }
    if (text_editor_reserve_composition_buffer(n, (uint32_t)utf8_len + 1u) != CROFT_EDITOR_OK) {
        return -1;
    }

    memcpy(n->composition_utf8, utf8, utf8_len);
    n->composition_utf8[utf8_len] = '\0';
    n->composition_len = (uint32_t)utf8_len;
    if (selection_end < selection_start) {
        uint32_t tmp = selection_start;
        selection_start = selection_end;
        selection_end = tmp;
    }
    n->composition_selection_start = selection_start;
    n->composition_selection_end = selection_end;
    return 0;
}

void text_editor_node_clear_composition(text_editor_node *n) {
    text_editor_clear_composition_state(n);
}

int32_t text_editor_node_copy_composition_utf8(const text_editor_node *n,
                                               char **out_utf8,
                                               size_t *out_len) {
    char* copy;

    if (!n || !out_utf8 || !out_len) {
        return -1;
    }

    copy = (char*)malloc((size_t)n->composition_len + 1u);
    if (!copy) {
        return -1;
    }
    if (n->composition_len > 0u && n->composition_utf8) {
        memcpy(copy, n->composition_utf8, n->composition_len);
    }
    copy[n->composition_len] = '\0';
    *out_utf8 = copy;
    *out_len = n->composition_len;
    return 0;
}

void text_editor_node_find_activate(text_editor_node *n) {
    if (!n) {
        return;
    }
    text_editor_clear_composition_state(n);
    n->find_active = 1;
    n->replace_active = 0;
    n->search_focus_field = TEXT_EDITOR_SEARCH_FIELD_FIND;
    if (!text_editor_copy_selection_into_find_query(n) && !text_editor_find_has_query(n)) {
        text_editor_find_query_clear(n);
    }
    if (text_editor_find_has_query(n)) {
        text_editor_find_refresh_from_cursor(n);
    }
}

void text_editor_node_replace_activate(text_editor_node *n) {
    if (!n) {
        return;
    }
    text_editor_clear_composition_state(n);
    n->find_active = 1;
    n->replace_active = 1;
    if (!text_editor_copy_selection_into_find_query(n) && !text_editor_find_has_query(n)) {
        text_editor_find_query_clear(n);
    }
    n->search_focus_field = text_editor_find_has_query(n)
        ? TEXT_EDITOR_SEARCH_FIELD_REPLACE
        : TEXT_EDITOR_SEARCH_FIELD_FIND;
    if (text_editor_find_has_query(n)) {
        text_editor_find_refresh_from_cursor(n);
    }
}

void text_editor_node_find_close(text_editor_node *n) {
    if (!n) {
        return;
    }
    text_editor_clear_composition_state(n);
    n->find_active = 0;
    n->replace_active = 0;
    n->search_focus_field = TEXT_EDITOR_SEARCH_FIELD_FIND;
}

int32_t text_editor_node_set_find_query_utf8(text_editor_node *n,
                                             const char *utf8,
                                             size_t utf8_len) {
    if (!n) {
        return -1;
    }
    if (!text_editor_find_set_query_utf8(n, utf8, (uint32_t)utf8_len)) {
        return -1;
    }
    if (n->find_active && text_editor_find_has_query(n)) {
        text_editor_find_refresh_from_cursor(n);
    }
    return 0;
}

int32_t text_editor_node_set_replace_query_utf8(text_editor_node *n,
                                                const char *utf8,
                                                size_t utf8_len) {
    if (!n) {
        return -1;
    }
    return text_editor_replace_set_query_utf8(n, utf8, (uint32_t)utf8_len) ? 0 : -1;
}

int32_t text_editor_node_find_next(text_editor_node *n) {
    uint32_t selection_min = 0u;
    uint32_t selection_max = 0u;
    uint32_t start_offset;

    if (!n) {
        return -1;
    }
    if (!text_editor_find_has_query(n) && !text_editor_copy_selection_into_find_query(n)) {
        return -1;
    }
    n->find_active = 1;
    if (!n->replace_active) {
        n->search_focus_field = TEXT_EDITOR_SEARCH_FIELD_FIND;
    }
    text_editor_selection_bounds(n, &selection_min, &selection_max);
    start_offset = text_editor_selection_matches_query(n) ? selection_max : selection_min;
    return text_editor_find_next_from(n, start_offset, 1);
}

int32_t text_editor_node_find_previous(text_editor_node *n) {
    uint32_t selection_min = 0u;

    if (!n) {
        return -1;
    }
    if (!text_editor_find_has_query(n) && !text_editor_copy_selection_into_find_query(n)) {
        return -1;
    }
    n->find_active = 1;
    if (!n->replace_active) {
        n->search_focus_field = TEXT_EDITOR_SEARCH_FIELD_FIND;
    }
    text_editor_selection_bounds(n, &selection_min, NULL);
    return text_editor_find_previous_before(n, selection_min, 1);
}

int32_t text_editor_node_replace_next(text_editor_node *n) {
    if (!n) {
        return -1;
    }
    if (!text_editor_find_has_query(n) && !text_editor_copy_selection_into_find_query(n)) {
        return -1;
    }
    n->find_active = 1;
    n->replace_active = 1;
    n->search_focus_field = TEXT_EDITOR_SEARCH_FIELD_REPLACE;
    return text_editor_replace_next_internal(n);
}

int32_t text_editor_node_replace_all(text_editor_node *n) {
    if (!n) {
        return -1;
    }
    if (!text_editor_find_has_query(n) && !text_editor_copy_selection_into_find_query(n)) {
        return -1;
    }
    n->find_active = 1;
    n->replace_active = 1;
    n->search_focus_field = TEXT_EDITOR_SEARCH_FIELD_REPLACE;
    return text_editor_replace_all_internal(n);
}

int32_t text_editor_node_fold(text_editor_node *n) {
    return text_editor_fold_line(n, text_editor_current_line_number(n));
}

int32_t text_editor_node_unfold(text_editor_node *n) {
    return text_editor_unfold_line(n, text_editor_current_line_number(n));
}

int32_t text_editor_node_toggle_fold(text_editor_node *n) {
    return n ? text_editor_toggle_fold_line(n, text_editor_current_line_number(n)) : -1;
}

int32_t text_editor_node_indent(text_editor_node *n) {
    return text_editor_apply_tab_edit(n, 0);
}

int32_t text_editor_node_outdent(text_editor_node *n) {
    return text_editor_apply_tab_edit(n, 1);
}

int32_t text_editor_node_copy_selection_utf8(text_editor_node *n, char **out_utf8, size_t *out_len) {
    uint32_t selection_min = 0;
    uint32_t selection_max = 0;
    uint32_t start_byte;
    uint32_t end_byte;
    size_t len;
    char *copy;

    if (!n || !out_utf8 || !out_len) {
        return -1;
    }

    text_editor_selection_bounds(n, &selection_min, &selection_max);
    start_byte = croft_editor_text_model_byte_offset_at(&n->text_model, selection_min);
    end_byte = croft_editor_text_model_byte_offset_at(&n->text_model, selection_max);
    len = (size_t)(end_byte - start_byte);
    copy = (char*)malloc(len + 1u);
    if (!copy) {
        return -1;
    }
    if (len > 0u) {
        memcpy(copy, n->text_model.utf8 + start_byte, len);
    }
    copy[len] = '\0';
    *out_utf8 = copy;
    *out_len = len;
    return 0;
}

int32_t text_editor_node_replace_selection_utf8(text_editor_node *n,
                                                const uint8_t *utf8,
                                                size_t utf8_len) {
    uint32_t selection_min = 0;
    uint32_t selection_max = 0;
    uint32_t inserted_count = 0u;

    if (!n || (!n->document && (!n->text_tree || !n->env))) {
        return -1;
    }

    text_editor_selection_bounds(n, &selection_min, &selection_max);
    if (text_editor_replace_range_utf8(n,
                                       selection_min,
                                       selection_max,
                                       utf8,
                                       utf8_len,
                                       CROFT_EDITOR_EDIT_INSERT) != 0) {
        return -1;
    }
    if (text_editor_utf8_codepoint_count(utf8, utf8_len, &inserted_count) != 0) {
        return -1;
    }

    text_editor_collapse_selection(n, selection_min + (uint32_t)inserted_count, 0);
    text_editor_sync_cache(n);
    return 0;
}

int32_t text_editor_node_delete_selection(text_editor_node *n, int backward) {
    uint32_t selection_min = 0;
    uint32_t selection_max = 0;

    if (!n) {
        return -1;
    }

    text_editor_selection_bounds(n, &selection_min, &selection_max);
    if (selection_min == selection_max) {
        return 0;
    }

    if (n->document) {
        if (croft_editor_document_delete_range(n->document,
                                               selection_min,
                                               selection_max,
                                               backward
                                                   ? CROFT_EDITOR_EDIT_DELETE_BACKWARD
                                                   : CROFT_EDITOR_EDIT_DELETE_FORWARD) != 0) {
            return -1;
        }
    } else {
        SapTxnCtx *txn = sap_txn_begin(n->env, NULL, 0);
        if (!txn) {
            return -1;
        }
        text_editor_delete_range(txn, n, selection_min, selection_max);
        sap_txn_commit(txn);
    }

    text_editor_collapse_selection(n, selection_min, 0);
    text_editor_sync_cache(n);
    return 0;
}

int32_t text_editor_node_undo(text_editor_node *n) {
    if (!n || !n->document) {
        return -1;
    }
    if (croft_editor_document_undo(n->document) != 0) {
        return -1;
    }
    text_editor_sync_cache(n);
    return 0;
}

int32_t text_editor_node_redo(text_editor_node *n) {
    if (!n || !n->document) {
        return -1;
    }
    if (croft_editor_document_redo(n->document) != 0) {
        return -1;
    }
    text_editor_sync_cache(n);
    return 0;
}

uint32_t text_editor_node_visible_line_count_for_bounds(text_editor_node *n,
                                                        float width,
                                                        float height) {
    text_editor_layout layout;

    if (!n || text_editor_prepare_layout(n, width, height, &layout) != CROFT_EDITOR_OK) {
        return 0u;
    }
    return n->visual_row_count > 0u ? n->visual_row_count : 1u;
}

int32_t text_editor_node_offset_to_local_position_with_affinity(
    text_editor_node *n,
    float width,
    float height,
    uint32_t offset,
    croft_text_editor_caret_affinity affinity,
    float *out_x,
    float *out_y) {
    text_editor_layout layout;
    uint32_t row_number;
    const text_editor_visual_row* row;
    uint32_t line_len_bytes = 0u;
    const char* line_text = NULL;

    if (!n || !out_x || !out_y || text_editor_prepare_layout(n, width, height, &layout) != CROFT_EDITOR_OK) {
        return -1;
    }

    row_number = text_editor_visual_row_number_for_affinity(n, offset, affinity);
    row = text_editor_visual_row_at_visible_line(n, row_number);
    if (!row) {
        return -1;
    }
    if (row->model_line_number <= croft_editor_text_model_line_count(&n->text_model)) {
        line_text = croft_editor_text_model_line_utf8(&n->text_model,
                                                      row->model_line_number,
                                                      &line_len_bytes);
    }

    *out_x = layout.gutter_width + n->scroll_x + layout.text_inset_x
        + (line_text ? text_editor_visual_row_prefix_width(n, row, line_text, offset) : 0.0f);
    *out_y = layout.text_inset_y + n->scroll_y + ((float)(row_number - 1u) * n->line_height);
    return 0;
}

int32_t text_editor_node_offset_to_local_position(text_editor_node *n,
                                                  float width,
                                                  float height,
                                                  uint32_t offset,
                                                  float *out_x,
                                                  float *out_y) {
    return text_editor_node_offset_to_local_position_with_affinity(n,
                                                                   width,
                                                                   height,
                                                                   offset,
                                                                   CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING,
                                                                   out_x,
                                                                   out_y);
}

int32_t text_editor_node_hit_test_offset_with_affinity(
    text_editor_node *n,
    float width,
    float height,
    float local_x,
    float local_y,
    uint32_t *out_offset,
    croft_text_editor_caret_affinity *out_affinity) {
    text_editor_layout layout;
    const text_editor_visual_row* row;
    const char* line_text = NULL;
    uint32_t visible_line_number;
    uint32_t line_len_bytes = 0u;
    float doc_x;
    float doc_y;

    if (!n || !out_offset || text_editor_prepare_layout(n, width, height, &layout) != CROFT_EDITOR_OK) {
        return -1;
    }

    doc_x = local_x - layout.gutter_width - n->scroll_x - layout.text_inset_x;
    doc_y = local_y - layout.text_inset_y - n->scroll_y;
    if (doc_y < 0.0f) {
        *out_offset = 0u;
        if (out_affinity) {
            *out_affinity = CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING;
        }
        return 0;
    }

    visible_line_number = text_editor_clamp_u32((uint32_t)(doc_y / n->line_height) + 1u,
                                                1u,
                                                n->visual_row_count > 0u ? n->visual_row_count : 1u);
    row = text_editor_visual_row_at_visible_line(n, visible_line_number);
    if (!row) {
        return -1;
    }
    if (row->model_line_number <= croft_editor_text_model_line_count(&n->text_model)) {
        line_text = croft_editor_text_model_line_utf8(&n->text_model,
                                                      row->model_line_number,
                                                      &line_len_bytes);
    }

    *out_offset = text_editor_pick_offset_in_row_for_x(n, row, line_text, doc_x, out_affinity);
    return 0;
}

int32_t text_editor_node_hit_test_offset(text_editor_node *n,
                                         float width,
                                         float height,
                                         float local_x,
                                         float local_y,
                                         uint32_t *out_offset) {
    return text_editor_node_hit_test_offset_with_affinity(n,
                                                          width,
                                                          height,
                                                          local_x,
                                                          local_y,
                                                          out_offset,
                                                          NULL);
}

void text_editor_node_dispose(text_editor_node *n) {
    if (!n) {
        return;
    }
    if (n->base.a11y_handle) {
        croft_scene_a11y_destroy_node(n->base.a11y_handle);
        n->base.a11y_handle = (croft_scene_a11y_handle)0u;
    }
    croft_editor_text_model_dispose(&n->text_model);
    n->utf8_cache = NULL;
    n->utf8_len = 0;
    n->sel_start = 0;
    n->sel_end = 0;
    n->caret_affinity = CROFT_TEXT_EDITOR_CARET_AFFINITY_LEADING;
    n->preferred_column = 0;
    n->preferred_visual_x = 0.0f;
    n->modifiers = 0u;
    n->find_active = 0;
    n->replace_active = 0;
    n->search_focus_field = TEXT_EDITOR_SEARCH_FIELD_FIND;
    n->find_query[0] = '\0';
    n->find_query_len = 0u;
    n->replace_query[0] = '\0';
    n->replace_query_len = 0u;
    n->folded_region_count = 0u;
    n->wrap_enabled = 0u;
    n->visual_layout_dirty = 1u;
    n->visual_layout_width = 0.0f;
    n->visual_row_count = 0u;
    n->visual_row_capacity = 0u;
    free(n->visual_rows);
    n->visual_rows = NULL;
    free(n->line_first_visible_rows);
    n->line_first_visible_rows = NULL;
    free(n->line_visible_row_counts);
    n->line_visible_row_counts = NULL;
    n->line_visible_row_capacity = 0u;
    memset(&n->font_probe, 0, sizeof(n->font_probe));
    n->font_metrics_valid = 0u;
    n->baseline_offset = 0.0f;
    text_editor_clear_composition_state(n);
    free(n->composition_utf8);
    n->composition_utf8 = NULL;
    n->composition_capacity = 0u;
    free(n->decorations);
    n->decorations = NULL;
    n->decoration_count = 0u;
    n->decoration_capacity = 0u;
    croft_editor_line_cache_dispose(&n->line_cache);
    n->profiling_enabled = 0u;
    memset(&n->profile_stats, 0, sizeof(n->profile_stats));
}
