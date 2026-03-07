#include "croft/wit_store_runtime.h"

#include "sapling/arena.h"
#include "sapling/err.h"
#include "sapling/sapling.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    SapMemArena* arena;
    DB* db;
} croft_wit_db_slot;

typedef struct {
    Txn* txn;
    SapWitCommonCoreDbResource db_handle;
} croft_wit_txn_slot;

struct croft_wit_store_runtime {
    uint32_t default_page_size;
    uint64_t initial_bytes;
    uint64_t max_bytes;
    croft_wit_db_slot* db_slots;
    size_t db_slot_count;
    size_t db_slot_cap;
    croft_wit_txn_slot* txn_slots;
    size_t txn_slot_count;
    size_t txn_slot_cap;
};

static uint8_t croft_wit_store_error_from_rc(int32_t rc)
{
    switch (rc) {
        case ERR_OOM:
            return SAP_WIT_COMMON_CORE_COMMON_ERROR_OOM;
        case ERR_RANGE:
            return SAP_WIT_COMMON_CORE_COMMON_ERROR_RANGE;
        case ERR_BUSY:
            return SAP_WIT_COMMON_CORE_COMMON_ERROR_BUSY;
        case ERR_NOT_FOUND:
            return SAP_WIT_COMMON_CORE_COMMON_ERROR_NOT_FOUND;
        case ERR_READONLY:
            return SAP_WIT_COMMON_CORE_COMMON_ERROR_READONLY;
        case ERR_CONFLICT:
            return SAP_WIT_COMMON_CORE_COMMON_ERROR_CONFLICT;
        default:
            return SAP_WIT_COMMON_CORE_COMMON_ERROR_INTERNAL;
    }
}

static void croft_wit_store_reply_zero(SapWitCommonCoreStoreReply* reply)
{
    if (!reply) {
        return;
    }
    memset(reply, 0, sizeof(*reply));
}

static void croft_wit_store_reply_db_ok(SapWitCommonCoreStoreReply* reply, SapWitCommonCoreDbResource handle)
{
    croft_wit_store_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_STORE_REPLY_DB;
    reply->val.db.case_tag = SAP_WIT_COMMON_CORE_DB_OP_RESULT_OK;
    reply->val.db.val.ok = handle;
}

static void croft_wit_store_reply_db_err(SapWitCommonCoreStoreReply* reply, uint8_t err)
{
    croft_wit_store_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_STORE_REPLY_DB;
    reply->val.db.case_tag = SAP_WIT_COMMON_CORE_DB_OP_RESULT_ERR;
    reply->val.db.val.err = err;
}

static void croft_wit_store_reply_txn_ok(SapWitCommonCoreStoreReply* reply, SapWitCommonCoreTxnResource handle)
{
    croft_wit_store_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_STORE_REPLY_TXN;
    reply->val.txn.case_tag = SAP_WIT_COMMON_CORE_TXN_OP_RESULT_OK;
    reply->val.txn.val.ok = handle;
}

static void croft_wit_store_reply_txn_err(SapWitCommonCoreStoreReply* reply, uint8_t err)
{
    croft_wit_store_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_STORE_REPLY_TXN;
    reply->val.txn.case_tag = SAP_WIT_COMMON_CORE_TXN_OP_RESULT_ERR;
    reply->val.txn.val.err = err;
}

static void croft_wit_store_reply_status_ok(SapWitCommonCoreStoreReply* reply)
{
    croft_wit_store_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_STORE_REPLY_STATUS;
    reply->val.status.case_tag = SAP_WIT_COMMON_CORE_STATUS_OK;
}

static void croft_wit_store_reply_status_err(SapWitCommonCoreStoreReply* reply, uint8_t err)
{
    croft_wit_store_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_STORE_REPLY_STATUS;
    reply->val.status.case_tag = SAP_WIT_COMMON_CORE_STATUS_ERR;
    reply->val.status.val.err = err;
}

static void croft_wit_store_reply_get_ok(SapWitCommonCoreStoreReply* reply, uint8_t* value, uint32_t len)
{
    croft_wit_store_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_STORE_REPLY_GET;
    reply->val.get.case_tag = SAP_WIT_COMMON_CORE_KV_GET_RESULT_OK;
    reply->val.get.val.ok.data = value;
    reply->val.get.val.ok.len = len;
}

