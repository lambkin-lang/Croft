#ifndef CROFT_ORCHESTRATION_GUEST_RUNTIME_H
#define CROFT_ORCHESTRATION_GUEST_RUNTIME_H

#include "croft/wit_croft_wasm_guest.h"
#include "wit_orchestration.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CROFT_ORCH_GUEST_TEXT_CAP 96u
#define CROFT_ORCH_GUEST_TABLE_CAP 16u
#define CROFT_ORCH_GUEST_MAILBOX_CAP 16u

typedef struct {
    uint32_t initialized;
    SapWitCroftWasmGuestContext guest_ctx;
    SapWitGuestTransport transport;
} CroftOrchGuestRuntime;

typedef struct {
    const char *name;
    const char *key_format;
    const char *value_format;
    uint32_t access;
} CroftOrchGuestTableDecl;

typedef struct {
    const char *name;
    const char *message_format;
    const char *const *producers;
    uint32_t producer_count;
    const char *const *consumers;
    uint32_t consumer_count;
    uint8_t durability;
} CroftOrchGuestMailboxDecl;

typedef struct {
    const char *name;
    const char *module;
    uint32_t replicas;
    const char *const *allowed_tables;
    uint32_t allowed_table_count;
    const char *const *inboxes;
    uint32_t inbox_count;
    const char *const *outboxes;
    uint32_t outbox_count;
    const char *startup_format;
    const uint8_t *startup_bytes;
    uint32_t startup_bytes_len;
} CroftOrchGuestWorkerDecl;

typedef struct {
    char name[CROFT_ORCH_GUEST_TEXT_CAP];
    char key_format[CROFT_ORCH_GUEST_TEXT_CAP];
    char value_format[CROFT_ORCH_GUEST_TEXT_CAP];
    uint32_t access;
} CroftOrchGuestTableBinding;

typedef struct {
    char name[CROFT_ORCH_GUEST_TEXT_CAP];
    SapWitOrchestrationMailboxResource handle;
    char message_format[CROFT_ORCH_GUEST_TEXT_CAP];
    uint8_t durability;
} CroftOrchGuestMailboxBinding;

typedef struct {
    SapWitOrchestrationDbResource db_handle;
    CroftOrchGuestTableBinding tables[CROFT_ORCH_GUEST_TABLE_CAP];
    uint32_t table_count;
    CroftOrchGuestMailboxBinding inboxes[CROFT_ORCH_GUEST_MAILBOX_CAP];
    uint32_t inbox_count;
    CroftOrchGuestMailboxBinding outboxes[CROFT_ORCH_GUEST_MAILBOX_CAP];
    uint32_t outbox_count;
    uint8_t has_startup_format;
    char startup_format[CROFT_ORCH_GUEST_TEXT_CAP];
    const uint8_t *startup_bytes;
    uint32_t startup_bytes_len;
} CroftOrchGuestWorkerStartup;

int32_t croft_orch_guest_runtime_init(CroftOrchGuestRuntime *runtime);
void croft_orch_guest_runtime_dispose(CroftOrchGuestRuntime *runtime);
uint32_t croft_orch_guest_strlen(const char *text);
int croft_orch_guest_text_equals(const char *lhs, const char *rhs);
int croft_orch_guest_copy_text(char *dest, uint32_t cap, const uint8_t *data, uint32_t len);

int32_t croft_orch_guest_builder_create(CroftOrchGuestRuntime *runtime,
                                        const char *name,
                                        const char *family,
                                        const char *applicability,
                                        SapWitOrchestrationBuilderResource *builder_out);
int32_t croft_orch_guest_builder_require_bundle(CroftOrchGuestRuntime *runtime,
                                                SapWitOrchestrationBuilderResource builder,
                                                const char *bundle);
int32_t croft_orch_guest_builder_prefer_slot(CroftOrchGuestRuntime *runtime,
                                             SapWitOrchestrationBuilderResource builder,
                                             const char *slot,
                                             const char *bundle);
int32_t croft_orch_guest_builder_add_module(CroftOrchGuestRuntime *runtime,
                                            SapWitOrchestrationBuilderResource builder,
                                            const char *name,
                                            const char *path);
int32_t croft_orch_guest_builder_set_db_schema(CroftOrchGuestRuntime *runtime,
                                               SapWitOrchestrationBuilderResource builder,
                                               const char *name,
                                               const CroftOrchGuestTableDecl *tables,
                                               uint32_t table_count);
