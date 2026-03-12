#include "orchestration_guest_runtime.h"

#ifndef CROFT_ORCH_TOY_WORKER_WASM_PATH
#error "CROFT_ORCH_TOY_WORKER_WASM_PATH must be defined"
#endif

#ifndef CROFT_ORCH_TOY_FAMILY
#define CROFT_ORCH_TOY_FAMILY "croft_orchestration_mailbox_demo_family_current_machine"
#endif

#ifndef CROFT_ORCH_TOY_APPLICABILITY
#define CROFT_ORCH_TOY_APPLICABILITY "current-machine"
#endif

typedef struct {
    uint32_t initialized;
    CroftOrchGuestRuntime runtime;
    SapWitOrchestrationBootstrapWorldExports exports;
} CroftOrchToyBootstrapState;

static CroftOrchToyBootstrapState g_croft_orch_toy_bootstrap;

static int32_t croft_orch_toy_bootstrap_run(void *ctx, SapWitOrchestrationRunReply *reply_out)
{
    static const CroftOrchGuestTableDecl k_tables[] = {
        {"events", "utf8", "utf8", SAP_WIT_ORCHESTRATION_TABLE_ACCESS_READ | SAP_WIT_ORCHESTRATION_TABLE_ACCESS_WRITE},
    };
    static const char *k_data_producers[] = {"producer"};
    static const char *k_data_consumers[] = {"consumer"};
    static const CroftOrchGuestMailboxDecl k_mailboxes[] = {
        {"toy-data", "toy/message", k_data_producers, 1u, k_data_consumers, 1u, SAP_WIT_ORCHESTRATION_MAILBOX_DURABILITY_VOLATILE},
    };
    static const char *k_producer_tables[] = {"events"};
    static const char *k_producer_outboxes[] = {"toy-data"};
    static const uint8_t k_producer_startup[] = "hello-from-bootstrap";
    static const CroftOrchGuestWorkerDecl k_producer = {
        "producer",
        "toy-workers",
        1u,
        k_producer_tables,
        1u,
        NULL,
        0u,
        k_producer_outboxes,
        1u,
        "toy/startup",
        k_producer_startup,
        (uint32_t)(sizeof(k_producer_startup) - 1u),
    };
    static const char *k_consumer_tables[] = {"events"};
    static const char *k_consumer_inboxes[] = {"toy-data"};
    static const CroftOrchGuestWorkerDecl k_consumer = {
        "consumer",
        "toy-workers",
        1u,
        k_consumer_tables,
        1u,
        k_consumer_inboxes,
        1u,
        NULL,
        0u,
        NULL,
        NULL,
        0u,
    };
    CroftOrchToyBootstrapState *state = (CroftOrchToyBootstrapState *)ctx;
    SapWitOrchestrationBuilderResource builder = SAP_WIT_ORCHESTRATION_BUILDER_RESOURCE_INVALID;
    SapWitOrchestrationSessionResource session = SAP_WIT_ORCHESTRATION_SESSION_RESOURCE_INVALID;
    int32_t rc;
    uint32_t i;

    if (!state || !reply_out) {
        return ERR_INVALID;
    }

    sap_wit_zero_orchestration_run_reply(reply_out);
    reply_out->case_tag = SAP_WIT_ORCHESTRATION_RUN_REPLY_STATUS;

    rc = croft_orch_guest_builder_create(&state->runtime,
                                         "toy-mailbox-topology",
                                         CROFT_ORCH_TOY_FAMILY,
                                         CROFT_ORCH_TOY_APPLICABILITY,
                                         &builder);
    if (rc == ERR_OK) {
        rc = croft_orch_guest_builder_add_module(&state->runtime,
                                                 builder,
                                                 "toy-workers",
                                                 CROFT_ORCH_TOY_WORKER_WASM_PATH);
    }
    if (rc == ERR_OK) {
        rc = croft_orch_guest_builder_set_db_schema(&state->runtime,
                                                    builder,
                                                    "toy-db",
                                                    k_tables,
                                                    1u);
    }
    for (i = 0u; rc == ERR_OK && i < 1u; i++) {
        rc = croft_orch_guest_builder_add_mailbox(&state->runtime, builder, &k_mailboxes[i]);
    }
    if (rc == ERR_OK) {
        rc = croft_orch_guest_builder_add_worker(&state->runtime, builder, &k_producer);
    }
    if (rc == ERR_OK) {
        rc = croft_orch_guest_builder_add_worker(&state->runtime, builder, &k_consumer);
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
        static const uint8_t k_error[] = "toy-bootstrap-failed";
        reply_out->val.status.is_v_ok = 0u;
        reply_out->val.status.v_val.err.v_data = k_error;
        reply_out->val.status.v_val.err.v_len = (uint32_t)(sizeof(k_error) - 1u);
        rc = ERR_OK;
    }
    return rc;
}

static int32_t croft_orch_toy_bootstrap_init(void)
{
    static const SapWitOrchestrationRunDispatchOps k_run_ops = {
        .run = croft_orch_toy_bootstrap_run,
    };
    int32_t rc;

    if (g_croft_orch_toy_bootstrap.initialized) {
        return ERR_OK;
    }
    rc = croft_orch_guest_runtime_init(&g_croft_orch_toy_bootstrap.runtime);
    if (rc != ERR_OK) {
        return rc;
    }
    g_croft_orch_toy_bootstrap.exports.run_ctx = &g_croft_orch_toy_bootstrap;
    g_croft_orch_toy_bootstrap.exports.run_ops = &k_run_ops;
    rc = sap_wit_guest_register_orchestration_bootstrap_exports(
        &g_croft_orch_toy_bootstrap.exports);
    if (rc != ERR_OK) {
        return rc;
    }
    g_croft_orch_toy_bootstrap.initialized = 1u;
    return ERR_OK;
}

__attribute__((constructor))
static void croft_orch_toy_bootstrap_ctor(void)
{
    (void)croft_orch_toy_bootstrap_init();
}
