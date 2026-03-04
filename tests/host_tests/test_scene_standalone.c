#include "croft/host_ui.h"
#include "croft/host_render.h"
#include "croft/scene.h"
#include <stdio.h>
#include <unistd.h>

static void mouse_button_callback(uint32_t button, uint32_t action, uint32_t mods, void *user_data) {
    if (action != 1) return; // Only process press
    
    scene_node *root = (scene_node *)user_data;
    double mx, my;
    host_ui_get_mouse_pos(&mx, &my);
    
    hit_result hit;
    scene_node_hit_test_tree(root, (float)mx, (float)my, &hit);
    
    if (hit.node) {
        printf("Hit node %p at local coordinates (%.1f, %.1f)!\n", hit.node, hit.local_x, hit.local_y);
    } else {
        printf("Clicked outside the scene graph.\n");
    }
}

int main(void) {
    if (host_ui_init() != 0) return 1;
    if (host_ui_create_window(800, 600, "Croft Tier 7 - Scene Graph Demo") != 0) return 1;
    if (host_render_init() != 0) return 1;

    //
    // Construct the manual test tree
    //
    
    viewport_node root;
    viewport_node_init(&root, 0, 0, 800, 600); // Full screen
    
    viewport_node panel1;
    viewport_node_init(&panel1, 50, 50, 300, 200); 
    
    code_block_node text1;
    code_block_node_init(&text1, 10, 30, 200, 50, "Hello Viewport 1");
    scene_node_add_child(&panel1.base, &text1.base);
    
    viewport_node panel2;
    viewport_node_init(&panel2, 400, 200, 300, 200); 
    
    code_block_node text2;
    code_block_node_init(&text2, 20, 40, 200, 50, "Hello Viewport 2");
    scene_node_add_child(&panel2.base, &text2.base);
    
    scene_node_add_child(&root.base, &panel1.base);
    scene_node_add_child(&root.base, &panel2.base);
    
    // Register basic click callback
    host_ui_set_user_data(&root.base);
    // In a real loop we'd plumb this through host_ui dispatch properly.
    // However, host_ui API doesn't have a direct click callback exposed yet.
    // For test purposes, we will synthesize a hit test by fetching the mouse state.
    
    printf("Starting Tier 7 Scene Graph Demo.\n");
    printf("Click to test hit resolution. Will exit after 1.5 seconds.\n");
    
    render_ctx rc = {
        .bg_color = 0xFFCCCCCC, // Light Grey panels
        .fg_color = 0x000000FF  // Black text
    };
    
    int was_mouse_down = 0;
    
    for (int i = 0; i < 90; i++) { // 1.5 seconds at 60fps
        if (host_ui_should_close()) break;
        host_ui_poll_events();
        
        // Emulate a click event dispatch for hit testing
        double mx, my;
        host_ui_get_mouse_pos(&mx, &my);
        int is_mouse_down = host_ui_get_mouse_button(0); // Left Click
        if (is_mouse_down && !was_mouse_down) {
            hit_result hit;
            scene_node_hit_test_tree(&root.base, (float)mx, (float)my, &hit);
            if (hit.node) {
                if (hit.node == &text1.base) printf("Hit text1: ");
                else if (hit.node == &text2.base) printf("Hit text2: ");
                else if (hit.node == &panel1.base) printf("Hit panel1: ");
                else if (hit.node == &panel2.base) printf("Hit panel2: ");
                else if (hit.node == &root.base) printf("Hit root: ");
                printf("local (%.1f, %.1f)\n", hit.local_x, hit.local_y);
            }
        }
        was_mouse_down = is_mouse_down;
        
        uint32_t fw, fh;
        host_ui_get_framebuffer_size(&fw, &fh);
        if (host_render_begin_frame(fw, fh) == 0) {
            host_render_clear(0xFFFFFFFF); // White background
            scene_node_draw_tree(&root.base, &rc);
            host_render_end_frame();
            host_ui_swap_buffers();
        }
        usleep(16000); 
    }

    host_render_terminate();
    host_ui_terminate();
    
    printf("Demo finished cleanly.\n");
    return 0;
}
