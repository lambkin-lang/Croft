#include "croft/wit_text_runtime.h"

#include "sapling/arena.h"
#include "sapling/err.h"
#include "sapling/seq.h"
#include "sapling/text.h"
#include "sapling/txn.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    Text* text;
} croft_wit_text_slot;

struct croft_wit_text_runtime {
    SapMemArena* arena;
    SapEnv* env;
    croft_wit_text_slot* slots;
    size_t slot_count;
    size_t slot_cap;
};

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

static const char* croft_wit_error_from_rc(int32_t rc)
{
    switch (rc) {
        case ERR_OOM:
            return "oom";
        case ERR_BUSY:
            return "busy";
        case ERR_RANGE:
            return "range";
        default:
            return "internal";
    }
}

static const char* croft_wit_text_input_error_from_rc(int32_t rc)
{
    switch (rc) {
        case ERR_OOM:
            return "oom";
        case ERR_RANGE:
            return "range";
        case ERR_INVALID:
        case ERR_PARSE:
        case ERR_TYPE:
            return "invalid-utf8";
        case ERR_BUSY:
            return "busy";
        default:
            return "internal";
    }
}

static void croft_wit_text_reply_zero(SapWitCommonCoreTextReply* reply)
{
    sap_wit_zero_common_core_text_reply(reply);
}

static void croft_wit_text_reply_text_ok(SapWitCommonCoreTextReply* reply, SapWitCommonCoreTextResource handle)
{
    croft_wit_text_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_TEXT_REPLY_TEXT;
    reply->val.text.is_v_ok = 1u;
    reply->val.text.v_val.ok.v = handle;
}

static void croft_wit_text_reply_text_err(SapWitCommonCoreTextReply* reply, const char* err)
{
    croft_wit_text_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_TEXT_REPLY_TEXT;
    reply->val.text.is_v_ok = 0u;
    croft_wit_set_string_view(err, &reply->val.text.v_val.err.v_data, &reply->val.text.v_val.err.v_len);
}

static void croft_wit_text_reply_status_ok(SapWitCommonCoreTextReply* reply)
{
    croft_wit_text_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_TEXT_REPLY_STATUS;
    reply->val.status.is_v_ok = 1u;
}

static void croft_wit_text_reply_status_err(SapWitCommonCoreTextReply* reply, const char* err)
{
    croft_wit_text_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_TEXT_REPLY_STATUS;
    reply->val.status.is_v_ok = 0u;
    croft_wit_set_string_view(err,
                              &reply->val.status.v_val.err.v_data,
                              &reply->val.status.v_val.err.v_len);
}

static void croft_wit_text_reply_export_ok(SapWitCommonCoreTextReply* reply, uint8_t* utf8, uint32_t utf8_len)
{
    croft_wit_text_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_TEXT_REPLY_EXPORT;
    reply->val.export.is_v_ok = 1u;
    reply->val.export.v_val.ok.v_data = utf8;
    reply->val.export.v_val.ok.v_len = utf8_len;
}

static void croft_wit_text_reply_export_err(SapWitCommonCoreTextReply* reply, const char* err)
{
    croft_wit_text_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_TEXT_REPLY_EXPORT;
    reply->val.export.is_v_ok = 0u;
    croft_wit_set_string_view(err,
                              &reply->val.export.v_val.err.v_data,
                              &reply->val.export.v_val.err.v_len);
}

void croft_wit_text_reply_dispose(SapWitCommonCoreTextReply* reply)
{
    sap_wit_dispose_common_core_text_reply(reply);
}

void croft_wit_text_runtime_config_default(croft_wit_text_runtime_config* config)
{
    if (!config) {
        return;
    }

    config->page_size = 4096u;
    config->initial_bytes = 64u * 1024u;
    config->max_bytes = 512u * 1024u;
}

