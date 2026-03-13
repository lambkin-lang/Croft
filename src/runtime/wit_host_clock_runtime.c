#include "croft/wit_host_clock_runtime.h"

#include "croft/host_time.h"

#include <stdlib.h>
#include <string.h>

struct croft_wit_host_clock_runtime {
    uint32_t reserved;
};
static const SapWitHostClockDispatchOps g_croft_wit_host_clock_dispatch_ops;

static void croft_wit_host_clock_reply_zero(SapWitHostClockReply* reply)
{
    sap_wit_zero_host_clock_reply(reply);
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

int croft_wit_host_clock_runtime_bind_exports(croft_wit_host_clock_runtime* runtime,
                                              SapWitHostClockHostClockWorldExports* exports_out)
{
    if (!runtime || !exports_out) {
        return ERR_INVALID;
    }

    exports_out->host_clock_ctx = runtime;
    exports_out->host_clock_ops = &g_croft_wit_host_clock_dispatch_ops;
    return ERR_OK;
}

static int32_t croft_wit_host_clock_dispatch_monotonic_now(void* ctx,
                                                           SapWitHostClockReply* reply_out)
{
    (void)ctx;
    croft_wit_host_clock_reply_now_ok(reply_out, host_time_millis());
    return 0;
}

static const SapWitHostClockDispatchOps g_croft_wit_host_clock_dispatch_ops = {
    .monotonic_now = croft_wit_host_clock_dispatch_monotonic_now,
};

int32_t croft_wit_host_clock_runtime_dispatch(croft_wit_host_clock_runtime* runtime,
                                              const SapWitHostClockCommand* command,
                                              SapWitHostClockReply* reply_out)
{
    int32_t rc;

    if (!runtime || !command || !reply_out) {
        return -1;
    }

    rc = sap_wit_dispatch_host_clock(runtime,
                                     &g_croft_wit_host_clock_dispatch_ops,
                                     command,
                                     reply_out);
    return rc == -1 ? -1 : rc;
}
