#include "croft/scene_a11y_bridge.h"

#include "croft/host_a11y.h"

#include <stddef.h>

typedef struct {
    const croft_scene_a11y_bridge_vtbl* vtbl;
    void* userdata;
} croft_scene_a11y_bridge_state;

static int32_t croft_scene_a11y_default_open(void* userdata)
{
    return host_a11y_init(userdata);
}

static void croft_scene_a11y_default_close(void* userdata)
{
    (void)userdata;
    host_a11y_terminate();
}

static host_a11y_role croft_scene_a11y_default_role(croft_scene_a11y_role role)
{
    switch (role) {
        case CROFT_SCENE_A11Y_ROLE_WINDOW:
            return ROLE_WINDOW;
        case CROFT_SCENE_A11Y_ROLE_GROUP:
            return ROLE_GROUP;
        case CROFT_SCENE_A11Y_ROLE_TEXT:
            return ROLE_TEXT;
        case CROFT_SCENE_A11Y_ROLE_BUTTON:
            return ROLE_BUTTON;
        case CROFT_SCENE_A11Y_ROLE_TEXT_AREA:
            return ROLE_TEXT_AREA;
        default:
            return ROLE_UNKNOWN;
    }
}

static croft_scene_a11y_handle croft_scene_a11y_default_create_node(
    void* userdata,
    croft_scene_a11y_role role,
    const croft_scene_a11y_node_config* config)
{
    host_a11y_node_config host_cfg;

    (void)userdata;
    if (!config) {
        return (croft_scene_a11y_handle)0u;
    }

    host_cfg.x = config->x;
    host_cfg.y = config->y;
    host_cfg.width = config->width;
    host_cfg.height = config->height;
    host_cfg.label = config->label;
    host_cfg.os_specific_mixin = NULL;
    return (croft_scene_a11y_handle)(uintptr_t)host_a11y_create_node(croft_scene_a11y_default_role(role), &host_cfg);
}

static void croft_scene_a11y_default_add_child(void* userdata,
                                               croft_scene_a11y_handle parent,
                                               croft_scene_a11y_handle child)
{
    (void)userdata;
    host_a11y_add_child((void*)(uintptr_t)parent, (void*)(uintptr_t)child);
}

static void croft_scene_a11y_default_update_frame(void* userdata,
                                                  croft_scene_a11y_handle node,
                                                  float x,
                                                  float y,
                                                  float w,
                                                  float h)
{
    (void)userdata;
    host_a11y_update_frame((void*)(uintptr_t)node, x, y, w, h);
}

static void croft_scene_a11y_default_update_label(void* userdata,
                                                  croft_scene_a11y_handle node,
                                                  const char* label)
{
    (void)userdata;
    host_a11y_update_label((void*)(uintptr_t)node, label);
}

static void croft_scene_a11y_default_update_value(void* userdata,
                                                  croft_scene_a11y_handle node,
                                                  const char* value)
{
    (void)userdata;
    host_a11y_update_value((void*)(uintptr_t)node, value);
}

static void croft_scene_a11y_default_destroy_node(void* userdata, croft_scene_a11y_handle node)
{
    (void)userdata;
    host_a11y_destroy_node((void*)(uintptr_t)node);
}

static const croft_scene_a11y_bridge_vtbl g_default_bridge = {
    .open = croft_scene_a11y_default_open,
    .close = croft_scene_a11y_default_close,
    .create_node = croft_scene_a11y_default_create_node,
    .add_child = croft_scene_a11y_default_add_child,
    .update_frame = croft_scene_a11y_default_update_frame,
    .update_label = croft_scene_a11y_default_update_label,
    .update_value = croft_scene_a11y_default_update_value,
    .destroy_node = croft_scene_a11y_default_destroy_node
};

static croft_scene_a11y_bridge_state g_scene_a11y = {
    &g_default_bridge,
    NULL
};

void croft_scene_a11y_reset_bridge(void)
{
    g_scene_a11y.vtbl = &g_default_bridge;
    g_scene_a11y.userdata = NULL;
}

void croft_scene_a11y_install_bridge(const croft_scene_a11y_bridge_vtbl* bridge, void* userdata)
{
    if (!bridge) {
        croft_scene_a11y_reset_bridge();
        return;
    }

    g_scene_a11y.vtbl = bridge;
    g_scene_a11y.userdata = userdata;
}

int32_t croft_scene_a11y_open(void)
{
    if (!g_scene_a11y.vtbl || !g_scene_a11y.vtbl->open) {
        return 0;
    }
    return g_scene_a11y.vtbl->open(g_scene_a11y.userdata);
}

void croft_scene_a11y_close(void)
{
    if (g_scene_a11y.vtbl && g_scene_a11y.vtbl->close) {
        g_scene_a11y.vtbl->close(g_scene_a11y.userdata);
    }
}

croft_scene_a11y_handle croft_scene_a11y_create_node(croft_scene_a11y_role role,
                                                      const croft_scene_a11y_node_config* config)
{
    if (!g_scene_a11y.vtbl || !g_scene_a11y.vtbl->create_node) {
        return (croft_scene_a11y_handle)0u;
    }
    return g_scene_a11y.vtbl->create_node(g_scene_a11y.userdata, role, config);
}

void croft_scene_a11y_add_child(croft_scene_a11y_handle parent, croft_scene_a11y_handle child)
{
    if (g_scene_a11y.vtbl && g_scene_a11y.vtbl->add_child) {
        g_scene_a11y.vtbl->add_child(g_scene_a11y.userdata, parent, child);
    }
}

void croft_scene_a11y_update_frame(croft_scene_a11y_handle node, float x, float y, float w, float h)
{
    if (g_scene_a11y.vtbl && g_scene_a11y.vtbl->update_frame) {
        g_scene_a11y.vtbl->update_frame(g_scene_a11y.userdata, node, x, y, w, h);
    }
}

void croft_scene_a11y_update_label(croft_scene_a11y_handle node, const char* label)
{
    if (g_scene_a11y.vtbl && g_scene_a11y.vtbl->update_label) {
        g_scene_a11y.vtbl->update_label(g_scene_a11y.userdata, node, label);
    }
}

void croft_scene_a11y_update_value(croft_scene_a11y_handle node, const char* value)
{
    if (g_scene_a11y.vtbl && g_scene_a11y.vtbl->update_value) {
        g_scene_a11y.vtbl->update_value(g_scene_a11y.userdata, node, value);
    }
}

void croft_scene_a11y_destroy_node(croft_scene_a11y_handle node)
{
    if (g_scene_a11y.vtbl && g_scene_a11y.vtbl->destroy_node) {
        g_scene_a11y.vtbl->destroy_node(g_scene_a11y.userdata, node);
    }
}
