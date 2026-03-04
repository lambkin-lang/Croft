#ifndef CROFT_SCENE_H
#define CROFT_SCENE_H

#include "croft/platform.h"

//
// Render Context
//

typedef struct render_ctx {
    uint32_t bg_color;
    uint32_t fg_color;
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
    void *a11y_handle;
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

#endif // CROFT_SCENE_H
