#include "croft/scene.h"
#include "croft/host_render.h"

//
// Viewport Node Implementation
//

static void viewport_draw(scene_node *n, render_ctx *rc) {
    viewport_node *vp = (viewport_node *)n;
    
    // Draw viewport background
    host_render_draw_rect(0, 0, n->sx, n->sy, rc->bg_color);
    
    // N.B: Real viewports would clip to sx,sy using a native tgfx Canvas clipRect
    // and then translate by scroll_x, scroll_y before drawing children.
    // For this basic tier 7 implementation, we just translate.
    host_render_translate(vp->scroll_x, vp->scroll_y);
}

static void viewport_hit_test(scene_node *n, float x, float y, hit_result *out) {
    // If we land here, no children matched, meaning we clicked the viewport background
    out->node = n;
    out->local_x = x;
    out->local_y = y;
}

static void viewport_update_accessibility(scene_node *n) {
    // Not implemented for Tier 7
}

static scene_node_vtbl viewport_vtbl = {
    .draw = viewport_draw,
    .hit_test = viewport_hit_test,
    .update_accessibility = viewport_update_accessibility
};

void viewport_node_init(viewport_node *n, float x, float y, float sx, float sy) {
    scene_node_init(&n->base, &viewport_vtbl, x, y, sx, sy);
    n->scroll_x = 0;
    n->scroll_y = 0;
}
