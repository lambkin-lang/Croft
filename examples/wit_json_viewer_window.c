#include "croft/json_viewer_window_app.h"
#include "croft/host_file_dialog.h"
#include "croft/wit_host_clock_runtime.h"
#include "croft/wit_host_gpu2d_runtime.h"
#include "croft/wit_host_window_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t k_default_json[] =
    "{"
    "\"project\":\"Croft\","
    "\"features\":{\"solver\":true,\"thatch\":\"json\",\"runtime\":\"wasm3-windowed\"},"
    "\"items\":[\"alpha\",{\"nested\":true,\"depth\":2},3],"
    "\"notes\":{\"viewer\":\"read-only\",\"mode\":\"collapsible\"}"
    "}";

static int env_flag_enabled(const char *value)
{
    return value && value[0] != '\0' && !(value[0] == '0' && value[1] == '\0');
}

typedef struct {
    croft_wit_host_window_runtime *window_runtime;
    croft_wit_host_gpu2d_runtime *gpu_runtime;
    croft_wit_host_clock_runtime *clock_runtime;
} CroftJsonViewerNativeDispatch;

static uint8_t *slurp_file(const char *path, uint32_t *out_len)
{
    FILE *file = NULL;
    uint8_t *bytes = NULL;
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
    bytes = (uint8_t *)malloc((size_t)size);
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

static int native_window_dispatch(void *ctx,
                                  const void *command,
                                  void *reply_out)
{
    CroftJsonViewerNativeDispatch *dispatch = (CroftJsonViewerNativeDispatch *)ctx;

    return dispatch && dispatch->window_runtime
        ? croft_wit_host_window_runtime_dispatch(dispatch->window_runtime,
                                                 (const SapWitHostWindowCommand *)command,
                                                 (SapWitHostWindowReply *)reply_out)
        : ERR_INVALID;
}

static int native_gpu_dispatch(void *ctx,
                               const void *command,
                               void *reply_out)
{
    CroftJsonViewerNativeDispatch *dispatch = (CroftJsonViewerNativeDispatch *)ctx;

    return dispatch && dispatch->gpu_runtime
        ? croft_wit_host_gpu2d_runtime_dispatch(dispatch->gpu_runtime,
                                                (const SapWitHostGpu2dCommand *)command,
                                                (SapWitHostGpu2dReply *)reply_out)
        : ERR_INVALID;
}

static int native_clock_dispatch(void *ctx,
                                 const void *command,
                                 void *reply_out)
{
    CroftJsonViewerNativeDispatch *dispatch = (CroftJsonViewerNativeDispatch *)ctx;

    return dispatch && dispatch->clock_runtime
        ? croft_wit_host_clock_runtime_dispatch(dispatch->clock_runtime,
                                                (const SapWitHostClockCommand *)command,
                                                (SapWitHostClockReply *)reply_out)
        : ERR_INVALID;
}

int main(int argc, char **argv)
{
    const char *auto_close_env = getenv("CROFT_WIT_JSON_VIEWER_AUTO_CLOSE_MS");
    const char *pick_file_env = getenv("CROFT_WIT_JSON_VIEWER_PICK_FILE");
    const uint8_t *json_data = k_default_json;
    uint32_t json_len = (uint32_t)(sizeof(k_default_json) - 1u);
    uint8_t *file_bytes = NULL;
    char *picked_path = NULL;
    const char *input_path = NULL;
    CroftJsonViewerNativeDispatch dispatch = {0};
    CroftJsonViewerWindowAppConfig app_config = {0};
    CroftJsonViewerWindowAppState app_state = {0};
    croft_wit_host_window_runtime *window_runtime = NULL;
    croft_wit_host_gpu2d_runtime *gpu_runtime = NULL;
    croft_wit_host_clock_runtime *clock_runtime = NULL;
    uint32_t auto_close_ms = 1500u;
    uint32_t frame_count = 0u;
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
            fprintf(stderr, "example_wit_json_viewer_window: unable to read %s\n", input_path);
            host_file_dialog_free_path(picked_path);
            return 1;
        }
        json_data = file_bytes;
    }

    if (auto_close_env && auto_close_env[0] != '\0') {
        int parsed = atoi(auto_close_env);
        if (parsed > 0) {
            auto_close_ms = (uint32_t)parsed;
        }
    }

    window_runtime = croft_wit_host_window_runtime_create();
    gpu_runtime = croft_wit_host_gpu2d_runtime_create();
    clock_runtime = croft_wit_host_clock_runtime_create();
    if (!window_runtime || !gpu_runtime || !clock_runtime) {
        goto cleanup;
    }

    dispatch.window_runtime = window_runtime;
    dispatch.gpu_runtime = gpu_runtime;
    dispatch.clock_runtime = clock_runtime;
    app_config.dispatch_ctx = &dispatch;
    app_config.window_dispatch = native_window_dispatch;
    app_config.gpu_dispatch = native_gpu_dispatch;
    app_config.clock_dispatch = native_clock_dispatch;
    app_config.title_data = (const uint8_t *)"Croft JSON Viewer";
    app_config.title_len = 17u;

    if (croft_json_viewer_window_app_run(&app_state,
                                         &app_config,
                                         json_data,
                                         json_len,
                                         auto_close_ms,
                                         &frame_count) != ERR_OK) {
        fprintf(stderr,
                "example_wit_json_viewer_window: json viewer run failed at byte %u\n",
                app_state.viewer.error_position);
        goto cleanup;
    }

    printf("frames=%u lines=%u selected=%s\n",
           frame_count,
           croft_json_viewer_state_line_count(&app_state.viewer),
           croft_json_viewer_state_selected_path(&app_state.viewer));
    rc = 0;

cleanup:
    croft_wit_host_clock_runtime_destroy(clock_runtime);
    croft_wit_host_gpu2d_runtime_destroy(gpu_runtime);
    croft_wit_host_window_runtime_destroy(window_runtime);
    free(file_bytes);
    host_file_dialog_free_path(picked_path);
    return rc;
}
