#include "croft/scene.h"
#include "croft/host_render.h"
#include "croft/host_a11y.h"
#include <sapling/text.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void text_editor_sync_cache(text_editor_node *te) {
    if (!te->text_tree) {
        if (te->utf8_cache) {
            free(te->utf8_cache);
            te->utf8_cache = NULL;
        }
        te->utf8_len = 0;
        return;
    }
    
    // For Phase 1 MVP, we export the entire string to a UTF-8 buffer and cache it.
    size_t utf8_len_out = 0;
    // We assume the text tree isn't full of non-codepoint handles that need literals/trees for now,
    // or we resolve them with NULL meaning they fail. 
    // We use the strict text_to_utf8 on just the code points.
    text_utf8_length((Text*)te->text_tree, &utf8_len_out);
    
    char *new_cache = (char *)malloc(utf8_len_out + 1);
    if (new_cache) {
        text_to_utf8((Text*)te->text_tree, (uint8_t*)new_cache, utf8_len_out + 1, &utf8_len_out);
        new_cache[utf8_len_out] = '\0';
        
        if (te->utf8_cache) free(te->utf8_cache);
        te->utf8_cache = new_cache;
        te->utf8_len = (uint32_t)utf8_len_out;
    }
}

static void text_editor_draw(scene_node *n, render_ctx *rc) {
    text_editor_node *te = (text_editor_node *)n;
    
    // Draw background
    host_render_draw_rect(0, 0, n->sx, n->sy, rc->bg_color);
    
    if (!te->utf8_cache || te->utf8_len == 0) return;
    
    // Save coordinate system before clipping
    host_render_save();
    
    // Clip strictly to the text editor bounding box to prevent text scrolling out
    host_render_clip_rect(0, 0, n->sx, n->sy);
    
    // Translate text by the scroll offset
    host_render_translate(te->scroll_x, te->scroll_y);
    
    // Naive line-by-line render
    const char *line_start = te->utf8_cache;
    const char *end = te->utf8_cache + te->utf8_len;
    float current_y = te->font_size; // Baseline
    
    while (line_start < end) {
        const char *newline = strchr(line_start, '\n');
        size_t len = newline ? (size_t)(newline - line_start) : (size_t)(end - line_start);
        
        // Culling: only draw if inside the viewport rect
        if (current_y + te->scroll_y >= 0 && current_y - te->font_size + te->scroll_y <= n->sy) {
            uint32_t line_start_idx = (uint32_t)(line_start - te->utf8_cache);
            uint32_t line_end_idx = line_start_idx + (uint32_t)len;
            
            uint32_t s_min = te->sel_start < te->sel_end ? te->sel_start : te->sel_end;
            uint32_t s_max = te->sel_start > te->sel_end ? te->sel_start : te->sel_end;
            
            if (s_min != s_max && s_min < line_end_idx && s_max > line_start_idx) {
                uint32_t h_start = s_min > line_start_idx ? s_min : line_start_idx;
                uint32_t h_end = s_max < line_end_idx ? s_max : line_end_idx;
                float x1 = host_render_measure_text(line_start, h_start - line_start_idx, te->font_size);
                float x2 = host_render_measure_text(line_start, h_end - line_start_idx, te->font_size);
                // Draw selection highlight blue: 0x4B9CE2FF (opaque blue)
                // Y runs from baseline down? Wait, rect draws from top-left.
                host_render_draw_rect(x1, current_y - te->font_size, x2 - x1, te->line_height, 0x4B9CE2FF);
            }
            if (s_min == s_max && s_min >= line_start_idx && s_min <= line_end_idx) { // Cursor is here
                // Blink at 1Hz (500ms on, 500ms off)
                // Need fmod from math.h, but we can avoid it with cast:
                int millis = (int)(rc->time * 1000.0);
                if ((millis / 500) % 2 == 0) {
                    float cx = host_render_measure_text(line_start, s_min - line_start_idx, te->font_size);
                    // Draw a 2px wide cursor
                    host_render_draw_rect(cx, current_y - te->font_size, 2.0f, te->line_height, 0x000000FF);
                }
            }
            
            // Draw baseline guideline
            host_render_draw_rect(0, current_y, n->sx, 1.0f, 0x0000FF33);
            
            if (len > 0) {
                host_render_draw_text(0, current_y, line_start, (uint32_t)len, te->font_size, rc->fg_color);
            }
        }
        
        current_y += te->line_height;
        if (!newline) break;
        line_start = newline + 1;
    }
    
    // Draw vertical margin guideline at X=0
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
    if (!te->utf8_cache || te->utf8_len == 0) return 0;
    
    float doc_x = lx - te->scroll_x;
    float doc_y = ly - te->scroll_y;
    
    if (doc_y < 0) return 0;
    
    uint32_t line_index = (uint32_t)(doc_y / te->line_height);
    
    const char *p = te->utf8_cache;
    const char *end = p + te->utf8_len;
    uint32_t current_line = 0;
    while (p < end && current_line < line_index) {
        const char *nl = strchr(p, '\n');
        if (!nl) { p = end; break; }
        p = nl + 1;
        current_line++;
    }
    if (p >= end) return te->utf8_len;
    
    const char *line_start = p;
    const char *nl = strchr(p, '\n');
    size_t line_len = nl ? (size_t)(nl - p) : (size_t)(end - p);
    
    uint32_t best_idx = 0;
    float prev_width = 0.0f;
    for (uint32_t i = 0; i <= line_len; i++) {
        float width = host_render_measure_text(line_start, i, te->font_size);
        if (width >= doc_x) {
            if (i > 0 && (doc_x - prev_width) < (width - doc_x)) {
                best_idx = i - 1;
            } else {
                best_idx = i;
            }
            break;
        }
        prev_width = width;
        best_idx = i;
    }
    
    return (uint32_t)(line_start - te->utf8_cache) + best_idx;
}

