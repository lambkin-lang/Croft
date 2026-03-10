#include "croft/wit_host_fs_runtime.h"

#include "croft/host_fs.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t live;
    uint64_t fd;
} croft_wit_host_file_slot;

struct croft_wit_host_fs_runtime {
    croft_wit_host_file_slot* slots;
    size_t slot_count;
    size_t slot_cap;
};

static void croft_wit_set_string_view(const char* text,
                                      const uint8_t** data_out,
                                      uint32_t* len_out)
{
    if (!data_out || !len_out) {
        return;
    }
    if (!text) {
        text = "";
    }
    *data_out = (const uint8_t*)text;
    *len_out = (uint32_t)strlen(text);
}

static const char* croft_wit_host_fs_error_from_rc(int32_t rc)
{
    switch (rc) {
        case HOST_FS_ERR_NOT_FOUND:
            return "not-found";
        case HOST_FS_ERR_ACCES:
            return "access";
        case HOST_FS_ERR_IO:
            return "io";
        case HOST_FS_ERR_INVALID:
            return "invalid";
        default:
            return "internal";
    }
}

static void croft_wit_host_fs_reply_zero(SapWitHostFsReply* reply)
{
    sap_wit_zero_host_fs_reply(reply);
}

static void croft_wit_host_fs_reply_file_ok(SapWitHostFsReply* reply, SapWitHostFsFileResource handle)
{
    croft_wit_host_fs_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_FS_REPLY_FILE;
    reply->val.file.is_v_ok = 1u;
    reply->val.file.v_val.ok.v = handle;
}

static void croft_wit_host_fs_reply_file_err(SapWitHostFsReply* reply, const char* err)
{
    croft_wit_host_fs_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_FS_REPLY_FILE;
    reply->val.file.is_v_ok = 0u;
    croft_wit_set_string_view(err, &reply->val.file.v_val.err.v_data, &reply->val.file.v_val.err.v_len);
}

static void croft_wit_host_fs_reply_status_ok(SapWitHostFsReply* reply)
{
    croft_wit_host_fs_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_FS_REPLY_STATUS;
    reply->val.status.is_v_ok = 1u;
}

static void croft_wit_host_fs_reply_status_err(SapWitHostFsReply* reply, const char* err)
{
    croft_wit_host_fs_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_FS_REPLY_STATUS;
    reply->val.status.is_v_ok = 0u;
    croft_wit_set_string_view(err,
                              &reply->val.status.v_val.err.v_data,
                              &reply->val.status.v_val.err.v_len);
}

static void croft_wit_host_fs_reply_read_ok(SapWitHostFsReply* reply, uint8_t* data, uint32_t len)
{
    croft_wit_host_fs_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_FS_REPLY_READ;
    reply->val.read.is_v_ok = 1u;
    reply->val.read.v_val.ok.v_data = data;
    reply->val.read.v_val.ok.v_len = len;
}

static void croft_wit_host_fs_reply_read_err(SapWitHostFsReply* reply, const char* err)
{
    croft_wit_host_fs_reply_zero(reply);
    reply->case_tag = SAP_WIT_HOST_FS_REPLY_READ;
    reply->val.read.is_v_ok = 0u;
    croft_wit_set_string_view(err, &reply->val.read.v_val.err.v_data, &reply->val.read.v_val.err.v_len);
}

void croft_wit_host_fs_reply_dispose(SapWitHostFsReply* reply)
{
    sap_wit_dispose_host_fs_reply(reply);
}

static int32_t croft_wit_host_fs_slots_reserve(croft_wit_host_fs_runtime* runtime, size_t needed)
{
    croft_wit_host_file_slot* next_slots;
    size_t next_cap;

    if (!runtime) {
        return HOST_FS_ERR_INVALID;
    }
    if (runtime->slot_cap >= needed) {
        return HOST_FS_OK;
    }

    next_cap = runtime->slot_cap > 0u ? runtime->slot_cap * 2u : 4u;
    while (next_cap < needed) {
        next_cap *= 2u;
    }

    next_slots = (croft_wit_host_file_slot*)realloc(runtime->slots, next_cap * sizeof(*next_slots));
    if (!next_slots) {
        return HOST_FS_ERR_IO;
    }

    memset(next_slots + runtime->slot_cap, 0,
           (next_cap - runtime->slot_cap) * sizeof(*next_slots));
    runtime->slots = next_slots;
    runtime->slot_cap = next_cap;
    return HOST_FS_OK;
}