static void croft_wit_store_reply_get_not_found(SapWitCommonCoreStoreReply* reply)
{
    croft_wit_store_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_STORE_REPLY_GET;
    reply->val.get.case_tag = SAP_WIT_COMMON_CORE_KV_GET_RESULT_NOT_FOUND;
}

static void croft_wit_store_reply_get_err(SapWitCommonCoreStoreReply* reply, uint8_t err)
{
    croft_wit_store_reply_zero(reply);
    reply->case_tag = SAP_WIT_COMMON_CORE_STORE_REPLY_GET;
    reply->val.get.case_tag = SAP_WIT_COMMON_CORE_KV_GET_RESULT_ERR;
    reply->val.get.val.err = err;
}

void croft_wit_store_reply_dispose(SapWitCommonCoreStoreReply* reply)
{
    if (!reply) {
        return;
    }

    if (reply->case_tag == SAP_WIT_COMMON_CORE_STORE_REPLY_GET
            && reply->val.get.case_tag == SAP_WIT_COMMON_CORE_KV_GET_RESULT_OK
            && reply->val.get.val.ok.data) {
        free((void*)reply->val.get.val.ok.data);
    }

    memset(reply, 0, sizeof(*reply));
}

void croft_wit_store_runtime_config_default(croft_wit_store_runtime_config* config)
{
    if (!config) {
        return;
    }

    config->default_page_size = 4096u;
    config->initial_bytes = 64u * 1024u;
    config->max_bytes = 512u * 1024u;
}

static int32_t croft_wit_db_slots_reserve(croft_wit_store_runtime* runtime, size_t needed)
{
    croft_wit_db_slot* next_slots;
    size_t next_cap;

    if (!runtime) {
        return ERR_INVALID;
    }
    if (runtime->db_slot_cap >= needed) {
        return ERR_OK;
    }

    next_cap = runtime->db_slot_cap > 0u ? runtime->db_slot_cap * 2u : 4u;
    while (next_cap < needed) {
        next_cap *= 2u;
    }

    next_slots = (croft_wit_db_slot*)realloc(runtime->db_slots, next_cap * sizeof(*next_slots));
    if (!next_slots) {
        return ERR_OOM;
    }

    memset(next_slots + runtime->db_slot_cap, 0,
           (next_cap - runtime->db_slot_cap) * sizeof(*next_slots));
    runtime->db_slots = next_slots;
    runtime->db_slot_cap = next_cap;
    return ERR_OK;
}

static int32_t croft_wit_txn_slots_reserve(croft_wit_store_runtime* runtime, size_t needed)
{
    croft_wit_txn_slot* next_slots;
    size_t next_cap;

    if (!runtime) {
        return ERR_INVALID;
    }
    if (runtime->txn_slot_cap >= needed) {
        return ERR_OK;
    }

    next_cap = runtime->txn_slot_cap > 0u ? runtime->txn_slot_cap * 2u : 8u;
    while (next_cap < needed) {
        next_cap *= 2u;
    }

    next_slots = (croft_wit_txn_slot*)realloc(runtime->txn_slots, next_cap * sizeof(*next_slots));
    if (!next_slots) {
        return ERR_OOM;
    }

    memset(next_slots + runtime->txn_slot_cap, 0,
           (next_cap - runtime->txn_slot_cap) * sizeof(*next_slots));
    runtime->txn_slots = next_slots;
    runtime->txn_slot_cap = next_cap;
    return ERR_OK;
}

