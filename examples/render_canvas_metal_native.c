#include "croft/host_render.h"
#include "croft/host_ui.h"

#include <stdio.h>
#include <unistd.h>

int main(void) {
    if (host_ui_init() != 0) {
        printf("Failed to init UI\n");
        return 1;
    }
    if (host_ui_create_window(800, 600, "Croft Render Canvas (Native Metal)") != 0) {
        printf("Failed to create window\n");
        host_ui_terminate();
        return 1;
    }
    if (host_render_init() != 0) {
        printf("Failed to init native Metal renderer\n");
        host_ui_terminate();
        return 1;
    }

    printf("Starting native Metal render canvas demo.\n");

    for (int i = 0; i < 60 && !host_ui_should_close(); ++i) {
        const float pulse = (float)(i % 30) / 29.0f;
        const float inset = 40.0f + pulse * 120.0f;
        uint32_t fw = 0;
        uint32_t fh = 0;

        host_ui_poll_events();
        host_ui_get_framebuffer_size(&fw, &fh);

        if (host_render_begin_frame(fw, fh) == 0) {
            host_render_clear(0xF2F4F8FF);
            host_render_draw_rect(60.0f, 60.0f, 220.0f, 140.0f, 0xD7263DFF);
            host_render_draw_rect(inset, 260.0f, 300.0f, 28.0f, 0x1B998BFF);
            host_render_draw_rect(420.0f, 120.0f, 180.0f, 240.0f, 0x2E294EFF);
            host_render_draw_text(96.0f, 188.0f, "Hello Croft Native Metal", 25, 32.0f, 0x102A43FF);
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
