#include "croft/editor_document.h"
#include "croft/host_fs.h"

#include "sapling/arena.h"
#include "sapling/err.h"
#include "sapling/seq.h"
#include "sapling/text.h"
#include "sapling/txn.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    Text* text;
    uint64_t revision;
} croft_editor_document_snapshot;

typedef struct {
    croft_editor_document_snapshot* entries;
    size_t len;
    size_t cap;
} croft_editor_document_history;

typedef struct {
    int active;
    croft_editor_document_edit_kind kind;
    size_t start_offset;
    size_t deleted_count;
    size_t inserted_count;
} croft_editor_document_coalesce_state;

struct croft_editor_document {
    SapMemArena* arena;
    SapEnv* env;
    Text* text;
    char* path;
    int dirty;
    uint64_t current_revision;
    uint64_t clean_revision;
    croft_editor_document_history undo_history;
    croft_editor_document_history redo_history;
    croft_editor_document_coalesce_state coalesce;
};

typedef int32_t (*croft_editor_document_mutator_fn)(SapTxnCtx* txn,
                                                    croft_editor_document* document,
                                                    const void* ctx);

typedef struct {
    const uint8_t* utf8;
    size_t utf8_len;
} croft_editor_document_utf8_ctx;

typedef struct {
    size_t start_offset;
    size_t end_offset;
    uint32_t codepoint;
} croft_editor_document_replace_codepoint_ctx;

typedef struct {
    size_t start_offset;
    size_t end_offset;
} croft_editor_document_delete_ctx;

static char* croft_strdup(const char* text) {
    size_t len;
    char* copy;

    if (!text) {
        return NULL;
    }

    len = strlen(text);
    copy = (char*)malloc(len + 1u);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, text, len + 1u);
    return copy;
}

static void croft_editor_document_sync_dirty(croft_editor_document* document) {
    if (!document) {
        return;
    }

    document->dirty = document->current_revision != document->clean_revision;
}

static void croft_editor_document_snapshot_reset(croft_editor_document_snapshot* snapshot) {
    if (!snapshot) {
        return;
    }

    snapshot->text = NULL;
    snapshot->revision = 0u;
}

static void croft_editor_document_snapshot_dispose(croft_editor_document* document,
                                                   croft_editor_document_snapshot* snapshot) {
    if (!document || !snapshot || !snapshot->text) {
        return;
    }

    text_free(document->env, snapshot->text);
    croft_editor_document_snapshot_reset(snapshot);
}

static void croft_editor_document_history_dispose(croft_editor_document* document,
                                                  croft_editor_document_history* history) {
    size_t i;

    if (!document || !history) {
        return;
    }

    for (i = 0; i < history->len; i++) {
        croft_editor_document_snapshot_dispose(document, &history->entries[i]);
    }

    free(history->entries);
    history->entries = NULL;
    history->len = 0u;
    history->cap = 0u;
}

static int32_t croft_editor_document_history_reserve(croft_editor_document_history* history,
                                                     size_t needed) {
    croft_editor_document_snapshot* next_entries;
    size_t next_cap;

    if (!history) {
        return ERR_INVALID;
    }
    if (history->cap >= needed) {
        return ERR_OK;
    }

    next_cap = history->cap > 0u ? history->cap * 2u : 8u;
    while (next_cap < needed) {
        next_cap *= 2u;
    }

    next_entries = (croft_editor_document_snapshot*)realloc(
        history->entries,
        next_cap * sizeof(*next_entries));
    if (!next_entries) {
        return ERR_OOM;
    }

    history->entries = next_entries;
    history->cap = next_cap;
    return ERR_OK;
}

static int32_t croft_editor_document_history_push(croft_editor_document_history* history,
                                                  croft_editor_document_snapshot* snapshot) {
    int32_t rc;

    if (!history || !snapshot || !snapshot->text) {
        return ERR_INVALID;
    }

    rc = croft_editor_document_history_reserve(history, history->len + 1u);
    if (rc != ERR_OK) {
        return rc;
    }

    history->entries[history->len++] = *snapshot;
    croft_editor_document_snapshot_reset(snapshot);
    return ERR_OK;
}

