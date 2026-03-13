#include "orchestration_guest_runtime.h"

typedef struct {
    uint32_t initialized;
    CroftOrchGuestRuntime runtime;
    SapWitOrchestrationWorkerWorldExports exports;
} CroftOrchFailWorkerState;

static CroftOrchFailWorkerState g_croft_orch_fail_worker;

static int32_t croft_orch_fail_worker_run(void *ctx,
                                          const SapWitOrchestrationRunWorker *payload,
                                          SapWitOrchestrationRunWorkerReply *reply_out)
{
    CroftOrchFailWorkerState *state = (CroftOrchFailWorkerState *)ctx;
    CroftOrchGuestWorkerStartup startup;
    char worker_name[64];
    int32_t rc;

    (void)state;
    if (!payload || !reply_out) {
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
    if (rc == ERR_OK
            && croft_orch_guest_text_equals(worker_name, "failer")
            && startup.startup_bytes_len > 0u) {
        static const uint8_t k_error[] = "failing-worker-triggered";
        reply_out->val.status.is_v_ok = 0u;
        reply_out->val.status.v_val.err.v_data = k_error;
        reply_out->val.status.v_val.err.v_len = (uint32_t)(sizeof(k_error) - 1u);
        return ERR_OK;
    }

    reply_out->val.status.is_v_ok = 0u;
    reply_out->val.status.v_val.err.v_data = (const uint8_t *)"failing-worker-invalid";
    reply_out->val.status.v_val.err.v_len = 22u;
    return ERR_OK;
}

static int32_t croft_orch_fail_worker_init(void)
{
    static const SapWitOrchestrationRunWorkerDispatchOps k_run_ops = {
        .run_worker = croft_orch_fail_worker_run,
    };
    int32_t rc;

    if (g_croft_orch_fail_worker.initialized) {
        return ERR_OK;
    }
    rc = croft_orch_guest_runtime_init(&g_croft_orch_fail_worker.runtime);
    if (rc != ERR_OK) {
        return rc;
    }
    g_croft_orch_fail_worker.exports.run_worker_ctx = &g_croft_orch_fail_worker;
    g_croft_orch_fail_worker.exports.run_worker_ops = &k_run_ops;
    rc = sap_wit_guest_register_orchestration_worker_exports(&g_croft_orch_fail_worker.exports);
    if (rc != ERR_OK) {
        return rc;
    }
    g_croft_orch_fail_worker.initialized = 1u;
    return ERR_OK;
}

__attribute__((constructor))
static void croft_orch_fail_worker_ctor(void)
{
    (void)croft_orch_fail_worker_init();
}
