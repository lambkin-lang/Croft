#include "croft/wit_croft_wasm_guest.h"
#include "generated/wit_wasi_cli_command.h"
#include "generated/wit_wasi_random_world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #expr, __FILE__, __LINE__); \
            return 1; \
        } \
    } while (0)

typedef struct {
    const SapWitGuestEndpointSet *sets;
    uint32_t set_count;
    const SapWitWorldEndpointDescriptor *resolved_endpoints[8];
    const void *resolved_bindings[8];
    uint32_t resolved_count;
    uint32_t find_calls;
    uint32_t call_calls;
} FakeCroftWasmHost;

static FakeCroftWasmHost *g_fake_host = NULL;

static int32_t random_get_random_u64(void *ctx, SapWitRandomReply *reply_out)
{
    uint32_t *calls = (uint32_t *)ctx;

    if (calls) {
        (*calls)++;
    }
    sap_wit_zero_random_reply(reply_out);
    reply_out->case_tag = SAP_WIT_RANDOM_REPLY_GET_RANDOM_U64;
    reply_out->val.get_random_u64 = 77u;
    return 0;
}

static int32_t cli_get_arguments(void *ctx, SapWitCliEnvironmentReply *reply_out)
{
    uint32_t *calls = (uint32_t *)ctx;

    if (calls) {
        (*calls)++;
    }
    sap_wit_zero_cli_environment_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLI_ENVIRONMENT_REPLY_GET_ARGUMENTS;
    reply_out->val.get_arguments.data = (const uint8_t *)"";
    reply_out->val.get_arguments.len = 0u;
    reply_out->val.get_arguments.byte_len = 0u;
    return 0;
}

static int fake_host_copy_name(const uint8_t *name_ptr,
                               uint32_t name_len,
                               char *stack_name,
                               size_t stack_size,
                               char **name_out)
{
    char *heap_name = NULL;

    if (!name_ptr || !name_out || name_len == 0u) {
        return ERR_INVALID;
    }

    if ((size_t)name_len < stack_size) {
        memcpy(stack_name, name_ptr, name_len);
        stack_name[name_len] = '\0';
        *name_out = stack_name;
        return ERR_OK;
    }

    heap_name = (char *)malloc((size_t)name_len + 1u);
    if (!heap_name) {
        return ERR_OOM;
    }
    memcpy(heap_name, name_ptr, name_len);
    heap_name[name_len] = '\0';
    *name_out = heap_name;
    return ERR_OK;
}

static int32_t fake_croft_wit_find_endpoint(const uint8_t *name_ptr, uint32_t name_len)
{
    char stack_name[256];
    char *qualified_name = NULL;
    int rc;
    uint32_t i;

    if (!g_fake_host) {
        return -ERR_INVALID;
    }

    rc = fake_host_copy_name(name_ptr,
                             name_len,
                             stack_name,
                             sizeof(stack_name),
                             &qualified_name);
    if (rc != ERR_OK) {
        return -rc;
    }

    g_fake_host->find_calls++;
    for (i = 0u; i < g_fake_host->set_count; i++) {
        const SapWitGuestEndpointSet *set = &g_fake_host->sets[i];
        const SapWitWorldEndpointDescriptor *endpoint;
        uint32_t handle_index;

        endpoint = sap_wit_find_world_endpoint_descriptor_qualified(set->endpoints,
                                                                    set->endpoint_count,
                                                                    qualified_name);
        if (!endpoint) {
            continue;
        }

        for (handle_index = 0u; handle_index < g_fake_host->resolved_count; handle_index++) {
            if (g_fake_host->resolved_endpoints[handle_index] == endpoint
                    && g_fake_host->resolved_bindings[handle_index] == set->bindings) {
                if (qualified_name != stack_name) {
                    free(qualified_name);
                }
                return (int32_t)handle_index + 1;
            }
        }

        if (g_fake_host->resolved_count >= 8u) {
            if (qualified_name != stack_name) {
                free(qualified_name);
            }
            return -ERR_FULL;
        }

        g_fake_host->resolved_endpoints[g_fake_host->resolved_count] = endpoint;
        g_fake_host->resolved_bindings[g_fake_host->resolved_count] = set->bindings;
        g_fake_host->resolved_count++;
        if (qualified_name != stack_name) {
            free(qualified_name);
        }
        return (int32_t)g_fake_host->resolved_count;
    }

    if (qualified_name != stack_name) {
        free(qualified_name);
    }
    return -ERR_NOT_FOUND;
}

