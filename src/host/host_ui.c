#include "croft/host_ui.h"
#include "croft/host_log.h"
#include <string.h>
#include <stdio.h>

/* GLFW must be included for UI builds */
#include <GLFW/glfw3.h>

#ifdef __APPLE__
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#endif

static GLFWwindow *g_window = NULL;
static host_ui_event_cb_t g_event_cb = NULL;
static uint32_t g_modifier_mask = 0;

static uint32_t croft_ui_map_glfw_modifiers(int mods) {
    uint32_t mask = 0;
    if (mods & GLFW_MOD_SHIFT) {
        mask |= CROFT_UI_MOD_SHIFT;
    }
    if (mods & GLFW_MOD_CONTROL) {
        mask |= CROFT_UI_MOD_CONTROL;
    }
    if (mods & GLFW_MOD_ALT) {
        mask |= CROFT_UI_MOD_ALT;
    }
    if (mods & GLFW_MOD_SUPER) {
        mask |= CROFT_UI_MOD_SUPER;
    }
    return mask;
}

/* -- GLFW Callbacks -- */

static void glfw_error_callback(int error, const char* description) {
    host_log(CROFT_LOG_ERROR, description, (uint32_t)strlen(description));
}

static void glfw_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)window;
    (void)scancode;
    g_modifier_mask = croft_ui_map_glfw_modifiers(mods);
    
    if (g_event_cb) {
        g_event_cb(CROFT_UI_EVENT_KEY, key, action); /* action: 0=release, 1=press, 2=repeat */
    }
}

static void glfw_mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    (void)window;
    g_modifier_mask = croft_ui_map_glfw_modifiers(mods);
    
    if (g_event_cb) {
        /* Encodes button in arg0, action in arg1 */
        g_event_cb(CROFT_UI_EVENT_MOUSE, button, action); 
    }
}

static void glfw_scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    (void)window;
    if (g_event_cb) {
        /* Encodes xoffset, yoffset using integer multiplication to preserve fractional precision */
        g_event_cb(CROFT_UI_EVENT_SCROLL, (int32_t)(xoffset * 1000.0), (int32_t)(yoffset * 1000.0));
    }
}

static void glfw_cursor_pos_callback(GLFWwindow* window, double xpos, double ypos) {
    (void)window;
    if (g_event_cb) {
        g_event_cb(CROFT_UI_EVENT_CURSOR_POS, (int32_t)(xpos * 1000.0), (int32_t)(ypos * 1000.0));
    }
}

static void glfw_char_callback(GLFWwindow* window, unsigned int codepoint) {
    (void)window;
    if (g_event_cb) {
        g_event_cb(CROFT_UI_EVENT_CHAR, (int32_t)codepoint, 0);
    }
}

/* -- Host UI API -- */

int32_t host_ui_init(void) {
    glfwSetErrorCallback(glfw_error_callback);
    
    if (!glfwInit()) {
        const char *err = "Failed to initialize GLFW";
        host_log(CROFT_LOG_ERROR, err, (uint32_t)strlen(err));
        return -1;
    }
    
#ifdef CROFT_HOST_UI_GLFW_NO_API
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#else
    /* Request a modern OpenGL Core context, used by tgfx later */
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
#endif

    return 0;
}

void host_ui_terminate(void) {
    if (g_window) {
        glfwDestroyWindow(g_window);
        g_window = NULL;
    }
    g_modifier_mask = 0;
    glfwTerminate();
}

int32_t host_ui_create_window(uint32_t width, uint32_t height, const char *title) {
    if (g_window) return 0; /* Already created */
    
    g_window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!g_window) {
        const char *err = "Failed to create GLFW window";
        host_log(CROFT_LOG_ERROR, err, (uint32_t)strlen(err));
        return -1;
    }
    
#ifndef CROFT_HOST_UI_GLFW_NO_API
    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1); /* Enable VSync */
#endif
    
    /* Register input callbacks */
    glfwSetKeyCallback(g_window, glfw_key_callback);
    glfwSetCharCallback(g_window, glfw_char_callback);
    glfwSetMouseButtonCallback(g_window, glfw_mouse_button_callback);
    glfwSetScrollCallback(g_window, glfw_scroll_callback);
    glfwSetCursorPosCallback(g_window, glfw_cursor_pos_callback);
    
    return 0;
}

void host_ui_get_framebuffer_size(uint32_t *w, uint32_t *h) {
    if (!g_window) {
        if (w) *w = 0;
        if (h) *h = 0;
        return;
    }
    int fw, fh;
    glfwGetFramebufferSize(g_window, &fw, &fh);
    if (w) *w = (uint32_t)fw;
    if (h) *h = (uint32_t)fh;
}

void host_ui_read_pixel(uint32_t x, uint32_t y, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
#ifdef CROFT_HOST_UI_GLFW_NO_API
    (void)x;
    (void)y;
    if (r) *r = 0;
    if (g) *g = 0;
    if (b) *b = 0;
    if (a) *a = 0;
#else
    uint8_t pixel[4] = {0, 0, 0, 0};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (r) *r = pixel[0];
    if (g) *g = pixel[1];
    if (b) *b = pixel[2];
    if (a) *a = pixel[3];
#endif
}

void host_ui_test_clear_blue(void) {
#ifndef CROFT_HOST_UI_GLFW_NO_API
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
#endif
}

int32_t host_ui_should_close(void) {
    if (!g_window) return 1;
    return glfwWindowShouldClose(g_window);
}

double host_ui_get_time(void) {
    return glfwGetTime();
}

void host_ui_get_mouse_pos(double *x, double *y) {
    if (g_window) {
        glfwGetCursorPos(g_window, x, y);
    } else {
        *x = 0; *y = 0;
    }
}

int32_t host_ui_get_mouse_button(int32_t button) {
    if (!g_window) return 0;
    return glfwGetMouseButton(g_window, button) == GLFW_PRESS;
}

uint32_t host_ui_get_modifiers(void) {
    return g_modifier_mask;
}

void host_ui_set_user_data(void *data) {
    if (g_window) {
        glfwSetWindowUserPointer(g_window, data);
    }
}

void *host_ui_get_user_data(void) {
    if (!g_window) return NULL;
    return glfwGetWindowUserPointer(g_window);
}

void host_ui_poll_events(void) {
    glfwPollEvents();
}

void host_ui_swap_buffers(void) {
#ifndef CROFT_HOST_UI_GLFW_NO_API
    if (g_window) {
        glfwSwapBuffers(g_window);
    }
#else
    (void)g_window;
#endif
}

void host_ui_set_event_callback(host_ui_event_cb_t cb) {
    g_event_cb = cb;
}

void* host_ui_get_window(void) {
    return (void*)g_window;
}

void* host_ui_get_native_window(void) {
    if (!g_window) return NULL;
#ifdef __APPLE__
    return (void*)glfwGetCocoaWindow(g_window);
#else
    return NULL;
#endif
}
