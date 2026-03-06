#ifndef CROFT_WIT_MAILBOX_RUNTIME_H
#define CROFT_WIT_MAILBOX_RUNTIME_H

#include "generated/wit_common_core.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct croft_wit_mailbox_runtime croft_wit_mailbox_runtime;

croft_wit_mailbox_runtime* croft_wit_mailbox_runtime_create(void);

void croft_wit_mailbox_runtime_destroy(croft_wit_mailbox_runtime* runtime);

/*
 * This runtime deliberately models mailbox traffic as explicit, nonblocking
 * message passing. Waiting, sleeping, worker scheduling, and other host policy
 * concerns belong in higher worlds rather than this common-core barrier.
 */
int32_t croft_wit_mailbox_runtime_dispatch(croft_wit_mailbox_runtime* runtime,
                                           const SapWitMailboxCommand* command,
                                           SapWitMailboxReply* reply_out);

void croft_wit_mailbox_reply_dispose(SapWitMailboxReply* reply);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_WIT_MAILBOX_RUNTIME_H */
