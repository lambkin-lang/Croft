#include "croft/scene.h"
#include "croft/editor_commands.h"
#include "croft/editor_document.h"
#include "croft/editor_search.h"
#include "croft/editor_status.h"
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
        gutter_label_width = host_render_measure_text(line_label,
                                                      (uint32_t)line_label_len,
                                                      gutter_font_size);
    } else {
        gutter_label_width = (float)digits * gutter_font_size * 0.6f;
    }

    out_layout->gutter_font_size = gutter_font_size;
    out_layout->status_font_size = status_font_size;
    out_layout->text_inset_x = 12.0f;
    out_layout->text_inset_y = 8.0f;
    out_layout->status_height = status_font_size + 10.0f;
    if (out_layout->status_height < 24.0f) {
        out_layout->status_height = 24.0f;
    }
    out_layout->content_height = node_height - out_layout->status_height;
    if (out_layout->content_height < te->line_height) {
        out_layout->content_height = te->line_height;
    }
    out_layout->gutter_width = gutter_label_width + 18.0f;
    if (out_layout->gutter_width < 34.0f) {
        out_layout->gutter_width = 34.0f;
    }
    out_layout->content_width = node_width - out_layout->gutter_width;
    if (out_layout->content_width < 0.0f) {
        out_layout->content_width = 0.0f;
    }
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

static uint32_t text_editor_clamp_u32(uint32_t value, uint32_t min_value, uint32_t max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void text_editor_sync_selection(text_editor_node *te) {
    uint32_t max_offset = croft_editor_text_model_codepoint_length(&te->text_model);

    if (te->sel_start > max_offset) {
        te->sel_start = max_offset;
    }
    if (te->sel_end > max_offset) {
        te->sel_end = max_offset;
    }

    te->selection = croft_editor_selection_from_offsets(&te->text_model, te->sel_start, te->sel_end);
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
    text_editor_sync_selection(te);
}

static void text_editor_sync_cache(text_editor_node *te) {
    te->text_tree = text_editor_live_text(te);

    if (!te->text_tree) {
        croft_editor_text_model_dispose(&te->text_model);
        te->utf8_cache = NULL;
        te->utf8_len = 0;
        te->sel_start = 0;
        te->sel_end = 0;
        te->preferred_column = 0;
        te->selection = croft_editor_selection_create(
            croft_editor_position_create(1, 1),
            croft_editor_position_create(1, 1)
        );
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
            return;
        }
        new_cache[utf8_len_out] = '\0';
        if (croft_editor_text_model_set_text(&te->text_model, new_cache, utf8_len_out) == CROFT_EDITOR_OK) {
            text_editor_refresh_cache(te);
        }
        free(new_cache);
    }
}

static void text_editor_set_selection(text_editor_node* te,
                                      uint32_t anchor_offset,
                                      uint32_t active_offset,
                                      int reset_preferred_column) {
    te->sel_start = anchor_offset;
    te->sel_end = active_offset;
    if (reset_preferred_column) {
        te->preferred_column = 0;
    }
    text_editor_sync_selection(te);
}

