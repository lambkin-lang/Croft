#include "croft/host_render.h"
#include "croft/host_ui.h"
#include <stdio.h>
#include <unistd.h>

int main(void) {
    if (host_ui_init() != 0) {
        printf("Failed to init UI\n");
        return 1;
    }
    if (host_ui_create_window(800, 600, "Croft Render Canvas (OpenGL)") != 0) {
        printf("Failed to create window\n");
        host_ui_terminate();
        return 1;
    }
    if (host_render_init() != 0) {
        printf("Failed to init renderer\n");
        host_ui_terminate();
        return 1;
    }

    printf("Starting OpenGL render canvas demo.\n");

    for (int i = 0; i < 60 && !host_ui_should_close(); ++i) {
        uint32_t fw = 0;
        uint32_t fh = 0;

        host_ui_poll_events();
        host_ui_get_framebuffer_size(&fw, &fh);

        if (host_render_begin_frame(fw, fh) == 0) {
            host_render_clear(0xFFFFFFFF);
            host_render_draw_rect(100, 100, 200, 200, 0xFF0000FF);
            host_render_draw_text(100, 350, "Hello Croft OpenGL", 19, 36.0f, 0x00FF00FF);
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