static int32_t croft_wit_db_slots_insert(croft_wit_store_runtime* runtime,
                                         SapMemArena* arena,
                                         DB* db,
                                         SapWitCommonCoreDbResource* handle_out)
{
    size_t i;
    int32_t rc;

    if (!runtime || !arena || !db || !handle_out) {
        return ERR_INVALID;
    }

    for (i = 0u; i < runtime->db_slot_count; i++) {
        if (!runtime->db_slots[i].db) {
            runtime->db_slots[i].arena = arena;
            runtime->db_slots[i].db = db;
            *handle_out = (SapWitCommonCoreDbResource)(i + 1u);
            return ERR_OK;
        }
    }

    rc = croft_wit_db_slots_reserve(runtime, runtime->db_slot_count + 1u);
    if (rc != ERR_OK) {
        return rc;
    }

    runtime->db_slots[runtime->db_slot_count].arena = arena;
    runtime->db_slots[runtime->db_slot_count].db = db;
    runtime->db_slot_count++;
    *handle_out = (SapWitCommonCoreDbResource)runtime->db_slot_count;
    return ERR_OK;
}

static int32_t croft_wit_txn_slots_insert(croft_wit_store_runtime* runtime,
                                          Txn* txn,
                                          SapWitCommonCoreDbResource db_handle,
                                          SapWitCommonCoreTxnResource* handle_out)
{
    size_t i;
    int32_t rc;

    if (!runtime || !txn || !handle_out) {
        return ERR_INVALID;
    }

    for (i = 0u; i < runtime->txn_slot_count; i++) {
        if (!runtime->txn_slots[i].txn) {
            runtime->txn_slots[i].txn = txn;
            runtime->txn_slots[i].db_handle = db_handle;
            *handle_out = (SapWitCommonCoreTxnResource)(i + 1u);
            return ERR_OK;
        }
    }

    rc = croft_wit_txn_slots_reserve(runtime, runtime->txn_slot_count + 1u);
    if (rc != ERR_OK) {
        return rc;
    }

    runtime->txn_slots[runtime->txn_slot_count].txn = txn;
    runtime->txn_slots[runtime->txn_slot_count].db_handle = db_handle;
    runtime->txn_slot_count++;
    *handle_out = (SapWitCommonCoreTxnResource)runtime->txn_slot_count;
    return ERR_OK;
}

static croft_wit_db_slot* croft_wit_db_slots_lookup(croft_wit_store_runtime* runtime,
                                                    SapWitCommonCoreDbResource handle)
{
    size_t slot;

    if (!runtime || handle == SAP_WIT_COMMON_CORE_DB_RESOURCE_INVALID) {
        return NULL;
    }

    slot = (size_t)handle - 1u;
    if (slot >= runtime->db_slot_count || !runtime->db_slots[slot].db) {
        return NULL;
    }

    return &runtime->db_slots[slot];
}

static croft_wit_txn_slot* croft_wit_txn_slots_lookup(croft_wit_store_runtime* runtime,
                                                      SapWitCommonCoreTxnResource handle)
{
    size_t slot;

    if (!runtime || handle == SAP_WIT_COMMON_CORE_TXN_RESOURCE_INVALID) {
        return NULL;
    }

    slot = (size_t)handle - 1u;
    if (slot >= runtime->txn_slot_count || !runtime->txn_slots[slot].txn) {
        return NULL;
    }

    return &runtime->txn_slots[slot];
}

static void croft_wit_txn_slot_release(croft_wit_store_runtime* runtime, SapWitCommonCoreTxnResource handle)
{
    size_t slot;

    if (!runtime || handle == SAP_WIT_COMMON_CORE_TXN_RESOURCE_INVALID) {
        return;
    }

    slot = (size_t)handle - 1u;
    if (slot >= runtime->txn_slot_count) {
        return;
    }

    runtime->txn_slots[slot].txn = NULL;
    runtime->txn_slots[slot].db_handle = SAP_WIT_COMMON_CORE_DB_RESOURCE_INVALID;
}

static int croft_wit_db_has_live_txn(const croft_wit_store_runtime* runtime, SapWitCommonCoreDbResource handle)
{
    size_t i;

    if (!runtime || handle == SAP_WIT_COMMON_CORE_DB_RESOURCE_INVALID) {
        return 0;
    }

    for (i = 0u; i < runtime->txn_slot_count; i++) {
        if (runtime->txn_slots[i].txn && runtime->txn_slots[i].db_handle == handle) {
            return 1;
        }
    }

    return 0;
}