static int32_t croft_wit_text_slots_reserve(croft_wit_text_runtime* runtime, size_t needed)
{
    croft_wit_text_slot* next_slots;
    size_t next_cap;

    if (!runtime) {
        return ERR_INVALID;
    }
    if (runtime->slot_cap >= needed) {
        return ERR_OK;
    }

    next_cap = runtime->slot_cap > 0u ? runtime->slot_cap * 2u : 8u;
    while (next_cap < needed) {
        next_cap *= 2u;
    }

    next_slots = (croft_wit_text_slot*)realloc(runtime->slots, next_cap * sizeof(*next_slots));
    if (!next_slots) {
        return ERR_OOM;
    }

    memset(next_slots + runtime->slot_cap, 0,
           (next_cap - runtime->slot_cap) * sizeof(*next_slots));
    runtime->slots = next_slots;
    runtime->slot_cap = next_cap;
    return ERR_OK;
}

/*
 * Resource handles are the deliberate join-point boundary here: generated code
 * only sees stable integers, while the runtime privately decides how those map
 * onto real Sapling objects, generations, or even remote resources later on.
 */
static int32_t croft_wit_text_slots_insert(croft_wit_text_runtime* runtime,
                                           Text* text,
                                           SapWitCommonCoreTextResource* handle_out)
{
    size_t i;
    int32_t rc;

    if (!runtime || !text || !handle_out) {
        return ERR_INVALID;
    }

    for (i = 0u; i < runtime->slot_count; i++) {
        if (!runtime->slots[i].text) {
            runtime->slots[i].text = text;
            *handle_out = (SapWitCommonCoreTextResource)(i + 1u);
            return ERR_OK;
        }
    }

    rc = croft_wit_text_slots_reserve(runtime, runtime->slot_count + 1u);
    if (rc != ERR_OK) {
        return rc;
    }

    runtime->slots[runtime->slot_count].text = text;
    runtime->slot_count++;
    *handle_out = (SapWitCommonCoreTextResource)runtime->slot_count;
    return ERR_OK;
}

static Text* croft_wit_text_slots_lookup(croft_wit_text_runtime* runtime, SapWitCommonCoreTextResource handle)
{
    size_t slot;

    if (!runtime || handle == SAP_WIT_COMMON_CORE_TEXT_RESOURCE_INVALID) {
        return NULL;
    }

    slot = (size_t)handle - 1u;
    if (slot >= runtime->slot_count) {
        return NULL;
    }

    return runtime->slots[slot].text;
}

static int32_t croft_wit_text_slots_release(croft_wit_text_runtime* runtime, SapWitCommonCoreTextResource handle)
{
    Text* text;
    size_t slot;

    if (!runtime || handle == SAP_WIT_COMMON_CORE_TEXT_RESOURCE_INVALID) {
        return ERR_INVALID;
    }

    slot = (size_t)handle - 1u;
    if (slot >= runtime->slot_count) {
        return ERR_NOT_FOUND;
    }

    text = runtime->slots[slot].text;
    if (!text) {
        return ERR_NOT_FOUND;
    }

    runtime->slots[slot].text = NULL;
    return text_free(runtime->env, text);
}

