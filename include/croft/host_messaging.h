#ifndef CROFT_HOST_MESSAGING_H
#define CROFT_HOST_MESSAGING_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CROFT_MAGIC_NUMBER 0x54465243 // "CRFT" in little-endian

/**
 * croft_msg_header
 * ABI Contract for cross-boundary Wasm-to-Host communication.
 * Every message passed through `host_queue_send` MUST begin with this exact 16-byte alignment.
 */
typedef struct {
    uint32_t magic;       // Must always be CROFT_MAGIC_NUMBER
    uint32_t abi_version; // Currently v1
    uint32_t msg_type;    // e.g. MSG_RENDER, MSG_FS_OPEN, MSG_LOG
    uint32_t payload_len; // Exact byte length of the payload following this header
} croft_msg_header;

/**
 * Validates the raw byte buffer against the enforced ABI schema.
 * Returns 0 on success (valid message).
 * Returns < 0 on structural or schema failure.
 */
int32_t croft_msg_validate(const uint8_t* data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif // CROFT_HOST_MESSAGING_H
