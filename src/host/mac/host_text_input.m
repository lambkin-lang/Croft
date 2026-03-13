#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

#include "croft/host_ui.h"

typedef void (*CroftSetMarkedTextImp)(id self,
                                      SEL _cmd,
                                      id string,
                                      NSRange selectedRange,
                                      NSRange replacementRange);
typedef void (*CroftUnmarkTextImp)(id self, SEL _cmd);
typedef void (*CroftMouseDownImp)(id self, SEL _cmd, NSEvent* event);

static CroftSetMarkedTextImp g_original_set_marked_text = NULL;
static CroftUnmarkTextImp g_original_unmark_text = NULL;
static CroftMouseDownImp g_original_mouse_down = NULL;
static CroftMouseDownImp g_original_right_mouse_down = NULL;
static CroftMouseDownImp g_original_other_mouse_down = NULL;
static Class g_hooked_view_class = Nil;
static int32_t g_last_click_count = 1;

extern void croft_host_ui_dispatch_composition_event(int32_t kind,
                                                     const uint8_t* utf8,
                                                     uint32_t utf8_len,
                                                     uint32_t selection_start,
                                                     uint32_t selection_end);

static NSString* croft_plain_string_from_marked_value(id value) {
    if (!value) {
        return @"";
    }
    if ([value isKindOfClass:[NSAttributedString class]]) {
        return [(NSAttributedString*)value string];
    }
    if ([value isKindOfClass:[NSString class]]) {
        return (NSString*)value;
    }
    return [value description] ?: @"";
}

static void croft_record_click_count(NSEvent* event) {
    NSInteger click_count = event ? event.clickCount : 0;
    g_last_click_count = click_count > 0 ? (int32_t)click_count : 1;
}

static void croft_emit_marked_text(id value, NSRange selectedRange) {
    NSString* string = croft_plain_string_from_marked_value(value);
    NSData* utf8 = [string dataUsingEncoding:NSUTF8StringEncoding];
    const uint8_t* bytes = utf8 ? (const uint8_t*)utf8.bytes : NULL;
    uint32_t len = utf8 ? (uint32_t)utf8.length : 0u;
    uint32_t selection_start = (uint32_t)selectedRange.location;
    uint32_t selection_end = selection_start + (uint32_t)selectedRange.length;

    if (len == 0u) {
        croft_host_ui_dispatch_composition_event(CROFT_UI_COMPOSITION_CLEAR, NULL, 0u, 0u, 0u);
        return;
    }

    croft_host_ui_dispatch_composition_event(CROFT_UI_COMPOSITION_UPDATE,
                                             bytes,
                                             len,
                                             selection_start,
                                             selection_end);
}

static void croft_mouseDown(id self, SEL _cmd, NSEvent* event) {
    croft_record_click_count(event);
    if (g_original_mouse_down) {
        g_original_mouse_down(self, _cmd, event);
    }
}

static void croft_rightMouseDown(id self, SEL _cmd, NSEvent* event) {
    croft_record_click_count(event);
    if (g_original_right_mouse_down) {
        g_original_right_mouse_down(self, _cmd, event);
    }
}

static void croft_otherMouseDown(id self, SEL _cmd, NSEvent* event) {
    croft_record_click_count(event);
    if (g_original_other_mouse_down) {
        g_original_other_mouse_down(self, _cmd, event);
    }
}

static void croft_setMarkedText(id self,
                                SEL _cmd,
                                id string,
                                NSRange selectedRange,
                                NSRange replacementRange) {
    if (g_original_set_marked_text) {
        g_original_set_marked_text(self, _cmd, string, selectedRange, replacementRange);
    }
    croft_emit_marked_text(string, selectedRange);
}

static void croft_unmarkText(id self, SEL _cmd) {
    if (g_original_unmark_text) {
        g_original_unmark_text(self, _cmd);
    }
    croft_host_ui_dispatch_composition_event(CROFT_UI_COMPOSITION_CLEAR, NULL, 0u, 0u, 0u);
}

int32_t host_text_input_mac_current_click_count(void) {
    NSEvent* event = [NSApp currentEvent];

    if (event) {
        NSEventType type = event.type;
        if (type == NSEventTypeLeftMouseDown
                || type == NSEventTypeRightMouseDown
                || type == NSEventTypeOtherMouseDown) {
            NSInteger click_count = event.clickCount;
            if (click_count > 0) {
                return (int32_t)click_count;
            }
        }
    }

    return g_last_click_count > 0 ? g_last_click_count : 1;
}

void host_text_input_mac_init(void *ns_window_ptr) {
    NSWindow* window;
    NSView* contentView;
    Class viewClass;
    Method markedMethod;
    Method unmarkMethod;
    Method mouseDownMethod;
    Method rightMouseDownMethod;
    Method otherMouseDownMethod;

    if (!ns_window_ptr) {
        return;
    }

    window = (__bridge NSWindow*)ns_window_ptr;
    contentView = window.contentView;
    if (!contentView) {
        return;
    }

    viewClass = [contentView class];
    if (g_hooked_view_class == viewClass) {
        return;
    }

    markedMethod = class_getInstanceMethod(viewClass,
                                           @selector(setMarkedText:selectedRange:replacementRange:));
    unmarkMethod = class_getInstanceMethod(viewClass, @selector(unmarkText));
    mouseDownMethod = class_getInstanceMethod(viewClass, @selector(mouseDown:));
    rightMouseDownMethod = class_getInstanceMethod(viewClass, @selector(rightMouseDown:));
    otherMouseDownMethod = class_getInstanceMethod(viewClass, @selector(otherMouseDown:));
    if (!markedMethod || !unmarkMethod) {
        return;
    }

    g_original_set_marked_text = (CroftSetMarkedTextImp)method_getImplementation(markedMethod);
    g_original_unmark_text = (CroftUnmarkTextImp)method_getImplementation(unmarkMethod);
    class_replaceMethod(viewClass,
                        @selector(setMarkedText:selectedRange:replacementRange:),
                        (IMP)croft_setMarkedText,
                        method_getTypeEncoding(markedMethod));
    class_replaceMethod(viewClass,
                        @selector(unmarkText),
                        (IMP)croft_unmarkText,
                        method_getTypeEncoding(unmarkMethod));
    if (mouseDownMethod) {
        g_original_mouse_down = (CroftMouseDownImp)method_getImplementation(mouseDownMethod);
        class_replaceMethod(viewClass,
                            @selector(mouseDown:),
                            (IMP)croft_mouseDown,
                            method_getTypeEncoding(mouseDownMethod));
    }
    if (rightMouseDownMethod) {
        g_original_right_mouse_down =
            (CroftMouseDownImp)method_getImplementation(rightMouseDownMethod);
        class_replaceMethod(viewClass,
                            @selector(rightMouseDown:),
                            (IMP)croft_rightMouseDown,
                            method_getTypeEncoding(rightMouseDownMethod));
    }
    if (otherMouseDownMethod) {
        g_original_other_mouse_down =
            (CroftMouseDownImp)method_getImplementation(otherMouseDownMethod);
        class_replaceMethod(viewClass,
                            @selector(otherMouseDown:),
                            (IMP)croft_otherMouseDown,
                            method_getTypeEncoding(otherMouseDownMethod));
    }
    g_hooked_view_class = viewClass;
}
