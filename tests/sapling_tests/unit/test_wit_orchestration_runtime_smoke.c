#include "croft/orchestration_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                                           \
    do {                                                                                      \
        if (!(expr)) {                                                                        \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                 \
            return 1;                                                                         \
        }                                                                                     \
    } while (0)

#define CHECK_STATUS_OK(reply_expr)                                                           \
    do {                                                                                      \
        const SapWitOrchestrationControlReply *_reply = &(reply_expr);                        \
        CHECK(_reply->case_tag == SAP_WIT_ORCHESTRATION_CONTROL_REPLY_STATUS);                \
        if (!_reply->val.status.is_v_ok) {                                                    \
            fprintf(stderr,                                                                    \
                    "STATUS ERROR: %.*s (%s:%d)\n",                                            \
                    (int)_reply->val.status.v_val.err.v_len,                                  \
                    _reply->val.status.v_val.err.v_data                                       \
                        ? (const char *)_reply->val.status.v_val.err.v_data                   \
                        : "",                                                                  \
                    __FILE__,                                                                  \
                    __LINE__);                                                                 \
            return 1;                                                                         \
        }                                                                                     \
    } while (0)

#define CHECK_PLAN_OK(reply_expr)                                                             \
    do {                                                                                      \
        const SapWitOrchestrationControlReply *_reply = &(reply_expr);                        \
        CHECK(_reply->case_tag == SAP_WIT_ORCHESTRATION_CONTROL_REPLY_RESOLVE);               \
        if (!_reply->val.resolve.is_v_ok) {                                                   \
            fprintf(stderr,                                                                    \
                    "PLAN ERROR: case=%u len=%u msg=%.*s (%s:%d)\n",                           \
                    (unsigned)_reply->case_tag,                                                \
                    (unsigned)_reply->val.resolve.v_val.err.v_len,                             \
                    (int)_reply->val.resolve.v_val.err.v_len,                                 \
                    _reply->val.resolve.v_val.err.v_data                                      \
                        ? (const char *)_reply->val.resolve.v_val.err.v_data                  \
                        : "",                                                                  \
                    __FILE__,                                                                  \
                    __LINE__);                                                                 \
            return 1;                                                                         \
        }                                                                                     \
    } while (0)

typedef int (*RecordWriterFn)(ThatchRegion *region, const void *value);

typedef struct {
    uint8_t *data;
    uint32_t len;
} Blob;

static void blob_dispose(Blob *blob)
{
    if (!blob) {
        return;
    }
    free(blob->data);
    blob->data = NULL;
    blob->len = 0u;
}

static int encode_string_list(const char *const *items, uint32_t count, Blob *blob_out)
{
    uint32_t capacity = 128u;
    int rc;

    blob_out->data = NULL;
    blob_out->len = 0u;
    if (count == 0u) {
        return ERR_OK;
    }
    for (;;) {
        ThatchRegion region;
        uint32_t i;
        uint8_t *buffer = (uint8_t *)malloc(capacity);

        CHECK(buffer != NULL);
        memset(&region, 0, sizeof(region));
        region.page_ptr = buffer;
        region.capacity = capacity;
        rc = ERR_OK;

        for (i = 0u; rc == ERR_OK && i < count; i++) {
            uint32_t len = (uint32_t)strlen(items[i]);
            rc = thatch_write_tag(&region, SAP_WIT_TAG_STRING);
            if (rc == ERR_OK) {
                rc = thatch_write_data(&region, &len, sizeof(len));
            }
            if (rc == ERR_OK && len > 0u) {
                rc = thatch_write_data(&region, items[i], len);
            }
        }
        if (rc == ERR_OK) {
            blob_out->data = buffer;
            blob_out->len = thatch_region_used(&region);
            return ERR_OK;
        }
        free(buffer);
        if (rc != ERR_OOM && rc != ERR_FULL) {
            return rc;
        }
        capacity *= 2u;
    }
}

