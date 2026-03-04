#include "croft/host_ui.h"
#include "croft/host_menu.h"
#include <stdio.h>
#include <string.h>

static int g_running = 1;

void on_menu_action(int32_t action_id) {
    printf("Menu Action Triggered: ID=%d\n", action_id);
    if (action_id == 99) {
        printf("Quit command received via Cmd+Q or click! Terminating UI.\n");
        // Flip the running state to allow the event loop to exit smoothly
        g_running = 0;
    }
}

int main(void) {
    if (host_ui_init() != 0) {
        return 1;
    }
    if (host_ui_create_window(800, 600, "Croft Menu Verification") != 0) {
        return 1;
    }

    host_menu_set_callback(on_menu_action);

    // Stream the intents!
    SapWitMenuIntent intent;

    // 1. Begin Update
    intent.case_tag = SAP_WIT_MENU_INTENT_BEGIN_UPDATE;
    host_menu_apply_intent(&intent);

    // 2. Add App Menu (Root)
    intent.case_tag = SAP_WIT_MENU_INTENT_ADD_ITEM;
    intent.val.add_item.action_id = 1;
    intent.val.add_item.parent_action_id = -1;
    intent.val.add_item.label_data = (const uint8_t*)"App";
    intent.val.add_item.label_len = 3;
    intent.val.add_item.has_shortcut = 0;
    intent.val.add_item.mods = 0;
    host_menu_apply_intent(&intent);

    // 3. Add Quit item underneath App Menu
    intent.case_tag = SAP_WIT_MENU_INTENT_ADD_ITEM;
    intent.val.add_item.action_id = 99;
    intent.val.add_item.parent_action_id = 1;
    intent.val.add_item.label_data = (const uint8_t*)"Quit Croft";
    intent.val.add_item.label_len = 10;
    intent.val.add_item.has_shortcut = 1;
    intent.val.add_item.shortcut_data = (const uint8_t*)"q";
    intent.val.add_item.shortcut_len = 1;
    intent.val.add_item.mods = SAP_WIT_MODIFIERS_CMD; // Bind Cmd+Q explicitly
    host_menu_apply_intent(&intent);

    // 4. Commit Update
    intent.case_tag = SAP_WIT_MENU_INTENT_COMMIT_UPDATE;
    host_menu_apply_intent(&intent);

    printf("Menu registered natively! Please verify the Apple Menu Bar, then press Cmd+Q or click 'Quit Croft' to exit.\n");

    while (g_running && !host_ui_should_close()) {
        host_ui_poll_events();
    }

    printf("Graceful Shutdown completed.\n");
    host_ui_terminate();
    return 0;
}
