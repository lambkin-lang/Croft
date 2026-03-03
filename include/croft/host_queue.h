/*
 * croft/host_queue.h — MPSC message queue and channel registry.
 */

#ifndef CROFT_HOST_QUEUE_H
#define CROFT_HOST_QUEUE_H

#include "croft/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Queue API ────────────────────────────────────────────────────── */

typedef struct host_queue host_queue_t;

/* Creates a queue. `max_bytes` of 0 means unlimited size. */
host_queue_t *host_queue_create(uint32_t max_bytes);
void          host_queue_destroy(host_queue_t *q);

int32_t       host_queue_send(host_queue_t *q, const uint8_t *ptr, uint32_t len);
/* Returns the number of bytes read on success, or -1 on error (e.g., buffer too small) */
int32_t       host_queue_recv(host_queue_t *q, uint8_t *out_ptr, uint32_t max_len);

/* ── Channel Registry API ─────────────────────────────────────────── */

void          host_channel_bind(uint32_t channel, host_queue_t *q);

/* Wasm-shaped generic messaging */
int32_t       host_send(uint32_t channel, const uint8_t *ptr, uint32_t len);
int32_t       host_recv(uint32_t channel, uint8_t *out_ptr, uint32_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_HOST_QUEUE_H */
