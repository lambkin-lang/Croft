#include "croft/editor_document_fs.h"
#include "croft/host_fs.h"

#include "sapling/err.h"

#include <stdlib.h>
#include <string.h>

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

croft_editor_document* croft_editor_document_open(const char* exe_path,
                                                  const char* file_path,
                                                  const uint8_t* fallback_utf8,
                                                  size_t fallback_len) {
    croft_editor_document* document;
    uint8_t* loaded_bytes = NULL;
    size_t loaded_len = 0u;
    const uint8_t* initial_bytes = fallback_utf8;
    size_t initial_len = fallback_len;
    int32_t rc;

    host_fs_init(exe_path);

    if (file_path) {
        rc = croft_editor_document_read_file(file_path, &loaded_bytes, &loaded_len);
        if (rc == ERR_OK || rc == HOST_FS_OK) {
            initial_bytes = loaded_bytes;
            initial_len = loaded_len;
        }
    }

    document = croft_editor_document_create(initial_bytes, initial_len);
    free(loaded_bytes);
    if (!document) {
        return NULL;
    }

    if (file_path) {
        rc = croft_editor_document_set_path(document, file_path);
        if (rc != ERR_OK) {
            croft_editor_document_destroy(document);
            return NULL;
        }
    }

    return document;
}

int32_t croft_editor_document_save(croft_editor_document* document) {
    return croft_editor_document_save_as(document,
                                         document ? croft_editor_document_path(document) : NULL);
}

int32_t croft_editor_document_save_as(croft_editor_document* document,
                                      const char* path) {
    char* utf8 = NULL;
    size_t utf8_len = 0u;
    int32_t rc;

    if (!document || !path) {
        return ERR_INVALID;
    }

    rc = croft_editor_document_export_utf8(document, &utf8, &utf8_len);
    if (rc != ERR_OK) {
        return rc;
    }

    rc = croft_editor_document_write_file(path, (const uint8_t*)utf8, utf8_len);
    free(utf8);

    if (rc == ERR_OK) {
        rc = croft_editor_document_set_path(document, path);
        if (rc != ERR_OK) {
            return rc;
        }
        croft_editor_document_mark_clean(document);
        croft_editor_document_break_coalescing(document);
    }

    return rc;
}
