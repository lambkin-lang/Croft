#include "croft/host_wasm.h"
#include "croft/wit_wasi_machine_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WIT_WORLD_BRIDGE_GUEST_WASM_PATH
#define WIT_WORLD_BRIDGE_GUEST_WASM_PATH "wit_world_bridge_guest.wasm"
#endif

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #expr, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

static uint8_t *slurp_file(const char *path, uint32_t *out_len)
{
    FILE *f = NULL;
    uint8_t *buf = NULL;
    long sz = 0;
    size_t nr = 0u;

    f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    nr = fread(buf, 1u, (size_t)sz, f);
    fclose(f);
    if ((long)nr != sz) {
        free(buf);
        return NULL;
    }

    if (out_len) {
        *out_len = (uint32_t)sz;
    }
    return buf;
}

static void init_writable_region(ThatchRegion *region, uint8_t *data, uint32_t cap)
{
    memset(region, 0, sizeof(*region));
    region->page_ptr = data;
    region->capacity = cap;
    region->head = 0u;
    region->sealed = 0;
}

static uint32_t encode_cli_get_arguments_command(uint8_t *out, uint32_t out_cap)
{
    SapWitCliEnvironmentCommand command = {0};
    ThatchRegion region = {0};

    command.case_tag = SAP_WIT_CLI_ENVIRONMENT_COMMAND_GET_ARGUMENTS;
    init_writable_region(&region, out, out_cap);
    CHECK(sap_wit_write_cli_environment_command(&region, &command) == 0);
    return thatch_region_used(&region);
}

static uint32_t encode_random_get_random_u64_command(uint8_t *out, uint32_t out_cap)
{
    SapWitRandomCommand command = {0};
    ThatchRegion region = {0};

    command.case_tag = SAP_WIT_RANDOM_COMMAND_GET_RANDOM_U64;
    init_writable_region(&region, out, out_cap);
    CHECK(sap_wit_write_random_command(&region, &command) == 0);
    return thatch_region_used(&region);
}

static void write_u32(uint8_t *memory, uint32_t offset, uint32_t value)
{
    memcpy(memory + offset, &value, sizeof(value));
}

static uint32_t read_u32(const uint8_t *memory, uint32_t offset)
{
    uint32_t value = 0u;
    memcpy(&value, memory + offset, sizeof(value));
    return value;
}

static int32_t invoke_guest_endpoint(host_wasm_ctx_t *ctx,
                                     uint32_t name_offset,
                                     uint32_t name_len,
                                     uint32_t command_offset,
                                     uint32_t command_len,
                                     uint32_t reply_offset,
                                     uint32_t reply_cap,
                                     uint32_t reply_len_offset)
{
    char name_offset_arg[32];
    char name_len_arg[32];
    char command_offset_arg[32];
    char command_len_arg[32];
    char reply_offset_arg[32];
    char reply_cap_arg[32];
    char reply_len_offset_arg[32];
    const char *argv[7];

    snprintf(name_offset_arg, sizeof(name_offset_arg), "%u", name_offset);
    snprintf(name_len_arg, sizeof(name_len_arg), "%u", name_len);
    snprintf(command_offset_arg, sizeof(command_offset_arg), "%u", command_offset);
    snprintf(command_len_arg, sizeof(command_len_arg), "%u", command_len);
    snprintf(reply_offset_arg, sizeof(reply_offset_arg), "%u", reply_offset);
    snprintf(reply_cap_arg, sizeof(reply_cap_arg), "%u", reply_cap);
    snprintf(reply_len_offset_arg, sizeof(reply_len_offset_arg), "%u", reply_len_offset);

    argv[0] = name_offset_arg;
    argv[1] = name_len_arg;
    argv[2] = command_offset_arg;
    argv[3] = command_len_arg;
    argv[4] = reply_offset_arg;
    argv[5] = reply_cap_arg;
    argv[6] = reply_len_offset_arg;
    return host_wasm_call(ctx, "invoke_endpoint", 7, argv);
}

