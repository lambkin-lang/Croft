#ifndef CROFT_SCENE_H
#define CROFT_SCENE_H

#include "croft/platform.h"
#include "croft/editor_line_cache.h"
#include "croft/editor_syntax.h"
#include "croft/editor_text_model.h"
#include "croft/scene_a11y_bridge.h"

#include <stddef.h>

//
// Render Context
//

typedef struct render_ctx {
    uint32_t bg_color;
    uint32_t fg_color;
    double time;
} render_ctx;

//
// Hit Test Result
//

typedef struct scene_node scene_node;

typedef struct hit_result {
    scene_node *node;
    float local_x;
    float local_y;
} hit_result;

//
// Scene Node Virtual Table
//

typedef struct scene_node_vtbl {
    void (*draw)(scene_node *n, render_ctx *rc);
    void (*hit_test)(scene_node *n, float x, float y, hit_result *out);
    void (*update_accessibility)(scene_node *n);
    void (*transform_coords)(scene_node *n, float *x, float *y);
    void (*on_mouse_event)(scene_node *n, int action, float local_x, float local_y);
    void (*on_key_event)(scene_node *n, int key, int action);
    void (*on_char_event)(scene_node *n, uint32_t codepoint);
} scene_node_vtbl;

//
// Scene Node Base Class
//

struct scene_node {
    float x;
    float y;
    float sx;
    float sy;
    uint32_t flags;
    scene_node_vtbl *vtbl;
    scene_node *first_child;
    scene_node *next_sibling;
    croft_scene_a11y_handle a11y_handle;
};

//
// Core Scene API
//

void scene_node_init(scene_node *n, scene_node_vtbl *vtbl, float x, float y, float sx, float sy);
void scene_node_add_child(scene_node *parent, scene_node *child);

void scene_node_draw_tree(scene_node *root, render_ctx *rc);
void scene_node_hit_test_tree(scene_node *root, float x, float y, hit_result *out);

//
// Built-in Nodes
//

// Viewport Node (Container that clips and translates)
typedef struct viewport_node {
    scene_node base;
    float scroll_x;
    float scroll_y;
    float scale;
} viewport_node;

void viewport_node_init(viewport_node *n, float x, float y, float sx, float sy);

// Code Block Node (Terminal leaf that draws text)
typedef struct code_block_node {
    scene_node base;
    const char *text;
    uint32_t text_len;
} code_block_node;

void code_block_node_init(code_block_node *n, float x, float y, float sx, float sy, const char *text);

// Text Editor Node (Draws text from a persistent Sapling Text engine)
struct Text;
struct SapEnv;
struct croft_editor_document;
struct text_editor_visual_row;

typedef struct croft_text_editor_profile_snapshot {
    uint32_t enabled;
    uint64_t draw_calls;
    uint64_t draw_total_usec;
    uint64_t layout_calls;
    uint64_t layout_total_usec;
    uint64_t visible_line_count_calls;
    uint64_t visible_line_count_total_usec;
    uint64_t visible_line_count_steps;
    uint64_t visible_line_lookup_calls;
    uint64_t visible_line_lookup_total_usec;
    uint64_t visible_line_lookup_steps;
    uint64_t model_line_lookup_calls;
    uint64_t model_line_lookup_total_usec;
    uint64_t model_line_lookup_steps;
    uint64_t ensure_cursor_visible_calls;
    uint64_t ensure_cursor_visible_total_usec;
    uint64_t search_draw_calls;
    uint64_t search_draw_total_usec;
    uint64_t bracket_draw_calls;
    uint64_t bracket_draw_total_usec;
    uint64_t hit_index_calls;
    uint64_t hit_index_total_usec;
    uint64_t hit_index_offsets_scanned;
    uint64_t measure_text_calls;
    uint64_t measure_text_total_usec;
    uint64_t measure_text_total_bytes;
    uint64_t background_pass_lines;
    uint64_t text_pass_lines;
    uint64_t gutter_pass_lines;
} croft_text_editor_profile_snapshot;

