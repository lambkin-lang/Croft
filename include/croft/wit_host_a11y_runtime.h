#ifndef CROFT_WIT_HOST_A11Y_RUNTIME_H
#define CROFT_WIT_HOST_A11Y_RUNTIME_H

#include "croft/scene_a11y_bridge.h"
#include "generated/wit_host_a11y.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct croft_wit_host_a11y_runtime croft_wit_host_a11y_runtime;

croft_wit_host_a11y_runtime* croft_wit_host_a11y_runtime_create(void);
void croft_wit_host_a11y_runtime_destroy(croft_wit_host_a11y_runtime* runtime);

int32_t croft_wit_host_a11y_runtime_dispatch(croft_wit_host_a11y_runtime* runtime,
                                             const SapWitHostA11yCommand* command,
                                             SapWitHostA11yReply* reply_out);

void croft_wit_host_a11y_runtime_install_bridge(croft_wit_host_a11y_runtime* runtime);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_WIT_HOST_A11Y_RUNTIME_H */