void run_test_wasm_wit_guest(int argc, char **argv)
{
    static const char *guest_argv[] = {"guest-program", "alpha"};
    croft_wit_wasi_machine_runtime_options options = {0};
    croft_wit_wasi_machine_runtime *runtime = NULL;
    SapWitCliCommandWorldImports cli_bindings = {0};
    SapWitRandomImportsWorldImports random_bindings = {0};
    uint32_t wasm_len = 0u;
    uint8_t *wasm_bytes = NULL;
    host_wasm_ctx_t *ctx = NULL;
    uint8_t *memory = NULL;
    uint32_t memory_size = 0u;
    const char *cli_name = "wasi:cli@0.2.9/command#import:environment";
    const char *random_name = "wasi:random@0.2.9/imports#import:random";
    const uint32_t cli_name_offset = 1024u;
    const uint32_t cli_command_offset = 1152u;
    const uint32_t cli_reply_offset = 1280u;
    const uint32_t cli_reply_len_offset = 1536u;
    const uint32_t random_name_offset = 1600u;
    const uint32_t random_command_offset = 1728u;
    const uint32_t random_reply_offset = 1856u;
    const uint32_t random_reply_len_offset = 1984u;
    uint8_t cli_command_bytes[32] = {0};
    uint8_t random_command_bytes[32] = {0};
    uint32_t cli_command_len = 0u;
    uint32_t cli_reply_len = 0u;
    uint32_t random_command_len = 0u;
    uint32_t random_reply_len = 0u;
    ThatchRegion view = {0};
    ThatchCursor cursor = 0u;
    SapWitCliEnvironmentReply cli_reply = {0};
    SapWitRandomReply random_reply = {0};
    int32_t rc = 0;

    (void)argc;
    (void)argv;

    croft_wit_wasi_machine_runtime_options_default(&options);
    options.argv = guest_argv;
    options.argc = 2u;
    options.inherit_environment = 0u;
    runtime = croft_wit_wasi_machine_runtime_create(&options);
    CHECK(runtime != NULL);
    CHECK(croft_wit_wasi_machine_runtime_bind_cli_command_imports(runtime, &cli_bindings) == 0);
    CHECK(croft_wit_wasi_machine_runtime_bind_random_imports(runtime, &random_bindings) == 0);

    wasm_bytes = slurp_file(WIT_WORLD_BRIDGE_GUEST_WASM_PATH, &wasm_len);
    if (!wasm_bytes) {
        printf("SKIP: %s not found (likely compiler was not available).\n",
               WIT_WORLD_BRIDGE_GUEST_WASM_PATH);
        croft_wit_wasi_machine_runtime_destroy(runtime);
        return;
    }

    ctx = host_wasm_create(wasm_bytes, wasm_len, 64u * 1024u);
    CHECK(ctx != NULL);
    CHECK(host_wasm_register_wit_world_endpoints(ctx,
                                                 sap_wit_cli_command_import_endpoints,
                                                 sap_wit_cli_command_import_endpoints_count,
                                                 &cli_bindings)
          == 0);
    CHECK(host_wasm_register_wit_world_endpoints(ctx,
                                                 sap_wit_random_imports_import_endpoints,
                                                 sap_wit_random_imports_import_endpoints_count,
                                                 &random_bindings)
          == 0);

    memory = host_wasm_get_memory(ctx, &memory_size);
    CHECK(memory != NULL);
    CHECK(memory_size > random_reply_len_offset + 64u);

    memcpy(memory + cli_name_offset, cli_name, strlen(cli_name));
    memcpy(memory + random_name_offset, random_name, strlen(random_name));

    cli_command_len = encode_cli_get_arguments_command(cli_command_bytes, sizeof(cli_command_bytes));
    random_command_len = encode_random_get_random_u64_command(random_command_bytes,
                                                              sizeof(random_command_bytes));
    memcpy(memory + cli_command_offset, cli_command_bytes, cli_command_len);
    memcpy(memory + random_command_offset, random_command_bytes, random_command_len);
    write_u32(memory, cli_reply_len_offset, 0u);
    write_u32(memory, random_reply_len_offset, 0u);

    rc = invoke_guest_endpoint(ctx,
                               cli_name_offset,
                               (uint32_t)strlen(cli_name),
                               cli_command_offset,
                               cli_command_len,
                               cli_reply_offset,
                               128u,
                               cli_reply_len_offset);
    CHECK(rc == 0);
    cli_reply_len = read_u32(memory, cli_reply_len_offset);
    CHECK(cli_reply_len > 0u);
    CHECK(thatch_region_init_readonly(&view, memory + cli_reply_offset, cli_reply_len) == 0);
    cursor = 0u;
    CHECK(sap_wit_read_cli_environment_reply(&view, &cursor, &cli_reply) == 0);
    CHECK(cursor == cli_reply_len);
    CHECK(cli_reply.case_tag == SAP_WIT_CLI_ENVIRONMENT_REPLY_GET_ARGUMENTS);
    CHECK(cli_reply.val.get_arguments.len == 2u);

    rc = invoke_guest_endpoint(ctx,
                               random_name_offset,
                               (uint32_t)strlen(random_name),
                               random_command_offset,
                               random_command_len,
                               random_reply_offset,
                               64u,
                               random_reply_len_offset);
    CHECK(rc == 0);
    random_reply_len = read_u32(memory, random_reply_len_offset);
    CHECK(random_reply_len > 0u);
    CHECK(thatch_region_init_readonly(&view, memory + random_reply_offset, random_reply_len) == 0);
    cursor = 0u;
    CHECK(sap_wit_read_random_reply(&view, &cursor, &random_reply) == 0);
    CHECK(cursor == random_reply_len);
    CHECK(random_reply.case_tag == SAP_WIT_RANDOM_REPLY_GET_RANDOM_U64);

    host_wasm_destroy(ctx);
    free(wasm_bytes);
    croft_wit_wasi_machine_runtime_destroy(runtime);
    printf("WASM WIT bridge test OK.\n");
}
