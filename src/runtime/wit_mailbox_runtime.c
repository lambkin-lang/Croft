#include "croft/wit_mailbox_runtime.h"

#include "sapling/err.h"

#include <stdlib.h>
#include <string.h>

typedef struct croft_wit_mailbox_message {
    uint8_t* data;
    uint32_t len;
    struct croft_wit_mailbox_message* next;
} croft_wit_mailbox_message;

typedef struct {
    uint8_t live;
    uint32_t max_messages;
    uint32_t message_count;
    croft_wit_mailbox_message* head;
    croft_wit_mailbox_message* tail;
} croft_wit_mailbox_slot;

struct croft_wit_mailbox_runtime {
    croft_wit_mailbox_slot* slots;
    size_t slot_count;
    size_t slot_cap;
};

static uint8_t croft_wit_mailbox_error_from_rc(int32_t rc)
{
    switch (rc) {
        case ERR_OOM:
            return SAP_WIT_COMMON_CORE_COMMON_ERROR_OOM;
        case ERR_BUSY:
            return SAP_WIT_COMMON_CORE_COMMON_ERROR_BUSY;
        default:
            return SAP_WIT_COMMON_CORE_COMMON_ERROR_INTERNAL;
    }
}

static void croft_wit_mailbox_reply_zero(SapWitCommonCoreMailboxReply* reply)
{
    if (!reply) {
        return;
    }
    memset(reply, 0, sizeof(*reply));
}

static void croft_wit_mailbox_reply_mailbox_ok(SapWitCommonCoreMailboxReply* reply, SapWitCommonCoreMailboxResource handle)
{
    croft_wit_mailbox_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_MAILBOX_REPLY_MAILBOX;
    reply->val.mailbox.case_tag = SAP_WIT_COMMON_CORE_MAILBOX_OP_RESULT_OK;
    reply->val.mailbox.val.ok = handle;
}

static void croft_wit_mailbox_reply_mailbox_err(SapWitCommonCoreMailboxReply* reply, uint8_t err)
{
    croft_wit_mailbox_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_MAILBOX_REPLY_MAILBOX;
    reply->val.mailbox.case_tag = SAP_WIT_COMMON_CORE_MAILBOX_OP_RESULT_ERR;
    reply->val.mailbox.val.err = err;
}

static void croft_wit_mailbox_reply_status_ok(SapWitCommonCoreMailboxReply* reply)
{
    croft_wit_mailbox_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_MAILBOX_REPLY_STATUS;
    reply->val.status.case_tag = SAP_WIT_COMMON_CORE_STATUS_OK;
}

static void croft_wit_mailbox_reply_status_err(SapWitCommonCoreMailboxReply* reply, uint8_t err)
{
    croft_wit_mailbox_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_MAILBOX_REPLY_STATUS;
    reply->val.status.case_tag = SAP_WIT_COMMON_CORE_STATUS_ERR;
    reply->val.status.val.err = err;
}

static void croft_wit_mailbox_reply_recv_ok(SapWitCommonCoreMailboxReply* reply, uint8_t* data, uint32_t len)
{
    croft_wit_mailbox_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_MAILBOX_REPLY_RECV;
    reply->val.recv.case_tag = SAP_WIT_COMMON_CORE_MAILBOX_RECV_RESULT_OK;
    reply->val.recv.val.ok.data = data;
    reply->val.recv.val.ok.len = len;
}

static void croft_wit_mailbox_reply_recv_empty(SapWitCommonCoreMailboxReply* reply)
{
    croft_wit_mailbox_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_MAILBOX_REPLY_RECV;
    reply->val.recv.case_tag = SAP_WIT_COMMON_CORE_MAILBOX_RECV_RESULT_EMPTY;
}

static void croft_wit_mailbox_reply_recv_err(SapWitCommonCoreMailboxReply* reply, uint8_t err)
{
    croft_wit_mailbox_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_MAILBOX_REPLY_RECV;
    reply->val.recv.case_tag = SAP_WIT_COMMON_CORE_MAILBOX_RECV_RESULT_ERR;
    reply->val.recv.val.err = err;
}

void croft_wit_mailbox_reply_dispose(SapWitCommonCoreMailboxReply* reply)
{
    if (!reply) {
        return;
    }

    if (reply->case_tag == SAP_WIT_COMMON_CORE_MAILBOX_REPLY_RECV
            && reply->val.recv.case_tag == SAP_WIT_COMMON_CORE_MAILBOX_RECV_RESULT_OK
            && reply->val.recv.val.ok.data) {
        free((void*)reply->val.recv.val.ok.data);
    }

    memset(reply, 0, sizeof(*reply));
}

static void croft_wit_mailbox_slot_clear(croft_wit_mailbox_slot* slot)
{
    croft_wit_mailbox_message* message;
    croft_wit_mailbox_message* next;

    if (!slot) {
        return;
    }

    message = slot->head;
    while (message) {
        next = message->next;
        free(message->data);
        free(message);
        message = next;
    }

    memset(slot, 0, sizeof(*slot));
}