static int croft_editor_document_history_pop(croft_editor_document_history* history,
                                             croft_editor_document_snapshot* out_snapshot) {
    if (!history || history->len == 0u || !out_snapshot) {
        return 0;
    }

    *out_snapshot = history->entries[history->len - 1u];
    history->len--;
    croft_editor_document_snapshot_reset(&history->entries[history->len]);
    return 1;
}

static void croft_editor_document_clear_redo(croft_editor_document* document) {
    if (!document) {
        return;
    }

    croft_editor_document_history_dispose(document, &document->redo_history);
}

static int32_t croft_editor_document_make_snapshot(croft_editor_document* document,
                                                   croft_editor_document_snapshot* out_snapshot) {
    Text* clone;

    if (!document || !document->text || !out_snapshot) {
        return ERR_INVALID;
    }

    clone = text_clone(document->env, document->text);
    if (!clone) {
        return ERR_OOM;
    }

    out_snapshot->text = clone;
    out_snapshot->revision = document->current_revision;
    return ERR_OK;
}

static int32_t croft_editor_document_restore_snapshot(croft_editor_document* document,
                                                      croft_editor_document_snapshot* snapshot) {
    Text* previous_text;

    if (!document || !snapshot || !snapshot->text) {
        return ERR_INVALID;
    }

    previous_text = document->text;
    document->text = snapshot->text;
    document->current_revision = snapshot->revision;
    croft_editor_document_snapshot_reset(snapshot);
    croft_editor_document_break_coalescing(document);
    croft_editor_document_sync_dirty(document);

    if (previous_text) {
        return text_free(document->env, previous_text);
    }
    return ERR_OK;
}

static int croft_editor_document_can_coalesce(const croft_editor_document* document,
                                              croft_editor_document_edit_kind kind,
                                              size_t start_offset,
                                              size_t deleted_count,
                                              size_t inserted_count) {
    const croft_editor_document_coalesce_state* state;

    if (!document || !document->coalesce.active) {
        return 0;
    }

    state = &document->coalesce;
    if (state->kind != kind) {
        return 0;
    }

    switch (kind) {
        case CROFT_EDITOR_EDIT_INSERT:
            if (deleted_count != 0u || state->deleted_count != 0u) {
                return 0;
            }
            return start_offset == state->start_offset + state->inserted_count;

        case CROFT_EDITOR_EDIT_DELETE_BACKWARD:
            if (inserted_count != 0u || state->inserted_count != 0u) {
                return 0;
            }
            return start_offset + deleted_count == state->start_offset;

        case CROFT_EDITOR_EDIT_DELETE_FORWARD:
            if (inserted_count != 0u || state->inserted_count != 0u) {
                return 0;
            }
            return start_offset == state->start_offset;

        case CROFT_EDITOR_EDIT_REPLACE_ALL:
        default:
            return 0;
    }
}

static void croft_editor_document_record_coalesce(croft_editor_document* document,
                                                  croft_editor_document_edit_kind kind,
                                                  size_t start_offset,
                                                  size_t deleted_count,
                                                  size_t inserted_count) {
    if (!document) {
        return;
    }

    if (kind == CROFT_EDITOR_EDIT_REPLACE_ALL) {
        croft_editor_document_break_coalescing(document);
        return;
    }

    document->coalesce.active = 1;
    document->coalesce.kind = kind;
    document->coalesce.start_offset = start_offset;
    document->coalesce.deleted_count = deleted_count;
    document->coalesce.inserted_count = inserted_count;
}

static int32_t croft_editor_document_apply_utf8_raw(SapTxnCtx* txn,
                                                    croft_editor_document* document,
                                                    const uint8_t* utf8,
                                                    size_t utf8_len) {
    int32_t rc;

    if (!txn || !document || !document->text) {
        return ERR_INVALID;
    }

    rc = text_reset(txn, document->text);
    if (rc == ERR_OK && utf8 && utf8_len > 0u) {
        rc = text_from_utf8(txn, document->text, utf8, utf8_len);
    }

    return rc;
}

static int32_t croft_editor_document_mutate_replace_utf8(SapTxnCtx* txn,
                                                         croft_editor_document* document,
                                                         const void* ctx) {
    const croft_editor_document_utf8_ctx* replace_ctx =
        (const croft_editor_document_utf8_ctx*)ctx;
    return croft_editor_document_apply_utf8_raw(txn,
                                                document,
                                                replace_ctx ? replace_ctx->utf8 : NULL,
                                                replace_ctx ? replace_ctx->utf8_len : 0u);
}

