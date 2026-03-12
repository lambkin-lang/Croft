#include "croft/orchestration_runtime.h"

#include "croft/host_thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CROFT_ORCH_MAX_BUILDERS 16u
#define CROFT_ORCH_MAX_SESSIONS 8u
#define CROFT_ORCH_MAX_MODULES 4u
#define CROFT_ORCH_MAX_BUNDLES 128u
#define CROFT_ORCH_MAX_SLOT_PREFERENCES 16u
#define CROFT_ORCH_MAX_PREF_BUNDLES 8u
#define CROFT_ORCH_MAX_TABLES 16u
#define CROFT_ORCH_MAX_MAILBOXES 16u
#define CROFT_ORCH_MAX_WORKERS 16u
#define CROFT_ORCH_MAX_DIAGNOSTICS 32u
#define CROFT_ORCH_MAX_TOTAL_WORKERS 32u
#define CROFT_ORCH_MAX_TXNS_PER_WORKER 16u
#define CROFT_ORCH_TEXT_CAP 160u
#define CROFT_ORCH_PATH_CAP 384u
#define CROFT_ORCH_ERROR_CAP 192u
#define CROFT_ORCH_STARTUP_CONTRACT "lambkin:orchestration/worker-startup@0.1.0"
#define CROFT_ORCH_DEFAULT_MAILBOX_DEPTH 64u

typedef struct {
    uint8_t *data;
    uint32_t len;
} croft_orch_blob;

typedef struct croft_wit_store_runtime croft_wit_store_runtime;
typedef struct croft_wit_store_runtime_config {
    uint32_t default_page_size;
    uint64_t initial_bytes;
    uint64_t max_bytes;
} croft_wit_store_runtime_config;
typedef struct croft_wit_mailbox_runtime croft_wit_mailbox_runtime;

void croft_wit_store_runtime_config_default(croft_wit_store_runtime_config *config);
croft_wit_store_runtime *croft_wit_store_runtime_create(
    const croft_wit_store_runtime_config *config);
void croft_wit_store_runtime_destroy(croft_wit_store_runtime *runtime);
int32_t croft_wit_store_runtime_dispatch(croft_wit_store_runtime *runtime,
                                         const SapWitOrchestrationStoreCommand *command,
                                         SapWitOrchestrationStoreReply *reply_out);
croft_wit_mailbox_runtime *croft_wit_mailbox_runtime_create(void);
void croft_wit_mailbox_runtime_destroy(croft_wit_mailbox_runtime *runtime);
int32_t croft_wit_mailbox_runtime_dispatch(croft_wit_mailbox_runtime *runtime,
                                           const SapWitOrchestrationMailboxCommand *command,
                                           SapWitOrchestrationMailboxReply *reply_out);

typedef struct {
    char slot[CROFT_ORCH_TEXT_CAP];
    char bundles[CROFT_ORCH_MAX_PREF_BUNDLES][CROFT_ORCH_TEXT_CAP];
    uint32_t bundle_count;
} croft_orch_slot_preference;

typedef struct {
    char name[CROFT_ORCH_TEXT_CAP];
    char path[CROFT_ORCH_PATH_CAP];
} croft_orch_payload_module;

typedef struct {
    char name[CROFT_ORCH_TEXT_CAP];
    char key_format[CROFT_ORCH_TEXT_CAP];
    char value_format[CROFT_ORCH_TEXT_CAP];
    uint32_t access;
} croft_orch_table_spec;

typedef struct {
    uint8_t defined;
    char name[CROFT_ORCH_TEXT_CAP];
    croft_orch_table_spec tables[CROFT_ORCH_MAX_TABLES];
    uint32_t table_count;
} croft_orch_db_schema_spec;

typedef struct {
    char name[CROFT_ORCH_TEXT_CAP];
    char message_format[CROFT_ORCH_TEXT_CAP];
    char producers[CROFT_ORCH_MAX_WORKERS][CROFT_ORCH_TEXT_CAP];
    uint32_t producer_count;
    char consumers[CROFT_ORCH_MAX_WORKERS][CROFT_ORCH_TEXT_CAP];
    uint32_t consumer_count;
    uint8_t durability;
} croft_orch_mailbox_spec;

typedef struct {
    char name[CROFT_ORCH_TEXT_CAP];
    char module[CROFT_ORCH_TEXT_CAP];
    uint32_t replicas;
    char allowed_tables[CROFT_ORCH_MAX_TABLES][CROFT_ORCH_TEXT_CAP];
    uint32_t allowed_table_count;
    char inboxes[CROFT_ORCH_MAX_MAILBOXES][CROFT_ORCH_TEXT_CAP];
    uint32_t inbox_count;
    char outboxes[CROFT_ORCH_MAX_MAILBOXES][CROFT_ORCH_TEXT_CAP];
    uint32_t outbox_count;
    uint8_t has_startup_format;
    char startup_format[CROFT_ORCH_TEXT_CAP];
    uint8_t *startup_bytes;
    uint32_t startup_bytes_len;
} croft_orch_worker_spec;

typedef struct {
    char name[CROFT_ORCH_TEXT_CAP];
    char family[CROFT_ORCH_TEXT_CAP];
    char applicability[CROFT_ORCH_TEXT_CAP];
    char required_bundles[CROFT_ORCH_MAX_BUNDLES][CROFT_ORCH_TEXT_CAP];
    uint32_t required_bundle_count;
    croft_orch_slot_preference preferred_slots[CROFT_ORCH_MAX_SLOT_PREFERENCES];
    uint32_t preferred_slot_count;
    croft_orch_payload_module payload_modules[CROFT_ORCH_MAX_MODULES];
    uint32_t payload_module_count;
    croft_orch_db_schema_spec db_schema;
    croft_orch_mailbox_spec mailboxes[CROFT_ORCH_MAX_MAILBOXES];
    uint32_t mailbox_count;
    croft_orch_worker_spec workers[CROFT_ORCH_MAX_WORKERS];
    uint32_t worker_count;
} croft_orch_manifest_spec;

typedef struct {
    char slot[CROFT_ORCH_TEXT_CAP];
    char bundle[CROFT_ORCH_TEXT_CAP];
} croft_orch_slot_binding;

typedef struct {
    char name[CROFT_ORCH_TEXT_CAP];
    char module[CROFT_ORCH_TEXT_CAP];
    uint32_t replicas;
    char allowed_tables[CROFT_ORCH_MAX_TABLES][CROFT_ORCH_TEXT_CAP];
    uint32_t allowed_table_count;
    char inboxes[CROFT_ORCH_MAX_MAILBOXES][CROFT_ORCH_TEXT_CAP];
    uint32_t inbox_count;
    char outboxes[CROFT_ORCH_MAX_MAILBOXES][CROFT_ORCH_TEXT_CAP];
    uint32_t outbox_count;
} croft_orch_resolved_worker;

typedef struct {
    char manifest_name[CROFT_ORCH_TEXT_CAP];
    char family[CROFT_ORCH_TEXT_CAP];
    char applicability[CROFT_ORCH_TEXT_CAP];
    croft_orch_slot_binding selected_slots[CROFT_ORCH_MAX_SLOT_PREFERENCES];
    uint32_t selected_slot_count;
    char required_bundles[CROFT_ORCH_MAX_BUNDLES][CROFT_ORCH_TEXT_CAP];
    uint32_t required_bundle_count;
    char provider_artifacts[CROFT_ORCH_MAX_BUNDLES][CROFT_ORCH_TEXT_CAP];
    uint32_t provider_artifact_count;
    char shared_substrates[CROFT_ORCH_MAX_BUNDLES][CROFT_ORCH_TEXT_CAP];
    uint32_t shared_substrate_count;
    char helper_interfaces[CROFT_ORCH_MAX_BUNDLES][CROFT_ORCH_TEXT_CAP];
    uint32_t helper_interface_count;
    char declared_worlds[CROFT_ORCH_MAX_BUNDLES][CROFT_ORCH_TEXT_CAP];
    uint32_t declared_world_count;
    char expanded_surfaces[CROFT_ORCH_MAX_BUNDLES][CROFT_ORCH_TEXT_CAP];
    uint32_t expanded_surface_count;
    croft_orch_payload_module payload_modules[CROFT_ORCH_MAX_MODULES];
    uint32_t payload_module_count;
    croft_orch_db_schema_spec db_schema;
    croft_orch_mailbox_spec mailboxes[CROFT_ORCH_MAX_MAILBOXES];
    uint32_t mailbox_count;
    croft_orch_resolved_worker workers[CROFT_ORCH_MAX_WORKERS];
    uint32_t worker_count;
    char diagnostics[CROFT_ORCH_MAX_DIAGNOSTICS][CROFT_ORCH_ERROR_CAP];
    uint32_t diagnostic_count;
} croft_orch_plan_spec;

typedef struct {
    croft_orch_blob bytes;
    SapWitOrchestrationManifest view;
} croft_orch_rendered_manifest;

typedef struct {
    croft_orch_blob bytes;
    SapWitOrchestrationPlan view;
} croft_orch_rendered_plan;

typedef struct {
    croft_orch_blob bytes;
    SapWitOrchestrationWorkerStartup view;
} croft_orch_rendered_startup;

typedef struct {
    uint8_t live;
    croft_orch_manifest_spec spec;
    croft_orch_plan_spec resolved;
    uint8_t resolved_valid;
    char last_error[CROFT_ORCH_ERROR_CAP];
    croft_orch_rendered_manifest rendered_manifest;
    croft_orch_rendered_plan rendered_plan;
} croft_orch_builder_slot;

typedef struct {
    char name[CROFT_ORCH_TEXT_CAP];
    SapWitOrchestrationMailboxResource handle;
    char message_format[CROFT_ORCH_TEXT_CAP];
    uint8_t durability;
} croft_orch_named_mailbox;

typedef struct {
    struct croft_orchestration_runtime *runtime;
    struct croft_orch_session_slot *session;
    croft_orch_resolved_worker *worker;
    SapWitOrchestrationDbResource db_handle;
    SapWitOrchestrationMailboxResource inbox_handles[CROFT_ORCH_MAX_MAILBOXES];
    uint32_t inbox_count;
    SapWitOrchestrationMailboxResource outbox_handles[CROFT_ORCH_MAX_MAILBOXES];
    uint32_t outbox_count;
    SapWitOrchestrationTxnResource txns[CROFT_ORCH_MAX_TXNS_PER_WORKER];
    uint32_t txn_count;
} croft_orch_worker_policy;

typedef struct croft_orch_worker_instance {
    struct croft_orch_session_slot *session;
    croft_orch_resolved_worker *worker;
    uint32_t replica_index;
    croft_orch_rendered_startup startup;
    croft_orch_worker_policy policy;
    host_thread_t thread;
    uint8_t thread_started;
    uint8_t joined;
    int32_t rc;
} croft_orch_worker_instance;

typedef struct croft_orch_session_slot {
    struct croft_orchestration_runtime *runtime;
    uint8_t live;
    croft_orch_manifest_spec manifest;
    croft_orch_plan_spec plan;
    croft_orch_rendered_manifest rendered_manifest;
    croft_orch_rendered_plan rendered_plan;
    croft_wit_store_runtime *store_runtime;
    croft_wit_mailbox_runtime *mailbox_runtime;
    SapWitOrchestrationDbResource db_handle;
    croft_orch_named_mailbox mailboxes[CROFT_ORCH_MAX_MAILBOXES];
    uint32_t mailbox_count;
    croft_orch_worker_instance workers[CROFT_ORCH_MAX_TOTAL_WORKERS];
    uint32_t worker_count;
    uint32_t running_count;
    uint8_t phase;
    char last_error[CROFT_ORCH_ERROR_CAP];
    uint8_t *payload_wasm_bytes;
    uint32_t payload_wasm_len;
    char payload_module_name[CROFT_ORCH_TEXT_CAP];
    host_mutex_t state_mutex;
    host_mutex_t store_mutex;
    host_mutex_t mailbox_mutex;
    host_cond_t mailbox_cond;
} croft_orch_session_slot;

struct croft_orchestration_runtime {
    croft_orchestration_runtime_config config;
    const CroftXpiRegistry *registry;
    croft_orch_builder_slot builders[CROFT_ORCH_MAX_BUILDERS];
    croft_orch_session_slot sessions[CROFT_ORCH_MAX_SESSIONS];
    SapWitOrchestrationSessionResource last_session;
    char last_error[CROFT_ORCH_ERROR_CAP];
};

static int32_t croft_orch_control_create(void *ctx,
                                         const SapWitOrchestrationBuilderCreate *payload,
                                         SapWitOrchestrationControlReply *reply_out);
static int32_t croft_orch_control_require_bundle(void *ctx,
                                                 const SapWitOrchestrationBuilderRequireBundle *payload,
                                                 SapWitOrchestrationControlReply *reply_out);
static int32_t croft_orch_control_prefer_slot(void *ctx,
                                              const SapWitOrchestrationBuilderPreferSlot *payload,
                                              SapWitOrchestrationControlReply *reply_out);
static int32_t croft_orch_control_add_module(void *ctx,
                                             const SapWitOrchestrationBuilderAddModule *payload,
                                             SapWitOrchestrationControlReply *reply_out);
static int32_t croft_orch_control_set_db_schema(void *ctx,
                                                const SapWitOrchestrationBuilderSetDbSchema *payload,
                                                SapWitOrchestrationControlReply *reply_out);
static int32_t croft_orch_control_add_mailbox(void *ctx,
                                              const SapWitOrchestrationBuilderAddMailbox *payload,
                                              SapWitOrchestrationControlReply *reply_out);
static int32_t croft_orch_control_add_worker(void *ctx,
                                             const SapWitOrchestrationBuilderAddWorker *payload,
                                             SapWitOrchestrationControlReply *reply_out);
static int32_t croft_orch_control_snapshot(void *ctx,
                                           const SapWitOrchestrationBuilderSnapshot *payload,
                                           SapWitOrchestrationControlReply *reply_out);
static int32_t croft_orch_control_resolve(void *ctx,
                                          const SapWitOrchestrationBuilderResolve *payload,
                                          SapWitOrchestrationControlReply *reply_out);
static int32_t croft_orch_control_launch(void *ctx,
                                         const SapWitOrchestrationBuilderLaunch *payload,
                                         SapWitOrchestrationControlReply *reply_out);
static int32_t croft_orch_control_builder_drop(void *ctx,
                                               const SapWitOrchestrationBuilderDrop *payload,
                                               SapWitOrchestrationControlReply *reply_out);
static int32_t croft_orch_control_manifest(void *ctx,
                                           const SapWitOrchestrationSessionManifest *payload,
                                           SapWitOrchestrationControlReply *reply_out);
static int32_t croft_orch_control_plan(void *ctx,
                                       const SapWitOrchestrationSessionPlan *payload,
                                       SapWitOrchestrationControlReply *reply_out);
static int32_t croft_orch_control_status(void *ctx,
                                         const SapWitOrchestrationSessionStatusRequest *payload,
                                         SapWitOrchestrationControlReply *reply_out);
static int32_t croft_orch_control_stop(void *ctx,
                                       const SapWitOrchestrationSessionStop *payload,
                                       SapWitOrchestrationControlReply *reply_out);
static int32_t croft_orch_control_join(void *ctx,
                                       const SapWitOrchestrationSessionJoin *payload,
                                       SapWitOrchestrationControlReply *reply_out);
static int32_t croft_orch_control_session_drop(void *ctx,
                                               const SapWitOrchestrationSessionDrop *payload,
                                               SapWitOrchestrationControlReply *reply_out);
static int32_t croft_orchestration_runtime_stop_session(croft_orchestration_runtime *runtime,
                                                        SapWitOrchestrationSessionResource session_handle);

static const SapWitOrchestrationControlDispatchOps g_croft_orch_control_ops = {
    .create = croft_orch_control_create,
    .require_bundle = croft_orch_control_require_bundle,
    .prefer_slot = croft_orch_control_prefer_slot,
    .add_module = croft_orch_control_add_module,
    .set_db_schema = croft_orch_control_set_db_schema,
    .add_mailbox = croft_orch_control_add_mailbox,
    .add_worker = croft_orch_control_add_worker,
    .snapshot = croft_orch_control_snapshot,
    .resolve = croft_orch_control_resolve,
    .launch = croft_orch_control_launch,
    .builder_drop = croft_orch_control_builder_drop,
    .manifest = croft_orch_control_manifest,
    .plan = croft_orch_control_plan,
    .status = croft_orch_control_status,
    .stop = croft_orch_control_stop,
    .join = croft_orch_control_join,
    .session_drop = croft_orch_control_session_drop,
};

static int croft_orch_copy_text(char *dest,
                                size_t cap,
                                const uint8_t *data,
                                uint32_t len)
{
    if (!dest || cap == 0u) {
        return ERR_INVALID;
    }
    if (!data && len > 0u) {
        return ERR_INVALID;
    }
    if ((size_t)len >= cap) {
        return ERR_RANGE;
    }
    if (len > 0u) {
        memcpy(dest, data, len);
    }
    dest[len] = '\0';
    return ERR_OK;
}

static const uint8_t *croft_orch_string_data(const char *text)
{
    return (const uint8_t *)(text ? text : "");
}

static uint32_t croft_orch_string_len(const char *text)
{
    return (uint32_t)strlen(text ? text : "");
}

static void croft_orch_set_reply_error(const char *err,
                                       const uint8_t **data_out,
                                       uint32_t *len_out)
{
    if (!data_out || !len_out) {
        return;
    }
    if (!err) {
        err = "internal";
    }
    *data_out = (const uint8_t *)err;
    *len_out = (uint32_t)strlen(err);
}

static const char *croft_orch_set_stable_error(char *storage, size_t storage_cap, const char *err)
{
    const char *text = err ? err : "internal";

    if (!storage || storage_cap == 0u) {
        return text;
    }
    snprintf(storage, storage_cap, "%s", text);
    return storage;
}

static void croft_orch_control_reply_builder_ok(SapWitOrchestrationControlReply *reply_out,
                                                SapWitOrchestrationBuilderResource handle)
{
    sap_wit_zero_orchestration_control_reply(reply_out);
    reply_out->case_tag = SAP_WIT_ORCHESTRATION_CONTROL_REPLY_BUILDER;
    reply_out->val.builder.is_v_ok = 1u;
    reply_out->val.builder.v_val.ok.v = handle;
}

static void croft_orch_control_reply_builder_err(SapWitOrchestrationControlReply *reply_out,
                                                 const char *err)
{
    sap_wit_zero_orchestration_control_reply(reply_out);
    reply_out->case_tag = SAP_WIT_ORCHESTRATION_CONTROL_REPLY_BUILDER;
    reply_out->val.builder.is_v_ok = 0u;
    croft_orch_set_reply_error(err,
                               &reply_out->val.builder.v_val.err.v_data,
                               &reply_out->val.builder.v_val.err.v_len);
}

static void croft_orch_control_reply_status_ok(SapWitOrchestrationControlReply *reply_out)
{
    sap_wit_zero_orchestration_control_reply(reply_out);
    reply_out->case_tag = SAP_WIT_ORCHESTRATION_CONTROL_REPLY_STATUS;
    reply_out->val.status.is_v_ok = 1u;
}

static void croft_orch_control_reply_status_err(SapWitOrchestrationControlReply *reply_out,
                                                const char *err)
{
    sap_wit_zero_orchestration_control_reply(reply_out);
    reply_out->case_tag = SAP_WIT_ORCHESTRATION_CONTROL_REPLY_STATUS;
    reply_out->val.status.is_v_ok = 0u;
    croft_orch_set_reply_error(err,
                               &reply_out->val.status.v_val.err.v_data,
                               &reply_out->val.status.v_val.err.v_len);
}

static void croft_orch_control_reply_manifest(SapWitOrchestrationControlReply *reply_out,
                                              const SapWitOrchestrationManifest *manifest)
{
    sap_wit_zero_orchestration_control_reply(reply_out);
    reply_out->case_tag = SAP_WIT_ORCHESTRATION_CONTROL_REPLY_MANIFEST;
    reply_out->val.manifest = *manifest;
}