croft_wit_store_runtime* croft_wit_store_runtime_create(
    const croft_wit_store_runtime_config* config)
{
    croft_wit_store_runtime_config local = {0};
    croft_wit_store_runtime* runtime;

    croft_wit_store_runtime_config_default(&local);
    if (config) {
        if (config->default_page_size > 0u) {
            local.default_page_size = config->default_page_size;
        }
        if (config->initial_bytes > 0u) {
            local.initial_bytes = config->initial_bytes;
        }
        if (config->max_bytes > 0u) {
            local.max_bytes = config->max_bytes;
        }
    }

    runtime = (croft_wit_store_runtime*)calloc(1u, sizeof(*runtime));
    if (!runtime) {
        return NULL;
    }

    runtime->default_page_size = local.default_page_size;
    runtime->initial_bytes = local.initial_bytes;
    runtime->max_bytes = local.max_bytes;
    return runtime;
}

void croft_wit_store_runtime_destroy(croft_wit_store_runtime* runtime)
{
    size_t i;

    if (!runtime) {
        return;
    }

    for (i = 0u; i < runtime->txn_slot_count; i++) {
        if (runtime->txn_slots[i].txn) {
            txn_abort(runtime->txn_slots[i].txn);
        }
    }

    for (i = 0u; i < runtime->db_slot_count; i++) {
        if (runtime->db_slots[i].db) {
            db_close(runtime->db_slots[i].db);
        }
        if (runtime->db_slots[i].arena) {
            sap_arena_destroy(runtime->db_slots[i].arena);
        }
    }

    free(runtime->txn_slots);
    free(runtime->db_slots);
    free(runtime);
}

static int32_t croft_wit_store_dispatch_db_open(croft_wit_store_runtime* runtime,
                                                const SapWitCommonCoreDbOpen* request,
                                                SapWitCommonCoreStoreReply* reply_out)
{
    SapArenaOptions options;
    SapMemArena* arena = NULL;
    DB* db = NULL;
    SapWitCommonCoreDbResource handle;
    uint32_t page_size;
    int32_t rc;

    if (!runtime || !request || !reply_out) {
        return ERR_INVALID;
    }

    page_size = request->page_size > 0u ? request->page_size : runtime->default_page_size;
    memset(&options, 0, sizeof(options));
    options.type = SAP_ARENA_BACKING_LINEAR;
    options.page_size = page_size;
    options.cfg.linear.initial_bytes = runtime->initial_bytes;
    options.cfg.linear.max_bytes = runtime->max_bytes;

    rc = sap_arena_init(&arena, &options);
    if (rc != ERR_OK) {
        croft_wit_store_reply_db_err(reply_out, croft_wit_store_error_from_rc(rc));
        return ERR_OK;
    }

    db = db_open(arena, page_size, NULL, NULL);
    if (!db) {
        sap_arena_destroy(arena);
        croft_wit_store_reply_db_err(reply_out, SAP_WIT_COMMON_CORE_COMMON_ERROR_OOM);
        return ERR_OK;
    }

    rc = croft_wit_db_slots_insert(runtime, arena, db, &handle);
    if (rc != ERR_OK) {
        db_close(db);
        sap_arena_destroy(arena);
        croft_wit_store_reply_db_err(reply_out, croft_wit_store_error_from_rc(rc));
        return ERR_OK;
    }

    croft_wit_store_reply_db_ok(reply_out, handle);
    return ERR_OK;
}

