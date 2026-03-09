#include "croft/wit_host_popup_menu_runtime.h"

static int wit_host_popup_menu_expect_status_ok(const SapWitHostPopupMenuReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_POPUP_MENU_REPLY_STATUS
        && reply->val.status.case_tag == SAP_WIT_HOST_POPUP_MENU_STATUS_OK;
}

static int wit_host_popup_menu_expect_action_empty(const SapWitHostPopupMenuReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_POPUP_MENU_REPLY_ACTION
        && reply->val.action.case_tag == SAP_WIT_HOST_POPUP_MENU_ACTION_RESULT_EMPTY;
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
            || reply.case_tag != SAP_WIT_HOST_POPUP_MENU_REPLY_ACTION
            || reply.val.action.case_tag != SAP_WIT_HOST_POPUP_MENU_ACTION_RESULT_ERR
            || reply.val.action.val.err != SAP_WIT_HOST_POPUP_MENU_ERROR_UNAVAILABLE) {
        croft_wit_host_popup_menu_runtime_destroy(runtime);
        return 1;
    }

    croft_wit_host_popup_menu_runtime_destroy(runtime);
    return 0;
}
