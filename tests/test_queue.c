#include "croft/host_queue.h"
#include "croft/host_messaging.h"
#include "croft/host_thread.h"
#include "croft/host_time.h"
#include <stdio.h>
#include <string.h>

#define TEST_PACKET_CAP 128u

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "    ASSERT failed: %s  (%s:%d)\n", #cond, __FILE__, __LINE__); \
            return 1; \
        } \
    } while (0)

static int make_opaque_packet(const uint8_t *payload, uint32_t payload_len, uint8_t *out,
                              uint32_t out_cap, uint32_t *out_len)
{
    return croft_msg_wrap(CROFT_MSG_ABI_V2, CROFT_MSG_TYPE_OPAQUE, payload, payload_len, out,
                          out_cap, out_len);
}

static int unpack_opaque_packet(const uint8_t *frame, uint32_t frame_len, const uint8_t **payload_out,
                                uint32_t *payload_len_out)
{
    croft_msg_header hdr = {0};
    int32_t rc = croft_msg_unpack(frame, frame_len, &hdr, payload_out, payload_len_out);
    if (rc != 0) return rc;
    if (hdr.msg_type != CROFT_MSG_TYPE_OPAQUE) return -1;
    return 0;
}

int test_queue_basic(void)
{
    host_queue_t *q = host_queue_create(1024);
    ASSERT(q != NULL);

    {
        uint8_t bad_payload[] = {0xFF};
        uint8_t scratch[TEST_PACKET_CAP];
        uint32_t scratch_len = 0;
        ASSERT(croft_msg_wrap(CROFT_MSG_ABI_V2, CROFT_MSG_TYPE_RUNNER_MESSAGE_V0, bad_payload,
                              (uint32_t)sizeof(bad_payload), scratch, sizeof(scratch),
                              &scratch_len) != 0);
    }

    const uint8_t msg1[] = {'h', 'e', 'l', 'l', 'o'};
    const uint8_t msg2[] = {'w', 'o', 'r', 'l', 'd'};
    uint8_t frame1[TEST_PACKET_CAP];
    uint8_t frame2[TEST_PACKET_CAP];
    uint8_t frame3[TEST_PACKET_CAP];
    uint32_t frame1_len = 0;
    uint32_t frame2_len = 0;
    uint32_t frame3_len = 0;

    ASSERT(make_opaque_packet(msg1, (uint32_t)sizeof(msg1), frame1, sizeof(frame1), &frame1_len) == 0);
    ASSERT(make_opaque_packet(msg2, (uint32_t)sizeof(msg2), frame2, sizeof(frame2), &frame2_len) == 0);
    ASSERT(make_opaque_packet(NULL, 0, frame3, sizeof(frame3), &frame3_len) == 0);

    ASSERT(host_queue_send(q, frame1, frame1_len) == 0);
    ASSERT(host_queue_send(q, frame2, frame2_len) == 0);
    ASSERT(host_queue_send(q, frame3, frame3_len) == 0); /* Zero-length payload */

    uint8_t buf[256];
    const uint8_t *payload = NULL;
    uint32_t payload_len = 0;

    int32_t first_len = host_queue_recv(q, buf, sizeof(buf));
    ASSERT(first_len > 0);
    ASSERT(unpack_opaque_packet(buf, (uint32_t)first_len, &payload, &payload_len) == 0);
    ASSERT(payload_len == sizeof(msg1));
    ASSERT(memcmp(payload, msg1, sizeof(msg1)) == 0);

    ASSERT(host_queue_recv(q, buf, 4) == -1); /* Buffer too small */

    int32_t second_len = host_queue_recv(q, buf, sizeof(buf));
    ASSERT(second_len > 0);
    ASSERT(unpack_opaque_packet(buf, (uint32_t)second_len, &payload, &payload_len) == 0);
    ASSERT(payload_len == sizeof(msg2));
    ASSERT(memcmp(payload, msg2, sizeof(msg2)) == 0);

    int32_t third_len = host_queue_recv(q, buf, sizeof(buf));
    ASSERT(third_len > 0);
    ASSERT(unpack_opaque_packet(buf, (uint32_t)third_len, &payload, &payload_len) == 0);
    ASSERT(payload_len == 0); /* Zero-length payload */

    host_queue_destroy(q);
    return 0;
}

/* Multi-Producer Test Shared State */
struct mp_ctx {
    host_queue_t *q;
    int num_messages;
    volatile int send_failures;
};

static void *producer_thread(void *arg)
{
    struct mp_ctx *ctx = (struct mp_ctx *)arg;
    uint8_t frame[TEST_PACKET_CAP];
    uint32_t frame_len = 0;

    for (int i = 0; i < ctx->num_messages; i++) {
        uint8_t payload = (uint8_t)(i % 256);
        if (make_opaque_packet(&payload, 1, frame, sizeof(frame), &frame_len) != 0 ||
            host_queue_send(ctx->q, frame, frame_len) != 0) {
            ctx->send_failures++;
        }
    }
    return NULL;
}

