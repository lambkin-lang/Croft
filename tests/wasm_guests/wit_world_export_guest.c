#include "croft/wit_croft_wasm_guest.h"

#include "world_inline_command.h"

typedef struct {
    uint32_t initialized;
    uint32_t environment_calls;
    SapWitCroftWasmGuestContext guest_ctx;
    SapWitGuestTransport transport;
    SapWitInlineWorldCommandWorldExports exports;
} WitWorldExportGuestState;

static WitWorldExportGuestState g_wit_world_export_guest;

static int32_t wit_world_export_guest_run(void *ctx, SapWitInlineWorldRunReply *reply_out)
{
    WitWorldExportGuestState *state = (WitWorldExportGuestState *)ctx;
    SapWitInlineWorldEnvironmentCommand command = {0};
    SapWitInlineWorldEnvironmentReply reply;
    int32_t rc;

    if (!state || !reply_out) {
        return ERR_INVALID;
    }

    sap_wit_zero_inline_world_run_reply(reply_out);
    sap_wit_zero_inline_world_environment_reply(&reply);

    command.case_tag = SAP_WIT_INLINE_WORLD_ENVIRONMENT_COMMAND_GET_ENVIRONMENT;
    rc = sap_wit_guest_inline_world_command_import_environment(&state->transport,
                                                               &command,
                                                               &reply);
    if (rc == ERR_OK && reply.case_tag != SAP_WIT_INLINE_WORLD_ENVIRONMENT_REPLY_GET_ENVIRONMENT) {
        rc = ERR_TYPE;
    }
    if (rc == ERR_OK) {
        state->environment_calls++;
    }

    reply_out->case_tag = SAP_WIT_INLINE_WORLD_RUN_REPLY_STATUS;
    if (rc == ERR_OK) {
        reply_out->val.status.is_v_ok = 1u;
    } else {
        static const uint8_t k_error[] = "env-import-failed";

        reply_out->val.status.is_v_ok = 0u;
        reply_out->val.status.v_val.err.v_data = k_error;
        reply_out->val.status.v_val.err.v_len = (uint32_t)(sizeof(k_error) - 1u);
        rc = ERR_OK;
    }

    sap_wit_dispose_inline_world_environment_reply(&reply);
    return rc;
}

static int32_t wit_world_export_guest_status_current(void *ctx,
                                                     SapWitInlineWorldStatusCheckReply *reply_out)
{
    WitWorldExportGuestState *state = (WitWorldExportGuestState *)ctx;

    if (!state || !reply_out) {
        return ERR_INVALID;
    }

    sap_wit_zero_inline_world_status_check_reply(reply_out);
    reply_out->case_tag = SAP_WIT_INLINE_WORLD_STATUS_CHECK_REPLY_STATUS;
    if (state->environment_calls > 0u) {
        reply_out->val.status.is_v_ok = 1u;
    } else {
        static const uint8_t k_error[] = "run-not-called";

        reply_out->val.status.is_v_ok = 0u;
        reply_out->val.status.v_val.err.v_data = k_error;
        reply_out->val.status.v_val.err.v_len = (uint32_t)(sizeof(k_error) - 1u);
    }
    return ERR_OK;
}

static int32_t wit_world_export_guest_ensure_init(void)
{
    static const SapWitInlineWorldRunDispatchOps k_run_ops = {
        .run = wit_world_export_guest_run,
    };
    static const SapWitInlineWorldStatusCheckDispatchOps k_status_ops = {
        .current = wit_world_export_guest_status_current,
    };
    int32_t rc;

    if (g_wit_world_export_guest.initialized) {
        return ERR_OK;
    }

    sap_wit_croft_wasm_guest_context_init_default(&g_wit_world_export_guest.guest_ctx);
    sap_wit_croft_wasm_guest_transport_init(&g_wit_world_export_guest.transport,
                                            &g_wit_world_export_guest.guest_ctx);

    g_wit_world_export_guest.exports.run_ctx = &g_wit_world_export_guest;
    g_wit_world_export_guest.exports.run_ops = &k_run_ops;
    g_wit_world_export_guest.exports.status_check_ctx = &g_wit_world_export_guest;
    g_wit_world_export_guest.exports.status_check_ops = &k_status_ops;

    rc = sap_wit_guest_register_inline_world_command_exports(&g_wit_world_export_guest.exports);
    if (rc != ERR_OK) {
        return rc;
    }

    g_wit_world_export_guest.initialized = 1u;
    return ERR_OK;
}

__attribute__((constructor))
static void wit_world_export_guest_ctor(void)
{
    (void)wit_world_export_guest_ensure_init();
}
