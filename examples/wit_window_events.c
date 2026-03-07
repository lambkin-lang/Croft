#include "croft/wit_host_clock_runtime.h"
#include "croft/wit_host_window_runtime.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int expect_window_ok(const SapWitHostWindowWindowReply* reply, SapWitHostWindowWindowResource* handle_out)
{
    if (!reply || !handle_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_WINDOW_WINDOW_REPLY_WINDOW
            || reply->val.window.case_tag != SAP_WIT_HOST_WINDOW_WINDOW_OP_RESULT_OK) {
        return 0;
    }
    *handle_out = reply->val.window.val.ok;
    return 1;
}

static int expect_status_ok(const SapWitHostWindowWindowReply* reply)
{
    return reply
        && reply->case_tag == SAP_WIT_HOST_WINDOW_WINDOW_REPLY_STATUS
        && reply->val.status.case_tag == SAP_WIT_HOST_WINDOW_WINDOW_STATUS_OK;
}

static int expect_clock_now(const SapWitHostClockClockReply* reply, uint64_t* now_out)
{
    if (!reply || !now_out) {
        return 0;
    }
    if (reply->case_tag != SAP_WIT_HOST_CLOCK_CLOCK_REPLY_NOW
            || reply->val.now.case_tag != SAP_WIT_HOST_CLOCK_CLOCK_NOW_RESULT_OK) {
        return 0;
    }
    *now_out = reply->val.now.val.ok;
    return 1;
}

int main(void)
{
    croft_wit_host_window_runtime* window_runtime;
    croft_wit_host_clock_runtime* clock_runtime;
    SapWitHostWindowWindowCommand window_cmd = {0};
    SapWitHostWindowWindowReply window_reply = {0};
    SapWitHostClockClockCommand clock_cmd = {0};
    SapWitHostClockClockReply clock_reply = {0};
    SapWitHostWindowWindowResource window = SAP_WIT_HOST_WINDOW_WINDOW_RESOURCE_INVALID;
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

    window_cmd.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_COMMAND_OPEN;
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

    clock_cmd.case_tag = SAP_WIT_HOST_CLOCK_CLOCK_COMMAND_MONOTONIC_NOW;
    if (croft_wit_host_clock_runtime_dispatch(clock_runtime, &clock_cmd, &clock_reply) != 0
            || !expect_clock_now(&clock_reply, &start_ms)) {
        fprintf(stderr, "example_wit_window_events: clock start failed\n");
        croft_wit_host_window_runtime_destroy(window_runtime);
        croft_wit_host_clock_runtime_destroy(clock_runtime);
        return 1;
    }

    for (;;) {
        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_COMMAND_POLL;
        window_cmd.val.poll.window = window;
        if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) != 0
                || !expect_status_ok(&window_reply)) {
            fprintf(stderr, "example_wit_window_events: poll failed\n");
            croft_wit_host_window_runtime_destroy(window_runtime);
            croft_wit_host_clock_runtime_destroy(clock_runtime);
            return 1;
        }

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_COMMAND_NEXT_EVENT;
        window_cmd.val.next_event.window = window;
        while (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) == 0
                && window_reply.case_tag == SAP_WIT_HOST_WINDOW_WINDOW_REPLY_EVENT
                && window_reply.val.event.case_tag == SAP_WIT_HOST_WINDOW_WINDOW_EVENT_RESULT_OK) {
            event_count++;
        }

        window_cmd.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_COMMAND_SHOULD_CLOSE;
        window_cmd.val.should_close.window = window;
        if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) != 0
                || window_reply.case_tag != SAP_WIT_HOST_WINDOW_WINDOW_REPLY_SHOULD_CLOSE
                || window_reply.val.should_close.case_tag != SAP_WIT_HOST_WINDOW_WINDOW_BOOL_RESULT_OK) {
            fprintf(stderr, "example_wit_window_events: should-close failed\n");
            croft_wit_host_window_runtime_destroy(window_runtime);
            croft_wit_host_clock_runtime_destroy(clock_runtime);
            return 1;
        }
        if (window_reply.val.should_close.val.ok) {
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

    window_cmd.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_COMMAND_FRAMEBUFFER_SIZE;
    window_cmd.val.framebuffer_size.window = window;
    if (croft_wit_host_window_runtime_dispatch(window_runtime, &window_cmd, &window_reply) != 0
            || window_reply.case_tag != SAP_WIT_HOST_WINDOW_WINDOW_REPLY_SIZE
            || window_reply.val.size.case_tag != SAP_WIT_HOST_WINDOW_WINDOW_SIZE_RESULT_OK) {
        fprintf(stderr, "example_wit_window_events: size failed\n");
        croft_wit_host_window_runtime_destroy(window_runtime);
        croft_wit_host_clock_runtime_destroy(clock_runtime);
        return 1;
    }
    width = window_reply.val.size.val.ok.width;
    height = window_reply.val.size.val.ok.height;

    printf("window=%ux%u events=%" PRIu32 "\n", width, height, event_count);

    window_cmd.case_tag = SAP_WIT_HOST_WINDOW_WINDOW_COMMAND_CLOSE;
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