static int32_t croft_editor_document_mutate_replace_range_with_codepoint(
    SapTxnCtx* txn,
    croft_editor_document* document,
    const void* ctx) {
    const croft_editor_document_replace_codepoint_ctx* replace_ctx =
        (const croft_editor_document_replace_codepoint_ctx*)ctx;
    size_t delete_count;
    size_t i;
    int32_t rc;

    if (!txn || !document || !document->text || !replace_ctx) {
        return ERR_INVALID;
    }
    if (replace_ctx->start_offset > replace_ctx->end_offset
            || replace_ctx->end_offset > text_length(document->text)) {
        return ERR_RANGE;
    }

    delete_count = replace_ctx->end_offset - replace_ctx->start_offset;
    for (i = 0u; i < delete_count; i++) {
        rc = text_delete(txn, document->text, replace_ctx->start_offset, NULL);
        if (rc != ERR_OK) {
            return rc;
        }
    }

    return text_insert(txn,
                       document->text,
                       replace_ctx->start_offset,
                       replace_ctx->codepoint);
}

static int32_t croft_editor_document_mutate_delete_range(SapTxnCtx* txn,
                                                         croft_editor_document* document,
                                                         const void* ctx) {
    const croft_editor_document_delete_ctx* delete_ctx =
        (const croft_editor_document_delete_ctx*)ctx;
    size_t delete_count;
    size_t i;
    int32_t rc;

    if (!txn || !document || !document->text || !delete_ctx) {
        return ERR_INVALID;
    }
    if (delete_ctx->start_offset > delete_ctx->end_offset
            || delete_ctx->end_offset > text_length(document->text)) {
        return ERR_RANGE;
    }

    delete_count = delete_ctx->end_offset - delete_ctx->start_offset;
    for (i = 0u; i < delete_count; i++) {
        rc = text_delete(txn, document->text, delete_ctx->start_offset, NULL);
        if (rc != ERR_OK) {
            return rc;
        }
    }

    return ERR_OK;
}

static int32_t croft_editor_document_apply_edit(croft_editor_document* document,
                                                croft_editor_document_edit_kind kind,
                                                size_t start_offset,
                                                size_t deleted_count,
                                                size_t inserted_count,
                                                croft_editor_document_mutator_fn mutator,
                                                const void* ctx) {
    croft_editor_document_snapshot snapshot = {0};
    SapTxnCtx* txn;
    int32_t rc;
    int coalesced;

    if (!document || !mutator) {
        return ERR_INVALID;
    }

    coalesced = croft_editor_document_can_coalesce(document,
                                                   kind,
                                                   start_offset,
                                                   deleted_count,
                                                   inserted_count);
    if (!coalesced) {
        rc = croft_editor_document_make_snapshot(document, &snapshot);
        if (rc != ERR_OK) {
            return rc;
        }
    }

    txn = sap_txn_begin(document->env, NULL, 0);
    if (!txn) {
        croft_editor_document_snapshot_dispose(document, &snapshot);
        return ERR_OOM;
    }

    rc = mutator(txn, document, ctx);
    if (rc != ERR_OK) {
        sap_txn_abort(txn);
        croft_editor_document_snapshot_dispose(document, &snapshot);
        return rc;
    }

    rc = sap_txn_commit(txn);
    if (rc != ERR_OK) {
        croft_editor_document_snapshot_dispose(document, &snapshot);
        return rc;
    }

    if (!coalesced) {
        croft_editor_document_clear_redo(document);
        rc = croft_editor_document_history_push(&document->undo_history, &snapshot);
        if (rc != ERR_OK) {
            croft_editor_document_snapshot_dispose(document, &snapshot);
            return rc;
        }
    }

    document->current_revision++;
    croft_editor_document_record_coalesce(document,
                                          kind,
                                          start_offset,
                                          deleted_count,
                                          inserted_count);
    croft_editor_document_sync_dirty(document);
    return ERR_OK;
}

