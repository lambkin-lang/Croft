#include "croft/wit_host_clock_runtime.h"
#include "croft/wit_host_window_runtime.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int expect_window_ok(const SapWitHostWindowReply* reply, SapWitHostWindowResource* handle_out)
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

static int expect_status_ok(const SapWitHostWindowReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_WINDOW_REPLY_STATUS
        && reply->val.status.is_v_ok;
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

static int expect_window_event(const SapWitHostWindowReply* reply, SapWitHostWindowEvent* event_out)
{
    if (!reply || !event_out) {
        return -1;
    }
    if (reply->case_tag != SAP_WIT_HOST_WINDOW_REPLY_EVENT) {
        return -1;
    }
    if (!reply->val.event.is_v_ok) {
        return -1;
    }
    if (!reply->val.event.v_val.ok.has_v) {
        return 0;
    }
    *event_out = reply->val.event.v_val.ok.v;
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
    croft_wit_host_window_runtime* window_runtime;
    croft_wit_host_clock_runtime* clock_runtime;
    SapWitHostWindowCommand window_cmd = {0};
    SapWitHostWindowReply window_reply = {0};
    SapWitHostClockCommand clock_cmd = {0};
    SapWitHostClockReply clock_reply = {0};
    SapWitHostWindowResource window = SAP_WIT_HOST_WINDOW_RESOURCE_INVALID;
    uint64_t start_ms = 0u;
    uint64_t now_ms = 0u;
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint32_t event_count = 0u;

    window_runtime = croft_wit_host_window_runtime_create();
    clock_runtime = croft_wit_host_clock_runtime_create();
    if (!window_runtime || !clock_runtime) {
        fprintf(stderr, "example_wit_window_events: runtime init failed\n");
        croft_wit_host_window_runtime_destroy(window_runtime);
        croft_wit_host_clock_runtime_destroy(clock_runtime);
        return 1;
    }

    window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_OPEN;
    window_cmd.val.open.width = 640u;
    window_cmd.val.open.height = 400u;
    window_cmd.val.open.title_data = (const uint8_t*)"Croft WIT Window";
    window_cmd.val.open.title_len = 16u;
    if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) != 0
            || !expect_window_ok(&window_reply, &window)) {
        fprintf(stderr, "example_wit_window_events: open failed\n");
        croft_wit_host_window_runtime_destroy(window_runtime);
        croft_wit_host_clock_runtime_destroy(clock_runtime);
        return 1;
    }

    clock_cmd.case_tag = SAP_WIT_HOST_CLOCK_COMMAND_MONOTONIC_NOW;
    if (croft_wit_host_clock_runtime_dispatch(clock_runtime, &clock_cmd, &clock_reply) != 0
            || !expect_clock_now(&clock_reply, &start_ms)) {
        fprintf(stderr, "example_wit_window_events: clock start failed\n");
        croft_wit_host_window_runtime_destroy(window_runtime);
        croft_wit_host_clock_runtime_destroy(clock_runtime);
        return 1;
    }

    for (;;) {
        SapWitHostWindowEvent event = {0};
        uint8_t should_close = 0u;

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_POLL;
        window_cmd.val.poll.window = window;
        if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) != 0
                || !expect_status_ok(&window_reply)) {
            fprintf(stderr, "example_wit_window_events: poll failed\n");
            croft_wit_host_window_runtime_destroy(window_runtime);
            croft_wit_host_clock_runtime_destroy(clock_runtime);
            return 1;
        }

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_NEXT_EVENT;
        window_cmd.val.next_event.window = window;
        while (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) == 0
                && expect_window_event(&window_reply, &event) > 0) {
            event_count++;
        }

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_SHOULD_CLOSE;
        window_cmd.val.should_close.window = window;
        if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) != 0
                || !expect_window_bool(&window_reply, &should_close)) {
            fprintf(stderr, "example_wit_window_events: should-close failed\n");
            croft_wit_host_window_runtime_destroy(window_runtime);
            croft_wit_host_clock_runtime_destroy(clock_runtime);
            return 1;
        }
        if (should_close) {
            break;
        }

        if (croft_wit_host_clock_runtime_dispatch(clock_runtime, &clock_cmd, &clock_reply) != 0
                || !expect_clock_now(&clock_reply, &now_ms)) {
            fprintf(stderr, "example_wit_window_events: clock loop failed\n");
            croft_wit_host_window_runtime_destroy(window_runtime);
            croft_wit_host_clock_runtime_destroy(clock_runtime);
            return 1;
        }
        if (now_ms - start_ms >= 250u) {
            break;
        }
    }

    window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_FRAMEBUFFER_SIZE;
    window_cmd.val.framebuffer_size.window = window;
    if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) != 0
            || !expect_window_size(&window_reply, &width, &height)) {
        fprintf(stderr, "example_wit_window_events: size failed\n");
        croft_wit_host_window_runtime_destroy(window_runtime);
        croft_wit_host_clock_runtime_destroy(clock_runtime);
        return 1;
    }

    printf("window=%ux%u events=%" PRIu32 " wall_ms=%llu\n",
           width,
           height,
           event_count,
           (unsigned long long)(now_ms - start_ms));
    window_cmd.case_tag = SAP_WIT_HOST_WINDOW_COMMAND_CLOSE;
    window_cmd.val.close.window = window;
    if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) != 0
            || !expect_status_ok(&window_reply)) {
        fprintf(stderr, "example_wit_window_events: close failed\n");
        croft_wit_host_window_runtime_destroy(window_runtime);
        croft_wit_host_clock_runtime_destroy(clock_runtime);
        return 1;
    }

    croft_wit_host_window_runtime_destroy(window_runtime);
    croft_wit_host_clock_runtime_destroy(clock_runtime);
    return 0;
}
