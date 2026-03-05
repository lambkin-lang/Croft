#ifndef CROFT_HOST_MESSAGING_H
#define CROFT_HOST_MESSAGING_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CROFT_MAGIC_NUMBER 0x54465243 // "CRFT" in little-endian

/* ABI versions for croft_msg_header. */
#define CROFT_MSG_ABI_V1 1u /* Legacy structural envelope validation only. */
#define CROFT_MSG_ABI_V2 2u /* Typed payload validation (runner/WIT-aware). */

/* Message type IDs used by ABI v2 validation. */
#define CROFT_MSG_TYPE_OPAQUE 0u /* Envelope validated; payload treated as opaque bytes. */
#define CROFT_MSG_TYPE_RUNNER_MESSAGE_V0 1u
#define CROFT_MSG_TYPE_RUNNER_INTENT_V0 2u
#define CROFT_MSG_TYPE_WIT_DBI1_INBOX_VALUE 101u
#define CROFT_MSG_TYPE_WIT_DBI2_OUTBOX_VALUE 102u
#define CROFT_MSG_TYPE_WIT_DBI4_TIMERS_VALUE 104u
#define CROFT_MSG_TYPE_WIT_DBI6_DEAD_LETTER_VALUE 106u

/**
 * croft_msg_header
 * ABI Contract for cross-boundary Wasm-to-Host communication.
 * Every message passed through `host_queue_send` MUST begin with this exact 16-byte alignment.
 */
typedef struct {
    uint32_t magic;       // Must always be CROFT_MAGIC_NUMBER
    uint32_t abi_version; // CROFT_MSG_ABI_*
    uint32_t msg_type;    // e.g. MSG_RENDER, MSG_FS_OPEN, MSG_LOG
    uint32_t payload_len; // Exact byte length of the payload following this header
} croft_msg_header;

/**
 * Validates the raw byte buffer against the enforced ABI schema.
 * Returns 0 on success (valid message).
 * Returns < 0 on structural or schema failure.
 */
int32_t croft_msg_validate(const uint8_t* data, uint32_t len);

/**
 * Validates an already-separated payload according to msg_type.
 * Returns 0 on success, < 0 on validation failure.
 */
int32_t croft_msg_validate_payload(uint32_t msg_type, const uint8_t *payload, uint32_t payload_len);

/**
 * Wraps payload bytes in a croft_msg_header frame.
 * On success writes total frame size to out_len and returns 0.
 */
int32_t croft_msg_wrap(uint32_t abi_version, uint32_t msg_type, const uint8_t *payload,
                       uint32_t payload_len, uint8_t *out, uint32_t out_cap, uint32_t *out_len);

/**
 * Validates and unpacks a frame into header + payload view.
 * `payload_out` points inside `frame`.
 */
int32_t croft_msg_unpack(const uint8_t *frame, uint32_t frame_len, croft_msg_header *header_out,
                         const uint8_t **payload_out, uint32_t *payload_len_out);

#ifdef __cplusplus
}
#endif

#endif // CROFT_HOST_MESSAGING_H