static int32_t croft_editor_document_read_file(const char* path,
                                               uint8_t** out_bytes,
                                               size_t* out_len) {
    uint64_t fd;
    uint64_t file_size;
    uint32_t total_read;
    uint8_t* buffer;
    int32_t rc;

    if (!path || !out_bytes || !out_len) {
        return ERR_INVALID;
    }

    rc = host_fs_open(path, (uint32_t)strlen(path), HOST_FS_O_RDONLY, &fd);
    if (rc != HOST_FS_OK) {
        return rc;
    }

    rc = host_fs_file_size(fd, &file_size);
    if (rc != HOST_FS_OK) {
        host_fs_close(fd);
        return rc;
    }

    buffer = (uint8_t*)malloc((size_t)file_size + 1u);
    if (!buffer) {
        host_fs_close(fd);
        return ERR_OOM;
    }

    total_read = 0u;
    while ((uint64_t)total_read < file_size) {
        uint32_t chunk_read = 0u;
        uint32_t request = (uint32_t)(file_size - (uint64_t)total_read);
        rc = host_fs_read(fd, buffer + total_read, request, &chunk_read);
        if (rc != HOST_FS_OK) {
            free(buffer);
            host_fs_close(fd);
            return rc;
        }
        if (chunk_read == 0u) {
            break;
        }
        total_read += chunk_read;
    }

    host_fs_close(fd);
    buffer[total_read] = '\0';
    *out_bytes = buffer;
    *out_len = (size_t)total_read;
    return ERR_OK;
}

static int32_t croft_editor_document_write_file(const char* path,
                                                const uint8_t* bytes,
                                                size_t len) {
    uint64_t fd;
    size_t total_written;
    int32_t rc;

    if (!path) {
        return ERR_INVALID;
    }

    rc = host_fs_open(path,
                      (uint32_t)strlen(path),
                      HOST_FS_O_WRONLY | HOST_FS_O_CREAT | HOST_FS_O_TRUNC,
                      &fd);
    if (rc != HOST_FS_OK) {
        return rc;
    }

    total_written = 0u;
    while (total_written < len) {
        uint32_t chunk_written = 0u;
        uint32_t request = (uint32_t)(len - total_written);
        rc = host_fs_write(fd, bytes + total_written, request, &chunk_written);
        if (rc != HOST_FS_OK) {
            host_fs_close(fd);
            return rc;
        }
        if (chunk_written == 0u) {
            host_fs_close(fd);
            return HOST_FS_ERR_IO;
        }
        total_written += chunk_written;
    }

    host_fs_close(fd);
    return ERR_OK;
}

croft_editor_document* croft_editor_document_create(const char* exe_path,
                                                    const char* file_path,
                                                    const uint8_t* fallback_utf8,
                                                    size_t fallback_len) {
    croft_editor_document* document;
    SapArenaOptions opts;
    uint8_t* loaded_bytes;
    size_t loaded_len;
    int32_t rc;
    int used_loaded_file = 0;

    document = (croft_editor_document*)calloc(1, sizeof(*document));
    if (!document) {
        return NULL;
    }

    host_fs_init(exe_path);

    memset(&opts, 0, sizeof(opts));
    opts.type = SAP_ARENA_BACKING_MALLOC;
    opts.page_size = 4096;

    rc = sap_arena_init(&document->arena, &opts);
    if (rc != ERR_OK) {
        free(document);
        return NULL;
    }

    document->env = sap_env_create(document->arena, 4096);
    if (!document->env) {
        sap_arena_destroy(document->arena);
        free(document);
        return NULL;
    }

    rc = sap_seq_subsystem_init(document->env);
    if (rc != ERR_OK) {
        sap_env_destroy(document->env);
        sap_arena_destroy(document->arena);
        free(document);
        return NULL;
    }

    document->text = text_new(document->env);
    if (!document->text) {
        sap_env_destroy(document->env);
        sap_arena_destroy(document->arena);
        free(document);
        return NULL;
    }

    if (file_path) {
        document->path = croft_strdup(file_path);
        if (!document->path) {
            croft_editor_document_destroy(document);
            return NULL;
        }
    }

    loaded_bytes = NULL;
    loaded_len = 0u;
    if (document->path) {
        rc = croft_editor_document_read_file(document->path, &loaded_bytes, &loaded_len);
        if (rc == ERR_OK || rc == HOST_FS_OK) {
            used_loaded_file = 1;
        }
    }

    if (used_loaded_file) {
        SapTxnCtx* txn = sap_txn_begin(document->env, NULL, 0);
        if (!txn) {
            free(loaded_bytes);
            croft_editor_document_destroy(document);
            return NULL;
        }
        rc = croft_editor_document_apply_utf8_raw(txn, document, loaded_bytes, loaded_len);
        free(loaded_bytes);
        if (rc != ERR_OK) {
            sap_txn_abort(txn);
            croft_editor_document_destroy(document);
            return NULL;
        }
        rc = sap_txn_commit(txn);
        if (rc != ERR_OK) {
            croft_editor_document_destroy(document);
            return NULL;
        }
    } else {
        SapTxnCtx* txn = sap_txn_begin(document->env, NULL, 0);
        if (!txn) {
            croft_editor_document_destroy(document);
            return NULL;
        }
        rc = croft_editor_document_apply_utf8_raw(txn, document, fallback_utf8, fallback_len);
        if (rc != ERR_OK) {
            sap_txn_abort(txn);
            croft_editor_document_destroy(document);
            return NULL;
        }
        rc = sap_txn_commit(txn);
        if (rc != ERR_OK) {
            croft_editor_document_destroy(document);
            return NULL;
        }
    }

    document->current_revision = 0u;
    document->clean_revision = 0u;
    croft_editor_document_break_coalescing(document);
    croft_editor_document_sync_dirty(document);
    return document;
}

