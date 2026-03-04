#include "croft/host_ui.h"
#include "croft/host_log.h"
#include <string.h>

/* GLFW must be included for UI builds */
#include <GLFW/glfw3.h>

static GLFWwindow *g_window = NULL;
static host_ui_event_cb_t g_event_cb = NULL;

/* -- GLFW Callbacks -- */

static void glfw_error_callback(int error, const char* description) {
    host_log(CROFT_LOG_ERROR, description, (uint32_t)strlen(description));
}

static void glfw_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)window;
    (void)scancode;
    (void)mods;
    
    if (g_event_cb) {
        g_event_cb(CROFT_UI_EVENT_KEY, key, action); /* action: 0=release, 1=press, 2=repeat */
    }
}

static void glfw_mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    (void)window;
    (void)mods;
    
    if (g_event_cb) {
        /* Encodes button in arg0, action in arg1 */
        g_event_cb(CROFT_UI_EVENT_MOUSE, button, action); 
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
    
    /* Request a modern OpenGL Core context, used by tgfx/Skia later */
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    return 0;
}

void host_ui_terminate(void) {
    if (g_window) {
        glfwDestroyWindow(g_window);
        g_window = NULL;
    }
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
    
    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1); /* Enable VSync */
    
    /* Register input callbacks */
    glfwSetKeyCallback(g_window, glfw_key_callback);
    glfwSetMouseButtonCallback(g_window, glfw_mouse_button_callback);
    
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
    uint8_t pixel[4] = {0, 0, 0, 0};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (r) *r = pixel[0];
    if (g) *g = pixel[1];
    if (b) *b = pixel[2];
    if (a) *a = pixel[3];
}

void host_ui_test_clear_blue(void) {
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

int32_t host_ui_should_close(void) {
    if (!g_window) return 1;
    return glfwWindowShouldClose(g_window);
}

void host_ui_poll_events(void) {
    glfwPollEvents();
}

void host_ui_swap_buffers(void) {
    if (g_window) {
        glfwSwapBuffers(g_window);
    }
}

void host_ui_set_event_callback(host_ui_event_cb_t cb) {
    g_event_cb = cb;
}

void* host_ui_get_window(void) {
    return (void*)g_window;
}
