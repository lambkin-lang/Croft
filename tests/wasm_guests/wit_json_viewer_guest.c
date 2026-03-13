#include "croft/json_viewer_window_app.h"
#include "croft/wit_croft_wasm_guest.h"

#include "wit_host_clock.h"
#include "wit_host_gpu2d.h"
#include "wit_host_window.h"

#include <stdint.h>

typedef struct {
    uint32_t initialized;
    SapWitCroftWasmGuestContext guest_ctx;
    SapWitGuestTransport transport;
    CroftJsonViewerWindowAppState app;
} CroftWitJsonViewerGuestState;

static CroftWitJsonViewerGuestState g_croft_wit_json_viewer_guest;

enum {
    CROFT_GUEST_JSON_VIEWER_AUTO_CLOSE_DEFAULT = 1500
};

static int32_t croft_wit_json_viewer_guest_init(void)
{
    if (g_croft_wit_json_viewer_guest.initialized) {
        return ERR_OK;
    }

    sap_wit_croft_wasm_guest_context_init_default(&g_croft_wit_json_viewer_guest.guest_ctx);
    sap_wit_croft_wasm_guest_transport_init(&g_croft_wit_json_viewer_guest.transport,
                                            &g_croft_wit_json_viewer_guest.guest_ctx);
    g_croft_wit_json_viewer_guest.initialized = 1u;
    return ERR_OK;
}

static int window_call(void *ctx,
                       const void *command,
                       void *reply_out)
{
    CroftWitJsonViewerGuestState *state = (CroftWitJsonViewerGuestState *)ctx;

    return sap_wit_guest_transport_call(&state->transport,
                                        &sap_wit_host_window_host_window_export_endpoints[0],
                                        (const SapWitHostWindowCommand *)command,
                                        (SapWitHostWindowReply *)reply_out);
}

static int gpu_call(void *ctx,
                    const void *command,
                    void *reply_out)
{
    CroftWitJsonViewerGuestState *state = (CroftWitJsonViewerGuestState *)ctx;

    return sap_wit_guest_transport_call(&state->transport,
                                        &sap_wit_host_gpu2d_host_gpu2d_export_endpoints[0],
                                        (const SapWitHostGpu2dCommand *)command,
                                        (SapWitHostGpu2dReply *)reply_out);
}

static int clock_call(void *ctx,
                      const void *command,
                      void *reply_out)
{
    CroftWitJsonViewerGuestState *state = (CroftWitJsonViewerGuestState *)ctx;

    return sap_wit_guest_transport_call(&state->transport,
                                        &sap_wit_host_clock_host_clock_export_endpoints[0],
                                        (const SapWitHostClockCommand *)command,
                                        (SapWitHostClockReply *)reply_out);
}

static int32_t croft_wit_json_viewer_guest_run(const uint8_t *json,
                                               uint32_t json_len,
                                               uint32_t auto_close_ms)
{
    CroftJsonViewerWindowAppConfig app_config = {0};
    uint32_t frame_count = 0u;
    int32_t rc = ERR_OK;

    rc = croft_wit_json_viewer_guest_init();
    if (rc != ERR_OK) {
        return -rc;
    }

    app_config.dispatch_ctx = &g_croft_wit_json_viewer_guest;
    app_config.window_dispatch = window_call;
    app_config.gpu_dispatch = gpu_call;
    app_config.clock_dispatch = clock_call;
    app_config.title_data = (const uint8_t *)"Croft Wasm JSON Viewer";
    app_config.title_len = 22u;

    rc = croft_json_viewer_window_app_run(&g_croft_wit_json_viewer_guest.app,
                                          &app_config,
                                          json,
                                          json_len,
                                          auto_close_ms,
                                          &frame_count);
    return rc == ERR_OK ? (int32_t)frame_count : -rc;
}

__attribute__((export_name("wit_guest_json_viewer_run")))
int32_t wit_guest_json_viewer_run(int32_t json_ptr, int32_t json_len, int32_t auto_close_ms)
{
    if (json_ptr <= 0 || json_len <= 0) {
        return -ERR_INVALID;
    }
    return croft_wit_json_viewer_guest_run((const uint8_t *)(uintptr_t)(uint32_t)json_ptr,
                                           (uint32_t)json_len,
                                           auto_close_ms > 0
                                               ? (uint32_t)auto_close_ms
                                               : (uint32_t)CROFT_GUEST_JSON_VIEWER_AUTO_CLOSE_DEFAULT);
}
