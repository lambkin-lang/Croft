#include "croft/host_a11y.h"

int32_t host_a11y_init(void *native_window_handle) { return 0; }
void host_a11y_terminate(void) {}
void* host_a11y_create_node(host_a11y_role role, const host_a11y_node_config* config) { return 0; }
void host_a11y_add_child(void* parent_a11y_node, void* child_a11y_node) {}
void host_a11y_update_frame(void* a11y_node, float x, float y, float w, float h) {}
void host_a11y_destroy_node(void* a11y_node) {}
