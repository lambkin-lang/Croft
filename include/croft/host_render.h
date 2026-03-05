#ifndef CROFT_HOST_RENDER_H
#define CROFT_HOST_RENDER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initializes the hardware-accelerated rendering context (tgfx/Skia).
 * Assumes a valid UI window (GLFW context) is active on the current thread.
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
