#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

#include "croft/host_ui.h"

typedef void (*CroftSetMarkedTextImp)(id self,
                                      SEL _cmd,
                                      id string,
                                      NSRange selectedRange,
                                      NSRange replacementRange);
typedef void (*CroftUnmarkTextImp)(id self, SEL _cmd);

static CroftSetMarkedTextImp g_original_set_marked_text = NULL;
static CroftUnmarkTextImp g_original_unmark_text = NULL;
static Class g_hooked_view_class = Nil;

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

void host_text_input_mac_init(void *ns_window_ptr) {
    NSWindow* window;
    NSView* contentView;
    Class viewClass;
    Method markedMethod;
    Method unmarkMethod;

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
    g_hooked_view_class = viewClass;
}
