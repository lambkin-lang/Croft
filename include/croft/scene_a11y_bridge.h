#ifndef CROFT_SCENE_A11Y_BRIDGE_H
#define CROFT_SCENE_A11Y_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uintptr_t croft_scene_a11y_handle;

typedef enum croft_scene_a11y_role {
    CROFT_SCENE_A11Y_ROLE_UNKNOWN = 0,
    CROFT_SCENE_A11Y_ROLE_WINDOW = 1,
    CROFT_SCENE_A11Y_ROLE_GROUP = 2,
    CROFT_SCENE_A11Y_ROLE_TEXT = 3,
    CROFT_SCENE_A11Y_ROLE_BUTTON = 4,
    CROFT_SCENE_A11Y_ROLE_TEXT_AREA = 5
} croft_scene_a11y_role;

typedef struct croft_scene_a11y_node_config {
    float x;
    float y;
    float width;
    float height;
    const char* label;
} croft_scene_a11y_node_config;

typedef struct croft_scene_a11y_bridge_vtbl {
    int32_t (*open)(void* userdata);
    void (*close)(void* userdata);
    croft_scene_a11y_handle (*create_node)(void* userdata,
                                           croft_scene_a11y_role role,
                                           const croft_scene_a11y_node_config* config);
    void (*add_child)(void* userdata,
                      croft_scene_a11y_handle parent,
                      croft_scene_a11y_handle child);
    void (*update_frame)(void* userdata,
                         croft_scene_a11y_handle node,
                         float x,
                         float y,
                         float w,
                         float h);
    void (*update_label)(void* userdata,
                         croft_scene_a11y_handle node,
                         const char* label);
    void (*update_value)(void* userdata,
                         croft_scene_a11y_handle node,
                         const char* value);
    void (*destroy_node)(void* userdata, croft_scene_a11y_handle node);
} croft_scene_a11y_bridge_vtbl;

void croft_scene_a11y_reset_bridge(void);
void croft_scene_a11y_install_bridge(const croft_scene_a11y_bridge_vtbl* bridge, void* userdata);

int32_t croft_scene_a11y_open(void);
void croft_scene_a11y_close(void);
croft_scene_a11y_handle croft_scene_a11y_create_node(croft_scene_a11y_role role,
                                                      const croft_scene_a11y_node_config* config);
void croft_scene_a11y_add_child(croft_scene_a11y_handle parent, croft_scene_a11y_handle child);
void croft_scene_a11y_update_frame(croft_scene_a11y_handle node, float x, float y, float w, float h);
void croft_scene_a11y_update_label(croft_scene_a11y_handle node, const char* label);
void croft_scene_a11y_update_value(croft_scene_a11y_handle node, const char* value);
void croft_scene_a11y_destroy_node(croft_scene_a11y_handle node);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_SCENE_A11Y_BRIDGE_H */
