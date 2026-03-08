#include "croft/host_render.h"
#include "croft/host_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int env_flag_enabled(const char* value) {
    return value && value[0] != '\0' && !(value[0] == '0' && value[1] == '\0');
}

static double usec_to_msec(uint64_t usec) {
    return (double)usec / 1000.0;
}

static void print_render_profile_summary(const char* variant) {
    croft_host_render_profile_snapshot profile = {0};

    host_render_get_profile(&profile);
    if (!profile.enabled) {
        return;
    }

    printf("render-profile variant=%s begin_calls=%llu begin_ms=%.3f lock_ms=%.3f target_ms=%.3f drawable_ms=%.3f surface_ms=%.3f command_buffer_ms=%.3f encoder_ms=%.3f flush_ms=%.3f submit_ms=%.3f wait_ms=%.3f present_ms=%.3f unlock_ms=%.3f blit_ms=%.3f end_calls=%llu end_ms=%.3f\n",
           variant,
           (unsigned long long)profile.begin_frame_calls,
           usec_to_msec(profile.begin_frame_total_usec),
           usec_to_msec(profile.context_lock_total_usec),
           usec_to_msec(profile.target_update_total_usec),
           usec_to_msec(profile.acquire_drawable_total_usec),
           usec_to_msec(profile.surface_create_total_usec),
           usec_to_msec(profile.command_buffer_total_usec),
           usec_to_msec(profile.encoder_start_total_usec),
           usec_to_msec(profile.flush_total_usec),
           usec_to_msec(profile.submit_total_usec),
           usec_to_msec(profile.wait_total_usec),
           usec_to_msec(profile.present_total_usec),
           usec_to_msec(profile.unlock_total_usec),
           usec_to_msec(profile.blit_total_usec),
           (unsigned long long)profile.end_frame_calls,
           usec_to_msec(profile.end_frame_total_usec));
}

int main(void) {
    const char* auto_close_env = getenv("CROFT_RENDER_AUTO_CLOSE_MS");
    const char* profile_env = getenv("CROFT_RENDER_PROFILE");
    double start_time = 0.0;
    double end_time = 0.0;
    uint32_t frame_count = 0u;
    uint32_t auto_close_ms = 0u;
    int profile_enabled = env_flag_enabled(profile_env);

    if (auto_close_env && auto_close_env[0] != '\0') {
        int parsed = atoi(auto_close_env);
        if (parsed > 0) {
            auto_close_ms = (uint32_t)parsed;
        }
    }

    if (host_ui_init() != 0) {
        printf("Failed to init UI\n");
        return 1;
    }
    if (host_ui_create_window(800, 600, "Croft Render Canvas (Metal)") != 0) {
        printf("Failed to create window\n");
        host_ui_terminate();
        return 1;
    }
    if (host_render_init() != 0) {
        printf("Failed to init renderer\n");
        host_ui_terminate();
        return 1;
    }
    host_render_set_profiling(profile_enabled);

    printf("Starting Metal render canvas demo.\n");
    start_time = host_ui_get_time();

    for (int i = 0; i < 60 && !host_ui_should_close(); ++i) {
        uint32_t fw = 0;
        uint32_t fh = 0;

        if (auto_close_ms > 0u
                && ((host_ui_get_time() - start_time) * 1000.0) >= (double)auto_close_ms) {
            break;
        }

        host_ui_poll_events();
        host_ui_get_framebuffer_size(&fw, &fh);

        if (host_render_begin_frame(fw, fh) == 0) {
            host_render_clear(0xFFFFFFFF);
            host_render_draw_rect(100, 100, 200, 200, 0xFF0000FF);
            host_render_draw_text(100, 350, "Hello Croft Metal", 18, 36.0f, 0x00FF00FF);
            host_render_end_frame();
            host_ui_swap_buffers();
            frame_count++;
        }

        usleep(16000);
    }

    end_time = host_ui_get_time();
    printf("render-canvas frames=%u wall_ms=%llu\n",
           frame_count,
           (unsigned long long)((end_time - start_time) * 1000.0));
    print_render_profile_summary("canvas-tgfx");
    host_render_terminate();
    host_ui_terminate();
    printf("Demo finished cleanly.\n");
    return 0;
}
