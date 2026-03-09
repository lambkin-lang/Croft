#include "croft/wit_host_clock_runtime.h"

#include "croft/host_time.h"

#include <stdlib.h>
#include <string.h>

struct croft_wit_host_clock_runtime {
    uint32_t reserved;
};

static void croft_wit_host_clock_reply_zero(SapWitHostClockReply* reply)
{
    if (!reply) {
        return;
    }
    memset(reply, 0, sizeof(*reply));
}

static void croft_wit_host_clock_reply_now_ok(SapWitHostClockReply* reply, uint64_t now_ms)
{
    croft_wit_host_clock_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_CLOCK_REPLY_NOW;
    reply->val.now.is_v_ok = 1u;
    reply->val.now.v_val.ok.v = now_ms;
}

croft_wit_host_clock_runtime* croft_wit_host_clock_runtime_create(void)
{
    return (croft_wit_host_clock_runtime*)calloc(1u, sizeof(croft_wit_host_clock_runtime));
}

void croft_wit_host_clock_runtime_destroy(croft_wit_host_clock_runtime* runtime)
{
    free(runtime);
}

int32_t croft_wit_host_clock_runtime_dispatch(croft_wit_host_clock_runtime* runtime,
                                              const SapWitHostClockCommand* command,
                                              SapWitHostClockReply* reply_out)
{
    if (!runtime || !command || !reply_out) {
        return -1;
    }

    switch (command->case_tag) {
        case SAP_WIT_HOST_CLOCK_COMMAND_MONOTONIC_NOW:
            croft_wit_host_clock_reply_now_ok(reply_out, host_time_millis());
            return 0;
        default:
            return -1;
    }
}
