#ifndef CROFT_HOST_A11Y_H
#define CROFT_HOST_A11Y_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// High-level generic roles our Scene Graph understands
typedef enum {
    ROLE_UNKNOWN = 0,
    ROLE_WINDOW = 1,
    ROLE_GROUP = 2,
    ROLE_TEXT = 3,
    ROLE_BUTTON = 4,
} host_a11y_role;

// A generic "mix-in" struct for OS-specific affordances not available everywhere.
// By isolating this, the core C API stays mostly uniform while passing down extra metadata.
typedef struct {
    // Basic bounds in absolutely positioned screen coordinates.
    // Core OSes (macOS, Windows, Linux) all require screen-space bounds.
    float x;
    float y;
    float width;
    float height;
    
    // (Optional) String payload for text nodes
    const char* label;
    
    // OS Mix-in: e.g. macOS-specific "subroles" or Android-specific "actions" 
    // are passed opaquely via void* or mapped by role. For simplicity in the MVP,
    // we just use the enum, but a larger struct could be appended.
    void* os_specific_mixin;
} host_a11y_node_config;

// 1. Initialize the Accessibility Root for a given OS window
//    `native_window_handle` corresponds to NSWindow* on Mac, or HWND on Windows.
int32_t host_a11y_init(void *native_window_handle);

// 2. Shut down the Accessibility System
void host_a11y_terminate(void);

// 3. Create an abstract accessiblity node representing a scene element
//    Returns an opaque pointer (e.g. CroftAccessibilityElement* on Mac)
void* host_a11y_create_node(host_a11y_role role, const host_a11y_node_config* config);

// 4. Link a child node to a parent node in the logical tree.
//    If parent_a11y_node is NULL, it links directly to the root OS Window.
void host_a11y_add_child(void* parent_a11y_node, void* child_a11y_node);

// 5. Update the bounding box of a node (e.g., when the window is dragged or scrolling)
//    Many OSes query this dynamically, but we can preemptively push frame updates too.
void host_a11y_update_frame(void* a11y_node, float x, float y, float w, float h);

// 6. Free the allocated OS node
void host_a11y_destroy_node(void* a11y_node);

#ifdef __cplusplus
}
#endif

#endif // CROFT_HOST_A11Y_H