static void croft_orch_control_reply_plan_ok(SapWitOrchestrationControlReply *reply_out,
                                             const SapWitOrchestrationPlan *plan)
{
    sap_wit_zero_orchestration_control_reply(reply_out);
    reply_out->case_tag = SAP_WIT_ORCHESTRATION_CONTROL_REPLY_RESOLVE;
    reply_out->val.resolve.is_v_ok = 1u;
    reply_out->val.resolve.v_val.ok.v = *plan;
}

static void croft_orch_control_reply_plan_err(SapWitOrchestrationControlReply *reply_out,
                                              const char *err)
{
    sap_wit_zero_orchestration_control_reply(reply_out);
    reply_out->case_tag = SAP_WIT_ORCHESTRATION_CONTROL_REPLY_RESOLVE;
    reply_out->val.resolve.is_v_ok = 0u;
    croft_orch_set_reply_error(err,
                               &reply_out->val.resolve.v_val.err.v_data,
                               &reply_out->val.resolve.v_val.err.v_len);
}

static void croft_orch_control_reply_session_ok(SapWitOrchestrationControlReply *reply_out,
                                                SapWitOrchestrationSessionResource handle)
{
    sap_wit_zero_orchestration_control_reply(reply_out);
    reply_out->case_tag = SAP_WIT_ORCHESTRATION_CONTROL_REPLY_SESSION;
    reply_out->val.session.is_v_ok = 1u;
    reply_out->val.session.v_val.ok.v = handle;
}

static void croft_orch_control_reply_session_err(SapWitOrchestrationControlReply *reply_out,
                                                 const char *err)
{
    sap_wit_zero_orchestration_control_reply(reply_out);
    reply_out->case_tag = SAP_WIT_ORCHESTRATION_CONTROL_REPLY_SESSION;
    reply_out->val.session.is_v_ok = 0u;
    croft_orch_set_reply_error(err,
                               &reply_out->val.session.v_val.err.v_data,
                               &reply_out->val.session.v_val.err.v_len);
}

static void croft_orch_control_reply_plan_snapshot(SapWitOrchestrationControlReply *reply_out,
                                                   const SapWitOrchestrationPlan *plan)
{
    sap_wit_zero_orchestration_control_reply(reply_out);
    reply_out->case_tag = SAP_WIT_ORCHESTRATION_CONTROL_REPLY_PLAN;
    reply_out->val.plan = *plan;
}

static void croft_orch_control_reply_session_status(SapWitOrchestrationControlReply *reply_out,
                                                    const SapWitOrchestrationSessionStatus *status)
{
    sap_wit_zero_orchestration_control_reply(reply_out);
    reply_out->case_tag = SAP_WIT_ORCHESTRATION_CONTROL_REPLY_SESSION_STATUS;
    reply_out->val.session_status = *status;
}

static uint32_t croft_orch_blob_initial_capacity(uint32_t count)
{
    uint32_t cap = count * 64u + 64u;
    if (cap < 256u) {
        cap = 256u;
    }
    return cap;
}

static void croft_orch_blob_dispose(croft_orch_blob *blob)
{
    if (!blob) {
        return;
    }
    free(blob->data);
    blob->data = NULL;
    blob->len = 0u;
}

static int croft_orch_blob_encode(croft_orch_blob *blob,
                                  uint32_t initial_cap,
                                  int (*encode_fn)(ThatchRegion *region, void *ctx),
                                  void *ctx)
{
    uint32_t cap;

    if (!blob || !encode_fn) {
        return ERR_INVALID;
    }

    croft_orch_blob_dispose(blob);
    cap = initial_cap > 0u ? initial_cap : 256u;
    for (;;) {
        ThatchRegion region;
        int rc;

        blob->data = (uint8_t *)malloc(cap);
        if (!blob->data) {
            return ERR_OOM;
        }
        sap_wit_guest_region_init_writable(&region, blob->data, cap);
        rc = encode_fn(&region, ctx);
        if (rc == ERR_FULL || rc == ERR_OOM) {
            free(blob->data);
            blob->data = NULL;
            if (cap > UINT32_MAX / 2u) {
                return ERR_OOM;
            }
            cap *= 2u;
            continue;
        }
        if (rc != ERR_OK) {
            free(blob->data);
            blob->data = NULL;
            return rc;
        }
        blob->len = thatch_region_used(&region);
        return ERR_OK;
    }
}

typedef struct {
    const char *items;
    size_t stride;
    uint32_t count;
} croft_orch_string_list_encode_ctx;

static int croft_orch_encode_string_list_region(ThatchRegion *region, void *ctx_ptr)
{
    croft_orch_string_list_encode_ctx *ctx = (croft_orch_string_list_encode_ctx *)ctx_ptr;
    uint32_t i;

    if (!region || !ctx) {
        return ERR_INVALID;
    }
    for (i = 0u; i < ctx->count; i++) {
        const char *item = ctx->items + ((size_t)i * ctx->stride);
        uint32_t len = croft_orch_string_len(item);
        if (thatch_write_tag(region, SAP_WIT_TAG_STRING) != ERR_OK
                || thatch_write_data(region, &len, sizeof(len)) != ERR_OK
                || (len > 0u && thatch_write_data(region, item, len) != ERR_OK)) {
            return ERR_FULL;
        }
    }
    return ERR_OK;
}

static int croft_orch_encode_string_list(const char items[][CROFT_ORCH_TEXT_CAP],
                                         uint32_t count,
                                         croft_orch_blob *blob)
{
    croft_orch_string_list_encode_ctx ctx;

    if (!blob) {
        return ERR_INVALID;
    }
    if (count == 0u) {
        croft_orch_blob_dispose(blob);
        return ERR_OK;
    }
    ctx.items = (const char *)items;
    ctx.stride = sizeof(items[0]);
    ctx.count = count;
    return croft_orch_blob_encode(blob,
                                  croft_orch_blob_initial_capacity(count),
                                  croft_orch_encode_string_list_region,
                                  &ctx);
}

static int croft_orch_encode_string_list_cap(const char *items,
                                             size_t stride,
                                             uint32_t count,
                                             croft_orch_blob *blob)
{
    croft_orch_string_list_encode_ctx ctx;

    if (!blob) {
        return ERR_INVALID;
    }
    croft_orch_blob_dispose(blob);
    if (count == 0u) {
        return ERR_OK;
    }
    ctx.items = items;
    ctx.stride = stride;
    ctx.count = count;
    return croft_orch_blob_encode(blob,
                                  croft_orch_blob_initial_capacity(count),
                                  croft_orch_encode_string_list_region,
                                  &ctx);
}

typedef int (*croft_orch_record_encode_fn)(ThatchRegion *region, const void *record);

typedef struct {
    const void *records;
    uint32_t count;
    size_t stride;
    croft_orch_record_encode_fn write_record;
} croft_orch_record_list_encode_ctx;

static int croft_orch_encode_record_list_region(ThatchRegion *region, void *ctx_ptr)
{
    croft_orch_record_list_encode_ctx *ctx = (croft_orch_record_list_encode_ctx *)ctx_ptr;
    uint32_t i;

    if (!region || !ctx || !ctx->write_record) {
        return ERR_INVALID;
    }
    for (i = 0u; i < ctx->count; i++) {
        const void *record = (const uint8_t *)ctx->records + ((size_t)i * ctx->stride);
        int rc = ctx->write_record(region, record);
        if (rc != ERR_OK) {
            return rc == ERR_FULL ? ERR_FULL : rc;
        }
    }
    return ERR_OK;
}

static int croft_orch_encode_record_list(const void *records,
                                         uint32_t count,
                                         size_t stride,
                                         croft_orch_record_encode_fn write_record,
                                         croft_orch_blob *blob)
{
    croft_orch_record_list_encode_ctx ctx;

    if (!blob) {
        return ERR_INVALID;
    }
    if (count == 0u) {
        croft_orch_blob_dispose(blob);
        return ERR_OK;
    }
    ctx.records = records;
    ctx.count = count;
    ctx.stride = stride;
    ctx.write_record = write_record;
    return croft_orch_blob_encode(blob,
                                  croft_orch_blob_initial_capacity(count),
                                  croft_orch_encode_record_list_region,
                                  &ctx);
}

static int croft_orch_string_list_contains(const char items[][CROFT_ORCH_TEXT_CAP],
                                           uint32_t count,
                                           const char *value)
{
    uint32_t i;

    if (!value) {
        return 0;
    }
    for (i = 0u; i < count; i++) {
        if (strcmp(items[i], value) == 0) {
            return 1;
        }
    }
    return 0;
}

static int croft_orch_string_list_add_unique(char items[][CROFT_ORCH_TEXT_CAP],
                                             uint32_t *count_io,
                                             uint32_t cap,
                                             const char *value)
{
    if (!items || !count_io || !value || value[0] == '\0') {
        return ERR_INVALID;
    }
    if (croft_orch_string_list_contains(items, *count_io, value)) {
        return ERR_OK;
    }
    if (*count_io >= cap) {
        return ERR_RANGE;
    }
    if (strlen(value) >= CROFT_ORCH_TEXT_CAP) {
        return ERR_RANGE;
    }
    strcpy(items[*count_io], value);
    (*count_io)++;
    return ERR_OK;
}

static int croft_orch_string_list_add_len_unique(char items[][CROFT_ORCH_TEXT_CAP],
                                                 uint32_t *count_io,
                                                 uint32_t cap,
                                                 const char *value,
                                                 size_t len)
{
    char temp[CROFT_ORCH_TEXT_CAP];

    if (!value) {
        return ERR_INVALID;
    }
    if (len >= sizeof(temp)) {
        return ERR_RANGE;
    }
    memcpy(temp, value, len);
    temp[len] = '\0';
    return croft_orch_string_list_add_unique(items, count_io, cap, temp);
}

static int croft_orch_parse_string_list(const uint8_t *data,
                                        uint32_t byte_len,
                                        uint32_t count,
                                        char items[][CROFT_ORCH_TEXT_CAP],
                                        uint32_t *count_out,
                                        uint32_t cap)
{
    ThatchRegion region;
    ThatchCursor cursor = 0u;
    uint8_t tag;
    uint32_t i;

    if (!count_out) {
        return ERR_INVALID;
    }
    *count_out = 0u;
    if (count == 0u) {
        if (byte_len != 0u) {
            return ERR_CORRUPT;
        }
        return ERR_OK;
    }
    if (cap == 0u) {
        return ERR_RANGE;
    }
    if (!data || byte_len == 0u) {
        return ERR_CORRUPT;
    }
    if (thatch_region_init_readonly(&region, data, byte_len) != ERR_OK) {
        return ERR_INVALID;
    }
    if (count > cap) {
        return ERR_RANGE;
    }
    for (i = 0u; i < count; i++) {
        const void *ptr = NULL;
        uint32_t len = 0u;

        if (thatch_read_tag(&region, &cursor, &tag) != ERR_OK
                || (tag != SAP_WIT_TAG_STRING && tag != SAP_WIT_TAG_BYTES)
                || thatch_read_data(&region, &cursor, sizeof(len), &len) != ERR_OK
                || thatch_read_ptr(&region, &cursor, len, &ptr) != ERR_OK) {
            return ERR_CORRUPT;
        }
        if (croft_orch_copy_text(items[i], CROFT_ORCH_TEXT_CAP, (const uint8_t *)ptr, len) != ERR_OK) {
            return ERR_RANGE;
        }
    }
    if (cursor != byte_len) {
        return ERR_CORRUPT;
    }
    *count_out = count;
    return ERR_OK;
}

static int croft_orch_write_slot_preference_record(ThatchRegion *region, const void *record)
{
    const croft_orch_slot_preference *pref = (const croft_orch_slot_preference *)record;
    SapWitOrchestrationSlotPreference wit_pref = {0};
    croft_orch_blob bundles = {0};
    int rc;

    rc = croft_orch_encode_string_list(pref->bundles, pref->bundle_count, &bundles);
    if (rc != ERR_OK) {
        return rc;
    }

    wit_pref.slot_data = croft_orch_string_data(pref->slot);
    wit_pref.slot_len = croft_orch_string_len(pref->slot);
    wit_pref.bundles_data = bundles.data;
    wit_pref.bundles_len = pref->bundle_count;
    wit_pref.bundles_byte_len = bundles.len;
    rc = sap_wit_write_orchestration_slot_preference(region, &wit_pref);
    croft_orch_blob_dispose(&bundles);
    return rc;
}

static int croft_orch_write_payload_module_record(ThatchRegion *region, const void *record)
{
    const croft_orch_payload_module *module = (const croft_orch_payload_module *)record;
    SapWitOrchestrationPayloadModuleSpec wit_module = {0};

    wit_module.name_data = croft_orch_string_data(module->name);
    wit_module.name_len = croft_orch_string_len(module->name);
    wit_module.path_data = croft_orch_string_data(module->path);
    wit_module.path_len = croft_orch_string_len(module->path);
    return sap_wit_write_orchestration_payload_module_spec(region, &wit_module);
}

static int croft_orch_write_table_spec_record(ThatchRegion *region, const void *record)
{
    const croft_orch_table_spec *table = (const croft_orch_table_spec *)record;
    SapWitOrchestrationTableSpec wit_table = {0};

    wit_table.name_data = croft_orch_string_data(table->name);
    wit_table.name_len = croft_orch_string_len(table->name);
    wit_table.key_format_data = croft_orch_string_data(table->key_format);
    wit_table.key_format_len = croft_orch_string_len(table->key_format);
    wit_table.value_format_data = croft_orch_string_data(table->value_format);
    wit_table.value_format_len = croft_orch_string_len(table->value_format);
    wit_table.access = table->access;
    return sap_wit_write_orchestration_table_spec(region, &wit_table);
}

static int croft_orch_render_db_schema_wit(const croft_orch_db_schema_spec *schema,
                                           SapWitOrchestrationDbSchemaSpec *out_wit,
                                           croft_orch_blob *tables_blob)
{
    int rc;

    if (!schema || !out_wit || !tables_blob) {
        return ERR_INVALID;
    }
    memset(out_wit, 0, sizeof(*out_wit));
    croft_orch_blob_dispose(tables_blob);
    if (!schema->defined) {
        return ERR_OK;
    }
    rc = croft_orch_encode_record_list(schema->tables,
                                       schema->table_count,
                                       sizeof(schema->tables[0]),
                                       croft_orch_write_table_spec_record,
                                       tables_blob);
    if (rc != ERR_OK) {
        return rc;
    }
    out_wit->name_data = croft_orch_string_data(schema->name);
    out_wit->name_len = croft_orch_string_len(schema->name);
    out_wit->tables_data = tables_blob->data;
    out_wit->tables_len = schema->table_count;
    out_wit->tables_byte_len = tables_blob->len;
    return ERR_OK;
}

static int croft_orch_write_mailbox_record(ThatchRegion *region, const void *record)
{
    const croft_orch_mailbox_spec *mailbox = (const croft_orch_mailbox_spec *)record;
    SapWitOrchestrationMailboxSpec wit_mailbox = {0};
    croft_orch_blob producers = {0};
    croft_orch_blob consumers = {0};
    int rc;

    rc = croft_orch_encode_string_list(mailbox->producers, mailbox->producer_count, &producers);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_orch_encode_string_list(mailbox->consumers, mailbox->consumer_count, &consumers);
    if (rc != ERR_OK) {
        croft_orch_blob_dispose(&producers);
        return rc;
    }

    wit_mailbox.name_data = croft_orch_string_data(mailbox->name);
    wit_mailbox.name_len = croft_orch_string_len(mailbox->name);
    wit_mailbox.message_format_data = croft_orch_string_data(mailbox->message_format);
    wit_mailbox.message_format_len = croft_orch_string_len(mailbox->message_format);
    wit_mailbox.producers_data = producers.data;
    wit_mailbox.producers_len = mailbox->producer_count;
    wit_mailbox.producers_byte_len = producers.len;
    wit_mailbox.consumers_data = consumers.data;
    wit_mailbox.consumers_len = mailbox->consumer_count;
    wit_mailbox.consumers_byte_len = consumers.len;
    wit_mailbox.durability = mailbox->durability;
    rc = sap_wit_write_orchestration_mailbox_spec(region, &wit_mailbox);
    croft_orch_blob_dispose(&producers);
    croft_orch_blob_dispose(&consumers);
    return rc;
}

static int croft_orch_write_worker_spec_record(ThatchRegion *region, const void *record)
{
    const croft_orch_worker_spec *worker = (const croft_orch_worker_spec *)record;
    SapWitOrchestrationWorkerSpec wit_worker = {0};
    croft_orch_blob tables = {0};
    croft_orch_blob inboxes = {0};
    croft_orch_blob outboxes = {0};
    int rc;

    rc = croft_orch_encode_string_list(worker->allowed_tables, worker->allowed_table_count, &tables);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_orch_encode_string_list(worker->inboxes, worker->inbox_count, &inboxes);
    if (rc != ERR_OK) {
        croft_orch_blob_dispose(&tables);
        return rc;
    }
    rc = croft_orch_encode_string_list(worker->outboxes, worker->outbox_count, &outboxes);
    if (rc != ERR_OK) {
        croft_orch_blob_dispose(&tables);
        croft_orch_blob_dispose(&inboxes);
        return rc;
    }

    wit_worker.name_data = croft_orch_string_data(worker->name);
    wit_worker.name_len = croft_orch_string_len(worker->name);
    wit_worker.module_data = croft_orch_string_data(worker->module);
    wit_worker.module_len = croft_orch_string_len(worker->module);
    wit_worker.replicas = worker->replicas;
    wit_worker.allowed_tables_data = tables.data;
    wit_worker.allowed_tables_len = worker->allowed_table_count;
    wit_worker.allowed_tables_byte_len = tables.len;
    wit_worker.inboxes_data = inboxes.data;
    wit_worker.inboxes_len = worker->inbox_count;
    wit_worker.inboxes_byte_len = inboxes.len;
    wit_worker.outboxes_data = outboxes.data;
    wit_worker.outboxes_len = worker->outbox_count;
    wit_worker.outboxes_byte_len = outboxes.len;
    wit_worker.has_startup_format = worker->has_startup_format;
    wit_worker.startup_format_data = worker->has_startup_format
                                         ? croft_orch_string_data(worker->startup_format)
                                         : NULL;
    wit_worker.startup_format_len = worker->has_startup_format
                                        ? croft_orch_string_len(worker->startup_format)
                                        : 0u;
    wit_worker.startup_bytes_data = worker->startup_bytes;
    wit_worker.startup_bytes_len = worker->startup_bytes_len;
    rc = sap_wit_write_orchestration_worker_spec(region, &wit_worker);
    croft_orch_blob_dispose(&tables);
    croft_orch_blob_dispose(&inboxes);
    croft_orch_blob_dispose(&outboxes);
    return rc;
}

static int croft_orch_write_resolved_worker_record(ThatchRegion *region, const void *record)
{
    const croft_orch_resolved_worker *worker = (const croft_orch_resolved_worker *)record;
    SapWitOrchestrationResolvedWorker wit_worker = {0};
    croft_orch_blob tables = {0};
    croft_orch_blob inboxes = {0};
    croft_orch_blob outboxes = {0};
    int rc;

    rc = croft_orch_encode_string_list(worker->allowed_tables, worker->allowed_table_count, &tables);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_orch_encode_string_list(worker->inboxes, worker->inbox_count, &inboxes);
    if (rc != ERR_OK) {
        croft_orch_blob_dispose(&tables);
        return rc;
    }
    rc = croft_orch_encode_string_list(worker->outboxes, worker->outbox_count, &outboxes);
    if (rc != ERR_OK) {
        croft_orch_blob_dispose(&tables);
        croft_orch_blob_dispose(&inboxes);
        return rc;
    }

    wit_worker.name_data = croft_orch_string_data(worker->name);
    wit_worker.name_len = croft_orch_string_len(worker->name);
    wit_worker.module_data = croft_orch_string_data(worker->module);
    wit_worker.module_len = croft_orch_string_len(worker->module);
    wit_worker.replicas = worker->replicas;
    wit_worker.allowed_tables_data = tables.data;
    wit_worker.allowed_tables_len = worker->allowed_table_count;
    wit_worker.allowed_tables_byte_len = tables.len;
    wit_worker.inboxes_data = inboxes.data;
    wit_worker.inboxes_len = worker->inbox_count;
    wit_worker.inboxes_byte_len = inboxes.len;
    wit_worker.outboxes_data = outboxes.data;
    wit_worker.outboxes_len = worker->outbox_count;
    wit_worker.outboxes_byte_len = outboxes.len;
    rc = sap_wit_write_orchestration_resolved_worker(region, &wit_worker);
    croft_orch_blob_dispose(&tables);
    croft_orch_blob_dispose(&inboxes);
    croft_orch_blob_dispose(&outboxes);
    return rc;
}

