#include "croft/wit_host_clock_runtime.h"
#include "croft/wit_host_gpu2d_runtime.h"
#include "croft/wit_host_window_runtime.h"
#include "croft/wit_text_program.h"
#include "croft/wit_text_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int expect_window_ok(const SapWitHostWindowReply* reply,
                            SapWitHostWindowResource* handle_out)
{
    if (!reply || !handle_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_WINDOW_REPLY_WINDOW
            || !reply->val.window.is_v_ok) {
        return 0;
    }
    *handle_out = reply->val.window.v_val.ok.v;
    return 1;
}

static int expect_window_status_ok(const SapWitHostWindowReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_WINDOW_REPLY_STATUS
        && reply->val.status.is_v_ok;
}

static int expect_surface_ok(const SapWitHostGpu2dReply* reply,
                             SapWitHostGpu2dSurfaceResource* handle_out)
{
    if (!reply || !handle_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_GPU2D_REPLY_SURFACE
            || !reply->val.surface.is_v_ok) {
        return 0;
    }
    *handle_out = reply->val.surface.v_val.ok.v;
    return 1;
}

static int expect_gpu_status_ok(const SapWitHostGpu2dReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_GPU2D_REPLY_STATUS
        && reply->val.status.is_v_ok;
}

static int expect_gpu_caps_ok(const SapWitHostGpu2dReply* reply, uint32_t* caps_out)
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

static int expect_measure_ok(const SapWitHostGpu2dReply* reply, float* width_out)
{
    if (!reply || !width_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_GPU2D_REPLY_MEASURE
            || !reply->val.measure.is_v_ok) {
        return 0;
    }
    *width_out = reply->val.measure.v_val.ok.v;
    return 1;
}

static int expect_clock_now(const SapWitHostClockReply* reply, uint64_t* now_out)
{
    if (!reply || !now_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_CLOCK_REPLY_NOW
            || !reply->val.now.is_v_ok) {
        return 0;
    }
    *now_out = reply->val.now.v_val.ok.v;
    return 1;
}

static int expect_window_bool(const SapWitHostWindowReply* reply, uint8_t* value_out)
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

static int expect_window_size(const SapWitHostWindowReply* reply,
                              uint32_t* width_out,
                              uint32_t* height_out)
{
    if (!reply || !width_out || !height_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_WINDOW_REPLY_SIZE
            || !reply->val.size.is_v_ok) {
        return 0;
    }
    *width_out = reply->val.size.v_val.ok.v.width;
    *height_out = reply->val.size.v_val.ok.v.height;
    return 1;
}

