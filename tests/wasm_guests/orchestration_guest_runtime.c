#include "orchestration_guest_runtime.h"

#include "croft/wit_wire.h"
#include "sapling/err.h"
#include "sapling/thatch.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *data;
    uint32_t len;
} CroftOrchGuestBlob;

typedef int (*CroftOrchGuestRecordWriterFn)(ThatchRegion *region, const void *value);

static void croft_orch_guest_blob_dispose(CroftOrchGuestBlob *blob)
{
    if (!blob) {
        return;
    }
    sap_wit_rt_free(blob->data);
    blob->data = NULL;
    blob->len = 0u;
}

uint32_t croft_orch_guest_strlen(const char *text)
{
    return text ? (uint32_t)sap_wit_rt_strlen(text) : 0u;
}

int croft_orch_guest_text_equals(const char *lhs, const char *rhs)
{
    uint32_t i = 0u;

    if (!lhs || !rhs) {
        return 0;
    }
    while (lhs[i] != '\0' && rhs[i] != '\0') {
        if (lhs[i] != rhs[i]) {
            return 0;
        }
        i++;
    }
    return lhs[i] == rhs[i];
}

int croft_orch_guest_copy_text(char *dest, uint32_t cap, const uint8_t *data, uint32_t len)
{
    if (!dest || cap == 0u) {
        return ERR_INVALID;
    }
    dest[0] = '\0';
    if (!data && len > 0u) {
        return ERR_INVALID;
    }
    if (len + 1u > cap) {
        return ERR_RANGE;
    }
    if (len > 0u) {
        sap_wit_rt_memcpy(dest, data, len);
    }
    dest[len] = '\0';
    return ERR_OK;
}