void croft_editor_document_destroy(croft_editor_document* document) {
    if (!document) {
        return;
    }

    croft_editor_document_history_dispose(document, &document->undo_history);
    croft_editor_document_history_dispose(document, &document->redo_history);
    if (document->text) {
        text_free(document->env, document->text);
    }
    if (document->env) {
        sap_env_destroy(document->env);
    }
    if (document->arena) {
        sap_arena_destroy(document->arena);
    }
    free(document->path);
    free(document);
}

int32_t croft_editor_document_replace_utf8(croft_editor_document* document,
                                           const uint8_t* utf8,
                                           size_t utf8_len) {
    croft_editor_document_utf8_ctx ctx = {
        .utf8 = utf8,
        .utf8_len = utf8_len
    };

    if (!document) {
        return ERR_INVALID;
    }

    return croft_editor_document_apply_edit(document,
                                            CROFT_EDITOR_EDIT_REPLACE_ALL,
                                            0u,
                                            text_length(document->text),
                                            0u,
                                            croft_editor_document_mutate_replace_utf8,
                                            &ctx);
}

int32_t croft_editor_document_replace_range_with_codepoint(
    croft_editor_document* document,
    size_t start_offset,
    size_t end_offset,
    uint32_t codepoint,
    croft_editor_document_edit_kind edit_kind) {
    croft_editor_document_replace_codepoint_ctx ctx = {
        .start_offset = start_offset,
        .end_offset = end_offset,
        .codepoint = codepoint
    };

    return croft_editor_document_apply_edit(document,
                                            edit_kind,
                                            start_offset,
                                            end_offset >= start_offset ? (end_offset - start_offset) : 0u,
                                            1u,
                                            croft_editor_document_mutate_replace_range_with_codepoint,
                                            &ctx);
}

int32_t croft_editor_document_delete_range(croft_editor_document* document,
                                           size_t start_offset,
                                           size_t end_offset,
                                           croft_editor_document_edit_kind edit_kind) {
    croft_editor_document_delete_ctx ctx = {
        .start_offset = start_offset,
        .end_offset = end_offset
    };

    if (start_offset == end_offset) {
        return ERR_OK;
    }

    return croft_editor_document_apply_edit(document,
                                            edit_kind,
                                            start_offset,
                                            end_offset - start_offset,
                                            0u,
                                            croft_editor_document_mutate_delete_range,
                                            &ctx);
}

int32_t croft_editor_document_export_utf8(croft_editor_document* document,
                                          char** out_utf8,
                                          size_t* out_len) {
    size_t utf8_len;
    uint8_t* buffer;
    int rc;

    if (!document || !out_utf8 || !out_len) {
        return ERR_INVALID;
    }

    rc = text_utf8_length(document->text, &utf8_len);
    if (rc != ERR_OK) {
        return rc;
    }

    buffer = (uint8_t*)malloc(utf8_len + 1u);
    if (!buffer) {
        return ERR_OOM;
    }

    rc = text_to_utf8(document->text, buffer, utf8_len + 1u, &utf8_len);
    if (rc != ERR_OK) {
        free(buffer);
        return rc;
    }

    buffer[utf8_len] = '\0';
    *out_utf8 = (char*)buffer;
    *out_len = utf8_len;
    return ERR_OK;
}

