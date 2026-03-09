#include "croft/wit_host_popup_menu_runtime.h"

#include <string.h>

static int wit_host_popup_menu_expect_status_ok(const SapWitHostPopupMenuReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_POPUP_MENU_REPLY_STATUS
        && reply->val.status.is_v_ok;
}

static int wit_host_popup_menu_expect_action_empty(const SapWitHostPopupMenuReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_POPUP_MENU_REPLY_ACTION
        && reply->val.action.is_v_ok
        && !reply->val.action.v_val.ok.has_v;
}

static int wit_host_popup_menu_expect_error(const SapWitHostPopupMenuReply* reply, const char* expected)
{
    size_t expected_len;

    if (!reply || !expected) {
        return 0;
    }
    expected_len = strlen(expected);
    return reply->case_tag == SAP_WIT_HOST_POPUP_MENU_REPLY_ACTION
        && !reply->val.action.is_v_ok
        && reply->val.action.v_val.err.v_len == (uint32_t)expected_len
        && memcmp(reply->val.action.v_val.err.v_data, expected, expected_len) == 0;
}

int test_wit_host_popup_menu_runtime_empty_popup(void)
{
    croft_wit_host_popup_menu_runtime* runtime;
    SapWitHostPopupMenuCommand command = {0};
    SapWitHostPopupMenuReply reply = {0};

    runtime = croft_wit_host_popup_menu_runtime_create();
    if (!runtime) {
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_POPUP_MENU_COMMAND_BEGIN_POPUP;
    if (croft_wit_host_popup_menu_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_host_popup_menu_expect_status_ok(&reply)) {
        croft_wit_host_popup_menu_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_POPUP_MENU_COMMAND_SHOW_AT;
    command.val.show_at.x_milli = 4000;
    command.val.show_at.y_milli = 7000;
    if (croft_wit_host_popup_menu_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_host_popup_menu_expect_action_empty(&reply)) {
        croft_wit_host_popup_menu_runtime_destroy(runtime);
        return 1;
    }

    croft_wit_host_popup_menu_runtime_destroy(runtime);
    return 0;
}

int test_wit_host_popup_menu_runtime_unavailable_without_window(void)
{
    croft_wit_host_popup_menu_runtime* runtime;
    SapWitHostPopupMenuCommand command = {0};
    SapWitHostPopupMenuReply reply = {0};

    runtime = croft_wit_host_popup_menu_runtime_create();
    if (!runtime) {
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_POPUP_MENU_COMMAND_BEGIN_POPUP;
    if (croft_wit_host_popup_menu_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_host_popup_menu_expect_status_ok(&reply)) {
        croft_wit_host_popup_menu_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_POPUP_MENU_COMMAND_ADD_ITEM;
    command.val.add_item.action_id = 201;
    command.val.add_item.label_data = (const uint8_t*)"Open...";
    command.val.add_item.label_len = 7u;
    command.val.add_item.enabled = 1u;
    command.val.add_item.separator = 0u;
    if (croft_wit_host_popup_menu_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_host_popup_menu_expect_status_ok(&reply)) {
        croft_wit_host_popup_menu_runtime_destroy(runtime);
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_POPUP_MENU_COMMAND_SHOW_AT;
    command.val.show_at.x_milli = 4000;
    command.val.show_at.y_milli = 7000;
    if (croft_wit_host_popup_menu_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_host_popup_menu_expect_error(&reply, "unavailable")) {
        croft_wit_host_popup_menu_runtime_destroy(runtime);
        return 1;
    }

    croft_wit_host_popup_menu_runtime_destroy(runtime);
    return 0;
}