static int32_t croft_wit_store_dispatch_db_drop(croft_wit_store_runtime* runtime,
                                                const SapWitCommonCoreDbDrop* request,
                                                SapWitCommonCoreStoreReply* reply_out)
{
    size_t slot;
    croft_wit_db_slot* db_slot;

    if (!runtime || !request || !reply_out || request->db == SAP_WIT_COMMON_CORE_DB_RESOURCE_INVALID) {
        return ERR_INVALID;
    }

    db_slot = croft_wit_db_slots_lookup(runtime, request->db);
    if (!db_slot) {
        croft_wit_store_reply_status_err(reply_out, SAP_WIT_COMMON_CORE_COMMON_ERROR_INVALID_HANDLE);
        return ERR_OK;
    }
    if (croft_wit_db_has_live_txn(runtime, request->db)) {
        croft_wit_store_reply_status_err(reply_out, SAP_WIT_COMMON_CORE_COMMON_ERROR_BUSY);
        return ERR_OK;
    }

    slot = (size_t)request->db - 1u;
    db_close(db_slot->db);
    sap_arena_destroy(db_slot->arena);
    runtime->db_slots[slot].db = NULL;
    runtime->db_slots[slot].arena = NULL;

    croft_wit_store_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_wit_store_dispatch_txn_begin(croft_wit_store_runtime* runtime,
                                                  const SapWitCommonCoreTxnBegin* request,
                                                  SapWitCommonCoreStoreReply* reply_out)
{
    croft_wit_db_slot* db_slot;
    Txn* txn;
    SapWitCommonCoreTxnResource handle;
    unsigned flags = 0u;
    int32_t rc;

    if (!runtime || !request || !reply_out) {
        return ERR_INVALID;
    }

    db_slot = croft_wit_db_slots_lookup(runtime, request->db);
    if (!db_slot) {
        croft_wit_store_reply_txn_err(reply_out, SAP_WIT_COMMON_CORE_COMMON_ERROR_INVALID_HANDLE);
        return ERR_OK;
    }

    if (request->read_only) {
        flags |= TXN_RDONLY;
    }

    txn = txn_begin(db_slot->db, NULL, flags);
    if (!txn) {
        croft_wit_store_reply_txn_err(reply_out, SAP_WIT_COMMON_CORE_COMMON_ERROR_BUSY);
        return ERR_OK;
    }

    rc = croft_wit_txn_slots_insert(runtime, txn, request->db, &handle);
    if (rc != ERR_OK) {
        txn_abort(txn);
        croft_wit_store_reply_txn_err(reply_out, croft_wit_store_error_from_rc(rc));
        return ERR_OK;
    }

    croft_wit_store_reply_txn_ok(reply_out, handle);
    return ERR_OK;
}

static int32_t croft_wit_store_dispatch_txn_commit(croft_wit_store_runtime* runtime,
                                                   const SapWitCommonCoreTxnCommit* request,
                                                   SapWitCommonCoreStoreReply* reply_out)
{
    croft_wit_txn_slot* txn_slot;
    int32_t rc;

    if (!runtime || !request || !reply_out) {
        return ERR_INVALID;
    }

    txn_slot = croft_wit_txn_slots_lookup(runtime, request->txn);
    if (!txn_slot) {
        croft_wit_store_reply_status_err(reply_out, SAP_WIT_COMMON_CORE_COMMON_ERROR_INVALID_HANDLE);
        return ERR_OK;
    }

    rc = txn_commit(txn_slot->txn);
    croft_wit_txn_slot_release(runtime, request->txn);
    if (rc != ERR_OK) {
        croft_wit_store_reply_status_err(reply_out, croft_wit_store_error_from_rc(rc));
        return ERR_OK;
    }

    croft_wit_store_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_wit_store_dispatch_txn_abort(croft_wit_store_runtime* runtime,
                                                  const SapWitCommonCoreTxnAbort* request,
                                                  SapWitCommonCoreStoreReply* reply_out)
{
    croft_wit_txn_slot* txn_slot;

    if (!runtime || !request || !reply_out) {
        return ERR_INVALID;
    }

    txn_slot = croft_wit_txn_slots_lookup(runtime, request->txn);
    if (!txn_slot) {
        croft_wit_store_reply_status_err(reply_out, SAP_WIT_COMMON_CORE_COMMON_ERROR_INVALID_HANDLE);
        return ERR_OK;
    }

    txn_abort(txn_slot->txn);
    croft_wit_txn_slot_release(runtime, request->txn);
    croft_wit_store_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_wit_store_dispatch_kv_put(croft_wit_store_runtime* runtime,
                                               const SapWitCommonCoreKvPut* request,
                                               SapWitCommonCoreStoreReply* reply_out)
{
    croft_wit_txn_slot* txn_slot;
    int32_t rc;

    if (!runtime || !request || !reply_out) {
        return ERR_INVALID;
    }

    txn_slot = croft_wit_txn_slots_lookup(runtime, request->txn);
    if (!txn_slot) {
        croft_wit_store_reply_status_err(reply_out, SAP_WIT_COMMON_CORE_COMMON_ERROR_INVALID_HANDLE);
        return ERR_OK;
    }

    rc = txn_put(txn_slot->txn,
                 request->key_data,
                 request->key_len,
                 request->value_data,
                 request->value_len);
    if (rc != ERR_OK) {
        croft_wit_store_reply_status_err(reply_out, croft_wit_store_error_from_rc(rc));
        return ERR_OK;
    }

    croft_wit_store_reply_status_ok(reply_out);
    return ERR_OK;
}

/*
 * Reads cross the WIT barrier as owned copies instead of borrowed Sapling
 * pointers. That keeps the generated-side ownership rules simple and makes the
 * later move to remote/distributed implementations easier to model.
 */
static int32_t croft_wit_store_dispatch_kv_get(croft_wit_store_runtime* runtime,
                                               const SapWitCommonCoreKvGet* request,
                                               SapWitCommonCoreStoreReply* reply_out)
{
    croft_wit_txn_slot* txn_slot;
    const void* value = NULL;
    uint32_t value_len = 0u;
    uint8_t* copy = NULL;
    int32_t rc;

    if (!runtime || !request || !reply_out) {
        return ERR_INVALID;
    }

    txn_slot = croft_wit_txn_slots_lookup(runtime, request->txn);
    if (!txn_slot) {
        croft_wit_store_reply_get_err(reply_out, SAP_WIT_COMMON_CORE_COMMON_ERROR_INVALID_HANDLE);
        return ERR_OK;
    }

    rc = txn_get(txn_slot->txn, request->key_data, request->key_len, &value, &value_len);
    if (rc == ERR_NOT_FOUND) {
        croft_wit_store_reply_get_not_found(reply_out);
        return ERR_OK;
    }
    if (rc != ERR_OK) {
        croft_wit_store_reply_get_err(reply_out, croft_wit_store_error_from_rc(rc));
        return ERR_OK;
    }

    copy = (uint8_t*)malloc(value_len + 1u);
    if (!copy) {
        croft_wit_store_reply_get_err(reply_out, SAP_WIT_COMMON_CORE_COMMON_ERROR_OOM);
        return ERR_OK;
    }
    if (value_len > 0u) {
        memcpy(copy, value, value_len);
    }
    copy[value_len] = '\0';

    croft_wit_store_reply_get_ok(reply_out, copy, value_len);
    return ERR_OK;
}

int32_t croft_wit_store_runtime_dispatch(croft_wit_store_runtime* runtime,
                                         const SapWitCommonCoreStoreCommand* command,
                                         SapWitCommonCoreStoreReply* reply_out)
{
    if (!runtime || !command || !reply_out) {
        return ERR_INVALID;
    }

    switch (command->case_tag) {
        case SAP_WIT_COMMON_CORE_STORE_COMMAND_DB_OPEN:
            return croft_wit_store_dispatch_db_open(runtime, &command->val.db_open, reply_out);
        case SAP_WIT_COMMON_CORE_STORE_COMMAND_DB_DROP:
            return croft_wit_store_dispatch_db_drop(runtime, &command->val.db_drop, reply_out);
        case SAP_WIT_COMMON_CORE_STORE_COMMAND_TXN_BEGIN:
            return croft_wit_store_dispatch_txn_begin(runtime, &command->val.txn_begin, reply_out);
        case SAP_WIT_COMMON_CORE_STORE_COMMAND_TXN_COMMIT:
            return croft_wit_store_dispatch_txn_commit(runtime, &command->val.txn_commit, reply_out);
        case SAP_WIT_COMMON_CORE_STORE_COMMAND_TXN_ABORT:
            return croft_wit_store_dispatch_txn_abort(runtime, &command->val.txn_abort, reply_out);
        case SAP_WIT_COMMON_CORE_STORE_COMMAND_KV_PUT:
            return croft_wit_store_dispatch_kv_put(runtime, &command->val.kv_put, reply_out);
        case SAP_WIT_COMMON_CORE_STORE_COMMAND_KV_GET:
            return croft_wit_store_dispatch_kv_get(runtime, &command->val.kv_get, reply_out);
        default:
            return ERR_INVALID;
    }
}
