#include "croft/host_popup_menu.h"
#include "croft/editor_typography_macos.h"

#include <AppKit/AppKit.h>
#include <objc/runtime.h>

static int32_t g_selected_action_id = 0;
static NSTextView* g_pending_context_helper = nil;
static NSWindow* g_pending_context_window = nil;
static NSResponder* g_pending_previous_first_responder = nil;
static const void* g_croft_popup_retained_target_key = &g_croft_popup_retained_target_key;

static NSPoint croft_popup_view_point(NSView* view, float x, float y);
static NSEvent* croft_context_menu_event(NSWindow* window, NSView* view, float x, float y);

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

@interface CroftDefinitionMenuProxy : NSObject
@property(nonatomic, weak) NSView* anchor_view;
@property(nonatomic, strong) NSAttributedString* attributed_string;
@property(nonatomic, assign) NSPoint baseline_origin;
- (void)showDefinition:(id)sender;
@end

@implementation CroftDefinitionMenuProxy
- (void)showDefinition:(id)sender
{
    (void)sender;
    [self.anchor_view showDefinitionForAttributedString:self.attributed_string
                                                atPoint:self.baseline_origin];
}
@end

static NSPoint croft_popup_view_point(NSView* view, float x, float y)
{
    CGFloat height;

    if (!view) {
        return NSMakePoint((CGFloat)x, (CGFloat)y);
    }

    height = NSHeight(view.bounds);
    return NSMakePoint((CGFloat)x, height - (CGFloat)y);
}

static NSEvent* croft_context_menu_event(NSWindow* window, NSView* view, float x, float y)
{
    NSEvent* source_event;
    NSPoint view_point;
    NSPoint window_point;
    NSEventModifierFlags modifier_flags = 0;
    NSTimeInterval timestamp = 0.0;
    NSInteger event_number = 0;

    if (!window || !view) {
        return nil;
    }

    source_event = [NSApp currentEvent];
    if (source_event) {
        modifier_flags = source_event.modifierFlags;
        timestamp = source_event.timestamp;
        event_number = source_event.eventNumber;
    }

    view_point = croft_popup_view_point(view, x, y);
    window_point = [view convertPoint:view_point toView:nil];
    return [NSEvent mouseEventWithType:NSEventTypeRightMouseDown
                              location:window_point
                         modifierFlags:modifier_flags
                             timestamp:timestamp
                          windowNumber:window.windowNumber
                               context:nil
                           eventNumber:event_number
                            clickCount:1
                              pressure:1.0];
}

