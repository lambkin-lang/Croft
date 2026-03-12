#ifndef CROFT_ORCHESTRATION_RUNTIME_H
#define CROFT_ORCHESTRATION_RUNTIME_H

#include "croft/host_wasm.h"
#include "croft/xpi_registry.h"
#include "generated/wit_orchestration.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct croft_orchestration_runtime croft_orchestration_runtime;

typedef struct {
    uint32_t wasm_stack_size;
    uint32_t default_page_size;
} croft_orchestration_runtime_config;

void croft_orchestration_runtime_config_default(croft_orchestration_runtime_config *config);

croft_orchestration_runtime *croft_orchestration_runtime_create(
    const croft_orchestration_runtime_config *config);
void croft_orchestration_runtime_destroy(croft_orchestration_runtime *runtime);

void croft_orchestration_runtime_bind_bootstrap_imports(
    croft_orchestration_runtime *runtime,
    SapWitOrchestrationBootstrapWorldImports *imports_out);

const SapWitOrchestrationControlDispatchOps *croft_orchestration_runtime_control_ops(void);

SapWitOrchestrationSessionResource croft_orchestration_runtime_last_session(
    const croft_orchestration_runtime *runtime);

int32_t croft_orchestration_runtime_join_session(
    croft_orchestration_runtime *runtime,
    SapWitOrchestrationSessionResource session);

int32_t croft_orchestration_runtime_bootstrap_wasm_path(
    croft_orchestration_runtime *runtime,
    const char *wasm_path,
    SapWitOrchestrationSessionResource *session_out);

const CroftXpiRegistry *croft_orchestration_runtime_registry(
    const croft_orchestration_runtime *runtime);

int32_t croft_orchestration_runtime_session_status(
    croft_orchestration_runtime *runtime,
    SapWitOrchestrationSessionResource session,
    SapWitOrchestrationSessionStatus *status_out);

int32_t croft_orchestration_runtime_session_get(
    croft_orchestration_runtime *runtime,
    SapWitOrchestrationSessionResource session,
    const char *table_name,
    const uint8_t *key,
    uint32_t key_len,
    uint8_t **value_out,
    uint32_t *value_len_out);

void croft_orchestration_runtime_free_bytes(uint8_t *bytes);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_ORCHESTRATION_RUNTIME_H */
