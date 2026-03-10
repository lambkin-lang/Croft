#include "croft/wit_world_runtime.h"
#include "generated/wit_wasi_cli_command.h"
#include "generated/wit_wasi_clocks_world.h"
#include "generated/wit_wasi_filesystem_world.h"
#include "generated/wit_wasi_io_world.h"
#include "generated/wit_wasi_random_world.h"

#include <stdio.h>
#include <string.h>

static int expect_u32(const char *label, uint32_t actual, uint32_t expected)
{
    if (actual == expected) {
        return 1;
    }
    fprintf(stderr, "%s: expected %u, got %u\n", label, expected, actual);
    return 0;
}

static int expect_ptr(const char *label, const void *actual, const void *expected)
{
    if (actual == expected) {
        return 1;
    }
    fprintf(stderr, "%s: expected %p, got %p\n", label, expected, actual);
    return 0;
}

static int expect_true(const char *label, int actual)
{
    if (actual) {
        return 1;
    }
    fprintf(stderr, "%s: expected true\n", label);
    return 0;
}

static int expect_str(const char *label, const char *actual, const char *expected)
{
    if (actual && expected && strcmp(actual, expected) == 0) {
        return 1;
    }
    fprintf(stderr,
            "%s: expected '%s', got '%s'\n",
            label,
            expected ? expected : "<null>",
            actual ? actual : "<null>");
    return 0;
}

static void init_writable_region(ThatchRegion *region, uint8_t *data, uint32_t cap)
{
    memset(region, 0, sizeof(*region));
    region->page_ptr = data;
    region->capacity = cap;
    region->head = 0u;
    region->sealed = 0;
}

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

