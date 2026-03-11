#include "croft/host_wasm.h"
#include "croft/wit_wasi_machine_runtime.h"

#include <stdio.h>
#include <stdlib.h>

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

static int32_t call_guest_noargs(host_wasm_ctx_t *ctx, const char *func_name)
{
    return host_wasm_call(ctx, func_name, 0, NULL);
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
    int32_t rc = 0;
    uint64_t random_value = 0u;

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

    CHECK(call_guest_noargs(ctx, "wit_guest_handle_count") == 0);

    rc = call_guest_noargs(ctx, "wit_guest_fetch_arguments");
    CHECK(rc == 0);
    CHECK(call_guest_noargs(ctx, "wit_guest_argument_count") == 2);
    CHECK(call_guest_noargs(ctx, "wit_guest_handle_count") == 1);

    rc = call_guest_noargs(ctx, "wit_guest_fetch_random_u64");
    CHECK(rc == 0);
    CHECK(call_guest_noargs(ctx, "wit_guest_handle_count") == 2);
    random_value = (uint64_t)(uint32_t)call_guest_noargs(ctx, "wit_guest_random_low32");
    random_value |= (uint64_t)(uint32_t)call_guest_noargs(ctx, "wit_guest_random_high32") << 32;
    (void)random_value;

    rc = call_guest_noargs(ctx, "wit_guest_fetch_random_u64");
    CHECK(rc == 0);
    CHECK(call_guest_noargs(ctx, "wit_guest_handle_count") == 2);

    host_wasm_destroy(ctx);
    free(wasm_bytes);
    croft_wit_wasi_machine_runtime_destroy(runtime);
    printf("WASM WIT bridge test OK.\n");
}
