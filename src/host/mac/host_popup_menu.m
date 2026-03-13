#include "croft/host_popup_menu.h"

#include <AppKit/AppKit.h>

static int32_t g_selected_action_id = 0;

@interface CroftPopupMenuProxy : NSObject
@property(nonatomic, assign) int32_t action_id;
- (void)invokeAction:(id)sender;
@end

@implementation CroftPopupMenuProxy
- (void)invokeAction:(id)sender
{
    (void)sender;
    g_selected_action_id = self.action_id;
}
@end

static NSMenu* croft_build_popup_menu(const host_popup_menu_item* items, uint32_t item_count)
{
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Context"];
    uint32_t index;

    for (index = 0u; index < item_count; index++) {
        const host_popup_menu_item* item = &items[index];

        if (item->separator) {
            [menu addItem:[NSMenuItem separatorItem]];
            continue;
        }
        if (!item->label) {
            continue;
        }

        {
            NSString* title = [NSString stringWithUTF8String:item->label];
            CroftPopupMenuProxy* proxy = [[CroftPopupMenuProxy alloc] init];
            NSMenuItem* menu_item;

            if (!title) {
                continue;
            }
            proxy.action_id = item->action_id;
            menu_item = [[NSMenuItem alloc] initWithTitle:title
                                                   action:@selector(invokeAction:)
                                            keyEquivalent:@""];
            [menu_item setTarget:proxy];
            [menu_item setRepresentedObject:proxy];
            [menu_item setEnabled:item->enabled ? YES : NO];
            [menu addItem:menu_item];
        }
    }

    return menu;
}

static NSString* croft_contextual_string(const char* utf8, size_t utf8_len)
{
    NSData* data;
    NSString* string;

    if (!utf8 || utf8_len == 0u) {
        return @"";
    }

    data = [NSData dataWithBytes:utf8 length:utf8_len];
    if (!data) {
        return @"";
    }

    string = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    return string ?: @"";
}

static int croft_is_mouse_down_event(NSEvent* event)
{
    if (!event) {
        return 0;
    }
    return event.type == NSEventTypeLeftMouseDown
        || event.type == NSEventTypeRightMouseDown
        || event.type == NSEventTypeOtherMouseDown;
}

static NSMenu* croft_build_native_text_menu(NSWindow* window,
                                            NSView* parent_view,
                                            NSMenu* custom_menu,
                                            const char* contextual_utf8,
                                            size_t contextual_utf8_len,
                                            NSTextView** helper_out,
                                            NSResponder** previous_first_responder_out)
{
    NSTextView* helper;
    NSString* contextual_string;
    NSEvent* event;
    NSResponder* previous_first_responder;
    NSMenu* menu;

    if (helper_out) {
        *helper_out = nil;
    }
    if (previous_first_responder_out) {
        *previous_first_responder_out = nil;
    }
    if (!window || !parent_view || !custom_menu || !contextual_utf8 || contextual_utf8_len == 0u) {
        return custom_menu;
    }

    event = [NSApp currentEvent];
    if (!croft_is_mouse_down_event(event)) {
        return custom_menu;
    }

    helper = [[NSTextView alloc] initWithFrame:NSMakeRect(0.0, 0.0, 1.0, 1.0)];
    if (!helper) {
        return custom_menu;
    }

    contextual_string = croft_contextual_string(contextual_utf8, contextual_utf8_len);
    [helper setRichText:NO];
    [helper setEditable:NO];
    [helper setSelectable:YES];
    [helper setString:contextual_string];
    [helper setSelectedRange:NSMakeRange(0u, contextual_string.length)];
    [helper setMenu:custom_menu];
    [parent_view addSubview:helper];

    previous_first_responder = [window firstResponder];
    [window makeFirstResponder:helper];
    menu = [helper menuForEvent:event];
    if (!menu) {
        menu = custom_menu;
    }
    if (helper_out) {
        *helper_out = helper;
    }
    if (previous_first_responder_out) {
        *previous_first_responder_out = previous_first_responder;
    }
    return menu;
}

host_popup_menu_result host_popup_menu_show_with_context(const host_popup_menu_item* items,
                                                         uint32_t item_count,
                                                         float x,
                                                         float y,
                                                         const char* contextual_utf8,
                                                         size_t contextual_utf8_len,
                                                         uint8_t include_native_text_services,
                                                         int32_t* action_id_out)
{
    NSWindow* window;
    NSView* view;
    NSMenu* custom_menu;
    NSMenu* menu;
    NSTextView* helper = nil;
    NSResponder* previous_first_responder = nil;
    NSEvent* event;
    NSPoint location;
    int used_native_text_menu = 0;

    if (action_id_out) {
        *action_id_out = 0;
    }

    if (!action_id_out) {
        return HOST_POPUP_MENU_RESULT_INTERNAL;
    }
    if (!items || item_count == 0u) {
        return HOST_POPUP_MENU_RESULT_EMPTY;
    }

    window = [NSApp keyWindow];
    if (!window) {
        window = [NSApp mainWindow];
    }
    if (!window) {
        return HOST_POPUP_MENU_RESULT_UNAVAILABLE;
    }

    view = window.contentView;
    if (!view) {
        return HOST_POPUP_MENU_RESULT_UNAVAILABLE;
    }

    custom_menu = croft_build_popup_menu(items, item_count);
    menu = custom_menu;
    if (include_native_text_services) {
        menu = croft_build_native_text_menu(window,
                                            view,
                                            custom_menu,
                                            contextual_utf8,
                                            contextual_utf8_len,
                                            &helper,
                                            &previous_first_responder);
        used_native_text_menu = helper != nil;
    }

    g_selected_action_id = 0;
    event = [NSApp currentEvent];
    if (used_native_text_menu && croft_is_mouse_down_event(event)) {
        [NSMenu popUpContextMenu:menu withEvent:event forView:view];
    } else {
        location = NSMakePoint((CGFloat)x, NSHeight(view.bounds) - (CGFloat)y);
        [menu popUpMenuPositioningItem:nil atLocation:location inView:view];
    }
    if (helper) {
        if (previous_first_responder && previous_first_responder != helper) {
            [window makeFirstResponder:previous_first_responder];
        }
        [helper removeFromSuperview];
    }
    if (g_selected_action_id == 0) {
        return HOST_POPUP_MENU_RESULT_EMPTY;
    }

    *action_id_out = g_selected_action_id;
    return HOST_POPUP_MENU_RESULT_OK;
}

host_popup_menu_result host_popup_menu_show(const host_popup_menu_item* items,
                                            uint32_t item_count,
                                            float x,
                                            float y,
                                            int32_t* action_id_out)
{
    return host_popup_menu_show_with_context(items,
                                             item_count,
                                             x,
                                             y,
                                             NULL,
                                             0u,
                                             0u,
                                             action_id_out);
}