static int32_t croft_wit_host_fs_slots_insert(croft_wit_host_fs_runtime* runtime,
                                              uint64_t fd,
                                              SapWitHostFsFileResource* handle_out)
{
    size_t i;
    int32_t rc;

    if (!runtime || !handle_out || fd == 0u) {
        return HOST_FS_ERR_INVALID;
    }

    for (i = 0u; i < runtime->slot_count; i++) {
        if (!runtime->slots[i].live) {
            runtime->slots[i].live = 1u;
            runtime->slots[i].fd = fd;
            *handle_out = (SapWitHostFsFileResource)(i + 1u);
            return HOST_FS_OK;
        }
    }

    rc = croft_wit_host_fs_slots_reserve(runtime, runtime->slot_count + 1u);
    if (rc != HOST_FS_OK) {
        return rc;
    }

    runtime->slots[runtime->slot_count].live = 1u;
    runtime->slots[runtime->slot_count].fd = fd;
    runtime->slot_count++;
    *handle_out = (SapWitHostFsFileResource)runtime->slot_count;
    return HOST_FS_OK;
}

static croft_wit_host_file_slot* croft_wit_host_fs_slots_lookup(croft_wit_host_fs_runtime* runtime,
                                                                SapWitHostFsFileResource handle)
{
    size_t slot;

    if (!runtime || handle == SAP_WIT_HOST_FS_FILE_RESOURCE_INVALID) {
        return NULL;
    }

    slot = (size_t)handle - 1u;
    if (slot >= runtime->slot_count || !runtime->slots[slot].live) {
        return NULL;
    }

    return &runtime->slots[slot];
}

croft_wit_host_fs_runtime* croft_wit_host_fs_runtime_create(void)
{
    return (croft_wit_host_fs_runtime*)calloc(1u, sizeof(croft_wit_host_fs_runtime));
}

void croft_wit_host_fs_runtime_destroy(croft_wit_host_fs_runtime* runtime)
{
    size_t i;

    if (!runtime) {
        return;
    }

    for (i = 0u; i < runtime->slot_count; i++) {
        if (runtime->slots[i].live) {
            host_fs_close(runtime->slots[i].fd);
        }
    }

    free(runtime->slots);
    free(runtime);
}

static uint32_t croft_wit_host_fs_flags_from_open(const SapWitHostFsFileOpen* request)
{
    uint32_t flags = 0u;

    if (!request) {
        return flags;
    }

    if (request->write) {
        flags |= HOST_FS_O_WRONLY;
    } else {
        flags |= HOST_FS_O_RDONLY;
    }
    if (request->create) {
        flags |= HOST_FS_O_CREAT;
    }
    if (request->truncate) {
        flags |= HOST_FS_O_TRUNC;
    }
    if (request->append) {
        flags |= HOST_FS_O_APPEND;
    }
    return flags;
}

static int32_t croft_wit_host_fs_dispatch_open(croft_wit_host_fs_runtime* runtime,
                                               const SapWitHostFsFileOpen* request,
                                               SapWitHostFsReply* reply_out)
{
    uint64_t fd = 0u;
    SapWitHostFsFileResource handle = SAP_WIT_HOST_FS_FILE_RESOURCE_INVALID;
    int32_t rc;

    if (!runtime || !request || !reply_out) {
        return HOST_FS_ERR_INVALID;
    }

    rc = host_fs_open((const char*)request->path_data,
                      request->path_len,
                      croft_wit_host_fs_flags_from_open(request),
                      &fd);
    if (rc != HOST_FS_OK) {
        croft_wit_host_fs_reply_file_err(reply_out, croft_wit_host_fs_error_from_rc(rc));
        return HOST_FS_OK;
    }

    rc = croft_wit_host_fs_slots_insert(runtime, fd, &handle);
    if (rc != HOST_FS_OK) {
        host_fs_close(fd);
        croft_wit_host_fs_reply_file_err(reply_out, "oom");
        return HOST_FS_OK;
    }

    croft_wit_host_fs_reply_file_ok(reply_out, handle);
    return HOST_FS_OK;
}