int32_t croft_editor_document_save(croft_editor_document* document) {
    char* utf8 = NULL;
    size_t utf8_len = 0u;
    int32_t rc;

    if (!document || !document->path) {
        return ERR_INVALID;
    }

    rc = croft_editor_document_export_utf8(document, &utf8, &utf8_len);
    if (rc != ERR_OK) {
        return rc;
    }

    rc = croft_editor_document_write_file(document->path, (const uint8_t*)utf8, utf8_len);
    free(utf8);

    if (rc == ERR_OK) {
        document->clean_revision = document->current_revision;
        croft_editor_document_sync_dirty(document);
        croft_editor_document_break_coalescing(document);
    }

    return rc;
}

const char* croft_editor_document_path(const croft_editor_document* document) {
    if (!document) {
        return NULL;
    }
    return document->path;
}

int croft_editor_document_is_dirty(const croft_editor_document* document) {
    if (!document) {
        return 0;
    }
    return document->dirty;
}

int croft_editor_document_can_undo(const croft_editor_document* document) {
    if (!document) {
        return 0;
    }
    return document->undo_history.len > 0u;
}

int croft_editor_document_can_redo(const croft_editor_document* document) {
    if (!document) {
        return 0;
    }
    return document->redo_history.len > 0u;
}

void croft_editor_document_mark_clean(croft_editor_document* document) {
    if (!document) {
        return;
    }

    document->clean_revision = document->current_revision;
    croft_editor_document_sync_dirty(document);
}

void croft_editor_document_break_coalescing(croft_editor_document* document) {
    if (!document) {
        return;
    }

    memset(&document->coalesce, 0, sizeof(document->coalesce));
}

int32_t croft_editor_document_undo(croft_editor_document* document) {
    croft_editor_document_snapshot current_snapshot = {0};
    croft_editor_document_snapshot previous_snapshot = {0};
    int32_t rc;

    if (!document) {
        return ERR_INVALID;
    }
    if (!croft_editor_document_history_pop(&document->undo_history, &previous_snapshot)) {
        return ERR_RANGE;
    }

    rc = croft_editor_document_make_snapshot(document, &current_snapshot);
    if (rc != ERR_OK) {
        croft_editor_document_history_push(&document->undo_history, &previous_snapshot);
        return rc;
    }

    rc = croft_editor_document_restore_snapshot(document, &previous_snapshot);
    if (rc != ERR_OK) {
        croft_editor_document_snapshot_dispose(document, &previous_snapshot);
        croft_editor_document_snapshot_dispose(document, &current_snapshot);
        return rc;
    }

    rc = croft_editor_document_history_push(&document->redo_history, &current_snapshot);
    if (rc != ERR_OK) {
        croft_editor_document_snapshot_dispose(document, &current_snapshot);
        return rc;
    }

    return ERR_OK;
}

int32_t croft_editor_document_redo(croft_editor_document* document) {
    croft_editor_document_snapshot current_snapshot = {0};
    croft_editor_document_snapshot next_snapshot = {0};
    int32_t rc;

    if (!document) {
        return ERR_INVALID;
    }
    if (!croft_editor_document_history_pop(&document->redo_history, &next_snapshot)) {
        return ERR_RANGE;
    }

    rc = croft_editor_document_make_snapshot(document, &current_snapshot);
    if (rc != ERR_OK) {
        croft_editor_document_history_push(&document->redo_history, &next_snapshot);
        return rc;
    }

    rc = croft_editor_document_restore_snapshot(document, &next_snapshot);
    if (rc != ERR_OK) {
        croft_editor_document_snapshot_dispose(document, &next_snapshot);
        croft_editor_document_snapshot_dispose(document, &current_snapshot);
        return rc;
    }

    rc = croft_editor_document_history_push(&document->undo_history, &current_snapshot);
    if (rc != ERR_OK) {
        croft_editor_document_snapshot_dispose(document, &current_snapshot);
        return rc;
    }

    return ERR_OK;
}

SapEnv* croft_editor_document_env(croft_editor_document* document) {
    if (!document) {
        return NULL;
    }
    return document->env;
}

Text* croft_editor_document_text(croft_editor_document* document) {
    if (!document) {
        return NULL;
    }
    return document->text;
}
