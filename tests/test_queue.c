#include "croft/host_queue.h"
#include "croft/host_thread.h"
#include "croft/host_time.h"
#include <stdio.h>
#include <string.h>

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "    ASSERT failed: %s  (%s:%d)\n", #cond, __FILE__, __LINE__); \
            return 1; \
        } \
    } while (0)

int test_queue_basic(void)
{
    host_queue_t *q = host_queue_create(1024);
    ASSERT(q != NULL);
    
    const char *msg1 = "hello";
    const char *msg2 = "world";
    
    ASSERT(host_queue_send(q, (const uint8_t *)msg1, 5) == 0);
    ASSERT(host_queue_send(q, (const uint8_t *)msg2, 5) == 0);
    ASSERT(host_queue_send(q, NULL, 0) == 0); /* Zero-length message */
    
    uint8_t buf[256];
    ASSERT(host_queue_recv(q, buf, sizeof(buf)) == 5);
    ASSERT(memcmp(buf, "hello", 5) == 0);
    
    ASSERT(host_queue_recv(q, buf, 4) == -1); /* Buffer too small */
    
    ASSERT(host_queue_recv(q, buf, sizeof(buf)) == 5);
    ASSERT(memcmp(buf, "world", 5) == 0);
    
    ASSERT(host_queue_recv(q, buf, sizeof(buf)) == 0); /* Zero length message */
    
    host_queue_destroy(q);
    return 0;
}

/* Multi-Producer Test Shared State */
struct mp_ctx {
    host_queue_t *q;
    int num_messages;
};

static void *producer_thread(void *arg)
{
    struct mp_ctx *ctx = (struct mp_ctx *)arg;
    for (int i = 0; i < ctx->num_messages; i++) {
        uint8_t payload = (uint8_t)(i % 256);
        host_queue_send(ctx->q, &payload, 1);
    }
    return NULL;
}

int test_queue_multi_producer(void)
{
    host_queue_t *q = host_queue_create(0); /* Unlimited size */
    ASSERT(q != NULL);
    
    enum { NUM_THREADS = 4, MSG_PER_THREAD = 1000 };
    struct mp_ctx ctx = { .q = q, .num_messages = MSG_PER_THREAD };
    
    host_thread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        ASSERT(host_thread_create(&threads[i], producer_thread, &ctx) == 0);
    }
    
    int total_received = 0;
    uint8_t buf[16];
    for (int i = 0; i < NUM_THREADS * MSG_PER_THREAD; i++) {
        int32_t len = host_queue_recv(q, buf, sizeof(buf));
        ASSERT(len == 1);
        total_received++;
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        ASSERT(host_thread_join(threads[i], NULL) == 0);
    }
    
    ASSERT(total_received == NUM_THREADS * MSG_PER_THREAD);
    host_queue_destroy(q);
    return 0;
}

static void *slow_consumer_thread(void *arg)
{
    host_queue_t *q = (host_queue_t *)arg;
    uint8_t buf[16];
    
    /* Consume 3 messages slowly. */
    for (int i = 0; i < 3; i++) {
        uint64_t start = host_time_millis();
        while (host_time_millis() - start < 50) { } /* ~50ms delay */
        host_queue_recv(q, buf, sizeof(buf));
    }
    return NULL;
}

int test_queue_backpressure(void)
{
    /* Queue can only hold 2 bytes */
    host_queue_t *q = host_queue_create(2);
    ASSERT(q != NULL);

    host_thread_t t;
    ASSERT(host_thread_create(&t, slow_consumer_thread, q) == 0);
    
    uint64_t start = host_time_millis();
    /* These should fit/proceed. Send 3 msgs of 1 byte each.
     * 1st byte fits, 2nd byte fits. 
     * 3rd byte blocks until consumer reads 1st byte (~50ms). */
    host_queue_send(q, (const uint8_t *)"A", 1);
    host_queue_send(q, (const uint8_t *)"B", 1);
    host_queue_send(q, (const uint8_t *)"C", 1);
    
    uint64_t diff = host_time_millis() - start;
    ASSERT(diff >= 40); /* Should have blocked */

    ASSERT(host_thread_join(t, NULL) == 0);
    host_queue_destroy(q);
    return 0;
}

int test_queue_unbind(void)
{
    host_queue_t *q1 = host_queue_create(1024);
    host_queue_t *q2 = host_queue_create(1024);
    
    host_channel_bind(5, q1);
    ASSERT(host_send(5, (const uint8_t *)"foo", 3) == 0);
    
    host_channel_bind(5, q2); /* Rebind */
    ASSERT(host_send(5, (const uint8_t *)"bar", 3) == 0);
    
    uint8_t buf[16];
    /* We expect foo in q1, bar in q2 */
    ASSERT(host_queue_recv(q1, buf, sizeof(buf)) == 3);
    ASSERT(memcmp(buf, "foo", 3) == 0);
    
    /* Current bound channel 5 */
    ASSERT(host_recv(5, buf, sizeof(buf)) == 3);
    ASSERT(memcmp(buf, "bar", 3) == 0);
    
    host_channel_bind(5, NULL); /* Unbind */
    ASSERT(host_send(5, (const uint8_t *)"baz", 3) == -1);
    
    host_queue_destroy(q1);
    host_queue_destroy(q2);
    return 0;
}