typedef struct {
    const croft_orch_manifest_spec *spec;
} croft_orch_manifest_encode_ctx;

static int croft_orch_encode_manifest_region(ThatchRegion *region, void *ctx_ptr)
{
    croft_orch_manifest_encode_ctx *ctx = (croft_orch_manifest_encode_ctx *)ctx_ptr;
    const croft_orch_manifest_spec *spec = ctx ? ctx->spec : NULL;
    SapWitOrchestrationManifest manifest = {0};
    SapWitOrchestrationDbSchemaSpec db_schema = {0};
    croft_orch_blob required = {0};
    croft_orch_blob preferred = {0};
    croft_orch_blob modules = {0};
    croft_orch_blob tables = {0};
    croft_orch_blob mailboxes = {0};
    croft_orch_blob workers = {0};
    int rc;

    if (!region || !spec) {
        return ERR_INVALID;
    }

    rc = croft_orch_encode_string_list(spec->required_bundles,
                                       spec->required_bundle_count,
                                       &required);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    rc = croft_orch_encode_record_list(spec->preferred_slots,
                                       spec->preferred_slot_count,
                                       sizeof(spec->preferred_slots[0]),
                                       croft_orch_write_slot_preference_record,
                                       &preferred);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    rc = croft_orch_encode_record_list(spec->payload_modules,
                                       spec->payload_module_count,
                                       sizeof(spec->payload_modules[0]),
                                       croft_orch_write_payload_module_record,
                                       &modules);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    rc = croft_orch_render_db_schema_wit(&spec->db_schema, &db_schema, &tables);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    rc = croft_orch_encode_record_list(spec->mailboxes,
                                       spec->mailbox_count,
                                       sizeof(spec->mailboxes[0]),
                                       croft_orch_write_mailbox_record,
                                       &mailboxes);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    rc = croft_orch_encode_record_list(spec->workers,
                                       spec->worker_count,
                                       sizeof(spec->workers[0]),
                                       croft_orch_write_worker_spec_record,
                                       &workers);
    if (rc != ERR_OK) {
        goto cleanup;
    }

    manifest.name_data = croft_orch_string_data(spec->name);
    manifest.name_len = croft_orch_string_len(spec->name);
    manifest.family_data = croft_orch_string_data(spec->family);
    manifest.family_len = croft_orch_string_len(spec->family);
    manifest.applicability_data = croft_orch_string_data(spec->applicability);
    manifest.applicability_len = croft_orch_string_len(spec->applicability);
    manifest.required_bundles_data = required.data;
    manifest.required_bundles_len = spec->required_bundle_count;
    manifest.required_bundles_byte_len = required.len;
    manifest.preferred_slots_data = preferred.data;
    manifest.preferred_slots_len = spec->preferred_slot_count;
    manifest.preferred_slots_byte_len = preferred.len;
    manifest.payload_modules_data = modules.data;
    manifest.payload_modules_len = spec->payload_module_count;
    manifest.payload_modules_byte_len = modules.len;
    manifest.has_db_schema = spec->db_schema.defined;
    manifest.db_schema = db_schema;
    manifest.mailboxes_data = mailboxes.data;
    manifest.mailboxes_len = spec->mailbox_count;
    manifest.mailboxes_byte_len = mailboxes.len;
    manifest.workers_data = workers.data;
    manifest.workers_len = spec->worker_count;
    manifest.workers_byte_len = workers.len;
    rc = sap_wit_write_orchestration_manifest(region, &manifest);

cleanup:
    croft_orch_blob_dispose(&required);
    croft_orch_blob_dispose(&preferred);
    croft_orch_blob_dispose(&modules);
    croft_orch_blob_dispose(&tables);
    croft_orch_blob_dispose(&mailboxes);
    croft_orch_blob_dispose(&workers);
    return rc;
}

typedef struct {
    const croft_orch_plan_spec *spec;
} croft_orch_plan_encode_ctx;

static int croft_orch_write_slot_binding_record(ThatchRegion *region, const void *record)
{
    const croft_orch_slot_binding *binding = (const croft_orch_slot_binding *)record;
    SapWitOrchestrationSlotBinding wit_binding = {0};

    wit_binding.slot_data = croft_orch_string_data(binding->slot);
    wit_binding.slot_len = croft_orch_string_len(binding->slot);
    wit_binding.bundle_data = croft_orch_string_data(binding->bundle);
    wit_binding.bundle_len = croft_orch_string_len(binding->bundle);
    return sap_wit_write_orchestration_slot_binding(region, &wit_binding);
}

static int croft_orch_encode_plan_region(ThatchRegion *region, void *ctx_ptr)
{
    croft_orch_plan_encode_ctx *ctx = (croft_orch_plan_encode_ctx *)ctx_ptr;
    const croft_orch_plan_spec *spec = ctx ? ctx->spec : NULL;
    SapWitOrchestrationPlan plan = {0};
    SapWitOrchestrationDbSchemaSpec db_schema = {0};
    croft_orch_blob selected = {0};
    croft_orch_blob required = {0};
    croft_orch_blob artifacts = {0};
    croft_orch_blob substrates = {0};
    croft_orch_blob helpers = {0};
    croft_orch_blob worlds = {0};
    croft_orch_blob surfaces = {0};
    croft_orch_blob modules = {0};
    croft_orch_blob tables = {0};
    croft_orch_blob mailboxes = {0};
    croft_orch_blob workers = {0};
    croft_orch_blob diagnostics = {0};
    int rc;

    if (!region || !spec) {
        return ERR_INVALID;
    }

    rc = croft_orch_encode_record_list(spec->selected_slots,
                                       spec->selected_slot_count,
                                       sizeof(spec->selected_slots[0]),
                                       croft_orch_write_slot_binding_record,
                                       &selected);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    rc = croft_orch_encode_string_list(spec->required_bundles, spec->required_bundle_count, &required);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    rc = croft_orch_encode_string_list(spec->provider_artifacts, spec->provider_artifact_count, &artifacts);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    rc = croft_orch_encode_string_list(spec->shared_substrates, spec->shared_substrate_count, &substrates);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    rc = croft_orch_encode_string_list(spec->helper_interfaces, spec->helper_interface_count, &helpers);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    rc = croft_orch_encode_string_list(spec->declared_worlds, spec->declared_world_count, &worlds);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    rc = croft_orch_encode_string_list(spec->expanded_surfaces, spec->expanded_surface_count, &surfaces);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    rc = croft_orch_encode_record_list(spec->payload_modules,
                                       spec->payload_module_count,
                                       sizeof(spec->payload_modules[0]),
                                       croft_orch_write_payload_module_record,
                                       &modules);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    rc = croft_orch_render_db_schema_wit(&spec->db_schema, &db_schema, &tables);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    rc = croft_orch_encode_record_list(spec->mailboxes,
                                       spec->mailbox_count,
                                       sizeof(spec->mailboxes[0]),
                                       croft_orch_write_mailbox_record,
                                       &mailboxes);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    rc = croft_orch_encode_record_list(spec->workers,
                                       spec->worker_count,
                                       sizeof(spec->workers[0]),
                                       croft_orch_write_resolved_worker_record,
                                       &workers);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    rc = croft_orch_encode_string_list_cap((const char *)spec->diagnostics,
                                           sizeof(spec->diagnostics[0]),
                                           spec->diagnostic_count,
                                           &diagnostics);
    if (rc != ERR_OK) {
        goto cleanup;
    }

    plan.manifest_name_data = croft_orch_string_data(spec->manifest_name);
    plan.manifest_name_len = croft_orch_string_len(spec->manifest_name);
    plan.family_data = croft_orch_string_data(spec->family);
    plan.family_len = croft_orch_string_len(spec->family);
    plan.applicability_data = croft_orch_string_data(spec->applicability);
    plan.applicability_len = croft_orch_string_len(spec->applicability);
    plan.selected_slots_data = selected.data;
    plan.selected_slots_len = spec->selected_slot_count;
    plan.selected_slots_byte_len = selected.len;
    plan.required_bundles_data = required.data;
    plan.required_bundles_len = spec->required_bundle_count;
    plan.required_bundles_byte_len = required.len;
    plan.provider_artifacts_data = artifacts.data;
    plan.provider_artifacts_len = spec->provider_artifact_count;
    plan.provider_artifacts_byte_len = artifacts.len;
    plan.shared_substrates_data = substrates.data;
    plan.shared_substrates_len = spec->shared_substrate_count;
    plan.shared_substrates_byte_len = substrates.len;
    plan.helper_interfaces_data = helpers.data;
    plan.helper_interfaces_len = spec->helper_interface_count;
    plan.helper_interfaces_byte_len = helpers.len;
    plan.declared_worlds_data = worlds.data;
    plan.declared_worlds_len = spec->declared_world_count;
    plan.declared_worlds_byte_len = worlds.len;
    plan.expanded_surfaces_data = surfaces.data;
    plan.expanded_surfaces_len = spec->expanded_surface_count;
    plan.expanded_surfaces_byte_len = surfaces.len;
    plan.payload_modules_data = modules.data;
    plan.payload_modules_len = spec->payload_module_count;
    plan.payload_modules_byte_len = modules.len;
    plan.has_db_schema = spec->db_schema.defined;
    plan.db_schema = db_schema;
    plan.mailboxes_data = mailboxes.data;
    plan.mailboxes_len = spec->mailbox_count;
    plan.mailboxes_byte_len = mailboxes.len;
    plan.workers_data = workers.data;
    plan.workers_len = spec->worker_count;
    plan.workers_byte_len = workers.len;
    plan.diagnostics_data = diagnostics.data;
    plan.diagnostics_len = spec->diagnostic_count;
    plan.diagnostics_byte_len = diagnostics.len;
    rc = sap_wit_write_orchestration_plan(region, &plan);

cleanup:
    croft_orch_blob_dispose(&selected);
    croft_orch_blob_dispose(&required);
    croft_orch_blob_dispose(&artifacts);
    croft_orch_blob_dispose(&substrates);
    croft_orch_blob_dispose(&helpers);
    croft_orch_blob_dispose(&worlds);
    croft_orch_blob_dispose(&surfaces);
    croft_orch_blob_dispose(&modules);
    croft_orch_blob_dispose(&tables);
    croft_orch_blob_dispose(&mailboxes);
    croft_orch_blob_dispose(&workers);
    croft_orch_blob_dispose(&diagnostics);
    return rc;
}

typedef struct {
    uint32_t db_handle;
    croft_orch_table_spec *tables;
    uint32_t table_count;
    croft_orch_named_mailbox *inboxes;
    uint32_t inbox_count;
    croft_orch_named_mailbox *outboxes;
    uint32_t outbox_count;
    uint8_t has_startup_format;
    const char *startup_format;
    const uint8_t *startup_bytes;
    uint32_t startup_bytes_len;
} croft_orch_worker_startup_encode_ctx;

static int croft_orch_write_table_binding_record(ThatchRegion *region, const void *record)
{
    const croft_orch_table_spec *table = (const croft_orch_table_spec *)record;
    SapWitOrchestrationTableBinding binding = {0};

    binding.name_data = croft_orch_string_data(table->name);
    binding.name_len = croft_orch_string_len(table->name);
    binding.key_format_data = croft_orch_string_data(table->key_format);
    binding.key_format_len = croft_orch_string_len(table->key_format);
    binding.value_format_data = croft_orch_string_data(table->value_format);
    binding.value_format_len = croft_orch_string_len(table->value_format);
    binding.access = table->access;
    return sap_wit_write_orchestration_table_binding(region, &binding);
}

static int croft_orch_write_mailbox_binding_record(ThatchRegion *region, const void *record)
{
    const croft_orch_named_mailbox *mailbox = (const croft_orch_named_mailbox *)record;
    SapWitOrchestrationMailboxBinding binding = {0};

    binding.name_data = croft_orch_string_data(mailbox->name);
    binding.name_len = croft_orch_string_len(mailbox->name);
    binding.handle = mailbox->handle;
    binding.message_format_data = croft_orch_string_data(mailbox->message_format);
    binding.message_format_len = croft_orch_string_len(mailbox->message_format);
    binding.durability = mailbox->durability;
    return sap_wit_write_orchestration_mailbox_binding(region, &binding);
}

static int croft_orch_encode_worker_startup_region(ThatchRegion *region, void *ctx_ptr)
{
    croft_orch_worker_startup_encode_ctx *ctx = (croft_orch_worker_startup_encode_ctx *)ctx_ptr;
    SapWitOrchestrationWorkerStartup startup = {0};
    croft_orch_blob tables = {0};
    croft_orch_blob inboxes = {0};
    croft_orch_blob outboxes = {0};
    int rc;

    if (!region || !ctx) {
        return ERR_INVALID;
    }

    rc = croft_orch_encode_record_list(ctx->tables,
                                       ctx->table_count,
                                       sizeof(ctx->tables[0]),
                                       croft_orch_write_table_binding_record,
                                       &tables);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    rc = croft_orch_encode_record_list(ctx->inboxes,
                                       ctx->inbox_count,
                                       sizeof(ctx->inboxes[0]),
                                       croft_orch_write_mailbox_binding_record,
                                       &inboxes);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    rc = croft_orch_encode_record_list(ctx->outboxes,
                                       ctx->outbox_count,
                                       sizeof(ctx->outboxes[0]),
                                       croft_orch_write_mailbox_binding_record,
                                       &outboxes);
    if (rc != ERR_OK) {
        goto cleanup;
    }

    startup.db_handle = ctx->db_handle;
    startup.tables_data = tables.data;
    startup.tables_len = ctx->table_count;
    startup.tables_byte_len = tables.len;
    startup.inboxes_data = inboxes.data;
    startup.inboxes_len = ctx->inbox_count;
    startup.inboxes_byte_len = inboxes.len;
    startup.outboxes_data = outboxes.data;
    startup.outboxes_len = ctx->outbox_count;
    startup.outboxes_byte_len = outboxes.len;
    startup.has_startup_format = ctx->has_startup_format;
    startup.startup_format_data = ctx->has_startup_format
                                      ? croft_orch_string_data(ctx->startup_format)
                                      : NULL;
    startup.startup_format_len = ctx->has_startup_format
                                     ? croft_orch_string_len(ctx->startup_format)
                                     : 0u;
    startup.startup_bytes_data = ctx->startup_bytes;
    startup.startup_bytes_len = ctx->startup_bytes_len;
    rc = sap_wit_write_orchestration_worker_startup(region, &startup);

cleanup:
    croft_orch_blob_dispose(&tables);
    croft_orch_blob_dispose(&inboxes);
    croft_orch_blob_dispose(&outboxes);
    return rc;
}

static void croft_orch_worker_spec_clear(croft_orch_worker_spec *worker)
{
    if (!worker) {
        return;
    }
    free(worker->startup_bytes);
    memset(worker, 0, sizeof(*worker));
}

static void croft_orch_manifest_spec_clear(croft_orch_manifest_spec *spec)
{
    uint32_t i;

    if (!spec) {
        return;
    }
    for (i = 0u; i < spec->worker_count; i++) {
        croft_orch_worker_spec_clear(&spec->workers[i]);
    }
    memset(spec, 0, sizeof(*spec));
}

static int32_t croft_orch_manifest_spec_clone(croft_orch_manifest_spec *dst,
                                              const croft_orch_manifest_spec *src)
{
    uint32_t i;

    if (!dst || !src) {
        return ERR_INVALID;
    }
    memset(dst, 0, sizeof(*dst));
    memcpy(dst, src, sizeof(*dst));
    for (i = 0u; i < CROFT_ORCH_MAX_WORKERS; i++) {
        dst->workers[i].startup_bytes = NULL;
        dst->workers[i].startup_bytes_len = 0u;
    }
    for (i = 0u; i < src->worker_count; i++) {
        if (src->workers[i].startup_bytes_len > 0u) {
            dst->workers[i].startup_bytes = (uint8_t *)malloc(src->workers[i].startup_bytes_len);
            if (!dst->workers[i].startup_bytes) {
                croft_orch_manifest_spec_clear(dst);
                return ERR_OOM;
            }
            memcpy(dst->workers[i].startup_bytes,
                   src->workers[i].startup_bytes,
                   src->workers[i].startup_bytes_len);
            dst->workers[i].startup_bytes_len = src->workers[i].startup_bytes_len;
        }
    }
    return ERR_OK;
}

static void croft_orch_rendered_manifest_clear(croft_orch_rendered_manifest *rendered)
{
    if (!rendered) {
        return;
    }
    croft_orch_blob_dispose(&rendered->bytes);
    memset(&rendered->view, 0, sizeof(rendered->view));
}

static void croft_orch_rendered_plan_clear(croft_orch_rendered_plan *rendered)
{
    if (!rendered) {
        return;
    }
    croft_orch_blob_dispose(&rendered->bytes);
    memset(&rendered->view, 0, sizeof(rendered->view));
}

static void croft_orch_rendered_startup_clear(croft_orch_rendered_startup *rendered)
{
    if (!rendered) {
        return;
    }
    croft_orch_blob_dispose(&rendered->bytes);
    memset(&rendered->view, 0, sizeof(rendered->view));
}

static void croft_orch_builder_slot_clear(croft_orch_builder_slot *slot)
{
    if (!slot) {
        return;
    }
    croft_orch_manifest_spec_clear(&slot->spec);
    memset(&slot->resolved, 0, sizeof(slot->resolved));
    croft_orch_rendered_manifest_clear(&slot->rendered_manifest);
    croft_orch_rendered_plan_clear(&slot->rendered_plan);
    memset(slot, 0, sizeof(*slot));
}

static void croft_orch_session_slot_clear(croft_orch_session_slot *slot)
{
    uint32_t i;

    if (!slot) {
        return;
    }
    for (i = 0u; i < slot->worker_count; i++) {
        croft_orch_rendered_startup_clear(&slot->workers[i].startup);
    }
    croft_orch_manifest_spec_clear(&slot->manifest);
    croft_orch_rendered_manifest_clear(&slot->rendered_manifest);
    croft_orch_rendered_plan_clear(&slot->rendered_plan);
    free(slot->payload_wasm_bytes);
    if (slot->store_runtime) {
        croft_wit_store_runtime_destroy(slot->store_runtime);
    }
    if (slot->mailbox_runtime) {
        croft_wit_mailbox_runtime_destroy(slot->mailbox_runtime);
    }
    if (slot->live) {
        host_mutex_destroy(&slot->state_mutex);
        host_mutex_destroy(&slot->store_mutex);
        host_mutex_destroy(&slot->mailbox_mutex);
        host_cond_destroy(&slot->mailbox_cond);
    }
    memset(slot, 0, sizeof(*slot));
}

static int croft_orch_render_manifest(croft_orch_rendered_manifest *out,
                                      const croft_orch_manifest_spec *spec)
{
    croft_orch_manifest_encode_ctx ctx;
    ThatchRegion region;
    ThatchCursor cursor = 0u;
    int rc;

    if (!out || !spec) {
        return ERR_INVALID;
    }

    croft_orch_rendered_manifest_clear(out);
    ctx.spec = spec;
    rc = croft_orch_blob_encode(&out->bytes, 1024u, croft_orch_encode_manifest_region, &ctx);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = thatch_region_init_readonly(&region, out->bytes.data, out->bytes.len);
    if (rc != ERR_OK) {
        croft_orch_rendered_manifest_clear(out);
        return rc;
    }
    rc = sap_wit_read_orchestration_manifest(&region, &cursor, &out->view);
    if (rc != ERR_OK || cursor != out->bytes.len) {
        croft_orch_rendered_manifest_clear(out);
        return rc == ERR_OK ? ERR_CORRUPT : rc;
    }
    return ERR_OK;
}

