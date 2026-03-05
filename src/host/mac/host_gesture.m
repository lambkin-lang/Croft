#import <Cocoa/Cocoa.h>
#import "croft/host_ui.h"
#import <objc/runtime.h>

// We store the global fallback C router as an associated object on the view
static const void *kCroftGestureCallbackKey = &kCroftGestureCallbackKey;

// Our custom implementation for magnifyWithEvent
static void croft_magnifyWithEvent(id self, SEL _cmd, NSEvent *event) {
    // Invoke original implementation in case GLFW added something later
    // To do this properly, we should call the IMP of the superclass.
    // However, NSView's default is a no-op unless it's a specific subclass. 
    // We just override it directly for the GLFW contentView footprint.
    
    // Safety check for associated callback
    NSValue *val = objc_getAssociatedObject(self, kCroftGestureCallbackKey);
    if (val) {
        host_ui_event_cb_t callback = (host_ui_event_cb_t)[val pointerValue];
        if (callback) {
            // macOS magnification is a tiny float (e.g. 0.05 per tick).
            // We encode it as an integer multiplying by 1,000,000 for high precision.
            int32_t arg0 = (int32_t)(event.magnification * 1000000.0);
            callback(CROFT_UI_EVENT_ZOOM_GESTURE, arg0, 0);
        }
    }
}

// macOS specific initializer
void host_gesture_mac_init(void *ns_window_ptr, void *ui_callback) {
    if (!ns_window_ptr || !ui_callback) return;
    
    NSWindow *window = (__bridge NSWindow *)ns_window_ptr;
    NSView *contentView = window.contentView;
    if (!contentView) return;
    
    // Store the C callback on the view instance
    NSValue *cbVal = [NSValue valueWithPointer:ui_callback];
    objc_setAssociatedObject(contentView, kCroftGestureCallbackKey, cbVal, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    
    // Swizzle (or simply add if it doesn't exist) the magnifyWithEvent: method on the view's specific Class
    Class viewClass = [contentView class];
    SEL selector = @selector(magnifyWithEvent:);
    
    Method originalMethod = class_getInstanceMethod(viewClass, selector);
    if (!originalMethod) {
        // Method didn't exist, we add it directly
        class_addMethod(viewClass, selector, (IMP)croft_magnifyWithEvent, "v@:@");
    } else {
        // Method exists, replace it to hook into our logic securely
        class_replaceMethod(viewClass, selector, (IMP)croft_magnifyWithEvent, method_getTypeEncoding(originalMethod));
    }
}