int main(void)
{
    const char* base = "small binaries";
    const char* prefix = "Big analysis, ";
    const char* title = "Croft WIT Text Window";
    const char* auto_close_env = getenv("CROFT_WIT_TEXT_WINDOW_AUTO_CLOSE_MS");
    croft_wit_text_runtime* text_runtime = NULL;
    croft_wit_host_window_runtime* window_runtime = NULL;
    croft_wit_host_gpu2d_runtime* gpu_runtime = NULL;
    croft_wit_host_clock_runtime* clock_runtime = NULL;
    croft_wit_text_program_host text_host = {0};
    croft_wit_owned_bytes label = {0};
    SapWitHostWindowResource window = SAP_WIT_HOST_WINDOW_RESOURCE_INVALID;
    SapWitHostGpu2dSurfaceResource surface = SAP_WIT_HOST_GPU2D_SURFACE_RESOURCE_INVALID;
    SapWitHostWindowCommand window_cmd = {0};
    SapWitHostWindowReply window_reply = {0};
    SapWitHostGpu2dCommand gpu_cmd = {0};
    SapWitHostGpu2dReply gpu_reply = {0};
    SapWitHostClockCommand clock_cmd = {0};
    SapWitHostClockReply clock_reply = {0};
    uint32_t caps = 0u;
    uint32_t auto_close_ms = 350u;
    uint64_t start_ms = 0u;
    uint64_t end_ms = 0u;
    float text_width = 0.0f;
    uint32_t frame_count = 0u;
    int rc = 1;

    if (auto_close_env && auto_close_env[0] != '\0') {
        int parsed = atoi(auto_close_env);
        if (parsed > 0) {
            auto_close_ms = (uint32_t)parsed;
        }
    }

    text_runtime = croft_wit_text_runtime_create(NULL);
    window_runtime = croft_wit_host_window_runtime_create();
    gpu_runtime = croft_wit_host_gpu2d_runtime_create();
    clock_runtime = croft_wit_host_clock_runtime_create();
    if (!text_runtime || !window_runtime || !gpu_runtime || !clock_runtime) {
        goto cleanup;
    }

    text_host.userdata = text_runtime;
    text_host.dispatch = (croft_wit_text_program_dispatch_fn)croft_wit_text_runtime_dispatch;
    text_host.dispose_reply = croft_wit_text_reply_dispose;
    if (croft_wit_text_program_prepend(&text_host,
                                       (const uint8_t*)base,
                                       (uint32_t)strlen(base),
                                       (const uint8_t*)prefix,
                                       (uint32_t)strlen(prefix),
                                       &label) != 0) {
        goto cleanup;
    }

    window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_OPEN;
    window_cmd.val.open.width = 900u;
    window_cmd.val.open.height = 520u;
    window_cmd.val.open.title_data = (const uint8_t*)title;
    window_cmd.val.open.title_len = (uint32_t)strlen(title);
    if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) != 0
            || !expect_window_ok(&window_reply, &window)) {
        goto cleanup;
    }

    gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_CAPABILITIES;
    if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
            || !expect_gpu_caps_ok(&gpu_reply, &caps)) {
        goto cleanup;
    }
    if ((caps & SAP_WIT_HOST_GPU2D_CAPABILITIES_TEXT) == 0u
            || (caps & SAP_WIT_HOST_GPU2D_CAPABILITIES_PRESENT) == 0u) {
        goto cleanup;
    }

    gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_OPEN;
    if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
            || !expect_surface_ok(&gpu_reply, &surface)) {
        goto cleanup;
    }

    gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_MEASURE_TEXT;
    gpu_cmd.val.measure_text.surface = surface;
    gpu_cmd.val.measure_text.utf8_data = label.data;
    gpu_cmd.val.measure_text.utf8_len = label.len;
    gpu_cmd.val.measure_text.font_size = 30.0f;
    if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
            || !expect_measure_ok(&gpu_reply, &text_width)) {
        goto cleanup;
    }

    clock_cmd.case_tag = SAP_WIT_HOST_CLOCK_COMMAND_MONOTONIC_NOW;
    if (croft_wit_host_clock_runtime_dispatch(clock_runtime, &clock_cmd, &clock_reply) != 0
            || !expect_clock_now(&clock_reply, &start_ms)) {
        goto cleanup;
    }

    for (;;) {
        uint64_t now_ms = 0u;
        uint8_t should_close = 0u;
        uint32_t width = 0u;
        uint32_t height = 0u;

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_POLL;
        window_cmd.val.poll.window = window;
        if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) != 0
                || !expect_window_status_ok(&window_reply)) {
            goto cleanup;
        }

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_SHOULD_CLOSE;
        window_cmd.val.should_close.window = window;
        if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) != 0
                || !expect_window_bool(&window_reply, &should_close)) {
            goto cleanup;
        }
        if (should_close) {
            break;
        }

        if (croft_wit_host_clock_runtime_dispatch(clock_runtime, &clock_cmd, &clock_reply) != 0
                || !expect_clock_now(&clock_reply, &now_ms)) {
            goto cleanup;
        }
        if (now_ms - start_ms >= (uint64_t)auto_close_ms) {
            break;
        }

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_FRAMEBUFFER_SIZE;
        window_cmd.val.framebuffer_size.window = window;
        if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) != 0
                || !expect_window_size(&window_reply, &width, &height)) {
            goto cleanup;
        }

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_BEGIN_FRAME;
        gpu_cmd.val.begin_frame.surface = surface;
        gpu_cmd.val.begin_frame.width = width;
        gpu_cmd.val.begin_frame.height = height;
        if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                || !expect_gpu_status_ok(&gpu_reply)) {
            goto cleanup;
        }

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_CLEAR;
        gpu_cmd.val.clear.surface = surface;
        gpu_cmd.val.clear.color_rgba = 0xF2F4F8FF;
        if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                || !expect_gpu_status_ok(&gpu_reply)) {
            goto cleanup;
        }

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_DRAW_RECT;
        gpu_cmd.val.draw_rect.surface = surface;
        gpu_cmd.val.draw_rect.x = 72.0f;
        gpu_cmd.val.draw_rect.y = 72.0f;
        gpu_cmd.val.draw_rect.w = (float)width - 144.0f;
        gpu_cmd.val.draw_rect.h = 96.0f;
        gpu_cmd.val.draw_rect.color_rgba = 0xD9E2ECFF;
        if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                || !expect_gpu_status_ok(&gpu_reply)) {
            goto cleanup;
        }

        gpu_cmd.val.draw_rect.x = 72.0f;
        gpu_cmd.val.draw_rect.y = 200.0f;
        gpu_cmd.val.draw_rect.w = (float)width - 144.0f;
        gpu_cmd.val.draw_rect.h = 184.0f;
        gpu_cmd.val.draw_rect.color_rgba = 0xBCCCDCFF;
        if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                || !expect_gpu_status_ok(&gpu_reply)) {
            goto cleanup;
        }

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_DRAW_TEXT;
        gpu_cmd.val.draw_text.surface = surface;
        gpu_cmd.val.draw_text.x = (width > (uint32_t)(text_width + 144.0f))
                                  ? (((float)width - text_width) * 0.5f)
                                  : 72.0f;
        gpu_cmd.val.draw_text.y = 136.0f;
        gpu_cmd.val.draw_text.utf8_data = label.data;
        gpu_cmd.val.draw_text.utf8_len = label.len;
        gpu_cmd.val.draw_text.font_size = 30.0f;
        gpu_cmd.val.draw_text.color_rgba = 0x102A43FF;
        if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                || !expect_gpu_status_ok(&gpu_reply)) {
            goto cleanup;
        }

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_END_FRAME;
        gpu_cmd.val.end_frame.surface = surface;
        if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                || !expect_gpu_status_ok(&gpu_reply)) {
            goto cleanup;
        }

        frame_count++;
    }

    end_ms = start_ms;
    if (croft_wit_host_clock_runtime_dispatch(clock_runtime, &clock_cmd, &clock_reply) == 0
            && expect_clock_now(&clock_reply, &end_ms)) {
    }

    printf("window-text=\"%.*s\" frames=%u wall_ms=%llu\n",
           (int)label.len,
           (const char*)label.data,
           frame_count,
           (unsigned long long)(end_ms - start_ms));
    rc = 0;

cleanup:
    if (surface != SAP_WIT_HOST_GPU2D_SURFACE_RESOURCE_INVALID) {
        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_DROP;
        gpu_cmd.val.drop.surface = surface;
        croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply);
    }
    if (window != SAP_WIT_HOST_WINDOW_RESOURCE_INVALID) {
        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_CLOSE;
        window_cmd.val.close.window = window;
        croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply);
    }

    croft_wit_owned_bytes_dispose(&label);
    croft_wit_host_clock_runtime_destroy(clock_runtime);
    croft_wit_host_gpu2d_runtime_destroy(gpu_runtime);
    croft_wit_host_window_runtime_destroy(window_runtime);
    croft_wit_text_runtime_destroy(text_runtime);
    return rc;
}