static int encode_record_list(const void *items,
                              uint32_t count,
                              size_t stride,
                              RecordWriterFn writer,
                              Blob *blob_out)
{
    uint32_t capacity = 256u;
    int rc;

    blob_out->data = NULL;
    blob_out->len = 0u;
    if (count == 0u) {
        return ERR_OK;
    }
    for (;;) {
        ThatchRegion region;
        uint32_t i;
        uint8_t *buffer = (uint8_t *)malloc(capacity);

        CHECK(buffer != NULL);
        memset(&region, 0, sizeof(region));
        region.page_ptr = buffer;
        region.capacity = capacity;
        rc = ERR_OK;

        for (i = 0u; rc == ERR_OK && i < count; i++) {
            rc = writer(&region, (const uint8_t *)items + (i * stride));
        }
        if (rc == ERR_OK) {
            blob_out->data = buffer;
            blob_out->len = thatch_region_used(&region);
            return ERR_OK;
        }
        free(buffer);
        if (rc != ERR_OOM && rc != ERR_FULL) {
            return rc;
        }
        capacity *= 2u;
    }
}

static int write_table_spec_record(ThatchRegion *region, const void *value)
{
    return sap_wit_write_orchestration_table_spec(
        region,
        (const SapWitOrchestrationTableSpec *)value);
}

static int parse_first_slot_binding(const SapWitOrchestrationPlan *plan,
                                    char *slot_out,
                                    size_t slot_cap,
                                    char *bundle_out,
                                    size_t bundle_cap)
{
    ThatchRegion view;
    ThatchCursor cursor = 0u;
    SapWitOrchestrationSlotBinding binding;
    int rc;

    if (!plan || !slot_out || !bundle_out || plan->selected_slots_len == 0u) {
        return ERR_INVALID;
    }
    rc = thatch_region_init_readonly(&view,
                                     plan->selected_slots_data,
                                     plan->selected_slots_byte_len);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = sap_wit_read_orchestration_slot_binding(&view, &cursor, &binding);
    if (rc != ERR_OK) {
        return rc;
    }
    if (cursor > plan->selected_slots_byte_len) {
        return ERR_CORRUPT;
    }
    if (binding.slot_len + 1u > slot_cap || binding.bundle_len + 1u > bundle_cap) {
        return ERR_RANGE;
    }
    memcpy(slot_out, binding.slot_data, binding.slot_len);
    slot_out[binding.slot_len] = '\0';
    memcpy(bundle_out, binding.bundle_data, binding.bundle_len);
    bundle_out[binding.bundle_len] = '\0';
    return ERR_OK;
}

static int parse_first_diagnostic(const SapWitOrchestrationPlan *plan,
                                  char *text_out,
                                  size_t text_cap)
{
    ThatchRegion view;
    ThatchCursor cursor = 0u;
    uint8_t tag = 0u;
    uint32_t len = 0u;
    const uint8_t *text = NULL;
    int rc;

    if (!plan || !text_out || text_cap == 0u || plan->diagnostics_len == 0u) {
        return ERR_INVALID;
    }
    rc = thatch_region_init_readonly(&view, plan->diagnostics_data, plan->diagnostics_byte_len);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = thatch_read_tag(&view, &cursor, &tag);
    if (rc != ERR_OK || tag != SAP_WIT_TAG_STRING) {
        return rc != ERR_OK ? rc : ERR_TYPE;
    }
    rc = thatch_read_data(&view, &cursor, sizeof(len), &len);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = thatch_read_ptr(&view, &cursor, len, (const void **)&text);
    if (rc != ERR_OK || len + 1u > text_cap) {
        return rc != ERR_OK ? rc : ERR_RANGE;
    }
    memcpy(text_out, text, len);
    text_out[len] = '\0';
    return ERR_OK;
}

