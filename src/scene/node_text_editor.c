#include "croft/scene.h"
#include "croft/editor_commands.h"
#include "croft/editor_document.h"
#include "croft/host_ui.h"
#include "croft/host_render.h"
#include <sapling/txn.h>
#include <sapling/text.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum {
    CROFT_KEY_RELEASE = 0,
    CROFT_KEY_BACKSPACE = 259,
    CROFT_KEY_DELETE = 261,
    CROFT_KEY_RIGHT = 262,
    CROFT_KEY_LEFT = 263,
    CROFT_KEY_DOWN = 264,
    CROFT_KEY_UP = 265,
    CROFT_KEY_HOME = 268,
    CROFT_KEY_END = 269,
    CROFT_KEY_ENTER = 257,
    CROFT_KEY_Y = 89,
    CROFT_KEY_Z = 90,
    CROFT_KEY_KP_ENTER_OLD = 284,
    CROFT_KEY_KP_ENTER = 335
};

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
        *out_min = start;
        *out_max = end;
    } else {
        *out_min = end;
        *out_max = start;
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

static void text_editor_draw(scene_node *n, render_ctx *rc) {
    text_editor_node *te = (text_editor_node *)n;
    uint32_t line_count;
    uint32_t selection_min = 0;
    uint32_t selection_max = 0;
    uint32_t cursor_offset = te->sel_end;
    uint32_t line_number;

    host_render_draw_rect(0, 0, n->sx, n->sy, rc->bg_color);

    host_render_save();
    host_render_clip_rect(0, 0, n->sx, n->sy);
    host_render_translate(te->scroll_x, te->scroll_y);

    text_editor_selection_bounds(te, &selection_min, &selection_max);
    line_count = croft_editor_text_model_line_count(&te->text_model);

    for (line_number = 1; line_number <= line_count; line_number++) {
        uint32_t line_start_offset = croft_editor_text_model_line_start_offset(&te->text_model, line_number);
        uint32_t line_end_offset = croft_editor_text_model_line_end_offset(&te->text_model, line_number);
        uint32_t line_start_byte = croft_editor_text_model_byte_offset_at(&te->text_model, line_start_offset);
        uint32_t line_len_bytes = 0;
        const char* line_text = croft_editor_text_model_line_utf8(&te->text_model, line_number, &line_len_bytes);
        float current_y = te->font_size + ((float)(line_number - 1u) * te->line_height);

        if (current_y + te->scroll_y >= 0.0f && current_y - te->font_size + te->scroll_y <= n->sy) {
            if (selection_min != selection_max
                    && selection_min < line_end_offset
                    && selection_max > line_start_offset) {
                uint32_t highlight_start = selection_min > line_start_offset ? selection_min : line_start_offset;
                uint32_t highlight_end = selection_max < line_end_offset ? selection_max : line_end_offset;
                uint32_t highlight_start_bytes =
                    croft_editor_text_model_byte_offset_at(&te->text_model, highlight_start) - line_start_byte;
                uint32_t highlight_end_bytes =
                    croft_editor_text_model_byte_offset_at(&te->text_model, highlight_end) - line_start_byte;
                float x1 = host_render_measure_text(line_text, highlight_start_bytes, te->font_size);
                float x2 = host_render_measure_text(line_text, highlight_end_bytes, te->font_size);
                host_render_draw_rect(x1, current_y - te->font_size, x2 - x1, te->line_height, 0x4B9CE2FF);
            }

            if (selection_min == selection_max
                    && cursor_offset >= line_start_offset
                    && cursor_offset <= line_end_offset) {
                int millis = (int)(rc->time * 1000.0);
                if ((millis / 500) % 2 == 0) {
                    uint32_t cursor_bytes =
                        croft_editor_text_model_byte_offset_at(&te->text_model, cursor_offset) - line_start_byte;
                    float cursor_x = host_render_measure_text(line_text, cursor_bytes, te->font_size);
                    host_render_draw_rect(cursor_x, current_y - te->font_size, 2.0f, te->line_height, 0x000000FF);
                }
            }

            host_render_draw_rect(0, current_y, n->sx, 1.0f, 0x0000FF33);

            if (line_len_bytes > 0u) {
                host_render_draw_text(0, current_y, line_text, line_len_bytes, te->font_size, rc->fg_color);
            }
        }
    }

    host_render_draw_rect(0, 0, 1.0f, n->sy, 0x0000FF33);
    host_render_restore();
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
    uint32_t line_number;
    uint32_t line_start_offset;
    uint32_t line_end_offset;
    uint32_t line_start_byte;
    uint32_t line_len_bytes = 0;
    const char* line_text;
    uint32_t best_offset;
    uint32_t offset;
    float best_distance;

    float doc_x = lx - te->scroll_x;
    float doc_y = ly - te->scroll_y;

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
    n->font_size = 36.0f;     // Match tgfx default in host_render
    n->line_height = 42.0f;
    n->sel_start = 0;
    n->sel_end = 0;
    n->preferred_column = 0;
    n->modifiers = 0u;
    n->is_selecting = 0;
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
}