static int32_t croft_wit_text_utf8_decode_one(const uint8_t* utf8,
                                              size_t utf8_len,
                                              size_t* consumed_out,
                                              uint32_t* codepoint_out)
{
    uint8_t b0;
    uint8_t b1;
    uint8_t b2;
    uint8_t b3;

    if (!utf8 || utf8_len == 0u || !consumed_out || !codepoint_out) {
        return ERR_INVALID;
    }

    b0 = utf8[0];
    if (b0 < 0x80u) {
        *consumed_out = 1u;
        *codepoint_out = b0;
        return ERR_OK;
    }

    if ((b0 & 0xE0u) == 0xC0u) {
        if (utf8_len < 2u) {
            return ERR_INVALID;
        }
        b1 = utf8[1];
        if ((b1 & 0xC0u) != 0x80u) {
            return ERR_INVALID;
        }

        *consumed_out = 2u;
        *codepoint_out = ((uint32_t)(b0 & 0x1Fu) << 6) | (uint32_t)(b1 & 0x3Fu);
        if (*codepoint_out < 0x80u) {
            return ERR_INVALID;
        }
        return ERR_OK;
    }

    if ((b0 & 0xF0u) == 0xE0u) {
        if (utf8_len < 3u) {
            return ERR_INVALID;
        }
        b1 = utf8[1];
        b2 = utf8[2];
        if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u) {
            return ERR_INVALID;
        }

        *consumed_out = 3u;
        *codepoint_out = ((uint32_t)(b0 & 0x0Fu) << 12)
            | ((uint32_t)(b1 & 0x3Fu) << 6)
            | (uint32_t)(b2 & 0x3Fu);
        if (*codepoint_out < 0x800u || (*codepoint_out >= 0xD800u && *codepoint_out <= 0xDFFFu)) {
            return ERR_INVALID;
        }
        return ERR_OK;
    }

    if ((b0 & 0xF8u) == 0xF0u) {
        if (utf8_len < 4u) {
            return ERR_INVALID;
        }
        b1 = utf8[1];
        b2 = utf8[2];
        b3 = utf8[3];
        if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u || (b3 & 0xC0u) != 0x80u) {
            return ERR_INVALID;
        }

        *consumed_out = 4u;
        *codepoint_out = ((uint32_t)(b0 & 0x07u) << 18)
            | ((uint32_t)(b1 & 0x3Fu) << 12)
            | ((uint32_t)(b2 & 0x3Fu) << 6)
            | (uint32_t)(b3 & 0x3Fu);
        if (*codepoint_out < 0x10000u || *codepoint_out > 0x10FFFFu) {
            return ERR_INVALID;
        }
        return ERR_OK;
    }

    return ERR_INVALID;
}

static int32_t croft_wit_text_decode_codepoints(const uint8_t* utf8,
                                                size_t utf8_len,
                                                uint32_t** codepoints_out,
                                                size_t* count_out)
{
    uint32_t* codepoints;
    size_t cap;
    size_t count;
    size_t offset;

    if (!codepoints_out || !count_out) {
        return ERR_INVALID;
    }

    *codepoints_out = NULL;
    *count_out = 0u;
    if (!utf8 && utf8_len > 0u) {
        return ERR_INVALID;
    }
    if (utf8_len == 0u) {
        return ERR_OK;
    }

    cap = utf8_len;
    codepoints = (uint32_t*)malloc(cap * sizeof(*codepoints));
    if (!codepoints) {
        return ERR_OOM;
    }

    count = 0u;
    offset = 0u;
    while (offset < utf8_len) {
        size_t consumed = 0u;
        uint32_t codepoint = 0u;
        int32_t rc = croft_wit_text_utf8_decode_one(utf8 + offset,
                                                    utf8_len - offset,
                                                    &consumed,
                                                    &codepoint);
        if (rc != ERR_OK) {
            free(codepoints);
            return rc;
        }

        codepoints[count++] = codepoint;
        offset += consumed;
    }

    *codepoints_out = codepoints;
    *count_out = count;
    return ERR_OK;
}

static int32_t croft_wit_text_insert_utf8(Text* text,
                                          SapTxnCtx* txn,
                                          size_t offset,
                                          const uint8_t* utf8,
                                          size_t utf8_len)
{
    uint32_t* codepoints = NULL;
    size_t codepoint_count = 0u;
    size_t i;
    int32_t rc;

    if (!text || !txn) {
        return ERR_INVALID;
    }
    if (offset > text_length(text)) {
        return ERR_RANGE;
    }

    rc = croft_wit_text_decode_codepoints(utf8, utf8_len, &codepoints, &codepoint_count);
    if (rc != ERR_OK) {
        return rc;
    }

    for (i = 0u; i < codepoint_count; i++) {
        rc = text_insert(txn, text, offset + i, codepoints[i]);
        if (rc != ERR_OK) {
            free(codepoints);
            return rc;
        }
    }

    free(codepoints);
    return ERR_OK;
}