int32_t croft_orch_guest_builder_add_mailbox(CroftOrchGuestRuntime *runtime,
                                             SapWitOrchestrationBuilderResource builder,
                                             const CroftOrchGuestMailboxDecl *mailbox);
int32_t croft_orch_guest_builder_add_worker(CroftOrchGuestRuntime *runtime,
                                            SapWitOrchestrationBuilderResource builder,
                                            const CroftOrchGuestWorkerDecl *worker);
int32_t croft_orch_guest_builder_resolve(CroftOrchGuestRuntime *runtime,
                                         SapWitOrchestrationBuilderResource builder);
int32_t croft_orch_guest_builder_launch(CroftOrchGuestRuntime *runtime,
                                        SapWitOrchestrationBuilderResource builder,
                                        SapWitOrchestrationSessionResource *session_out);

int32_t croft_orch_guest_decode_worker_startup(const uint8_t *bytes,
                                               uint32_t len,
                                               CroftOrchGuestWorkerStartup *startup_out);
const CroftOrchGuestMailboxBinding *croft_orch_guest_find_inbox(
    const CroftOrchGuestWorkerStartup *startup,
    const char *name);
const CroftOrchGuestMailboxBinding *croft_orch_guest_find_outbox(
    const CroftOrchGuestWorkerStartup *startup,
    const char *name);
const CroftOrchGuestTableBinding *croft_orch_guest_find_table(
    const CroftOrchGuestWorkerStartup *startup,
    const char *name);

int32_t croft_orch_guest_store_begin(CroftOrchGuestRuntime *runtime,
                                     SapWitOrchestrationDbResource db,
                                     uint8_t read_only,
                                     SapWitOrchestrationTxnResource *txn_out);
int32_t croft_orch_guest_store_commit(CroftOrchGuestRuntime *runtime,
                                      SapWitOrchestrationTxnResource txn);
int32_t croft_orch_guest_store_abort(CroftOrchGuestRuntime *runtime,
                                     SapWitOrchestrationTxnResource txn);
int32_t croft_orch_guest_store_put(CroftOrchGuestRuntime *runtime,
                                   SapWitOrchestrationTxnResource txn,
                                   const char *table,
                                   const uint8_t *key,
                                   uint32_t key_len,
                                   const uint8_t *value,
                                   uint32_t value_len);
int32_t croft_orch_guest_store_get_alloc(CroftOrchGuestRuntime *runtime,
                                         SapWitOrchestrationTxnResource txn,
                                         const char *table,
                                         const uint8_t *key,
                                         uint32_t key_len,
                                         uint8_t **value_out,
                                         uint32_t *value_len_out);
int32_t croft_orch_guest_store_put_cstr(CroftOrchGuestRuntime *runtime,
                                        SapWitOrchestrationTxnResource txn,
                                        const char *table,
                                        const char *key,
                                        const char *value);

int32_t croft_orch_guest_mailbox_send(CroftOrchGuestRuntime *runtime,
                                      SapWitOrchestrationMailboxResource mailbox,
                                      const uint8_t *payload,
                                      uint32_t payload_len);
int32_t croft_orch_guest_mailbox_send_cstr(CroftOrchGuestRuntime *runtime,
                                           SapWitOrchestrationMailboxResource mailbox,
                                           const char *text);
int32_t croft_orch_guest_mailbox_recv_alloc_ex(CroftOrchGuestRuntime *runtime,
                                               SapWitOrchestrationMailboxResource mailbox,
                                               uint8_t **payload_out,
                                               uint32_t *payload_len_out,
                                               const uint8_t **error_data_out,
                                               uint32_t *error_len_out,
                                               uint8_t *was_empty_out);
int32_t croft_orch_guest_mailbox_recv_alloc(CroftOrchGuestRuntime *runtime,
                                            SapWitOrchestrationMailboxResource mailbox,
                                            uint8_t **payload_out,
                                            uint32_t *payload_len_out);
int32_t croft_orch_guest_mailbox_recv_retry_alloc(CroftOrchGuestRuntime *runtime,
                                                  SapWitOrchestrationMailboxResource mailbox,
                                                  uint32_t max_empty_polls,
                                                  uint8_t **payload_out,
                                                  uint32_t *payload_len_out);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_ORCHESTRATION_GUEST_RUNTIME_H */
