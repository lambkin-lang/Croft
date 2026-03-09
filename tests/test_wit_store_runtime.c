#include "croft/wit_store_runtime.h"

#include <stdint.h>
#include <string.h>

static int wit_store_expect_db_ok(const SapWitCommonCoreStoreReply* reply, SapWitCommonCoreDbResource* handle_out)
{
    if (!reply || !handle_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_COMMON_CORE_STORE_REPLY_DB
            || !reply->val.db.is_v_ok) {
        return 0;
    }
    *handle_out = reply->val.db.v_val.ok.v;
    return 1;
}

static int wit_store_expect_txn_ok(const SapWitCommonCoreStoreReply* reply, SapWitCommonCoreTxnResource* handle_out)
{
    if (!reply || !handle_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_COMMON_CORE_STORE_REPLY_TXN
            || !reply->val.txn.is_v_ok) {
        return 0;
    }
    *handle_out = reply->val.txn.v_val.ok.v;
    return 1;
}

static int wit_store_expect_status_ok(const SapWitCommonCoreStoreReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_COMMON_CORE_STORE_REPLY_STATUS
        && reply->val.status.is_v_ok;
}

static int wit_store_expect_error(const uint8_t* data, uint32_t len, const char* expected)
{
    size_t expected_len;

    if (!expected) {
        return 0;
    }
    expected_len = strlen(expected);
    return len == (uint32_t)expected_len && memcmp(data, expected, expected_len) == 0;
}

int test_wit_store_runtime_roundtrip(void)
{
    croft_wit_store_runtime* runtime;
    SapWitCommonCoreStoreCommand command = {0};
    SapWitCommonCoreStoreReply reply = {0};
    SapWitCommonCoreDbResource db = SAP_WIT_COMMON_CORE_DB_RESOURCE_INVALID;
    SapWitCommonCoreTxnResource write_txn = SAP_WIT_COMMON_CORE_TXN_RESOURCE_INVALID;
    SapWitCommonCoreTxnResource read_txn = SAP_WIT_COMMON_CORE_TXN_RESOURCE_INVALID;
    const uint8_t* value = NULL;
    uint32_t value_len = 0u;

    runtime = croft_wit_store_runtime_create(NULL);
    if (!runtime) {
        return 1;
    }

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_DB_OPEN;
    command.val.db_open.page_size = 4096u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_db_ok(&reply, &db)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_TXN_BEGIN;
    command.val.txn_begin.db = db;
    command.val.txn_begin.read_only = 0u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_txn_ok(&reply, &write_txn)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_KV_PUT;
    command.val.kv_put.txn = write_txn;
    command.val.kv_put.key_data = (const uint8_t*)"editor";
    command.val.kv_put.key_len = 6u;
    command.val.kv_put.value_data = (const uint8_t*)"small binaries";
    command.val.kv_put.value_len = 14u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_status_ok(&reply)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_TXN_COMMIT;
    command.val.txn_commit.txn = write_txn;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_status_ok(&reply)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_TXN_BEGIN;
    command.val.txn_begin.db = db;
    command.val.txn_begin.read_only = 1u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_txn_ok(&reply, &read_txn)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_KV_GET;
    command.val.kv_get.txn = read_txn;
    command.val.kv_get.key_data = (const uint8_t*)"editor";
    command.val.kv_get.key_len = 6u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || reply.case_tag != SAP_WIT_COMMON_CORE_STORE_REPLY_GET
            || !reply.val.get.is_v_ok
            || !reply.val.get.v_val.ok.has_v) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    value = reply.val.get.v_val.ok.v_data;
    value_len = reply.val.get.v_val.ok.v_len;
    if (value_len != 14u || memcmp(value, "small binaries", 14u) != 0) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_TXN_ABORT;
    command.val.txn_abort.txn = read_txn;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_status_ok(&reply)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_DB_DROP;
    command.val.db_drop.db = db;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_status_ok(&reply)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    croft_wit_store_runtime_destroy(runtime);
    return 0;
}

int test_wit_store_runtime_readonly_put_rejected(void)
{
    croft_wit_store_runtime* runtime;
    SapWitCommonCoreStoreCommand command = {0};
    SapWitCommonCoreStoreReply reply = {0};
    SapWitCommonCoreDbResource db = SAP_WIT_COMMON_CORE_DB_RESOURCE_INVALID;
    SapWitCommonCoreTxnResource read_txn = SAP_WIT_COMMON_CORE_TXN_RESOURCE_INVALID;

    runtime = croft_wit_store_runtime_create(NULL);
    if (!runtime) {
        return 1;
    }

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_DB_OPEN;
    command.val.db_open.page_size = 4096u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_db_ok(&reply, &db)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_TXN_BEGIN;
    command.val.txn_begin.db = db;
    command.val.txn_begin.read_only = 1u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_txn_ok(&reply, &read_txn)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_KV_PUT;
    command.val.kv_put.txn = read_txn;
    command.val.kv_put.key_data = (const uint8_t*)"k";
    command.val.kv_put.key_len = 1u;
    command.val.kv_put.value_data = (const uint8_t*)"v";
    command.val.kv_put.value_len = 1u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || reply.case_tag != SAP_WIT_COMMON_CORE_STORE_REPLY_STATUS
            || reply.val.status.is_v_ok
            || !wit_store_expect_error(reply.val.status.v_val.err.v_data,
                                       reply.val.status.v_val.err.v_len,
                                       "readonly")) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_TXN_ABORT;
    command.val.txn_abort.txn = read_txn;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_status_ok(&reply)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_DB_DROP;
    command.val.db_drop.db = db;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_status_ok(&reply)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    croft_wit_store_runtime_destroy(runtime);
    return 0;
}
