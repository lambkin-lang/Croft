#include "croft/wit_store_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int expect_db_handle(const SapWitCommonCoreStoreReply* reply, SapWitCommonCoreDbResource* handle_out)
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

static int expect_txn_handle(const SapWitCommonCoreStoreReply* reply, SapWitCommonCoreTxnResource* handle_out)
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

static int expect_status_ok(const SapWitCommonCoreStoreReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_COMMON_CORE_STORE_REPLY_STATUS
        && reply->val.status.is_v_ok;
}

static int expect_get_ok(const SapWitCommonCoreStoreReply* reply, const uint8_t** data_out, uint32_t* len_out)
{
    if (!reply || !data_out || !len_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_COMMON_CORE_STORE_REPLY_GET
            || !reply->val.get.is_v_ok
            || !reply->val.get.v_val.ok.has_v) {
        return 0;
    }
    *data_out = reply->val.get.v_val.ok.v_data;
    *len_out = reply->val.get.v_val.ok.v_len;
    return 1;
}

int main(void)
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
        fprintf(stderr, "example_wit_db_kv: runtime init failed\n");
        return 1;
    }

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_DB_OPEN;
    command.val.db_open.page_size = 4096u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !expect_db_handle(&reply, &db)) {
        fprintf(stderr, "example_wit_db_kv: db_open failed\n");
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_TXN_BEGIN;
    command.val.txn_begin.db = db;
    command.val.txn_begin.read_only = 0u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !expect_txn_handle(&reply, &write_txn)) {
        fprintf(stderr, "example_wit_db_kv: txn_begin write failed\n");
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
            || !expect_status_ok(&reply)) {
        fprintf(stderr, "example_wit_db_kv: kv_put failed\n");
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_TXN_COMMIT;
    command.val.txn_commit.txn = write_txn;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !expect_status_ok(&reply)) {
        fprintf(stderr, "example_wit_db_kv: txn_commit failed\n");
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_TXN_BEGIN;
    command.val.txn_begin.db = db;
    command.val.txn_begin.read_only = 1u;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !expect_txn_handle(&reply, &read_txn)) {
        fprintf(stderr, "example_wit_db_kv: txn_begin read failed\n");
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
            || !expect_get_ok(&reply, &value, &value_len)) {
        fprintf(stderr, "example_wit_db_kv: kv_get failed\n");
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    printf("editor=\"%.*s\"\n", (int)value_len, (const char*)value);
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_TXN_ABORT;
    command.val.txn_abort.txn = read_txn;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !expect_status_ok(&reply)) {
        fprintf(stderr, "example_wit_db_kv: txn_abort failed\n");
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    command.case_tag = SAP_WIT_COMMON_CORE_STORE_COMMAND_DB_DROP;
    command.val.db_drop.db = db;
    if (croft_wit_store_runtime_dispatch(runtime, &command, &reply) != 0
            || !expect_status_ok(&reply)) {
        fprintf(stderr, "example_wit_db_kv: db_drop failed\n");
        croft_wit_store_reply_dispose(&reply);
        croft_wit_store_runtime_destroy(runtime);
        return 1;
    }
    croft_wit_store_reply_dispose(&reply);

    croft_wit_store_runtime_destroy(runtime);
    return 0;
}
