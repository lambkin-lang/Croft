#include "croft/orchestration_runtime.h"

#include <stdio.h>
#include <string.h>

#ifndef CROFT_DEFAULT_ORCHESTRATION_BOOTSTRAP_WASM_PATH
#define CROFT_DEFAULT_ORCHESTRATION_BOOTSTRAP_WASM_PATH ""
#endif

static void print_usage(const char *argv0)
{
    fprintf(stderr, "usage: %s <bootstrap.wasm>\n", argv0 ? argv0 : "example_orchestration_bootstrap_runner");
}

int main(int argc, char **argv)
{
    const char *bootstrap_path = NULL;
    croft_orchestration_runtime_config config;
    croft_orchestration_runtime *runtime = NULL;
    SapWitOrchestrationSessionResource session = SAP_WIT_ORCHESTRATION_SESSION_RESOURCE_INVALID;
    SapWitOrchestrationSessionStatus status;
    int32_t rc;

    if (argc > 1) {
        bootstrap_path = argv[1];
    } else if (CROFT_DEFAULT_ORCHESTRATION_BOOTSTRAP_WASM_PATH[0] != '\0') {
        bootstrap_path = CROFT_DEFAULT_ORCHESTRATION_BOOTSTRAP_WASM_PATH;
    }

    if (!bootstrap_path || bootstrap_path[0] == '\0') {
        print_usage(argv[0]);
        return 2;
    }

    croft_orchestration_runtime_config_default(&config);
    runtime = croft_orchestration_runtime_create(&config);
    if (!runtime) {
        fprintf(stderr, "failed to create orchestration runtime\n");
        return 1;
    }

    rc = croft_orchestration_runtime_bootstrap_wasm_path(runtime, bootstrap_path, &session);
    if (rc != 0 || session == SAP_WIT_ORCHESTRATION_SESSION_RESOURCE_INVALID) {
        fprintf(stderr, "bootstrap launch failed rc=%d path=%s\n", (int)rc, bootstrap_path);
        croft_orchestration_runtime_destroy(runtime);
        return 1;
    }

    rc = croft_orchestration_runtime_join_session(runtime, session);
    if (rc != 0) {
        fprintf(stderr, "session join failed rc=%d\n", (int)rc);
        croft_orchestration_runtime_destroy(runtime);
        return 1;
    }

    rc = croft_orchestration_runtime_session_status(runtime, session, &status);
    if (rc != 0) {
        fprintf(stderr, "status fetch failed rc=%d\n", (int)rc);
        croft_orchestration_runtime_destroy(runtime);
        return 1;
    }

    printf("bootstrap: %s\n", bootstrap_path);
    printf("session: %u\n", session);
    printf("phase: %u\n", (unsigned)status.phase);
    printf("workers: %u\n", (unsigned)status.worker_count);
    printf("running: %u\n", (unsigned)status.running_count);
    if (status.has_last_error && status.last_error_data && status.last_error_len > 0u) {
        printf("last-error: %.*s\n",
               (int)status.last_error_len,
               (const char *)status.last_error_data);
    }

    croft_orchestration_runtime_destroy(runtime);
    return 0;
}
