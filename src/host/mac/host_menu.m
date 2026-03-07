#include "croft/host_menu.h"
#include <AppKit/AppKit.h>
#include <TargetConditionals.h>

static host_menu_callback_t g_callback = NULL;
static NSMutableDictionary<NSNumber*, NSMenuItem*>* g_item_map = nil;
static NSMenu* g_root_menu = nil;

@interface CroftMenuProxy : NSObject
@property (nonatomic, assign) int32_t action_id;
- (void)invokeAction:(id)sender;
@end

@implementation CroftMenuProxy
- (void)invokeAction:(id)sender {
    if (g_callback) {
        g_callback(self.action_id);
    }
}
@end

void host_menu_set_callback(host_menu_callback_t cb) {
    g_callback = cb;
}

void host_menu_reset(void) {
    if (g_item_map) {
        [g_item_map removeAllObjects];
        g_item_map = nil;
    }
    g_root_menu = nil;
    if (NSApp) {
        [NSApp setMainMenu:nil];
    }
}

void host_menu_apply_intent(const SapWitMenuSchemaMenuIntent* intent) {
    if (intent->case_tag == SAP_WIT_MENU_SCHEMA_MENU_INTENT_BEGIN_UPDATE) {
        host_menu_reset();
        g_item_map = [[NSMutableDictionary alloc] init];
        g_root_menu = [[NSMenu alloc] initWithTitle:@"Main Menu"];
    } 
    else if (intent->case_tag == SAP_WIT_MENU_SCHEMA_MENU_INTENT_ADD_ITEM) {
        const SapWitMenuSchemaMenuItem* md = &intent->val.add_item;
        
        NSString* title = [[NSString alloc] initWithBytes:md->label_data 
                                                   length:md->label_len 
                                                 encoding:NSUTF8StringEncoding];
        
        NSString* keyEq = @"";
        if (md->has_shortcut) {
            keyEq = [[NSString alloc] initWithBytes:md->shortcut_data 
                                             length:md->shortcut_len 
                                           encoding:NSUTF8StringEncoding];
        }
        
        // Proxy Object to handle dynamic action routing
        CroftMenuProxy* proxy = [[CroftMenuProxy alloc] init];
        proxy.action_id = md->action_id;
        
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title 
                                                      action:@selector(invokeAction:) 
                                               keyEquivalent:keyEq];
        item.target = proxy;
        
        // Retain proxy via representedObject so it isn't garbage collected under ARC
        [item setRepresentedObject:proxy];

        // Apple Mac Modifiers
        NSEventModifierFlags mask = 0;
        if (md->mods & SAP_WIT_MENU_SCHEMA_MODIFIERS_CMD)   mask |= NSEventModifierFlagCommand;
        if (md->mods & SAP_WIT_MENU_SCHEMA_MODIFIERS_SHIFT) mask |= NSEventModifierFlagShift;
        if (md->mods & SAP_WIT_MENU_SCHEMA_MODIFIERS_CTRL)  mask |= NSEventModifierFlagControl;
        if (md->mods & SAP_WIT_MENU_SCHEMA_MODIFIERS_ALT)   mask |= NSEventModifierFlagOption;
        
        if (md->has_shortcut) {
            [item setKeyEquivalentModifierMask:mask];
        }
        
        g_item_map[@(md->action_id)] = item;
        
        // Attach to hierarchy
        if (md->parent_action_id == -1) {
            [g_root_menu addItem:item];
        } else {
            NSMenuItem* parent = g_item_map[@(md->parent_action_id)];
            if (parent) {
                if (!parent.submenu) {
                    parent.submenu = [[NSMenu alloc] initWithTitle:parent.title];
                }
                [parent.submenu addItem:item];
            }
        }
    }
    else if (intent->case_tag == SAP_WIT_MENU_SCHEMA_MENU_INTENT_COMMIT_UPDATE) {
        // Force the OS Global Application Menu to adopt our WIT-formulated tree
        if (NSApp) {
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            [NSApp setMainMenu:g_root_menu];
            [NSApp activateIgnoringOtherApps:YES];
        }
    }
}
