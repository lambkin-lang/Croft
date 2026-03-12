#include "orchestration_guest_runtime.h"

#ifndef CROFT_ORCH_JSON_WORKER_WASM_PATH
#error "CROFT_ORCH_JSON_WORKER_WASM_PATH must be defined"
#endif

#ifndef CROFT_ORCH_JSON_FAMILY
#define CROFT_ORCH_JSON_FAMILY "croft_json_tree_text_view_family_current_machine"
#endif

#ifndef CROFT_ORCH_JSON_APPLICABILITY
#define CROFT_ORCH_JSON_APPLICABILITY "current-machine-windowed"
#endif

typedef struct {
    uint32_t initialized;
    CroftOrchGuestRuntime runtime;
    SapWitOrchestrationBootstrapWorldExports exports;
} CroftOrchJsonBootstrapState;

static CroftOrchJsonBootstrapState g_croft_orch_json_bootstrap;

static int32_t croft_orch_json_bootstrap_run(void *ctx, SapWitOrchestrationRunReply *reply_out)
{
    static const CroftOrchGuestTableDecl k_tables[] = {
        {"views", "utf8", "utf8", SAP_WIT_ORCHESTRATION_TABLE_ACCESS_READ | SAP_WIT_ORCHESTRATION_TABLE_ACCESS_WRITE},
        {"cursors", "utf8", "utf8", SAP_WIT_ORCHESTRATION_TABLE_ACCESS_READ | SAP_WIT_ORCHESTRATION_TABLE_ACCESS_WRITE},
    };
    static const char *k_parser_tables[] = {"views", "cursors"};
    static const uint8_t k_json_payload[] =
        "{"
        "\"project\":\"Croft\","
        "\"features\":{\"solver\":true,\"thatch\":\"json\"},"
        "\"items\":[\"alpha\",{\"nested\":true},3]"
        "}";
    static const CroftOrchGuestWorkerDecl k_parser = {
        "json-parser",
        "json-workers",
        1u,
        k_parser_tables,
        2u,
        NULL,
        0u,
        NULL,
        0u,
        "application/json",
        k_json_payload,
        (uint32_t)(sizeof(k_json_payload) - 1u),
    };
    CroftOrchJsonBootstrapState *state = (CroftOrchJsonBootstrapState *)ctx;
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
                                         "json-thatch-orchestration",
                                         CROFT_ORCH_JSON_FAMILY,
                                         CROFT_ORCH_JSON_APPLICABILITY,
                                         &builder);
    if (rc == ERR_OK) {
        rc = croft_orch_guest_builder_prefer_slot(&state->runtime,
                                                  builder,
                                                  "croft-editor-shell-slot-current-machine",
                                                  "croft-editor-appkit-current-machine");
    }
    if (rc == ERR_OK) {
        rc = croft_orch_guest_builder_add_module(&state->runtime,
                                                 builder,
                                                 "json-workers",
                                                 CROFT_ORCH_JSON_WORKER_WASM_PATH);
    }
    if (rc == ERR_OK) {
        rc = croft_orch_guest_builder_set_db_schema(&state->runtime,
                                                    builder,
                                                    "json-db",
                                                    k_tables,
                                                    2u);
    }
    if (rc == ERR_OK) {
        rc = croft_orch_guest_builder_add_worker(&state->runtime, builder, &k_parser);
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
        static const uint8_t k_error[] = "json-bootstrap-failed";
        reply_out->val.status.is_v_ok = 0u;
        reply_out->val.status.v_val.err.v_data = k_error;
        reply_out->val.status.v_val.err.v_len = (uint32_t)(sizeof(k_error) - 1u);
        rc = ERR_OK;
    }
    return rc;
}

static int32_t croft_orch_json_bootstrap_init(void)
{
    static const SapWitOrchestrationRunDispatchOps k_run_ops = {
        .run = croft_orch_json_bootstrap_run,
    };
    int32_t rc;

    if (g_croft_orch_json_bootstrap.initialized) {
        return ERR_OK;
    }
    rc = croft_orch_guest_runtime_init(&g_croft_orch_json_bootstrap.runtime);
    if (rc != ERR_OK) {
        return rc;
    }
    g_croft_orch_json_bootstrap.exports.run_ctx = &g_croft_orch_json_bootstrap;
    g_croft_orch_json_bootstrap.exports.run_ops = &k_run_ops;
    rc = sap_wit_guest_register_orchestration_bootstrap_exports(
        &g_croft_orch_json_bootstrap.exports);
    if (rc != ERR_OK) {
        return rc;
    }
    g_croft_orch_json_bootstrap.initialized = 1u;
    return ERR_OK;
}

__attribute__((constructor))
static void croft_orch_json_bootstrap_ctor(void)
{
    (void)croft_orch_json_bootstrap_init();
}