static int32_t croft_wit_mailbox_slots_reserve(croft_wit_mailbox_runtime* runtime, size_t needed)
{
    croft_wit_mailbox_slot* next_slots;
    size_t next_cap;

    if (!runtime) {
        return ERR_INVALID;
    }
    if (runtime->slot_cap >= needed) {
        return ERR_OK;
    }

    next_cap = runtime->slot_cap > 0u ? runtime->slot_cap * 2u : 4u;
    while (next_cap < needed) {
        next_cap *= 2u;
    }

    next_slots = (croft_wit_mailbox_slot*)realloc(runtime->slots, next_cap * sizeof(*next_slots));
    if (!next_slots) {
        return ERR_OOM;
    }

    memset(next_slots + runtime->slot_cap, 0,
           (next_cap - runtime->slot_cap) * sizeof(*next_slots));
    runtime->slots = next_slots;
    runtime->slot_cap = next_cap;
    return ERR_OK;
}

static int32_t croft_wit_mailbox_slots_insert(croft_wit_mailbox_runtime* runtime,
                                              uint32_t max_messages,
                                              SapWitCommonCoreMailboxResource* handle_out)
{
    size_t i;
    int32_t rc;

    if (!runtime || !handle_out) {
        return ERR_INVALID;
    }

    for (i = 0u; i < runtime->slot_count; i++) {
        if (!runtime->slots[i].live) {
            runtime->slots[i].live = 1u;
            runtime->slots[i].max_messages = max_messages;
            *handle_out = (SapWitCommonCoreMailboxResource)(i + 1u);
            return ERR_OK;
        }
    }

    rc = croft_wit_mailbox_slots_reserve(runtime, runtime->slot_count + 1u);
    if (rc != ERR_OK) {
        return rc;
    }

    runtime->slots[runtime->slot_count].live = 1u;
    runtime->slots[runtime->slot_count].max_messages = max_messages;
    runtime->slot_count++;
    *handle_out = (SapWitCommonCoreMailboxResource)runtime->slot_count;
    return ERR_OK;
}

static croft_wit_mailbox_slot* croft_wit_mailbox_slots_lookup(croft_wit_mailbox_runtime* runtime,
                                                              SapWitCommonCoreMailboxResource handle)
{
    size_t slot;

    if (!runtime || handle == SAP_WIT_COMMON_CORE_MAILBOX_RESOURCE_INVALID) {
        return NULL;
    }

    slot = (size_t)handle - 1u;
    if (slot >= runtime->slot_count) {
        return NULL;
    }

    if (!runtime->slots[slot].live) {
        return NULL;
    }

    return &runtime->slots[slot];
}

croft_wit_mailbox_runtime* croft_wit_mailbox_runtime_create(void)
{
    return (croft_wit_mailbox_runtime*)calloc(1u, sizeof(croft_wit_mailbox_runtime));
}

void croft_wit_mailbox_runtime_destroy(croft_wit_mailbox_runtime* runtime)
{
    size_t i;

    if (!runtime) {
        return;
    }

    for (i = 0u; i < runtime->slot_count; i++) {
        croft_wit_mailbox_slot_clear(&runtime->slots[i]);
    }

    free(runtime->slots);
    free(runtime);
}

static int32_t croft_wit_mailbox_dispatch_open(croft_wit_mailbox_runtime* runtime,
                                               const SapWitCommonCoreMailboxOpen* request,
                                               SapWitCommonCoreMailboxReply* reply_out)
{
    SapWitCommonCoreMailboxResource handle = SAP_WIT_COMMON_CORE_MAILBOX_RESOURCE_INVALID;
    int32_t rc;

    if (!runtime || !request || !reply_out) {
        return ERR_INVALID;
    }

    rc = croft_wit_mailbox_slots_insert(runtime, request->max_messages, &handle);
    if (rc != ERR_OK) {
        croft_wit_mailbox_reply_mailbox_err(reply_out, croft_wit_mailbox_error_from_rc(rc));
        return ERR_OK;
    }

    croft_wit_mailbox_reply_mailbox_ok(reply_out, handle);
    return ERR_OK;
}

static int32_t croft_wit_mailbox_dispatch_drop(croft_wit_mailbox_runtime* runtime,
                                               const SapWitCommonCoreMailboxDrop* request,
                                               SapWitCommonCoreMailboxReply* reply_out)
{
    croft_wit_mailbox_slot* slot;

    if (!runtime || !request || !reply_out) {
        return ERR_INVALID;
    }

    slot = croft_wit_mailbox_slots_lookup(runtime, request->mailbox);
    if (!slot) {
        croft_wit_mailbox_reply_status_err(reply_out, SAP_WIT_COMMON_CORE_COMMON_ERROR_INVALID_HANDLE);
        return ERR_OK;
    }
    if (slot->message_count > 0u) {
        croft_wit_mailbox_reply_status_err(reply_out, SAP_WIT_COMMON_CORE_COMMON_ERROR_BUSY);
        return ERR_OK;
    }

    croft_wit_mailbox_slot_clear(slot);
    croft_wit_mailbox_reply_status_ok(reply_out);
    return ERR_OK;
}