static int croft_orch_guest_encode_string_list(const char *const *items,
                                               uint32_t count,
                                               CroftOrchGuestBlob *blob_out)
{
    ThatchRegion region;
    uint8_t *buffer = NULL;
    uint32_t capacity = 128u;
    uint32_t i;
    int rc;

    if (!blob_out) {
        return ERR_INVALID;
    }
    blob_out->data = NULL;
    blob_out->len = 0u;
    if (count == 0u) {
        return ERR_OK;
    }

    for (;;) {
        buffer = (uint8_t *)sap_wit_rt_malloc(capacity);
        if (!buffer) {
            return ERR_OOM;
        }
        sap_wit_guest_region_init_writable(&region, buffer, capacity);
        rc = ERR_OK;

        for (i = 0u; rc == ERR_OK && i < count; i++) {
            uint32_t len = croft_orch_guest_strlen(items ? items[i] : NULL);
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
        sap_wit_rt_free(buffer);
        if (rc != ERR_OOM && rc != ERR_FULL) {
            return rc;
        }
        if (capacity >= (UINT32_MAX / 2u)) {
            return ERR_OOM;
        }
        capacity *= 2u;
    }
}

static int croft_orch_guest_encode_record_list(const void *items,
                                               uint32_t count,
                                               uint32_t stride,
                                               CroftOrchGuestRecordWriterFn writer,
                                               CroftOrchGuestBlob *blob_out)
{
    ThatchRegion region;
    uint8_t *buffer = NULL;
    uint32_t capacity = 256u;
    uint32_t i;
    int rc;

    if (!items || stride == 0u || !writer || !blob_out) {
        return ERR_INVALID;
    }
    blob_out->data = NULL;
    blob_out->len = 0u;
    if (count == 0u) {
        return ERR_OK;
    }

    for (;;) {
        buffer = (uint8_t *)sap_wit_rt_malloc(capacity);
        if (!buffer) {
            return ERR_OOM;
        }
        sap_wit_guest_region_init_writable(&region, buffer, capacity);
        rc = ERR_OK;
        for (i = 0u; rc == ERR_OK && i < count; i++) {
            const uint8_t *item_ptr = (const uint8_t *)items + ((size_t)i * stride);
            rc = writer(&region, item_ptr);
        }
        if (rc == ERR_OK) {
            blob_out->data = buffer;
            blob_out->len = thatch_region_used(&region);
            return ERR_OK;
        }
        sap_wit_rt_free(buffer);
        if (rc != ERR_OOM && rc != ERR_FULL) {
            return rc;
        }
        if (capacity >= (UINT32_MAX / 2u)) {
            return ERR_OOM;
        }
        capacity *= 2u;
    }
}

static int croft_orch_guest_control_call(CroftOrchGuestRuntime *runtime,
                                         const SapWitOrchestrationControlCommand *command,
                                         SapWitOrchestrationControlReply *reply_out)
{
    if (!runtime || !runtime->initialized || !command || !reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_orchestration_control_reply(reply_out);
    return sap_wit_guest_orchestration_bootstrap_import_control(&runtime->transport,
                                                                command,
                                                                reply_out);
}

static int croft_orch_guest_status_from_reply(const SapWitOrchestrationControlReply *reply)
{
    if (!reply
            || reply->case_tag != SAP_WIT_ORCHESTRATION_CONTROL_REPLY_STATUS
            || !reply->val.status.is_v_ok) {
        return ERR_INVALID;
    }
    return ERR_OK;
}

static int croft_orch_guest_init_list_view(const uint8_t *data,
                                           uint32_t len,
                                           ThatchRegion *view_out,
                                           ThatchCursor *cursor_out)
{
    int rc;

    if (!view_out || !cursor_out) {
        return ERR_INVALID;
    }
    if (!data && len > 0u) {
        return ERR_INVALID;
    }
    rc = thatch_region_init_readonly(view_out, data, len);
    if (rc != ERR_OK) {
        return rc;
    }
    *cursor_out = 0u;
    return ERR_OK;
}

static int croft_orch_guest_store_qualified_key(const char *table,
                                                const uint8_t *key,
                                                uint32_t key_len,
                                                uint8_t **qualified_out,
                                                uint32_t *qualified_len_out)
{
    uint32_t table_len;
    uint8_t *qualified;

    if (!table || !qualified_out || !qualified_len_out || (!key && key_len > 0u)) {
        return ERR_INVALID;
    }
    *qualified_out = NULL;
    *qualified_len_out = 0u;

    table_len = croft_orch_guest_strlen(table);
    qualified = (uint8_t *)sap_wit_rt_malloc(table_len + 1u + key_len);
    if (!qualified) {
        return ERR_OOM;
    }
    if (table_len > 0u) {
        sap_wit_rt_memcpy(qualified, table, table_len);
    }
    qualified[table_len] = 0u;
    if (key_len > 0u) {
        sap_wit_rt_memcpy(qualified + table_len + 1u, key, key_len);
    }
    *qualified_out = qualified;
    *qualified_len_out = table_len + 1u + key_len;
    return ERR_OK;
}

int32_t croft_orch_guest_runtime_init(CroftOrchGuestRuntime *runtime)
{
    if (!runtime) {
        return ERR_INVALID;
    }
    if (runtime->initialized) {
        return ERR_OK;
    }
    sap_wit_rt_memset(runtime, 0, sizeof(*runtime));
    sap_wit_croft_wasm_guest_context_init_default(&runtime->guest_ctx);
    sap_wit_croft_wasm_guest_transport_init(&runtime->transport, &runtime->guest_ctx);
    runtime->initialized = 1u;
    return ERR_OK;
}

void croft_orch_guest_runtime_dispose(CroftOrchGuestRuntime *runtime)
{
    if (!runtime) {
        return;
    }
    sap_wit_guest_transport_dispose(&runtime->transport);
    sap_wit_croft_wasm_guest_context_dispose(&runtime->guest_ctx);
    sap_wit_rt_memset(runtime, 0, sizeof(*runtime));
}

int32_t croft_orch_guest_builder_create(CroftOrchGuestRuntime *runtime,
                                        const char *name,
                                        const char *family,
                                        const char *applicability,
                                        SapWitOrchestrationBuilderResource *builder_out)
{
    SapWitOrchestrationControlCommand command = {0};
    SapWitOrchestrationControlReply reply;
    int32_t rc;

    if (!builder_out || !name || !family || !applicability) {
        return ERR_INVALID;
    }
    *builder_out = SAP_WIT_ORCHESTRATION_BUILDER_RESOURCE_INVALID;
    command.case_tag = SAP_WIT_ORCHESTRATION_CONTROL_COMMAND_CREATE;
    command.val.create.name_data = (const uint8_t *)name;
    command.val.create.name_len = croft_orch_guest_strlen(name);
    command.val.create.family_data = (const uint8_t *)family;
    command.val.create.family_len = croft_orch_guest_strlen(family);
    command.val.create.applicability_data = (const uint8_t *)applicability;
    command.val.create.applicability_len = croft_orch_guest_strlen(applicability);

    rc = croft_orch_guest_control_call(runtime, &command, &reply);
    if (rc == ERR_OK) {
        if (reply.case_tag != SAP_WIT_ORCHESTRATION_CONTROL_REPLY_BUILDER
                || !reply.val.builder.is_v_ok) {
            rc = ERR_INVALID;
        } else {
            *builder_out = reply.val.builder.v_val.ok.v;
        }
    }
    sap_wit_dispose_orchestration_control_reply(&reply);
    return rc;
}

int32_t croft_orch_guest_builder_require_bundle(CroftOrchGuestRuntime *runtime,
                                                SapWitOrchestrationBuilderResource builder,
                                                const char *bundle)
{
    SapWitOrchestrationControlCommand command = {0};
    SapWitOrchestrationControlReply reply;
    int32_t rc;

    if (!bundle) {
        return ERR_INVALID;
    }
    command.case_tag = SAP_WIT_ORCHESTRATION_CONTROL_COMMAND_REQUIRE_BUNDLE;
    command.val.require_bundle.builder = builder;
    command.val.require_bundle.bundle_data = (const uint8_t *)bundle;
    command.val.require_bundle.bundle_len = croft_orch_guest_strlen(bundle);
    rc = croft_orch_guest_control_call(runtime, &command, &reply);
    if (rc == ERR_OK) {
        rc = croft_orch_guest_status_from_reply(&reply);
    }
    sap_wit_dispose_orchestration_control_reply(&reply);
    return rc;
}

int32_t croft_orch_guest_builder_prefer_slot(CroftOrchGuestRuntime *runtime,
                                             SapWitOrchestrationBuilderResource builder,
                                             const char *slot,
                                             const char *bundle)
{
    SapWitOrchestrationControlCommand command = {0};
    SapWitOrchestrationControlReply reply;
    int32_t rc;

    if (!slot || !bundle) {
        return ERR_INVALID;
    }
    command.case_tag = SAP_WIT_ORCHESTRATION_CONTROL_COMMAND_PREFER_SLOT;
    command.val.prefer_slot.builder = builder;
    command.val.prefer_slot.slot_data = (const uint8_t *)slot;
    command.val.prefer_slot.slot_len = croft_orch_guest_strlen(slot);
    command.val.prefer_slot.bundle_data = (const uint8_t *)bundle;
    command.val.prefer_slot.bundle_len = croft_orch_guest_strlen(bundle);
    rc = croft_orch_guest_control_call(runtime, &command, &reply);
    if (rc == ERR_OK) {
        rc = croft_orch_guest_status_from_reply(&reply);
    }
    sap_wit_dispose_orchestration_control_reply(&reply);
    return rc;
}

int32_t croft_orch_guest_builder_add_module(CroftOrchGuestRuntime *runtime,
                                            SapWitOrchestrationBuilderResource builder,
                                            const char *name,
                                            const char *path)
{
    SapWitOrchestrationControlCommand command = {0};
    SapWitOrchestrationControlReply reply;
    int32_t rc;

    if (!name || !path) {
        return ERR_INVALID;
    }
    command.case_tag = SAP_WIT_ORCHESTRATION_CONTROL_COMMAND_ADD_MODULE;
    command.val.add_module.builder = builder;
    command.val.add_module.name_data = (const uint8_t *)name;
    command.val.add_module.name_len = croft_orch_guest_strlen(name);
    command.val.add_module.path_data = (const uint8_t *)path;
    command.val.add_module.path_len = croft_orch_guest_strlen(path);
    rc = croft_orch_guest_control_call(runtime, &command, &reply);
    if (rc == ERR_OK) {
        rc = croft_orch_guest_status_from_reply(&reply);
    }
    sap_wit_dispose_orchestration_control_reply(&reply);
    return rc;
}

int32_t croft_orch_guest_builder_set_db_schema(CroftOrchGuestRuntime *runtime,
                                               SapWitOrchestrationBuilderResource builder,
                                               const char *name,
                                               const CroftOrchGuestTableDecl *tables,
                                               uint32_t table_count)
{
    SapWitOrchestrationControlCommand command = {0};
    SapWitOrchestrationControlReply reply;
    SapWitOrchestrationTableSpec wit_tables[CROFT_ORCH_GUEST_TABLE_CAP];
    CroftOrchGuestBlob table_blob = {0};
    int32_t rc;
    uint32_t i;

    if (!name || (!tables && table_count > 0u) || table_count > CROFT_ORCH_GUEST_TABLE_CAP) {
        return ERR_INVALID;
    }
    for (i = 0u; i < table_count; i++) {
        wit_tables[i].name_data = (const uint8_t *)tables[i].name;
        wit_tables[i].name_len = croft_orch_guest_strlen(tables[i].name);
        wit_tables[i].key_format_data = (const uint8_t *)tables[i].key_format;
        wit_tables[i].key_format_len = croft_orch_guest_strlen(tables[i].key_format);
        wit_tables[i].value_format_data = (const uint8_t *)tables[i].value_format;
        wit_tables[i].value_format_len = croft_orch_guest_strlen(tables[i].value_format);
        wit_tables[i].access = tables[i].access;
    }
    rc = croft_orch_guest_encode_record_list(wit_tables,
                                             table_count,
                                             sizeof(wit_tables[0]),
                                             (CroftOrchGuestRecordWriterFn)sap_wit_write_orchestration_table_spec,
                                             &table_blob);
    if (rc != ERR_OK) {
        return rc;
    }

    command.case_tag = SAP_WIT_ORCHESTRATION_CONTROL_COMMAND_SET_DB_SCHEMA;
    command.val.set_db_schema.builder = builder;
    command.val.set_db_schema.schema.name_data = (const uint8_t *)name;
    command.val.set_db_schema.schema.name_len = croft_orch_guest_strlen(name);
    command.val.set_db_schema.schema.tables_data = table_blob.data;
    command.val.set_db_schema.schema.tables_len = table_count;
    command.val.set_db_schema.schema.tables_byte_len = table_blob.len;
    rc = croft_orch_guest_control_call(runtime, &command, &reply);
    if (rc == ERR_OK) {
        rc = croft_orch_guest_status_from_reply(&reply);
    }
    sap_wit_dispose_orchestration_control_reply(&reply);
    croft_orch_guest_blob_dispose(&table_blob);
    return rc;
}

int32_t croft_orch_guest_builder_add_mailbox(CroftOrchGuestRuntime *runtime,
                                             SapWitOrchestrationBuilderResource builder,
                                             const CroftOrchGuestMailboxDecl *mailbox)
{
    SapWitOrchestrationControlCommand command = {0};
    SapWitOrchestrationControlReply reply;
    CroftOrchGuestBlob producers = {0};
    CroftOrchGuestBlob consumers = {0};
    int32_t rc;

    if (!mailbox || !mailbox->name || !mailbox->message_format) {
        return ERR_INVALID;
    }
    rc = croft_orch_guest_encode_string_list(mailbox->producers, mailbox->producer_count, &producers);
    if (rc == ERR_OK) {
        rc = croft_orch_guest_encode_string_list(mailbox->consumers, mailbox->consumer_count, &consumers);
    }
    if (rc != ERR_OK) {
        croft_orch_guest_blob_dispose(&producers);
        croft_orch_guest_blob_dispose(&consumers);
        return rc;
    }

    command.case_tag = SAP_WIT_ORCHESTRATION_CONTROL_COMMAND_ADD_MAILBOX;
    command.val.add_mailbox.builder = builder;
    command.val.add_mailbox.mailbox.name_data = (const uint8_t *)mailbox->name;
    command.val.add_mailbox.mailbox.name_len = croft_orch_guest_strlen(mailbox->name);
    command.val.add_mailbox.mailbox.message_format_data = (const uint8_t *)mailbox->message_format;
    command.val.add_mailbox.mailbox.message_format_len = croft_orch_guest_strlen(mailbox->message_format);
    command.val.add_mailbox.mailbox.producers_data = producers.data;
    command.val.add_mailbox.mailbox.producers_len = mailbox->producer_count;
    command.val.add_mailbox.mailbox.producers_byte_len = producers.len;
    command.val.add_mailbox.mailbox.consumers_data = consumers.data;
    command.val.add_mailbox.mailbox.consumers_len = mailbox->consumer_count;
    command.val.add_mailbox.mailbox.consumers_byte_len = consumers.len;
    command.val.add_mailbox.mailbox.durability = mailbox->durability;

    rc = croft_orch_guest_control_call(runtime, &command, &reply);
    if (rc == ERR_OK) {
        rc = croft_orch_guest_status_from_reply(&reply);
    }
    sap_wit_dispose_orchestration_control_reply(&reply);
    croft_orch_guest_blob_dispose(&producers);
    croft_orch_guest_blob_dispose(&consumers);
    return rc;
}

int32_t croft_orch_guest_builder_add_worker(CroftOrchGuestRuntime *runtime,
                                            SapWitOrchestrationBuilderResource builder,
                                            const CroftOrchGuestWorkerDecl *worker)
{
    SapWitOrchestrationControlCommand command = {0};
    SapWitOrchestrationControlReply reply;
    CroftOrchGuestBlob tables = {0};
    CroftOrchGuestBlob inboxes = {0};
    CroftOrchGuestBlob outboxes = {0};
    int32_t rc;

    if (!worker || !worker->name || !worker->module || worker->replicas == 0u) {
        return ERR_INVALID;
    }
    rc = croft_orch_guest_encode_string_list(worker->allowed_tables,
                                             worker->allowed_table_count,
                                             &tables);
    if (rc == ERR_OK) {
        rc = croft_orch_guest_encode_string_list(worker->inboxes, worker->inbox_count, &inboxes);
    }
    if (rc == ERR_OK) {
        rc = croft_orch_guest_encode_string_list(worker->outboxes, worker->outbox_count, &outboxes);
    }
    if (rc != ERR_OK) {
        croft_orch_guest_blob_dispose(&tables);
        croft_orch_guest_blob_dispose(&inboxes);
        croft_orch_guest_blob_dispose(&outboxes);
        return rc;
    }

    command.case_tag = SAP_WIT_ORCHESTRATION_CONTROL_COMMAND_ADD_WORKER;
    command.val.add_worker.builder = builder;
    command.val.add_worker.worker.name_data = (const uint8_t *)worker->name;
    command.val.add_worker.worker.name_len = croft_orch_guest_strlen(worker->name);
    command.val.add_worker.worker.module_data = (const uint8_t *)worker->module;
    command.val.add_worker.worker.module_len = croft_orch_guest_strlen(worker->module);
    command.val.add_worker.worker.replicas = worker->replicas;
    command.val.add_worker.worker.allowed_tables_data = tables.data;
    command.val.add_worker.worker.allowed_tables_len = worker->allowed_table_count;
    command.val.add_worker.worker.allowed_tables_byte_len = tables.len;
    command.val.add_worker.worker.inboxes_data = inboxes.data;
    command.val.add_worker.worker.inboxes_len = worker->inbox_count;
    command.val.add_worker.worker.inboxes_byte_len = inboxes.len;
    command.val.add_worker.worker.outboxes_data = outboxes.data;
    command.val.add_worker.worker.outboxes_len = worker->outbox_count;
    command.val.add_worker.worker.outboxes_byte_len = outboxes.len;
    command.val.add_worker.worker.has_startup_format = worker->startup_format ? 1u : 0u;
    command.val.add_worker.worker.startup_format_data = (const uint8_t *)worker->startup_format;
    command.val.add_worker.worker.startup_format_len = croft_orch_guest_strlen(worker->startup_format);
    command.val.add_worker.worker.startup_bytes_data = worker->startup_bytes;
    command.val.add_worker.worker.startup_bytes_len = worker->startup_bytes_len;

    rc = croft_orch_guest_control_call(runtime, &command, &reply);
    if (rc == ERR_OK) {
        rc = croft_orch_guest_status_from_reply(&reply);
    }
    sap_wit_dispose_orchestration_control_reply(&reply);
    croft_orch_guest_blob_dispose(&tables);
    croft_orch_guest_blob_dispose(&inboxes);
    croft_orch_guest_blob_dispose(&outboxes);
    return rc;
}

int32_t croft_orch_guest_builder_resolve(CroftOrchGuestRuntime *runtime,
                                         SapWitOrchestrationBuilderResource builder)
{
    SapWitOrchestrationControlCommand command = {0};
    SapWitOrchestrationControlReply reply;
    int32_t rc;

    command.case_tag = SAP_WIT_ORCHESTRATION_CONTROL_COMMAND_RESOLVE;
    command.val.resolve.builder = builder;
    rc = croft_orch_guest_control_call(runtime, &command, &reply);
    if (rc == ERR_OK) {
        if (reply.case_tag != SAP_WIT_ORCHESTRATION_CONTROL_REPLY_RESOLVE
                || !reply.val.resolve.is_v_ok) {
            rc = ERR_INVALID;
        }
    }
    sap_wit_dispose_orchestration_control_reply(&reply);
    return rc;
}

int32_t croft_orch_guest_builder_launch(CroftOrchGuestRuntime *runtime,
                                        SapWitOrchestrationBuilderResource builder,
                                        SapWitOrchestrationSessionResource *session_out)
{
    SapWitOrchestrationControlCommand command = {0};
    SapWitOrchestrationControlReply reply;
    int32_t rc;

    if (!session_out) {
        return ERR_INVALID;
    }
    *session_out = SAP_WIT_ORCHESTRATION_SESSION_RESOURCE_INVALID;
    command.case_tag = SAP_WIT_ORCHESTRATION_CONTROL_COMMAND_LAUNCH;
    command.val.launch.builder = builder;
    rc = croft_orch_guest_control_call(runtime, &command, &reply);
    if (rc == ERR_OK) {
        if (reply.case_tag != SAP_WIT_ORCHESTRATION_CONTROL_REPLY_SESSION
                || !reply.val.session.is_v_ok) {
            rc = ERR_INVALID;
        } else {
            *session_out = reply.val.session.v_val.ok.v;
        }
    }
    sap_wit_dispose_orchestration_control_reply(&reply);
    return rc;
}

int32_t croft_orch_guest_decode_worker_startup(const uint8_t *bytes,
                                               uint32_t len,
                                               CroftOrchGuestWorkerStartup *startup_out)
{
    ThatchRegion view;
    ThatchCursor cursor = 0u;
    SapWitOrchestrationWorkerStartup startup;
    ThatchRegion list_view;
    ThatchCursor list_cursor = 0u;
    uint32_t i;
    int32_t rc;

    if (!bytes || !startup_out) {
        return ERR_INVALID;
    }
    sap_wit_rt_memset(startup_out, 0, sizeof(*startup_out));
    rc = thatch_region_init_readonly(&view, bytes, len);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = sap_wit_read_orchestration_worker_startup(&view, &cursor, &startup);
    if (rc != ERR_OK || cursor != len) {
        return rc != ERR_OK ? rc : ERR_CORRUPT;
    }
    startup_out->db_handle = startup.db_handle;

    if (startup.tables_len > CROFT_ORCH_GUEST_TABLE_CAP
            || startup.inboxes_len > CROFT_ORCH_GUEST_MAILBOX_CAP
            || startup.outboxes_len > CROFT_ORCH_GUEST_MAILBOX_CAP) {
        return ERR_RANGE;
    }

    rc = croft_orch_guest_init_list_view(startup.tables_data,
                                         startup.tables_byte_len,
                                         &list_view,
                                         &list_cursor);
    if (rc != ERR_OK) {
        return rc;
    }
    for (i = 0u; i < startup.tables_len; i++) {
        SapWitOrchestrationTableBinding binding;
        rc = sap_wit_read_orchestration_table_binding(&list_view, &list_cursor, &binding);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_orch_guest_copy_text(startup_out->tables[i].name,
                                        sizeof(startup_out->tables[i].name),
                                        binding.name_data,
                                        binding.name_len);
        if (rc == ERR_OK) {
            rc = croft_orch_guest_copy_text(startup_out->tables[i].key_format,
                                            sizeof(startup_out->tables[i].key_format),
                                            binding.key_format_data,
                                            binding.key_format_len);
        }
        if (rc == ERR_OK) {
            rc = croft_orch_guest_copy_text(startup_out->tables[i].value_format,
                                            sizeof(startup_out->tables[i].value_format),
                                            binding.value_format_data,
                                            binding.value_format_len);
        }
        if (rc != ERR_OK) {
            return rc;
        }
        startup_out->tables[i].access = binding.access;
    }
    if (list_cursor != startup.tables_byte_len) {
        return ERR_CORRUPT;
    }
    startup_out->table_count = startup.tables_len;

    rc = croft_orch_guest_init_list_view(startup.inboxes_data,
                                         startup.inboxes_byte_len,
                                         &list_view,
                                         &list_cursor);
    if (rc != ERR_OK) {
        return rc;
    }
    for (i = 0u; i < startup.inboxes_len; i++) {
        SapWitOrchestrationMailboxBinding binding;
        rc = sap_wit_read_orchestration_mailbox_binding(&list_view, &list_cursor, &binding);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_orch_guest_copy_text(startup_out->inboxes[i].name,
                                        sizeof(startup_out->inboxes[i].name),
                                        binding.name_data,
                                        binding.name_len);
        if (rc == ERR_OK) {
            rc = croft_orch_guest_copy_text(startup_out->inboxes[i].message_format,
                                            sizeof(startup_out->inboxes[i].message_format),
                                            binding.message_format_data,
                                            binding.message_format_len);
        }
        if (rc != ERR_OK) {
            return rc;
        }
        startup_out->inboxes[i].handle = binding.handle;
        startup_out->inboxes[i].durability = binding.durability;
    }
    if (list_cursor != startup.inboxes_byte_len) {
        return ERR_CORRUPT;
    }
    startup_out->inbox_count = startup.inboxes_len;

    rc = croft_orch_guest_init_list_view(startup.outboxes_data,
                                         startup.outboxes_byte_len,
                                         &list_view,
                                         &list_cursor);
    if (rc != ERR_OK) {
        return rc;
    }
    for (i = 0u; i < startup.outboxes_len; i++) {
        SapWitOrchestrationMailboxBinding binding;
        rc = sap_wit_read_orchestration_mailbox_binding(&list_view, &list_cursor, &binding);
        if (rc != ERR_OK) {
            return rc;
        }
        rc = croft_orch_guest_copy_text(startup_out->outboxes[i].name,
                                        sizeof(startup_out->outboxes[i].name),
                                        binding.name_data,
                                        binding.name_len);
        if (rc == ERR_OK) {
            rc = croft_orch_guest_copy_text(startup_out->outboxes[i].message_format,
                                            sizeof(startup_out->outboxes[i].message_format),
                                            binding.message_format_data,
                                            binding.message_format_len);
        }
        if (rc != ERR_OK) {
            return rc;
        }
        startup_out->outboxes[i].handle = binding.handle;
        startup_out->outboxes[i].durability = binding.durability;
    }
    if (list_cursor != startup.outboxes_byte_len) {
        return ERR_CORRUPT;
    }
    startup_out->outbox_count = startup.outboxes_len;
    startup_out->has_startup_format = startup.has_startup_format;
    if (startup.has_startup_format) {
        rc = croft_orch_guest_copy_text(startup_out->startup_format,
                                        sizeof(startup_out->startup_format),
                                        startup.startup_format_data,
                                        startup.startup_format_len);
        if (rc != ERR_OK) {
            return rc;
        }
    }
    startup_out->startup_bytes = startup.startup_bytes_data;
    startup_out->startup_bytes_len = startup.startup_bytes_len;
    return ERR_OK;
}

const CroftOrchGuestMailboxBinding *croft_orch_guest_find_inbox(
    const CroftOrchGuestWorkerStartup *startup,
    const char *name)
{
    uint32_t i;

    if (!startup || !name) {
        return NULL;
    }
    for (i = 0u; i < startup->inbox_count; i++) {
        if (croft_orch_guest_text_equals(startup->inboxes[i].name, name)) {
            return &startup->inboxes[i];
        }
    }
    return NULL;
}

const CroftOrchGuestMailboxBinding *croft_orch_guest_find_outbox(
    const CroftOrchGuestWorkerStartup *startup,
    const char *name)
{
    uint32_t i;

    if (!startup || !name) {
        return NULL;
    }
    for (i = 0u; i < startup->outbox_count; i++) {
        if (croft_orch_guest_text_equals(startup->outboxes[i].name, name)) {
            return &startup->outboxes[i];
        }
    }
    return NULL;
}

const CroftOrchGuestTableBinding *croft_orch_guest_find_table(
    const CroftOrchGuestWorkerStartup *startup,
    const char *name)
{
    uint32_t i;

    if (!startup || !name) {
        return NULL;
    }
    for (i = 0u; i < startup->table_count; i++) {
        if (croft_orch_guest_text_equals(startup->tables[i].name, name)) {
            return &startup->tables[i];
        }
    }
    return NULL;
}

int32_t croft_orch_guest_store_begin(CroftOrchGuestRuntime *runtime,
                                     SapWitOrchestrationDbResource db,
                                     uint8_t read_only,
                                     SapWitOrchestrationTxnResource *txn_out)
{
    SapWitOrchestrationStoreCommand command = {0};
    SapWitOrchestrationStoreReply reply;
    int32_t rc;

    if (!runtime || !txn_out) {
        return ERR_INVALID;
    }
    *txn_out = SAP_WIT_ORCHESTRATION_TXN_RESOURCE_INVALID;
    sap_wit_zero_orchestration_store_reply(&reply);
    command.case_tag = SAP_WIT_ORCHESTRATION_STORE_COMMAND_BEGIN;
    command.val.begin.db = db;
    command.val.begin.read_only = read_only;
    rc = sap_wit_guest_orchestration_worker_import_store(&runtime->transport, &command, &reply);
    if (rc == ERR_OK) {
        if (reply.case_tag != SAP_WIT_ORCHESTRATION_STORE_REPLY_TXN || !reply.val.txn.is_v_ok) {
            rc = ERR_INVALID;
        } else {
            *txn_out = reply.val.txn.v_val.ok.v;
        }
    }
    sap_wit_dispose_orchestration_store_reply(&reply);
    return rc;
}

int32_t croft_orch_guest_store_commit(CroftOrchGuestRuntime *runtime,
                                      SapWitOrchestrationTxnResource txn)
{
    SapWitOrchestrationStoreCommand command = {0};
    SapWitOrchestrationStoreReply reply;
    int32_t rc;

    sap_wit_zero_orchestration_store_reply(&reply);
    command.case_tag = SAP_WIT_ORCHESTRATION_STORE_COMMAND_COMMIT;
    command.val.commit.txn = txn;
    rc = sap_wit_guest_orchestration_worker_import_store(&runtime->transport, &command, &reply);
    if (rc == ERR_OK
            && (reply.case_tag != SAP_WIT_ORCHESTRATION_STORE_REPLY_STATUS
                || !reply.val.status.is_v_ok)) {
        rc = ERR_INVALID;
    }
    sap_wit_dispose_orchestration_store_reply(&reply);
    return rc;
}

int32_t croft_orch_guest_store_abort(CroftOrchGuestRuntime *runtime,
                                     SapWitOrchestrationTxnResource txn)
{
    SapWitOrchestrationStoreCommand command = {0};
    SapWitOrchestrationStoreReply reply;
    int32_t rc;

    sap_wit_zero_orchestration_store_reply(&reply);
    command.case_tag = SAP_WIT_ORCHESTRATION_STORE_COMMAND_ABORT;
    command.val.abort.txn = txn;
    rc = sap_wit_guest_orchestration_worker_import_store(&runtime->transport, &command, &reply);
    if (rc == ERR_OK
            && (reply.case_tag != SAP_WIT_ORCHESTRATION_STORE_REPLY_STATUS
                || !reply.val.status.is_v_ok)) {
        rc = ERR_INVALID;
    }
    sap_wit_dispose_orchestration_store_reply(&reply);
    return rc;
}

int32_t croft_orch_guest_store_put(CroftOrchGuestRuntime *runtime,
                                   SapWitOrchestrationTxnResource txn,
                                   const char *table,
                                   const uint8_t *key,
                                   uint32_t key_len,
                                   const uint8_t *value,
                                   uint32_t value_len)
{
    SapWitOrchestrationStoreCommand command = {0};
    SapWitOrchestrationStoreReply reply;
    uint8_t *qualified = NULL;
    uint32_t qualified_len = 0u;
    int32_t rc;

    rc = croft_orch_guest_store_qualified_key(table, key, key_len, &qualified, &qualified_len);
    if (rc != ERR_OK) {
        return rc;
    }

    sap_wit_zero_orchestration_store_reply(&reply);
    command.case_tag = SAP_WIT_ORCHESTRATION_STORE_COMMAND_PUT;
    command.val.put.txn = txn;
    command.val.put.key_data = qualified;
    command.val.put.key_len = qualified_len;
    command.val.put.value_data = value;
    command.val.put.value_len = value_len;
    rc = sap_wit_guest_orchestration_worker_import_store(&runtime->transport, &command, &reply);
    if (rc == ERR_OK
            && (reply.case_tag != SAP_WIT_ORCHESTRATION_STORE_REPLY_STATUS
                || !reply.val.status.is_v_ok)) {
        rc = ERR_INVALID;
    }
    sap_wit_dispose_orchestration_store_reply(&reply);
    sap_wit_rt_free(qualified);
    return rc;
}

int32_t croft_orch_guest_store_put_cstr(CroftOrchGuestRuntime *runtime,
                                        SapWitOrchestrationTxnResource txn,
                                        const char *table,
                                        const char *key,
                                        const char *value)
{
    return croft_orch_guest_store_put(runtime,
                                      txn,
                                      table,
                                      (const uint8_t *)key,
                                      croft_orch_guest_strlen(key),
                                      (const uint8_t *)value,
                                      croft_orch_guest_strlen(value));
}

int32_t croft_orch_guest_store_get_alloc(CroftOrchGuestRuntime *runtime,
                                         SapWitOrchestrationTxnResource txn,
                                         const char *table,
                                         const uint8_t *key,
                                         uint32_t key_len,
                                         uint8_t **value_out,
                                         uint32_t *value_len_out)
{
    SapWitOrchestrationStoreCommand command = {0};
    SapWitOrchestrationStoreReply reply;
    uint8_t *qualified = NULL;
    uint32_t qualified_len = 0u;
    uint8_t *copy = NULL;
    int32_t rc;

    if (!value_out || !value_len_out) {
        return ERR_INVALID;
    }
    *value_out = NULL;
    *value_len_out = 0u;

    rc = croft_orch_guest_store_qualified_key(table, key, key_len, &qualified, &qualified_len);
    if (rc != ERR_OK) {
        return rc;
    }
    sap_wit_zero_orchestration_store_reply(&reply);
    command.case_tag = SAP_WIT_ORCHESTRATION_STORE_COMMAND_GET;
    command.val.get.txn = txn;
    command.val.get.key_data = qualified;
    command.val.get.key_len = qualified_len;
    rc = sap_wit_guest_orchestration_worker_import_store(&runtime->transport, &command, &reply);
    sap_wit_rt_free(qualified);
    if (rc == ERR_OK) {
        if (reply.case_tag != SAP_WIT_ORCHESTRATION_STORE_REPLY_GET || !reply.val.get.is_v_ok) {
            rc = ERR_INVALID;
        } else if (!reply.val.get.v_val.ok.has_v) {
            rc = ERR_NOT_FOUND;
        }
    }
    if (rc == ERR_OK) {
        copy = (uint8_t *)sap_wit_rt_malloc(reply.val.get.v_val.ok.v_len);
        if (!copy && reply.val.get.v_val.ok.v_len > 0u) {
            rc = ERR_OOM;
        } else {
            if (reply.val.get.v_val.ok.v_len > 0u) {
                sap_wit_rt_memcpy(copy,
                                  reply.val.get.v_val.ok.v_data,
                                  reply.val.get.v_val.ok.v_len);
            }
            *value_out = copy;
            *value_len_out = reply.val.get.v_val.ok.v_len;
        }
    }
    sap_wit_dispose_orchestration_store_reply(&reply);
    return rc;
}

int32_t croft_orch_guest_mailbox_send(CroftOrchGuestRuntime *runtime,
                                      SapWitOrchestrationMailboxResource mailbox,
                                      const uint8_t *payload,
                                      uint32_t payload_len)
{
    SapWitOrchestrationMailboxCommand command = {0};
    SapWitOrchestrationMailboxReply reply;
    int32_t rc;

    sap_wit_zero_orchestration_mailbox_reply(&reply);
    command.case_tag = SAP_WIT_ORCHESTRATION_MAILBOX_COMMAND_SEND;
    command.val.send.mailbox = mailbox;
    command.val.send.payload_data = payload;
    command.val.send.payload_len = payload_len;
    rc = sap_wit_guest_orchestration_worker_import_mailbox(&runtime->transport, &command, &reply);
    if (rc == ERR_OK
            && (reply.case_tag != SAP_WIT_ORCHESTRATION_MAILBOX_REPLY_STATUS
                || !reply.val.status.is_v_ok)) {
        rc = ERR_INVALID;
    }
    sap_wit_dispose_orchestration_mailbox_reply(&reply);
    return rc;
}

int32_t croft_orch_guest_mailbox_send_cstr(CroftOrchGuestRuntime *runtime,
                                           SapWitOrchestrationMailboxResource mailbox,
                                           const char *text)
{
    return croft_orch_guest_mailbox_send(runtime,
                                         mailbox,
                                         (const uint8_t *)text,
                                         croft_orch_guest_strlen(text));
}

int32_t croft_orch_guest_mailbox_recv_alloc_ex(CroftOrchGuestRuntime *runtime,
                                               SapWitOrchestrationMailboxResource mailbox,
                                               uint8_t **payload_out,
                                               uint32_t *payload_len_out,
                                               const uint8_t **error_data_out,
                                               uint32_t *error_len_out,
                                               uint8_t *was_empty_out)
{
    SapWitOrchestrationMailboxCommand command = {0};
    SapWitOrchestrationMailboxReply reply;
    uint8_t *copy = NULL;
    int32_t rc;

    if (!payload_out || !payload_len_out) {
        return ERR_INVALID;
    }
    *payload_out = NULL;
    *payload_len_out = 0u;
    if (error_data_out) {
        *error_data_out = NULL;
    }
    if (error_len_out) {
        *error_len_out = 0u;
    }
    if (was_empty_out) {
        *was_empty_out = 0u;
    }

    sap_wit_zero_orchestration_mailbox_reply(&reply);
    command.case_tag = SAP_WIT_ORCHESTRATION_MAILBOX_COMMAND_RECV;
    command.val.recv.mailbox = mailbox;
    rc = sap_wit_guest_orchestration_worker_import_mailbox(&runtime->transport, &command, &reply);
    if (rc == ERR_OK) {
        if (reply.case_tag != SAP_WIT_ORCHESTRATION_MAILBOX_REPLY_RECV) {
            rc = ERR_INVALID;
        } else if (!reply.val.recv.is_v_ok) {
            if (error_data_out) {
                *error_data_out = reply.val.recv.v_val.err.v_data;
            }
            if (error_len_out) {
                *error_len_out = reply.val.recv.v_val.err.v_len;
            }
            rc = ERR_INVALID;
        } else if (!reply.val.recv.v_val.ok.has_v) {
            if (was_empty_out) {
                *was_empty_out = 1u;
            }
            rc = ERR_NOT_FOUND;
        }
    }
    if (rc == ERR_OK) {
        copy = (uint8_t *)sap_wit_rt_malloc(reply.val.recv.v_val.ok.v_len);
        if (!copy && reply.val.recv.v_val.ok.v_len > 0u) {
            rc = ERR_OOM;
        } else {
            if (reply.val.recv.v_val.ok.v_len > 0u) {
                sap_wit_rt_memcpy(copy,
                                  reply.val.recv.v_val.ok.v_data,
                                  reply.val.recv.v_val.ok.v_len);
            }
            *payload_out = copy;
            *payload_len_out = reply.val.recv.v_val.ok.v_len;
        }
    }
    sap_wit_dispose_orchestration_mailbox_reply(&reply);
    return rc;
}

int32_t croft_orch_guest_mailbox_recv_alloc(CroftOrchGuestRuntime *runtime,
                                            SapWitOrchestrationMailboxResource mailbox,
                                            uint8_t **payload_out,
                                            uint32_t *payload_len_out)
{
    return croft_orch_guest_mailbox_recv_alloc_ex(runtime,
                                                  mailbox,
                                                  payload_out,
                                                  payload_len_out,
                                                  NULL,
                                                  NULL,
                                                  NULL);
}

int32_t croft_orch_guest_mailbox_recv_retry_alloc(CroftOrchGuestRuntime *runtime,
                                                  SapWitOrchestrationMailboxResource mailbox,
                                                  uint32_t max_empty_polls,
                                                  uint8_t **payload_out,
                                                  uint32_t *payload_len_out)
{
    uint32_t polls = 0u;
    uint8_t was_empty = 0u;
    int32_t rc;

    do {
        rc = croft_orch_guest_mailbox_recv_alloc_ex(runtime,
                                                    mailbox,
                                                    payload_out,
                                                    payload_len_out,
                                                    NULL,
                                                    NULL,
                                                    &was_empty);
        if (rc != ERR_NOT_FOUND || !was_empty) {
            return rc;
        }
        polls++;
    } while (polls <= max_empty_polls);

    return ERR_NOT_FOUND;
}
