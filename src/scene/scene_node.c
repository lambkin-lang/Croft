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

void scene_node_draw_tree(scene_node *root, render_ctx *rc) {
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
    
    // Test children in reverse-paint order (deepest layer first)
    // For a linked list, we collect them all and traverse backward or just do a greedy match.
    // Given the sibling nature, we test all children, and the *last* one drawn that matches wins.
    int hit_found = 0;
    scene_node *child = node->first_child;
    
    // To do true z-order, we should traverse list backwards.
    // But for a simple greedy approach, traversing forwards but overwriting hits is equivalent.
    while (child) {
        if (hit_test_recursive(child, local_x, local_y, out)) {
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
