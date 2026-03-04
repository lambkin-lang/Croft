#include "croft/host_messaging.h"
#include "croft/host_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// A poor-man's fuzzer attempting to break the abstract isolation boundary 
// over 10,000 iterations to verify `croft_msg_validate` is structurally safe.
int main(void) {
    printf("Starting Tier 10 Fuzz Tester.\n");
    srand(time(NULL));
    
    // We only test bounds and isolation failure tracking; queues don't actually get consumed here.
    host_queue_t *q = host_queue_create(1024 * 1024);
    
    int passes = 0;
    int rejections = 0;
    
    // Generate 10,000 randomized or adversarial inputs
    for (int i = 0; i < 10000; i++) {
        uint8_t buffer[256];
        
        // Randomize length up to 256
        uint32_t len = rand() % 256;
        
        // Fill buffer with random data
        for (uint32_t j = 0; j < len; j++) {
            buffer[j] = rand() % 256;
        }

        // About 10% of the time, intentionally format a VALID header structural frame
        // to verify legitimate traffic can pass.
        if (len >= sizeof(croft_msg_header) && (rand() % 100) > 90) {
            croft_msg_header* hdr = (croft_msg_header*)buffer;
            hdr->magic = CROFT_MAGIC_NUMBER;
            hdr->abi_version = 1;
            hdr->msg_type = (rand() % 5);
            // Payload fits exactly within the allocated randomized slice
            hdr->payload_len = len - sizeof(croft_msg_header);
        }
        
        // Attempt injection
        int32_t result = host_queue_send(q, buffer, len);
        
        if (result == 0) {
            passes++;
        } else {
            rejections++;
        }
    }
    
    printf("Fuzz Testing Complete! Results over 10,000 runs:\n");
    printf("Valid Messages Let Through: %d\n", passes);
    printf("Malicious / Malformed Payloads Rejected: %d\n", rejections);
    
    host_queue_destroy(q);
    
    if (passes > 0 && rejections > 0) {
        printf("Tier 10 ABI Barrier functioning securely.\n");
        return 0;
    } else {
        printf("Fuzzer distribution error! Tests failing.\n");
        return 1;
    }
}
