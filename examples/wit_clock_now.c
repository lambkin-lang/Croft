#include "croft/wit_host_clock_runtime.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    croft_wit_host_clock_runtime* runtime;
    SapWitHostClockClockCommand command = {0};
    SapWitHostClockClockReply reply = {0};

    runtime = croft_wit_host_clock_runtime_create();
    if (!runtime) {
        fprintf(stderr, "example_wit_clock_now: runtime init failed\n");
        return 1;
    }

    command.case_tag = SAP_WIT_HOST_CLOCK_CLOCK_COMMAND_MONOTONIC_NOW;
    if (croft_wit_host_clock_runtime_dispatch(runtime, &command, &reply) != 0
            || reply.case_tag != SAP_WIT_HOST_CLOCK_CLOCK_REPLY_NOW
            || reply.val.now.case_tag != SAP_WIT_HOST_CLOCK_CLOCK_NOW_RESULT_OK) {
        fprintf(stderr, "example_wit_clock_now: monotonic-now failed\n");
        croft_wit_host_clock_runtime_destroy(runtime);
        return 1;
    }

    printf("clock=%" PRIu64 "\n", reply.val.now.val.ok);
    croft_wit_host_clock_runtime_destroy(runtime);
    return 0;
}