static int croft_orch_render_plan(croft_orch_rendered_plan *out,
                                  const croft_orch_plan_spec *spec)
{
    croft_orch_plan_encode_ctx ctx;
    ThatchRegion region;
    ThatchCursor cursor = 0u;
    int rc;

    if (!out || !spec) {
        return ERR_INVALID;
    }

    croft_orch_rendered_plan_clear(out);
    ctx.spec = spec;
    rc = croft_orch_blob_encode(&out->bytes, 2048u, croft_orch_encode_plan_region, &ctx);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = thatch_region_init_readonly(&region, out->bytes.data, out->bytes.len);
    if (rc != ERR_OK) {
        croft_orch_rendered_plan_clear(out);
        return rc;
    }
    rc = sap_wit_read_orchestration_plan(&region, &cursor, &out->view);
    if (rc != ERR_OK || cursor != out->bytes.len) {
        croft_orch_rendered_plan_clear(out);
        return rc == ERR_OK ? ERR_CORRUPT : rc;
    }
    return ERR_OK;
}

static int croft_orch_render_worker_startup(croft_orch_rendered_startup *out,
                                            uint32_t db_handle,
                                            croft_orch_table_spec *tables,
                                            uint32_t table_count,
                                            croft_orch_named_mailbox *inboxes,
                                            uint32_t inbox_count,
                                            croft_orch_named_mailbox *outboxes,
                                            uint32_t outbox_count,
                                            const croft_orch_worker_spec *worker)
{
    croft_orch_worker_startup_encode_ctx ctx;
    ThatchRegion region;
    ThatchCursor cursor = 0u;
    int rc;

    if (!out) {
        return ERR_INVALID;
    }

    croft_orch_rendered_startup_clear(out);
    memset(&ctx, 0, sizeof(ctx));
    ctx.db_handle = db_handle;
    ctx.tables = tables;
    ctx.table_count = table_count;
    ctx.inboxes = inboxes;
    ctx.inbox_count = inbox_count;
    ctx.outboxes = outboxes;
    ctx.outbox_count = outbox_count;
    ctx.has_startup_format = worker ? worker->has_startup_format : 0u;
    ctx.startup_format = (worker && worker->has_startup_format) ? worker->startup_format : NULL;
    ctx.startup_bytes = worker ? worker->startup_bytes : NULL;
    ctx.startup_bytes_len = worker ? worker->startup_bytes_len : 0u;
    rc = croft_orch_blob_encode(&out->bytes, 1024u, croft_orch_encode_worker_startup_region, &ctx);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = thatch_region_init_readonly(&region, out->bytes.data, out->bytes.len);
    if (rc != ERR_OK) {
        croft_orch_rendered_startup_clear(out);
        return rc;
    }
    rc = sap_wit_read_orchestration_worker_startup(&region, &cursor, &out->view);
    if (rc != ERR_OK || cursor != out->bytes.len) {
        croft_orch_rendered_startup_clear(out);
        return rc == ERR_OK ? ERR_CORRUPT : rc;
    }
    return ERR_OK;
}

static croft_orch_builder_slot *croft_orch_builder_lookup(croft_orchestration_runtime *runtime,
                                                          SapWitOrchestrationBuilderResource handle)
{
    uint32_t index;

    if (!runtime || handle == SAP_WIT_ORCHESTRATION_BUILDER_RESOURCE_INVALID) {
        return NULL;
    }
    index = handle - 1u;
    if (index >= CROFT_ORCH_MAX_BUILDERS || !runtime->builders[index].live) {
        return NULL;
    }
    return &runtime->builders[index];
}

static croft_orch_session_slot *croft_orch_session_lookup(croft_orchestration_runtime *runtime,
                                                          SapWitOrchestrationSessionResource handle)
{
    uint32_t index;

    if (!runtime || handle == SAP_WIT_ORCHESTRATION_SESSION_RESOURCE_INVALID) {
        return NULL;
    }
    index = handle - 1u;
    if (index >= CROFT_ORCH_MAX_SESSIONS || !runtime->sessions[index].live) {
        return NULL;
    }
    return &runtime->sessions[index];
}

static int croft_orch_allocate_builder(croft_orchestration_runtime *runtime,
                                       SapWitOrchestrationBuilderResource *handle_out)
{
    uint32_t i;

    if (!runtime || !handle_out) {
        return ERR_INVALID;
    }
    for (i = 0u; i < CROFT_ORCH_MAX_BUILDERS; i++) {
        if (!runtime->builders[i].live) {
            runtime->builders[i].live = 1u;
            *handle_out = i + 1u;
            return ERR_OK;
        }
    }
    return ERR_RANGE;
}

static int croft_orch_allocate_session(croft_orchestration_runtime *runtime,
                                       SapWitOrchestrationSessionResource *handle_out)
{
    uint32_t i;

    if (!runtime || !handle_out) {
        return ERR_INVALID;
    }
    for (i = 0u; i < CROFT_ORCH_MAX_SESSIONS; i++) {
        if (!runtime->sessions[i].live) {
            memset(&runtime->sessions[i], 0, sizeof(runtime->sessions[i]));
            runtime->sessions[i].runtime = runtime;
            runtime->sessions[i].live = 1u;
            if (host_mutex_init(&runtime->sessions[i].state_mutex) != 0
                    || host_mutex_init(&runtime->sessions[i].store_mutex) != 0
                    || host_mutex_init(&runtime->sessions[i].mailbox_mutex) != 0
                    || host_cond_init(&runtime->sessions[i].mailbox_cond) != 0) {
                croft_orch_session_slot_clear(&runtime->sessions[i]);
                return ERR_INVALID;
            }
            *handle_out = i + 1u;
            return ERR_OK;
        }
    }
    return ERR_RANGE;
}

static int croft_orch_manifest_add_required_bundle(croft_orch_manifest_spec *spec, const char *bundle)
{
    return croft_orch_string_list_add_unique(spec->required_bundles,
                                             &spec->required_bundle_count,
                                             CROFT_ORCH_MAX_BUNDLES,
                                             bundle);
}

static croft_orch_slot_preference *croft_orch_manifest_find_preference(croft_orch_manifest_spec *spec,
                                                                       const char *slot)
{
    uint32_t i;

    if (!spec || !slot) {
        return NULL;
    }
    for (i = 0u; i < spec->preferred_slot_count; i++) {
        if (strcmp(spec->preferred_slots[i].slot, slot) == 0) {
            return &spec->preferred_slots[i];
        }
    }
    return NULL;
}

static croft_orch_payload_module *croft_orch_manifest_find_module(croft_orch_manifest_spec *spec,
                                                                  const char *name)
{
    uint32_t i;

    if (!spec || !name) {
        return NULL;
    }
    for (i = 0u; i < spec->payload_module_count; i++) {
        if (strcmp(spec->payload_modules[i].name, name) == 0) {
            return &spec->payload_modules[i];
        }
    }
    return NULL;
}

static croft_orch_mailbox_spec *croft_orch_manifest_find_mailbox(croft_orch_manifest_spec *spec,
                                                                 const char *name)
{
    uint32_t i;

    if (!spec || !name) {
        return NULL;
    }
    for (i = 0u; i < spec->mailbox_count; i++) {
        if (strcmp(spec->mailboxes[i].name, name) == 0) {
            return &spec->mailboxes[i];
        }
    }
    return NULL;
}

static croft_orch_worker_spec *croft_orch_manifest_find_worker(croft_orch_manifest_spec *spec,
                                                               const char *name)
{
    uint32_t i;

    if (!spec || !name) {
        return NULL;
    }
    for (i = 0u; i < spec->worker_count; i++) {
        if (strcmp(spec->workers[i].name, name) == 0) {
            return &spec->workers[i];
        }
    }
    return NULL;
}

static const croft_orch_table_spec *croft_orch_manifest_find_table(const croft_orch_manifest_spec *spec,
                                                                   const char *name)
{
    uint32_t i;

    if (!spec || !spec->db_schema.defined || !name) {
        return NULL;
    }
    for (i = 0u; i < spec->db_schema.table_count; i++) {
        if (strcmp(spec->db_schema.tables[i].name, name) == 0) {
            return &spec->db_schema.tables[i];
        }
    }
    return NULL;
}

static int croft_orch_parse_slot_preference_list(const uint8_t *data,
                                                 uint32_t byte_len,
                                                 uint32_t count,
                                                 croft_orch_slot_preference *items,
                                                 uint32_t *count_out,
                                                 uint32_t cap)
{
    ThatchRegion region;
    ThatchCursor cursor = 0u;
    uint32_t i;

    if (!count_out) {
        return ERR_INVALID;
    }
    *count_out = 0u;
    if (count == 0u) {
        if (byte_len != 0u) {
            return ERR_CORRUPT;
        }
        return ERR_OK;
    }
    if (!data || byte_len == 0u) {
        return ERR_CORRUPT;
    }
    if (thatch_region_init_readonly(&region, data, byte_len) != ERR_OK) {
        return ERR_INVALID;
    }
    if (count > cap) {
        return ERR_RANGE;
    }
    for (i = 0u; i < count; i++) {
        SapWitOrchestrationSlotPreference pref = {0};
        int rc = sap_wit_read_orchestration_slot_preference(&region, &cursor, &pref);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_orch_copy_text(items[i].slot, sizeof(items[i].slot), pref.slot_data, pref.slot_len);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_orch_parse_string_list(pref.bundles_data,
                                          pref.bundles_byte_len,
                                          pref.bundles_len,
                                          items[i].bundles,
                                          &items[i].bundle_count,
                                          CROFT_ORCH_MAX_PREF_BUNDLES);
        if (rc != ERR_OK) {
            return rc;
        }
    }
    if (cursor != byte_len) {
        return ERR_CORRUPT;
    }
    *count_out = count;
    return ERR_OK;
}

static int croft_orch_parse_payload_module_list(const uint8_t *data,
                                                uint32_t byte_len,
                                                uint32_t count,
                                                croft_orch_payload_module *items,
                                                uint32_t *count_out,
                                                uint32_t cap)
{
    ThatchRegion region;
    ThatchCursor cursor = 0u;
    uint32_t i;

    if (!count_out) {
        return ERR_INVALID;
    }
    *count_out = 0u;
    if (count == 0u) {
        if (byte_len != 0u) {
            return ERR_CORRUPT;
        }
        return ERR_OK;
    }
    if (!data || byte_len == 0u) {
        return ERR_CORRUPT;
    }
    if (thatch_region_init_readonly(&region, data, byte_len) != ERR_OK) {
        return ERR_INVALID;
    }
    if (count > cap) {
        return ERR_RANGE;
    }
    for (i = 0u; i < count; i++) {
        SapWitOrchestrationPayloadModuleSpec module = {0};
        int rc = sap_wit_read_orchestration_payload_module_spec(&region, &cursor, &module);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_orch_copy_text(items[i].name, sizeof(items[i].name), module.name_data, module.name_len);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_orch_copy_text(items[i].path, sizeof(items[i].path), module.path_data, module.path_len);
        if (rc != ERR_OK) {
            return rc;
        }
    }
    if (cursor != byte_len) {
        return ERR_CORRUPT;
    }
    *count_out = count;
    return ERR_OK;
}

