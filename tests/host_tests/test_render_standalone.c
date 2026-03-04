#include "croft/host_ui.h"
#include "croft/host_render.h"
#include <stdio.h>
#include <unistd.h>

int main(void) {
    if (host_ui_init() != 0) {
        printf("Failed to init UI\n");
        return 1;
    }
    if (host_ui_create_window(800, 600, "Croft Tier 6 - Render Demo") != 0) {
        printf("Failed to create window\n");
        return 1;
    }
    if (host_render_init() != 0) {
        printf("Failed to init renderer\n");
        return 1;
    }

    printf("Starting Tier 6 Rendering Demo.\nA colored window should appear for 1 second.\n\n");

    for (int i = 0; i < 60; i++) {
        if (host_ui_should_close()) {
            break;
        }

        host_ui_poll_events();
        
        if (host_render_begin_frame(800, 600) == 0) {
            // clear black
            host_render_clear(0x000000FF);
            // draw red rect
            host_render_draw_rect(100, 100, 200, 200, 0xFF0000FF);
            // draw text
            host_render_draw_text(100, 350, "Hello Croft Rendering", 21, 0x00FF00FF);
            
            host_render_end_frame();
            host_ui_swap_buffers();
        }
        usleep(16000); // ~60fps
    }

    host_render_terminate();
    host_ui_terminate();
    
    printf("\nDemo finished cleanly.\n");
    return 0;
}
