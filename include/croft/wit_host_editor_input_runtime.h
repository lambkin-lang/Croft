#ifndef CROFT_WIT_HOST_EDITOR_INPUT_RUNTIME_H
#define CROFT_WIT_HOST_EDITOR_INPUT_RUNTIME_H

#include "generated/wit_host_editor_input.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct croft_wit_host_editor_input_runtime croft_wit_host_editor_input_runtime;

croft_wit_host_editor_input_runtime* croft_wit_host_editor_input_runtime_create(void);
void croft_wit_host_editor_input_runtime_destroy(croft_wit_host_editor_input_runtime* runtime);

int32_t croft_wit_host_editor_input_runtime_dispatch(
    croft_wit_host_editor_input_runtime* runtime,
    const SapWitHostEditorInputCommand* command,
    SapWitHostEditorInputReply* reply_out);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_WIT_HOST_EDITOR_INPUT_RUNTIME_H */
