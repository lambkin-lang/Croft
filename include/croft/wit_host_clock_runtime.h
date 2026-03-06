#ifndef CROFT_WIT_HOST_CLOCK_RUNTIME_H
#define CROFT_WIT_HOST_CLOCK_RUNTIME_H

#include "generated/wit_host_clock.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct croft_wit_host_clock_runtime croft_wit_host_clock_runtime;

croft_wit_host_clock_runtime* croft_wit_host_clock_runtime_create(void);

void croft_wit_host_clock_runtime_destroy(croft_wit_host_clock_runtime* runtime);

/*
 * Unlike file/database/mailbox barriers, the clock mix-in is a pure service
 * query. That is deliberate: not every host capability should be forced into a
 * resource/lifetime model when there is no owned state to track.
 */
int32_t croft_wit_host_clock_runtime_dispatch(croft_wit_host_clock_runtime* runtime,
                                              const SapWitClockCommand* command,
                                              SapWitClockReply* reply_out);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_WIT_HOST_CLOCK_RUNTIME_H */
