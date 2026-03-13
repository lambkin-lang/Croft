#include "croft/host_wasm.h"
#include "croft/wit_host_clock_runtime.h"
#include "croft/wit_host_gpu2d_runtime.h"
#include "croft/wit_host_window_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef WIT_WINDOW_GUEST_WASM_PATH
#define WIT_WINDOW_GUEST_WASM_PATH "wit_window_guest.wasm"
#endif

static uint8_t* slurp_file(const char* path, uint32_t* out_len)
{
    FILE* file = NULL;
    uint8_t* bytes = NULL;
    long size = 0;
    size_t read_count = 0u;

    if (!path) {
        return NULL;
    }

    file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    bytes = (uint8_t*)malloc((size_t)size);
    if (!bytes) {
        fclose(file);
        return NULL;
    }

    read_count = fread(bytes, 1u, (size_t)size, file);
    fclose(file);
    if ((long)read_count != size) {
        free(bytes);
        return NULL;
    }

    if (out_len) {
        *out_len = (uint32_t)size;
    }
    return bytes;
}

int main(void)
{
    const char* auto_close_env = getenv("CROFT_WIT_WINDOW_WASM_HOST_AUTO_CLOSE_MS");
    croft_wit_host_window_runtime* window_runtime = NULL;
    croft_wit_host_gpu2d_runtime* gpu_runtime = NULL;
    croft_wit_host_clock_runtime* clock_runtime = NULL;
    SapWitHostWindowHostWindowWorldExports window_exports = {0};
    SapWitHostGpu2dHostGpu2dWorldExports gpu_exports = {0};
    SapWitHostClockHostClockWorldExports clock_exports = {0};
    uint8_t* wasm_bytes = NULL;
    uint32_t wasm_len = 0u;
    host_wasm_ctx_t* wasm = NULL;
    char auto_close_arg[32];
    const char* argv[1];
    int auto_close_ms = 600;
    int32_t wasm_rc = -1;
    int rc = 1;

    if (auto_close_env && auto_close_env[0] != '\0') {
        int parsed = atoi(auto_close_env);
        if (parsed > 0) {
            auto_close_ms = parsed;
        }
    }

    window_runtime = croft_wit_host_window_runtime_create();
    gpu_runtime = croft_wit_host_gpu2d_runtime_create();
    clock_runtime = croft_wit_host_clock_runtime_create();
    if (!window_runtime || !gpu_runtime || !clock_runtime) {
        fprintf(stderr, "example_wit_window_wasm_host: runtime init failed\n");
        goto cleanup;
    }
    if (croft_wit_host_window_runtime_bind_exports(window_runtime, &window_exports) != 0
            || croft_wit_host_gpu2d_runtime_bind_exports(gpu_runtime, &gpu_exports) != 0
            || croft_wit_host_clock_runtime_bind_exports(clock_runtime, &clock_exports) != 0) {
        fprintf(stderr, "example_wit_window_wasm_host: export binding failed\n");
        goto cleanup;
    }

    wasm_bytes = slurp_file(WIT_WINDOW_GUEST_WASM_PATH, &wasm_len);
    if (!wasm_bytes) {
        fprintf(stderr, "example_wit_window_wasm_host: unable to read %s\n", WIT_WINDOW_GUEST_WASM_PATH);
        goto cleanup;
    }

    wasm = host_wasm_create(wasm_bytes, wasm_len, 128u * 1024u);
    if (!wasm) {
        fprintf(stderr, "example_wit_window_wasm_host: host_wasm_create failed\n");
        goto cleanup;
    }

    if (host_wasm_register_wit_world_endpoints(wasm,
                                               sap_wit_host_window_host_window_export_endpoints,
                                               sap_wit_host_window_host_window_export_endpoints_count,
                                               &window_exports) != 0
            || host_wasm_register_wit_world_endpoints(wasm,
                                                      sap_wit_host_gpu2d_host_gpu2d_export_endpoints,
                                                      sap_wit_host_gpu2d_host_gpu2d_export_endpoints_count,
                                                      &gpu_exports) != 0
            || host_wasm_register_wit_world_endpoints(wasm,
                                                      sap_wit_host_clock_host_clock_export_endpoints,
                                                      sap_wit_host_clock_host_clock_export_endpoints_count,
                                                      &clock_exports) != 0) {
        fprintf(stderr, "example_wit_window_wasm_host: WIT endpoint registration failed\n");
        goto cleanup;
    }

    snprintf(auto_close_arg, sizeof(auto_close_arg), "%d", auto_close_ms);
    argv[0] = auto_close_arg;
    wasm_rc = host_wasm_call(wasm, "wit_guest_window_demo_run", 1, argv);
    if (wasm_rc <= 0) {
        fprintf(stderr, "example_wit_window_wasm_host: guest returned %d\n", wasm_rc);
        goto cleanup;
    }

    printf("frames=%d auto_close_ms=%d guest=%s\n",
           wasm_rc,
           auto_close_ms,
           WIT_WINDOW_GUEST_WASM_PATH);
    rc = 0;

cleanup:
    host_wasm_destroy(wasm);
    free(wasm_bytes);
    croft_wit_host_clock_runtime_destroy(clock_runtime);
    croft_wit_host_gpu2d_runtime_destroy(gpu_runtime);
    croft_wit_host_window_runtime_destroy(window_runtime);
    return rc;
}
