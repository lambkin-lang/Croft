#include "croft/wit_host_a11y_runtime.h"

#include "croft/host_a11y.h"
#include "croft/host_ui.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t live;
    void* native_node;
} croft_wit_host_a11y_slot;

struct croft_wit_host_a11y_runtime {
    uint8_t open;
    croft_wit_host_a11y_slot* slots;
    size_t slot_count;
    size_t slot_cap;
};

static void croft_wit_host_a11y_reply_zero(SapWitHostA11yReply* reply)
{
    sap_wit_zero_host_a11y_reply(reply);
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

static void croft_wit_host_a11y_reply_status_ok(SapWitHostA11yReply* reply)
{
    croft_wit_host_a11y_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_A11Y_REPLY_STATUS;
    reply->val.status.is_v_ok = 1u;
}

static void croft_wit_host_a11y_reply_status_err(SapWitHostA11yReply* reply, const char* err)
{
    croft_wit_host_a11y_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_A11Y_REPLY_STATUS;
    reply->val.status.is_v_ok = 0u;
    croft_wit_set_string_view(err,
                              &reply->val.status.v_val.err.v_data,
                              &reply->val.status.v_val.err.v_len);
}

static void croft_wit_host_a11y_reply_node_ok(SapWitHostA11yReply* reply, SapWitHostA11yNodeResource handle)
{
    croft_wit_host_a11y_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_A11Y_REPLY_NODE;
    reply->val.node.is_v_ok = 1u;
    reply->val.node.v_val.ok.v = handle;
}

static void croft_wit_host_a11y_reply_node_err(SapWitHostA11yReply* reply, const char* err)
{
    croft_wit_host_a11y_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_A11Y_REPLY_NODE;
    reply->val.node.is_v_ok = 0u;
    croft_wit_set_string_view(err,
                              &reply->val.node.v_val.err.v_data,
                              &reply->val.node.v_val.err.v_len);
}

static host_a11y_role croft_wit_host_a11y_role(uint8_t role)
{
    switch (role) {
        case SAP_WIT_HOST_A11Y_ROLE_WINDOW:
            return ROLE_WINDOW;
        case SAP_WIT_HOST_A11Y_ROLE_GROUP:
            return ROLE_GROUP;
        case SAP_WIT_HOST_A11Y_ROLE_TEXT:
            return ROLE_TEXT;
        case SAP_WIT_HOST_A11Y_ROLE_BUTTON:
            return ROLE_BUTTON;
        default:
            return ROLE_UNKNOWN;
    }
}

static int32_t croft_wit_host_a11y_slots_reserve(croft_wit_host_a11y_runtime* runtime, size_t needed)
{
    croft_wit_host_a11y_slot* next_slots;
    size_t next_cap;

    if (!runtime) {
        return -1;
    }
    if (runtime->slot_cap >= needed) {
        return 0;
    }

    next_cap = runtime->slot_cap > 0u ? runtime->slot_cap * 2u : 8u;
    while (next_cap < needed) {
        next_cap *= 2u;
    }

    next_slots = (croft_wit_host_a11y_slot*)realloc(runtime->slots, next_cap * sizeof(*next_slots));
    if (!next_slots) {
        return -1;
    }
    memset(next_slots + runtime->slot_cap, 0, (next_cap - runtime->slot_cap) * sizeof(*next_slots));
    runtime->slots = next_slots;
    runtime->slot_cap = next_cap;
    return 0;
}

static int32_t croft_wit_host_a11y_slots_insert(croft_wit_host_a11y_runtime* runtime,
                                                void* native_node,
                                                SapWitHostA11yNodeResource* handle_out)
{
    size_t i;

    if (!runtime || !native_node || !handle_out) {
        return -1;
    }

    for (i = 0u; i < runtime->slot_count; i++) {
        if (!runtime->slots[i].live) {
            runtime->slots[i].live = 1u;
            runtime->slots[i].native_node = native_node;
            *handle_out = (SapWitHostA11yNodeResource)(i + 1u);
            return 0;
        }
    }

    if (croft_wit_host_a11y_slots_reserve(runtime, runtime->slot_count + 1u) != 0) {
        return -1;
    }
    runtime->slots[runtime->slot_count].live = 1u;
    runtime->slots[runtime->slot_count].native_node = native_node;
    runtime->slot_count++;
    *handle_out = (SapWitHostA11yNodeResource)runtime->slot_count;
    return 0;
}

static croft_wit_host_a11y_slot* croft_wit_host_a11y_lookup(croft_wit_host_a11y_runtime* runtime,
                                                            SapWitHostA11yNodeResource handle)
{
    size_t slot;

    if (!runtime || handle == SAP_WIT_HOST_A11Y_NODE_RESOURCE_INVALID) {
        return NULL;
    }

    slot = (size_t)handle - 1u;
    if (slot >= runtime->slot_count || !runtime->slots[slot].live) {
        return NULL;
    }
    return &runtime->slots[slot];
}

croft_wit_host_a11y_runtime* croft_wit_host_a11y_runtime_create(void)
{
    return (croft_wit_host_a11y_runtime*)calloc(1u, sizeof(croft_wit_host_a11y_runtime));
}

void croft_wit_host_a11y_runtime_destroy(croft_wit_host_a11y_runtime* runtime)
{
    size_t i;

    if (!runtime) {
        return;
    }

    for (i = 0u; i < runtime->slot_count; i++) {
        if (runtime->slots[i].live && runtime->slots[i].native_node) {
            host_a11y_destroy_node(runtime->slots[i].native_node);
        }
    }
    if (runtime->open) {
        host_a11y_terminate();
    }
    free(runtime->slots);
    free(runtime);
}

static int32_t croft_wit_host_a11y_dispatch_open(void* ctx,
                                                 SapWitHostA11yReply* reply_out)
{
    croft_wit_host_a11y_runtime* runtime = (croft_wit_host_a11y_runtime*)ctx;
    if (!runtime || !reply_out) {
        return -1;
    }
    if (runtime->open) {
        croft_wit_host_a11y_reply_status_err(reply_out, "busy");
        return 0;
    }
    if (host_a11y_init(host_ui_get_native_window()) != 0) {
        croft_wit_host_a11y_reply_status_err(reply_out, "unavailable");
        return 0;
    }
    runtime->open = 1u;
    croft_wit_host_a11y_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_a11y_dispatch_close(void* ctx,
                                                  SapWitHostA11yReply* reply_out)
{
    croft_wit_host_a11y_runtime* runtime = (croft_wit_host_a11y_runtime*)ctx;
    size_t i;

    if (!runtime || !reply_out) {
        return -1;
    }
    if (!runtime->open) {
        croft_wit_host_a11y_reply_status_ok(reply_out);
        return 0;
    }
    for (i = 0u; i < runtime->slot_count; i++) {
        if (runtime->slots[i].live && runtime->slots[i].native_node) {
            host_a11y_destroy_node(runtime->slots[i].native_node);
            runtime->slots[i].live = 0u;
            runtime->slots[i].native_node = NULL;
        }
    }
    host_a11y_terminate();
    runtime->open = 0u;
    croft_wit_host_a11y_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_a11y_dispatch_create_node(
    void* ctx,
    const SapWitHostA11yCreateNode* request,
    SapWitHostA11yReply* reply_out)
{
    croft_wit_host_a11y_runtime* runtime = (croft_wit_host_a11y_runtime*)ctx;
    SapWitHostA11yNodeResource handle = SAP_WIT_HOST_A11Y_NODE_RESOURCE_INVALID;
    host_a11y_node_config cfg = {0};
    void* native_node;
    char* label = NULL;

    if (!runtime || !request || !reply_out) {
        return -1;
    }
    if (!runtime->open) {
        croft_wit_host_a11y_reply_node_err(reply_out, "unavailable");
        return 0;
    }
    cfg.x = request->bounds.x;
    cfg.y = request->bounds.y;
    cfg.width = request->bounds.width;
    cfg.height = request->bounds.height;
    cfg.os_specific_mixin = NULL;
    if (request->has_label) {
        label = (char*)malloc((size_t)request->label_len + 1u);
        if (!label) {
            croft_wit_host_a11y_reply_node_err(reply_out, "internal");
            return 0;
        }
        memcpy(label, request->label_data, request->label_len);
        label[request->label_len] = '\0';
        cfg.label = label;
    }
    native_node = host_a11y_create_node(croft_wit_host_a11y_role(request->role), &cfg);
    free(label);
    if (!native_node || croft_wit_host_a11y_slots_insert(runtime, native_node, &handle) != 0) {
        if (native_node) {
            host_a11y_destroy_node(native_node);
        }
        croft_wit_host_a11y_reply_node_err(reply_out, "internal");
        return 0;
    }
    croft_wit_host_a11y_reply_node_ok(reply_out, handle);
    return 0;
}

static int32_t croft_wit_host_a11y_dispatch_add_child(
    void* ctx,
    const SapWitHostA11yAddChild* request,
    SapWitHostA11yReply* reply_out)
{
    croft_wit_host_a11y_runtime* runtime = (croft_wit_host_a11y_runtime*)ctx;
    croft_wit_host_a11y_slot* child;
    croft_wit_host_a11y_slot* parent = NULL;

    if (!runtime || !request || !reply_out) {
        return -1;
    }
    child = croft_wit_host_a11y_lookup(runtime, request->child);
    if (!child) {
        croft_wit_host_a11y_reply_status_err(reply_out, "invalid-handle");
        return 0;
    }
    if (request->has_parent) {
        parent = croft_wit_host_a11y_lookup(runtime, request->parent);
        if (!parent) {
            croft_wit_host_a11y_reply_status_err(reply_out, "invalid-handle");
            return 0;
        }
    }
    host_a11y_add_child(parent ? parent->native_node : NULL, child->native_node);
    croft_wit_host_a11y_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_a11y_dispatch_update_frame(
    void* ctx,
    const SapWitHostA11yUpdateFrame* request,
    SapWitHostA11yReply* reply_out)
{
    croft_wit_host_a11y_runtime* runtime = (croft_wit_host_a11y_runtime*)ctx;
    croft_wit_host_a11y_slot* node;

    if (!runtime || !request || !reply_out) {
        return -1;
    }
    node = croft_wit_host_a11y_lookup(runtime, request->node);
    if (!node) {
        croft_wit_host_a11y_reply_status_err(reply_out, "invalid-handle");
        return 0;
    }
    host_a11y_update_frame(node->native_node,
                           request->bounds.x,
                           request->bounds.y,
                           request->bounds.width,
                           request->bounds.height);
    croft_wit_host_a11y_reply_status_ok(reply_out);
    return 0;
}

static int32_t croft_wit_host_a11y_dispatch_destroy_node(
    void* ctx,
    const SapWitHostA11yDestroyNode* request,
    SapWitHostA11yReply* reply_out)
{
    croft_wit_host_a11y_runtime* runtime = (croft_wit_host_a11y_runtime*)ctx;
    croft_wit_host_a11y_slot* node;

    if (!runtime || !request || !reply_out) {
        return -1;
    }
    node = croft_wit_host_a11y_lookup(runtime, request->node);
    if (!node) {
        croft_wit_host_a11y_reply_status_err(reply_out, "invalid-handle");
        return 0;
    }
    host_a11y_destroy_node(node->native_node);
    node->live = 0u;
    node->native_node = NULL;
    croft_wit_host_a11y_reply_status_ok(reply_out);
    return 0;
}

static const SapWitHostA11yDispatchOps g_croft_wit_host_a11y_dispatch_ops = {
    .open = croft_wit_host_a11y_dispatch_open,
    .close = croft_wit_host_a11y_dispatch_close,
    .create_node = croft_wit_host_a11y_dispatch_create_node,
    .add_child = croft_wit_host_a11y_dispatch_add_child,
    .update_frame = croft_wit_host_a11y_dispatch_update_frame,
    .destroy_node = croft_wit_host_a11y_dispatch_destroy_node,
};

int32_t croft_wit_host_a11y_runtime_dispatch(croft_wit_host_a11y_runtime* runtime,
                                             const SapWitHostA11yCommand* command,
                                             SapWitHostA11yReply* reply_out)
{
    int32_t rc;

    if (!runtime || !command || !reply_out) {
        return -1;
    }

    rc = sap_wit_dispatch_host_a11y(runtime, &g_croft_wit_host_a11y_dispatch_ops, command, reply_out);
    if (rc == -1) {
        croft_wit_host_a11y_reply_status_err(reply_out, "internal");
        return 0;
    }
    return rc;
}

static int32_t croft_wit_host_a11y_bridge_open(void* userdata)
{
    croft_wit_host_a11y_runtime* runtime = (croft_wit_host_a11y_runtime*)userdata;
    SapWitHostA11yCommand command = {0};
    SapWitHostA11yReply reply = {0};

    command.case_tag = SAP_WIT_HOST_A11Y_COMMAND_OPEN;
    if (croft_wit_host_a11y_runtime_dispatch(runtime, &command, &reply) != 0
            || reply.case_tag != SAP_WIT_HOST_A11Y_REPLY_STATUS
            || !reply.val.status.is_v_ok) {
        return -1;
    }
    return 0;
}

static void croft_wit_host_a11y_bridge_close(void* userdata)
{
    croft_wit_host_a11y_runtime* runtime = (croft_wit_host_a11y_runtime*)userdata;
    SapWitHostA11yCommand command = {0};
    SapWitHostA11yReply reply = {0};

    command.case_tag = SAP_WIT_HOST_A11Y_COMMAND_CLOSE;
    croft_wit_host_a11y_runtime_dispatch(runtime, &command, &reply);
}

static uint8_t croft_wit_host_a11y_bridge_role(croft_scene_a11y_role role)
{
    switch (role) {
        case CROFT_SCENE_A11Y_ROLE_WINDOW:
            return SAP_WIT_HOST_A11Y_ROLE_WINDOW;
        case CROFT_SCENE_A11Y_ROLE_GROUP:
            return SAP_WIT_HOST_A11Y_ROLE_GROUP;
        case CROFT_SCENE_A11Y_ROLE_TEXT:
            return SAP_WIT_HOST_A11Y_ROLE_TEXT;
        case CROFT_SCENE_A11Y_ROLE_BUTTON:
            return SAP_WIT_HOST_A11Y_ROLE_BUTTON;
        default:
            return SAP_WIT_HOST_A11Y_ROLE_UNKNOWN;
    }
}

static croft_scene_a11y_handle croft_wit_host_a11y_bridge_create_node(
    void* userdata,
    croft_scene_a11y_role role,
    const croft_scene_a11y_node_config* config)
{
    croft_wit_host_a11y_runtime* runtime = (croft_wit_host_a11y_runtime*)userdata;
    SapWitHostA11yCommand command = {0};
    SapWitHostA11yReply reply = {0};

    if (!config) {
        return (croft_scene_a11y_handle)0u;
    }

    command.case_tag = SAP_WIT_HOST_A11Y_COMMAND_CREATE_NODE;
    command.val.create_node.role = croft_wit_host_a11y_bridge_role(role);
    command.val.create_node.bounds.x = config->x;
    command.val.create_node.bounds.y = config->y;
    command.val.create_node.bounds.width = config->width;
    command.val.create_node.bounds.height = config->height;
    command.val.create_node.has_label = config->label ? 1u : 0u;
    command.val.create_node.label_data = (const uint8_t*)config->label;
    command.val.create_node.label_len = config->label ? (uint32_t)strlen(config->label) : 0u;
    if (croft_wit_host_a11y_runtime_dispatch(runtime, &command, &reply) != 0
            || reply.case_tag != SAP_WIT_HOST_A11Y_REPLY_NODE
            || !reply.val.node.is_v_ok) {
        return (croft_scene_a11y_handle)0u;
    }
    return (croft_scene_a11y_handle)reply.val.node.v_val.ok.v;
}

static void croft_wit_host_a11y_bridge_add_child(void* userdata,
                                                 croft_scene_a11y_handle parent,
                                                 croft_scene_a11y_handle child)
{
    croft_wit_host_a11y_runtime* runtime = (croft_wit_host_a11y_runtime*)userdata;
    SapWitHostA11yCommand command = {0};
    SapWitHostA11yReply reply = {0};

    command.case_tag = SAP_WIT_HOST_A11Y_COMMAND_ADD_CHILD;
    command.val.add_child.has_parent = parent ? 1u : 0u;
    command.val.add_child.parent = (SapWitHostA11yNodeResource)parent;
    command.val.add_child.child = (SapWitHostA11yNodeResource)child;
    croft_wit_host_a11y_runtime_dispatch(runtime, &command, &reply);
}

static void croft_wit_host_a11y_bridge_update_frame(void* userdata,
                                                    croft_scene_a11y_handle node,
                                                    float x,
                                                    float y,
                                                    float w,
                                                    float h)
{
    croft_wit_host_a11y_runtime* runtime = (croft_wit_host_a11y_runtime*)userdata;
    SapWitHostA11yCommand command = {0};
    SapWitHostA11yReply reply = {0};

    command.case_tag = SAP_WIT_HOST_A11Y_COMMAND_UPDATE_FRAME;
    command.val.update_frame.node = (SapWitHostA11yNodeResource)node;
    command.val.update_frame.bounds.x = x;
    command.val.update_frame.bounds.y = y;
    command.val.update_frame.bounds.width = w;
    command.val.update_frame.bounds.height = h;
    croft_wit_host_a11y_runtime_dispatch(runtime, &command, &reply);
}

static void croft_wit_host_a11y_bridge_destroy_node(void* userdata, croft_scene_a11y_handle node)
{
    croft_wit_host_a11y_runtime* runtime = (croft_wit_host_a11y_runtime*)userdata;
    SapWitHostA11yCommand command = {0};
    SapWitHostA11yReply reply = {0};

    command.case_tag = SAP_WIT_HOST_A11Y_COMMAND_DESTROY_NODE;
    command.val.destroy_node.node = (SapWitHostA11yNodeResource)node;
    croft_wit_host_a11y_runtime_dispatch(runtime, &command, &reply);
}

void croft_wit_host_a11y_runtime_install_bridge(croft_wit_host_a11y_runtime* runtime)
{
    static const croft_scene_a11y_bridge_vtbl bridge = {
        .open = croft_wit_host_a11y_bridge_open,
        .close = croft_wit_host_a11y_bridge_close,
        .create_node = croft_wit_host_a11y_bridge_create_node,
        .add_child = croft_wit_host_a11y_bridge_add_child,
        .update_frame = croft_wit_host_a11y_bridge_update_frame,
        .destroy_node = croft_wit_host_a11y_bridge_destroy_node
    };

    croft_scene_a11y_install_bridge(&bridge, runtime);
}