static int32_t monotonic_now(void *ctx, SapWitClocksMonotonicClockReply *reply_out)
{
    uint32_t *calls = (uint32_t *)ctx;

    if (calls) {
        (*calls)++;
    }
    sap_wit_zero_clocks_monotonic_clock_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLOCKS_MONOTONIC_CLOCK_REPLY_NOW;
    reply_out->val.now = 123u;
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

static int32_t cli_run(void *ctx, SapWitCliRunReply *reply_out)
{
    uint32_t *calls = (uint32_t *)ctx;

    if (calls) {
        (*calls)++;
    }
    sap_wit_zero_cli_run_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLI_RUN_REPLY_STATUS;
    reply_out->val.status.is_v_ok = 1u;
    return 0;
}

int main(void)
{
    const SapWitWorldDescriptor *random_world = NULL;
    const SapWitWorldBindingDescriptor *random_binding = NULL;
    const SapWitWorldEndpointDescriptor *random_endpoint = NULL;
    const SapWitWorldDescriptor *clocks_world = NULL;
    const SapWitWorldBindingDescriptor *clocks_binding = NULL;
    const SapWitWorldEndpointDescriptor *clocks_endpoint = NULL;
    const SapWitWorldDescriptor *io_world = NULL;
    const SapWitWorldBindingDescriptor *io_streams_binding = NULL;
    const SapWitWorldBindingDescriptor *io_poll_binding = NULL;
    const SapWitWorldEndpointDescriptor *io_streams_endpoint = NULL;
    const SapWitWorldEndpointDescriptor *io_poll_endpoint = NULL;
    const SapWitWorldDescriptor *filesystem_world = NULL;
    const SapWitWorldBindingDescriptor *filesystem_types_binding = NULL;
    const SapWitWorldBindingDescriptor *filesystem_preopens_binding = NULL;
    const SapWitWorldEndpointDescriptor *filesystem_types_endpoint = NULL;
    const SapWitWorldEndpointDescriptor *filesystem_preopens_endpoint = NULL;
    const SapWitWorldDescriptor *cli_command_world = NULL;
    const SapWitWorldDescriptor *cli_imports_world = NULL;
    const SapWitWorldBindingDescriptor *cli_import_binding = NULL;
    const SapWitWorldBindingDescriptor *cli_export_binding = NULL;
    const SapWitWorldEndpointDescriptor *cli_environment_endpoint = NULL;
    const SapWitWorldEndpointDescriptor *cli_run_endpoint = NULL;
    SapWitRandomImportsWorldImports random_imports = {0};
    SapWitClocksImportsWorldImports clocks_imports = {0};
    SapWitFilesystemImportsWorldImports filesystem_imports = {0};
    SapWitIoImportsWorldImports io_imports = {0};
    SapWitCliCommandWorldImports cli_imports = {0};
    SapWitCliCommandWorldExports cli_exports = {0};
    SapWitRandomDispatchOps random_ops = {
        .get_random_u64 = random_get_random_u64,
    };
    SapWitClocksMonotonicClockDispatchOps monotonic_ops = {
        .now = monotonic_now,
    };
    SapWitIoStreamsDispatchOps io_streams_ops = {0};
    SapWitIoPollDispatchOps io_poll_ops = {0};
    SapWitFilesystemTypesDispatchOps filesystem_types_ops = {0};
    SapWitFilesystemPreopensDispatchOps filesystem_preopens_ops = {0};
    SapWitCliEnvironmentDispatchOps cli_environment_ops = {
        .get_arguments = cli_get_arguments,
    };
    SapWitCliRunDispatchOps cli_run_ops = {
        .run = cli_run,
    };
    SapWitRandomCommand random_command = {0};
    SapWitRandomReply random_reply = {0};
    SapWitClocksMonotonicClockCommand clocks_command = {0};
    SapWitClocksMonotonicClockReply clocks_reply = {0};
    SapWitCliEnvironmentCommand cli_environment_command = {0};
    SapWitCliEnvironmentReply cli_environment_reply = {0};
    SapWitCliEnvironmentReply cli_environment_reply_bytes = {0};
    SapWitCliRunCommand cli_run_command = {0};
    SapWitCliRunReply cli_run_reply = {0};
    SapWitRandomReply random_reply_bytes = {0};
    ThatchRegion region = {0};
    ThatchRegion view = {0};
    ThatchCursor cursor = 0u;
    char endpoint_name[256];
    uint8_t random_command_bytes[32] = {0};
    uint8_t random_reply_blob[32] = {0};
    uint8_t cli_command_bytes[32] = {0};
    uint8_t cli_reply_blob[64] = {0};
    uint32_t random_command_len = 0u;
    uint32_t random_reply_len = 0u;
    uint32_t cli_command_len = 0u;
    uint32_t cli_reply_len = 0u;
    uint32_t random_calls = 0u;
    uint32_t clocks_calls = 0u;
    uint32_t cli_environment_calls = 0u;
    uint32_t cli_run_calls = 0u;
    int ok = 1;

    ok &= expect_u32("random worlds", sap_wit_random_worlds_count, 1u);
    ok &= expect_u32("random world bindings", sap_wit_random_world_bindings_count, 3u);
    ok &= expect_u32("random import endpoints", sap_wit_random_imports_import_endpoints_count, 3u);

    ok &= expect_u32("clocks worlds", sap_wit_clocks_worlds_count, 1u);
    ok &= expect_u32("clocks world bindings", sap_wit_clocks_world_bindings_count, 3u);
    ok &= expect_u32("clocks import endpoints", sap_wit_clocks_imports_import_endpoints_count, 3u);

    ok &= expect_u32("io worlds", sap_wit_io_worlds_count, 1u);
    ok &= expect_u32("io world bindings", sap_wit_io_world_bindings_count, 2u);
    ok &= expect_u32("io import endpoints", sap_wit_io_imports_import_endpoints_count, 2u);

    ok &= expect_u32("filesystem worlds", sap_wit_filesystem_worlds_count, 1u);
    ok &= expect_u32("filesystem world bindings", sap_wit_filesystem_world_bindings_count, 2u);
    ok &= expect_u32("filesystem import endpoints",
                     sap_wit_filesystem_imports_import_endpoints_count,
                     2u);

    ok &= expect_u32("cli worlds", sap_wit_cli_worlds_count, 2u);
    ok &= expect_u32("cli world bindings", sap_wit_cli_world_bindings_count, 3u);
    ok &= expect_u32("cli import endpoints", sap_wit_cli_command_import_endpoints_count, 1u);
    ok &= expect_u32("cli export endpoints", sap_wit_cli_command_export_endpoints_count, 1u);

    random_world = sap_wit_find_world_descriptor(sap_wit_random_worlds,
                                                 sap_wit_random_worlds_count,
                                                 "imports");
    random_binding = sap_wit_find_world_binding_descriptor(sap_wit_random_world_bindings,
                                                           sap_wit_random_world_bindings_count,
                                                           "imports",
                                                           "random",
                                                           SAP_WIT_WORLD_ITEM_IMPORT);
    random_endpoint = sap_wit_find_world_endpoint_descriptor(sap_wit_random_imports_import_endpoints,
                                                             sap_wit_random_imports_import_endpoints_count,
                                                             "random");
    clocks_world = sap_wit_find_world_descriptor(sap_wit_clocks_worlds,
                                                 sap_wit_clocks_worlds_count,
                                                 "imports");
    clocks_binding = sap_wit_find_world_binding_descriptor(sap_wit_clocks_world_bindings,
                                                           sap_wit_clocks_world_bindings_count,
                                                           "imports",
                                                           "monotonic-clock",
                                                           SAP_WIT_WORLD_ITEM_IMPORT);
    clocks_endpoint = sap_wit_find_world_endpoint_descriptor(sap_wit_clocks_imports_import_endpoints,
                                                             sap_wit_clocks_imports_import_endpoints_count,
                                                             "monotonic-clock");
    io_world = sap_wit_find_world_descriptor(sap_wit_io_worlds,
                                             sap_wit_io_worlds_count,
                                             "imports");
    io_streams_binding = sap_wit_find_world_binding_descriptor(sap_wit_io_world_bindings,
                                                               sap_wit_io_world_bindings_count,
                                                               "imports",
                                                               "streams",
                                                               SAP_WIT_WORLD_ITEM_IMPORT);
    io_poll_binding = sap_wit_find_world_binding_descriptor(sap_wit_io_world_bindings,
                                                            sap_wit_io_world_bindings_count,
                                                            "imports",
                                                            "poll",
                                                            SAP_WIT_WORLD_ITEM_IMPORT);
    io_streams_endpoint = sap_wit_find_world_endpoint_descriptor(
        sap_wit_io_imports_import_endpoints,
        sap_wit_io_imports_import_endpoints_count,
        "streams");
    io_poll_endpoint = sap_wit_find_world_endpoint_descriptor(sap_wit_io_imports_import_endpoints,
                                                              sap_wit_io_imports_import_endpoints_count,
                                                              "poll");
    filesystem_world = sap_wit_find_world_descriptor(sap_wit_filesystem_worlds,
                                                     sap_wit_filesystem_worlds_count,
                                                     "imports");
    filesystem_types_binding =
        sap_wit_find_world_binding_descriptor(sap_wit_filesystem_world_bindings,
                                              sap_wit_filesystem_world_bindings_count,
                                              "imports",
                                              "types",
                                              SAP_WIT_WORLD_ITEM_IMPORT);
    filesystem_preopens_binding =
        sap_wit_find_world_binding_descriptor(sap_wit_filesystem_world_bindings,
                                              sap_wit_filesystem_world_bindings_count,
                                              "imports",
                                              "preopens",
                                              SAP_WIT_WORLD_ITEM_IMPORT);
    filesystem_types_endpoint =
        sap_wit_find_world_endpoint_descriptor(sap_wit_filesystem_imports_import_endpoints,
                                               sap_wit_filesystem_imports_import_endpoints_count,
                                               "types");
    filesystem_preopens_endpoint =
        sap_wit_find_world_endpoint_descriptor(sap_wit_filesystem_imports_import_endpoints,
                                               sap_wit_filesystem_imports_import_endpoints_count,
                                               "preopens");
    cli_command_world = sap_wit_find_world_descriptor(sap_wit_cli_worlds,
                                                      sap_wit_cli_worlds_count,
                                                      "command");
    cli_imports_world = sap_wit_find_world_descriptor(sap_wit_cli_worlds,
                                                      sap_wit_cli_worlds_count,
                                                      "imports");
    cli_import_binding = sap_wit_find_world_binding_descriptor(sap_wit_cli_world_bindings,
                                                               sap_wit_cli_world_bindings_count,
                                                               "imports",
                                                               "environment",
                                                               SAP_WIT_WORLD_ITEM_IMPORT);
    cli_export_binding = sap_wit_find_world_binding_descriptor(sap_wit_cli_world_bindings,
                                                               sap_wit_cli_world_bindings_count,
                                                               "command",
                                                               "run",
                                                               SAP_WIT_WORLD_ITEM_EXPORT);
    cli_environment_endpoint =
        sap_wit_find_world_endpoint_descriptor(sap_wit_cli_command_import_endpoints,
                                              sap_wit_cli_command_import_endpoints_count,
                                              "environment");
    cli_run_endpoint =
        sap_wit_find_world_endpoint_descriptor(sap_wit_cli_command_export_endpoints,
                                              sap_wit_cli_command_export_endpoints_count,
                                              "run");

    ok &= expect_true("random world descriptor", random_world != NULL);
    ok &= expect_true("random binding descriptor", random_binding != NULL);
    ok &= expect_true("random endpoint descriptor", random_endpoint != NULL);
    ok &= expect_true("clocks world descriptor", clocks_world != NULL);
    ok &= expect_true("clocks binding descriptor", clocks_binding != NULL);
    ok &= expect_true("clocks endpoint descriptor", clocks_endpoint != NULL);
    ok &= expect_true("io world descriptor", io_world != NULL);
    ok &= expect_true("io streams binding descriptor", io_streams_binding != NULL);
    ok &= expect_true("io poll binding descriptor", io_poll_binding != NULL);
    ok &= expect_true("io streams endpoint descriptor", io_streams_endpoint != NULL);
    ok &= expect_true("io poll endpoint descriptor", io_poll_endpoint != NULL);
    ok &= expect_true("filesystem world descriptor", filesystem_world != NULL);
    ok &= expect_true("filesystem types binding descriptor", filesystem_types_binding != NULL);
    ok &= expect_true("filesystem preopens binding descriptor", filesystem_preopens_binding != NULL);
    ok &= expect_true("filesystem types endpoint", filesystem_types_endpoint != NULL);
    ok &= expect_true("filesystem preopens endpoint", filesystem_preopens_endpoint != NULL);
    ok &= expect_true("cli command world descriptor", cli_command_world != NULL);
    ok &= expect_true("cli imports world descriptor", cli_imports_world != NULL);
    ok &= expect_true("cli import binding descriptor", cli_import_binding != NULL);
    ok &= expect_true("cli export binding descriptor", cli_export_binding != NULL);
    ok &= expect_true("cli environment endpoint", cli_environment_endpoint != NULL);
    ok &= expect_true("cli run endpoint", cli_run_endpoint != NULL);

    if (!random_world || !random_binding || !random_endpoint || !clocks_world || !clocks_binding
            || !clocks_endpoint || !io_world || !io_streams_binding || !io_poll_binding
            || !io_streams_endpoint || !io_poll_endpoint
            || !filesystem_world || !filesystem_types_binding || !filesystem_preopens_binding
            || !filesystem_types_endpoint || !filesystem_preopens_endpoint
            || !cli_command_world || !cli_imports_world || !cli_import_binding
            || !cli_export_binding || !cli_environment_endpoint || !cli_run_endpoint) {
        return 1;
    }

    random_command.case_tag = SAP_WIT_RANDOM_COMMAND_GET_RANDOM_U64;
    clocks_command.case_tag = SAP_WIT_CLOCKS_MONOTONIC_CLOCK_COMMAND_NOW;
    cli_environment_command.case_tag = SAP_WIT_CLI_ENVIRONMENT_COMMAND_GET_ARGUMENTS;
    cli_run_command.case_tag = SAP_WIT_CLI_RUN_COMMAND_RUN;

    ok &= expect_str("random endpoint name",
                     (sap_wit_world_endpoint_name(random_endpoint,
                                                  endpoint_name,
                                                  sizeof(endpoint_name))
                      > 0u)
                         ? endpoint_name
                         : NULL,
                     "wasi:random@0.2.9/imports#import:random");
    ok &= expect_true("random endpoint qualified lookup",
                      sap_wit_find_world_endpoint_descriptor_qualified(
                          sap_wit_random_imports_import_endpoints,
                          sap_wit_random_imports_import_endpoints_count,
                          endpoint_name)
                          == random_endpoint);
    ok &= expect_str("cli import endpoint name",
                     (sap_wit_world_endpoint_name(cli_environment_endpoint,
                                                  endpoint_name,
                                                  sizeof(endpoint_name))
                      > 0u)
                         ? endpoint_name
                         : NULL,
                     "wasi:cli@0.2.9/command#import:environment");
    ok &= expect_true("cli import endpoint qualified lookup",
                      sap_wit_find_world_endpoint_descriptor_qualified(
                          sap_wit_cli_command_import_endpoints,
                          sap_wit_cli_command_import_endpoints_count,
                          endpoint_name)
                          == cli_environment_endpoint);

    ok &= expect_u32("bind random endpoint",
                     (uint32_t)sap_wit_world_endpoint_bind(&random_imports,
                                                           random_endpoint,
                                                           &random_calls,
                                                           &random_ops),
                     0u);
    ok &= expect_u32("bind clocks endpoint",
                     (uint32_t)sap_wit_world_endpoint_bind(&clocks_imports,
                                                           clocks_endpoint,
                                                           &clocks_calls,
                                                           &monotonic_ops),
                     0u);
    ok &= expect_u32("bind io streams endpoint",
                     (uint32_t)sap_wit_world_endpoint_bind(&io_imports,
                                                           io_streams_endpoint,
                                                           NULL,
                                                           &io_streams_ops),
                     0u);
    ok &= expect_u32("bind io poll endpoint",
                     (uint32_t)sap_wit_world_endpoint_bind(&io_imports,
                                                           io_poll_endpoint,
                                                           NULL,
                                                           &io_poll_ops),
                     0u);
    ok &= expect_u32("bind cli import endpoint",
                     (uint32_t)sap_wit_world_endpoint_bind(&cli_imports,
                                                           cli_environment_endpoint,
                                                           &cli_environment_calls,
                                                           &cli_environment_ops),
                     0u);
    ok &= expect_u32("bind filesystem types endpoint",
                     (uint32_t)sap_wit_world_endpoint_bind(&filesystem_imports,
                                                           filesystem_types_endpoint,
                                                           NULL,
                                                           &filesystem_types_ops),
                     0u);
    ok &= expect_u32("bind filesystem preopens endpoint",
                     (uint32_t)sap_wit_world_endpoint_bind(&filesystem_imports,
                                                           filesystem_preopens_endpoint,
                                                           NULL,
                                                           &filesystem_preopens_ops),
                     0u);
    ok &= expect_u32("bind cli export endpoint",
                     (uint32_t)sap_wit_world_endpoint_bind(&cli_exports,
                                                           cli_run_endpoint,
                                                           &cli_run_calls,
                                                           &cli_run_ops),
                     0u);
    ok &= expect_ptr("random endpoint ctx",
                     sap_wit_world_endpoint_ctx(&random_imports, random_endpoint),
                     &random_calls);
    ok &= expect_ptr("random endpoint ops",
                     sap_wit_world_endpoint_ops(&random_imports, random_endpoint),
                     &random_ops);
    ok &= expect_ptr("clocks endpoint ctx",
                     sap_wit_world_endpoint_ctx(&clocks_imports, clocks_endpoint),
                     &clocks_calls);
    ok &= expect_ptr("cli import endpoint ctx",
                     sap_wit_world_endpoint_ctx(&cli_imports, cli_environment_endpoint),
                     &cli_environment_calls);
    ok &= expect_ptr("cli export endpoint ctx",
                     sap_wit_world_endpoint_ctx(&cli_exports, cli_run_endpoint),
                     &cli_run_calls);

    ok &= expect_u32("invoke random endpoint",
                     (uint32_t)sap_wit_world_endpoint_invoke(random_endpoint,
                                                             &random_imports,
                                                             &random_command,
                                                             &random_reply),
                     0u);
    ok &= expect_u32("invoke clocks endpoint",
                     (uint32_t)sap_wit_world_endpoint_invoke(clocks_endpoint,
                                                             &clocks_imports,
                                                             &clocks_command,
                                                             &clocks_reply),
                     0u);
    ok &= expect_u32("invoke cli import endpoint",
                     (uint32_t)sap_wit_world_endpoint_invoke(cli_environment_endpoint,
                                                             &cli_imports,
                                                             &cli_environment_command,
                                                             &cli_environment_reply),
                     0u);
    ok &= expect_u32("invoke cli export endpoint",
                     (uint32_t)sap_wit_world_endpoint_invoke(cli_run_endpoint,
                                                             &cli_exports,
                                                             &cli_run_command,
                                                             &cli_run_reply),
                     0u);

    init_writable_region(&region, random_command_bytes, (uint32_t)sizeof(random_command_bytes));
    ok &= expect_u32("encode random command bytes",
                     (uint32_t)sap_wit_write_random_command(&region, &random_command),
                     0u);
    random_command_len = thatch_region_used(&region);
    ok &= expect_u32("invoke random endpoint bytes",
                     (uint32_t)sap_wit_world_endpoint_invoke_bytes(random_endpoint,
                                                                   &random_imports,
                                                                   random_command_bytes,
                                                                   random_command_len,
                                                                   random_reply_blob,
                                                                   (uint32_t)sizeof(random_reply_blob),
                                                                   &random_reply_len),
                     0u);
    ok &= expect_true("random reply len > 0", random_reply_len > 0u);
    ok &= expect_u32("init random reply view",
                     (uint32_t)thatch_region_init_readonly(&view,
                                                           random_reply_blob,
                                                           random_reply_len),
                     0u);
    cursor = 0u;
    ok &= expect_u32("decode random reply bytes",
                     (uint32_t)sap_wit_read_random_reply(&view, &cursor, &random_reply_bytes),
                     0u);
    ok &= expect_u32("random reply bytes cursor", cursor, random_reply_len);

    init_writable_region(&region, cli_command_bytes, (uint32_t)sizeof(cli_command_bytes));
    ok &= expect_u32("encode cli command bytes",
                     (uint32_t)sap_wit_write_cli_environment_command(&region,
                                                                     &cli_environment_command),
                     0u);
    cli_command_len = thatch_region_used(&region);
    ok &= expect_u32("invoke cli endpoint bytes",
                     (uint32_t)sap_wit_world_endpoint_invoke_bytes(cli_environment_endpoint,
                                                                   &cli_imports,
                                                                   cli_command_bytes,
                                                                   cli_command_len,
                                                                   cli_reply_blob,
                                                                   (uint32_t)sizeof(cli_reply_blob),
                                                                   &cli_reply_len),
                     0u);
    ok &= expect_true("cli reply len > 0", cli_reply_len > 0u);
    ok &= expect_u32("init cli reply view",
                     (uint32_t)thatch_region_init_readonly(&view, cli_reply_blob, cli_reply_len),
                     0u);
    cursor = 0u;
    ok &= expect_u32("decode cli reply bytes",
                     (uint32_t)sap_wit_read_cli_environment_reply(&view,
                                                                  &cursor,
                                                                  &cli_environment_reply_bytes),
                     0u);
    ok &= expect_u32("cli reply bytes cursor", cursor, cli_reply_len);

    ok &= expect_u32("random calls", random_calls, 2u);
    ok &= expect_u32("clocks calls", clocks_calls, 1u);
    ok &= expect_u32("cli environment calls", cli_environment_calls, 2u);
    ok &= expect_u32("cli run calls", cli_run_calls, 1u);
    ok &= expect_u32("random reply case",
                     random_reply.case_tag,
                     SAP_WIT_RANDOM_REPLY_GET_RANDOM_U64);
    ok &= expect_u32("random reply value", (uint32_t)random_reply.val.get_random_u64, 77u);
    ok &= expect_u32("random reply bytes case",
                     random_reply_bytes.case_tag,
                     SAP_WIT_RANDOM_REPLY_GET_RANDOM_U64);
    ok &= expect_u32("random reply bytes value",
                     (uint32_t)random_reply_bytes.val.get_random_u64,
                     77u);
    ok &= expect_u32("clocks reply case",
                     clocks_reply.case_tag,
                     SAP_WIT_CLOCKS_MONOTONIC_CLOCK_REPLY_NOW);
    ok &= expect_u32("clocks reply value", (uint32_t)clocks_reply.val.now, 123u);
    ok &= expect_u32("cli environment reply case",
                     cli_environment_reply.case_tag,
                     SAP_WIT_CLI_ENVIRONMENT_REPLY_GET_ARGUMENTS);
    ok &= expect_u32("cli environment reply bytes case",
                     cli_environment_reply_bytes.case_tag,
                     SAP_WIT_CLI_ENVIRONMENT_REPLY_GET_ARGUMENTS);
    ok &= expect_u32("cli run reply case",
                     cli_run_reply.case_tag,
                     SAP_WIT_CLI_RUN_REPLY_STATUS);
    ok &= expect_u32("cli run reply ok", cli_run_reply.val.status.is_v_ok, 1u);

    return ok ? 0 : 1;
}
