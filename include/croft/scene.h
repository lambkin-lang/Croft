#ifndef CROFT_SCENE_H
#define CROFT_SCENE_H

#include "croft/platform.h"
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
    uint32_t modifiers;
    int is_selecting;
    int find_active;
    char find_query[128];
    uint32_t find_query_len;
    croft_editor_text_model text_model;
    croft_editor_selection selection;
} text_editor_node;

void text_editor_node_init(text_editor_node *n, struct SapEnv *env, float x, float y, float sx, float sy, struct Text *text_tree);
void text_editor_node_bind_document(text_editor_node *n, struct croft_editor_document *document);
void text_editor_node_set_text(text_editor_node *n, struct Text *text_tree);
void text_editor_node_set_modifiers(text_editor_node *n, uint32_t modifiers);
void text_editor_node_select_all(text_editor_node *n);
int text_editor_node_is_find_active(const text_editor_node *n);
void text_editor_node_find_activate(text_editor_node *n);
void text_editor_node_find_close(text_editor_node *n);
int32_t text_editor_node_find_next(text_editor_node *n);
int32_t text_editor_node_find_previous(text_editor_node *n);
int32_t text_editor_node_indent(text_editor_node *n);
int32_t text_editor_node_outdent(text_editor_node *n);
int32_t text_editor_node_copy_selection_utf8(text_editor_node *n, char **out_utf8, size_t *out_len);
int32_t text_editor_node_replace_selection_utf8(text_editor_node *n,
                                                const uint8_t *utf8,
                                                size_t utf8_len);
int32_t text_editor_node_delete_selection(text_editor_node *n, int backward);
int32_t text_editor_node_undo(text_editor_node *n);
int32_t text_editor_node_redo(text_editor_node *n);
void text_editor_node_dispose(text_editor_node *n);

#endif // CROFT_SCENE_H
