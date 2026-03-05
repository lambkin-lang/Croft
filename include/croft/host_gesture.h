#ifndef CROFT_HOST_GESTURE_H
#define CROFT_HOST_GESTURE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// macOS specific: Wire up the NSWindow's contentView to intercept magnifyWithEvent
// Receives the raw NSWindow pointer and the global C event callback.
void host_gesture_mac_init(void *ns_window, void *ui_callback);

#ifdef __cplusplus
}
#endif

#endif // CROFT_HOST_GESTURE_H
