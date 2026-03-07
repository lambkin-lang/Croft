#include "croft/wit_host_clock_runtime.h"
#include "croft/wit_host_gpu2d_runtime.h"
#include "croft/wit_host_window_runtime.h"

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
            || reply->val.window.case_tag != SAP_WIT_HOST_WINDOW_OP_RESULT_OK) {
        return 0;
    }
    *handle_out = reply->val.window.val.ok;
    return 1;
}

static int expect_surface_ok(const SapWitHostGpu2dReply* reply,
                             SapWitHostGpu2dSurfaceResource* handle_out)
{
    if (!reply || !handle_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_GPU2D_REPLY_SURFACE
            || reply->val.surface.case_tag != SAP_WIT_HOST_GPU2D_SURFACE_RESULT_OK) {
        return 0;
    }
    *handle_out = reply->val.surface.val.ok;
    return 1;
}

static int expect_gpu_status_ok(const SapWitHostGpu2dReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_GPU2D_REPLY_STATUS
        && reply->val.status.case_tag == SAP_WIT_HOST_GPU2D_STATUS_OK;
}

static int expect_gpu_caps_ok(const SapWitHostGpu2dReply* reply, uint32_t* caps_out)
{
    if (!reply || !caps_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_GPU2D_REPLY_CAPABILITIES
            || reply->val.capabilities.case_tag != SAP_WIT_HOST_GPU2D_CAPABILITIES_RESULT_OK) {
        return 0;
    }
    *caps_out = reply->val.capabilities.val.ok;
    return 1;
}

static int expect_measure_ok(const SapWitHostGpu2dReply* reply, float* width_out)
{
    if (!reply || !width_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_GPU2D_REPLY_MEASURE
            || reply->val.measure.case_tag != SAP_WIT_HOST_GPU2D_MEASURE_RESULT_OK) {
        return 0;
    }
    *width_out = reply->val.measure.val.ok;
    return 1;
}

static int expect_clock_now(const SapWitHostClockReply* reply, uint64_t* now_out)
{
    if (!reply || !now_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_CLOCK_REPLY_NOW
            || reply->val.now.case_tag != SAP_WIT_HOST_CLOCK_NOW_RESULT_OK) {
        return 0;
    }
    *now_out = reply->val.now.val.ok;
    return 1;
}