static int32_t fake_croft_wit_call_endpoint(int32_t endpoint_handle,
                                            const uint8_t *command_ptr,
                                            uint32_t command_len,
                                            uint8_t *reply_ptr,
                                            uint32_t reply_cap,
                                            uint32_t *reply_len_out)
{
    uint32_t index;

    if (!g_fake_host || endpoint_handle <= 0 || !reply_len_out) {
        return ERR_INVALID;
    }

    index = (uint32_t)(endpoint_handle - 1);
    if (index >= g_fake_host->resolved_count) {
        return ERR_NOT_FOUND;
    }

    g_fake_host->call_calls++;
    return sap_wit_world_endpoint_invoke_bytes(g_fake_host->resolved_endpoints[index],
                                               g_fake_host->resolved_bindings[index],
                                               command_ptr,
                                               command_len,
                                               reply_ptr,
                                               reply_cap,
                                               reply_len_out);
}

int main(void)
{
    const SapWitWorldEndpointDescriptor *random_endpoint = NULL;
    const SapWitWorldEndpointDescriptor *cli_endpoint = NULL;
    SapWitRandomImportsWorldImports random_imports = {0};
    SapWitCliCommandWorldImports cli_imports = {0};
    SapWitRandomDispatchOps random_ops = {
        .get_random_u64 = random_get_random_u64,
    };
    SapWitCliEnvironmentDispatchOps cli_ops = {
        .get_arguments = cli_get_arguments,
    };
    SapWitGuestEndpointSet guest_sets[2] = {0};
    FakeCroftWasmHost fake_host = {0};
    SapWitCroftWasmGuestImports imports = {0};
    SapWitCroftWasmGuestContext ctx = {0};
    SapWitGuestTransport transport = {0};
    SapWitRandomCommand random_command = {0};
    SapWitCliEnvironmentCommand cli_command = {0};
    SapWitRandomReply random_reply = {0};
    SapWitRandomReply random_reply_cached = {0};
    SapWitCliEnvironmentReply cli_reply = {0};
    uint32_t random_calls = 0u;
    uint32_t cli_calls = 0u;

    random_endpoint = sap_wit_find_world_endpoint_descriptor(sap_wit_random_imports_import_endpoints,
                                                             sap_wit_random_imports_import_endpoints_count,
                                                             "random");
    cli_endpoint = sap_wit_find_world_endpoint_descriptor(sap_wit_cli_command_import_endpoints,
                                                          sap_wit_cli_command_import_endpoints_count,
                                                          "environment");
    CHECK(random_endpoint != NULL);
    CHECK(cli_endpoint != NULL);

    CHECK(sap_wit_world_endpoint_bind(&random_imports,
                                      random_endpoint,
                                      &random_calls,
                                      &random_ops)
          == 0);
    CHECK(sap_wit_world_endpoint_bind(&cli_imports,
                                      cli_endpoint,
                                      &cli_calls,
                                      &cli_ops)
          == 0);

    guest_sets[0].endpoints = sap_wit_random_imports_import_endpoints;
    guest_sets[0].endpoint_count = sap_wit_random_imports_import_endpoints_count;
    guest_sets[0].bindings = &random_imports;
    guest_sets[1].endpoints = sap_wit_cli_command_import_endpoints;
    guest_sets[1].endpoint_count = sap_wit_cli_command_import_endpoints_count;
    guest_sets[1].bindings = &cli_imports;

    fake_host.sets = guest_sets;
    fake_host.set_count = 2u;
    g_fake_host = &fake_host;

    imports.find_endpoint = fake_croft_wit_find_endpoint;
    imports.call_endpoint = fake_croft_wit_call_endpoint;
    sap_wit_croft_wasm_guest_context_init(&ctx, imports);
    sap_wit_croft_wasm_guest_transport_init(&transport, &ctx);

    random_command.case_tag = SAP_WIT_RANDOM_COMMAND_GET_RANDOM_U64;
    cli_command.case_tag = SAP_WIT_CLI_ENVIRONMENT_COMMAND_GET_ARGUMENTS;

    CHECK(sap_wit_guest_random_imports_import_random(&transport,
                                                     &random_command,
                                                     &random_reply)
          == 0);
    CHECK(sap_wit_guest_random_imports_import_random(&transport,
                                                     &random_command,
                                                     &random_reply_cached)
          == 0);
    CHECK(sap_wit_guest_cli_command_import_environment(&transport, &cli_command, &cli_reply) == 0);

    CHECK(random_reply.case_tag == SAP_WIT_RANDOM_REPLY_GET_RANDOM_U64);
    CHECK(random_reply.val.get_random_u64 == 77u);
    CHECK(random_reply_cached.case_tag == SAP_WIT_RANDOM_REPLY_GET_RANDOM_U64);
    CHECK(random_reply_cached.val.get_random_u64 == 77u);
    CHECK(cli_reply.case_tag == SAP_WIT_CLI_ENVIRONMENT_REPLY_GET_ARGUMENTS);
    CHECK(random_calls == 2u);
    CHECK(cli_calls == 1u);
    CHECK(fake_host.find_calls == 2u);
    CHECK(fake_host.call_calls == 3u);
    CHECK(ctx.handle_count == 2u);

    sap_wit_guest_transport_dispose(&transport);
    sap_wit_croft_wasm_guest_context_dispose(&ctx);
    g_fake_host = NULL;
    return 0;
}
