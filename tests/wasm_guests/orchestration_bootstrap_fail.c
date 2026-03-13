#include "orchestration_guest_runtime.h"

#ifndef CROFT_ORCH_FAIL_WORKER_WASM_PATH
#error "CROFT_ORCH_FAIL_WORKER_WASM_PATH must be defined"
#endif

#ifndef CROFT_ORCH_FAIL_FAMILY
#define CROFT_ORCH_FAIL_FAMILY "croft_orchestration_mailbox_demo_family_current_machine"
#endif

#ifndef CROFT_ORCH_FAIL_APPLICABILITY
#define CROFT_ORCH_FAIL_APPLICABILITY "current-machine"
#endif

typedef struct {
    uint32_t initialized;
    CroftOrchGuestRuntime runtime;
    SapWitOrchestrationBootstrapWorldExports exports;
} CroftOrchFailBootstrapState;

static CroftOrchFailBootstrapState g_croft_orch_fail_bootstrap;

static int32_t croft_orch_fail_bootstrap_run(void *ctx, SapWitOrchestrationRunReply *reply_out)
{
    static const uint8_t k_fail_startup[] = "deliberate-failure";
    static const CroftOrchGuestWorkerDecl k_fail_worker = {
        "failer",
        "failing-workers",
        1u,
        NULL,
        0u,
        NULL,
        0u,
        NULL,
        0u,
        "text/plain",
        k_fail_startup,
        (uint32_t)(sizeof(k_fail_startup) - 1u),
    };
    CroftOrchFailBootstrapState *state = (CroftOrchFailBootstrapState *)ctx;
    SapWitOrchestrationBuilderResource builder = SAP_WIT_ORCHESTRATION_BUILDER_RESOURCE_INVALID;
    SapWitOrchestrationSessionResource session = SAP_WIT_ORCHESTRATION_SESSION_RESOURCE_INVALID;
    int32_t rc;

    if (!state || !reply_out) {
        return ERR_INVALID;
    }

    sap_wit_zero_orchestration_run_reply(reply_out);
    reply_out->case_tag = SAP_WIT_ORCHESTRATION_RUN_REPLY_STATUS;

    rc = croft_orch_guest_builder_create(&state->runtime,
                                         "failing-worker-orchestration",
                                         CROFT_ORCH_FAIL_FAMILY,
                                         CROFT_ORCH_FAIL_APPLICABILITY,
                                         &builder);
    if (rc == ERR_OK) {
        rc = croft_orch_guest_builder_add_module(&state->runtime,
                                                 builder,
                                                 "failing-workers",
                                                 CROFT_ORCH_FAIL_WORKER_WASM_PATH);
    }
    if (rc == ERR_OK) {
        rc = croft_orch_guest_builder_add_worker(&state->runtime, builder, &k_fail_worker);
    }
    if (rc == ERR_OK) {
        rc = croft_orch_guest_builder_resolve(&state->runtime, builder);
    }
    if (rc == ERR_OK) {
        rc = croft_orch_guest_builder_launch(&state->runtime, builder, &session);
    }

    if (rc == ERR_OK && session != SAP_WIT_ORCHESTRATION_SESSION_RESOURCE_INVALID) {
        reply_out->val.status.is_v_ok = 1u;
    } else {
        static const uint8_t k_error[] = "failing-bootstrap-failed";
        reply_out->val.status.is_v_ok = 0u;
        reply_out->val.status.v_val.err.v_data = k_error;
        reply_out->val.status.v_val.err.v_len = (uint32_t)(sizeof(k_error) - 1u);
        rc = ERR_OK;
    }
    return rc;
}

static int32_t croft_orch_fail_bootstrap_init(void)
{
    static const SapWitOrchestrationRunDispatchOps k_run_ops = {
        .run = croft_orch_fail_bootstrap_run,
    };
    int32_t rc;

    if (g_croft_orch_fail_bootstrap.initialized) {
        return ERR_OK;
    }
    rc = croft_orch_guest_runtime_init(&g_croft_orch_fail_bootstrap.runtime);
    if (rc != ERR_OK) {
        return rc;
    }
    g_croft_orch_fail_bootstrap.exports.run_ctx = &g_croft_orch_fail_bootstrap;
    g_croft_orch_fail_bootstrap.exports.run_ops = &k_run_ops;
    rc = sap_wit_guest_register_orchestration_bootstrap_exports(
        &g_croft_orch_fail_bootstrap.exports);
    if (rc != ERR_OK) {
        return rc;
    }
    g_croft_orch_fail_bootstrap.initialized = 1u;
    return ERR_OK;
}

__attribute__((constructor))
static void croft_orch_fail_bootstrap_ctor(void)
{
    (void)croft_orch_fail_bootstrap_init();
}