static int32_t croft_wit_text_delete_range(Text* text, SapTxnCtx* txn, size_t start, size_t end)
{
    size_t count;
    size_t i;
    int32_t rc;

    if (!text || !txn) {
        return ERR_INVALID;
    }
    if (start > end || end > text_length(text)) {
        return ERR_RANGE;
    }

    count = end - start;
    for (i = 0u; i < count; i++) {
        rc = text_delete(txn, text, start, NULL);
        if (rc != ERR_OK) {
            return rc;
        }
    }

    return ERR_OK;
}

croft_wit_text_runtime* croft_wit_text_runtime_create(
    const croft_wit_text_runtime_config* config)
{
    croft_wit_text_runtime_config local = {0};
    SapArenaOptions arena_options;
    croft_wit_text_runtime* runtime;
    int32_t rc;

    croft_wit_text_runtime_config_default(&local);
    if (config) {
        if (config->page_size > 0u) {
            local.page_size = config->page_size;
        }
        if (config->initial_bytes > 0u) {
            local.initial_bytes = config->initial_bytes;
        }
        if (config->max_bytes > 0u) {
            local.max_bytes = config->max_bytes;
        }
    }

    runtime = (croft_wit_text_runtime*)calloc(1u, sizeof(*runtime));
    if (!runtime) {
        return NULL;
    }

    memset(&arena_options, 0, sizeof(arena_options));
    arena_options.type = SAP_ARENA_BACKING_LINEAR;
    arena_options.page_size = local.page_size;
    arena_options.cfg.linear.initial_bytes = local.initial_bytes;
    arena_options.cfg.linear.max_bytes = local.max_bytes;

    rc = sap_arena_init(&runtime->arena, &arena_options);
    if (rc != ERR_OK) {
        free(runtime);
        return NULL;
    }

    runtime->env = sap_env_create(runtime->arena, local.page_size);
    if (!runtime->env) {
        sap_arena_destroy(runtime->arena);
        free(runtime);
        return NULL;
    }

    rc = sap_seq_subsystem_init(runtime->env);
    if (rc != ERR_OK) {
        sap_env_destroy(runtime->env);
        sap_arena_destroy(runtime->arena);
        free(runtime);
        return NULL;
    }

    return runtime;
}

void croft_wit_text_runtime_destroy(croft_wit_text_runtime* runtime)
{
    size_t i;

    if (!runtime) {
        return;
    }

    if (runtime->env) {
        for (i = 0u; i < runtime->slot_count; i++) {
            if (runtime->slots[i].text) {
                text_free(runtime->env, runtime->slots[i].text);
            }
        }
    }

    free(runtime->slots);
    if (runtime->env) {
        sap_env_destroy(runtime->env);
    }
    if (runtime->arena) {
        sap_arena_destroy(runtime->arena);
    }
    free(runtime);
}

static int32_t croft_wit_text_dispatch_open(void* ctx,
                                            const SapWitCommonCoreTextOpen* request,
                                            SapWitCommonCoreTextReply* reply_out)
{
    croft_wit_text_runtime* runtime = (croft_wit_text_runtime*)ctx;
    SapTxnCtx* txn;
    Text* text;
    SapWitCommonCoreTextResource handle;
    int32_t rc;

    if (!runtime || !request || !reply_out) {
        return ERR_INVALID;
    }

    text = text_new(runtime->env);
    if (!text) {
        croft_wit_text_reply_text_err(reply_out, "oom");
        return ERR_OK;
    }

    txn = sap_txn_begin(runtime->env, NULL, 0u);
    if (!txn) {
        text_free(runtime->env, text);
        croft_wit_text_reply_text_err(reply_out, "busy");
        return ERR_OK;
    }

    rc = text_from_utf8(txn, text, request->initial_data, request->initial_len);
    if (rc == ERR_OK) {
        rc = sap_txn_commit(txn);
        txn = NULL;
    }
    if (txn) {
        sap_txn_abort(txn);
    }
    if (rc != ERR_OK) {
        text_free(runtime->env, text);
        croft_wit_text_reply_text_err(reply_out, croft_wit_text_input_error_from_rc(rc));
        return ERR_OK;
    }

    rc = croft_wit_text_slots_insert(runtime, text, &handle);
    if (rc != ERR_OK) {
        text_free(runtime->env, text);
        croft_wit_text_reply_text_err(reply_out, croft_wit_error_from_rc(rc));
        return ERR_OK;
    }

    croft_wit_text_reply_text_ok(reply_out, handle);
    return ERR_OK;
}

