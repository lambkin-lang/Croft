#ifndef CROFT_WIT_HOST_WINDOW_RUNTIME_H
#define CROFT_WIT_HOST_WINDOW_RUNTIME_H

#include "generated/wit_host_window.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct croft_wit_host_window_runtime croft_wit_host_window_runtime;

croft_wit_host_window_runtime* croft_wit_host_window_runtime_create(void);

void croft_wit_host_window_runtime_destroy(croft_wit_host_window_runtime* runtime);

/*
 * This runtime makes the current singleton/callback GLFW host look like an
 * explicit window resource plus polled event stream. That mismatch is exactly
 * the pressure point we want to model for Lambkin’s later weaving logic.
 */
int32_t croft_wit_host_window_runtime_dispatch(croft_wit_host_window_runtime* runtime,
                                               const SapWitHostWindowCommand* command,
                                               SapWitHostWindowReply* reply_out);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_WIT_HOST_WINDOW_RUNTIME_H */
