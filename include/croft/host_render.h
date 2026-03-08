#ifndef CROFT_HOST_RENDER_H
#define CROFT_HOST_RENDER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct croft_host_render_profile_snapshot {
    uint32_t enabled;
    uint64_t begin_frame_calls;
    uint64_t begin_frame_total_usec;
    uint64_t context_lock_total_usec;
    uint64_t target_update_total_usec;
    uint64_t acquire_drawable_total_usec;
    uint64_t surface_create_total_usec;
    uint64_t command_buffer_total_usec;
    uint64_t encoder_start_total_usec;
    uint64_t submit_total_usec;
    uint64_t present_total_usec;
    uint64_t unlock_total_usec;
    uint64_t blit_total_usec;
    uint64_t end_frame_calls;
    uint64_t end_frame_total_usec;
} croft_host_render_profile_snapshot;

/**
 * Initializes the hardware-accelerated rendering context (tgfx).
 * Assumes a valid UI window is available for the selected backend.
 * Returns 0 on success, < 0 on failure.
 */
int32_t host_render_init(void);

/**
 * Terminates the rendering context, freeing GPU resources.
 */
void host_render_terminate(void);

/**
 * Prepares the surface for a new frame of the given dimensions.
 */
int32_t host_render_begin_frame(uint32_t width, uint32_t height);

void host_render_save(void);
void host_render_restore(void);
void host_render_translate(float dx, float dy);
void host_render_scale(float sx, float sy);
void host_render_clip_rect(float x, float y, float w, float h);
void host_render_set_profiling(int enabled);
void host_render_reset_profile(void);
void host_render_get_profile(croft_host_render_profile_snapshot* out_snapshot);

/**
 * Clears the screen to the given RGBA color.
 */
int32_t host_render_clear(uint32_t color_rgba);

/**
 * Draws a filled rectangle.
 */
int32_t host_render_draw_rect(float x, float y, float w, float h, uint32_t color_rgba);

/**
 * Draws a shaped text string.
 */
int32_t host_render_draw_text(float x, float y, const char* text, uint32_t len, float font_size, uint32_t color_rgba);

// Computes the graphical width of the given utf8 text
float host_render_measure_text(const char* text, uint32_t len, float font_size);

/**
 * Flushes drawing commands to the GPU.
 */
int32_t host_render_end_frame(void);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_HOST_RENDER_H */
