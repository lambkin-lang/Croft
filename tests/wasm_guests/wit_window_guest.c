#include "croft/wit_croft_wasm_guest.h"
#include "croft/wit_runtime_support.h"

#include "wit_host_clock.h"
#include "wit_host_gpu2d.h"
#include "wit_host_window.h"

#include <stdint.h>

typedef struct {
    uint32_t initialized;
    SapWitCroftWasmGuestContext guest_ctx;
    SapWitGuestTransport transport;
} CroftWitWindowGuestState;

static CroftWitWindowGuestState g_croft_wit_window_guest;

static int32_t croft_wit_window_guest_init(void)
{
    if (g_croft_wit_window_guest.initialized) {
        return ERR_OK;
    }

    sap_wit_croft_wasm_guest_context_init_default(&g_croft_wit_window_guest.guest_ctx);
    sap_wit_croft_wasm_guest_transport_init(&g_croft_wit_window_guest.transport,
                                            &g_croft_wit_window_guest.guest_ctx);
    g_croft_wit_window_guest.initialized = 1u;
    return ERR_OK;
}

static int32_t croft_wit_window_guest_window_call(const SapWitHostWindowCommand* command,
                                                  SapWitHostWindowReply* reply_out)
{
    return sap_wit_guest_transport_call(&g_croft_wit_window_guest.transport,
                                        &sap_wit_host_window_host_window_export_endpoints[0],
                                        command,
                                        reply_out);
}

static int32_t croft_wit_window_guest_gpu_call(const SapWitHostGpu2dCommand* command,
                                               SapWitHostGpu2dReply* reply_out)
{
    return sap_wit_guest_transport_call(&g_croft_wit_window_guest.transport,
                                        &sap_wit_host_gpu2d_host_gpu2d_export_endpoints[0],
                                        command,
                                        reply_out);
}

static int32_t croft_wit_window_guest_clock_call(const SapWitHostClockCommand* command,
                                                 SapWitHostClockReply* reply_out)
{
    return sap_wit_guest_transport_call(&g_croft_wit_window_guest.transport,
                                        &sap_wit_host_clock_host_clock_export_endpoints[0],
                                        command,
                                        reply_out);
}

static int croft_wit_window_expect_window_ok(const SapWitHostWindowReply* reply,
                                             SapWitHostWindowResource* handle_out)
{
    if (!reply || !handle_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_WINDOW_REPLY_WINDOW || !reply->val.window.is_v_ok) {
        return 0;
    }
    *handle_out = reply->val.window.v_val.ok.v;
    return 1;
}

static int croft_wit_window_expect_window_status_ok(const SapWitHostWindowReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_WINDOW_REPLY_STATUS
        && reply->val.status.is_v_ok;
}

static int croft_wit_window_expect_window_event(const SapWitHostWindowReply* reply)
{
    if (!reply || reply->case_tag != SAP_WIT_HOST_WINDOW_REPLY_EVENT || !reply->val.event.is_v_ok) {
        return -1;
    }
    return reply->val.event.v_val.ok.has_v ? 1 : 0;
}

static int croft_wit_window_expect_window_bool(const SapWitHostWindowReply* reply,
                                               uint8_t* value_out)
{
    if (!reply || !value_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_WINDOW_REPLY_SHOULD_CLOSE
            || !reply->val.should_close.is_v_ok) {
        return 0;
    }
    *value_out = reply->val.should_close.v_val.ok.v;
    return 1;
}

static int croft_wit_window_expect_window_size(const SapWitHostWindowReply* reply,
                                               uint32_t* width_out,
                                               uint32_t* height_out)
{
    if (!reply || !width_out || !height_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_WINDOW_REPLY_SIZE || !reply->val.size.is_v_ok) {
        return 0;
    }
    *width_out = reply->val.size.v_val.ok.v.width;
    *height_out = reply->val.size.v_val.ok.v.height;
    return 1;
}

static int croft_wit_window_expect_surface_ok(const SapWitHostGpu2dReply* reply,
                                              SapWitHostGpu2dSurfaceResource* handle_out)
{
    if (!reply || !handle_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_GPU2D_REPLY_SURFACE || !reply->val.surface.is_v_ok) {
        return 0;
    }
    *handle_out = reply->val.surface.v_val.ok.v;
    return 1;
}

static int croft_wit_window_expect_gpu_status_ok(const SapWitHostGpu2dReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_GPU2D_REPLY_STATUS
        && reply->val.status.is_v_ok;
}

static int croft_wit_window_expect_gpu_caps_ok(const SapWitHostGpu2dReply* reply,
                                               uint32_t* caps_out)
{
    if (!reply || !caps_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_GPU2D_REPLY_CAPABILITIES
            || !reply->val.capabilities.is_v_ok) {
        return 0;
    }
    *caps_out = reply->val.capabilities.v_val.ok.v;
    return 1;
}