/*
 * The mailbox boundary intentionally copies payloads on send. Later worlds can
 * replace this with shared transport, worker hops, or host queues, but the
 * common-core model keeps ownership explicit and local.
 */
static int32_t croft_wit_mailbox_dispatch_send(croft_wit_mailbox_runtime* runtime,
                                               const SapWitCommonCoreMailboxSend* request,
                                               SapWitCommonCoreMailboxReply* reply_out)
{
    croft_wit_mailbox_slot* slot;
    croft_wit_mailbox_message* message;
    size_t alloc_len;

    if (!runtime || !request || !reply_out) {
        return ERR_INVALID;
    }

    slot = croft_wit_mailbox_slots_lookup(runtime, request->mailbox);
    if (!slot) {
        croft_wit_mailbox_reply_status_err(reply_out, SAP_WIT_COMMON_CORE_COMMON_ERROR_INVALID_HANDLE);
        return ERR_OK;
    }
    if (slot->max_messages > 0u && slot->message_count >= slot->max_messages) {
        croft_wit_mailbox_reply_status_err(reply_out, SAP_WIT_COMMON_CORE_COMMON_ERROR_BUSY);
        return ERR_OK;
    }

    message = (croft_wit_mailbox_message*)calloc(1u, sizeof(*message));
    if (!message) {
        croft_wit_mailbox_reply_status_err(reply_out, SAP_WIT_COMMON_CORE_COMMON_ERROR_OOM);
        return ERR_OK;
    }

    alloc_len = request->payload_len > 0u ? (size_t)request->payload_len : 1u;
    message->data = (uint8_t*)malloc(alloc_len);
    if (!message->data) {
        free(message);
        croft_wit_mailbox_reply_status_err(reply_out, SAP_WIT_COMMON_CORE_COMMON_ERROR_OOM);
        return ERR_OK;
    }

    if (request->payload_len > 0u) {
        memcpy(message->data, request->payload_data, request->payload_len);
    }
    message->len = request->payload_len;

    if (!slot->tail) {
        slot->head = message;
        slot->tail = message;
    } else {
        slot->tail->next = message;
        slot->tail = message;
    }
    slot->message_count++;

    croft_wit_mailbox_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_wit_mailbox_dispatch_recv(croft_wit_mailbox_runtime* runtime,
                                               const SapWitCommonCoreMailboxRecv* request,
                                               SapWitCommonCoreMailboxReply* reply_out)
{
    croft_wit_mailbox_slot* slot;
    croft_wit_mailbox_message* message;

    if (!runtime || !request || !reply_out) {
        return ERR_INVALID;
    }

    slot = croft_wit_mailbox_slots_lookup(runtime, request->mailbox);
    if (!slot) {
        croft_wit_mailbox_reply_recv_err(reply_out, SAP_WIT_COMMON_CORE_COMMON_ERROR_INVALID_HANDLE);
        return ERR_OK;
    }
    if (!slot->head) {
        croft_wit_mailbox_reply_recv_empty(reply_out);
        return ERR_OK;
    }

    message = slot->head;
    slot->head = message->next;
    if (!slot->head) {
        slot->tail = NULL;
    }
    slot->message_count--;

    croft_wit_mailbox_reply_recv_ok(reply_out, message->data, message->len);
    free(message);
    return ERR_OK;
}

int32_t croft_wit_mailbox_runtime_dispatch(croft_wit_mailbox_runtime* runtime,
                                           const SapWitCommonCoreMailboxCommand* command,
                                           SapWitCommonCoreMailboxReply* reply_out)
{
    if (!runtime || !command || !reply_out) {
        return ERR_INVALID;
    }

    switch (command->case_tag) {
        case SAP_WIT_COMMON_CORE_MAILBOX_COMMAND_OPEN:
            return croft_wit_mailbox_dispatch_open(runtime, &command->val.open, reply_out);
        case SAP_WIT_COMMON_CORE_MAILBOX_COMMAND_DROP:
            return croft_wit_mailbox_dispatch_drop(runtime, &command->val.drop, reply_out);
        case SAP_WIT_COMMON_CORE_MAILBOX_COMMAND_SEND:
            return croft_wit_mailbox_dispatch_send(runtime, &command->val.send, reply_out);
        case SAP_WIT_COMMON_CORE_MAILBOX_COMMAND_RECV:
            return croft_wit_mailbox_dispatch_recv(runtime, &command->val.recv, reply_out);
        default:
            return ERR_INVALID;
    }
}
