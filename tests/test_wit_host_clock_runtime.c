#include "croft/wit_host_clock_runtime.h"

#include <stdint.h>

int test_wit_host_clock_runtime_monotonic(void)
{
    croft_wit_host_clock_runtime* runtime;
    SapWitHostClockClockCommand command = {0};
    SapWitHostClockClockReply reply_a = {0};
    SapWitHostClockClockReply reply_b = {0};

    runtime = croft_wit_host_clock_runtime_create();
    if (!runtime) {
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_CLOCK_CLOCK_COMMAND_MONOTONIC_NOW;
    if (croft_wit_host_clock_runtime_dispatch(runtime, &command, &reply_a) != 0
            || reply_a.case_tag != SAP_WIT_HOST_CLOCK_CLOCK_REPLY_NOW
            || reply_a.val.now.case_tag != SAP_WIT_HOST_CLOCK_CLOCK_NOW_RESULT_OK) {
        croft_wit_host_clock_runtime_destroy(runtime);
        return 1;
    }

    if (croft_wit_host_clock_runtime_dispatch(runtime, &command, &reply_b) != 0
            || reply_b.case_tag != SAP_WIT_HOST_CLOCK_CLOCK_REPLY_NOW
            || reply_b.val.now.case_tag != SAP_WIT_HOST_CLOCK_CLOCK_NOW_RESULT_OK) {
        croft_wit_host_clock_runtime_destroy(runtime);
        return 1;
    }

    if (reply_b.val.now.val.ok < reply_a.val.now.val.ok) {
        croft_wit_host_clock_runtime_destroy(runtime);
        return 1;
    }

    croft_wit_host_clock_runtime_destroy(runtime);
    return 0;
}
