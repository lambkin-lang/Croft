#ifndef CROFT_WIT_TEXT_RUNTIME_H
#define CROFT_WIT_TEXT_RUNTIME_H

#include "generated/wit_common_core.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct croft_wit_text_runtime croft_wit_text_runtime;

typedef struct croft_wit_text_runtime_config {
    uint32_t page_size;
    uint64_t initial_bytes;
    uint64_t max_bytes;
} croft_wit_text_runtime_config;

void croft_wit_text_runtime_config_default(croft_wit_text_runtime_config* config);

croft_wit_text_runtime* croft_wit_text_runtime_create(
    const croft_wit_text_runtime_config* config);

void croft_wit_text_runtime_destroy(croft_wit_text_runtime* runtime);

/*
 * Generated model programs should treat this dispatch point as the hand-modeled
 * XPI boundary: they construct WIT-shaped commands and only ever observe opaque
 * resource handles on success.
 */
int32_t croft_wit_text_runtime_dispatch(croft_wit_text_runtime* runtime,
                                        const SapWitTextCommand* command,
                                        SapWitTextReply* reply_out);

void croft_wit_text_reply_dispose(SapWitTextReply* reply);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_WIT_TEXT_RUNTIME_H */
