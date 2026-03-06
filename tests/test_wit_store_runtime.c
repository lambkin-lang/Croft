#include "croft/wit_store_runtime.h"

#include <stdint.h>
#include <string.h>

static int wit_store_expect_db_ok(const SapWitStoreReply* reply, SapWitDbResource* handle_out)
{
    if (!reply || !handle_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_STORE_REPLY_DB
            || reply->val.db.case_tag != SAP_WIT_DB_OP_RESULT_OK) {
        return 0;
    }
    *handle_out = reply->val.db.val.ok;
    return 1;
}

static int wit_store_expect_txn_ok(const SapWitStoreReply* reply, SapWitTxnResource* handle_out)
{
    if (!reply || !handle_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_STORE_REPLY_TXN
            || reply->val.txn.case_tag != SAP_WIT_TXN_OP_RESULT_OK) {
        return 0;
    }
    *handle_out = reply->val.txn.val.ok;
    return 1;
}

static int wit_store_expect_status_ok(const SapWitStoreReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_STORE_REPLY_STATUS
        && reply->val.status.case_tag == SAP_WIT_STATUS_OK;
}

int test_wit_store_runtime_roundtrip(void)
{
    croft_wit_store_runtime* runtime;
    SapWitStoreCommand command = {0};
    SapWitStoreReply reply = {0};
    SapWitDbResource db = SAP_WIT_DB_RESOURCE_INVALID;
    SapWitTxnResource write_txn = SAP_WIT_TXN_RESOURCE_INVALID;
    SapWitTxnResource read_txn = SAP_WIT_TXN_RESOURCE_INVALID;
    const uint8_t* value = NULL;
    uint32_t value_len = 0u;

    runtime = croft_wit_store_runtime_create(NULL);
    if (!runtime) {
        return 1;
    }

    command.case_tag = SAP_WIT_STORE_COMMAND_DB_OPEN;
    command.val.db_open.page_size = 4096u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_db_ok(&reply, &db)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_STORE_COMMAND_TXN_BEGIN;
    command.val.txn_begin.db = db;
    command.val.txn_begin.read_only = 0u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_txn_ok(&reply, &write_txn)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_STORE_COMMAND_KV_PUT;
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

    command.case_tag = SAP_WIT_STORE_COMMAND_TXN_COMMIT;
    command.val.txn_commit.txn = write_txn;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_status_ok(&reply)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_STORE_COMMAND_TXN_BEGIN;
    command.val.txn_begin.db = db;
    command.val.txn_begin.read_only = 1u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_txn_ok(&reply, &read_txn)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_STORE_COMMAND_KV_GET;
    command.val.kv_get.txn = read_txn;
    command.val.kv_get.key_data = (const uint8_t*)"editor";
    command.val.kv_get.key_len = 6u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || reply.case_tag != SAP_WIT_STORE_REPLY_GET
            || reply.val.get.case_tag != SAP_WIT_KV_GET_RESULT_OK) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    value = reply.val.get.val.ok.data;
    value_len = reply.val.get.val.ok.len;
    if (value_len != 14u || memcmp(value, "small binaries", 14u) != 0) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_STORE_COMMAND_TXN_ABORT;
    command.val.txn_abort.txn = read_txn;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_status_ok(&reply)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_STORE_COMMAND_DB_DROP;
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
    SapWitStoreCommand command = {0};
    SapWitStoreReply reply = {0};
    SapWitDbResource db = SAP_WIT_DB_RESOURCE_INVALID;
    SapWitTxnResource read_txn = SAP_WIT_TXN_RESOURCE_INVALID;

    runtime = croft_wit_store_runtime_create(NULL);
    if (!runtime) {
        return 1;
    }

    command.case_tag = SAP_WIT_STORE_COMMAND_DB_OPEN;
    command.val.db_open.page_size = 4096u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_db_ok(&reply, &db)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_STORE_COMMAND_TXN_BEGIN;
    command.val.txn_begin.db = db;
    command.val.txn_begin.read_only = 1u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_txn_ok(&reply, &read_txn)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_STORE_COMMAND_KV_PUT;
    command.val.kv_put.txn = read_txn;
    command.val.kv_put.key_data = (const uint8_t*)"k";
    command.val.kv_put.key_len = 1u;
    command.val.kv_put.value_data = (const uint8_t*)"v";
    command.val.kv_put.value_len = 1u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || reply.case_tag != SAP_WIT_STORE_REPLY_STATUS
            || reply.val.status.case_tag != SAP_WIT_STATUS_ERR
            || reply.val.status.val.err != SAP_WIT_COMMON_ERROR_READONLY) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_STORE_COMMAND_TXN_ABORT;
    command.val.txn_abort.txn = read_txn;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !wit_store_expect_status_ok(&reply)) {
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_STORE_COMMAND_DB_DROP;
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