static void croft_retain_menu_targets(NSMenu* menu)
{
    NSInteger index;

    if (!menu) {
        return;
    }

    for (index = 0; index < menu.numberOfItems; index++) {
        NSMenuItem* item = [menu itemAtIndex:index];

        if (!item || item.separatorItem) {
            continue;
        }
        if (item.target) {
            objc_setAssociatedObject(item,
                                     g_croft_popup_retained_target_key,
                                     item.target,
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
        if (item.hasSubmenu) {
            croft_retain_menu_targets(item.submenu);
        }
    }
}

static int croft_menu_item_is_native_lookup(NSMenuItem* item)
{
    NSString* title;
    NSString* selector_name;

    if (!item || item.separatorItem) {
        return 0;
    }
    title = item.title ?: @"";
    selector_name = item.action ? NSStringFromSelector(item.action) : @"";
    if ([title hasPrefix:@"Look Up"]) {
        return 1;
    }
    return [selector_name isEqualToString:@"_rvMenuItemAction"]
        && [NSStringFromClass([item.target class]) isEqualToString:@"RVMenuItem"];
}

static void croft_replace_native_lookup_item(NSMenu* menu,
                                             NSView* anchor_view,
                                             NSString* contextual_string,
                                             NSPoint baseline_origin,
                                             CGFloat font_size)
{
    NSInteger index;
    NSString* lookup_title = nil;
    CroftDefinitionMenuProxy* proxy;
    NSMenuItem* replacement;
    NSFont* font;
    NSDictionary* attributes;

    if (!menu || !anchor_view || !contextual_string || contextual_string.length == 0u) {
        return;
    }

    for (index = 0; index < menu.numberOfItems; index++) {
        NSMenuItem* item = [menu itemAtIndex:index];

        if (!croft_menu_item_is_native_lookup(item)) {
            continue;
        }
        lookup_title = item.title;
        [menu removeItemAtIndex:index];
        break;
    }

    if (!lookup_title) {
        lookup_title = [NSString stringWithFormat:@"Look Up \"%@\"", contextual_string];
    }

    proxy = [[CroftDefinitionMenuProxy alloc] init];
    font = croft_editor_mac_monospace_font(font_size > 0.0 ? font_size : 15.0);
    attributes = font
        ? @{ NSFontAttributeName: font, NSForegroundColorAttributeName: [NSColor textColor] }
        : @{ NSForegroundColorAttributeName: [NSColor textColor] };
    proxy.anchor_view = anchor_view;
    proxy.attributed_string = [[NSAttributedString alloc] initWithString:contextual_string
                                                              attributes:attributes];
    proxy.baseline_origin = baseline_origin;

    replacement = [[NSMenuItem alloc] initWithTitle:lookup_title
                                             action:@selector(showDefinition:)
                                      keyEquivalent:@""];
    [replacement setTarget:proxy];
    objc_setAssociatedObject(replacement,
                             g_croft_popup_retained_target_key,
                             proxy,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [menu insertItem:replacement atIndex:0];
}

@interface CroftPopupMenuCleanupProxy : NSObject
+ (CroftPopupMenuCleanupProxy*)sharedProxy;
- (void)performDeferredCleanup:(id)sender;
@end

@implementation CroftPopupMenuCleanupProxy
+ (CroftPopupMenuCleanupProxy*)sharedProxy
{
    static CroftPopupMenuCleanupProxy* proxy = nil;

    if (!proxy) {
        proxy = [[CroftPopupMenuCleanupProxy alloc] init];
    }
    return proxy;
}

- (void)performDeferredCleanup:(id)sender
{
    (void)sender;
    if (g_pending_context_helper) {
        if (g_pending_context_window
                && g_pending_previous_first_responder
                && g_pending_previous_first_responder != g_pending_context_helper) {
            [g_pending_context_window makeFirstResponder:g_pending_previous_first_responder];
        }
        [g_pending_context_helper removeFromSuperview];
    }
    g_pending_context_helper = nil;
    g_pending_context_window = nil;
    g_pending_previous_first_responder = nil;
}
@end

static void croft_cleanup_pending_context_helper(void)
{
    CroftPopupMenuCleanupProxy* proxy = [CroftPopupMenuCleanupProxy sharedProxy];

    [NSObject cancelPreviousPerformRequestsWithTarget:proxy
                                             selector:@selector(performDeferredCleanup:)
                                               object:nil];
    [proxy performDeferredCleanup:nil];
}

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
                                            NSEvent* popup_event,
                                            const host_popup_menu_text_context* text_context,
                                            NSTextView** helper_out,
                                            NSResponder** previous_first_responder_out)
{
    NSTextView* helper;
    NSString* contextual_string;
    NSResponder* previous_first_responder;
    NSMenu* menu;
    NSFont* helper_font;
    NSPoint baseline_origin = NSZeroPoint;

    if (helper_out) {
        *helper_out = nil;
    }
    if (previous_first_responder_out) {
        *previous_first_responder_out = nil;
    }
    if (!window || !parent_view || !custom_menu || !text_context || !text_context->utf8
            || text_context->utf8_len == 0u) {
        return custom_menu;
    }

    if (!popup_event || !croft_is_mouse_down_event(popup_event)) {
        return custom_menu;
    }

    contextual_string = croft_contextual_string(text_context->utf8, text_context->utf8_len);
    helper_font = croft_editor_mac_monospace_font(text_context->font_size > 0.0
                                                      ? text_context->font_size
                                                      : 15.0);
    baseline_origin = NSMakePoint((CGFloat)text_context->baseline_x,
                                  NSHeight(parent_view.bounds) - (CGFloat)text_context->baseline_y);

    helper = [[NSTextView alloc] initWithFrame:parent_view.bounds];
    if (!helper) {
        return custom_menu;
    }

    [helper setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [helper setRichText:NO];
    [helper setEditable:NO];
    [helper setSelectable:YES];
    [helper setDrawsBackground:NO];
    [helper setTextColor:[NSColor clearColor]];
    [helper setInsertionPointColor:[NSColor clearColor]];
    [helper setSelectedTextAttributes:@{
        NSForegroundColorAttributeName: [NSColor clearColor],
        NSBackgroundColorAttributeName: [NSColor clearColor]
    }];
    [helper setFont:helper_font];
    [helper setHorizontallyResizable:NO];
    [helper setVerticallyResizable:NO];
    [helper setTextContainerInset:NSMakeSize(0.0, 0.0)];
    [helper.textContainer setWidthTracksTextView:NO];
    [helper.textContainer setHeightTracksTextView:NO];
    [helper.textContainer setContainerSize:parent_view.bounds.size];
    [helper.textContainer setLineFragmentPadding:0.0];
    [helper setString:contextual_string];
    [helper setSelectedRange:NSMakeRange(0u, contextual_string.length)];
    [helper setMenu:custom_menu];
    [parent_view addSubview:helper];

    previous_first_responder = [window firstResponder];
    [window makeFirstResponder:helper];
    menu = [helper menuForEvent:popup_event];
    if (!menu) {
        menu = custom_menu;
    }
    croft_replace_native_lookup_item(menu,
                                     parent_view,
                                     contextual_string,
                                     baseline_origin,
                                     text_context->font_size);
    croft_retain_menu_targets(menu);
    if (helper_out) {
        *helper_out = helper;
    }
    if (previous_first_responder_out) {
        *previous_first_responder_out = previous_first_responder;
    }
    return menu;
}

host_popup_menu_result host_popup_menu_show_with_text_context(
    const host_popup_menu_item* items,
    uint32_t item_count,
    float x,
    float y,
    const host_popup_menu_text_context* text_context,
    int32_t* action_id_out)
{
    NSWindow* window;
    NSView* view;
    NSMenu* custom_menu;
    NSMenu* menu;
    NSTextView* helper = nil;
    NSResponder* previous_first_responder = nil;
    NSEvent* event;
    NSEvent* popup_event = nil;
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

    croft_cleanup_pending_context_helper();

    view = window.contentView;
    if (!view) {
        return HOST_POPUP_MENU_RESULT_UNAVAILABLE;
    }

    custom_menu = croft_build_popup_menu(items, item_count);
    menu = custom_menu;
    if (text_context && text_context->include_native_text_services) {
        popup_event = croft_context_menu_event(window, view, x, y);
        menu = croft_build_native_text_menu(window,
                                            view,
                                            custom_menu,
                                            popup_event,
                                            text_context,
                                            &helper,
                                            &previous_first_responder);
        used_native_text_menu = helper != nil;
    }

    g_selected_action_id = 0;
    event = used_native_text_menu ? popup_event : [NSApp currentEvent];
    if (used_native_text_menu && croft_is_mouse_down_event(event)) {
        [NSMenu popUpContextMenu:menu withEvent:event forView:helper];
    } else {
        location = croft_popup_view_point(view, x, y);
        [menu popUpMenuPositioningItem:nil atLocation:location inView:view];
    }
    if (helper) {
        CroftPopupMenuCleanupProxy* proxy = [CroftPopupMenuCleanupProxy sharedProxy];

        g_pending_context_helper = helper;
        g_pending_context_window = window;
        g_pending_previous_first_responder = previous_first_responder;
        [NSObject cancelPreviousPerformRequestsWithTarget:proxy
                                                 selector:@selector(performDeferredCleanup:)
                                                   object:nil];
        [proxy performSelector:@selector(performDeferredCleanup:)
                    withObject:nil
                    afterDelay:0.0];
    }
    if (g_selected_action_id == 0) {
        return HOST_POPUP_MENU_RESULT_EMPTY;
    }

    *action_id_out = g_selected_action_id;
    return HOST_POPUP_MENU_RESULT_OK;
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
    host_popup_menu_text_context text_context = {
        .utf8 = contextual_utf8,
        .utf8_len = contextual_utf8_len,
        .baseline_x = x,
        .baseline_y = y,
        .font_size = 15.0f,
        .include_native_text_services = include_native_text_services
    };

    return host_popup_menu_show_with_text_context(items,
                                                  item_count,
                                                  x,
                                                  y,
                                                  include_native_text_services ? &text_context : NULL,
                                                  action_id_out);
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