int main(void)
{
    const char* auto_close_env = getenv("CROFT_WIT_GPU_AUTO_CLOSE_MS");
    const char* label = "Croft WIT GPU";
    croft_wit_host_window_runtime* window_runtime = NULL;
    croft_wit_host_gpu2d_runtime* gpu_runtime = NULL;
    croft_wit_host_clock_runtime* clock_runtime = NULL;
    SapWitHostWindowResource window = SAP_WIT_HOST_WINDOW_RESOURCE_INVALID;
    SapWitHostGpu2dSurfaceResource surface = SAP_WIT_HOST_GPU2D_SURFACE_RESOURCE_INVALID;
    SapWitHostWindowCommand window_cmd = {0};
    SapWitHostWindowReply window_reply = {0};
    SapWitHostGpu2dCommand gpu_cmd = {0};
    SapWitHostGpu2dReply gpu_reply = {0};
    SapWitHostClockCommand clock_cmd = {0};
    SapWitHostClockReply clock_reply = {0};
    uint32_t caps = 0u;
    uint32_t auto_close_ms = 300u;
    float text_width = 0.0f;
    uint64_t start_ms = 0u;
    uint32_t frame_count = 0u;

    window_runtime = croft_wit_host_window_runtime_create();
    gpu_runtime = croft_wit_host_gpu2d_runtime_create();
    clock_runtime = croft_wit_host_clock_runtime_create();
    if (!window_runtime || !gpu_runtime || !clock_runtime) {
        croft_wit_host_clock_runtime_destroy(clock_runtime);
        croft_wit_host_gpu2d_runtime_destroy(gpu_runtime);
        croft_wit_host_window_runtime_destroy(window_runtime);
        return 1;
    }

    window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_OPEN;
    window_cmd.val.open.width = 800u;
    window_cmd.val.open.height = 480u;
    window_cmd.val.open.title_data = (const uint8_t*)"Croft WIT GPU Canvas";
    window_cmd.val.open.title_len = (uint32_t)strlen("Croft WIT GPU Canvas");
    if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) != 0
            || !expect_window_ok(&window_reply, &window)) {
        croft_wit_host_window_runtime_destroy(window_runtime);
        croft_wit_host_gpu2d_runtime_destroy(gpu_runtime);
        croft_wit_host_clock_runtime_destroy(clock_runtime);
        return 1;
    }

    gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_CAPABILITIES;
    if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
            || !expect_gpu_caps_ok(&gpu_reply, &caps)) {
        croft_wit_host_window_runtime_destroy(window_runtime);
        croft_wit_host_gpu2d_runtime_destroy(gpu_runtime);
        croft_wit_host_clock_runtime_destroy(clock_runtime);
        return 1;
    }
    if ((caps & SAP_WIT_HOST_GPU2D_CAPABILITIES_TEXT) == 0u
            || (caps & SAP_WIT_HOST_GPU2D_CAPABILITIES_PRESENT) == 0u) {
        croft_wit_host_window_runtime_destroy(window_runtime);
        croft_wit_host_gpu2d_runtime_destroy(gpu_runtime);
        croft_wit_host_clock_runtime_destroy(clock_runtime);
        return 1;
    }

    if (auto_close_env && auto_close_env[0] != '\0') {
        int parsed = atoi(auto_close_env);
        if (parsed > 0) {
            auto_close_ms = (uint32_t)parsed;
        }
    }

    gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_OPEN;
    if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
            || !expect_surface_ok(&gpu_reply, &surface)) {
        croft_wit_host_window_runtime_destroy(window_runtime);
        croft_wit_host_gpu2d_runtime_destroy(gpu_runtime);
        croft_wit_host_clock_runtime_destroy(clock_runtime);
        return 1;
    }

    gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_MEASURE_TEXT;
    gpu_cmd.val.measure_text.surface = surface;
    gpu_cmd.val.measure_text.utf8_data = (const uint8_t*)label;
    gpu_cmd.val.measure_text.utf8_len = (uint32_t)strlen(label);
    gpu_cmd.val.measure_text.font_size = 28.0f;
    if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
            || !expect_measure_ok(&gpu_reply, &text_width)) {
        croft_wit_host_window_runtime_destroy(window_runtime);
        croft_wit_host_gpu2d_runtime_destroy(gpu_runtime);
        croft_wit_host_clock_runtime_destroy(clock_runtime);
        return 1;
    }

    clock_cmd.case_tag = SAP_WIT_HOST_CLOCK_COMMAND_MONOTONIC_NOW;
    if (croft_wit_host_clock_runtime_dispatch(clock_runtime, &clock_cmd, &clock_reply) != 0
            || !expect_clock_now(&clock_reply, &start_ms)) {
        croft_wit_host_window_runtime_destroy(window_runtime);
        croft_wit_host_gpu2d_runtime_destroy(gpu_runtime);
        croft_wit_host_clock_runtime_destroy(clock_runtime);
        return 1;
    }

    while (1) {
        uint64_t now_ms = 0u;
        uint32_t width = 0u;
        uint32_t height = 0u;
        float pulse = (float)(frame_count % 60u) / 59.0f;
        float inset = 48.0f + pulse * 96.0f;

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_POLL;
        window_cmd.val.poll.window = window;
        if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) != 0) {
            break;
        }

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_SHOULD_CLOSE;
        window_cmd.val.should_close.window = window;
        if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) != 0) {
            break;
        }
        if (window_reply.case_tag == SAP_WIT_HOST_WINDOW_REPLY_SHOULD_CLOSE
                && window_reply.val.should_close.case_tag == SAP_WIT_HOST_WINDOW_BOOL_RESULT_OK
                && window_reply.val.should_close.val.ok) {
            break;
        }

        clock_cmd.case_tag = SAP_WIT_HOST_CLOCK_COMMAND_MONOTONIC_NOW;
        if (croft_wit_host_clock_runtime_dispatch(clock_runtime, &clock_cmd, &clock_reply) != 0
                || !expect_clock_now(&clock_reply, &now_ms)) {
            break;
        }
        if (now_ms - start_ms >= (uint64_t)auto_close_ms) {
            break;
        }

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_FRAMEBUFFER_SIZE;
        window_cmd.val.framebuffer_size.window = window;
        if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) != 0
                || window_reply.case_tag != SAP_WIT_HOST_WINDOW_REPLY_SIZE
                || window_reply.val.size.case_tag != SAP_WIT_HOST_WINDOW_SIZE_RESULT_OK) {
            break;
        }
        width = window_reply.val.size.val.ok.width;
        height = window_reply.val.size.val.ok.height;

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_BEGIN_FRAME;
        gpu_cmd.val.begin_frame.surface = surface;
        gpu_cmd.val.begin_frame.width = width;
        gpu_cmd.val.begin_frame.height = height;
        if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                || !expect_gpu_status_ok(&gpu_reply)) {
            break;
        }

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_CLEAR;
        gpu_cmd.val.clear.surface = surface;
        gpu_cmd.val.clear.color_rgba = 0xF2F4F8FF;
        if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                || !expect_gpu_status_ok(&gpu_reply)) {
            break;
        }

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_DRAW_RECT;
        gpu_cmd.val.draw_rect.surface = surface;
        gpu_cmd.val.draw_rect.x = 48.0f;
        gpu_cmd.val.draw_rect.y = 56.0f;
        gpu_cmd.val.draw_rect.w = 256.0f;
        gpu_cmd.val.draw_rect.h = 132.0f;
        gpu_cmd.val.draw_rect.color_rgba = 0xD7263DFF;
        if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                || !expect_gpu_status_ok(&gpu_reply)) {
            break;
        }

        gpu_cmd.val.draw_rect.x = inset;
        gpu_cmd.val.draw_rect.y = 244.0f;
        gpu_cmd.val.draw_rect.w = 320.0f;
        gpu_cmd.val.draw_rect.h = 24.0f;
        gpu_cmd.val.draw_rect.color_rgba = 0x1B998BFF;
        if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                || !expect_gpu_status_ok(&gpu_reply)) {
            break;
        }

        gpu_cmd.val.draw_rect.x = 420.0f;
        gpu_cmd.val.draw_rect.y = 112.0f;
        gpu_cmd.val.draw_rect.w = 220.0f;
        gpu_cmd.val.draw_rect.h = 220.0f;
        gpu_cmd.val.draw_rect.color_rgba = 0x2E294EFF;
        if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                || !expect_gpu_status_ok(&gpu_reply)) {
            break;
        }

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_DRAW_TEXT;
        gpu_cmd.val.draw_text.surface = surface;
        gpu_cmd.val.draw_text.x = (width > (uint32_t)(text_width + 96.0f))
                                  ? (((float)width - text_width) * 0.5f)
                                  : 64.0f;
        gpu_cmd.val.draw_text.y = 188.0f;
        gpu_cmd.val.draw_text.utf8_data = (const uint8_t*)label;
        gpu_cmd.val.draw_text.utf8_len = (uint32_t)strlen(label);
        gpu_cmd.val.draw_text.font_size = 28.0f;
        gpu_cmd.val.draw_text.color_rgba = 0x102A43FF;
        if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                || !expect_gpu_status_ok(&gpu_reply)) {
            break;
        }

        gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_END_FRAME;
        gpu_cmd.val.end_frame.surface = surface;
        if (croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply) != 0
                || !expect_gpu_status_ok(&gpu_reply)) {
            break;
        }

        frame_count++;
    }

    gpu_cmd.case_tag = SAP_WIT_HOST_GPU2D_COMMAND_DROP;
    gpu_cmd.val.drop.surface = surface;
    if (surface != SAP_WIT_HOST_GPU2D_SURFACE_RESOURCE_INVALID) {
        croft_wit_host_gpu2d_runtime_dispatch(gpu_runtime, &gpu_cmd, &gpu_reply);
    }

    window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_CLOSE;
    window_cmd.val.close.window = window;
    if (window != SAP_WIT_HOST_WINDOW_RESOURCE_INVALID) {
        croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply);
    }

    printf("gpu_caps=0x%X text_width=%.2f frames=%u\n", caps, text_width, frame_count);

    croft_wit_host_clock_runtime_destroy(clock_runtime);
    croft_wit_host_gpu2d_runtime_destroy(gpu_runtime);
    croft_wit_host_window_runtime_destroy(window_runtime);
    return 0;
}
