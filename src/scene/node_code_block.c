#include "croft/scene.h"
#include "croft/host_render.h"
#include "croft/host_a11y.h"

//
// Code Block Node Implementation
//

static void code_block_draw(scene_node *n, render_ctx *rc) {
    code_block_node *cb = (code_block_node *)n;
    
    // N.B: We draw at (0,0) because the parent scene_node_draw_tree already
    // pushed our specific coordinates into the GPU transform matrix!
    host_render_draw_text(0, 0, cb->text, cb->text_len, 20.0f, rc->fg_color);
}

static void code_block_hit_test(scene_node *n, float x, float y, hit_result *out) {
    // We were directly clicked!
    out->node = n;
    out->local_x = x;
    out->local_y = y;
}

static void code_block_update_accessibility(scene_node *n) {
    // Not implemented for Tier 7
}

static scene_node_vtbl code_block_vtbl = {
    .draw = code_block_draw,
    .hit_test = code_block_hit_test,
    .update_accessibility = code_block_update_accessibility
};

void code_block_node_init(code_block_node *n, float x, float y, float sx, float sy, const char *text) {
    scene_node_init(&n->base, &code_block_vtbl, x, y, sx, sy);
    n->text = text;
    
    // Wire text into the screen-reader node
    host_a11y_node_config cfg = {
        .x = x, .y = y, .width = sx, .height = sy,
        .label = text,
        .os_specific_mixin = NULL
    };
    n->base.a11y_handle = host_a11y_create_node(ROLE_TEXT, &cfg);
    // Calculate naive length
    const char *p = text;
    while (*p) p++;
    n->text_len = (uint32_t)(p - text);
}