static void text_editor_collapse_selection(text_editor_node* te,
                                           uint32_t offset,
                                           int preserve_preferred_column) {
    te->sel_start = offset;
    te->sel_end = offset;
    if (!preserve_preferred_column) {
        te->preferred_column = 0;
    }
    text_editor_sync_selection(te);
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

static void text_editor_find_query_clear(text_editor_node* te)
{
    if (!te) {
        return;
    }
    te->find_query[0] = '\0';
    te->find_query_len = 0u;
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
    if (!te || !utf8 || utf8_len >= sizeof(te->find_query)) {
        return 0;
    }

    memcpy(te->find_query, utf8, utf8_len);
    te->find_query[utf8_len] = '\0';
    te->find_query_len = utf8_len;
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
    text_editor_layout layout;
    croft_editor_position position;
    uint32_t line_start_offset;
    uint32_t line_start_byte;
    uint32_t cursor_byte;
    uint32_t line_len_bytes = 0u;
    const char* line_text;
    float line_top;
    float visible_top;
    float visible_bottom;
    float cursor_doc_x;
    float visible_left;
    float visible_right;

    if (!te) {
        return;
    }

    text_editor_compute_layout(te, te->base.sx, te->base.sy, &layout);
    position = croft_editor_text_model_get_position_at(&te->text_model, te->sel_end);
    line_top = (float)(position.line_number - 1u) * te->line_height;
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

    line_start_offset = croft_editor_text_model_line_start_offset(&te->text_model, position.line_number);
    line_start_byte = croft_editor_text_model_byte_offset_at(&te->text_model, line_start_offset);
    cursor_byte = croft_editor_text_model_byte_offset_at(&te->text_model, te->sel_end) - line_start_byte;
    line_text = croft_editor_text_model_line_utf8(&te->text_model, position.line_number, &line_len_bytes);
    cursor_doc_x = layout.text_inset_x + host_render_measure_text(line_text, cursor_byte, te->font_size);
    visible_left = -te->scroll_x;
    visible_right = visible_left + layout.content_width;
    if (cursor_doc_x < visible_left + layout.text_inset_x) {
        te->scroll_x = -(cursor_doc_x - layout.text_inset_x);
    } else if (cursor_doc_x > visible_right - 24.0f) {
        te->scroll_x = -(cursor_doc_x - (layout.content_width - 24.0f));
    }
    if (te->scroll_x > 0.0f) {
        te->scroll_x = 0.0f;
    }
}

static int32_t text_editor_apply_search_match(text_editor_node* te,
                                              const croft_editor_search_match* match)
{
    if (!te || !match) {
        return -1;
    }

    text_editor_set_selection(te, match->start_offset, match->end_offset, 1);
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

static int text_editor_find_query_backspace(text_editor_node* te)
{
    uint32_t len;

    if (!te || te->find_query_len == 0u) {
        return 0;
    }

    len = te->find_query_len;
    while (len > 0u) {
        len--;
        if ((((unsigned char)te->find_query[len]) & 0xC0u) != 0x80u) {
            te->find_query[len] = '\0';
            te->find_query_len = len;
            return 1;
        }
    }

    text_editor_find_query_clear(te);
    return 1;
}

static int text_editor_find_query_append_codepoint(text_editor_node* te, uint32_t codepoint)
{
    char encoded[4];
    uint32_t encoded_len = 0u;

    if (!te || codepoint < 0x20u || codepoint == 0x7Fu) {
        return 0;
    }
    if (text_editor_utf8_encode_codepoint(codepoint, encoded, &encoded_len) != 0) {
        return 0;
    }
    if (te->find_query_len + encoded_len >= sizeof(te->find_query)) {
        return 0;
    }

    memcpy(te->find_query + te->find_query_len, encoded, encoded_len);
    te->find_query_len += encoded_len;
    te->find_query[te->find_query_len] = '\0';
    return 1;
}

static void text_editor_draw_search_match(const text_editor_node* te,
                                          const text_editor_layout* layout,
                                          const croft_editor_search_match* match,
                                          uint32_t color_rgba)
{
    croft_editor_position start_position;
    croft_editor_position end_position;
    uint32_t line_start_offset;
    uint32_t line_start_byte;
    uint32_t start_byte;
    uint32_t end_byte;
    uint32_t line_len_bytes = 0u;
    const char* line_text;
    float current_y;
    float x1;
    float x2;

    if (!te || !layout || !match) {
        return;
    }

    start_position = croft_editor_text_model_get_position_at(&te->text_model, match->start_offset);
    end_position = croft_editor_text_model_get_position_at(&te->text_model, match->end_offset);
    if (start_position.line_number != end_position.line_number) {
        return;
    }

    current_y = te->font_size + ((float)(start_position.line_number - 1u) * te->line_height);
    if (current_y + te->scroll_y < 0.0f
            || current_y - te->font_size + te->scroll_y > layout->content_height) {
        return;
    }

    line_start_offset = croft_editor_text_model_line_start_offset(&te->text_model, start_position.line_number);
    line_start_byte = croft_editor_text_model_byte_offset_at(&te->text_model, line_start_offset);
    start_byte = croft_editor_text_model_byte_offset_at(&te->text_model, match->start_offset) - line_start_byte;
    end_byte = croft_editor_text_model_byte_offset_at(&te->text_model, match->end_offset) - line_start_byte;
    line_text = croft_editor_text_model_line_utf8(&te->text_model, start_position.line_number, &line_len_bytes);
    x1 = layout->text_inset_x + host_render_measure_text(line_text, start_byte, te->font_size);
    x2 = layout->text_inset_x + host_render_measure_text(line_text, end_byte, te->font_size);
    host_render_draw_rect(x1, current_y - te->font_size, x2 - x1, te->line_height, color_rgba);
}

static void text_editor_draw_search_matches(text_editor_node* te,
                                            const text_editor_layout* layout)
{
    croft_editor_search_match match = {0};
    uint32_t selection_min = 0u;
    uint32_t selection_max = 0u;
    const char* needle = NULL;
    uint32_t needle_len = 0u;
    uint32_t color_rgba = 0u;
    uint32_t search_from = 0u;

    if (!te || !layout) {
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
            text_editor_draw_search_match(te, layout, &match, color_rgba);
        }
        if (match.start_offset == match.end_offset) {
            break;
        }
        search_from = match.start_offset + 1u;
        if (search_from >= croft_editor_text_model_codepoint_length(&te->text_model)) {
            break;
        }
    }
}

static void text_editor_draw_find_overlay(const text_editor_node* te,
                                          const text_editor_layout* layout,
                                          float node_width)
{
    const char* query_text = (te && te->find_query_len > 0u) ? te->find_query : "Find...";
    uint32_t query_color = (te && te->find_query_len > 0u) ? 0x1E2A38FF : 0x7A8694FF;
    uint32_t query_len = (uint32_t)strlen(query_text);
    float box_width = 220.0f;
    float box_height = 42.0f;
    float x = node_width - box_width - 12.0f;
    float y = 12.0f;

    (void)layout;

    if (!te || !te->find_active) {
        return;
    }

    host_render_draw_rect(x, y, box_width, box_height, 0xFFFFFFFF);
    host_render_draw_rect(x, y, box_width, 1.0f, 0xD6DCE4FF);
    host_render_draw_rect(x, y + box_height - 1.0f, box_width, 1.0f, 0xD6DCE4FF);
    host_render_draw_rect(x, y, 1.0f, box_height, 0xD6DCE4FF);
    host_render_draw_rect(x + box_width - 1.0f, y, 1.0f, box_height, 0xD6DCE4FF);
    host_render_draw_text(x + 10.0f, y + 14.0f, "Find", 4u, 11.0f, 0x667484FF);
    host_render_draw_text(x + 10.0f, y + 31.0f, query_text, query_len, 13.0f, query_color);
}

static void text_editor_draw(scene_node *n, render_ctx *rc) {
    text_editor_node *te = (text_editor_node *)n;
    text_editor_layout layout;
    croft_editor_status_snapshot status_snapshot;
    char status_text[96];
    uint32_t line_count;
    uint32_t selection_min = 0;
    uint32_t selection_max = 0;
    uint32_t cursor_offset = te->sel_end;
    uint32_t current_line = text_editor_current_line_number(te);
    uint32_t line_number;

    text_editor_compute_layout(te, n->sx, n->sy, &layout);
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

    for (line_number = 1; line_number <= line_count; line_number++) {
        float current_y = te->font_size + ((float)(line_number - 1u) * te->line_height);

        if (current_y + te->scroll_y >= 0.0f
                && current_y - te->font_size + te->scroll_y <= layout.content_height) {
            if (line_number == current_line) {
                host_render_draw_rect(-te->scroll_x,
                                      current_y - te->font_size,
                                      layout.content_width,
                                      te->line_height,
                                      0xE2ECF8FF);
            }
            host_render_draw_rect(-te->scroll_x, current_y, layout.content_width, 1.0f, 0x00000012);
        }
    }

    text_editor_draw_search_matches(te, &layout);

    for (line_number = 1; line_number <= line_count; line_number++) {
        uint32_t line_start_offset = croft_editor_text_model_line_start_offset(&te->text_model, line_number);
        uint32_t line_end_offset = croft_editor_text_model_line_end_offset(&te->text_model, line_number);
        uint32_t line_start_byte = croft_editor_text_model_byte_offset_at(&te->text_model, line_start_offset);
        uint32_t line_len_bytes = 0;
        const char* line_text = croft_editor_text_model_line_utf8(&te->text_model, line_number, &line_len_bytes);
        float current_y = te->font_size + ((float)(line_number - 1u) * te->line_height);

        if (current_y + te->scroll_y >= 0.0f
                && current_y - te->font_size + te->scroll_y <= layout.content_height) {
            if (selection_min != selection_max
                    && selection_min < line_end_offset
                    && selection_max > line_start_offset) {
                uint32_t highlight_start = selection_min > line_start_offset ? selection_min : line_start_offset;
                uint32_t highlight_end = selection_max < line_end_offset ? selection_max : line_end_offset;
                uint32_t highlight_start_bytes =
                    croft_editor_text_model_byte_offset_at(&te->text_model, highlight_start) - line_start_byte;
                uint32_t highlight_end_bytes =
                    croft_editor_text_model_byte_offset_at(&te->text_model, highlight_end) - line_start_byte;
                float x1 = layout.text_inset_x
                    + host_render_measure_text(line_text, highlight_start_bytes, te->font_size);
                float x2 = layout.text_inset_x
                    + host_render_measure_text(line_text, highlight_end_bytes, te->font_size);
                host_render_draw_rect(x1, current_y - te->font_size, x2 - x1, te->line_height, 0x5B9FE0CC);
            }

            if (selection_min == selection_max
                    && cursor_offset >= line_start_offset
                    && cursor_offset <= line_end_offset) {
                int millis = (int)(rc->time * 1000.0);
                if ((millis / 500) % 2 == 0) {
                    uint32_t cursor_bytes =
                        croft_editor_text_model_byte_offset_at(&te->text_model, cursor_offset) - line_start_byte;
                    float cursor_x = layout.text_inset_x
                        + host_render_measure_text(line_text, cursor_bytes, te->font_size);
                    host_render_draw_rect(cursor_x, current_y - te->font_size, 2.0f, te->line_height, 0x000000FF);
                }
            }

            if (line_len_bytes > 0u) {
                host_render_draw_text(layout.text_inset_x,
                                      current_y,
                                      line_text,
                                      line_len_bytes,
                                      te->font_size,
                                      rc->fg_color);
            }
        }
    }
    host_render_restore();

    host_render_save();
    host_render_clip_rect(0, 0, layout.gutter_width, layout.content_height);
    host_render_translate(0.0f, layout.text_inset_y + te->scroll_y);
    for (line_number = 1; line_number <= line_count; line_number++) {
        char line_label[16];
        int line_label_len;
        float current_y = te->font_size + ((float)(line_number - 1u) * te->line_height);
        float label_width;
        uint32_t color = (line_number == current_line) ? 0x17385EFF : 0x798493FF;

        if (current_y + te->scroll_y < 0.0f
                || current_y - te->font_size + te->scroll_y > layout.content_height) {
            continue;
        }

        line_label_len = snprintf(line_label, sizeof(line_label), "%u", line_number);
        label_width = (line_label_len > 0)
            ? host_render_measure_text(line_label,
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
}

static void text_editor_hit_test(scene_node *n, float x, float y, hit_result *out) {
    out->node = n;
    out->local_x = x;
    out->local_y = y;
}

static void text_editor_update_accessibility(scene_node *n) {
    // Phase 4 will map this to ROLE_TEXT_AREA
}

static uint32_t text_editor_hit_index(text_editor_node *te, float lx, float ly) {
    text_editor_layout layout;
    uint32_t line_number;
    uint32_t line_start_offset;
    uint32_t line_end_offset;
    uint32_t line_start_byte;
    uint32_t line_len_bytes = 0;
    const char* line_text;
    uint32_t best_offset;
    uint32_t offset;
    float best_distance;

    float doc_x;
    float doc_y;

    text_editor_compute_layout(te, te->base.sx, te->base.sy, &layout);
    doc_x = lx - layout.gutter_width - te->scroll_x - layout.text_inset_x;
    doc_y = ly - layout.text_inset_y - te->scroll_y;

    if (doc_y < 0.0f) {
        return 0;
    }

    line_number = (uint32_t)(doc_y / te->line_height) + 1u;
    line_number = text_editor_clamp_u32(
        line_number,
        1u,
        croft_editor_text_model_line_count(&te->text_model)
    );
    line_start_offset = croft_editor_text_model_line_start_offset(&te->text_model, line_number);
    line_end_offset = croft_editor_text_model_line_end_offset(&te->text_model, line_number);
    line_start_byte = croft_editor_text_model_byte_offset_at(&te->text_model, line_start_offset);
    line_text = croft_editor_text_model_line_utf8(&te->text_model, line_number, &line_len_bytes);

    best_offset = line_start_offset;
    best_distance = doc_x < 0.0f ? -doc_x : doc_x;
    for (offset = line_start_offset; offset <= line_end_offset; offset++) {
        uint32_t byte_length =
            croft_editor_text_model_byte_offset_at(&te->text_model, offset) - line_start_byte;
        float width = host_render_measure_text(line_text, byte_length, te->font_size);
        float distance = width >= doc_x ? (width - doc_x) : (doc_x - width);
        if (offset == line_start_offset || distance <= best_distance) {
            best_distance = distance;
            best_offset = offset;
        }
    }

    return best_offset;
}

static void text_editor_mouse_event(scene_node *n, int action, float local_x, float local_y) {
    text_editor_node *te = (text_editor_node *)n;
    text_editor_layout layout;

    text_editor_compute_layout(te, n->sx, n->sy, &layout);
    if (local_y >= layout.content_height) {
        if (action == 0 || action == 2) {
            te->is_selecting = 0;
        }
        return;
    }
    if (local_x < layout.gutter_width) {
        local_x = layout.gutter_width;
    }

    if (action == 1) { // Down
        uint32_t hit_index = text_editor_hit_index(te, local_x, local_y);
        text_editor_break_coalescing(te);
        te->is_selecting = 1;
        text_editor_set_selection(te, hit_index, hit_index, 1);
    } else if (action == 3 && te->is_selecting) { // Drag
        text_editor_break_coalescing(te);
        text_editor_set_selection(te, te->sel_start, text_editor_hit_index(te, local_x, local_y), 1);
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
        if (text_editor_find_query_append_codepoint(te, codepoint)) {
            text_editor_find_refresh_from_cursor(te);
        }
        return;
    }

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
            text_editor_node_find_activate(te);
            return;
        }
        if ((key == CROFT_KEY_ENTER
                    || key == CROFT_KEY_KP_ENTER_OLD
                    || key == CROFT_KEY_KP_ENTER
                    || (command_mode && key == CROFT_KEY_G))) {
            if (selecting) {
                text_editor_node_find_previous(te);
            } else {
                text_editor_node_find_next(te);
            }
            return;
        }
        if (key == CROFT_KEY_BACKSPACE || key == CROFT_KEY_DELETE) {
            if (text_editor_find_query_backspace(te)) {
                if (text_editor_find_has_query(te)) {
                    text_editor_find_refresh_from_cursor(te);
                }
            }
            return;
        }
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
        text_editor_sync_selection(te);
    } else if (key == CROFT_KEY_UP || key == CROFT_KEY_DOWN) {
        text_editor_break_coalescing(te);
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
        text_editor_sync_selection(te);
    } else if (key == CROFT_KEY_HOME) {
        text_editor_break_coalescing(te);
        croft_editor_command_move_home(&te->text_model,
                                       &te->sel_start,
                                       &te->sel_end,
                                       &te->preferred_column,
                                       selecting);
        text_editor_sync_selection(te);
    } else if (key == CROFT_KEY_END) {
        text_editor_break_coalescing(te);
        croft_editor_command_move_end(&te->text_model,
                                      &te->sel_start,
                                      &te->sel_end,
                                      &te->preferred_column,
                                      selecting);
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
    n->line_height = 22.0f;
    n->sel_start = 0;
    n->sel_end = 0;
    n->preferred_column = 0;
    n->modifiers = 0u;
    n->is_selecting = 0;
    n->find_active = 0;
    n->find_query[0] = '\0';
    n->find_query_len = 0u;
    croft_editor_text_model_init(&n->text_model);
    n->selection = croft_editor_selection_create(
        croft_editor_position_create(1, 1),
        croft_editor_position_create(1, 1)
    );
    
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

void text_editor_node_select_all(text_editor_node *n) {
    if (!n) {
        return;
    }
    text_editor_set_selection(n,
                              0u,
                              croft_editor_text_model_codepoint_length(&n->text_model),
                              1);
}

int text_editor_node_is_find_active(const text_editor_node *n) {
    return n ? n->find_active : 0;
}

void text_editor_node_find_activate(text_editor_node *n) {
    if (!n) {
        return;
    }
    n->find_active = 1;
    if (!text_editor_copy_selection_into_find_query(n) && !text_editor_find_has_query(n)) {
        text_editor_find_query_clear(n);
    }
    if (text_editor_find_has_query(n)) {
        text_editor_find_refresh_from_cursor(n);
    }
}

void text_editor_node_find_close(text_editor_node *n) {
    if (!n) {
        return;
    }
    n->find_active = 0;
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
    text_editor_selection_bounds(n, &selection_min, NULL);
    return text_editor_find_previous_before(n, selection_min, 1);
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

    if (!n || !n->document) {
        return -1;
    }

    text_editor_selection_bounds(n, &selection_min, &selection_max);
    if (croft_editor_document_replace_range_with_utf8(n->document,
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
    n->preferred_column = 0;
    n->modifiers = 0u;
    n->find_active = 0;
    n->find_query[0] = '\0';
    n->find_query_len = 0u;
}
