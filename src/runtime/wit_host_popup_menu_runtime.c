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
    sap_wit_zero_host_popup_menu_reply(reply);
}

static void croft_wit_set_string_view(const char* text,
                                      const uint8_t** data_out,
                                      uint32_t* len_out)
{
    if (!data_out || !len_out) {
        return;
    }
    if (!text) {
        text = "";
    }
    *data_out = (const uint8_t*)text;
    *len_out = (uint32_t)strlen(text);
}

static void croft_wit_host_popup_menu_reply_status_ok(SapWitHostPopupMenuReply* reply)
{
    croft_wit_host_popup_menu_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_POPUP_MENU_REPLY_STATUS;
    reply->val.status.is_v_ok = 1u;
}

static void croft_wit_host_popup_menu_reply_status_err(SapWitHostPopupMenuReply* reply, const char* err)
{
    croft_wit_host_popup_menu_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_POPUP_MENU_REPLY_STATUS;
    reply->val.status.is_v_ok = 0u;
    croft_wit_set_string_view(err,
                              &reply->val.status.v_val.err.v_data,
                              &reply->val.status.v_val.err.v_len);
}

static void croft_wit_host_popup_menu_reply_action_ok(SapWitHostPopupMenuReply* reply, int32_t action_id)
{
    croft_wit_host_popup_menu_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_POPUP_MENU_REPLY_ACTION;
    reply->val.action.is_v_ok = 1u;
    reply->val.action.v_val.ok.has_v = 1u;
    reply->val.action.v_val.ok.v = action_id;
}

static void croft_wit_host_popup_menu_reply_action_empty(SapWitHostPopupMenuReply* reply)
{
    croft_wit_host_popup_menu_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_POPUP_MENU_REPLY_ACTION;
    reply->val.action.is_v_ok = 1u;
    reply->val.action.v_val.ok.has_v = 0u;
}

static void croft_wit_host_popup_menu_reply_action_err(SapWitHostPopupMenuReply* reply, const char* err)
{
    croft_wit_host_popup_menu_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_POPUP_MENU_REPLY_ACTION;
    reply->val.action.is_v_ok = 0u;
    croft_wit_set_string_view(err,
                              &reply->val.action.v_val.err.v_data,
                              &reply->val.action.v_val.err.v_len);
}

static const char* croft_wit_host_popup_menu_map_result(host_popup_menu_result result)
{
    switch (result) {
        case HOST_POPUP_MENU_RESULT_UNAVAILABLE:
            return "unavailable";
        case HOST_POPUP_MENU_RESULT_INTERNAL:
            return "internal";
        default:
            return "internal";
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

static int32_t croft_wit_host_popup_menu_dispatch_begin_popup(void* ctx,
                                                              SapWitHostPopupMenuReply* reply_out)
{
    croft_wit_host_popup_menu_runtime* runtime = (croft_wit_host_popup_menu_runtime*)ctx;

    if (!runtime || !reply_out) {
        return -1;
    }
    croft_wit_host_popup_menu_reset(runtime);
    croft_wit_host_popup_menu_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_popup_menu_dispatch_add_item(void* ctx,
                                                           const SapWitHostPopupMenuPopupItem* item,
                                                           SapWitHostPopupMenuReply* reply_out)
{
    croft_wit_host_popup_menu_runtime* runtime = (croft_wit_host_popup_menu_runtime*)ctx;

    if (!runtime || !item || !reply_out) {
        return -1;
    }
    if (!croft_wit_host_popup_menu_add_item(runtime, item)) {
        croft_wit_host_popup_menu_reply_status_err(reply_out, "internal");
        return 0;
    }
    croft_wit_host_popup_menu_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_popup_menu_dispatch_show_at(void* ctx,
                                                          const SapWitHostPopupMenuShowAt* at,
                                                          SapWitHostPopupMenuReply* reply_out)
{
    croft_wit_host_popup_menu_runtime* runtime = (croft_wit_host_popup_menu_runtime*)ctx;
    int32_t action_id = 0;
    host_popup_menu_result result;

    if (!runtime || !at || !reply_out) {
        return -1;
    }

    result = host_popup_menu_show(runtime->items,
                                  runtime->item_count,
                                  (float)at->x_milli / 1000.0f,
                                  (float)at->y_milli / 1000.0f,
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

static const SapWitHostPopupMenuDispatchOps g_croft_wit_host_popup_menu_dispatch_ops = {
    .begin_popup = croft_wit_host_popup_menu_dispatch_begin_popup,
    .add_item = croft_wit_host_popup_menu_dispatch_add_item,
    .show_at = croft_wit_host_popup_menu_dispatch_show_at,
};

int32_t croft_wit_host_popup_menu_runtime_dispatch(croft_wit_host_popup_menu_runtime* runtime,
                                                   const SapWitHostPopupMenuCommand* command,
                                                   SapWitHostPopupMenuReply* reply_out)
{
    int32_t rc;

    if (!runtime || !command || !reply_out) {
        return -1;
    }

    rc = sap_wit_dispatch_host_popup_menu(runtime,
                                          &g_croft_wit_host_popup_menu_dispatch_ops,
                                          command,
                                          reply_out);
    if (rc == -1) {
        croft_wit_host_popup_menu_reply_status_err(reply_out, "internal");
        return 0;
    }
    return rc;
}
