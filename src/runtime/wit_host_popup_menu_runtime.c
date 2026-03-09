#include "croft/wit_host_popup_menu_runtime.h"

#include "croft/host_popup_menu.h"

#include <stdlib.h>
#include <string.h>

#define CROFT_WIT_HOST_POPUP_MENU_ITEM_CAP 32u

struct croft_wit_host_popup_menu_runtime {
    host_popup_menu_item items[CROFT_WIT_HOST_POPUP_MENU_ITEM_CAP];
    char* labels[CROFT_WIT_HOST_POPUP_MENU_ITEM_CAP];
    uint32_t item_count;
};

static void croft_wit_host_popup_menu_reset(croft_wit_host_popup_menu_runtime* runtime)
{
    uint32_t index;

    if (!runtime) {
        return;
    }

    for (index = 0u; index < runtime->item_count; index++) {
        free(runtime->labels[index]);
        runtime->labels[index] = NULL;
    }
    memset(runtime->items, 0, sizeof(runtime->items));
    runtime->item_count = 0u;
}

static void croft_wit_host_popup_menu_reply_zero(SapWitHostPopupMenuReply* reply)
{
    if (reply) {
        memset(reply, 0, sizeof(*reply));
    }
}

static void croft_wit_host_popup_menu_reply_status_ok(SapWitHostPopupMenuReply* reply)
{
    croft_wit_host_popup_menu_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_POPUP_MENU_REPLY_STATUS;
    reply->val.status.case_tag = SAP_WIT_HOST_POPUP_MENU_STATUS_OK;
}

static void croft_wit_host_popup_menu_reply_status_err(SapWitHostPopupMenuReply* reply, uint8_t err)
{
    croft_wit_host_popup_menu_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_POPUP_MENU_REPLY_STATUS;
    reply->val.status.case_tag = SAP_WIT_HOST_POPUP_MENU_STATUS_ERR;
    reply->val.status.val.err = err;
}

static void croft_wit_host_popup_menu_reply_action_ok(SapWitHostPopupMenuReply* reply, int32_t action_id)
{
    croft_wit_host_popup_menu_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_POPUP_MENU_REPLY_ACTION;
    reply->val.action.case_tag = SAP_WIT_HOST_POPUP_MENU_ACTION_RESULT_OK;
    reply->val.action.val.ok = action_id;
}

static void croft_wit_host_popup_menu_reply_action_empty(SapWitHostPopupMenuReply* reply)
{
    croft_wit_host_popup_menu_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_POPUP_MENU_REPLY_ACTION;
    reply->val.action.case_tag = SAP_WIT_HOST_POPUP_MENU_ACTION_RESULT_EMPTY;
}

static void croft_wit_host_popup_menu_reply_action_err(SapWitHostPopupMenuReply* reply, uint8_t err)
{
    croft_wit_host_popup_menu_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_POPUP_MENU_REPLY_ACTION;
    reply->val.action.case_tag = SAP_WIT_HOST_POPUP_MENU_ACTION_RESULT_ERR;
    reply->val.action.val.err = err;
}

static uint8_t croft_wit_host_popup_menu_map_result(host_popup_menu_result result)
{
    switch (result) {
        case HOST_POPUP_MENU_RESULT_UNAVAILABLE:
            return SAP_WIT_HOST_POPUP_MENU_ERROR_UNAVAILABLE;
        case HOST_POPUP_MENU_RESULT_INTERNAL:
            return SAP_WIT_HOST_POPUP_MENU_ERROR_INTERNAL;
        default:
            return SAP_WIT_HOST_POPUP_MENU_ERROR_INTERNAL;
    }
}

static int croft_wit_host_popup_menu_add_item(croft_wit_host_popup_menu_runtime* runtime,
                                              const SapWitHostPopupMenuPopupItem* item)
{
    host_popup_menu_item* popup_item;
    char* label_copy = NULL;

    if (!runtime || !item) {
        return 0;
    }
    if (runtime->item_count >= CROFT_WIT_HOST_POPUP_MENU_ITEM_CAP) {
        return 0;
    }

    if (!item->separator) {
        if (!item->label_data && item->label_len != 0u) {
            return 0;
        }
        label_copy = (char*)malloc((size_t)item->label_len + 1u);
        if (!label_copy) {
            return 0;
        }
        if (item->label_len != 0u) {
            memcpy(label_copy, item->label_data, item->label_len);
        }
        label_copy[item->label_len] = '\0';
    }

    popup_item = &runtime->items[runtime->item_count];
    popup_item->action_id = item->action_id;
    popup_item->label = label_copy;
    popup_item->enabled = item->enabled;
    popup_item->separator = item->separator;
    runtime->labels[runtime->item_count] = label_copy;
    runtime->item_count++;
    return 1;
}

croft_wit_host_popup_menu_runtime* croft_wit_host_popup_menu_runtime_create(void)
{
    return (croft_wit_host_popup_menu_runtime*)calloc(1u, sizeof(croft_wit_host_popup_menu_runtime));
}

void croft_wit_host_popup_menu_runtime_destroy(croft_wit_host_popup_menu_runtime* runtime)
{
    if (!runtime) {
        return;
    }
    croft_wit_host_popup_menu_reset(runtime);
    free(runtime);
}

int32_t croft_wit_host_popup_menu_runtime_dispatch(croft_wit_host_popup_menu_runtime* runtime,
                                                   const SapWitHostPopupMenuCommand* command,
                                                   SapWitHostPopupMenuReply* reply_out)
{
    if (!runtime || !command || !reply_out) {
        return -1;
    }

    switch (command->case_tag) {
        case SAP_WIT_HOST_POPUP_MENU_COMMAND_BEGIN_POPUP:
            croft_wit_host_popup_menu_reset(runtime);
            croft_wit_host_popup_menu_reply_status_ok(reply_out);
            return 0;
        case SAP_WIT_HOST_POPUP_MENU_COMMAND_ADD_ITEM:
            if (!croft_wit_host_popup_menu_add_item(runtime, &command->val.add_item)) {
                croft_wit_host_popup_menu_reply_status_err(reply_out, SAP_WIT_HOST_POPUP_MENU_ERROR_INTERNAL);
                return 0;
            }
            croft_wit_host_popup_menu_reply_status_ok(reply_out);
            return 0;
        case SAP_WIT_HOST_POPUP_MENU_COMMAND_SHOW_AT: {
            int32_t action_id = 0;
            host_popup_menu_result result = host_popup_menu_show(runtime->items,
                                                                 runtime->item_count,
                                                                 (float)command->val.show_at.x_milli / 1000.0f,
                                                                 (float)command->val.show_at.y_milli / 1000.0f,
                                                                 &action_id);
            if (result == HOST_POPUP_MENU_RESULT_OK) {
                croft_wit_host_popup_menu_reply_action_ok(reply_out, action_id);
            } else if (result == HOST_POPUP_MENU_RESULT_EMPTY) {
                croft_wit_host_popup_menu_reply_action_empty(reply_out);
            } else {
                croft_wit_host_popup_menu_reply_action_err(reply_out,
                                                           croft_wit_host_popup_menu_map_result(result));
            }
            return 0;
        }
        default:
            croft_wit_host_popup_menu_reply_status_err(reply_out, SAP_WIT_HOST_POPUP_MENU_ERROR_INTERNAL);
            return 0;
    }
}