typedef struct text_editor_node {
    scene_node base;
    struct SapEnv *env;
    struct Text *text_tree;
    struct croft_editor_document *document;
    float scroll_x;
    float scroll_y;
    // Cache for naive line rendering MVP
    char *utf8_cache;
    uint32_t utf8_len;
    float font_size;
    float line_height;
    uint32_t sel_start;
    uint32_t sel_end;
    uint32_t preferred_column;
    float preferred_visual_x;
    uint32_t modifiers;
    int is_selecting;
    int find_active;
    int replace_active;
    uint32_t search_focus_field;
    char find_query[128];
    uint32_t find_query_len;
    char replace_query[128];
    uint32_t replace_query_len;
    uint32_t folded_region_count;
    struct {
        uint32_t start_line_number;
        uint32_t end_line_number;
    } folded_regions[64];
    croft_editor_text_model text_model;
    croft_editor_line_cache line_cache;
    croft_editor_selection selection;
    croft_editor_syntax_language syntax_language;
    uint32_t wrap_enabled;
    uint32_t visual_layout_dirty;
    float visual_layout_width;
    struct text_editor_visual_row* visual_rows;
    uint32_t visual_row_count;
    uint32_t visual_row_capacity;
    uint32_t* line_first_visible_rows;
    uint32_t* line_visible_row_counts;
    uint32_t line_visible_row_capacity;
    uint32_t profiling_enabled;
    croft_text_editor_profile_snapshot profile_stats;
} text_editor_node;

void text_editor_node_init(text_editor_node *n, struct SapEnv *env, float x, float y, float sx, float sy, struct Text *text_tree);
void text_editor_node_bind_document(text_editor_node *n, struct croft_editor_document *document);
void text_editor_node_set_text(text_editor_node *n, struct Text *text_tree);
void text_editor_node_set_modifiers(text_editor_node *n, uint32_t modifiers);
void text_editor_node_set_wrap_enabled(text_editor_node *n, int enabled);
int text_editor_node_is_wrap_enabled(const text_editor_node *n);
void text_editor_node_set_profiling(text_editor_node *n, int enabled);
void text_editor_node_reset_profile(text_editor_node *n);
void text_editor_node_get_profile(const text_editor_node *n,
                                  croft_text_editor_profile_snapshot *out_snapshot);
void text_editor_node_select_all(text_editor_node *n);
int text_editor_node_is_find_active(const text_editor_node *n);
int text_editor_node_is_replace_active(const text_editor_node *n);
void text_editor_node_find_activate(text_editor_node *n);
void text_editor_node_replace_activate(text_editor_node *n);
void text_editor_node_find_close(text_editor_node *n);
int32_t text_editor_node_set_find_query_utf8(text_editor_node *n,
                                             const char *utf8,
                                             size_t utf8_len);
int32_t text_editor_node_set_replace_query_utf8(text_editor_node *n,
                                                const char *utf8,
                                                size_t utf8_len);
int32_t text_editor_node_find_next(text_editor_node *n);
int32_t text_editor_node_find_previous(text_editor_node *n);
int32_t text_editor_node_replace_next(text_editor_node *n);
int32_t text_editor_node_replace_all(text_editor_node *n);
int32_t text_editor_node_fold(text_editor_node *n);
int32_t text_editor_node_unfold(text_editor_node *n);
int32_t text_editor_node_toggle_fold(text_editor_node *n);
int32_t text_editor_node_indent(text_editor_node *n);
int32_t text_editor_node_outdent(text_editor_node *n);
int32_t text_editor_node_copy_selection_utf8(text_editor_node *n, char **out_utf8, size_t *out_len);
int32_t text_editor_node_replace_selection_utf8(text_editor_node *n,
                                                const uint8_t *utf8,
                                                size_t utf8_len);
int32_t text_editor_node_delete_selection(text_editor_node *n, int backward);
int32_t text_editor_node_undo(text_editor_node *n);
int32_t text_editor_node_redo(text_editor_node *n);
uint32_t text_editor_node_visible_line_count_for_bounds(text_editor_node *n,
                                                        float width,
                                                        float height);
int32_t text_editor_node_offset_to_local_position(text_editor_node *n,
                                                  float width,
                                                  float height,
                                                  uint32_t offset,
                                                  float *out_x,
                                                  float *out_y);
int32_t text_editor_node_hit_test_offset(text_editor_node *n,
                                         float width,
                                         float height,
                                         float local_x,
                                         float local_y,
                                         uint32_t *out_offset);
void text_editor_node_dispose(text_editor_node *n);

#endif // CROFT_SCENE_H
