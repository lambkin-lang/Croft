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

host_popup_menu_result host_popup_menu_show(const host_popup_menu_item* items,
                                            uint32_t item_count,
                                            float x,
                                            float y,
                                            int32_t* action_id_out)
{
    NSWindow* window;
    NSView* view;
    NSMenu* menu;
    NSPoint location;
    uint32_t index;

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

    menu = [[NSMenu alloc] initWithTitle:@"Context"];
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

    g_selected_action_id = 0;
    location = NSMakePoint((CGFloat)x, NSHeight(view.bounds) - (CGFloat)y);
    [menu popUpMenuPositioningItem:nil atLocation:location inView:view];
    if (g_selected_action_id == 0) {
        return HOST_POPUP_MENU_RESULT_EMPTY;
    }

    *action_id_out = g_selected_action_id;
    return HOST_POPUP_MENU_RESULT_OK;
}