static int croft_wit_window_expect_measure_ok(const SapWitHostGpu2dReply* reply,
                                              float* width_out)
{
    if (!reply || !width_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_GPU2D_REPLY_MEASURE || !reply->val.measure.is_v_ok) {
        return 0;
    }
    *width_out = reply->val.measure.v_val.ok.v;
    return 1;
}

static int croft_wit_window_expect_clock_now(const SapWitHostClockReply* reply,
                                             uint64_t* now_out)
{
    if (!reply || !now_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_CLOCK_REPLY_NOW || !reply->val.now.is_v_ok) {
        return 0;
    }
    *now_out = reply->val.now.v_val.ok.v;
    return 1;
}

static int32_t croft_wit_window_guest_run_loop(uint32_t auto_close_ms)
{
    static const uint8_t k_title[] = "Croft Wasm3 Window Host";
    static const uint8_t k_headline[] = "wasm3 window path";
    static const uint8_t k_subtitle[] = "WIT host-window + host-gpu2d";
    SapWitHostWindowResource window = SAP_WIT_HOST_WINDOW_RESOURCE_INVALID;
    SapWitHostGpu2dSurfaceResource surface = SAP_WIT_HOST_GPU2D_SURFACE_RESOURCE_INVALID;
    SapWitHostWindowCommand window_cmd = {0};
    SapWitHostWindowReply window_reply = {0};
    SapWitHostGpu2dCommand gpu_cmd = {0};
    SapWitHostGpu2dReply gpu_reply = {0};
    SapWitHostClockCommand clock_cmd = {0};
    SapWitHostClockReply clock_reply = {0};
    uint32_t caps = 0u;
    uint64_t start_ms = 0u;
    float headline_width = 0.0f;
    int32_t rc = ERR_OK;
    int32_t frame_count = 0;

    rc = croft_wit_window_guest_init();
    if (rc != ERR_OK) {
        return -rc;
    }

    window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_OPEN;
    window_cmd.val.open.width = 960u;
    window_cmd.val.open.height = 640u;
    window_cmd.val.open.title_data = k_title;
    window_cmd.val.open.title_len = (uint32_t)(sizeof(k_title) - 1u);
    rc = croft_wit_window_guest_window_call(&window_cmd, &window_reply);
    if (rc != ERR_OK || !croft_wit_window_expect_window_ok(&window_reply, &window)) {
        sap_wit_dispose_host_window_reply(&window_reply);
        return -(rc != ERR_OK ? rc : ERR_TYPE);
    }
    sap_wit_dispose_host_window_reply(&window_reply);

    gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_CAPABILITIES;
    rc = croft_wit_window_guest_gpu_call(&gpu_cmd, &gpu_reply);
    if (rc != ERR_OK || !croft_wit_window_expect_gpu_caps_ok(&gpu_reply, &caps)) {
        sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
        rc = rc != ERR_OK ? rc : ERR_TYPE;
        goto cleanup;
    }
    sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
    if ((caps & SAP_WIT_HOST_GPU2D_CAPABILITIES_TEXT) == 0u
            || (caps & SAP_WIT_HOST_GPU2D_CAPABILITIES_PRESENT) == 0u) {
        rc = ERR_TYPE;
        goto cleanup;
    }

    gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_OPEN;
    rc = croft_wit_window_guest_gpu_call(&gpu_cmd, &gpu_reply);
    if (rc != ERR_OK || !croft_wit_window_expect_surface_ok(&gpu_reply, &surface)) {
        sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
        rc = rc != ERR_OK ? rc : ERR_TYPE;
        goto cleanup;
    }
    sap_wit_dispose_host_gpu2d_reply(&gpu_reply);

    gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_MEASURE_TEXT;
    gpu_cmd.val.measure_text.surface = surface;
    gpu_cmd.val.measure_text.utf8_data = k_headline;
    gpu_cmd.val.measure_text.utf8_len = (uint32_t)(sizeof(k_headline) - 1u);
    gpu_cmd.val.measure_text.font_size = 34.0f;
    rc = croft_wit_window_guest_gpu_call(&gpu_cmd, &gpu_reply);
    if (rc != ERR_OK || !croft_wit_window_expect_measure_ok(&gpu_reply, &headline_width)) {
        sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
        rc = rc != ERR_OK ? rc : ERR_TYPE;
        goto cleanup;
    }
    sap_wit_dispose_host_gpu2d_reply(&gpu_reply);

    clock_cmd.case_tag = SAP_WIT_HOST_CLOCK_COMMAND_MONOTONIC_NOW;
    rc = croft_wit_window_guest_clock_call(&clock_cmd, &clock_reply);
    if (rc != ERR_OK || !croft_wit_window_expect_clock_now(&clock_reply, &start_ms)) {
        sap_wit_dispose_host_clock_reply(&clock_reply);
        rc = rc != ERR_OK ? rc : ERR_TYPE;
        goto cleanup;
    }
    sap_wit_dispose_host_clock_reply(&clock_reply);

    for (;;) {
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint64_t now_ms = 0u;
        uint8_t should_close = 0u;
        float header_x;

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_POLL;
        window_cmd.val.poll.window = window;
        rc = croft_wit_window_guest_window_call(&window_cmd, &window_reply);
        if (rc != ERR_OK || !croft_wit_window_expect_window_status_ok(&window_reply)) {
            sap_wit_dispose_host_window_reply(&window_reply);
            rc = rc != ERR_OK ? rc : ERR_TYPE;
            goto cleanup;
        }
        sap_wit_dispose_host_window_reply(&window_reply);

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_NEXT_EVENT;
        window_cmd.val.next_event.window = window;
        for (;;) {
            int event_status;

            rc = croft_wit_window_guest_window_call(&window_cmd, &window_reply);
            if (rc != ERR_OK) {
                sap_wit_dispose_host_window_reply(&window_reply);
                goto cleanup;
            }
            event_status = croft_wit_window_expect_window_event(&window_reply);
            sap_wit_dispose_host_window_reply(&window_reply);
            if (event_status <= 0) {
                if (event_status < 0) {
                    rc = ERR_TYPE;
                    goto cleanup;
                }
                break;
            }
        }

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_SHOULD_CLOSE;
        window_cmd.val.should_close.window = window;
        rc = croft_wit_window_guest_window_call(&window_cmd, &window_reply);
        if (rc != ERR_OK || !croft_wit_window_expect_window_bool(&window_reply, &should_close)) {
            sap_wit_dispose_host_window_reply(&window_reply);
            rc = rc != ERR_OK ? rc : ERR_TYPE;
            goto cleanup;
        }
        sap_wit_dispose_host_window_reply(&window_reply);
        if (should_close) {
            break;
        }

        rc = croft_wit_window_guest_clock_call(&clock_cmd, &clock_reply);
        if (rc != ERR_OK || !croft_wit_window_expect_clock_now(&clock_reply, &now_ms)) {
            sap_wit_dispose_host_clock_reply(&clock_reply);
            rc = rc != ERR_OK ? rc : ERR_TYPE;
            goto cleanup;
        }
        sap_wit_dispose_host_clock_reply(&clock_reply);
        if (now_ms - start_ms >= auto_close_ms) {
            break;
        }

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_FRAMEBUFFER_SIZE;
        window_cmd.val.framebuffer_size.window = window;
        rc = croft_wit_window_guest_window_call(&window_cmd, &window_reply);
        if (rc != ERR_OK || !croft_wit_window_expect_window_size(&window_reply, &width, &height)) {
            sap_wit_dispose_host_window_reply(&window_reply);
            rc = rc != ERR_OK ? rc : ERR_TYPE;
            goto cleanup;
        }
        sap_wit_dispose_host_window_reply(&window_reply);

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_BEGIN_FRAME;
        gpu_cmd.val.begin_frame.surface = surface;
        gpu_cmd.val.begin_frame.width = width;
        gpu_cmd.val.begin_frame.height = height;
        rc = croft_wit_window_guest_gpu_call(&gpu_cmd, &gpu_reply);
        if (rc != ERR_OK || !croft_wit_window_expect_gpu_status_ok(&gpu_reply)) {
            sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
            rc = rc != ERR_OK ? rc : ERR_TYPE;
            goto cleanup;
        }
        sap_wit_dispose_host_gpu2d_reply(&gpu_reply);

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_CLEAR;
        gpu_cmd.val.clear.surface = surface;
        gpu_cmd.val.clear.color_rgba = 0xFFF4E7FFu;
        rc = croft_wit_window_guest_gpu_call(&gpu_cmd, &gpu_reply);
        if (rc != ERR_OK || !croft_wit_window_expect_gpu_status_ok(&gpu_reply)) {
            sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
            rc = rc != ERR_OK ? rc : ERR_TYPE;
            goto cleanup;
        }
        sap_wit_dispose_host_gpu2d_reply(&gpu_reply);

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_DRAW_RECT;
        gpu_cmd.val.draw_rect.surface = surface;
        gpu_cmd.val.draw_rect.x = 24.0f;
        gpu_cmd.val.draw_rect.y = 24.0f;
        gpu_cmd.val.draw_rect.w = (float)width - 48.0f;
        gpu_cmd.val.draw_rect.h = 112.0f;
        gpu_cmd.val.draw_rect.color_rgba = 0x203D5BFFu;
        rc = croft_wit_window_guest_gpu_call(&gpu_cmd, &gpu_reply);
        if (rc != ERR_OK || !croft_wit_window_expect_gpu_status_ok(&gpu_reply)) {
            sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
            rc = rc != ERR_OK ? rc : ERR_TYPE;
            goto cleanup;
        }
        sap_wit_dispose_host_gpu2d_reply(&gpu_reply);

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_DRAW_RECT;
        gpu_cmd.val.draw_rect.surface = surface;
        gpu_cmd.val.draw_rect.x = 24.0f;
        gpu_cmd.val.draw_rect.y = 164.0f;
        gpu_cmd.val.draw_rect.w = (float)width - 48.0f;
        gpu_cmd.val.draw_rect.h = (float)height - 188.0f;
        gpu_cmd.val.draw_rect.color_rgba = 0xFFFDF8FFu;
        rc = croft_wit_window_guest_gpu_call(&gpu_cmd, &gpu_reply);
        if (rc != ERR_OK || !croft_wit_window_expect_gpu_status_ok(&gpu_reply)) {
            sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
            rc = rc != ERR_OK ? rc : ERR_TYPE;
            goto cleanup;
        }
        sap_wit_dispose_host_gpu2d_reply(&gpu_reply);

        header_x = ((float)width - headline_width) * 0.5f;
        if (header_x < 36.0f) {
            header_x = 36.0f;
        }

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_DRAW_TEXT;
        gpu_cmd.val.draw_text.surface = surface;
        gpu_cmd.val.draw_text.x = header_x;
        gpu_cmd.val.draw_text.y = 74.0f;
        gpu_cmd.val.draw_text.utf8_data = k_headline;
        gpu_cmd.val.draw_text.utf8_len = (uint32_t)(sizeof(k_headline) - 1u);
        gpu_cmd.val.draw_text.font_size = 34.0f;
        gpu_cmd.val.draw_text.color_rgba = 0xFFF4E7FFu;
        rc = croft_wit_window_guest_gpu_call(&gpu_cmd, &gpu_reply);
        if (rc != ERR_OK || !croft_wit_window_expect_gpu_status_ok(&gpu_reply)) {
            sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
            rc = rc != ERR_OK ? rc : ERR_TYPE;
            goto cleanup;
        }
        sap_wit_dispose_host_gpu2d_reply(&gpu_reply);

        gpu_cmd.val.draw_text.x = 52.0f;
        gpu_cmd.val.draw_text.y = 228.0f;
        gpu_cmd.val.draw_text.utf8_data = k_subtitle;
        gpu_cmd.val.draw_text.utf8_len = (uint32_t)(sizeof(k_subtitle) - 1u);
        gpu_cmd.val.draw_text.font_size = 24.0f;
        gpu_cmd.val.draw_text.color_rgba = 0x203D5BFFu;
        rc = croft_wit_window_guest_gpu_call(&gpu_cmd, &gpu_reply);
        if (rc != ERR_OK || !croft_wit_window_expect_gpu_status_ok(&gpu_reply)) {
            sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
            rc = rc != ERR_OK ? rc : ERR_TYPE;
            goto cleanup;
        }
        sap_wit_dispose_host_gpu2d_reply(&gpu_reply);

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_END_FRAME;
        gpu_cmd.val.end_frame.surface = surface;
        rc = croft_wit_window_guest_gpu_call(&gpu_cmd, &gpu_reply);
        if (rc != ERR_OK || !croft_wit_window_expect_gpu_status_ok(&gpu_reply)) {
            sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
            rc = rc != ERR_OK ? rc : ERR_TYPE;
            goto cleanup;
        }
        sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
        frame_count++;
    }

cleanup:
    if (surface != SAP_WIT_HOST_GPU2D_SURFACE_RESOURCE_INVALID) {
        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_DROP;
        gpu_cmd.val.drop.surface = surface;
        if (croft_wit_window_guest_gpu_call(&gpu_cmd, &gpu_reply) == ERR_OK) {
            sap_wit_dispose_host_gpu2d_reply(&gpu_reply);
        }
    }
    if (window != SAP_WIT_HOST_WINDOW_RESOURCE_INVALID) {
        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_CLOSE;
        window_cmd.val.close.window = window;
        if (croft_wit_window_guest_window_call(&window_cmd, &window_reply) == ERR_OK) {
            sap_wit_dispose_host_window_reply(&window_reply);
        }
    }

    if (rc != ERR_OK) {
        return -rc;
    }
    return frame_count;
}

__attribute__((export_name("wit_guest_window_demo_run")))
int32_t wit_guest_window_demo_run(int32_t auto_close_ms)
{
    uint32_t effective_auto_close_ms = auto_close_ms > 0 ? (uint32_t)auto_close_ms : 450u;

    return croft_wit_window_guest_run_loop(effective_auto_close_ms);
}
