#include "croft/scene.h"
#include "croft/host_render.h"
#include <stddef.h>

void scene_node_init(scene_node *n, scene_node_vtbl *vtbl, float x, float y, float sx, float sy) {
    n->x = x;
    n->y = y;
    n->sx = sx;
    n->sy = sy;
    n->flags = 0;
    n->vtbl = vtbl;
    n->first_child = NULL;
    n->next_sibling = NULL;
    n->a11y_handle = (croft_scene_a11y_handle)0u;
}

void scene_node_add_child(scene_node *parent, scene_node *child) {
    if (!parent->first_child) {
        parent->first_child = child;
    } else {
        scene_node *curr = parent->first_child;
        while (curr->next_sibling) {
            curr = curr->next_sibling;
        }
        curr->next_sibling = child;
    }
    
    // Wire OS accessibility tree
    if (child->a11y_handle) {
        croft_scene_a11y_add_child(parent->a11y_handle, child->a11y_handle);
    }
}

// Internal recursive pass
static void draw_tree_recursive(scene_node *node, render_ctx *rc) {
    if (!node) return;
    
    // Save coordinate system and translate into local element space
    host_render_save();
    host_render_translate(node->x, node->y);
    
    // Virtual draw call natively draws at (0,0) knowing the host translated the matrix
    if (node->vtbl && node->vtbl->draw) {
        node->vtbl->draw(node, rc);
    }
    
    // Propagate down to children
    scene_node *child = node->first_child;
    while (child) {
        draw_tree_recursive(child, rc);
        child = child->next_sibling;
    }
    
    // Restore parent coordinate system
    host_render_restore();
}

static void scene_node_child_accessibility_transform(scene_node* node,
                                                     float* out_offset_x,
                                                     float* out_offset_y,
                                                     float* out_scale_x,
                                                     float* out_scale_y) {
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float scale_x = 1.0f;
    float scale_y = 1.0f;

    if (node && node->vtbl && node->vtbl->transform_coords) {
        float origin_x = 0.0f;
        float origin_y = 0.0f;
        float unit_x = 1.0f;
        float unit_y = 1.0f;
        float step_x;
        float step_y;

        node->vtbl->transform_coords(node, &origin_x, &origin_y);
        node->vtbl->transform_coords(node, &unit_x, &unit_y);
        step_x = unit_x - origin_x;
        step_y = unit_y - origin_y;

        if (step_x > 0.0001f || step_x < -0.0001f) {
            scale_x = 1.0f / step_x;
            offset_x = -origin_x * scale_x;
        }
        if (step_y > 0.0001f || step_y < -0.0001f) {
            scale_y = 1.0f / step_y;
            offset_y = -origin_y * scale_y;
        }
    }

    if (out_offset_x) {
        *out_offset_x = offset_x;
    }
    if (out_offset_y) {
        *out_offset_y = offset_y;
    }
    if (out_scale_x) {
        *out_scale_x = scale_x;
    }
    if (out_scale_y) {
        *out_scale_y = scale_y;
    }
}

static void update_accessibility_recursive(scene_node* node,
                                           float parent_origin_x,
                                           float parent_origin_y,
                                           float parent_scale_x,
                                           float parent_scale_y) {
    float node_origin_x;
    float node_origin_y;
    float node_width;
    float node_height;
    float child_offset_x = 0.0f;
    float child_offset_y = 0.0f;
    float child_scale_x = 1.0f;
    float child_scale_y = 1.0f;
    scene_node* child;

    if (!node) {
        return;
    }

    node_origin_x = parent_origin_x + (node->x * parent_scale_x);
    node_origin_y = parent_origin_y + (node->y * parent_scale_y);
    node_width = node->sx * parent_scale_x;
    node_height = node->sy * parent_scale_y;

    if (node->a11y_handle) {
        croft_scene_a11y_update_frame(node->a11y_handle,
                                      node_origin_x,
                                      node_origin_y,
                                      node_width,
                                      node_height);
    }
    if (node->vtbl && node->vtbl->update_accessibility) {
        node->vtbl->update_accessibility(node);
    }

    scene_node_child_accessibility_transform(node,
                                             &child_offset_x,
                                             &child_offset_y,
                                             &child_scale_x,
                                             &child_scale_y);
    child = node->first_child;
    while (child) {
        update_accessibility_recursive(child,
                                       node_origin_x + (child_offset_x * parent_scale_x),
                                       node_origin_y + (child_offset_y * parent_scale_y),
                                       parent_scale_x * child_scale_x,
                                       parent_scale_y * child_scale_y);
        child = child->next_sibling;
    }
}

void scene_node_update_accessibility_tree(scene_node *root) {
    update_accessibility_recursive(root, 0.0f, 0.0f, 1.0f, 1.0f);
}

void scene_node_draw_tree(scene_node *root, render_ctx *rc) {
    scene_node_update_accessibility_tree(root);
    draw_tree_recursive(root, rc);
}

// Internal hit test algorithm: depth-first reverse pass (paint order)
static int hit_test_recursive(scene_node *node, float x, float y, hit_result *out) {
    if (!node) return 0;
    
    // Transform coordinates into local space for this node
    float local_x = x - node->x;
    float local_y = y - node->y;
    
    // Bounding Box Test
    if (local_x < 0 || local_x > node->sx || local_y < 0 || local_y > node->sy) {
        return 0; // Miss entirely
    }
    
    float child_x = local_x;
    float child_y = local_y;
    if (node->vtbl && node->vtbl->transform_coords) {
        node->vtbl->transform_coords(node, &child_x, &child_y);
    }
    
    // Test children in reverse-paint order (deepest layer first)
    // For a linked list, we collect them all and traverse backward or just do a greedy match.
    // Given the sibling nature, we test all children, and the *last* one drawn that matches wins.
    int hit_found = 0;
    scene_node *child = node->first_child;
    
    // To do true z-order, we should traverse list backwards.
    // But for a simple greedy approach, traversing forwards but overwriting hits is equivalent.
    while (child) {
        if (hit_test_recursive(child, child_x, child_y, out)) {
            hit_found = 1;
        }
        child = child->next_sibling;
    }
    
    if (hit_found) {
        return 1;
    }
    
    // If no children matched (or leaf node), test self
    if (node->vtbl && node->vtbl->hit_test) {
        node->vtbl->hit_test(node, local_x, local_y, out);
        // If the virtual function reported a hit:
        if (out->node != NULL) {
            return 1;
        }
    } else {
        // Default box fallback
        out->node = node;
        out->local_x = local_x;
        out->local_y = local_y;
        return 1;
    }
    
    return 0;
}

void scene_node_hit_test_tree(scene_node *root, float x, float y, hit_result *out) {
    out->node = NULL;
    hit_test_recursive(root, x, y, out);
}
