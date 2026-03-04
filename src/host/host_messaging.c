#include "croft/host_messaging.h"

int32_t croft_msg_validate(const uint8_t* data, uint32_t len) {
    // 1. Minimum Size Check (Must contain at least the header)
    if (!data || len < sizeof(croft_msg_header)) {
        return -1; // MSG_ERR_TRUNCATED
    }

    const croft_msg_header* hdr = (const croft_msg_header*)data;

    // 2. Magic Number Validation
    if (hdr->magic != CROFT_MAGIC_NUMBER) {
        return -2; // MSG_ERR_INVALID_MAGIC
    }

    // 3. ABI Versioning
    if (hdr->abi_version != 1) {
        return -3; // MSG_ERR_UNSUPPORTED_ABI
    }

    // 4. Exact Boundaries Check
    // Prevent buffer overreads by rejecting messages promising payloads larger than actual bytes passed
    uint32_t expected_total_size = sizeof(croft_msg_header) + hdr->payload_len;
    if (expected_total_size != len) {
        return -4; // MSG_ERR_LENGTH_MISMATCH
    }

    // Structural validation successful
    return 0;
}
