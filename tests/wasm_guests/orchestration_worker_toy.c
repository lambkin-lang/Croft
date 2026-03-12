#include "orchestration_guest_runtime.h"

typedef struct {
    uint32_t initialized;
    CroftOrchGuestRuntime runtime;
    SapWitOrchestrationWorkerWorldExports exports;
} CroftOrchToyWorkerState;

static CroftOrchToyWorkerState g_croft_orch_toy_worker;

#define CROFT_ORCH_TOY_MAILBOX_POLLS 500000u

static int croft_orch_toy_run_producer(CroftOrchToyWorkerState *state,
                                       const CroftOrchGuestWorkerStartup *startup)
{
    const CroftOrchGuestMailboxBinding *outbox;
    SapWitOrchestrationTxnResource txn = SAP_WIT_ORCHESTRATION_TXN_RESOURCE_INVALID;
    int32_t rc;

    outbox = croft_orch_guest_find_outbox(startup, "toy-data");
    if (!outbox || !croft_orch_guest_find_table(startup, "events") || startup->startup_bytes_len == 0u) {
        return ERR_NOT_FOUND;
    }
    rc = croft_orch_guest_store_begin(&state->runtime, startup->db_handle, 0u, &txn);
    if (rc == ERR_OK) {
        rc = croft_orch_guest_store_put(&state->runtime,
                                        txn,
                                        "events",
                                        (const uint8_t *)"producer-start",
                                        14u,
                                        startup->startup_bytes,
                                        startup->startup_bytes_len);
    }
    if (rc == ERR_OK) {
        rc = croft_orch_guest_store_commit(&state->runtime, txn);
    } else if (txn != SAP_WIT_ORCHESTRATION_TXN_RESOURCE_INVALID) {
        (void)croft_orch_guest_store_abort(&state->runtime, txn);
    }
    if (rc == ERR_OK) {
        rc = croft_orch_guest_mailbox_send(&state->runtime,
                                           outbox->handle,
                                           startup->startup_bytes,
                                           startup->startup_bytes_len);
    }
    return rc;
}

static int croft_orch_toy_run_consumer(CroftOrchToyWorkerState *state,
                                       const CroftOrchGuestWorkerStartup *startup)
{
    const CroftOrchGuestMailboxBinding *inbox;
    SapWitOrchestrationTxnResource txn = SAP_WIT_ORCHESTRATION_TXN_RESOURCE_INVALID;
    uint8_t *payload = NULL;
    uint32_t payload_len = 0u;
    int32_t rc;

    inbox = croft_orch_guest_find_inbox(startup, "toy-data");
    if (!inbox || !croft_orch_guest_find_table(startup, "events")) {
        return ERR_NOT_FOUND;
    }
    rc = croft_orch_guest_mailbox_recv_retry_alloc(&state->runtime,
                                                   inbox->handle,
                                                   CROFT_ORCH_TOY_MAILBOX_POLLS,
                                                   &payload,
                                                   &payload_len);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_orch_guest_store_begin(&state->runtime, startup->db_handle, 0u, &txn);
    if (rc == ERR_OK) {
        rc = croft_orch_guest_store_put(&state->runtime,
                                        txn,
                                        "events",
                                        (const uint8_t *)"consumer-seen",
                                        13u,
                                        payload,
                                        payload_len);
    }
    if (rc == ERR_OK) {
        rc = croft_orch_guest_store_commit(&state->runtime, txn);
    } else if (txn != SAP_WIT_ORCHESTRATION_TXN_RESOURCE_INVALID) {
        (void)croft_orch_guest_store_abort(&state->runtime, txn);
    }
    sap_wit_rt_free(payload);
    return rc;
}

static int32_t croft_orch_toy_worker_run(void *ctx,
                                         const SapWitOrchestrationRunWorker *payload,
                                         SapWitOrchestrationRunWorkerReply *reply_out)
{
    CroftOrchToyWorkerState *state = (CroftOrchToyWorkerState *)ctx;
    CroftOrchGuestWorkerStartup startup;
    char worker_name[64];
    int32_t rc;

    if (!state || !payload || !reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_orchestration_run_worker_reply(reply_out);
    reply_out->case_tag = SAP_WIT_ORCHESTRATION_RUN_WORKER_REPLY_STATUS;

    rc = croft_orch_guest_decode_worker_startup(payload->startup_bytes_data,
                                                payload->startup_bytes_len,
                                                &startup);
    if (rc == ERR_OK) {
        rc = croft_orch_guest_copy_text(worker_name,
                                        sizeof(worker_name),
                                        payload->worker_name_data,
                                        payload->worker_name_len);
    }
    if (rc == ERR_OK) {
        if (croft_orch_guest_text_equals(worker_name, "producer")) {
            rc = croft_orch_toy_run_producer(state, &startup);
        } else if (croft_orch_guest_text_equals(worker_name, "consumer")) {
            rc = croft_orch_toy_run_consumer(state, &startup);
        } else {
            rc = ERR_NOT_FOUND;
        }
    }

    if (rc == ERR_OK) {
        reply_out->val.status.is_v_ok = 1u;
    } else {
        static const uint8_t k_error[] = "toy-worker-failed";
        reply_out->val.status.is_v_ok = 0u;
        reply_out->val.status.v_val.err.v_data = k_error;
        reply_out->val.status.v_val.err.v_len = (uint32_t)(sizeof(k_error) - 1u);
        rc = ERR_OK;
    }
    return rc;
}

static int32_t croft_orch_toy_worker_init(void)
{
    static const SapWitOrchestrationRunWorkerDispatchOps k_run_ops = {
        .run_worker = croft_orch_toy_worker_run,
    };
    int32_t rc;

    if (g_croft_orch_toy_worker.initialized) {
        return ERR_OK;
    }
    rc = croft_orch_guest_runtime_init(&g_croft_orch_toy_worker.runtime);
    if (rc != ERR_OK) {
        return rc;
    }
    g_croft_orch_toy_worker.exports.run_worker_ctx = &g_croft_orch_toy_worker;
    g_croft_orch_toy_worker.exports.run_worker_ops = &k_run_ops;
    rc = sap_wit_guest_register_orchestration_worker_exports(&g_croft_orch_toy_worker.exports);
    if (rc != ERR_OK) {
        return rc;
    }
    g_croft_orch_toy_worker.initialized = 1u;
    return ERR_OK;
}

__attribute__((constructor))
static void croft_orch_toy_worker_ctor(void)
{
    (void)croft_orch_toy_worker_init();
}
