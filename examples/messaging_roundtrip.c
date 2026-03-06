#include "croft/host_messaging.h"
#include "croft/host_queue.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    static const uint8_t payload[] = "queue roundtrip";
    uint8_t frame[128];
    uint8_t received[128];
    uint8_t invalid[8] = {0};
    croft_msg_header header;
    const uint8_t* payload_out = NULL;
    uint32_t frame_len = 0;
    uint32_t payload_len = 0;
    int32_t rc;
    host_queue_t* queue = host_queue_create(4096);

    if (!queue) {
        fprintf(stderr, "example_messaging_roundtrip: host_queue_create failed\n");
        return 1;
    }

    rc = croft_msg_wrap(CROFT_MSG_ABI_V2, CROFT_MSG_TYPE_OPAQUE,
                        payload, (uint32_t)(sizeof(payload) - 1),
                        frame, sizeof(frame), &frame_len);
    if (rc != 0) {
        fprintf(stderr, "example_messaging_roundtrip: croft_msg_wrap failed (%d)\n", rc);
        host_queue_destroy(queue);
        return 1;
    }

    rc = host_queue_send(queue, frame, frame_len);
    if (rc != 0) {
        fprintf(stderr, "example_messaging_roundtrip: host_queue_send failed (%d)\n", rc);
        host_queue_destroy(queue);
        return 1;
    }

    rc = host_queue_recv(queue, received, sizeof(received));
    if (rc <= 0) {
        fprintf(stderr, "example_messaging_roundtrip: host_queue_recv failed (%d)\n", rc);
        host_queue_destroy(queue);
        return 1;
    }

    rc = croft_msg_unpack(received, (uint32_t)rc, &header, &payload_out, &payload_len);
    if (rc != 0) {
        fprintf(stderr, "example_messaging_roundtrip: croft_msg_unpack failed (%d)\n", rc);
        host_queue_destroy(queue);
        return 1;
    }

    if (header.abi_version != CROFT_MSG_ABI_V2 ||
        header.msg_type != CROFT_MSG_TYPE_OPAQUE ||
        payload_len != sizeof(payload) - 1 ||
        memcmp(payload_out, payload, sizeof(payload) - 1) != 0) {
        fprintf(stderr, "example_messaging_roundtrip: payload mismatch after round-trip\n");
        host_queue_destroy(queue);
        return 1;
    }

    rc = host_queue_send(queue, invalid, sizeof(invalid));
    if (rc == 0) {
        fprintf(stderr, "example_messaging_roundtrip: invalid frame unexpectedly accepted\n");
        host_queue_destroy(queue);
        return 1;
    }

    printf("roundtrip_ok abi=%u type=%u payload=\"%.*s\" rejected_invalid=%d\n",
           header.abi_version,
           header.msg_type,
           (int)payload_len,
           (const char*)payload_out,
           rc);

    host_queue_destroy(queue);
    return 0;
}
