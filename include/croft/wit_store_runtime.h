#ifndef CROFT_WIT_STORE_RUNTIME_H
#define CROFT_WIT_STORE_RUNTIME_H

#include "generated/wit_common_core.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct croft_wit_store_runtime croft_wit_store_runtime;

typedef struct croft_wit_store_runtime_config {
    uint32_t default_page_size;
    uint64_t initial_bytes;
    uint64_t max_bytes;
} croft_wit_store_runtime_config;

void croft_wit_store_runtime_config_default(croft_wit_store_runtime_config* config);

croft_wit_store_runtime* croft_wit_store_runtime_create(
    const croft_wit_store_runtime_config* config);

void croft_wit_store_runtime_destroy(croft_wit_store_runtime* runtime);

int32_t croft_wit_store_runtime_dispatch(croft_wit_store_runtime* runtime,
                                         const SapWitCommonCoreStoreCommand* command,
                                         SapWitCommonCoreStoreReply* reply_out);

void croft_wit_store_reply_dispose(SapWitCommonCoreStoreReply* reply);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_WIT_STORE_RUNTIME_H */
