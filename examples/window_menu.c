#include "croft/host_menu.h"
#include "croft/host_ui.h"
#include <stdio.h>

static int g_running = 1;

static void on_menu_action(int32_t action_id) {
    printf("Menu action triggered: ID=%d\n", action_id);
    if (action_id == 99) {
        g_running = 0;
    }
}

int main(void) {
    SapWitMenuSchemaMenuIntent intent;

    if (host_ui_init() != 0) {
        return 1;
    }
    if (host_ui_create_window(800, 600, "Croft Window + Menu Demo") != 0) {
        host_ui_terminate();
        return 1;
    }

    host_menu_set_callback(on_menu_action);

    intent.case_tag = SAP_WIT_MENU_SCHEMA_MENU_INTENT_BEGIN_UPDATE;
    host_menu_apply_intent(&intent);

    intent.case_tag = SAP_WIT_MENU_SCHEMA_MENU_INTENT_ADD_ITEM;
    intent.val.add_item.action_id = 1;
    intent.val.add_item.parent_action_id = -1;
    intent.val.add_item.label_data = (const uint8_t*)"App";
    intent.val.add_item.label_len = 3;
    intent.val.add_item.has_shortcut = 0;
    intent.val.add_item.mods = 0;
    host_menu_apply_intent(&intent);

    intent.case_tag = SAP_WIT_MENU_SCHEMA_MENU_INTENT_ADD_ITEM;
    intent.val.add_item.action_id = 99;
    intent.val.add_item.parent_action_id = 1;
    intent.val.add_item.label_data = (const uint8_t*)"Quit Croft";
    intent.val.add_item.label_len = 10;
    intent.val.add_item.has_shortcut = 1;
    intent.val.add_item.shortcut_data = (const uint8_t*)"q";
    intent.val.add_item.shortcut_len = 1;
    intent.val.add_item.mods = SAP_WIT_MENU_SCHEMA_MODIFIERS_CMD;
    host_menu_apply_intent(&intent);

    intent.case_tag = SAP_WIT_MENU_SCHEMA_MENU_INTENT_COMMIT_UPDATE;
    host_menu_apply_intent(&intent);

    printf("Menu registered natively. Use Cmd+Q or the Quit menu item to exit.\n");

    while (g_running && !host_ui_should_close()) {
        host_ui_poll_events();
    }

    host_ui_terminate();
    printf("Graceful shutdown completed.\n");
    return 0;
}