static int32_t croft_wit_text_dispatch_clone(void* ctx,
                                             const SapWitCommonCoreTextClone* request,
                                             SapWitCommonCoreTextReply* reply_out)
{
    croft_wit_text_runtime* runtime = (croft_wit_text_runtime*)ctx;
    Text* source;
    Text* clone;
    SapWitCommonCoreTextResource handle;
    int32_t rc;

    if (!runtime || !request || !reply_out) {
        return ERR_INVALID;
    }

    source = croft_wit_text_slots_lookup(runtime, request->source);
    if (!source) {
        croft_wit_text_reply_text_err(reply_out, "invalid-handle");
        return ERR_OK;
    }

    clone = text_clone(runtime->env, source);
    if (!clone) {
        croft_wit_text_reply_text_err(reply_out, "oom");
        return ERR_OK;
    }

    rc = croft_wit_text_slots_insert(runtime, clone, &handle);
    if (rc != ERR_OK) {
        text_free(runtime->env, clone);
        croft_wit_text_reply_text_err(reply_out, croft_wit_error_from_rc(rc));
        return ERR_OK;
    }

    croft_wit_text_reply_text_ok(reply_out, handle);
    return ERR_OK;
}

static int32_t croft_wit_text_dispatch_insert(void* ctx,
                                              const SapWitCommonCoreTextInsert* request,
                                              SapWitCommonCoreTextReply* reply_out)
{
    croft_wit_text_runtime* runtime = (croft_wit_text_runtime*)ctx;
    SapTxnCtx* txn;
    Text* text;
    int32_t rc;

    if (!runtime || !request || !reply_out) {
        return ERR_INVALID;
    }

    text = croft_wit_text_slots_lookup(runtime, request->text);
    if (!text) {
        croft_wit_text_reply_status_err(reply_out, "invalid-handle");
        return ERR_OK;
    }

    txn = sap_txn_begin(runtime->env, NULL, 0u);
    if (!txn) {
        croft_wit_text_reply_status_err(reply_out, "busy");
        return ERR_OK;
    }

    rc = croft_wit_text_insert_utf8(text, txn, request->offset, request->utf8_data, request->utf8_len);
    if (rc == ERR_OK) {
        rc = sap_txn_commit(txn);
        txn = NULL;
    }
    if (txn) {
        sap_txn_abort(txn);
    }

    if (rc != ERR_OK) {
        croft_wit_text_reply_status_err(reply_out, croft_wit_text_input_error_from_rc(rc));
        return ERR_OK;
    }

    croft_wit_text_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_wit_text_dispatch_delete(void* ctx,
                                              const SapWitCommonCoreTextDelete* request,
                                              SapWitCommonCoreTextReply* reply_out)
{
    croft_wit_text_runtime* runtime = (croft_wit_text_runtime*)ctx;
    SapTxnCtx* txn;
    Text* text;
    int32_t rc;

    if (!runtime || !request || !reply_out) {
        return ERR_INVALID;
    }

    text = croft_wit_text_slots_lookup(runtime, request->text);
    if (!text) {
        croft_wit_text_reply_status_err(reply_out, "invalid-handle");
        return ERR_OK;
    }

    txn = sap_txn_begin(runtime->env, NULL, 0u);
    if (!txn) {
        croft_wit_text_reply_status_err(reply_out, "busy");
        return ERR_OK;
    }

    rc = croft_wit_text_delete_range(text, txn, request->start, request->end);
    if (rc == ERR_OK) {
        rc = sap_txn_commit(txn);
        txn = NULL;
    }
    if (txn) {
        sap_txn_abort(txn);
    }

    if (rc != ERR_OK) {
        croft_wit_text_reply_status_err(reply_out, croft_wit_error_from_rc(rc));
        return ERR_OK;
    }

    croft_wit_text_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_wit_text_dispatch_export(void* ctx,
                                              const SapWitCommonCoreTextExport* request,
                                              SapWitCommonCoreTextReply* reply_out)
{
    croft_wit_text_runtime* runtime = (croft_wit_text_runtime*)ctx;
    Text* text;
    size_t utf8_len = 0u;
    size_t written = 0u;
    uint8_t* buffer;
    int32_t rc;

    if (!runtime || !request || !reply_out) {
        return ERR_INVALID;
    }

    text = croft_wit_text_slots_lookup(runtime, request->text);
    if (!text) {
        croft_wit_text_reply_export_err(reply_out, "invalid-handle");
        return ERR_OK;
    }

    rc = text_utf8_length(text, &utf8_len);
    if (rc != ERR_OK) {
        croft_wit_text_reply_export_err(reply_out, croft_wit_error_from_rc(rc));
        return ERR_OK;
    }

    buffer = (uint8_t*)malloc(utf8_len + 1u);
    if (!buffer) {
        croft_wit_text_reply_export_err(reply_out, "oom");
        return ERR_OK;
    }

    rc = text_to_utf8(text, buffer, utf8_len + 1u, &written);
    if (rc != ERR_OK) {
        free(buffer);
        croft_wit_text_reply_export_err(reply_out, croft_wit_error_from_rc(rc));
        return ERR_OK;
    }

    buffer[written] = '\0';
    croft_wit_text_reply_export_ok(reply_out, buffer, (uint32_t)written);
    return ERR_OK;
}

static int32_t croft_wit_text_dispatch_drop(void* ctx,
                                            const SapWitCommonCoreTextDrop* request,
                                            SapWitCommonCoreTextReply* reply_out)
{
    croft_wit_text_runtime* runtime = (croft_wit_text_runtime*)ctx;
    int32_t rc;

    if (!runtime || !request || !reply_out) {
        return ERR_INVALID;
    }

    rc = croft_wit_text_slots_release(runtime, request->text);
    if (rc == ERR_NOT_FOUND || rc == ERR_INVALID) {
        croft_wit_text_reply_status_err(reply_out, "invalid-handle");
        return ERR_OK;
    }
    if (rc != ERR_OK) {
        croft_wit_text_reply_status_err(reply_out, croft_wit_error_from_rc(rc));
        return ERR_OK;
    }

    croft_wit_text_reply_status_ok(reply_out);
    return ERR_OK;
}

static const SapWitCommonCoreTextDispatchOps g_croft_wit_text_dispatch_ops = {
    .open = croft_wit_text_dispatch_open,
    .clone = croft_wit_text_dispatch_clone,
    .insert = croft_wit_text_dispatch_insert,
    .delete = croft_wit_text_dispatch_delete,
    .export = croft_wit_text_dispatch_export,
    .drop = croft_wit_text_dispatch_drop,
};

int32_t croft_wit_text_runtime_dispatch(croft_wit_text_runtime* runtime,
                                        const SapWitCommonCoreTextCommand* command,
                                        SapWitCommonCoreTextReply* reply_out)
{
    int32_t rc;

    if (!runtime || !command || !reply_out) {
        return ERR_INVALID;
    }

    rc = sap_wit_dispatch_common_core_text(runtime, &g_croft_wit_text_dispatch_ops, command, reply_out);
    return rc == -1 ? ERR_INVALID : rc;
}
