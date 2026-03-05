#include "croft/host_messaging.h"
#include "generated/wit_schema_dbis.h"
#include "runner/wire_v0.h"

#include <string.h>

enum {
    CROFT_MSG_ERR_TRUNCATED = -1,
    CROFT_MSG_ERR_INVALID_MAGIC = -2,
    CROFT_MSG_ERR_UNSUPPORTED_ABI = -3,
    CROFT_MSG_ERR_LENGTH_MISMATCH = -4,
    CROFT_MSG_ERR_UNSUPPORTED_TYPE = -5,
    CROFT_MSG_ERR_INVALID_PAYLOAD = -6,
    CROFT_MSG_ERR_RANGE = -7,
};

int32_t croft_msg_validate_payload(uint32_t msg_type, const uint8_t *payload, uint32_t payload_len)
{
    SapRunnerMessageV0 msg = {0};
    SapRunnerIntentV0 intent = {0};
    int rc = 0;

    if (payload_len > 0u && !payload) {
        return CROFT_MSG_ERR_INVALID_PAYLOAD;
    }

    switch (msg_type) {
    case CROFT_MSG_TYPE_OPAQUE:
        return 0;
    case CROFT_MSG_TYPE_RUNNER_MESSAGE_V0:
        if (sap_runner_message_v0_decode(payload, payload_len, &msg) != SAP_RUNNER_WIRE_OK) {
            return CROFT_MSG_ERR_INVALID_PAYLOAD;
        }
        return 0;
    case CROFT_MSG_TYPE_RUNNER_INTENT_V0:
        if (sap_runner_intent_v0_decode(payload, payload_len, &intent) != SAP_RUNNER_WIRE_OK) {
            return CROFT_MSG_ERR_INVALID_PAYLOAD;
        }
        return 0;
    case CROFT_MSG_TYPE_WIT_DBI1_INBOX_VALUE:
        rc = sap_wit_validate_dbi1_inbox_value(payload, payload_len);
        return (rc == 0) ? 0 : CROFT_MSG_ERR_INVALID_PAYLOAD;
    case CROFT_MSG_TYPE_WIT_DBI2_OUTBOX_VALUE:
        rc = sap_wit_validate_dbi2_outbox_value(payload, payload_len);
        return (rc == 0) ? 0 : CROFT_MSG_ERR_INVALID_PAYLOAD;
    case CROFT_MSG_TYPE_WIT_DBI4_TIMERS_VALUE:
        rc = sap_wit_validate_dbi4_timers_value(payload, payload_len);
        return (rc == 0) ? 0 : CROFT_MSG_ERR_INVALID_PAYLOAD;
    case CROFT_MSG_TYPE_WIT_DBI6_DEAD_LETTER_VALUE:
        rc = sap_wit_validate_dbi6_dead_letter_value(payload, payload_len);
        return (rc == 0) ? 0 : CROFT_MSG_ERR_INVALID_PAYLOAD;
    default:
        return CROFT_MSG_ERR_UNSUPPORTED_TYPE;
    }
}

int32_t croft_msg_validate(const uint8_t *data, uint32_t len)
{
    const croft_msg_header *hdr;
    const uint8_t *payload;
    uint64_t expected_total_size;

    /* 1. Minimum size check (must include header). */
    if (!data || len < sizeof(croft_msg_header)) {
        return CROFT_MSG_ERR_TRUNCATED;
    }

    hdr = (const croft_msg_header *)data;

    /* 2. Magic number validation. */
    if (hdr->magic != CROFT_MAGIC_NUMBER) {
        return CROFT_MSG_ERR_INVALID_MAGIC;
    }

    /* 3. Exact size check (overflow-safe). */
    expected_total_size = (uint64_t)sizeof(croft_msg_header) + (uint64_t)hdr->payload_len;
    if (expected_total_size != (uint64_t)len) {
        return CROFT_MSG_ERR_LENGTH_MISMATCH;
    }

    payload = data + sizeof(croft_msg_header);

    /* 4. ABI-specific payload validation. */
    if (hdr->abi_version == CROFT_MSG_ABI_V1) {
        return 0;
    }
    if (hdr->abi_version == CROFT_MSG_ABI_V2) {
        return croft_msg_validate_payload(hdr->msg_type, payload, hdr->payload_len);
    }
    return CROFT_MSG_ERR_UNSUPPORTED_ABI;
}

int32_t croft_msg_wrap(uint32_t abi_version, uint32_t msg_type, const uint8_t *payload,
                       uint32_t payload_len, uint8_t *out, uint32_t out_cap, uint32_t *out_len)
{
    croft_msg_header hdr;
    uint64_t total;
    int32_t rc;

    if (!out_len) {
        return CROFT_MSG_ERR_RANGE;
    }
    *out_len = 0u;

    if (payload_len > 0u && !payload) {
        return CROFT_MSG_ERR_INVALID_PAYLOAD;
    }

    if (abi_version != CROFT_MSG_ABI_V1 && abi_version != CROFT_MSG_ABI_V2) {
        return CROFT_MSG_ERR_UNSUPPORTED_ABI;
    }

    if (abi_version == CROFT_MSG_ABI_V2) {
        rc = croft_msg_validate_payload(msg_type, payload, payload_len);
        if (rc != 0) {
            return rc;
        }
    }

    total = (uint64_t)sizeof(croft_msg_header) + (uint64_t)payload_len;
    if (total > 0xFFFFFFFFu) {
        return CROFT_MSG_ERR_RANGE;
    }
    *out_len = (uint32_t)total;

    if (!out || out_cap < *out_len) {
        return CROFT_MSG_ERR_RANGE;
    }

    hdr.magic = CROFT_MAGIC_NUMBER;
    hdr.abi_version = abi_version;
    hdr.msg_type = msg_type;
    hdr.payload_len = payload_len;

    memcpy(out, &hdr, sizeof(hdr));
    if (payload_len > 0u) {
        memcpy(out + sizeof(hdr), payload, payload_len);
    }
    return 0;
}

int32_t croft_msg_unpack(const uint8_t *frame, uint32_t frame_len, croft_msg_header *header_out,
                         const uint8_t **payload_out, uint32_t *payload_len_out)
{
    const croft_msg_header *hdr;
    int32_t rc = croft_msg_validate(frame, frame_len);
    if (rc != 0) {
        return rc;
    }

    hdr = (const croft_msg_header *)frame;
    if (header_out) {
        *header_out = *hdr;
    }
    if (payload_out) {
        *payload_out = frame + sizeof(croft_msg_header);
    }
    if (payload_len_out) {
        *payload_len_out = hdr->payload_len;
    }
    return 0;
}
