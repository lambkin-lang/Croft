#ifndef CROFT_WIT_HOST_GPU2D_RUNTIME_H
#define CROFT_WIT_HOST_GPU2D_RUNTIME_H

#include "generated/wit_host_gpu2d.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct croft_wit_host_gpu2d_runtime croft_wit_host_gpu2d_runtime;

croft_wit_host_gpu2d_runtime* croft_wit_host_gpu2d_runtime_create(void);

void croft_wit_host_gpu2d_runtime_destroy(croft_wit_host_gpu2d_runtime* runtime);

int croft_wit_host_gpu2d_runtime_bind_exports(
    croft_wit_host_gpu2d_runtime* runtime,
    SapWitHostGpu2dHostGpu2dWorldExports* exports_out);

/*
 * This runtime models the direct-Metal renderer as a stateful `surface`
 * resource plus one stateless capability query. That split is intentional:
 * Lambkin will likely need both owned presentation resources and reusable
 * capability/service mix-ins at the same host boundary.
 */
int32_t croft_wit_host_gpu2d_runtime_dispatch(
    croft_wit_host_gpu2d_runtime* runtime,
    const SapWitHostGpu2dCommand* command,
    SapWitHostGpu2dReply* reply_out);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_WIT_HOST_GPU2D_RUNTIME_H */