int test_queue_multi_producer(void)
{
    host_queue_t *q = host_queue_create(0); /* Unlimited size */
    ASSERT(q != NULL);
    
    enum { NUM_THREADS = 4, MSG_PER_THREAD = 1000 };
    struct mp_ctx ctx = { .q = q, .num_messages = MSG_PER_THREAD, .send_failures = 0 };
    
    host_thread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        ASSERT(host_thread_create(&threads[i], producer_thread, &ctx) == 0);
    }
    
    int total_received = 0;
    uint8_t buf[TEST_PACKET_CAP];
    for (int i = 0; i < NUM_THREADS * MSG_PER_THREAD; i++) {
        int32_t len = host_queue_recv(q, buf, sizeof(buf));
        const uint8_t *payload = NULL;
        uint32_t payload_len = 0;
        ASSERT(len > 0);
        ASSERT(unpack_opaque_packet(buf, (uint32_t)len, &payload, &payload_len) == 0);
        ASSERT(payload_len == 1);
        total_received++;
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        ASSERT(host_thread_join(threads[i], NULL) == 0);
    }
    
    ASSERT(ctx.send_failures == 0);
    ASSERT(total_received == NUM_THREADS * MSG_PER_THREAD);
    host_queue_destroy(q);
    return 0;
}

static void *slow_consumer_thread(void *arg)
{
    host_queue_t *q = (host_queue_t *)arg;
    uint8_t buf[TEST_PACKET_CAP];
    
    /* Consume 3 messages slowly. */
    for (int i = 0; i < 3; i++) {
        uint64_t start = host_time_millis();
        while (host_time_millis() - start < 50) { } /* ~50ms delay */
        (void)host_queue_recv(q, buf, sizeof(buf));
    }
    return NULL;
}

int test_queue_backpressure(void)
{
    uint8_t packet_a[TEST_PACKET_CAP];
    uint8_t packet_b[TEST_PACKET_CAP];
    uint8_t packet_c[TEST_PACKET_CAP];
    uint32_t len_a = 0, len_b = 0, len_c = 0;

    ASSERT(make_opaque_packet((const uint8_t *)"A", 1, packet_a, sizeof(packet_a), &len_a) == 0);
    ASSERT(make_opaque_packet((const uint8_t *)"B", 1, packet_b, sizeof(packet_b), &len_b) == 0);
    ASSERT(make_opaque_packet((const uint8_t *)"C", 1, packet_c, sizeof(packet_c), &len_c) == 0);

    /* Queue can hold two framed packets before applying backpressure. */
    host_queue_t *q = host_queue_create(len_a * 2);
    ASSERT(q != NULL);

    host_thread_t t;
    ASSERT(host_thread_create(&t, slow_consumer_thread, q) == 0);
    
    uint64_t start = host_time_millis();
    /* 1st packet fits, 2nd packet fits, 3rd blocks until consumer drains one. */
    ASSERT(host_queue_send(q, packet_a, len_a) == 0);
    ASSERT(host_queue_send(q, packet_b, len_b) == 0);
    ASSERT(host_queue_send(q, packet_c, len_c) == 0);
    
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
    uint8_t foo_packet[TEST_PACKET_CAP];
    uint8_t bar_packet[TEST_PACKET_CAP];
    uint8_t baz_packet[TEST_PACKET_CAP];
    uint32_t foo_len = 0, bar_len = 0, baz_len = 0;
    
    ASSERT(make_opaque_packet((const uint8_t *)"foo", 3, foo_packet, sizeof(foo_packet), &foo_len) == 0);
    ASSERT(make_opaque_packet((const uint8_t *)"bar", 3, bar_packet, sizeof(bar_packet), &bar_len) == 0);
    ASSERT(make_opaque_packet((const uint8_t *)"baz", 3, baz_packet, sizeof(baz_packet), &baz_len) == 0);

    host_channel_bind(5, q1);
    ASSERT(host_send(5, foo_packet, foo_len) == 0);
    
    host_channel_bind(5, q2); /* Rebind */
    ASSERT(host_send(5, bar_packet, bar_len) == 0);
    
    uint8_t buf[TEST_PACKET_CAP];
    const uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    /* We expect foo in q1, bar in q2 */
    int32_t q1_len = host_queue_recv(q1, buf, sizeof(buf));
    ASSERT(q1_len > 0);
    ASSERT(unpack_opaque_packet(buf, (uint32_t)q1_len, &payload, &payload_len) == 0);
    ASSERT(payload_len == 3);
    ASSERT(memcmp(payload, "foo", 3) == 0);
    
    /* Current bound channel 5 */
    int32_t q2_len = host_recv(5, buf, sizeof(buf));
    ASSERT(q2_len > 0);
    ASSERT(unpack_opaque_packet(buf, (uint32_t)q2_len, &payload, &payload_len) == 0);
    ASSERT(payload_len == 3);
    ASSERT(memcmp(payload, "bar", 3) == 0);
    
    host_channel_bind(5, NULL); /* Unbind */
    ASSERT(host_send(5, baz_packet, baz_len) == -1);
    
    host_queue_destroy(q1);
    host_queue_destroy(q2);
    return 0;
}