static void text_editor_mouse_event(scene_node *n, int action, float local_x, float local_y) {
    text_editor_node *te = (text_editor_node *)n;
    if (action == 1) { // Down
        te->is_selecting = 1;
        te->sel_start = text_editor_hit_index(te, local_x, local_y);
        te->sel_end = te->sel_start;
    } else if (action == 3 && te->is_selecting) { // Drag
        te->sel_end = text_editor_hit_index(te, local_x, local_y);
    } else if (action == 0 || action == 2) { // Up
        te->is_selecting = 0;
    }
}

static void text_editor_char_event(scene_node *n, uint32_t codepoint) {
    text_editor_node *te = (text_editor_node *)n;
    if (!te->text_tree || !te->env) {
        printf("DEBUG: char_event failed due to missing text_tree or env\n");
        return;
    }
    
    SapTxnCtx *txn = sap_txn_begin(te->env, NULL, 0);
    if (!txn) {
        printf("DEBUG: char_event failed to begin txn\n");
        return;
    }
    
    // Delete selection if any
    if (te->sel_start != te->sel_end) {
        uint32_t s_min = te->sel_start < te->sel_end ? te->sel_start : te->sel_end;
        uint32_t s_max = te->sel_start > te->sel_end ? te->sel_start : te->sel_end;
        uint32_t count = s_max - s_min;
        for (uint32_t i = 0; i < count; i++) {
            text_delete(txn, te->text_tree, s_min, NULL);
        }
        te->sel_start = s_min;
        te->sel_end = s_min;
    }
    
    int err = text_insert(txn, te->text_tree, te->sel_start, codepoint);
    if (err != 0) {
        printf("DEBUG: text_insert failed with err=%d\n", err);
    }
    sap_txn_commit(txn);
    
    te->sel_start++;
    te->sel_end = te->sel_start;
    
    text_editor_sync_cache(te);
}

static void text_editor_key_event(scene_node *n, int key, int action) {
    text_editor_node *te = (text_editor_node *)n;
    if (action == 0) return; // ignore release
    
    if (key == 259) { // GLFW_KEY_BACKSPACE
        SapTxnCtx *txn = sap_txn_begin(te->env, NULL, 0);
        if (!txn) return;
        
        if (te->sel_start != te->sel_end) {
            uint32_t s_min = te->sel_start < te->sel_end ? te->sel_start : te->sel_end;
            uint32_t s_max = te->sel_start > te->sel_end ? te->sel_start : te->sel_end;
            uint32_t count = s_max - s_min;
            for (uint32_t i = 0; i < count; i++) {
                text_delete(txn, te->text_tree, s_min, NULL);
            }
            te->sel_start = s_min;
            te->sel_end = s_min;
        } else if (te->sel_start > 0) {
            te->sel_start--;
            te->sel_end = te->sel_start;
            int err = text_delete(txn, te->text_tree, te->sel_start, NULL);
            if (err != 0) {
                printf("DEBUG: text_delete failed with err=%d\n", err);
            }
        }
        
        sap_txn_commit(txn);
        text_editor_sync_cache(te);
    }
    else if (key == 263) { // GLFW_KEY_LEFT
        if (te->sel_start > 0) {
            te->sel_start--;
            te->sel_end = te->sel_start;
        }
    }
    else if (key == 262) { // GLFW_KEY_RIGHT
        if (te->sel_start < te->utf8_len) { // simplified for single-code-point characters MVP
            te->sel_start++;
            te->sel_end = te->sel_start;
        }
    }
    else if (key == 257 || key == 284) { // GLFW_KEY_ENTER / KP_ENTER
        text_editor_char_event(n, '\n');
    }
    
    // Ensure terminal cursor doesn't magically blink out of existence on moving past end of line
    if (te->sel_start > te->utf8_len) te->sel_start = te->utf8_len;
    if (te->sel_end > te->utf8_len) te->sel_end = te->utf8_len;
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
    n->scroll_x = 0;
    n->scroll_y = 0;
    n->utf8_cache = NULL;
    n->utf8_len = 0;
    n->font_size = 36.0f;     // Match tgfx default in host_render
    n->line_height = 42.0f;
    
    text_editor_sync_cache(n);
    
    host_a11y_node_config cfg = {
        .x = x, .y = y, .width = sx, .height = sy,
        .label = "Code Editor",
        .os_specific_mixin = NULL
    };
    n->base.a11y_handle = host_a11y_create_node(ROLE_TEXT, &cfg); // Standard text for now until ROLE_TEXT_AREA map
}

void text_editor_node_set_text(text_editor_node *n, struct Text *text_tree) {
    n->text_tree = text_tree;
    text_editor_sync_cache(n);
}
