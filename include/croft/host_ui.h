#ifndef CROFT_HOST_UI_H
#define CROFT_HOST_UI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UI Event Types */
#define CROFT_UI_EVENT_KEY   1
#define CROFT_UI_EVENT_MOUSE 2

/* Event callback signature */
typedef void (*host_ui_event_cb_t)(int32_t event_type, int32_t arg0, int32_t arg1);

/**
 * Initializes the Windowing platform (GLFW).
 * Returns 0 on success, < 0 on failure.
 */
int32_t host_ui_init(void);

/**
 * Terminates the Windowing platform.
 */
void host_ui_terminate(void);

/**
 * Creates the primary application window.
 * Returns 0 on success, < 0 on failure.
 */
int32_t host_ui_create_window(uint32_t width, uint32_t height, const char *title);

/**
 * Gets the actual pixel size of the window's framebuffer. Required for rendering backends 
 * like tgfx/OpenGL on High-DPI/Retina screens.
 */
void host_ui_get_framebuffer_size(uint32_t *w, uint32_t *h);

/**
 * Reads a single pixel's RGBA format from the current OpenGL Context/Framebuffer. 
 * Allows debugging unit tests headlessly.
 */
void host_ui_read_pixel(uint32_t x, uint32_t y, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);

/**
 * Debugging function to clear the native window to blue using raw OpenGL.
 */
void host_ui_test_clear_blue(void);

/**
 * Checks if the user requested the window to close.
 * Returns 1 if should close, 0 otherwise.
 */
int32_t host_ui_should_close(void);

void host_ui_get_mouse_pos(double *x, double *y);
int32_t host_ui_get_mouse_button(int32_t button);
void host_ui_set_user_data(void *data);
void *host_ui_get_user_data(void);

/**
 * Process pending window events. Will trigger the registered event callback.
 */
void host_ui_poll_events(void);

/**
 * Swaps the rendering buffers.
 */
void host_ui_swap_buffers(void);

/**
 * Registers a global callback for UI events.
 */
void host_ui_set_event_callback(host_ui_event_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_HOST_UI_H */
