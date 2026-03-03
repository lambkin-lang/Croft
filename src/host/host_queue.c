/*
 * host_queue.c — MPSC message queue implementation.
 */

#include "croft/host_queue.h"
#include "croft/host_thread.h"
#include "croft/host_log.h"
#include <stdlib.h>
#include <string.h>

#define MAX_CHANNELS 256

/* ── State ────────────────────────────────────────────────────────── */

typedef struct msg_node {
    struct msg_node *next;
    uint32_t len;
    uint8_t  data[];
} msg_node_t;

struct host_queue {
    host_mutex_t mutex;
    host_cond_t  cond_recv;
    host_cond_t  cond_send;
    
    msg_node_t  *head;
    msg_node_t  *tail;
    
    uint32_t     max_bytes;
    uint32_t     cur_bytes;
    int          is_destroyed;
};

static host_queue_t *g_channels[MAX_CHANNELS] = { NULL };
static host_mutex_t  g_channel_mutex;
static int           g_channel_mutex_init = 0;

/* ── Helpers ──────────────────────────────────────────────────────── */

static void init_channel_mutex_if_needed(void)
{
    if (!g_channel_mutex_init) {
        host_mutex_init(&g_channel_mutex);
        g_channel_mutex_init = 1;
    }
}

/* ── Queue Implementation ─────────────────────────────────────────── */

host_queue_t *host_queue_create(uint32_t max_bytes)
{
    host_queue_t *q = calloc(1, sizeof(host_queue_t));
    if (!q) return NULL;
    
    host_mutex_init(&q->mutex);
    host_cond_init(&q->cond_recv);
    host_cond_init(&q->cond_send);
    q->max_bytes = max_bytes;
    
    return q;
}

void host_queue_destroy(host_queue_t *q)
{
    if (!q) return;

    host_mutex_lock(&q->mutex);
    q->is_destroyed = 1;
    host_cond_broadcast(&q->cond_recv);
    host_cond_broadcast(&q->cond_send);
    
    msg_node_t *curr = q->head;
    while (curr) {
        msg_node_t *next = curr->next;
        free(curr);
        curr = next;
    }
    q->head = NULL;
    q->tail = NULL;
    q->cur_bytes = 0;
    host_mutex_unlock(&q->mutex);

    host_cond_destroy(&q->cond_recv);
    host_cond_destroy(&q->cond_send);
    host_mutex_destroy(&q->mutex);
    free(q);
}

int32_t host_queue_send(host_queue_t *q, const uint8_t *ptr, uint32_t len)
{
    if (!q || (!ptr && len > 0)) return -1;

    host_mutex_lock(&q->mutex);
    
    /* Block if back pressure applies, but always allow the first element if queue is empty */
    while (!q->is_destroyed && q->max_bytes > 0 && (q->cur_bytes + len > q->max_bytes) && q->head != NULL) {
        host_cond_wait(&q->cond_send, &q->mutex);
    }
    
    if (q->is_destroyed) {
        host_mutex_unlock(&q->mutex);
        return -1;
    }

    msg_node_t *node = malloc(sizeof(msg_node_t) + len);
    if (!node) {
        host_mutex_unlock(&q->mutex);
        return -1;
    }
    node->len = len;
    if (len > 0) memcpy(node->data, ptr, len);
    node->next = NULL;

    if (q->tail) {
        q->tail->next = node;
    } else {
        q->head = node;
    }
    q->tail = node;
    q->cur_bytes += len;

    host_cond_signal(&q->cond_recv);
    host_mutex_unlock(&q->mutex);
    return 0;
}

int32_t host_queue_recv(host_queue_t *q, uint8_t *out_ptr, uint32_t max_len)
{
    if (!q || (!out_ptr && max_len > 0)) return -1;

    host_mutex_lock(&q->mutex);
    
    while (!q->is_destroyed && q->head == NULL) {
        host_cond_wait(&q->cond_recv, &q->mutex);
    }
    
    if (q->head == NULL) {
        /* is_destroyed and empty */
        host_mutex_unlock(&q->mutex);
        return -1;
    }
    
    msg_node_t *node = q->head;
    if (node->len > max_len) {
        /* buffer too small, message left in queue */
        host_mutex_unlock(&q->mutex);
        return -1;
    }
    
    if (node->len > 0) {
        memcpy(out_ptr, node->data, node->len);
    }
    int32_t ret_len = node->len;
    
    q->head = node->next;
    if (q->head == NULL) {
        q->tail = NULL;
    }
    q->cur_bytes -= node->len;
    free(node);
    
    host_cond_signal(&q->cond_send);
    host_mutex_unlock(&q->mutex);
    
    return ret_len;
}

/* ── Registry Implementation ──────────────────────────────────────── */

void host_channel_bind(uint32_t channel, host_queue_t *q)
{
    if (channel >= MAX_CHANNELS) return;
    init_channel_mutex_if_needed();
    
    host_mutex_lock(&g_channel_mutex);
    g_channels[channel] = q;
    host_mutex_unlock(&g_channel_mutex);
}

int32_t host_send(uint32_t channel, const uint8_t *ptr, uint32_t len)
{
    if (channel >= MAX_CHANNELS) return -1;
    init_channel_mutex_if_needed();
    
    host_mutex_lock(&g_channel_mutex);
    host_queue_t *q = g_channels[channel];
    host_mutex_unlock(&g_channel_mutex);
    
    if (!q) return -1;
    return host_queue_send(q, ptr, len);
}

int32_t host_recv(uint32_t channel, uint8_t *out_ptr, uint32_t max_len)
{
    if (channel >= MAX_CHANNELS) return -1;
    init_channel_mutex_if_needed();
    
    host_mutex_lock(&g_channel_mutex);
    host_queue_t *q = g_channels[channel];
    host_mutex_unlock(&g_channel_mutex);
    
    if (!q) return -1;
    return host_queue_recv(q, out_ptr, max_len);
}
