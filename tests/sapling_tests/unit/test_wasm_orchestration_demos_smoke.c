#include "croft/orchestration_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CROFT_ORCH_TOY_BOOTSTRAP_WASM_PATH
#error "CROFT_ORCH_TOY_BOOTSTRAP_WASM_PATH must be defined"
#endif

#ifndef CROFT_ORCH_JSON_BOOTSTRAP_WASM_PATH
#error "CROFT_ORCH_JSON_BOOTSTRAP_WASM_PATH must be defined"
#endif

#define CHECK(expr)                                                                           \
    do {                                                                                      \
        if (!(expr)) {                                                                        \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                 \
            return 1;                                                                         \
        }                                                                                     \
    } while (0)

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    fclose(f);
    return 1;
}

static int run_bootstrap_and_join(croft_orchestration_runtime *runtime,
                                  const char *path,
                                  SapWitOrchestrationSessionResource *session_out)
{
    SapWitOrchestrationSessionResource session = SAP_WIT_ORCHESTRATION_SESSION_RESOURCE_INVALID;
    int32_t rc;

    rc = croft_orchestration_runtime_bootstrap_wasm_path(runtime, path, &session);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_orchestration_runtime_join_session(runtime, session);
    if (rc != ERR_OK) {
        return rc;
    }
    *session_out = session;
    return ERR_OK;
}

static int expect_status(croft_orchestration_runtime *runtime,
                         SapWitOrchestrationSessionResource session,
                         uint8_t phase,
                         uint32_t workers)
{
    SapWitOrchestrationSessionStatus status;

    if (croft_orchestration_runtime_session_status(runtime, session, &status) != ERR_OK) {
        fprintf(stderr, "status-fetch-failed for session=%u\n", session);
        return 0;
    }
    if (!(status.phase == phase && status.worker_count == workers && status.running_count == 0u)) {
        fprintf(stderr,
                "status-mismatch: session=%u phase=%u workers=%u running=%u last_error=%.*s\n",
                session,
                (unsigned)status.phase,
                (unsigned)status.worker_count,
                (unsigned)status.running_count,
                (int)(status.has_last_error ? status.last_error_len : 0u),
                (status.has_last_error && status.last_error_data)
                    ? (const char *)status.last_error_data
                    : "");
    }
    return status.phase == phase && status.worker_count == workers && status.running_count == 0u;
}

static int expect_table_value(croft_orchestration_runtime *runtime,
                              SapWitOrchestrationSessionResource session,
                              const char *table,
                              const char *key,
                              const char *expected_substr)
{
    uint8_t *value = NULL;
    uint32_t value_len = 0u;
    char *text = NULL;
    int ok = 0;

    if (croft_orchestration_runtime_session_get(runtime,
                                                session,
                                                table,
                                                (const uint8_t *)key,
                                                (uint32_t)strlen(key),
                                                &value,
                                                &value_len)
        != ERR_OK) {
        return 0;
    }
    text = (char *)malloc((size_t)value_len + 1u);
    if (!text) {
        croft_orchestration_runtime_free_bytes(value);
        return 0;
    }
    memcpy(text, value, value_len);
    text[value_len] = '\0';
    ok = value_len >= strlen(expected_substr) && strstr(text, expected_substr) != NULL;
    free(text);
    croft_orchestration_runtime_free_bytes(value);
    return ok;
}

int main(void)
{
    croft_orchestration_runtime_config config;
    croft_orchestration_runtime *runtime = NULL;
    SapWitOrchestrationSessionResource toy_session;
    SapWitOrchestrationSessionResource json_session;

    if (!file_exists(CROFT_ORCH_TOY_BOOTSTRAP_WASM_PATH)
            || !file_exists(CROFT_ORCH_JSON_BOOTSTRAP_WASM_PATH)) {
        printf("SKIP: orchestration demo Wasm guests are unavailable.\n");
        return 0;
    }

    croft_orchestration_runtime_config_default(&config);
    runtime = croft_orchestration_runtime_create(&config);
    CHECK(runtime != NULL);

    CHECK(run_bootstrap_and_join(runtime,
                                 CROFT_ORCH_TOY_BOOTSTRAP_WASM_PATH,
                                 &toy_session)
          == ERR_OK);
    CHECK(expect_status(runtime,
                        toy_session,
                        SAP_WIT_ORCHESTRATION_SESSION_PHASE_STOPPED,
                        2u));
    CHECK(expect_table_value(runtime, toy_session, "events", "producer-start", "hello-from-bootstrap"));
    CHECK(expect_table_value(runtime, toy_session, "events", "consumer-seen", "hello-from-bootstrap"));

    CHECK(run_bootstrap_and_join(runtime,
                                 CROFT_ORCH_JSON_BOOTSTRAP_WASM_PATH,
                                 &json_session)
          == ERR_OK);
    CHECK(expect_status(runtime,
                        json_session,
                        SAP_WIT_ORCHESTRATION_SESSION_PHASE_STOPPED,
                        1u));
    CHECK(expect_table_value(runtime, json_session, "views", "main", "{3 keys}"));
    CHECK(expect_table_value(runtime, json_session, "views", "summary", "collapsed-view-ready"));
    CHECK(expect_table_value(runtime, json_session, "cursors", "main", ".features"));
    CHECK(expect_table_value(runtime, json_session, "cursors", "main", ".items"));

    croft_orchestration_runtime_destroy(runtime);
    return 0;
}
