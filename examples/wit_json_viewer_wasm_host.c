#include "croft/host_file_dialog.h"
#include "croft/host_wasm.h"
#include "croft/wit_host_clock_runtime.h"
#include "croft/wit_host_gpu2d_runtime.h"
#include "croft/wit_host_window_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WIT_JSON_VIEWER_GUEST_WASM_PATH
#define WIT_JSON_VIEWER_GUEST_WASM_PATH "wit_json_viewer_guest.wasm"
#endif

static const uint8_t k_default_json[] =
    "{"
    "\"project\":\"Croft\","
    "\"features\":{\"solver\":true,\"thatch\":\"json\",\"runtime\":\"wasm3-windowed\"},"
    "\"items\":[\"alpha\",{\"nested\":true,\"depth\":2},3],"
    "\"notes\":{\"viewer\":\"read-only\",\"mode\":\"collapsible\"}"
    "}";

static int env_flag_enabled(const char* value)
{
    return value && value[0] != '\0' && !(value[0] == '0' && value[1] == '\0');
}

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

int main(int argc, char** argv)
{
    const char* auto_close_env = getenv("CROFT_WIT_JSON_VIEWER_WASM_HOST_AUTO_CLOSE_MS");
    const char* pick_file_env = getenv("CROFT_WIT_JSON_VIEWER_WASM_HOST_PICK_FILE");
    const uint8_t* json_data = k_default_json;
    uint32_t json_len = (uint32_t)(sizeof(k_default_json) - 1u);
    uint8_t* file_bytes = NULL;
    char* picked_path = NULL;
    const char* input_path = NULL;
    croft_wit_host_window_runtime* window_runtime = NULL;
    croft_wit_host_gpu2d_runtime* gpu_runtime = NULL;
    croft_wit_host_clock_runtime* clock_runtime = NULL;
    SapWitHostWindowHostWindowWorldExports window_exports = {0};
    SapWitHostGpu2dHostGpu2dWorldExports gpu_exports = {0};
    SapWitHostClockHostClockWorldExports clock_exports = {0};
    uint8_t* wasm_bytes = NULL;
    uint32_t wasm_len = 0u;
    host_wasm_ctx_t* wasm = NULL;
    uint8_t* memory = NULL;
    uint32_t memory_size = 0u;
    const uint32_t json_offset = 4096u;
    char json_offset_arg[32];
    char json_len_arg[32];
    char auto_close_arg[32];
    const char* guest_argv[3];
    int auto_close_ms = 1500;
    int32_t wasm_rc = -1;
    int rc = 1;
    int prompt_for_file = env_flag_enabled(pick_file_env);
    int argi;

    for (argi = 1; argi < argc; argi++) {
        if (strcmp(argv[argi], "--open") == 0 || strcmp(argv[argi], "-p") == 0) {
            prompt_for_file = 1;
        } else if (!input_path) {
            input_path = argv[argi];
        }
    }

    if (!input_path && prompt_for_file) {
        picked_path = host_file_dialog_open_path();
        if (picked_path) {
            input_path = picked_path;
        }
    }

    if (input_path) {
        file_bytes = slurp_file(input_path, &json_len);
        if (!file_bytes) {
            fprintf(stderr, "example_wit_json_viewer_wasm_host: unable to read %s\n", input_path);
            host_file_dialog_free_path(picked_path);
            return 1;
        }
        json_data = file_bytes;
    }

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
        fprintf(stderr, "example_wit_json_viewer_wasm_host: runtime init failed\n");
        goto cleanup;
    }
    if (croft_wit_host_window_runtime_bind_exports(window_runtime, &window_exports) != 0
            || croft_wit_host_gpu2d_runtime_bind_exports(gpu_runtime, &gpu_exports) != 0
            || croft_wit_host_clock_runtime_bind_exports(clock_runtime, &clock_exports) != 0) {
        fprintf(stderr, "example_wit_json_viewer_wasm_host: export binding failed\n");
        goto cleanup;
    }

    wasm_bytes = slurp_file(WIT_JSON_VIEWER_GUEST_WASM_PATH, &wasm_len);
    if (!wasm_bytes) {
        fprintf(stderr,
                "example_wit_json_viewer_wasm_host: unable to read %s\n",
                WIT_JSON_VIEWER_GUEST_WASM_PATH);
        goto cleanup;
    }

    wasm = host_wasm_create(wasm_bytes, wasm_len, 128u * 1024u);
    if (!wasm) {
        fprintf(stderr, "example_wit_json_viewer_wasm_host: host_wasm_create failed\n");
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
        fprintf(stderr, "example_wit_json_viewer_wasm_host: WIT endpoint registration failed\n");
        goto cleanup;
    }

    memory = host_wasm_get_memory(wasm, &memory_size);
    if (!memory || memory_size < json_offset + json_len) {
        fprintf(stderr, "example_wit_json_viewer_wasm_host: guest memory too small\n");
        goto cleanup;
    }
    memcpy(memory + json_offset, json_data, json_len);

    snprintf(json_offset_arg, sizeof(json_offset_arg), "%u", json_offset);
    snprintf(json_len_arg, sizeof(json_len_arg), "%u", json_len);
    snprintf(auto_close_arg, sizeof(auto_close_arg), "%d", auto_close_ms);
    guest_argv[0] = json_offset_arg;
    guest_argv[1] = json_len_arg;
    guest_argv[2] = auto_close_arg;

    wasm_rc = host_wasm_call(wasm, "wit_guest_json_viewer_run", 3, guest_argv);
    if (wasm_rc <= 0) {
        fprintf(stderr, "example_wit_json_viewer_wasm_host: guest returned %d\n", wasm_rc);
        goto cleanup;
    }

    printf("frames=%d json_bytes=%u guest=%s\n",
           wasm_rc,
           json_len,
           WIT_JSON_VIEWER_GUEST_WASM_PATH);
    rc = 0;

cleanup:
    host_wasm_destroy(wasm);
    free(wasm_bytes);
    croft_wit_host_clock_runtime_destroy(clock_runtime);
    croft_wit_host_gpu2d_runtime_destroy(gpu_runtime);
    croft_wit_host_window_runtime_destroy(window_runtime);
    free(file_bytes);
    host_file_dialog_free_path(picked_path);
    return rc;
}
