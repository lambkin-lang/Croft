#ifndef CROFT_WIT_TEXT_PROGRAM_H
#define CROFT_WIT_TEXT_PROGRAM_H

#include "generated/wit_common_core.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t (*croft_wit_text_program_dispatch_fn)(
    void* userdata,
    const SapWitCommonCoreTextCommand* command,
    SapWitCommonCoreTextReply* reply_out);

typedef void (*croft_wit_text_program_reply_dispose_fn)(
    SapWitCommonCoreTextReply* reply);

typedef struct croft_wit_text_program_host {
    void* userdata;
    croft_wit_text_program_dispatch_fn dispatch;
    croft_wit_text_program_reply_dispose_fn dispose_reply;
} croft_wit_text_program_host;

typedef struct croft_wit_owned_bytes {
    uint8_t* data;
    uint32_t len;
} croft_wit_owned_bytes;

/*
 * This models the kind of common-side, machine-generated program logic Lambkin
 * should be able to reuse across very different host worlds. The only thing it
 * sees is the WIT text command surface plus opaque resource handles.
 */
int32_t croft_wit_text_program_prepend(
    const croft_wit_text_program_host* host,
    const uint8_t* initial_utf8,
    uint32_t initial_len,
    const uint8_t* prefix_utf8,
    uint32_t prefix_len,
    croft_wit_owned_bytes* out_bytes);

void croft_wit_owned_bytes_dispose(croft_wit_owned_bytes* bytes);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_WIT_TEXT_PROGRAM_H */
