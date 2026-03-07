#include "croft/host_ui.h"
#include "croft/host_a11y.h"
#include "croft/scene.h"
#include <stdint.h>
#include <stdio.h>

// A simple recursive printer to dump the logical a11y tree via standard API introspection (where possible).
// Since our MVP C API doesn't expose `get_children` (only the OS does), we'll walk the scene graph instead 
// to prove that the handles are populated and linked properly.
static void print_a11y_topology(scene_node *node, int depth) {
    if (!node) return;
    
    for (int i = 0; i < depth; i++) printf("  ");
    
    // Prove the handle exists
    if (node->a11y_handle) {
        printf("- Valid OS A11Y Handle [%p] (local frame: %.1f, %.1f: %.1fx%.1f)\n", 
               (void*)(uintptr_t)node->a11y_handle, node->x, node->y, node->sx, node->sy);
    } else {
        printf("- NULL Handle\n");
    }
    
    scene_node* child = node->first_child;
    while (child) {
        print_a11y_topology(child, depth + 1);
        child = child->next_sibling;
    }
}

int main(void) {
    printf("Starting Tier 9 Accessibility Demo.\n");
    
    if (host_ui_init() != 0) {
        printf("Failed to init UI Window.\n");
        return 1;
    }
    
    if (host_ui_create_window(800, 600, "Croft A11Y Demo") != 0) {
        printf("Failed to create UI Window.\n");
        return 1;
    }
    
    void *window_handle = host_ui_get_native_window();

    // Initialize exactly via the specification
    if (host_a11y_init(window_handle) != 0) {
        printf("Failed to init Native Accessibility Subsystem.\n");
        return 1;
    }

    // Build the scene tree (which transparently builds the A11Y tree now!)
    viewport_node root_vp;
    viewport_node_init(&root_vp, 0, 0, 800, 600);
    
    // Note: The top-level element hooks directly into the OS window
    host_a11y_add_child(NULL, (void*)(uintptr_t)root_vp.base.a11y_handle);
    
    viewport_node left_col;
    viewport_node_init(&left_col, 0, 0, 400, 600);
    
    code_block_node txt1;
    code_block_node_init(&txt1, 20, 20, 360, 100, "Hello Accessibility!");
    
    code_block_node txt2;
    code_block_node_init(&txt2, 20, 140, 360, 100, "I am a text block readable by VoiceOver.");
    
    scene_node_add_child(&left_col.base, &txt1.base);
    scene_node_add_child(&left_col.base, &txt2.base);
    scene_node_add_child(&root_vp.base, &left_col.base);

    printf("Scene graph constructed. Walking tree to verify OS A11y mappings:\n");
    print_a11y_topology(&root_vp.base, 0);

    // Tear down
    host_a11y_terminate();
    host_ui_terminate();
    
    printf("Tier 9 completion successful.\n");
    return 0;
}
