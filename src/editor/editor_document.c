#include "croft/editor_document.h"
#include "croft/host_fs.h"

#include "sapling/arena.h"
#include "sapling/err.h"
#include "sapling/seq.h"
#include "sapling/text.h"
#include "sapling/txn.h"

#include <stdlib.h>
#include <string.h>

struct croft_editor_document {
    SapMemArena* arena;
    SapEnv* env;
    Text* text;
    char* path;
    int dirty;
};

static char* croft_strdup(const char* text) {
    size_t len;
    char* copy;

    if (!text) {
        return NULL;
    }

    len = strlen(text);
    copy = (char*)malloc(len + 1);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, text, len + 1);
    return copy;
}

static int32_t croft_editor_document_apply_utf8(croft_editor_document* document,
                                                const uint8_t* utf8,
                                                size_t utf8_len) {
    SapTxnCtx* txn;
    int rc;

    if (!document || !document->env || !document->text) {
        return ERR_INVALID;
    }

    txn = sap_txn_begin(document->env, NULL, 0);
    if (!txn) {
        return ERR_OOM;
    }

    rc = text_reset(txn, document->text);
    if (rc == ERR_OK && utf8 && utf8_len > 0) {
        rc = text_from_utf8(txn, document->text, utf8, utf8_len);
    }

    if (rc != ERR_OK) {
        sap_txn_abort(txn);
        return rc;
    }

    rc = sap_txn_commit(txn);
    if (rc != ERR_OK) {
        return rc;
    }

    document->dirty = 0;
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

    total_read = 0;
    while ((uint64_t)total_read < file_size) {
        uint32_t chunk_read = 0;
        uint32_t request = (uint32_t)(file_size - (uint64_t)total_read);
        rc = host_fs_read(fd, buffer + total_read, request, &chunk_read);
        if (rc != HOST_FS_OK) {
            free(buffer);
            host_fs_close(fd);
            return rc;
        }
        if (chunk_read == 0) {
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

    total_written = 0;
    while (total_written < len) {
        uint32_t chunk_written = 0;
        uint32_t request = (uint32_t)(len - total_written);
        rc = host_fs_write(fd, bytes + total_written, request, &chunk_written);
        if (rc != HOST_FS_OK) {
            host_fs_close(fd);
            return rc;
        }
        if (chunk_written == 0) {
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
    loaded_len = 0;
    if (document->path) {
        rc = croft_editor_document_read_file(document->path, &loaded_bytes, &loaded_len);
        if (rc == ERR_OK || rc == HOST_FS_OK) {
            rc = croft_editor_document_apply_utf8(document, loaded_bytes, loaded_len);
            free(loaded_bytes);
            if (rc != ERR_OK) {
                croft_editor_document_destroy(document);
                return NULL;
            }
            return document;
        }
    }

    rc = croft_editor_document_apply_utf8(document, fallback_utf8, fallback_len);
    if (rc != ERR_OK) {
        croft_editor_document_destroy(document);
        return NULL;
    }

    return document;
}

void croft_editor_document_destroy(croft_editor_document* document) {
    if (!document) {
        return;
    }

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
    int32_t rc = croft_editor_document_apply_utf8(document, utf8, utf8_len);
    if (rc == ERR_OK) {
        document->dirty = 1;
    }
    return rc;
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
    size_t utf8_len = 0;
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
        document->dirty = 0;
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

void croft_editor_document_mark_clean(croft_editor_document* document) {
    if (document) {
        document->dirty = 0;
    }
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