static int croft_orch_parse_db_schema(const SapWitOrchestrationDbSchemaSpec *schema,
                                      croft_orch_db_schema_spec *out)
{
    ThatchRegion region;
    ThatchCursor cursor = 0u;
    uint32_t i;

    if (!out) {
        return ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    if (!schema || !schema->name_data) {
        return ERR_OK;
    }
    out->defined = 1u;
    if (croft_orch_copy_text(out->name, sizeof(out->name), schema->name_data, schema->name_len) != ERR_OK) {
        return ERR_RANGE;
    }
    if (schema->tables_len == 0u) {
        if (schema->tables_byte_len != 0u) {
            return ERR_CORRUPT;
        }
        return ERR_OK;
    }
    if (!schema->tables_data || schema->tables_byte_len == 0u) {
        return ERR_CORRUPT;
    }
    if (thatch_region_init_readonly(&region, schema->tables_data, schema->tables_byte_len) != ERR_OK) {
        return ERR_INVALID;
    }
    if (schema->tables_len > CROFT_ORCH_MAX_TABLES) {
        return ERR_RANGE;
    }
    for (i = 0u; i < schema->tables_len; i++) {
        SapWitOrchestrationTableSpec table = {0};
        int rc = sap_wit_read_orchestration_table_spec(&region, &cursor, &table);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_orch_copy_text(out->tables[i].name, sizeof(out->tables[i].name), table.name_data, table.name_len);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_orch_copy_text(out->tables[i].key_format,
                                  sizeof(out->tables[i].key_format),
                                  table.key_format_data,
                                  table.key_format_len);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_orch_copy_text(out->tables[i].value_format,
                                  sizeof(out->tables[i].value_format),
                                  table.value_format_data,
                                  table.value_format_len);
        if (rc != ERR_OK) {
            return rc;
        }
        out->tables[i].access = table.access;
    }
    if (cursor != schema->tables_byte_len) {
        return ERR_CORRUPT;
    }
    out->table_count = schema->tables_len;
    return ERR_OK;
}

static int croft_orch_parse_mailbox_list(const uint8_t *data,
                                         uint32_t byte_len,
                                         uint32_t count,
                                         croft_orch_mailbox_spec *items,
                                         uint32_t *count_out,
                                         uint32_t cap)
{
    ThatchRegion region;
    ThatchCursor cursor = 0u;
    uint32_t i;

    if (!count_out) {
        return ERR_INVALID;
    }
    *count_out = 0u;
    if (count == 0u) {
        if (byte_len != 0u) {
            return ERR_CORRUPT;
        }
        return ERR_OK;
    }
    if (!data || byte_len == 0u) {
        return ERR_CORRUPT;
    }
    if (thatch_region_init_readonly(&region, data, byte_len) != ERR_OK) {
        return ERR_INVALID;
    }
    if (count > cap) {
        return ERR_RANGE;
    }
    for (i = 0u; i < count; i++) {
        SapWitOrchestrationMailboxSpec mailbox = {0};
        int rc = sap_wit_read_orchestration_mailbox_spec(&region, &cursor, &mailbox);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_orch_copy_text(items[i].name, sizeof(items[i].name), mailbox.name_data, mailbox.name_len);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_orch_copy_text(items[i].message_format,
                                  sizeof(items[i].message_format),
                                  mailbox.message_format_data,
                                  mailbox.message_format_len);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_orch_parse_string_list(mailbox.producers_data,
                                          mailbox.producers_byte_len,
                                          mailbox.producers_len,
                                          items[i].producers,
                                          &items[i].producer_count,
                                          CROFT_ORCH_MAX_WORKERS);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_orch_parse_string_list(mailbox.consumers_data,
                                          mailbox.consumers_byte_len,
                                          mailbox.consumers_len,
                                          items[i].consumers,
                                          &items[i].consumer_count,
                                          CROFT_ORCH_MAX_WORKERS);
        if (rc != ERR_OK) {
            return rc;
        }
        items[i].durability = mailbox.durability;
    }
    if (cursor != byte_len) {
        return ERR_CORRUPT;
    }
    *count_out = count;
    return ERR_OK;
}

static int croft_orch_parse_worker_list(const uint8_t *data,
                                        uint32_t byte_len,
                                        uint32_t count,
                                        croft_orch_worker_spec *items,
                                        uint32_t *count_out,
                                        uint32_t cap)
{
    ThatchRegion region;
    ThatchCursor cursor = 0u;
    uint32_t i;

    if (!count_out) {
        return ERR_INVALID;
    }
    *count_out = 0u;
    if (count == 0u) {
        if (byte_len != 0u) {
            return ERR_CORRUPT;
        }
        return ERR_OK;
    }
    if (!data || byte_len == 0u) {
        return ERR_CORRUPT;
    }
    if (thatch_region_init_readonly(&region, data, byte_len) != ERR_OK) {
        return ERR_INVALID;
    }
    if (count > cap) {
        return ERR_RANGE;
    }
    for (i = 0u; i < count; i++) {
        SapWitOrchestrationWorkerSpec worker = {0};
        int rc = sap_wit_read_orchestration_worker_spec(&region, &cursor, &worker);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_orch_copy_text(items[i].name, sizeof(items[i].name), worker.name_data, worker.name_len);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_orch_copy_text(items[i].module, sizeof(items[i].module), worker.module_data, worker.module_len);
        if (rc != ERR_OK) {
            return rc;
        }
        items[i].replicas = worker.replicas;
        rc = croft_orch_parse_string_list(worker.allowed_tables_data,
                                          worker.allowed_tables_byte_len,
                                          worker.allowed_tables_len,
                                          items[i].allowed_tables,
                                          &items[i].allowed_table_count,
                                          CROFT_ORCH_MAX_TABLES);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_orch_parse_string_list(worker.inboxes_data,
                                          worker.inboxes_byte_len,
                                          worker.inboxes_len,
                                          items[i].inboxes,
                                          &items[i].inbox_count,
                                          CROFT_ORCH_MAX_MAILBOXES);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_orch_parse_string_list(worker.outboxes_data,
                                          worker.outboxes_byte_len,
                                          worker.outboxes_len,
                                          items[i].outboxes,
                                          &items[i].outbox_count,
                                          CROFT_ORCH_MAX_MAILBOXES);
        if (rc != ERR_OK) {
            return rc;
        }
        items[i].has_startup_format = worker.has_startup_format;
        if (worker.has_startup_format) {
            rc = croft_orch_copy_text(items[i].startup_format,
                                      sizeof(items[i].startup_format),
                                      worker.startup_format_data,
                                      worker.startup_format_len);
            if (rc != ERR_OK) {
                return rc;
            }
        }
        if (worker.startup_bytes_len > 0u) {
            items[i].startup_bytes = (uint8_t *)malloc(worker.startup_bytes_len);
            if (!items[i].startup_bytes) {
                return ERR_OOM;
            }
            memcpy(items[i].startup_bytes, worker.startup_bytes_data, worker.startup_bytes_len);
            items[i].startup_bytes_len = worker.startup_bytes_len;
        }
    }
    if (cursor != byte_len) {
        return ERR_CORRUPT;
    }
    *count_out = count;
    return ERR_OK;
}

static int croft_orch_plan_add_diagnostic(croft_orch_plan_spec *plan, const char *message)
{
    if (!plan || !message) {
        return ERR_INVALID;
    }
    if (plan->diagnostic_count >= CROFT_ORCH_MAX_DIAGNOSTICS) {
        return ERR_RANGE;
    }
    if (strlen(message) >= CROFT_ORCH_ERROR_CAP) {
        return ERR_RANGE;
    }
    strcpy(plan->diagnostics[plan->diagnostic_count], message);
    plan->diagnostic_count++;
    return ERR_OK;
}

static int croft_orch_plan_add_bundle(croft_orch_plan_spec *plan, const char *bundle)
{
    return croft_orch_string_list_add_unique(plan->required_bundles,
                                             &plan->required_bundle_count,
                                             CROFT_ORCH_MAX_BUNDLES,
                                             bundle);
}

static int croft_orch_plan_add_artifact(croft_orch_plan_spec *plan, const char *name)
{
    return croft_orch_string_list_add_unique(plan->provider_artifacts,
                                             &plan->provider_artifact_count,
                                             CROFT_ORCH_MAX_BUNDLES,
                                             name);
}

static int croft_orch_plan_add_substrate(croft_orch_plan_spec *plan, const char *name)
{
    return croft_orch_string_list_add_unique(plan->shared_substrates,
                                             &plan->shared_substrate_count,
                                             CROFT_ORCH_MAX_BUNDLES,
                                             name);
}

static int croft_orch_plan_add_helper(croft_orch_plan_spec *plan, const char *name)
{
    return croft_orch_string_list_add_unique(plan->helper_interfaces,
                                             &plan->helper_interface_count,
                                             CROFT_ORCH_MAX_BUNDLES,
                                             name);
}

static int croft_orch_plan_add_world(croft_orch_plan_spec *plan, const char *name)
{
    return croft_orch_string_list_add_unique(plan->declared_worlds,
                                             &plan->declared_world_count,
                                             CROFT_ORCH_MAX_BUNDLES,
                                             name);
}

static int croft_orch_plan_add_surface(croft_orch_plan_spec *plan, const char *name)
{
    return croft_orch_string_list_add_unique(plan->expanded_surfaces,
                                             &plan->expanded_surface_count,
                                             CROFT_ORCH_MAX_BUNDLES,
                                             name);
}

static int croft_orch_copy_cursor_items(char items[][CROFT_ORCH_TEXT_CAP],
                                        uint32_t *count_io,
                                        uint32_t cap,
                                        const char *joined)
{
    CroftXpiListCursor cursor;
    const char *item = NULL;
    size_t len = 0u;
    int rc;

    if (!count_io) {
        return ERR_INVALID;
    }
    if (!joined || joined[0] == '\0') {
        return ERR_OK;
    }
    croft_xpi_list_cursor_init(&cursor, joined);
    while (croft_xpi_list_cursor_next(&cursor, &item, &len)) {
        rc = croft_orch_string_list_add_len_unique(items, count_io, cap, item, len);
        if (rc != ERR_OK) {
            return rc;
        }
    }
    return ERR_OK;
}

static int croft_orch_plan_copy_bundle_fields(croft_orch_plan_spec *plan,
                                              const CroftXpiBundleDescriptor *bundle)
{
    int rc;

    if (!plan || !bundle) {
        return ERR_INVALID;
    }
    rc = croft_orch_copy_cursor_items(plan->provider_artifacts,
                                      &plan->provider_artifact_count,
                                      CROFT_ORCH_MAX_BUNDLES,
                                      bundle->artifacts);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_orch_copy_cursor_items(plan->shared_substrates,
                                      &plan->shared_substrate_count,
                                      CROFT_ORCH_MAX_BUNDLES,
                                      bundle->substrates);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_orch_copy_cursor_items(plan->helper_interfaces,
                                      &plan->helper_interface_count,
                                      CROFT_ORCH_MAX_BUNDLES,
                                      bundle->helper_interfaces);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_orch_copy_cursor_items(plan->declared_worlds,
                                      &plan->declared_world_count,
                                      CROFT_ORCH_MAX_BUNDLES,
                                      bundle->declared_worlds);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_orch_copy_cursor_items(plan->expanded_surfaces,
                                      &plan->expanded_surface_count,
                                      CROFT_ORCH_MAX_BUNDLES,
                                      bundle->expanded_surfaces);
    if (rc != ERR_OK) {
        return rc;
    }
    return ERR_OK;
}

static const CroftXpiBundleDescriptor *croft_orch_find_selected_bundle(const croft_orch_plan_spec *plan,
                                                                       const CroftXpiRegistry *registry,
                                                                       const char *bundle_name)
{
    if (!plan || !registry || !bundle_name) {
        return NULL;
    }
    if (!croft_orch_string_list_contains(plan->required_bundles, plan->required_bundle_count, bundle_name)) {
        return NULL;
    }
    return croft_xpi_find_bundle(registry, bundle_name);
}

static int croft_orch_select_slot_bundle(croft_orch_plan_spec *plan,
                                         const CroftXpiRegistry *registry,
                                         const CroftXpiSlotDescriptor *slot,
                                         const croft_orch_slot_preference *pref)
{
    CroftXpiListCursor cursor;
    const char *item = NULL;
    size_t len = 0u;
    char bundle_name[CROFT_ORCH_TEXT_CAP];

    if (!plan || !registry || !slot) {
        return ERR_INVALID;
    }

    if (pref) {
        uint32_t i;
        for (i = 0u; i < pref->bundle_count; i++) {
            if (!croft_xpi_list_contains(slot->bundles, pref->bundles[i])) {
                continue;
            }
            if (croft_orch_string_list_contains(plan->required_bundles,
                                                plan->required_bundle_count,
                                                pref->bundles[i])) {
                strcpy(plan->selected_slots[plan->selected_slot_count].slot, slot->name);
                strcpy(plan->selected_slots[plan->selected_slot_count].bundle, pref->bundles[i]);
                plan->selected_slot_count++;
                return ERR_OK;
            }
            if (croft_xpi_find_bundle(registry, pref->bundles[i])) {
                strcpy(plan->selected_slots[plan->selected_slot_count].slot, slot->name);
                strcpy(plan->selected_slots[plan->selected_slot_count].bundle, pref->bundles[i]);
                plan->selected_slot_count++;
                return croft_orch_plan_add_bundle(plan, pref->bundles[i]);
            }
        }
    }

    croft_xpi_list_cursor_init(&cursor, slot->bundles);
    while (croft_xpi_list_cursor_next(&cursor, &item, &len)) {
        const CroftXpiBundleDescriptor *bundle;

        if (len >= sizeof(bundle_name)) {
            return ERR_RANGE;
        }
        memcpy(bundle_name, item, len);
        bundle_name[len] = '\0';
        bundle = croft_xpi_find_bundle(registry, bundle_name);
        if (!bundle) {
            continue;
        }
        if (!croft_xpi_applicability_matches(bundle->applicability_traits,
                                             registry->context->current_machine_traits)) {
            continue;
        }
        strcpy(plan->selected_slots[plan->selected_slot_count].slot, slot->name);
        strcpy(plan->selected_slots[plan->selected_slot_count].bundle, bundle_name);
        plan->selected_slot_count++;
        return croft_orch_plan_add_bundle(plan, bundle_name);
    }
    return ERR_NOT_FOUND;
}

static int croft_orch_plan_has_conflict(const croft_orch_plan_spec *plan,
                                        const CroftXpiBundleDescriptor *bundle)
{
    CroftXpiListCursor cursor;
    const char *item = NULL;
    size_t len = 0u;
    char name[CROFT_ORCH_TEXT_CAP];

    if (!plan || !bundle || !bundle->conflicts_with) {
        return 0;
    }
    croft_xpi_list_cursor_init(&cursor, bundle->conflicts_with);
    while (croft_xpi_list_cursor_next(&cursor, &item, &len)) {
        if (len >= sizeof(name)) {
            continue;
        }
        memcpy(name, item, len);
        name[len] = '\0';
        if (croft_orch_string_list_contains(plan->required_bundles, plan->required_bundle_count, name)) {
            return 1;
        }
    }
    return 0;
}

static int croft_orch_resolve_manifest(const croft_orch_manifest_spec *manifest,
                                       const CroftXpiRegistry *registry,
                                       croft_orch_plan_spec *plan,
                                       char *err_out,
                                       size_t err_cap)
{
    const CroftXpiEntrypointDescriptor *entrypoint;
    uint32_t i;

    if (err_out && err_cap > 0u) {
        err_out[0] = '\0';
    }
    if (!manifest || !registry || !plan) {
        return ERR_INVALID;
    }

    memset(plan, 0, sizeof(*plan));
    strncpy(plan->manifest_name, manifest->name, sizeof(plan->manifest_name) - 1u);
    strncpy(plan->family, manifest->family, sizeof(plan->family) - 1u);
    strncpy(plan->applicability, manifest->applicability, sizeof(plan->applicability) - 1u);
    memcpy(plan->payload_modules, manifest->payload_modules, sizeof(plan->payload_modules));
    plan->payload_module_count = manifest->payload_module_count;
    memcpy(&plan->db_schema, &manifest->db_schema, sizeof(plan->db_schema));
    memcpy(plan->mailboxes, manifest->mailboxes, sizeof(plan->mailboxes));
    plan->mailbox_count = manifest->mailbox_count;
    for (i = 0u; i < manifest->worker_count; i++) {
        croft_orch_resolved_worker *dst = &plan->workers[i];
        const croft_orch_worker_spec *src = &manifest->workers[i];

        strncpy(dst->name, src->name, sizeof(dst->name) - 1u);
        strncpy(dst->module, src->module, sizeof(dst->module) - 1u);
        dst->replicas = src->replicas;
        memcpy(dst->allowed_tables, src->allowed_tables, sizeof(dst->allowed_tables));
        dst->allowed_table_count = src->allowed_table_count;
        memcpy(dst->inboxes, src->inboxes, sizeof(dst->inboxes));
        dst->inbox_count = src->inbox_count;
        memcpy(dst->outboxes, src->outboxes, sizeof(dst->outboxes));
        dst->outbox_count = src->outbox_count;
    }
    plan->worker_count = manifest->worker_count;

    if (!croft_xpi_applicability_spec_matches(manifest->applicability,
                                              registry->context->current_machine_traits)) {
        snprintf(err_out, err_cap, "applicability-mismatch");
        return ERR_INVALID;
    }

    entrypoint = croft_xpi_find_entrypoint(registry, manifest->family);
    if (!entrypoint) {
        snprintf(err_out, err_cap, "unknown-family");
        return ERR_NOT_FOUND;
    }
    if (!croft_xpi_applicability_matches(entrypoint->applicability_traits,
                                         registry->context->current_machine_traits)) {
        snprintf(err_out, err_cap, "family-applicability-mismatch");
        return ERR_INVALID;
    }

    if (entrypoint->requires_bundles && entrypoint->requires_bundles[0] != '\0') {
        CroftXpiListCursor cursor;
        const char *item = NULL;
        size_t len = 0u;

        croft_xpi_list_cursor_init(&cursor, entrypoint->requires_bundles);
        while (croft_xpi_list_cursor_next(&cursor, &item, &len)) {
            int rc = croft_orch_string_list_add_len_unique(plan->required_bundles,
                                                           &plan->required_bundle_count,
                                                           CROFT_ORCH_MAX_BUNDLES,
                                                           item,
                                                           len);
            if (rc != ERR_OK) {
                snprintf(err_out, err_cap, "bundle-requirements-overflow");
                return rc;
            }
        }
    }
    for (i = 0u; i < manifest->required_bundle_count; i++) {
        int rc = croft_orch_plan_add_bundle(plan, manifest->required_bundles[i]);
        if (rc != ERR_OK) {
            snprintf(err_out, err_cap, "bundle-requirements-overflow");
            return rc;
        }
    }

    if (entrypoint->open_slots && entrypoint->open_slots[0] != '\0') {
        CroftXpiListCursor cursor;
        const char *item = NULL;
        size_t len = 0u;
        char slot_name[CROFT_ORCH_TEXT_CAP];

        croft_xpi_list_cursor_init(&cursor, entrypoint->open_slots);
        while (croft_xpi_list_cursor_next(&cursor, &item, &len)) {
            const CroftXpiSlotDescriptor *slot;
            croft_orch_slot_preference *pref;
            int rc;

            if (len >= sizeof(slot_name) || plan->selected_slot_count >= CROFT_ORCH_MAX_SLOT_PREFERENCES) {
                return ERR_RANGE;
            }
            memcpy(slot_name, item, len);
            slot_name[len] = '\0';
            slot = croft_xpi_find_slot(registry, slot_name);
            if (!slot) {
                snprintf(err_out, err_cap, "unknown-slot");
                return ERR_NOT_FOUND;
            }
            pref = croft_orch_manifest_find_preference((croft_orch_manifest_spec *)manifest, slot_name);
            rc = croft_orch_select_slot_bundle(plan, registry, slot, pref);
            if (rc != ERR_OK) {
                snprintf(err_out, err_cap, "unresolved-slot");
                return rc;
            }
        }
    }

    for (i = 0u; i < plan->required_bundle_count; ) {
        const CroftXpiBundleDescriptor *bundle = croft_xpi_find_bundle(registry, plan->required_bundles[i]);
        if (!bundle) {
            snprintf(err_out, err_cap, "unknown-bundle");
            return ERR_NOT_FOUND;
        }
        if (!croft_xpi_applicability_matches(bundle->applicability_traits,
                                             registry->context->current_machine_traits)) {
            snprintf(err_out, err_cap, "bundle-applicability-mismatch");
            return ERR_INVALID;
        }
        if (croft_orch_plan_has_conflict(plan, bundle)) {
            snprintf(err_out, err_cap, "bundle-conflict");
            return ERR_CONFLICT;
        }
        if (bundle->requires_bundles && bundle->requires_bundles[0] != '\0') {
            CroftXpiListCursor cursor;
            const char *item = NULL;
            size_t len = 0u;

            croft_xpi_list_cursor_init(&cursor, bundle->requires_bundles);
            while (croft_xpi_list_cursor_next(&cursor, &item, &len)) {
                int rc = croft_orch_string_list_add_len_unique(plan->required_bundles,
                                                               &plan->required_bundle_count,
                                                               CROFT_ORCH_MAX_BUNDLES,
                                                               item,
                                                               len);
                if (rc != ERR_OK) {
                    snprintf(err_out, err_cap, "bundle-requirements-overflow");
                    return rc;
                }
            }
        }
        if (croft_orch_plan_copy_bundle_fields(plan, bundle) != ERR_OK) {
            snprintf(err_out, err_cap, "bundle-field-overflow");
            return ERR_RANGE;
        }
        i++;
    }

    if (manifest->payload_module_count > 1u) {
        snprintf(err_out, err_cap, "multiple-payload-modules-unsupported");
        return ERR_RANGE;
    }
    if (manifest->worker_count > 0u && manifest->payload_module_count == 0u) {
        snprintf(err_out, err_cap, "missing-payload-module");
        return ERR_INVALID;
    }

    for (i = 0u; i < manifest->worker_count; i++) {
        const croft_orch_worker_spec *worker = &manifest->workers[i];
        uint32_t j;

        if (worker->name[0] == '\0' || worker->module[0] == '\0' || worker->replicas == 0u) {
            snprintf(err_out, err_cap, "invalid-worker");
            return ERR_INVALID;
        }
        if (!croft_orch_manifest_find_module((croft_orch_manifest_spec *)manifest, worker->module)) {
            snprintf(err_out, err_cap, "unknown-worker-module");
            return ERR_NOT_FOUND;
        }
        for (j = 0u; j < worker->allowed_table_count; j++) {
            if (!croft_orch_manifest_find_table(manifest, worker->allowed_tables[j])) {
                snprintf(err_out, err_cap, "unknown-worker-table");
                return ERR_NOT_FOUND;
            }
        }
        for (j = 0u; j < worker->inbox_count; j++) {
            croft_orch_mailbox_spec *mailbox = croft_orch_manifest_find_mailbox((croft_orch_manifest_spec *)manifest,
                                                                                worker->inboxes[j]);
            if (!mailbox || !croft_orch_string_list_contains(mailbox->consumers, mailbox->consumer_count, worker->name)) {
                snprintf(err_out, err_cap, "invalid-inbox-binding");
                return ERR_INVALID;
            }
        }
        for (j = 0u; j < worker->outbox_count; j++) {
            croft_orch_mailbox_spec *mailbox = croft_orch_manifest_find_mailbox((croft_orch_manifest_spec *)manifest,
                                                                                worker->outboxes[j]);
            if (!mailbox || !croft_orch_string_list_contains(mailbox->producers, mailbox->producer_count, worker->name)) {
                snprintf(err_out, err_cap, "invalid-outbox-binding");
                return ERR_INVALID;
            }
        }
        if (worker->startup_bytes_len > 0u && worker->has_startup_format && worker->startup_format[0] == '\0') {
            snprintf(err_out, err_cap, "invalid-startup-format");
            return ERR_INVALID;
        }
    }

    (void)croft_orch_plan_add_diagnostic(plan, "resolved-with-compiled-xpi-registry");
    return ERR_OK;
}

static int32_t croft_orch_session_status_value(croft_orch_session_slot *session,
                                               SapWitOrchestrationSessionStatus *status_out)
{
    if (!session || !status_out) {
        return ERR_INVALID;
    }
    memset(status_out, 0, sizeof(*status_out));
    status_out->phase = session->phase;
    status_out->worker_count = session->worker_count;
    status_out->running_count = session->running_count;
    if (session->last_error[0] != '\0') {
        status_out->has_last_error = 1u;
        status_out->last_error_data = (const uint8_t *)session->last_error;
        status_out->last_error_len = (uint32_t)strlen(session->last_error);
    }
    return ERR_OK;
}

static int croft_orch_worker_policy_has_txn(const croft_orch_worker_policy *policy,
                                            SapWitOrchestrationTxnResource handle)
{
    uint32_t i;

    if (!policy || handle == SAP_WIT_ORCHESTRATION_TXN_RESOURCE_INVALID) {
        return 0;
    }
    for (i = 0u; i < policy->txn_count; i++) {
        if (policy->txns[i] == handle) {
            return 1;
        }
    }
    return 0;
}

static void croft_orch_worker_policy_remove_txn(croft_orch_worker_policy *policy,
                                                SapWitOrchestrationTxnResource handle)
{
    uint32_t i;

    if (!policy || handle == SAP_WIT_ORCHESTRATION_TXN_RESOURCE_INVALID) {
        return;
    }
    for (i = 0u; i < policy->txn_count; i++) {
        if (policy->txns[i] == handle) {
            memmove(&policy->txns[i],
                    &policy->txns[i + 1u],
                    (policy->txn_count - i - 1u) * sizeof(policy->txns[0]));
            policy->txn_count--;
            return;
        }
    }
}

static int croft_orch_worker_policy_add_txn(croft_orch_worker_policy *policy,
                                            SapWitOrchestrationTxnResource handle)
{
    if (!policy || handle == SAP_WIT_ORCHESTRATION_TXN_RESOURCE_INVALID) {
        return ERR_INVALID;
    }
    if (policy->txn_count >= CROFT_ORCH_MAX_TXNS_PER_WORKER) {
        return ERR_RANGE;
    }
    policy->txns[policy->txn_count] = handle;
    policy->txn_count++;
    return ERR_OK;
}

static int croft_orch_key_table_allowed(const croft_orch_worker_policy *policy,
                                        const uint8_t *key_data,
                                        uint32_t key_len)
{
    uint32_t i;
    uint32_t table_len = 0u;

    if (!policy || !key_data || key_len == 0u) {
        return 0;
    }
    while (table_len < key_len && key_data[table_len] != 0u) {
        table_len++;
    }
    if (table_len == 0u || table_len >= key_len) {
        return 0;
    }
    for (i = 0u; i < policy->worker->allowed_table_count; i++) {
        if (strlen(policy->worker->allowed_tables[i]) == table_len
                && memcmp(policy->worker->allowed_tables[i], key_data, table_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int croft_orch_mailbox_handle_allowed(const SapWitOrchestrationMailboxResource *handles,
                                             uint32_t count,
                                             SapWitOrchestrationMailboxResource handle)
{
    uint32_t i;

    for (i = 0u; i < count; i++) {
        if (handles[i] == handle) {
            return 1;
        }
    }
    return 0;
}

static int32_t croft_orch_worker_store_open(void *ctx,
                                            const SapWitOrchestrationDbOpen *payload,
                                            SapWitOrchestrationStoreReply *reply_out)
{
    (void)ctx;
    (void)payload;
    sap_wit_zero_orchestration_store_reply(reply_out);
    reply_out->case_tag = SAP_WIT_ORCHESTRATION_STORE_REPLY_DB;
    reply_out->val.db.is_v_ok = 0u;
    croft_orch_set_reply_error("db-open-disabled",
                               &reply_out->val.db.v_val.err.v_data,
                               &reply_out->val.db.v_val.err.v_len);
    return ERR_OK;
}

static int32_t croft_orch_worker_store_drop(void *ctx,
                                            const SapWitOrchestrationDbDrop *payload,
                                            SapWitOrchestrationStoreReply *reply_out)
{
    (void)ctx;
    (void)payload;
    sap_wit_zero_orchestration_store_reply(reply_out);
    reply_out->case_tag = SAP_WIT_ORCHESTRATION_STORE_REPLY_STATUS;
    reply_out->val.status.is_v_ok = 0u;
    croft_orch_set_reply_error("db-drop-disabled",
                               &reply_out->val.status.v_val.err.v_data,
                               &reply_out->val.status.v_val.err.v_len);
    return ERR_OK;
}

static int32_t croft_orch_worker_store_begin(void *ctx,
                                             const SapWitOrchestrationDbBegin *payload,
                                             SapWitOrchestrationStoreReply *reply_out)
{
    croft_orch_worker_policy *policy = (croft_orch_worker_policy *)ctx;
    int32_t rc;

    if (!policy || !payload || !reply_out) {
        return ERR_INVALID;
    }
    if (payload->db != policy->db_handle) {
        sap_wit_zero_orchestration_store_reply(reply_out);
        reply_out->case_tag = SAP_WIT_ORCHESTRATION_STORE_REPLY_TXN;
        reply_out->val.txn.is_v_ok = 0u;
        croft_orch_set_reply_error("db-handle-denied",
                                   &reply_out->val.txn.v_val.err.v_data,
                                   &reply_out->val.txn.v_val.err.v_len);
        return ERR_OK;
    }

    host_mutex_lock(&policy->session->store_mutex);
    rc = croft_wit_store_runtime_dispatch(policy->session->store_runtime,
                                          (const SapWitOrchestrationStoreCommand *)((const void *)&(SapWitOrchestrationStoreCommand){
                                              .case_tag = SAP_WIT_ORCHESTRATION_STORE_COMMAND_BEGIN,
                                              .val.begin = *payload,
                                          }),
                                          reply_out);
    host_mutex_unlock(&policy->session->store_mutex);
    if (rc == ERR_OK
            && reply_out->case_tag == SAP_WIT_ORCHESTRATION_STORE_REPLY_TXN
            && reply_out->val.txn.is_v_ok) {
        if (croft_orch_worker_policy_add_txn(policy, reply_out->val.txn.v_val.ok.v) != ERR_OK) {
            reply_out->val.txn.is_v_ok = 0u;
            croft_orch_set_reply_error("txn-capacity-exceeded",
                                       &reply_out->val.txn.v_val.err.v_data,
                                       &reply_out->val.txn.v_val.err.v_len);
        }
    }
    return rc;
}

static int32_t croft_orch_worker_store_commit(void *ctx,
                                              const SapWitOrchestrationTxnCommit *payload,
                                              SapWitOrchestrationStoreReply *reply_out)
{
    croft_orch_worker_policy *policy = (croft_orch_worker_policy *)ctx;
    int32_t rc;

    if (!policy || !payload || !reply_out) {
        return ERR_INVALID;
    }
    if (!croft_orch_worker_policy_has_txn(policy, payload->txn)) {
        sap_wit_zero_orchestration_store_reply(reply_out);
        reply_out->case_tag = SAP_WIT_ORCHESTRATION_STORE_REPLY_STATUS;
        reply_out->val.status.is_v_ok = 0u;
        croft_orch_set_reply_error("txn-denied",
                                   &reply_out->val.status.v_val.err.v_data,
                                   &reply_out->val.status.v_val.err.v_len);
        return ERR_OK;
    }
    host_mutex_lock(&policy->session->store_mutex);
    rc = croft_wit_store_runtime_dispatch(policy->session->store_runtime,
                                          (const SapWitOrchestrationStoreCommand *)((const void *)&(SapWitOrchestrationStoreCommand){
                                              .case_tag = SAP_WIT_ORCHESTRATION_STORE_COMMAND_COMMIT,
                                              .val.commit = *payload,
                                          }),
                                          reply_out);
    host_mutex_unlock(&policy->session->store_mutex);
    if (rc == ERR_OK
            && reply_out->case_tag == SAP_WIT_ORCHESTRATION_STORE_REPLY_STATUS
            && reply_out->val.status.is_v_ok) {
        croft_orch_worker_policy_remove_txn(policy, payload->txn);
    }
    return rc;
}

static int32_t croft_orch_worker_store_abort(void *ctx,
                                             const SapWitOrchestrationTxnAbort *payload,
                                             SapWitOrchestrationStoreReply *reply_out)
{
    croft_orch_worker_policy *policy = (croft_orch_worker_policy *)ctx;
    int32_t rc;

    if (!policy || !payload || !reply_out) {
        return ERR_INVALID;
    }
    if (!croft_orch_worker_policy_has_txn(policy, payload->txn)) {
        sap_wit_zero_orchestration_store_reply(reply_out);
        reply_out->case_tag = SAP_WIT_ORCHESTRATION_STORE_REPLY_STATUS;
        reply_out->val.status.is_v_ok = 0u;
        croft_orch_set_reply_error("txn-denied",
                                   &reply_out->val.status.v_val.err.v_data,
                                   &reply_out->val.status.v_val.err.v_len);
        return ERR_OK;
    }
    host_mutex_lock(&policy->session->store_mutex);
    rc = croft_wit_store_runtime_dispatch(policy->session->store_runtime,
                                          (const SapWitOrchestrationStoreCommand *)((const void *)&(SapWitOrchestrationStoreCommand){
                                              .case_tag = SAP_WIT_ORCHESTRATION_STORE_COMMAND_ABORT,
                                              .val.abort = *payload,
                                          }),
                                          reply_out);
    host_mutex_unlock(&policy->session->store_mutex);
    if (rc == ERR_OK
            && reply_out->case_tag == SAP_WIT_ORCHESTRATION_STORE_REPLY_STATUS
            && reply_out->val.status.is_v_ok) {
        croft_orch_worker_policy_remove_txn(policy, payload->txn);
    }
    return rc;
}

static int32_t croft_orch_worker_store_put(void *ctx,
                                           const SapWitOrchestrationTxnPut *payload,
                                           SapWitOrchestrationStoreReply *reply_out)
{
    croft_orch_worker_policy *policy = (croft_orch_worker_policy *)ctx;

    if (!policy || !payload || !reply_out) {
        return ERR_INVALID;
    }
    if (!croft_orch_worker_policy_has_txn(policy, payload->txn)
            || !croft_orch_key_table_allowed(policy, payload->key_data, payload->key_len)) {
        sap_wit_zero_orchestration_store_reply(reply_out);
        reply_out->case_tag = SAP_WIT_ORCHESTRATION_STORE_REPLY_STATUS;
        reply_out->val.status.is_v_ok = 0u;
        croft_orch_set_reply_error("put-denied",
                                   &reply_out->val.status.v_val.err.v_data,
                                   &reply_out->val.status.v_val.err.v_len);
        return ERR_OK;
    }
    host_mutex_lock(&policy->session->store_mutex);
    {
        int32_t rc = croft_wit_store_runtime_dispatch(policy->session->store_runtime,
                                                      (const SapWitOrchestrationStoreCommand *)((const void *)&(SapWitOrchestrationStoreCommand){
                                                          .case_tag = SAP_WIT_ORCHESTRATION_STORE_COMMAND_PUT,
                                                          .val.put = *payload,
                                                      }),
                                                      reply_out);
        host_mutex_unlock(&policy->session->store_mutex);
        return rc;
    }
}

static int32_t croft_orch_worker_store_get(void *ctx,
                                           const SapWitOrchestrationTxnGet *payload,
                                           SapWitOrchestrationStoreReply *reply_out)
{
    croft_orch_worker_policy *policy = (croft_orch_worker_policy *)ctx;

    if (!policy || !payload || !reply_out) {
        return ERR_INVALID;
    }
    if (!croft_orch_worker_policy_has_txn(policy, payload->txn)
            || !croft_orch_key_table_allowed(policy, payload->key_data, payload->key_len)) {
        sap_wit_zero_orchestration_store_reply(reply_out);
        reply_out->case_tag = SAP_WIT_ORCHESTRATION_STORE_REPLY_GET;
        reply_out->val.get.is_v_ok = 0u;
        croft_orch_set_reply_error("get-denied",
                                   &reply_out->val.get.v_val.err.v_data,
                                   &reply_out->val.get.v_val.err.v_len);
        return ERR_OK;
    }
    host_mutex_lock(&policy->session->store_mutex);
    {
        int32_t rc = croft_wit_store_runtime_dispatch(policy->session->store_runtime,
                                                      (const SapWitOrchestrationStoreCommand *)((const void *)&(SapWitOrchestrationStoreCommand){
                                                          .case_tag = SAP_WIT_ORCHESTRATION_STORE_COMMAND_GET,
                                                          .val.get = *payload,
                                                      }),
                                                      reply_out);
        host_mutex_unlock(&policy->session->store_mutex);
        return rc;
    }
}

static const SapWitOrchestrationStoreDispatchOps g_croft_orch_worker_store_ops = {
    .open = croft_orch_worker_store_open,
    .drop = croft_orch_worker_store_drop,
    .begin = croft_orch_worker_store_begin,
    .commit = croft_orch_worker_store_commit,
    .abort = croft_orch_worker_store_abort,
    .put = croft_orch_worker_store_put,
    .get = croft_orch_worker_store_get,
};

static int32_t croft_orch_worker_mailbox_open(void *ctx,
                                              const SapWitOrchestrationMailboxOpen *payload,
                                              SapWitOrchestrationMailboxReply *reply_out)
{
    (void)ctx;
    (void)payload;
    sap_wit_zero_orchestration_mailbox_reply(reply_out);
    reply_out->case_tag = SAP_WIT_ORCHESTRATION_MAILBOX_REPLY_MAILBOX;
    reply_out->val.mailbox.is_v_ok = 0u;
    croft_orch_set_reply_error("mailbox-open-disabled",
                               &reply_out->val.mailbox.v_val.err.v_data,
                               &reply_out->val.mailbox.v_val.err.v_len);
    return ERR_OK;
}

static int32_t croft_orch_worker_mailbox_send(void *ctx,
                                              const SapWitOrchestrationMailboxSend *payload,
                                              SapWitOrchestrationMailboxReply *reply_out)
{
    croft_orch_worker_policy *policy = (croft_orch_worker_policy *)ctx;
    int32_t rc;
    SapWitOrchestrationMailboxCommand command = {0};

    if (!policy || !payload || !reply_out) {
        return ERR_INVALID;
    }
    if (!croft_orch_mailbox_handle_allowed(policy->outbox_handles, policy->outbox_count, payload->mailbox)) {
        sap_wit_zero_orchestration_mailbox_reply(reply_out);
        reply_out->case_tag = SAP_WIT_ORCHESTRATION_MAILBOX_REPLY_STATUS;
        reply_out->val.status.is_v_ok = 0u;
        croft_orch_set_reply_error("send-denied",
                                   &reply_out->val.status.v_val.err.v_data,
                                   &reply_out->val.status.v_val.err.v_len);
        return ERR_OK;
    }

    command.case_tag = SAP_WIT_ORCHESTRATION_MAILBOX_COMMAND_SEND;
    command.val.send = *payload;
    host_mutex_lock(&policy->session->mailbox_mutex);
    rc = croft_wit_mailbox_runtime_dispatch(policy->session->mailbox_runtime,
                                            &command,
                                            reply_out);
    if (rc == ERR_OK
            && reply_out->case_tag == SAP_WIT_ORCHESTRATION_MAILBOX_REPLY_STATUS
            && reply_out->val.status.is_v_ok) {
        host_cond_broadcast(&policy->session->mailbox_cond);
    }
    host_mutex_unlock(&policy->session->mailbox_mutex);
    return rc;
}

static int32_t croft_orch_worker_mailbox_recv(void *ctx,
                                              const SapWitOrchestrationMailboxRecv *payload,
                                              SapWitOrchestrationMailboxReply *reply_out)
{
    croft_orch_worker_policy *policy = (croft_orch_worker_policy *)ctx;
    SapWitOrchestrationMailboxCommand command = {0};
    int32_t rc;

    if (!policy || !payload || !reply_out) {
        return ERR_INVALID;
    }
    if (!croft_orch_mailbox_handle_allowed(policy->inbox_handles, policy->inbox_count, payload->mailbox)) {
        sap_wit_zero_orchestration_mailbox_reply(reply_out);
        reply_out->case_tag = SAP_WIT_ORCHESTRATION_MAILBOX_REPLY_RECV;
        reply_out->val.recv.is_v_ok = 0u;
        croft_orch_set_reply_error("recv-denied",
                                   &reply_out->val.recv.v_val.err.v_data,
                                   &reply_out->val.recv.v_val.err.v_len);
        return ERR_OK;
    }

    command.case_tag = SAP_WIT_ORCHESTRATION_MAILBOX_COMMAND_RECV;
    command.val.recv = *payload;
    host_mutex_lock(&policy->session->mailbox_mutex);
    for (;;) {
        rc = croft_wit_mailbox_runtime_dispatch(policy->session->mailbox_runtime,
                                                &command,
                                                reply_out);
        if (rc != ERR_OK
                || reply_out->case_tag != SAP_WIT_ORCHESTRATION_MAILBOX_REPLY_RECV
                || !reply_out->val.recv.is_v_ok
                || reply_out->val.recv.v_val.ok.has_v) {
            break;
        }

        host_mutex_lock(&policy->session->state_mutex);
        if (policy->session->phase == SAP_WIT_ORCHESTRATION_SESSION_PHASE_FAILED
                || policy->session->phase == SAP_WIT_ORCHESTRATION_SESSION_PHASE_STOPPING
                || policy->session->phase == SAP_WIT_ORCHESTRATION_SESSION_PHASE_STOPPED) {
            host_mutex_unlock(&policy->session->state_mutex);
            break;
        }
        host_mutex_unlock(&policy->session->state_mutex);
        host_cond_wait(&policy->session->mailbox_cond, &policy->session->mailbox_mutex);
    }
    host_mutex_unlock(&policy->session->mailbox_mutex);
    return rc;
}

static int32_t croft_orch_worker_mailbox_drop(void *ctx,
                                              const SapWitOrchestrationMailboxDrop *payload,
                                              SapWitOrchestrationMailboxReply *reply_out)
{
    (void)ctx;
    (void)payload;
    sap_wit_zero_orchestration_mailbox_reply(reply_out);
    reply_out->case_tag = SAP_WIT_ORCHESTRATION_MAILBOX_REPLY_STATUS;
    reply_out->val.status.is_v_ok = 0u;
    croft_orch_set_reply_error("mailbox-drop-disabled",
                               &reply_out->val.status.v_val.err.v_data,
                               &reply_out->val.status.v_val.err.v_len);
    return ERR_OK;
}

static const SapWitOrchestrationMailboxDispatchOps g_croft_orch_worker_mailbox_ops = {
    .open = croft_orch_worker_mailbox_open,
    .send = croft_orch_worker_mailbox_send,
    .recv = croft_orch_worker_mailbox_recv,
    .drop = croft_orch_worker_mailbox_drop,
};

static uint8_t *croft_orch_read_file(const char *path, uint32_t *len_out)
{
    FILE *f;
    uint8_t *bytes;
    long size;
    size_t nr;

    if (len_out) {
        *len_out = 0u;
    }
    if (!path) {
        return NULL;
    }
    f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    bytes = (uint8_t *)malloc((size_t)size);
    if (!bytes) {
        fclose(f);
        return NULL;
    }
    nr = fread(bytes, 1u, (size_t)size, f);
    fclose(f);
    if ((long)nr != size) {
        free(bytes);
        return NULL;
    }
    if (len_out) {
        *len_out = (uint32_t)size;
    }
    return bytes;
}

static croft_orch_named_mailbox *croft_orch_session_find_named_mailbox(croft_orch_session_slot *session,
                                                                       const char *name)
{
    uint32_t i;

    if (!session || !name) {
        return NULL;
    }
    for (i = 0u; i < session->mailbox_count; i++) {
        if (strcmp(session->mailboxes[i].name, name) == 0) {
            return &session->mailboxes[i];
        }
    }
    return NULL;
}

static void croft_orch_session_set_error(croft_orch_session_slot *session, const char *message)
{
    if (!session) {
        return;
    }
    host_mutex_lock(&session->state_mutex);
    if (message && message[0] != '\0' && session->last_error[0] == '\0') {
        strncpy(session->last_error, message, sizeof(session->last_error) - 1u);
        session->last_error[sizeof(session->last_error) - 1u] = '\0';
        session->phase = SAP_WIT_ORCHESTRATION_SESSION_PHASE_FAILED;
    }
    host_mutex_unlock(&session->state_mutex);
    host_cond_broadcast(&session->mailbox_cond);
}

static void *croft_orch_worker_thread_main(void *arg)
{
    croft_orch_worker_instance *instance = (croft_orch_worker_instance *)arg;
    host_wasm_ctx_t *ctx = NULL;
    SapWitOrchestrationWorkerWorldImports imports = {0};
    SapWitOrchestrationRunWorkerCommand command = {0};
    SapWitOrchestrationRunWorkerReply reply;
    char error_text[CROFT_ORCH_ERROR_CAP] = {0};
    int32_t rc = ERR_INVALID;

    if (!instance || !instance->session) {
        return NULL;
    }

    ctx = host_wasm_create(instance->session->payload_wasm_bytes,
                           instance->session->payload_wasm_len,
                           instance->session->runtime->config.wasm_stack_size);
    if (!ctx) {
        croft_orch_session_set_error(instance->session, "worker-wasm-create-failed");
        instance->rc = ERR_INVALID;
        return NULL;
    }

    imports.store_ctx = &instance->policy;
    imports.store_ops = &g_croft_orch_worker_store_ops;
    imports.mailbox_ctx = &instance->policy;
    imports.mailbox_ops = &g_croft_orch_worker_mailbox_ops;
    if (host_wasm_register_wit_world_endpoints(ctx,
                                               sap_wit_orchestration_worker_import_endpoints,
                                               sap_wit_orchestration_worker_import_endpoints_count,
                                               &imports) != ERR_OK) {
        croft_orch_session_set_error(instance->session, "worker-import-bind-failed");
        instance->rc = ERR_INVALID;
        host_wasm_destroy(ctx);
        return NULL;
    }

    sap_wit_zero_orchestration_run_worker_reply(&reply);
    command.case_tag = SAP_WIT_ORCHESTRATION_RUN_WORKER_COMMAND_RUN_WORKER;
    command.val.run_worker.worker_name_data = croft_orch_string_data(instance->worker->name);
    command.val.run_worker.worker_name_len = croft_orch_string_len(instance->worker->name);
    command.val.run_worker.replica_index = instance->replica_index;
    command.val.run_worker.has_startup_format = 1u;
    command.val.run_worker.startup_format_data = (const uint8_t *)CROFT_ORCH_STARTUP_CONTRACT;
    command.val.run_worker.startup_format_len = (uint32_t)strlen(CROFT_ORCH_STARTUP_CONTRACT);
    command.val.run_worker.startup_bytes_data = instance->startup.bytes.data;
    command.val.run_worker.startup_bytes_len = instance->startup.bytes.len;
    rc = host_wasm_call_wit_export_endpoint(ctx,
                                            &sap_wit_orchestration_worker_export_endpoints[0],
                                            &command,
                                            &reply);
    if (rc == ERR_OK) {
        if (reply.case_tag != SAP_WIT_ORCHESTRATION_RUN_WORKER_REPLY_STATUS) {
            rc = ERR_TYPE;
        } else if (!reply.val.status.is_v_ok) {
            if (reply.val.status.v_val.err.v_data && reply.val.status.v_val.err.v_len > 0u) {
                snprintf(error_text,
                         sizeof(error_text),
                         "worker-run-failed:%s:%.*s",
                         instance->worker ? instance->worker->name : "unknown",
                         (int)reply.val.status.v_val.err.v_len,
                         (const char *)reply.val.status.v_val.err.v_data);
            }
            rc = ERR_INVALID;
        }
    }
    if (rc != ERR_OK) {
        if (error_text[0] == '\0') {
            snprintf(error_text,
                     sizeof(error_text),
                     "worker-run-failed:%s",
                     instance->worker ? instance->worker->name : "unknown");
        }
        croft_orch_session_set_error(instance->session, error_text);
    }
    sap_wit_dispose_orchestration_run_worker_reply(&reply);
    host_wasm_destroy(ctx);

    host_mutex_lock(&instance->session->state_mutex);
    if (instance->session->running_count > 0u) {
        instance->session->running_count--;
    }
    if (instance->session->running_count == 0u
            && instance->session->phase == SAP_WIT_ORCHESTRATION_SESSION_PHASE_RUNNING) {
        instance->session->phase = SAP_WIT_ORCHESTRATION_SESSION_PHASE_STOPPED;
    }
    host_mutex_unlock(&instance->session->state_mutex);

    instance->rc = rc;
    return NULL;
}

static int croft_orch_launch_session(croft_orchestration_runtime *runtime,
                                     const croft_orch_manifest_spec *manifest,
                                     const croft_orch_plan_spec *plan,
                                     SapWitOrchestrationSessionResource *handle_out,
                                     char *err_out,
                                     size_t err_cap)
{
    croft_wit_store_runtime_config store_config;
    croft_orch_session_slot *session;
    SapWitOrchestrationSessionResource handle = SAP_WIT_ORCHESTRATION_SESSION_RESOURCE_INVALID;
    SapWitOrchestrationStoreCommand open_command = {0};
    SapWitOrchestrationStoreReply open_reply;
    uint32_t i;
    uint32_t windex = 0u;
    int32_t rc;

    if (err_out && err_cap > 0u) {
        err_out[0] = '\0';
    }
    if (!runtime || !manifest || !plan || !handle_out) {
        return ERR_INVALID;
    }

    rc = croft_orch_allocate_session(runtime, &handle);
    if (rc != ERR_OK) {
        return rc;
    }
    session = croft_orch_session_lookup(runtime, handle);
    rc = croft_orch_manifest_spec_clone(&session->manifest, manifest);
    if (rc != ERR_OK) {
        snprintf(err_out, err_cap, "manifest-clone-failed");
        croft_orch_session_set_error(session, err_out);
        return rc;
    }
    memcpy(&session->plan, plan, sizeof(session->plan));
    session->phase = SAP_WIT_ORCHESTRATION_SESSION_PHASE_LAUNCHING;

    if (manifest->payload_module_count > 0u) {
        uint32_t wasm_len = 0u;
        session->payload_wasm_bytes = croft_orch_read_file(manifest->payload_modules[0].path, &wasm_len);
        if (!session->payload_wasm_bytes) {
            snprintf(err_out, err_cap, "payload-module-read-failed");
            croft_orch_session_set_error(session, err_out);
            return ERR_NOT_FOUND;
        }
        session->payload_wasm_len = wasm_len;
        strncpy(session->payload_module_name,
                manifest->payload_modules[0].name,
                sizeof(session->payload_module_name) - 1u);
    }

    croft_wit_store_runtime_config_default(&store_config);
    store_config.default_page_size = runtime->config.default_page_size;
    session->store_runtime = croft_wit_store_runtime_create(&store_config);
    session->mailbox_runtime = croft_wit_mailbox_runtime_create();
    if (!session->store_runtime || !session->mailbox_runtime) {
        snprintf(err_out, err_cap, "session-runtime-create-failed");
        croft_orch_session_set_error(session, err_out);
        return ERR_OOM;
    }

    sap_wit_zero_orchestration_store_reply(&open_reply);
    open_command.case_tag = SAP_WIT_ORCHESTRATION_STORE_COMMAND_OPEN;
    open_command.val.open.page_size = runtime->config.default_page_size;
    rc = croft_wit_store_runtime_dispatch(session->store_runtime,
                                          &open_command,
                                          &open_reply);
    if (rc != ERR_OK
            || open_reply.case_tag != SAP_WIT_ORCHESTRATION_STORE_REPLY_DB
            || !open_reply.val.db.is_v_ok) {
        snprintf(err_out, err_cap, "db-open-failed");
        sap_wit_dispose_orchestration_store_reply(&open_reply);
        croft_orch_session_set_error(session, err_out);
        return ERR_INVALID;
    }
    session->db_handle = open_reply.val.db.v_val.ok.v;
    sap_wit_dispose_orchestration_store_reply(&open_reply);

    for (i = 0u; i < manifest->mailbox_count; i++) {
        SapWitOrchestrationMailboxCommand command = {0};
        SapWitOrchestrationMailboxReply reply;

        sap_wit_zero_orchestration_mailbox_reply(&reply);
        command.case_tag = SAP_WIT_ORCHESTRATION_MAILBOX_COMMAND_OPEN;
        command.val.open.max_messages = CROFT_ORCH_DEFAULT_MAILBOX_DEPTH;
        host_mutex_lock(&session->mailbox_mutex);
        rc = croft_wit_mailbox_runtime_dispatch(session->mailbox_runtime,
                                                &command,
                                                &reply);
        host_mutex_unlock(&session->mailbox_mutex);
        if (rc != ERR_OK
                || reply.case_tag != SAP_WIT_ORCHESTRATION_MAILBOX_REPLY_MAILBOX
                || !reply.val.mailbox.is_v_ok) {
            snprintf(err_out, err_cap, "mailbox-open-failed");
            sap_wit_dispose_orchestration_mailbox_reply(&reply);
            croft_orch_session_set_error(session, err_out);
            return ERR_INVALID;
        }
        strncpy(session->mailboxes[session->mailbox_count].name,
                manifest->mailboxes[i].name,
                sizeof(session->mailboxes[session->mailbox_count].name) - 1u);
        session->mailboxes[session->mailbox_count].handle = reply.val.mailbox.v_val.ok.v;
        strncpy(session->mailboxes[session->mailbox_count].message_format,
                manifest->mailboxes[i].message_format,
                sizeof(session->mailboxes[session->mailbox_count].message_format) - 1u);
        session->mailboxes[session->mailbox_count].durability = manifest->mailboxes[i].durability;
        session->mailbox_count++;
        sap_wit_dispose_orchestration_mailbox_reply(&reply);
    }

    for (i = 0u; i < plan->worker_count; i++) {
        uint32_t replica;
        croft_orch_worker_spec *manifest_worker = croft_orch_manifest_find_worker(&session->manifest,
                                                                                  plan->workers[i].name);
        if (!manifest_worker) {
            snprintf(err_out, err_cap, "worker-missing");
            croft_orch_session_set_error(session, err_out);
            return ERR_NOT_FOUND;
        }
        for (replica = 0u; replica < plan->workers[i].replicas; replica++) {
            croft_orch_worker_instance *instance;
            croft_orch_table_spec startup_tables[CROFT_ORCH_MAX_TABLES];
            croft_orch_named_mailbox startup_inboxes[CROFT_ORCH_MAX_MAILBOXES];
            croft_orch_named_mailbox startup_outboxes[CROFT_ORCH_MAX_MAILBOXES];
            uint32_t startup_table_count = 0u;
            uint32_t startup_inbox_count = 0u;
            uint32_t startup_outbox_count = 0u;
            uint32_t t;

            if (windex >= CROFT_ORCH_MAX_TOTAL_WORKERS) {
                snprintf(err_out, err_cap, "too-many-worker-replicas");
                croft_orch_session_set_error(session, err_out);
                return ERR_RANGE;
            }
            instance = &session->workers[windex];
            instance->session = session;
            instance->worker = &session->plan.workers[i];
            instance->replica_index = replica;
            instance->policy.runtime = runtime;
            instance->policy.session = session;
            instance->policy.worker = &session->plan.workers[i];
            instance->policy.db_handle = session->db_handle;
            for (t = 0u; t < plan->workers[i].allowed_table_count; t++) {
                const croft_orch_table_spec *table = croft_orch_manifest_find_table(&session->manifest,
                                                                                   plan->workers[i].allowed_tables[t]);
                if (!table) {
                    snprintf(err_out, err_cap, "worker-table-missing");
                    croft_orch_session_set_error(session, err_out);
                    return ERR_NOT_FOUND;
                }
                startup_tables[startup_table_count++] = *table;
            }
            for (t = 0u; t < plan->workers[i].inbox_count; t++) {
                croft_orch_named_mailbox *mailbox = croft_orch_session_find_named_mailbox(session,
                                                                                           plan->workers[i].inboxes[t]);
                if (!mailbox) {
                    snprintf(err_out, err_cap, "worker-inbox-missing");
                    croft_orch_session_set_error(session, err_out);
                    return ERR_NOT_FOUND;
                }
                instance->policy.inbox_handles[instance->policy.inbox_count++] = mailbox->handle;
                startup_inboxes[startup_inbox_count++] = *mailbox;
            }
            for (t = 0u; t < plan->workers[i].outbox_count; t++) {
                croft_orch_named_mailbox *mailbox = croft_orch_session_find_named_mailbox(session,
                                                                                           plan->workers[i].outboxes[t]);
                if (!mailbox) {
                    snprintf(err_out, err_cap, "worker-outbox-missing");
                    croft_orch_session_set_error(session, err_out);
                    return ERR_NOT_FOUND;
                }
                instance->policy.outbox_handles[instance->policy.outbox_count++] = mailbox->handle;
                startup_outboxes[startup_outbox_count++] = *mailbox;
            }

            rc = croft_orch_render_worker_startup(&instance->startup,
                                                  session->db_handle,
                                                  startup_tables,
                                                  startup_table_count,
                                                  startup_inboxes,
                                                  startup_inbox_count,
                                                  startup_outboxes,
                                                  startup_outbox_count,
                                                  manifest_worker);
            if (rc != ERR_OK) {
                snprintf(err_out, err_cap, "worker-startup-render-failed");
                croft_orch_session_set_error(session, err_out);
                return rc;
            }

            if (host_thread_create(&instance->thread, croft_orch_worker_thread_main, instance) != 0) {
                snprintf(err_out, err_cap, "worker-thread-create-failed");
                croft_orch_session_set_error(session, err_out);
                return ERR_INVALID;
            }
            instance->thread_started = 1u;
            session->worker_count++;
            session->running_count++;
            windex++;
        }
    }

    session->phase = session->worker_count > 0u
                         ? SAP_WIT_ORCHESTRATION_SESSION_PHASE_RUNNING
                         : SAP_WIT_ORCHESTRATION_SESSION_PHASE_STOPPED;
    runtime->last_session = handle;
    *handle_out = handle;
    return ERR_OK;
}

void croft_orchestration_runtime_config_default(croft_orchestration_runtime_config *config)
{
    if (!config) {
        return;
    }
    config->wasm_stack_size = 96u * 1024u;
    config->default_page_size = 4096u;
}

croft_orchestration_runtime *croft_orchestration_runtime_create(
    const croft_orchestration_runtime_config *config)
{
    croft_orchestration_runtime *runtime;

    runtime = (croft_orchestration_runtime *)calloc(1u, sizeof(*runtime));
    if (!runtime) {
        return NULL;
    }
    croft_orchestration_runtime_config_default(&runtime->config);
    if (config) {
        runtime->config = *config;
        if (runtime->config.wasm_stack_size == 0u) {
            runtime->config.wasm_stack_size = 96u * 1024u;
        }
        if (runtime->config.default_page_size == 0u) {
            runtime->config.default_page_size = 4096u;
        }
    }
    runtime->registry = croft_xpi_registry_current();
    return runtime;
}

void croft_orchestration_runtime_destroy(croft_orchestration_runtime *runtime)
{
    uint32_t i;

    if (!runtime) {
        return;
    }
    for (i = 0u; i < CROFT_ORCH_MAX_BUILDERS; i++) {
        croft_orch_builder_slot_clear(&runtime->builders[i]);
    }
    for (i = 0u; i < CROFT_ORCH_MAX_SESSIONS; i++) {
        if (runtime->sessions[i].live) {
            (void)croft_orchestration_runtime_stop_session(runtime, i + 1u);
            croft_orchestration_runtime_join_session(runtime, i + 1u);
            croft_orch_session_slot_clear(&runtime->sessions[i]);
        }
    }
    free(runtime);
}

void croft_orchestration_runtime_bind_bootstrap_imports(
    croft_orchestration_runtime *runtime,
    SapWitOrchestrationBootstrapWorldImports *imports_out)
{
    if (!runtime || !imports_out) {
        return;
    }
    memset(imports_out, 0, sizeof(*imports_out));
    imports_out->control_ctx = runtime;
    imports_out->control_ops = &g_croft_orch_control_ops;
}

const SapWitOrchestrationControlDispatchOps *croft_orchestration_runtime_control_ops(void)
{
    return &g_croft_orch_control_ops;
}

SapWitOrchestrationSessionResource croft_orchestration_runtime_last_session(
    const croft_orchestration_runtime *runtime)
{
    return runtime ? runtime->last_session : SAP_WIT_ORCHESTRATION_SESSION_RESOURCE_INVALID;
}

static int32_t croft_orchestration_runtime_stop_session(
    croft_orchestration_runtime *runtime,
    SapWitOrchestrationSessionResource session_handle)
{
    croft_orch_session_slot *session = croft_orch_session_lookup(runtime, session_handle);

    if (!session) {
        return ERR_INVALID;
    }
    host_mutex_lock(&session->state_mutex);
    if (session->phase != SAP_WIT_ORCHESTRATION_SESSION_PHASE_FAILED
            && session->phase != SAP_WIT_ORCHESTRATION_SESSION_PHASE_STOPPED) {
        session->phase = SAP_WIT_ORCHESTRATION_SESSION_PHASE_STOPPING;
    }
    host_mutex_unlock(&session->state_mutex);
    host_cond_broadcast(&session->mailbox_cond);
    return ERR_OK;
}

int32_t croft_orchestration_runtime_join_session(
    croft_orchestration_runtime *runtime,
    SapWitOrchestrationSessionResource session_handle)
{
    croft_orch_session_slot *session = croft_orch_session_lookup(runtime, session_handle);
    uint32_t i;

    if (!session) {
        return ERR_INVALID;
    }
    for (i = 0u; i < session->worker_count; i++) {
        if (session->workers[i].thread_started && !session->workers[i].joined) {
            if (host_thread_join(session->workers[i].thread, NULL) != 0) {
                croft_orch_session_set_error(session, "worker-join-failed");
                return ERR_INVALID;
            }
            session->workers[i].joined = 1u;
        }
    }
    host_mutex_lock(&session->state_mutex);
    if (session->phase != SAP_WIT_ORCHESTRATION_SESSION_PHASE_FAILED) {
        session->phase = SAP_WIT_ORCHESTRATION_SESSION_PHASE_STOPPED;
    }
    session->running_count = 0u;
    host_mutex_unlock(&session->state_mutex);
    return ERR_OK;
}

static int croft_orch_read_plan_and_manifest(croft_orch_builder_slot *builder)
{
    int rc;

    rc = croft_orch_render_manifest(&builder->rendered_manifest, &builder->spec);
    if (rc != ERR_OK) {
        return rc;
    }
    if (builder->resolved_valid) {
        rc = croft_orch_render_plan(&builder->rendered_plan, &builder->resolved);
        if (rc != ERR_OK) {
            return rc;
        }
    } else {
        croft_orch_rendered_plan_clear(&builder->rendered_plan);
    }
    return ERR_OK;
}

int32_t croft_orchestration_runtime_bootstrap_wasm_path(
    croft_orchestration_runtime *runtime,
    const char *wasm_path,
    SapWitOrchestrationSessionResource *session_out)
{
    uint8_t *wasm_bytes;
    uint32_t wasm_len = 0u;
    host_wasm_ctx_t *ctx;
    SapWitOrchestrationBootstrapWorldImports imports;
    SapWitOrchestrationRunCommand command = {0};
    SapWitOrchestrationRunReply reply;
    int32_t rc = ERR_INVALID;

    if (session_out) {
        *session_out = SAP_WIT_ORCHESTRATION_SESSION_RESOURCE_INVALID;
    }
    if (!runtime || !wasm_path) {
        return ERR_INVALID;
    }

    wasm_bytes = croft_orch_read_file(wasm_path, &wasm_len);
    if (!wasm_bytes) {
        return ERR_NOT_FOUND;
    }
    ctx = host_wasm_create(wasm_bytes, wasm_len, runtime->config.wasm_stack_size);
    free(wasm_bytes);
    if (!ctx) {
        return ERR_INVALID;
    }

    croft_orchestration_runtime_bind_bootstrap_imports(runtime, &imports);
    rc = host_wasm_register_wit_world_endpoints(ctx,
                                                sap_wit_orchestration_bootstrap_import_endpoints,
                                                sap_wit_orchestration_bootstrap_import_endpoints_count,
                                                &imports);
    if (rc != ERR_OK) {
        host_wasm_destroy(ctx);
        return rc;
    }

    sap_wit_zero_orchestration_run_reply(&reply);
    command.case_tag = SAP_WIT_ORCHESTRATION_RUN_COMMAND_RUN;
    rc = host_wasm_call_wit_export_endpoint(ctx,
                                            &sap_wit_orchestration_bootstrap_export_endpoints[0],
                                            &command,
                                            &reply);
    if (rc == ERR_OK) {
        if (reply.case_tag != SAP_WIT_ORCHESTRATION_RUN_REPLY_STATUS) {
            rc = ERR_TYPE;
        } else if (!reply.val.status.is_v_ok) {
            rc = ERR_INVALID;
        }
    }
    sap_wit_dispose_orchestration_run_reply(&reply);
    host_wasm_destroy(ctx);
    if (rc == ERR_OK && session_out) {
        *session_out = runtime->last_session;
    }
    return rc;
}

const CroftXpiRegistry *croft_orchestration_runtime_registry(
    const croft_orchestration_runtime *runtime)
{
    return runtime ? runtime->registry : NULL;
}

int32_t croft_orchestration_runtime_session_status(
    croft_orchestration_runtime *runtime,
    SapWitOrchestrationSessionResource session_handle,
    SapWitOrchestrationSessionStatus *status_out)
{
    croft_orch_session_slot *session = croft_orch_session_lookup(runtime, session_handle);

    if (!session || !status_out) {
        return ERR_INVALID;
    }
    return croft_orch_session_status_value(session, status_out);
}

int32_t croft_orchestration_runtime_session_get(
    croft_orchestration_runtime *runtime,
    SapWitOrchestrationSessionResource session_handle,
    const char *table_name,
    const uint8_t *key,
    uint32_t key_len,
    uint8_t **value_out,
    uint32_t *value_len_out)
{
    croft_orch_session_slot *session = croft_orch_session_lookup(runtime, session_handle);
    SapWitOrchestrationStoreCommand command = {0};
    SapWitOrchestrationStoreReply reply;
    SapWitOrchestrationTxnResource txn_handle = SAP_WIT_ORCHESTRATION_TXN_RESOURCE_INVALID;
    uint8_t *qualified_key = NULL;
    size_t table_len;
    int32_t rc;

    if (value_out) {
        *value_out = NULL;
    }
    if (value_len_out) {
        *value_len_out = 0u;
    }
    if (!session || !table_name || (!key && key_len > 0u) || !value_out || !value_len_out) {
        return ERR_INVALID;
    }
    table_len = strlen(table_name);
    qualified_key = (uint8_t *)malloc(table_len + 1u + key_len);
    if (!qualified_key) {
        return ERR_OOM;
    }
    memcpy(qualified_key, table_name, table_len);
    qualified_key[table_len] = 0u;
    if (key_len > 0u) {
        memcpy(qualified_key + table_len + 1u, key, key_len);
    }

    sap_wit_zero_orchestration_store_reply(&reply);
    command.case_tag = SAP_WIT_ORCHESTRATION_STORE_COMMAND_BEGIN;
    command.val.begin.db = session->db_handle;
    command.val.begin.read_only = 1u;
    host_mutex_lock(&session->store_mutex);
    rc = croft_wit_store_runtime_dispatch(session->store_runtime,
                                          &command,
                                          &reply);
    host_mutex_unlock(&session->store_mutex);
    if (rc != ERR_OK
            || reply.case_tag != SAP_WIT_ORCHESTRATION_STORE_REPLY_TXN
            || !reply.val.txn.is_v_ok) {
        free(qualified_key);
        sap_wit_dispose_orchestration_store_reply(&reply);
        return ERR_INVALID;
    }
    txn_handle = reply.val.txn.v_val.ok.v;
    sap_wit_dispose_orchestration_store_reply(&reply);

    sap_wit_zero_orchestration_store_reply(&reply);
    command.case_tag = SAP_WIT_ORCHESTRATION_STORE_COMMAND_GET;
    command.val.get.txn = txn_handle;
    command.val.get.key_data = qualified_key;
    command.val.get.key_len = (uint32_t)(table_len + 1u + key_len);
    host_mutex_lock(&session->store_mutex);
    rc = croft_wit_store_runtime_dispatch(session->store_runtime,
                                          &command,
                                          &reply);
    host_mutex_unlock(&session->store_mutex);
    free(qualified_key);
    if (rc == ERR_OK
            && reply.case_tag == SAP_WIT_ORCHESTRATION_STORE_REPLY_GET
            && reply.val.get.is_v_ok
            && reply.val.get.v_val.ok.has_v) {
        *value_len_out = reply.val.get.v_val.ok.v_len;
        *value_out = (uint8_t *)malloc(*value_len_out);
        if (!*value_out) {
            sap_wit_dispose_orchestration_store_reply(&reply);
            return ERR_OOM;
        }
        memcpy(*value_out, reply.val.get.v_val.ok.v_data, *value_len_out);
    } else if (rc == ERR_OK
               && reply.case_tag == SAP_WIT_ORCHESTRATION_STORE_REPLY_GET
               && reply.val.get.is_v_ok
               && !reply.val.get.v_val.ok.has_v) {
        rc = ERR_NOT_FOUND;
    } else if (rc == ERR_OK) {
        rc = ERR_INVALID;
    }
    sap_wit_dispose_orchestration_store_reply(&reply);

    sap_wit_zero_orchestration_store_reply(&reply);
    command.case_tag = SAP_WIT_ORCHESTRATION_STORE_COMMAND_ABORT;
    command.val.abort.txn = txn_handle;
    host_mutex_lock(&session->store_mutex);
    (void)croft_wit_store_runtime_dispatch(session->store_runtime,
                                           &command,
                                           &reply);
    host_mutex_unlock(&session->store_mutex);
    sap_wit_dispose_orchestration_store_reply(&reply);
    return rc;
}

void croft_orchestration_runtime_free_bytes(uint8_t *bytes)
{
    free(bytes);
}

static int32_t croft_orch_control_create(void *ctx,
                                         const SapWitOrchestrationBuilderCreate *payload,
                                         SapWitOrchestrationControlReply *reply_out)
{
    croft_orchestration_runtime *runtime = (croft_orchestration_runtime *)ctx;
    SapWitOrchestrationBuilderResource handle = SAP_WIT_ORCHESTRATION_BUILDER_RESOURCE_INVALID;
    croft_orch_builder_slot *builder;
    int rc;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    rc = croft_orch_allocate_builder(runtime, &handle);
    if (rc != ERR_OK) {
        croft_orch_control_reply_builder_err(reply_out, "builder-capacity-exceeded");
        return ERR_OK;
    }
    builder = croft_orch_builder_lookup(runtime, handle);
    rc = croft_orch_copy_text(builder->spec.name, sizeof(builder->spec.name), payload->name_data, payload->name_len);
    if (rc == ERR_OK) {
        rc = croft_orch_copy_text(builder->spec.family, sizeof(builder->spec.family), payload->family_data, payload->family_len);
    }
    if (rc == ERR_OK) {
        rc = croft_orch_copy_text(builder->spec.applicability,
                                  sizeof(builder->spec.applicability),
                                  payload->applicability_data,
                                  payload->applicability_len);
    }
    if (rc != ERR_OK || croft_orch_read_plan_and_manifest(builder) != ERR_OK) {
        croft_orch_builder_slot_clear(builder);
        croft_orch_control_reply_builder_err(reply_out, "invalid-builder-create");
        return ERR_OK;
    }
    croft_orch_control_reply_builder_ok(reply_out, handle);
    return ERR_OK;
}

static int32_t croft_orch_control_require_bundle(void *ctx,
                                                 const SapWitOrchestrationBuilderRequireBundle *payload,
                                                 SapWitOrchestrationControlReply *reply_out)
{
    croft_orchestration_runtime *runtime = (croft_orchestration_runtime *)ctx;
    croft_orch_builder_slot *builder = croft_orch_builder_lookup(runtime, payload ? payload->builder : 0u);
    char bundle[CROFT_ORCH_TEXT_CAP];

    if (!builder || !reply_out || !payload) {
        return ERR_INVALID;
    }
    if (croft_orch_copy_text(bundle, sizeof(bundle), payload->bundle_data, payload->bundle_len) != ERR_OK
            || croft_orch_manifest_add_required_bundle(&builder->spec, bundle) != ERR_OK
            || croft_orch_read_plan_and_manifest(builder) != ERR_OK) {
        croft_orch_control_reply_status_err(reply_out, "require-bundle-failed");
        return ERR_OK;
    }
    croft_orch_control_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_orch_control_prefer_slot(void *ctx,
                                              const SapWitOrchestrationBuilderPreferSlot *payload,
                                              SapWitOrchestrationControlReply *reply_out)
{
    croft_orchestration_runtime *runtime = (croft_orchestration_runtime *)ctx;
    croft_orch_builder_slot *builder = croft_orch_builder_lookup(runtime, payload ? payload->builder : 0u);
    croft_orch_slot_preference *pref;
    char slot[CROFT_ORCH_TEXT_CAP];
    char bundle[CROFT_ORCH_TEXT_CAP];

    if (!builder || !reply_out || !payload) {
        return ERR_INVALID;
    }
    if (croft_orch_copy_text(slot, sizeof(slot), payload->slot_data, payload->slot_len) != ERR_OK
            || croft_orch_copy_text(bundle, sizeof(bundle), payload->bundle_data, payload->bundle_len) != ERR_OK) {
        croft_orch_control_reply_status_err(reply_out, "prefer-slot-invalid");
        return ERR_OK;
    }
    pref = croft_orch_manifest_find_preference(&builder->spec, slot);
    if (!pref) {
        if (builder->spec.preferred_slot_count >= CROFT_ORCH_MAX_SLOT_PREFERENCES) {
            croft_orch_control_reply_status_err(reply_out, "prefer-slot-capacity-exceeded");
            return ERR_OK;
        }
        pref = &builder->spec.preferred_slots[builder->spec.preferred_slot_count++];
        strcpy(pref->slot, slot);
    }
    if (croft_orch_string_list_add_unique(pref->bundles,
                                          &pref->bundle_count,
                                          CROFT_ORCH_MAX_PREF_BUNDLES,
                                          bundle) != ERR_OK
            || croft_orch_read_plan_and_manifest(builder) != ERR_OK) {
        croft_orch_control_reply_status_err(reply_out, "prefer-slot-failed");
        return ERR_OK;
    }
    croft_orch_control_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_orch_control_add_module(void *ctx,
                                             const SapWitOrchestrationBuilderAddModule *payload,
                                             SapWitOrchestrationControlReply *reply_out)
{
    croft_orchestration_runtime *runtime = (croft_orchestration_runtime *)ctx;
    croft_orch_builder_slot *builder = croft_orch_builder_lookup(runtime, payload ? payload->builder : 0u);
    croft_orch_payload_module *module;

    if (!builder || !payload || !reply_out) {
        return ERR_INVALID;
    }
    if (builder->spec.payload_module_count >= CROFT_ORCH_MAX_MODULES) {
        croft_orch_control_reply_status_err(reply_out, "module-capacity-exceeded");
        return ERR_OK;
    }
    module = &builder->spec.payload_modules[builder->spec.payload_module_count];
    if (croft_orch_copy_text(module->name, sizeof(module->name), payload->name_data, payload->name_len) != ERR_OK
            || croft_orch_copy_text(module->path, sizeof(module->path), payload->path_data, payload->path_len) != ERR_OK) {
        croft_orch_control_reply_status_err(reply_out, "module-invalid");
        return ERR_OK;
    }
    builder->spec.payload_module_count++;
    if (croft_orch_read_plan_and_manifest(builder) != ERR_OK) {
        croft_orch_control_reply_status_err(reply_out, "module-add-failed");
        return ERR_OK;
    }
    croft_orch_control_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_orch_control_set_db_schema(void *ctx,
                                                const SapWitOrchestrationBuilderSetDbSchema *payload,
                                                SapWitOrchestrationControlReply *reply_out)
{
    croft_orchestration_runtime *runtime = (croft_orchestration_runtime *)ctx;
    croft_orch_builder_slot *builder = croft_orch_builder_lookup(runtime, payload ? payload->builder : 0u);

    if (!builder || !payload || !reply_out) {
        return ERR_INVALID;
    }
    if (croft_orch_parse_db_schema(&payload->schema, &builder->spec.db_schema) != ERR_OK
            || croft_orch_read_plan_and_manifest(builder) != ERR_OK) {
        croft_orch_control_reply_status_err(reply_out, "db-schema-invalid");
        return ERR_OK;
    }
    croft_orch_control_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_orch_control_add_mailbox(void *ctx,
                                              const SapWitOrchestrationBuilderAddMailbox *payload,
                                              SapWitOrchestrationControlReply *reply_out)
{
    croft_orchestration_runtime *runtime = (croft_orchestration_runtime *)ctx;
    croft_orch_builder_slot *builder = croft_orch_builder_lookup(runtime, payload ? payload->builder : 0u);
    croft_orch_mailbox_spec *mailbox;
    croft_orch_mailbox_spec parsed[1];
    uint32_t count = 0u;

    if (!builder || !payload || !reply_out) {
        return ERR_INVALID;
    }
    if (builder->spec.mailbox_count >= CROFT_ORCH_MAX_MAILBOXES) {
        croft_orch_control_reply_status_err(reply_out, "mailbox-capacity-exceeded");
        return ERR_OK;
    }
    memset(parsed, 0, sizeof(parsed));
    {
        croft_orch_blob blob = {0};
        int rc = croft_orch_encode_record_list(&payload->mailbox,
                                               1u,
                                               sizeof(payload->mailbox),
                                               (croft_orch_record_encode_fn)sap_wit_write_orchestration_mailbox_spec,
                                               &blob);
        if (rc != ERR_OK) {
            croft_orch_control_reply_status_err(reply_out, "mailbox-encode-failed");
            return ERR_OK;
        }
        rc = croft_orch_parse_mailbox_list(blob.data, blob.len, 1u, parsed, &count, 1u);
        croft_orch_blob_dispose(&blob);
        if (rc != ERR_OK || count != 1u) {
            croft_orch_control_reply_status_err(reply_out, "mailbox-invalid");
            return ERR_OK;
        }
    }
    mailbox = &builder->spec.mailboxes[builder->spec.mailbox_count++];
    *mailbox = parsed[0];
    if (croft_orch_read_plan_and_manifest(builder) != ERR_OK) {
        croft_orch_control_reply_status_err(reply_out, "mailbox-add-failed");
        return ERR_OK;
    }
    croft_orch_control_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_orch_control_add_worker(void *ctx,
                                             const SapWitOrchestrationBuilderAddWorker *payload,
                                             SapWitOrchestrationControlReply *reply_out)
{
    croft_orchestration_runtime *runtime = (croft_orchestration_runtime *)ctx;
    croft_orch_builder_slot *builder = croft_orch_builder_lookup(runtime, payload ? payload->builder : 0u);
    croft_orch_worker_spec parsed[1];
    uint32_t count = 0u;

    if (!builder || !payload || !reply_out) {
        return ERR_INVALID;
    }
    if (builder->spec.worker_count >= CROFT_ORCH_MAX_WORKERS) {
        croft_orch_control_reply_status_err(reply_out, "worker-capacity-exceeded");
        return ERR_OK;
    }
    memset(parsed, 0, sizeof(parsed));
    {
        croft_orch_blob blob = {0};
        int rc = croft_orch_encode_record_list(&payload->worker,
                                               1u,
                                               sizeof(payload->worker),
                                               (croft_orch_record_encode_fn)sap_wit_write_orchestration_worker_spec,
                                               &blob);
        if (rc != ERR_OK) {
            croft_orch_control_reply_status_err(reply_out, "worker-encode-failed");
            return ERR_OK;
        }
        rc = croft_orch_parse_worker_list(blob.data, blob.len, 1u, parsed, &count, 1u);
        croft_orch_blob_dispose(&blob);
        if (rc != ERR_OK || count != 1u) {
            croft_orch_control_reply_status_err(reply_out, "worker-invalid");
            return ERR_OK;
        }
    }
    builder->spec.workers[builder->spec.worker_count++] = parsed[0];
    if (croft_orch_read_plan_and_manifest(builder) != ERR_OK) {
        croft_orch_control_reply_status_err(reply_out, "worker-add-failed");
        return ERR_OK;
    }
    croft_orch_control_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_orch_control_snapshot(void *ctx,
                                           const SapWitOrchestrationBuilderSnapshot *payload,
                                           SapWitOrchestrationControlReply *reply_out)
{
    croft_orchestration_runtime *runtime = (croft_orchestration_runtime *)ctx;
    croft_orch_builder_slot *builder = croft_orch_builder_lookup(runtime, payload ? payload->builder : 0u);

    if (!builder || !reply_out) {
        return ERR_INVALID;
    }
    if (croft_orch_read_plan_and_manifest(builder) != ERR_OK) {
        croft_orch_control_reply_status_err(reply_out, "snapshot-failed");
        return ERR_OK;
    }
    croft_orch_control_reply_manifest(reply_out, &builder->rendered_manifest.view);
    return ERR_OK;
}

static int32_t croft_orch_control_resolve(void *ctx,
                                          const SapWitOrchestrationBuilderResolve *payload,
                                          SapWitOrchestrationControlReply *reply_out)
{
    croft_orchestration_runtime *runtime = (croft_orchestration_runtime *)ctx;
    croft_orch_builder_slot *builder = croft_orch_builder_lookup(runtime, payload ? payload->builder : 0u);
    char err[CROFT_ORCH_ERROR_CAP];
    int rc;

    if (!builder || !reply_out) {
        return ERR_INVALID;
    }
    rc = croft_orch_resolve_manifest(&builder->spec, runtime->registry, &builder->resolved, err, sizeof(err));
    if (rc != ERR_OK) {
        croft_orch_control_reply_plan_err(reply_out,
                                          croft_orch_set_stable_error(builder->last_error,
                                                                      sizeof(builder->last_error),
                                                                      err[0] ? err : "resolve-failed"));
        return ERR_OK;
    }
    builder->resolved_valid = 1u;
    rc = croft_orch_read_plan_and_manifest(builder);
    if (rc != ERR_OK) {
        croft_orch_control_reply_plan_err(reply_out,
                                          croft_orch_set_stable_error(builder->last_error,
                                                                      sizeof(builder->last_error),
                                                                      "plan-render-failed"));
        return ERR_OK;
    }
    croft_orch_control_reply_plan_ok(reply_out, &builder->rendered_plan.view);
    return ERR_OK;
}

static int32_t croft_orch_control_launch(void *ctx,
                                         const SapWitOrchestrationBuilderLaunch *payload,
                                         SapWitOrchestrationControlReply *reply_out)
{
    croft_orchestration_runtime *runtime = (croft_orchestration_runtime *)ctx;
    croft_orch_builder_slot *builder = croft_orch_builder_lookup(runtime, payload ? payload->builder : 0u);
    char err[CROFT_ORCH_ERROR_CAP];
    SapWitOrchestrationSessionResource session_handle;
    int rc;

    if (!builder || !reply_out) {
        return ERR_INVALID;
    }
    if (!builder->resolved_valid) {
        rc = croft_orch_resolve_manifest(&builder->spec, runtime->registry, &builder->resolved, err, sizeof(err));
        if (rc != ERR_OK) {
            croft_orch_control_reply_session_err(reply_out,
                                                 croft_orch_set_stable_error(builder->last_error,
                                                                             sizeof(builder->last_error),
                                                                             err[0] ? err : "resolve-before-launch-failed"));
            return ERR_OK;
        }
        builder->resolved_valid = 1u;
    }
    rc = croft_orch_launch_session(runtime, &builder->spec, &builder->resolved, &session_handle, err, sizeof(err));
    if (rc != ERR_OK) {
        croft_orch_control_reply_session_err(reply_out,
                                             croft_orch_set_stable_error(builder->last_error,
                                                                         sizeof(builder->last_error),
                                                                         err[0] ? err : "launch-failed"));
        return ERR_OK;
    }
    croft_orch_control_reply_session_ok(reply_out, session_handle);
    return ERR_OK;
}

static int32_t croft_orch_control_builder_drop(void *ctx,
                                               const SapWitOrchestrationBuilderDrop *payload,
                                               SapWitOrchestrationControlReply *reply_out)
{
    croft_orchestration_runtime *runtime = (croft_orchestration_runtime *)ctx;
    croft_orch_builder_slot *builder = croft_orch_builder_lookup(runtime, payload ? payload->builder : 0u);

    if (!builder || !reply_out) {
        return ERR_INVALID;
    }
    croft_orch_builder_slot_clear(builder);
    croft_orch_control_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_orch_control_manifest(void *ctx,
                                           const SapWitOrchestrationSessionManifest *payload,
                                           SapWitOrchestrationControlReply *reply_out)
{
    croft_orchestration_runtime *runtime = (croft_orchestration_runtime *)ctx;
    croft_orch_session_slot *session = croft_orch_session_lookup(runtime, payload ? payload->session : 0u);

    if (!session || !reply_out) {
        return ERR_INVALID;
    }
    if (croft_orch_render_manifest(&session->rendered_manifest, &session->manifest) != ERR_OK) {
        croft_orch_control_reply_status_err(reply_out, "session-manifest-render-failed");
        return ERR_OK;
    }
    croft_orch_control_reply_manifest(reply_out, &session->rendered_manifest.view);
    return ERR_OK;
}

static int32_t croft_orch_control_plan(void *ctx,
                                       const SapWitOrchestrationSessionPlan *payload,
                                       SapWitOrchestrationControlReply *reply_out)
{
    croft_orchestration_runtime *runtime = (croft_orchestration_runtime *)ctx;
    croft_orch_session_slot *session = croft_orch_session_lookup(runtime, payload ? payload->session : 0u);

    if (!session || !reply_out) {
        return ERR_INVALID;
    }
    if (croft_orch_render_plan(&session->rendered_plan, &session->plan) != ERR_OK) {
        croft_orch_control_reply_status_err(reply_out, "session-plan-render-failed");
        return ERR_OK;
    }
    croft_orch_control_reply_plan_snapshot(reply_out, &session->rendered_plan.view);
    return ERR_OK;
}

static int32_t croft_orch_control_status(void *ctx,
                                         const SapWitOrchestrationSessionStatusRequest *payload,
                                         SapWitOrchestrationControlReply *reply_out)
{
    croft_orchestration_runtime *runtime = (croft_orchestration_runtime *)ctx;
    croft_orch_session_slot *session = croft_orch_session_lookup(runtime, payload ? payload->session : 0u);
    SapWitOrchestrationSessionStatus status;

    if (!session || !reply_out) {
        return ERR_INVALID;
    }
    if (croft_orch_session_status_value(session, &status) != ERR_OK) {
        croft_orch_control_reply_status_err(reply_out, "status-failed");
        return ERR_OK;
    }
    croft_orch_control_reply_session_status(reply_out, &status);
    return ERR_OK;
}

static int32_t croft_orch_control_stop(void *ctx,
                                       const SapWitOrchestrationSessionStop *payload,
                                       SapWitOrchestrationControlReply *reply_out)
{
    int32_t rc = croft_orchestration_runtime_stop_session((croft_orchestration_runtime *)ctx,
                                                          payload ? payload->session : 0u);
    if (!reply_out) {
        return ERR_INVALID;
    }
    if (rc != ERR_OK) {
        croft_orch_control_reply_status_err(reply_out, "stop-failed");
        return ERR_OK;
    }
    croft_orch_control_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_orch_control_join(void *ctx,
                                       const SapWitOrchestrationSessionJoin *payload,
                                       SapWitOrchestrationControlReply *reply_out)
{
    int32_t rc = croft_orchestration_runtime_join_session((croft_orchestration_runtime *)ctx,
                                                          payload ? payload->session : 0u);
    if (!reply_out) {
        return ERR_INVALID;
    }
    if (rc != ERR_OK) {
        croft_orch_control_reply_status_err(reply_out, "join-failed");
        return ERR_OK;
    }
    croft_orch_control_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_orch_control_session_drop(void *ctx,
                                               const SapWitOrchestrationSessionDrop *payload,
                                               SapWitOrchestrationControlReply *reply_out)
{
    croft_orchestration_runtime *runtime = (croft_orchestration_runtime *)ctx;
    croft_orch_session_slot *session = croft_orch_session_lookup(runtime, payload ? payload->session : 0u);

    if (!session || !reply_out) {
        return ERR_INVALID;
    }
    (void)croft_orchestration_runtime_stop_session(runtime, payload->session);
    (void)croft_orchestration_runtime_join_session(runtime, payload->session);
    croft_orch_session_slot_clear(session);
    if (runtime->last_session == payload->session) {
        runtime->last_session = SAP_WIT_ORCHESTRATION_SESSION_RESOURCE_INVALID;
    }
    croft_orch_control_reply_status_ok(reply_out);
    return ERR_OK;
}
