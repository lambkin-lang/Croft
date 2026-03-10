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

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_OPEN;
    command.val.open.page_size = 4096u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_db_ok(&reply, &db)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_BEGIN;
    command.val.begin.db = db;
    command.val.begin.read_only = 0u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_txn_ok(&reply, &write_txn)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_PUT;
    command.val.put.txn = write_txn;
    command.val.put.key_data = (const uint8_t*)"editor";
    command.val.put.key_len = 6u;
    command.val.put.value_data = (const uint8_t*)"small binaries";
    command.val.put.value_len = 14u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_status_ok(&reply)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_COMMIT;
    command.val.commit.txn = write_txn;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_status_ok(&reply)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_BEGIN;
    command.val.begin.db = db;
    command.val.begin.read_only = 1u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_txn_ok(&reply, &read_txn)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_GET;
    command.val.get.txn = read_txn;
    command.val.get.key_data = (const uint8_t*)"editor";
    command.val.get.key_len = 6u;
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

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_ABORT;
    command.val.abort.txn = read_txn;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_status_ok(&reply)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_DROP;
    command.val.drop.db = db;
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

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_OPEN;
    command.val.open.page_size = 4096u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_db_ok(&reply, &db)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_BEGIN;
    command.val.begin.db = db;
    command.val.begin.read_only = 1u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_txn_ok(&reply, &read_txn)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_PUT;
    command.val.put.txn = read_txn;
    command.val.put.key_data = (const uint8_t*)"k";
    command.val.put.key_len = 1u;
    command.val.put.value_data = (const uint8_t*)"v";
    command.val.put.value_len = 1u;
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

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_ABORT;
    command.val.abort.txn = read_txn;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_status_ok(&reply)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_DROP;
    command.val.drop.db = db;
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