static int32_t croft_wit_host_fs_dispatch_close(croft_wit_host_fs_runtime* runtime,
                                                const SapWitHostFsFileClose* request,
                                                SapWitHostFsReply* reply_out)
{
    croft_wit_host_file_slot* slot;
    int32_t rc;

    if (!runtime || !request || !reply_out) {
        return HOST_FS_ERR_INVALID;
    }

    slot = croft_wit_host_fs_slots_lookup(runtime, request->file);
    if (!slot) {
        croft_wit_host_fs_reply_status_err(reply_out, "invalid-handle");
        return HOST_FS_OK;
    }

    rc = host_fs_close(slot->fd);
    if (rc != HOST_FS_OK) {
        croft_wit_host_fs_reply_status_err(reply_out, croft_wit_host_fs_error_from_rc(rc));
        return HOST_FS_OK;
    }

    slot->live = 0u;
    slot->fd = 0u;
    croft_wit_host_fs_reply_status_ok(reply_out);
    return HOST_FS_OK;
}

/*
 * This first host-fs mix-in is intentionally minimal and one-shot. The current
 * `host_fs` substrate does not expose seek/reset, so repeated read-all on the
 * same handle is not yet modeled as a stable capability.
 */
static int32_t croft_wit_host_fs_dispatch_read_all(croft_wit_host_fs_runtime* runtime,
                                                   const SapWitHostFsFileReadAll* request,
                                                   SapWitHostFsReply* reply_out)
{
    croft_wit_host_file_slot* slot;
    uint64_t file_size = 0u;
    uint8_t* buffer = NULL;
    uint32_t total_read = 0u;
    uint32_t chunk_read = 0u;
    int32_t rc;
    size_t alloc_len;

    if (!runtime || !request || !reply_out) {
        return HOST_FS_ERR_INVALID;
    }

    slot = croft_wit_host_fs_slots_lookup(runtime, request->file);
    if (!slot) {
        croft_wit_host_fs_reply_read_err(reply_out, "invalid-handle");
        return HOST_FS_OK;
    }

    rc = host_fs_file_size(slot->fd, &file_size);
    if (rc != HOST_FS_OK) {
        croft_wit_host_fs_reply_read_err(reply_out, croft_wit_host_fs_error_from_rc(rc));
        return HOST_FS_OK;
    }
    if (file_size > (uint64_t)UINT_MAX) {
        croft_wit_host_fs_reply_read_err(reply_out, "too-large");
        return HOST_FS_OK;
    }

    alloc_len = file_size > 0u ? (size_t)file_size : 1u;
    buffer = (uint8_t*)malloc(alloc_len);
    if (!buffer) {
        croft_wit_host_fs_reply_read_err(reply_out, "oom");
        return HOST_FS_OK;
    }

    while (total_read < (uint32_t)file_size) {
        rc = host_fs_read(slot->fd,
                          buffer + total_read,
                          (uint32_t)file_size - total_read,
                          &chunk_read);
        if (rc != HOST_FS_OK) {
            free(buffer);
            croft_wit_host_fs_reply_read_err(reply_out, croft_wit_host_fs_error_from_rc(rc));
            return HOST_FS_OK;
        }
        if (chunk_read == 0u) {
            break;
        }
        total_read += chunk_read;
    }

    croft_wit_host_fs_reply_read_ok(reply_out, buffer, total_read);
    return HOST_FS_OK;
}

int32_t croft_wit_host_fs_runtime_dispatch(croft_wit_host_fs_runtime* runtime,
                                           const SapWitHostFsCommand* command,
                                           SapWitHostFsReply* reply_out)
{
    if (!runtime || !command || !reply_out) {
        return HOST_FS_ERR_INVALID;
    }

    switch (command->case_tag) {
        case SAP_WIT_HOST_FS_COMMAND_OPEN:
            return croft_wit_host_fs_dispatch_open(runtime, &command->val.open, reply_out);
        case SAP_WIT_HOST_FS_COMMAND_CLOSE:
            return croft_wit_host_fs_dispatch_close(runtime, &command->val.close, reply_out);
        case SAP_WIT_HOST_FS_COMMAND_READ_ALL:
            return croft_wit_host_fs_dispatch_read_all(runtime, &command->val.read_all, reply_out);
        default:
            return HOST_FS_ERR_INVALID;
    }
}