int main(void)
{
    croft_orchestration_runtime_config config;
    croft_orchestration_runtime *runtime = NULL;
    const SapWitOrchestrationControlDispatchOps *ops = NULL;
    SapWitOrchestrationBuilderCreate create_payload = {0};
    SapWitOrchestrationControlReply reply;
    SapWitOrchestrationBuilderResource builder = SAP_WIT_ORCHESTRATION_BUILDER_RESOURCE_INVALID;
    SapWitOrchestrationTableSpec table = {0};
    SapWitOrchestrationDbSchemaSpec schema = {0};
    SapWitOrchestrationMailboxSpec mailbox = {0};
    SapWitOrchestrationWorkerSpec worker = {0};
    Blob tables_blob = {0};
    Blob consumers_blob = {0};
    Blob producers_blob = {0};
    Blob worker_tables_blob = {0};
    Blob worker_inboxes_blob = {0};
    char slot[96];
    char bundle[96];
    char diagnostic[128];
    int32_t rc;

    croft_orchestration_runtime_config_default(&config);
    runtime = croft_orchestration_runtime_create(&config);
    CHECK(runtime != NULL);
    ops = croft_orchestration_runtime_control_ops();
    CHECK(ops != NULL);

    create_payload.name_data = (const uint8_t *)"runtime-smoke";
    create_payload.name_len = 13u;
    create_payload.family_data = (const uint8_t *)"croft_json_tree_text_view_family_current_machine";
    create_payload.family_len = (uint32_t)strlen((const char *)create_payload.family_data);
    create_payload.applicability_data = (const uint8_t *)"current-machine-windowed";
    create_payload.applicability_len = 24u;

    sap_wit_zero_orchestration_control_reply(&reply);
    CHECK(ops->create(runtime, &create_payload, &reply) == ERR_OK);
    CHECK(reply.case_tag == SAP_WIT_ORCHESTRATION_CONTROL_REPLY_BUILDER);
    CHECK(reply.val.builder.is_v_ok == 1u);
    builder = reply.val.builder.v_val.ok.v;
    sap_wit_dispose_orchestration_control_reply(&reply);

    {
        SapWitOrchestrationBuilderPreferSlot payload = {0};
        payload.builder = builder;
        payload.slot_data = (const uint8_t *)"croft-editor-shell-slot-current-machine";
        payload.slot_len = 39u;
        payload.bundle_data = (const uint8_t *)"croft-editor-appkit-current-machine";
        payload.bundle_len = 35u;
        sap_wit_zero_orchestration_control_reply(&reply);
        CHECK(ops->prefer_slot(runtime, &payload, &reply) == ERR_OK);
        CHECK(reply.case_tag == SAP_WIT_ORCHESTRATION_CONTROL_REPLY_STATUS);
        CHECK_STATUS_OK(reply);
        sap_wit_dispose_orchestration_control_reply(&reply);
    }
    {
        SapWitOrchestrationBuilderAddModule payload = {0};
        payload.builder = builder;
        payload.name_data = (const uint8_t *)"workers";
        payload.name_len = 7u;
        payload.path_data = (const uint8_t *)"/tmp/workers.wasm";
        payload.path_len = 17u;
        sap_wit_zero_orchestration_control_reply(&reply);
        CHECK(ops->add_module(runtime, &payload, &reply) == ERR_OK);
        CHECK_STATUS_OK(reply);
        sap_wit_dispose_orchestration_control_reply(&reply);
    }

    table.name_data = (const uint8_t *)"views";
    table.name_len = 5u;
    table.key_format_data = (const uint8_t *)"utf8";
    table.key_format_len = 4u;
    table.value_format_data = (const uint8_t *)"utf8";
    table.value_format_len = 4u;
    table.access = SAP_WIT_ORCHESTRATION_TABLE_ACCESS_READ | SAP_WIT_ORCHESTRATION_TABLE_ACCESS_WRITE;
    CHECK(encode_record_list(&table,
                             1u,
                             sizeof(table),
                             write_table_spec_record,
                             &tables_blob)
          == ERR_OK);
    schema.name_data = (const uint8_t *)"json-db";
    schema.name_len = 7u;
    schema.tables_data = tables_blob.data;
    schema.tables_len = 1u;
    schema.tables_byte_len = tables_blob.len;
    {
        SapWitOrchestrationBuilderSetDbSchema payload = {0};
        payload.builder = builder;
        payload.schema = schema;
        sap_wit_zero_orchestration_control_reply(&reply);
        CHECK(ops->set_db_schema(runtime, &payload, &reply) == ERR_OK);
        CHECK_STATUS_OK(reply);
        sap_wit_dispose_orchestration_control_reply(&reply);
    }

    CHECK(encode_string_list((const char *const[]){"bootstrap"}, 1u, &producers_blob) == ERR_OK);
    CHECK(encode_string_list((const char *const[]){"json-parser"}, 1u, &consumers_blob) == ERR_OK);
    mailbox.name_data = (const uint8_t *)"json-input";
    mailbox.name_len = 10u;
    mailbox.message_format_data = (const uint8_t *)"application/json";
    mailbox.message_format_len = 16u;
    mailbox.producers_data = producers_blob.data;
    mailbox.producers_len = 1u;
    mailbox.producers_byte_len = producers_blob.len;
    mailbox.consumers_data = consumers_blob.data;
    mailbox.consumers_len = 1u;
    mailbox.consumers_byte_len = consumers_blob.len;
    mailbox.durability = SAP_WIT_ORCHESTRATION_MAILBOX_DURABILITY_VOLATILE;
    {
        SapWitOrchestrationBuilderAddMailbox payload = {0};
        payload.builder = builder;
        payload.mailbox = mailbox;
        sap_wit_zero_orchestration_control_reply(&reply);
        CHECK(ops->add_mailbox(runtime, &payload, &reply) == ERR_OK);
        CHECK_STATUS_OK(reply);
        sap_wit_dispose_orchestration_control_reply(&reply);
    }

    CHECK(encode_string_list((const char *const[]){"views"}, 1u, &worker_tables_blob) == ERR_OK);
    CHECK(encode_string_list((const char *const[]){"json-input"}, 1u, &worker_inboxes_blob) == ERR_OK);
    worker.name_data = (const uint8_t *)"json-parser";
    worker.name_len = 11u;
    worker.module_data = (const uint8_t *)"workers";
    worker.module_len = 7u;
    worker.replicas = 1u;
    worker.allowed_tables_data = worker_tables_blob.data;
    worker.allowed_tables_len = 1u;
    worker.allowed_tables_byte_len = worker_tables_blob.len;
    worker.inboxes_data = worker_inboxes_blob.data;
    worker.inboxes_len = 1u;
    worker.inboxes_byte_len = worker_inboxes_blob.len;
    worker.outboxes_data = NULL;
    worker.outboxes_len = 0u;
    worker.outboxes_byte_len = 0u;
    worker.has_startup_format = 1u;
    worker.startup_format_data = (const uint8_t *)"application/json";
    worker.startup_format_len = 16u;
    worker.startup_bytes_data = (const uint8_t *)"{}";
    worker.startup_bytes_len = 2u;
    {
        SapWitOrchestrationBuilderAddWorker payload = {0};
        payload.builder = builder;
        payload.worker = worker;
        sap_wit_zero_orchestration_control_reply(&reply);
        CHECK(ops->add_worker(runtime, &payload, &reply) == ERR_OK);
        CHECK_STATUS_OK(reply);
        sap_wit_dispose_orchestration_control_reply(&reply);
    }

    {
        SapWitOrchestrationBuilderResolve payload = {0};
        payload.builder = builder;
        sap_wit_zero_orchestration_control_reply(&reply);
        CHECK(ops->resolve(runtime, &payload, &reply) == ERR_OK);
        CHECK_PLAN_OK(reply);
        CHECK(parse_first_slot_binding(&reply.val.resolve.v_val.ok.v, slot, sizeof(slot), bundle, sizeof(bundle)) == ERR_OK);
        CHECK(strcmp(slot, "croft-editor-shell-slot-current-machine") == 0);
        CHECK(strcmp(bundle, "croft-editor-appkit-current-machine") == 0);
        CHECK(parse_first_diagnostic(&reply.val.resolve.v_val.ok.v, diagnostic, sizeof(diagnostic)) == ERR_OK);
        CHECK(strcmp(diagnostic, "resolved-with-compiled-xpi-registry") == 0);
        sap_wit_dispose_orchestration_control_reply(&reply);
    }

    {
        SapWitOrchestrationBuilderCreate payload = create_payload;
        SapWitOrchestrationBuilderResource conflict_builder;
        SapWitOrchestrationBuilderRequireBundle require = {0};

        sap_wit_zero_orchestration_control_reply(&reply);
        CHECK(ops->create(runtime, &payload, &reply) == ERR_OK);
        conflict_builder = reply.val.builder.v_val.ok.v;
        sap_wit_dispose_orchestration_control_reply(&reply);

        require.builder = conflict_builder;
        require.bundle_data = (const uint8_t *)"croft-editor-appkit-current-machine";
        require.bundle_len = 35u;
        sap_wit_zero_orchestration_control_reply(&reply);
        CHECK(ops->require_bundle(runtime, &require, &reply) == ERR_OK);
        CHECK_STATUS_OK(reply);
        sap_wit_dispose_orchestration_control_reply(&reply);

        require.bundle_data = (const uint8_t *)"croft-editor-scene-metal-native-current-machine";
        require.bundle_len = 47u;
        sap_wit_zero_orchestration_control_reply(&reply);
        CHECK(ops->require_bundle(runtime, &require, &reply) == ERR_OK);
        CHECK_STATUS_OK(reply);
        sap_wit_dispose_orchestration_control_reply(&reply);

        {
            SapWitOrchestrationBuilderResolve resolve = { conflict_builder };
            sap_wit_zero_orchestration_control_reply(&reply);
            CHECK(ops->resolve(runtime, &resolve, &reply) == ERR_OK);
            CHECK(reply.case_tag == SAP_WIT_ORCHESTRATION_CONTROL_REPLY_RESOLVE);
            CHECK(reply.val.resolve.is_v_ok == 0u);
            sap_wit_dispose_orchestration_control_reply(&reply);
        }
    }

    {
        SapWitOrchestrationBuilderCreate payload = create_payload;
        SapWitOrchestrationBuilderResource applicability_builder;

        payload.applicability_data = (const uint8_t *)"current-machine-windows";
        payload.applicability_len = 23u;
        sap_wit_zero_orchestration_control_reply(&reply);
        CHECK(ops->create(runtime, &payload, &reply) == ERR_OK);
        applicability_builder = reply.val.builder.v_val.ok.v;
        sap_wit_dispose_orchestration_control_reply(&reply);
        {
            SapWitOrchestrationBuilderResolve resolve = { applicability_builder };
            sap_wit_zero_orchestration_control_reply(&reply);
            CHECK(ops->resolve(runtime, &resolve, &reply) == ERR_OK);
            CHECK(reply.case_tag == SAP_WIT_ORCHESTRATION_CONTROL_REPLY_RESOLVE);
            CHECK(reply.val.resolve.is_v_ok == 0u);
            sap_wit_dispose_orchestration_control_reply(&reply);
        }
    }

    {
        SapWitOrchestrationBuilderCreate payload = create_payload;
        SapWitOrchestrationBuilderResource multi_builder;
        SapWitOrchestrationBuilderAddModule add = {0};

        payload.family_data = (const uint8_t *)"croft_orchestration_mailbox_demo_family_current_machine";
        payload.family_len = (uint32_t)strlen((const char *)payload.family_data);
        payload.applicability_data = (const uint8_t *)"current-machine";
        payload.applicability_len = 15u;
        sap_wit_zero_orchestration_control_reply(&reply);
        CHECK(ops->create(runtime, &payload, &reply) == ERR_OK);
        multi_builder = reply.val.builder.v_val.ok.v;
        sap_wit_dispose_orchestration_control_reply(&reply);

        add.builder = multi_builder;
        add.name_data = (const uint8_t *)"a";
        add.name_len = 1u;
        add.path_data = (const uint8_t *)"/tmp/a.wasm";
        add.path_len = 11u;
        sap_wit_zero_orchestration_control_reply(&reply);
        CHECK(ops->add_module(runtime, &add, &reply) == ERR_OK);
        CHECK_STATUS_OK(reply);
        sap_wit_dispose_orchestration_control_reply(&reply);

        add.name_data = (const uint8_t *)"b";
        add.path_data = (const uint8_t *)"/tmp/b.wasm";
        sap_wit_zero_orchestration_control_reply(&reply);
        CHECK(ops->add_module(runtime, &add, &reply) == ERR_OK);
        CHECK_STATUS_OK(reply);
        sap_wit_dispose_orchestration_control_reply(&reply);

        {
            SapWitOrchestrationBuilderResolve resolve = { multi_builder };
            sap_wit_zero_orchestration_control_reply(&reply);
            CHECK(ops->resolve(runtime, &resolve, &reply) == ERR_OK);
            CHECK(reply.case_tag == SAP_WIT_ORCHESTRATION_CONTROL_REPLY_RESOLVE);
            CHECK(reply.val.resolve.is_v_ok == 0u);
            sap_wit_dispose_orchestration_control_reply(&reply);
        }
    }

    blob_dispose(&tables_blob);
    blob_dispose(&producers_blob);
    blob_dispose(&consumers_blob);
    blob_dispose(&worker_tables_blob);
    blob_dispose(&worker_inboxes_blob);
    croft_orchestration_runtime_destroy(runtime);
    return 0;
}
