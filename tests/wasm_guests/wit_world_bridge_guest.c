#include "croft/wit_croft_wasm_guest.h"

#include "wit_wasi_cli_command.h"
#include "wit_wasi_random_world.h"

typedef struct {
    uint32_t initialized;
    SapWitCroftWasmGuestContext guest_ctx;
    SapWitGuestTransport transport;
    uint32_t last_argument_count;
    uint64_t last_random_u64;
} WitWorldBridgeGuestState;

static WitWorldBridgeGuestState g_wit_world_bridge_guest;

static int32_t wit_world_bridge_guest_ensure_init(void)
{
    if (g_wit_world_bridge_guest.initialized) {
        return ERR_OK;
    }

    sap_wit_croft_wasm_guest_context_init_default(&g_wit_world_bridge_guest.guest_ctx);
    sap_wit_croft_wasm_guest_transport_init(&g_wit_world_bridge_guest.transport,
                                            &g_wit_world_bridge_guest.guest_ctx);
    g_wit_world_bridge_guest.initialized = 1u;
    return ERR_OK;
}

__attribute__((export_name("wit_guest_fetch_arguments")))
int32_t wit_guest_fetch_arguments(void)
{
    SapWitCliEnvironmentCommand command = {0};
    SapWitCliEnvironmentReply reply;
    int32_t rc;

    rc = wit_world_bridge_guest_ensure_init();
    if (rc != ERR_OK) {
        return rc;
    }

    sap_wit_zero_cli_environment_reply(&reply);
    command.case_tag = SAP_WIT_CLI_ENVIRONMENT_COMMAND_GET_ARGUMENTS;
    rc = sap_wit_guest_cli_command_import_environment(&g_wit_world_bridge_guest.transport,
                                                      &command,
                                                      &reply);
    if (rc == ERR_OK) {
        if (reply.case_tag != SAP_WIT_CLI_ENVIRONMENT_REPLY_GET_ARGUMENTS) {
            rc = ERR_TYPE;
        } else {
            g_wit_world_bridge_guest.last_argument_count = reply.val.get_arguments.len;
        }
    }
    sap_wit_dispose_cli_environment_reply(&reply);
    return rc;
}

__attribute__((export_name("wit_guest_argument_count")))
int32_t wit_guest_argument_count(void)
{
    return (int32_t)g_wit_world_bridge_guest.last_argument_count;
}

__attribute__((export_name("wit_guest_fetch_random_u64")))
int32_t wit_guest_fetch_random_u64(void)
{
    SapWitRandomCommand command = {0};
    SapWitRandomReply reply;
    int32_t rc;

    rc = wit_world_bridge_guest_ensure_init();
    if (rc != ERR_OK) {
        return rc;
    }

    sap_wit_zero_random_reply(&reply);
    command.case_tag = SAP_WIT_RANDOM_COMMAND_GET_RANDOM_U64;
    rc = sap_wit_guest_random_imports_import_random(&g_wit_world_bridge_guest.transport,
                                                    &command,
                                                    &reply);
    if (rc == ERR_OK) {
        if (reply.case_tag != SAP_WIT_RANDOM_REPLY_GET_RANDOM_U64) {
            rc = ERR_TYPE;
        } else {
            g_wit_world_bridge_guest.last_random_u64 = reply.val.get_random_u64;
        }
    }
    sap_wit_dispose_random_reply(&reply);
    return rc;
}

__attribute__((export_name("wit_guest_random_low32")))
int32_t wit_guest_random_low32(void)
{
    return (int32_t)(uint32_t)(g_wit_world_bridge_guest.last_random_u64 & 0xFFFFFFFFu);
}

__attribute__((export_name("wit_guest_random_high32")))
int32_t wit_guest_random_high32(void)
{
    return (int32_t)(uint32_t)(g_wit_world_bridge_guest.last_random_u64 >> 32);
}

__attribute__((export_name("wit_guest_handle_count")))
int32_t wit_guest_handle_count(void)
{
    return (int32_t)g_wit_world_bridge_guest.guest_ctx.handle_count;
}
