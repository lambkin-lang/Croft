#include "orchestration_guest_runtime.h"
#include "thatch_json_guest.h"

typedef struct {
    uint32_t initialized;
    CroftOrchGuestRuntime runtime;
    SapWitOrchestrationWorkerWorldExports exports;
} CroftOrchJsonWorkerState;

static CroftOrchJsonWorkerState g_croft_orch_json_worker;

static int croft_orch_json_run_parser(CroftOrchJsonWorkerState *state,
                                      const CroftOrchGuestWorkerStartup *startup,
                                      const uint8_t **error_out,
                                      uint32_t *error_len_out)
{
    static const char *k_expanded_paths[] = {
        ".features",
        ".items",
    };
    uint8_t thatch_storage[4096];
    CroftGuestJsonDocument doc;
    SapWitOrchestrationTxnResource txn = SAP_WIT_ORCHESTRATION_TXN_RESOURCE_INVALID;
    char collapsed[2048];
    char cursors[2048];
    uint32_t err_pos = 0u;
    int32_t rc;

    if (error_out) {
        *error_out = NULL;
    }
    if (error_len_out) {
        *error_len_out = 0u;
    }

    if (!croft_orch_guest_find_table(startup, "views")
            || !croft_orch_guest_find_table(startup, "cursors")
            || startup->startup_bytes_len == 0u) {
        return ERR_NOT_FOUND;
    }
    rc = croft_guest_json_parse(&doc,
                                thatch_storage,
                                sizeof(thatch_storage),
                                startup->startup_bytes,
                                startup->startup_bytes_len,
                                &err_pos);
    if (rc == ERR_OK) {
        rc = croft_guest_json_render_collapsed_view(&doc,
                                                    k_expanded_paths,
                                                    2u,
                                                    collapsed,
                                                    sizeof(collapsed));
    } else if (error_out && error_len_out) {
        *error_out = (const uint8_t *)"json-parse-parse-failed";
        *error_len_out = 23u;
    }
    if (rc == ERR_OK) {
        rc = croft_guest_json_render_cursor_paths(&doc, cursors, sizeof(cursors));
    } else if (error_out && error_len_out && *error_out == NULL) {
        *error_out = (const uint8_t *)"json-parse-render-failed";
        *error_len_out = 24u;
    }
    if (rc == ERR_OK) {
        rc = croft_orch_guest_store_begin(&state->runtime, startup->db_handle, 0u, &txn);
    } else if (error_out && error_len_out && *error_out == NULL) {
        *error_out = (const uint8_t *)"json-parse-cursors-failed";
        *error_len_out = 25u;
    }
    if (rc == ERR_OK) {
        rc = croft_orch_guest_store_put_cstr(&state->runtime, txn, "views", "main", collapsed);
    } else if (error_out && error_len_out && *error_out == NULL) {
        *error_out = (const uint8_t *)"json-parse-store-begin-failed";
        *error_len_out = 29u;
    }
    if (rc == ERR_OK) {
        rc = croft_orch_guest_store_put_cstr(&state->runtime, txn, "cursors", "main", cursors);
    } else if (error_out && error_len_out && *error_out == NULL) {
        *error_out = (const uint8_t *)"json-parse-store-view-failed";
        *error_len_out = 28u;
    }
    if (rc == ERR_OK) {
        rc = croft_orch_guest_store_put_cstr(&state->runtime,
                                             txn,
                                             "views",
                                             "summary",
                                             "collapsed-view-ready");
    } else if (error_out && error_len_out && *error_out == NULL) {
        *error_out = (const uint8_t *)"json-parse-store-cursors-failed";
        *error_len_out = 31u;
    }
    if (rc == ERR_OK) {
        rc = croft_orch_guest_store_commit(&state->runtime, txn);
    } else if (error_out && error_len_out && *error_out == NULL) {
        *error_out = (const uint8_t *)"json-parse-store-summary-failed";
        *error_len_out = 31u;
    }
    if (rc != ERR_OK) {
        if (txn != SAP_WIT_ORCHESTRATION_TXN_RESOURCE_INVALID) {
            (void)croft_orch_guest_store_abort(&state->runtime, txn);
        }
        if (error_out && error_len_out && *error_out == NULL) {
            *error_out = (const uint8_t *)"json-parse-store-commit-failed";
            *error_len_out = 30u;
        }
    }
    return rc;
}

static int32_t croft_orch_json_worker_run(void *ctx,
                                          const SapWitOrchestrationRunWorker *payload,
                                          SapWitOrchestrationRunWorkerReply *reply_out)
{
    CroftOrchJsonWorkerState *state = (CroftOrchJsonWorkerState *)ctx;
    CroftOrchGuestWorkerStartup startup;
    char worker_name[64];
    const uint8_t *error_text = (const uint8_t *)"json-worker-failed";
    uint32_t error_len = 18u;
    int32_t rc;

    if (!state || !payload || !reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_orchestration_run_worker_reply(reply_out);
    reply_out->case_tag = SAP_WIT_ORCHESTRATION_RUN_WORKER_REPLY_STATUS;

    rc = croft_orch_guest_decode_worker_startup(payload->startup_bytes_data,
                                                payload->startup_bytes_len,
                                                &startup);
    if (rc == ERR_OK) {
        rc = croft_orch_guest_copy_text(worker_name,
                                        sizeof(worker_name),
                                        payload->worker_name_data,
                                        payload->worker_name_len);
    }
    if (rc == ERR_OK) {
        if (croft_orch_guest_text_equals(worker_name, "json-parser")) {
            rc = croft_orch_json_run_parser(state, &startup, &error_text, &error_len);
        } else {
            rc = ERR_NOT_FOUND;
            error_text = (const uint8_t *)"json-worker-unknown";
            error_len = 19u;
        }
    }

    if (rc == ERR_OK) {
        reply_out->val.status.is_v_ok = 1u;
    } else {
        reply_out->val.status.is_v_ok = 0u;
        reply_out->val.status.v_val.err.v_data = error_text;
        reply_out->val.status.v_val.err.v_len = error_len;
        rc = ERR_OK;
    }
    return rc;
}

static int32_t croft_orch_json_worker_init(void)
{
    static const SapWitOrchestrationRunWorkerDispatchOps k_run_ops = {
        .run_worker = croft_orch_json_worker_run,
    };
    int32_t rc;

    if (g_croft_orch_json_worker.initialized) {
        return ERR_OK;
    }
    rc = croft_orch_guest_runtime_init(&g_croft_orch_json_worker.runtime);
    if (rc != ERR_OK) {
        return rc;
    }
    g_croft_orch_json_worker.exports.run_worker_ctx = &g_croft_orch_json_worker;
    g_croft_orch_json_worker.exports.run_worker_ops = &k_run_ops;
    rc = sap_wit_guest_register_orchestration_worker_exports(&g_croft_orch_json_worker.exports);
    if (rc != ERR_OK) {
        return rc;
    }
    g_croft_orch_json_worker.initialized = 1u;
    return ERR_OK;
}

__attribute__((constructor))
static void croft_orch_json_worker_ctor(void)
{
    (void)croft_orch_json_worker_init();
}
