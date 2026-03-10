#include "croft/wit_host_menu_runtime.h"

#include "croft/host_menu.h"

#include <stdlib.h>
#include <string.h>

#define CROFT_WIT_HOST_MENU_ACTION_CAP 64u

struct croft_wit_host_menu_runtime {
    int32_t actions[CROFT_WIT_HOST_MENU_ACTION_CAP];
    uint32_t action_head;
    uint32_t action_count;
};

static croft_wit_host_menu_runtime* g_menu_runtime = NULL;

static void croft_wit_host_menu_reply_zero(SapWitHostMenuReply* reply)
{
    sap_wit_zero_host_menu_reply(reply);
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

static void croft_wit_host_menu_reply_status_ok(SapWitHostMenuReply* reply)
{
    croft_wit_host_menu_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_MENU_REPLY_STATUS;
    reply->val.status.is_v_ok = 1u;
}

static void croft_wit_host_menu_reply_status_err(SapWitHostMenuReply* reply, const char* err)
{
    croft_wit_host_menu_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_MENU_REPLY_STATUS;
    reply->val.status.is_v_ok = 0u;
    croft_wit_set_string_view(err,
                              &reply->val.status.v_val.err.v_data,
                              &reply->val.status.v_val.err.v_len);
}

static void croft_wit_host_menu_reply_action_ok(SapWitHostMenuReply* reply, int32_t action_id)
{
    croft_wit_host_menu_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_MENU_REPLY_ACTION;
    reply->val.action.is_v_ok = 1u;
    reply->val.action.v_val.ok.has_v = 1u;
    reply->val.action.v_val.ok.v = action_id;
}

static void croft_wit_host_menu_reply_action_empty(SapWitHostMenuReply* reply)
{
    croft_wit_host_menu_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_MENU_REPLY_ACTION;
    reply->val.action.is_v_ok = 1u;
    reply->val.action.v_val.ok.has_v = 0u;
}

static void croft_wit_host_menu_enqueue_action(int32_t action_id)
{
    uint32_t slot;

    if (!g_menu_runtime || g_menu_runtime->action_count >= CROFT_WIT_HOST_MENU_ACTION_CAP) {
        return;
    }

    slot = (g_menu_runtime->action_head + g_menu_runtime->action_count) % CROFT_WIT_HOST_MENU_ACTION_CAP;
    g_menu_runtime->actions[slot] = action_id;
    g_menu_runtime->action_count++;
}

static void croft_wit_host_menu_callback(int32_t action_id)
{
    croft_wit_host_menu_enqueue_action(action_id);
}

static uint32_t croft_wit_host_menu_modifiers(uint32_t mods)
{
    uint32_t mapped = 0u;

    if (mods & SAP_WIT_HOST_MENU_MODIFIERS_SHIFT) mapped |= SAP_WIT_MENU_SCHEMA_MODIFIERS_SHIFT;
    if (mods & SAP_WIT_HOST_MENU_MODIFIERS_CTRL) mapped |= SAP_WIT_MENU_SCHEMA_MODIFIERS_CTRL;
    if (mods & SAP_WIT_HOST_MENU_MODIFIERS_ALT) mapped |= SAP_WIT_MENU_SCHEMA_MODIFIERS_ALT;
    if (mods & SAP_WIT_HOST_MENU_MODIFIERS_CMD) mapped |= SAP_WIT_MENU_SCHEMA_MODIFIERS_CMD;
    return mapped;
}

static void croft_wit_host_menu_apply_begin(void)
{
    SapWitMenuSchemaMenuIntent intent = {0};
    intent.case_tag = SAP_WIT_MENU_SCHEMA_MENU_INTENT_BEGIN_UPDATE;
    host_menu_apply_intent(&intent);
}

static void croft_wit_host_menu_apply_item(const SapWitHostMenuItem* item)
{
    SapWitMenuSchemaMenuIntent intent = {0};

    intent.case_tag = SAP_WIT_MENU_SCHEMA_MENU_INTENT_ADD_ITEM;
    intent.val.add_item.action_id = item->action_id;
    intent.val.add_item.parent_action_id = item->parent_action_id;
    intent.val.add_item.label_data = item->label_data;
    intent.val.add_item.label_len = item->label_len;
    intent.val.add_item.has_shortcut = item->has_shortcut;
    intent.val.add_item.shortcut_data = item->shortcut_data;
    intent.val.add_item.shortcut_len = item->shortcut_len;
    intent.val.add_item.mods = croft_wit_host_menu_modifiers(item->mods);
    host_menu_apply_intent(&intent);
}

static void croft_wit_host_menu_apply_commit(void)
{
    SapWitMenuSchemaMenuIntent intent = {0};
    intent.case_tag = SAP_WIT_MENU_SCHEMA_MENU_INTENT_COMMIT_UPDATE;
    host_menu_apply_intent(&intent);
}

croft_wit_host_menu_runtime* croft_wit_host_menu_runtime_create(void)
{
    croft_wit_host_menu_runtime* runtime =
        (croft_wit_host_menu_runtime*)calloc(1u, sizeof(croft_wit_host_menu_runtime));

    if (!runtime) {
        return NULL;
    }

    if (!g_menu_runtime) {
        g_menu_runtime = runtime;
        host_menu_set_callback(croft_wit_host_menu_callback);
    }

    return runtime;
}

void croft_wit_host_menu_runtime_destroy(croft_wit_host_menu_runtime* runtime)
{
    if (!runtime) {
        return;
    }
    if (g_menu_runtime == runtime) {
        host_menu_set_callback(NULL);
        host_menu_reset();
        g_menu_runtime = NULL;
    }
    free(runtime);
}

static int32_t croft_wit_host_menu_dispatch_begin_update(void* ctx, SapWitHostMenuReply* reply_out)
{
    (void)ctx;
    croft_wit_host_menu_apply_begin();
    croft_wit_host_menu_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_menu_dispatch_add_item(void* ctx,
                                                     const SapWitHostMenuItem* item,
                                                     SapWitHostMenuReply* reply_out)
{
    (void)ctx;
    if (!item || !reply_out) {
        return -1;
    }
    croft_wit_host_menu_apply_item(item);
    croft_wit_host_menu_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_menu_dispatch_commit_update(void* ctx, SapWitHostMenuReply* reply_out)
{
    (void)ctx;
    croft_wit_host_menu_apply_commit();
    croft_wit_host_menu_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_menu_dispatch_next_action(void* ctx, SapWitHostMenuReply* reply_out)
{
    croft_wit_host_menu_runtime* runtime = (croft_wit_host_menu_runtime*)ctx;

    if (!runtime || !reply_out) {
        return -1;
    }
    if (runtime->action_count == 0u) {
        croft_wit_host_menu_reply_action_empty(reply_out);
        return 0;
    }
    croft_wit_host_menu_reply_action_ok(reply_out, runtime->actions[runtime->action_head]);
    runtime->action_head = (runtime->action_head + 1u) % CROFT_WIT_HOST_MENU_ACTION_CAP;
    runtime->action_count--;
    return 0;
}

static const SapWitHostMenuDispatchOps g_croft_wit_host_menu_dispatch_ops = {
    .begin_update = croft_wit_host_menu_dispatch_begin_update,
    .add_item = croft_wit_host_menu_dispatch_add_item,
    .commit_update = croft_wit_host_menu_dispatch_commit_update,
    .next_action = croft_wit_host_menu_dispatch_next_action,
};

int32_t croft_wit_host_menu_runtime_dispatch(croft_wit_host_menu_runtime* runtime,
                                             const SapWitHostMenuCommand* command,
                                             SapWitHostMenuReply* reply_out)
{
    int32_t rc;

    if (!runtime || !command || !reply_out) {
        return -1;
    }

    rc = sap_wit_dispatch_host_menu(runtime, &g_croft_wit_host_menu_dispatch_ops, command, reply_out);
    if (rc == -1) {
        croft_wit_host_menu_reply_status_err(reply_out, "internal");
        return 0;
    }
    return rc;
}
