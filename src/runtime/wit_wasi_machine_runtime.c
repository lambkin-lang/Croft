#include "croft/wit_wasi_machine_runtime.h"

#include "croft/platform.h"

#include "sapling/err.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(CROFT_OS_WINDOWS)
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

#if defined(CROFT_OS_LINUX)
#include <sys/random.h>
#include <unistd.h>
#elif defined(CROFT_OS_MACOS)
#include <stdlib.h>
#include <unistd.h>
#endif

typedef struct {
    uint8_t* data;
    uint32_t len;
    uint32_t cap;
} croft_wit_wasi_byte_buffer;

typedef struct {
    uint8_t live;
    uint32_t handle;
    uint64_t ready_at_ns;
} croft_wit_wasi_pollable_slot;

typedef struct {
    uint8_t live;
    int fd;
    uint32_t descriptor_flags;
    uint8_t is_directory;
} croft_wit_wasi_descriptor_slot;

typedef struct {
    uint8_t live;
    DIR* dir;
} croft_wit_wasi_directory_stream_slot;

typedef struct {
    uint32_t descriptor_handle;
    char* guest_path;
} croft_wit_wasi_preopen_slot;

struct croft_wit_wasi_machine_runtime {
    croft_wit_wasi_byte_buffer env_blob;
    croft_wit_wasi_byte_buffer argv_blob;
    croft_wit_wasi_byte_buffer random_blob;
    croft_wit_wasi_byte_buffer poll_indexes_blob;
    croft_wit_wasi_byte_buffer filesystem_blob;
    croft_wit_wasi_byte_buffer preopen_blob;
    char* initial_cwd;
    char timezone_name[64];
    uint32_t env_count;
    uint32_t argc;
    croft_wit_wasi_pollable_slot* pollables;
    size_t pollable_count;
    size_t pollable_cap;
    croft_wit_wasi_descriptor_slot* descriptors;
    size_t descriptor_count;
    size_t descriptor_cap;
    croft_wit_wasi_directory_stream_slot* directory_streams;
    size_t directory_stream_count;
    size_t directory_stream_cap;
    croft_wit_wasi_preopen_slot* preopens;
    size_t preopen_count;
    size_t preopen_cap;
    uint32_t next_pollable_handle;
};

#if !defined(CROFT_OS_WINDOWS)
extern char** environ;

#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#ifndef AT_REMOVEDIR
#define AT_REMOVEDIR 0
#endif

#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0
#endif
#endif

static int croft_wit_wasi_buffer_reserve(croft_wit_wasi_byte_buffer* buffer,
                                         uint32_t needed)
{
    uint32_t required;
    uint32_t next_cap;
    uint8_t* next_data;

    if (!buffer) {
        return ERR_INVALID;
    }
    required = buffer->len + needed;
    if (required <= buffer->cap) {
        return ERR_OK;
    }

    next_cap = buffer->cap ? buffer->cap : 64u;
    while (next_cap < required) {
        if (next_cap > (UINT32_MAX / 2u)) {
            next_cap = required;
            break;
        }
        next_cap *= 2u;
    }

    next_data = (uint8_t*)realloc(buffer->data, next_cap);
    if (!next_data) {
        return ERR_OOM;
    }

    buffer->data = next_data;
    buffer->cap = next_cap;
    return ERR_OK;
}

static int croft_wit_wasi_buffer_append(croft_wit_wasi_byte_buffer* buffer,
                                        const void* data,
                                        uint32_t len)
{
    int rc;

    if (!buffer) {
        return ERR_INVALID;
    }
    if (len == 0u) {
        return ERR_OK;
    }
    if (!data) {
        return ERR_INVALID;
    }

    rc = croft_wit_wasi_buffer_reserve(buffer, len);
    if (rc != ERR_OK) {
        return rc;
    }

    memcpy(buffer->data + buffer->len, data, len);
    buffer->len += len;
    return ERR_OK;
}

static int croft_wit_wasi_buffer_append_tag(croft_wit_wasi_byte_buffer* buffer, uint8_t tag)
{
    return croft_wit_wasi_buffer_append(buffer, &tag, 1u);
}

static int croft_wit_wasi_buffer_append_u32(croft_wit_wasi_byte_buffer* buffer, uint32_t value)
{
    return croft_wit_wasi_buffer_append(buffer, &value, (uint32_t)sizeof(value));
}

static int croft_wit_wasi_buffer_begin_skip(croft_wit_wasi_byte_buffer* buffer,
                                            uint32_t* offset_out)
{
    uint32_t placeholder = 0u;

    if (!buffer || !offset_out) {
        return ERR_INVALID;
    }
    *offset_out = buffer->len;
    return croft_wit_wasi_buffer_append_u32(buffer, placeholder);
}

static int croft_wit_wasi_buffer_commit_skip(croft_wit_wasi_byte_buffer* buffer,
                                             uint32_t offset)
{
    uint32_t skip_len;

    if (!buffer || offset > buffer->len || (buffer->len - offset) < sizeof(uint32_t)) {
        return ERR_RANGE;
    }

    skip_len = buffer->len - offset - (uint32_t)sizeof(uint32_t);
    memcpy(buffer->data + offset, &skip_len, sizeof(skip_len));
    return ERR_OK;
}

static int croft_wit_wasi_buffer_encode_string(croft_wit_wasi_byte_buffer* buffer,
                                               const char* text,
                                               uint32_t text_len)
{
    int rc;

    rc = croft_wit_wasi_buffer_append_tag(buffer, SAP_WIT_TAG_STRING);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_wit_wasi_buffer_append_u32(buffer, text_len);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_wit_wasi_buffer_append(buffer, text, text_len);
}

static int croft_wit_wasi_buffer_encode_string_pair(croft_wit_wasi_byte_buffer* buffer,
                                                    const char* key,
                                                    uint32_t key_len,
                                                    const char* value,
                                                    uint32_t value_len)
{
    uint32_t skip_offset = 0u;
    int rc;

    rc = croft_wit_wasi_buffer_append_tag(buffer, SAP_WIT_TAG_TUPLE);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_wit_wasi_buffer_begin_skip(buffer, &skip_offset);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_wit_wasi_buffer_encode_string(buffer, key, key_len);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_wit_wasi_buffer_encode_string(buffer, value, value_len);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_wit_wasi_buffer_commit_skip(buffer, skip_offset);
}

static int croft_wit_wasi_buffer_encode_u32(croft_wit_wasi_byte_buffer* buffer, uint32_t value)
{
    int rc;

    rc = croft_wit_wasi_buffer_append_tag(buffer, SAP_WIT_TAG_U32);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_wit_wasi_buffer_append_u32(buffer, value);
}

static int croft_wit_wasi_buffer_encode_resource(croft_wit_wasi_byte_buffer* buffer,
                                                 uint32_t handle)
{
    int rc;

    rc = croft_wit_wasi_buffer_append_tag(buffer, SAP_WIT_TAG_RESOURCE);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_wit_wasi_buffer_append_u32(buffer, handle);
}

static int croft_wit_wasi_buffer_encode_resource_string_pair(croft_wit_wasi_byte_buffer* buffer,
                                                             uint32_t handle,
                                                             const char* text,
                                                             uint32_t text_len)
{
    uint32_t skip_offset = 0u;
    int rc;

    rc = croft_wit_wasi_buffer_append_tag(buffer, SAP_WIT_TAG_TUPLE);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_wit_wasi_buffer_begin_skip(buffer, &skip_offset);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_wit_wasi_buffer_encode_resource(buffer, handle);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_wit_wasi_buffer_encode_string(buffer, text, text_len);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_wit_wasi_buffer_commit_skip(buffer, skip_offset);
}

static char* croft_wit_wasi_copy_string(const char* text)
{
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

static uint32_t croft_wit_wasi_count_strings(const char* const* values, uint32_t known_count)
{
    uint32_t count = 0u;

    if (!values) {
        return 0u;
    }
    if (known_count > 0u) {
        return known_count;
    }
    while (values[count]) {
        count++;
    }
    return count;
}

static int croft_wit_wasi_build_argv_blob(croft_wit_wasi_machine_runtime* runtime,
                                          const char* const* argv,
                                          uint32_t argc)
{
    uint32_t i;

    if (!runtime) {
        return ERR_INVALID;
    }
    runtime->argv_blob.len = 0u;
    runtime->argc = 0u;
    argc = croft_wit_wasi_count_strings(argv, argc);
    if (argc > 0u && !argv) {
        return ERR_INVALID;
    }

    for (i = 0u; i < argc; i++) {
        uint32_t text_len;
        const char* text = argv[i] ? argv[i] : "";
        int rc;

        if (strlen(text) > UINT32_MAX) {
            return ERR_RANGE;
        }
        text_len = (uint32_t)strlen(text);
        rc = croft_wit_wasi_buffer_encode_string(&runtime->argv_blob, text, text_len);
        if (rc != ERR_OK) {
            return rc;
        }
    }

    runtime->argc = argc;
    return ERR_OK;
}

static int croft_wit_wasi_build_env_blob(croft_wit_wasi_machine_runtime* runtime,
                                         const char* const* envp,
                                         uint32_t envc)
{
    uint32_t i;

    if (!runtime) {
        return ERR_INVALID;
    }
    runtime->env_blob.len = 0u;
    runtime->env_count = 0u;
    envc = croft_wit_wasi_count_strings(envp, envc);
    if (envc > 0u && !envp) {
        return ERR_INVALID;
    }

    for (i = 0u; i < envc; i++) {
        const char* entry = envp[i] ? envp[i] : "";
        const char* equals = strchr(entry, '=');
        uint32_t key_len;
        uint32_t value_len;
        const char* value;
        int rc;

        if (equals) {
            key_len = (uint32_t)(equals - entry);
            value = equals + 1;
            if (strlen(value) > UINT32_MAX) {
                return ERR_RANGE;
            }
            value_len = (uint32_t)strlen(value);
        } else {
            if (strlen(entry) > UINT32_MAX) {
                return ERR_RANGE;
            }
            key_len = (uint32_t)strlen(entry);
            value = "";
            value_len = 0u;
        }

        rc = croft_wit_wasi_buffer_encode_string_pair(&runtime->env_blob,
                                                      entry,
                                                      key_len,
                                                      value,
                                                      value_len);
        if (rc != ERR_OK) {
            return rc;
        }
    }

    runtime->env_count = envc;
    return ERR_OK;
}

static int croft_wit_wasi_init_initial_cwd(croft_wit_wasi_machine_runtime* runtime,
                                           const char* configured)
{
    char path[PATH_MAX];
    char* copy = NULL;

    if (!runtime) {
        return ERR_INVALID;
    }

    free(runtime->initial_cwd);
    runtime->initial_cwd = NULL;

    if (configured) {
        copy = croft_wit_wasi_copy_string(configured);
        if (!copy) {
            return ERR_OOM;
        }
        runtime->initial_cwd = copy;
        return ERR_OK;
    }

    if (!getcwd(path, sizeof(path))) {
        return ERR_OK;
    }

    copy = croft_wit_wasi_copy_string(path);
    if (!copy) {
        return ERR_OOM;
    }
    runtime->initial_cwd = copy;
    return ERR_OK;
}

static int croft_wit_wasi_random_fill(void* data, uint32_t len)
{
#if defined(CROFT_OS_MACOS)
    arc4random_buf(data, len);
    return ERR_OK;
#elif defined(CROFT_OS_LINUX)
    uint8_t* bytes = (uint8_t*)data;
    size_t remaining = len;

    while (remaining > 0u) {
        ssize_t wrote = getrandom(bytes, remaining, 0);
        if (wrote <= 0) {
            return ERR_INVALID;
        }
        bytes += wrote;
        remaining -= (size_t)wrote;
    }
    return ERR_OK;
#else
    (void)data;
    (void)len;
    return ERR_UNSUPPORTED;
#endif
}

static int croft_wit_wasi_clock_get_ns(clockid_t clock_id, uint64_t* out)
{
    struct timespec ts;

    if (!out) {
        return ERR_INVALID;
    }
    if (clock_gettime(clock_id, &ts) != 0) {
        return ERR_INVALID;
    }
    *out = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    return ERR_OK;
}

static int croft_wit_wasi_clock_get_datetime(clockid_t clock_id, SapWitClocksDatetime* out)
{
    struct timespec ts;

    if (!out) {
        return ERR_INVALID;
    }
    if (clock_gettime(clock_id, &ts) != 0) {
        return ERR_INVALID;
    }
    out->seconds = (uint64_t)ts.tv_sec;
    out->nanoseconds = (uint32_t)ts.tv_nsec;
    return ERR_OK;
}

static int croft_wit_wasi_clock_get_resolution(clockid_t clock_id, SapWitClocksDatetime* out)
{
    struct timespec ts;

    if (!out) {
        return ERR_INVALID;
    }
    if (clock_getres(clock_id, &ts) != 0) {
        return ERR_INVALID;
    }
    out->seconds = (uint64_t)ts.tv_sec;
    out->nanoseconds = (uint32_t)ts.tv_nsec;
    return ERR_OK;
}

static int croft_wit_wasi_pollables_reserve(croft_wit_wasi_machine_runtime* runtime, size_t needed)
{
    croft_wit_wasi_pollable_slot* next_slots;
    size_t next_cap;

    if (!runtime) {
        return ERR_INVALID;
    }
    if (runtime->pollable_cap >= needed) {
        return ERR_OK;
    }

    next_cap = runtime->pollable_cap ? runtime->pollable_cap * 2u : 8u;
    while (next_cap < needed) {
        next_cap *= 2u;
    }

    next_slots = (croft_wit_wasi_pollable_slot*)realloc(runtime->pollables,
                                                        next_cap * sizeof(*next_slots));
    if (!next_slots) {
        return ERR_OOM;
    }

    memset(next_slots + runtime->pollable_cap,
           0,
           (next_cap - runtime->pollable_cap) * sizeof(*next_slots));
    runtime->pollables = next_slots;
    runtime->pollable_cap = next_cap;
    return ERR_OK;
}

static croft_wit_wasi_pollable_slot* croft_wit_wasi_pollable_lookup(
    croft_wit_wasi_machine_runtime* runtime,
    uint32_t handle)
{
    size_t i;

    if (!runtime || handle == 0u) {
        return NULL;
    }
    for (i = 0u; i < runtime->pollable_count; i++) {
        if (runtime->pollables[i].live && runtime->pollables[i].handle == handle) {
            return &runtime->pollables[i];
        }
    }
    return NULL;
}

static int croft_wit_wasi_pollable_insert(croft_wit_wasi_machine_runtime* runtime,
                                          uint64_t ready_at_ns,
                                          uint32_t* handle_out)
{
    croft_wit_wasi_pollable_slot* slot;
    int rc;

    if (!runtime || !handle_out) {
        return ERR_INVALID;
    }
    rc = croft_wit_wasi_pollables_reserve(runtime, runtime->pollable_count + 1u);
    if (rc != ERR_OK) {
        return rc;
    }

    slot = &runtime->pollables[runtime->pollable_count++];
    slot->live = 1u;
    slot->handle = runtime->next_pollable_handle++;
    if (slot->handle == 0u) {
        slot->handle = runtime->next_pollable_handle++;
    }
    slot->ready_at_ns = ready_at_ns;
    *handle_out = slot->handle;
    return ERR_OK;
}

static int croft_wit_wasi_pollable_ready_now(const croft_wit_wasi_pollable_slot* slot, int* out)
{
    uint64_t now_ns = 0u;
    int rc;

    if (!slot || !out) {
        return ERR_INVALID;
    }
    rc = croft_wit_wasi_clock_get_ns(CLOCK_MONOTONIC, &now_ns);
    if (rc != ERR_OK) {
        return rc;
    }
    *out = now_ns >= slot->ready_at_ns;
    return ERR_OK;
}

static int croft_wit_wasi_pollable_block_until_ready(const croft_wit_wasi_pollable_slot* slot)
{
    uint64_t now_ns = 0u;
    uint64_t remaining_ns;
    struct timespec req;
    int rc;

    if (!slot) {
        return ERR_INVALID;
    }
    rc = croft_wit_wasi_clock_get_ns(CLOCK_MONOTONIC, &now_ns);
    if (rc != ERR_OK) {
        return rc;
    }
    if (now_ns >= slot->ready_at_ns) {
        return ERR_OK;
    }

    remaining_ns = slot->ready_at_ns - now_ns;
    req.tv_sec = (time_t)(remaining_ns / 1000000000ull);
    req.tv_nsec = (long)(remaining_ns % 1000000000ull);
    while (nanosleep(&req, &req) != 0) {
        continue;
    }
    return ERR_OK;
}

static int croft_wit_wasi_reply_random_bytes(SapWitRandomReply* reply_out,
                                             croft_wit_wasi_byte_buffer* buffer)
{
    sap_wit_zero_random_reply(reply_out);
    reply_out->case_tag = SAP_WIT_RANDOM_REPLY_GET_RANDOM_BYTES;
    reply_out->val.get_random_bytes.data = buffer->data;
    reply_out->val.get_random_bytes.len = buffer->len;
    return ERR_OK;
}

static int croft_wit_wasi_reply_insecure_random_bytes(SapWitRandomInsecureReply* reply_out,
                                                      croft_wit_wasi_byte_buffer* buffer)
{
    sap_wit_zero_random_insecure_reply(reply_out);
    reply_out->case_tag = SAP_WIT_RANDOM_INSECURE_REPLY_GET_INSECURE_RANDOM_BYTES;
    reply_out->val.get_insecure_random_bytes.data = buffer->data;
    reply_out->val.get_insecure_random_bytes.len = buffer->len;
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_get_random_bytes(void* ctx,
                                                        const SapWitRandomGetRandomBytes* payload,
                                                        SapWitRandomReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    int rc;

    if (!runtime || !payload || !reply_out || payload->len > UINT32_MAX) {
        return ERR_INVALID;
    }

    runtime->random_blob.len = 0u;
    rc = croft_wit_wasi_buffer_reserve(&runtime->random_blob, (uint32_t)payload->len);
    if (rc != ERR_OK) {
        return rc;
    }
    runtime->random_blob.len = (uint32_t)payload->len;

    rc = croft_wit_wasi_random_fill(runtime->random_blob.data, runtime->random_blob.len);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_wit_wasi_reply_random_bytes(reply_out, &runtime->random_blob);
}

static int32_t croft_wit_wasi_dispatch_get_random_u64(void* ctx, SapWitRandomReply* reply_out)
{
    uint64_t value = 0u;
    int rc;

    if (!reply_out) {
        return ERR_INVALID;
    }
    rc = croft_wit_wasi_random_fill(&value, (uint32_t)sizeof(value));
    if (rc != ERR_OK) {
        return rc;
    }

    sap_wit_zero_random_reply(reply_out);
    reply_out->case_tag = SAP_WIT_RANDOM_REPLY_GET_RANDOM_U64;
    reply_out->val.get_random_u64 = value;
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_get_insecure_random_bytes(
    void* ctx,
    const SapWitRandomGetInsecureRandomBytes* payload,
    SapWitRandomInsecureReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    int rc;

    if (!runtime || !payload || !reply_out || payload->len > UINT32_MAX) {
        return ERR_INVALID;
    }

    runtime->random_blob.len = 0u;
    rc = croft_wit_wasi_buffer_reserve(&runtime->random_blob, (uint32_t)payload->len);
    if (rc != ERR_OK) {
        return rc;
    }
    runtime->random_blob.len = (uint32_t)payload->len;

    rc = croft_wit_wasi_random_fill(runtime->random_blob.data, runtime->random_blob.len);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_wit_wasi_reply_insecure_random_bytes(reply_out, &runtime->random_blob);
}

static int32_t croft_wit_wasi_dispatch_get_insecure_random_u64(void* ctx,
                                                               SapWitRandomInsecureReply* reply_out)
{
    uint64_t value = 0u;
    int rc;

    (void)ctx;
    if (!reply_out) {
        return ERR_INVALID;
    }
    rc = croft_wit_wasi_random_fill(&value, (uint32_t)sizeof(value));
    if (rc != ERR_OK) {
        return rc;
    }

    sap_wit_zero_random_insecure_reply(reply_out);
    reply_out->case_tag = SAP_WIT_RANDOM_INSECURE_REPLY_GET_INSECURE_RANDOM_U64;
    reply_out->val.get_insecure_random_u64 = value;
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_insecure_seed(void* ctx,
                                                     SapWitRandomInsecureSeedReply* reply_out)
{
    uint64_t seed[2] = {0u, 0u};
    int rc;

    (void)ctx;
    if (!reply_out) {
        return ERR_INVALID;
    }
    rc = croft_wit_wasi_random_fill(seed, (uint32_t)sizeof(seed));
    if (rc != ERR_OK) {
        return rc;
    }

    sap_wit_zero_random_insecure_seed_reply(reply_out);
    reply_out->case_tag = SAP_WIT_RANDOM_INSECURE_SEED_REPLY_INSECURE_SEED;
    reply_out->val.insecure_seed.v_0 = seed[0];
    reply_out->val.insecure_seed.v_1 = seed[1];
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_cli_get_environment(void* ctx,
                                                           SapWitCliEnvironmentReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;

    if (!runtime || !reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_cli_environment_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLI_ENVIRONMENT_REPLY_GET_ENVIRONMENT;
    reply_out->val.get_environment.data = runtime->env_blob.data;
    reply_out->val.get_environment.len = runtime->env_count;
    reply_out->val.get_environment.byte_len = runtime->env_blob.len;
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_cli_get_arguments(void* ctx,
                                                         SapWitCliEnvironmentReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;

    if (!runtime || !reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_cli_environment_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLI_ENVIRONMENT_REPLY_GET_ARGUMENTS;
    reply_out->val.get_arguments.data = runtime->argv_blob.data;
    reply_out->val.get_arguments.len = runtime->argc;
    reply_out->val.get_arguments.byte_len = runtime->argv_blob.len;
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_cli_initial_cwd(void* ctx,
                                                       SapWitCliEnvironmentReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;

    if (!runtime || !reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_cli_environment_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLI_ENVIRONMENT_REPLY_INITIAL_CWD;
    if (runtime->initial_cwd) {
        reply_out->val.initial_cwd.has_v = 1u;
        reply_out->val.initial_cwd.v_data = (const uint8_t*)runtime->initial_cwd;
        reply_out->val.initial_cwd.v_len = (uint32_t)strlen(runtime->initial_cwd);
    }
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_monotonic_now(void* ctx,
                                                     SapWitClocksMonotonicClockReply* reply_out)
{
    uint64_t now_ns = 0u;
    int rc;

    (void)ctx;
    if (!reply_out) {
        return ERR_INVALID;
    }

    rc = croft_wit_wasi_clock_get_ns(CLOCK_MONOTONIC, &now_ns);
    if (rc != ERR_OK) {
        return rc;
    }

    sap_wit_zero_clocks_monotonic_clock_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLOCKS_MONOTONIC_CLOCK_REPLY_NOW;
    reply_out->val.now = now_ns;
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_monotonic_subscribe(
    void* ctx,
    SapWitClocksMonotonicClockReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    uint64_t now_ns = 0u;
    uint32_t handle = 0u;
    int rc;

    if (!runtime || !reply_out) {
        return ERR_INVALID;
    }

    rc = croft_wit_wasi_clock_get_ns(CLOCK_MONOTONIC, &now_ns);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_wit_wasi_pollable_insert(runtime, now_ns, &handle);
    if (rc != ERR_OK) {
        return rc;
    }

    sap_wit_zero_clocks_monotonic_clock_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLOCKS_MONOTONIC_CLOCK_REPLY_POLLABLE;
    reply_out->val.pollable = (SapWitClocksPollableResource)handle;
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_wall_clock_now(void* ctx,
                                                      SapWitClocksWallClockReply* reply_out)
{
    int rc;

    (void)ctx;
    if (!reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_clocks_wall_clock_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLOCKS_WALL_CLOCK_REPLY_DATETIME;
    rc = croft_wit_wasi_clock_get_datetime(CLOCK_REALTIME, &reply_out->val.datetime);
    return rc;
}

static int32_t croft_wit_wasi_dispatch_wall_clock_resolution(
    void* ctx,
    SapWitClocksWallClockReply* reply_out)
{
    int rc;

    (void)ctx;
    if (!reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_clocks_wall_clock_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLOCKS_WALL_CLOCK_REPLY_DATETIME;
    rc = croft_wit_wasi_clock_get_resolution(CLOCK_REALTIME, &reply_out->val.datetime);
    return rc;
}

static int32_t croft_wit_wasi_dispatch_timezone_display(
    void* ctx,
    const SapWitClocksDatetime* payload,
    SapWitClocksTimezoneReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    time_t when;
    struct tm local_tm;
    struct tm gm_tm;
    time_t local_epoch;
    time_t gm_epoch;
    const char* zone_name;

    if (!runtime || !payload || !reply_out || payload->seconds > (uint64_t)LONG_MAX) {
        return ERR_INVALID;
    }

    when = (time_t)payload->seconds;
    if (!localtime_r(&when, &local_tm) || !gmtime_r(&when, &gm_tm)) {
        return ERR_INVALID;
    }

    tzset();
    local_epoch = mktime(&local_tm);
    gm_epoch = mktime(&gm_tm);
    zone_name = tzname[local_tm.tm_isdst > 0 ? 1 : 0];
    if (!zone_name) {
        zone_name = "";
    }

    memset(runtime->timezone_name, 0, sizeof(runtime->timezone_name));
    strncpy(runtime->timezone_name, zone_name, sizeof(runtime->timezone_name) - 1u);

    sap_wit_zero_clocks_timezone_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLOCKS_TIMEZONE_REPLY_TIMEZONE_DISPLAY;
    reply_out->val.timezone_display.utc_offset = (int32_t)difftime(local_epoch, gm_epoch);
    reply_out->val.timezone_display.name_data = (const uint8_t*)runtime->timezone_name;
    reply_out->val.timezone_display.name_len = (uint32_t)strlen(runtime->timezone_name);
    reply_out->val.timezone_display.in_daylight_saving_time =
        (uint8_t)(local_tm.tm_isdst > 0 ? 1u : 0u);
    return ERR_OK;
}

static void croft_wit_wasi_set_stream_error_closed(SapWitIoStreamError* out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->case_tag = SAP_WIT_IO_STREAM_ERROR_CLOSED;
}

static int32_t croft_wit_wasi_io_streams_reply_read_closed(SapWitIoStreamsReply* reply_out,
                                                            uint8_t blocking)
{
    if (!reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_io_streams_reply(reply_out);
    reply_out->case_tag = blocking ? SAP_WIT_IO_STREAMS_REPLY_BLOCKING_READ
                                   : SAP_WIT_IO_STREAMS_REPLY_READ;
    if (blocking) {
        reply_out->val.blocking_read.is_v_ok = 0u;
        croft_wit_wasi_set_stream_error_closed(&reply_out->val.blocking_read.v_val.err.v);
    } else {
        reply_out->val.read.is_v_ok = 0u;
        croft_wit_wasi_set_stream_error_closed(&reply_out->val.read.v_val.err.v);
    }
    return ERR_OK;
}

static int32_t croft_wit_wasi_io_streams_reply_skip_closed(SapWitIoStreamsReply* reply_out,
                                                            uint8_t blocking)
{
    if (!reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_io_streams_reply(reply_out);
    reply_out->case_tag = blocking ? SAP_WIT_IO_STREAMS_REPLY_BLOCKING_SKIP
                                   : SAP_WIT_IO_STREAMS_REPLY_SKIP;
    if (blocking) {
        reply_out->val.blocking_skip.is_v_ok = 0u;
        croft_wit_wasi_set_stream_error_closed(&reply_out->val.blocking_skip.v_val.err.v);
    } else {
        reply_out->val.skip.is_v_ok = 0u;
        croft_wit_wasi_set_stream_error_closed(&reply_out->val.skip.v_val.err.v);
    }
    return ERR_OK;
}

static int32_t croft_wit_wasi_io_streams_reply_pollable_invalid(SapWitIoStreamsReply* reply_out)
{
    if (!reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_io_streams_reply(reply_out);
    reply_out->case_tag = SAP_WIT_IO_STREAMS_REPLY_POLLABLE;
    reply_out->val.pollable = SAP_WIT_IO_POLLABLE_RESOURCE_INVALID;
    return ERR_OK;
}

static int32_t croft_wit_wasi_io_streams_reply_check_write_closed(
    SapWitIoStreamsReply* reply_out)
{
    if (!reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_io_streams_reply(reply_out);
    reply_out->case_tag = SAP_WIT_IO_STREAMS_REPLY_CHECK_WRITE;
    reply_out->val.check_write.is_v_ok = 0u;
    croft_wit_wasi_set_stream_error_closed(&reply_out->val.check_write.v_val.err.v);
    return ERR_OK;
}

static int32_t croft_wit_wasi_io_streams_reply_status_closed(SapWitIoStreamsReply* reply_out)
{
    if (!reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_io_streams_reply(reply_out);
    reply_out->case_tag = SAP_WIT_IO_STREAMS_REPLY_STATUS;
    reply_out->val.status.is_v_ok = 0u;
    croft_wit_wasi_set_stream_error_closed(&reply_out->val.status.v_val.err.v);
    return ERR_OK;
}

static int32_t croft_wit_wasi_io_streams_reply_splice_closed(SapWitIoStreamsReply* reply_out,
                                                              uint8_t blocking)
{
    if (!reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_io_streams_reply(reply_out);
    reply_out->case_tag = blocking ? SAP_WIT_IO_STREAMS_REPLY_BLOCKING_SPLICE
                                   : SAP_WIT_IO_STREAMS_REPLY_SPLICE;
    if (blocking) {
        reply_out->val.blocking_splice.is_v_ok = 0u;
        croft_wit_wasi_set_stream_error_closed(&reply_out->val.blocking_splice.v_val.err.v);
    } else {
        reply_out->val.splice.is_v_ok = 0u;
        croft_wit_wasi_set_stream_error_closed(&reply_out->val.splice.v_val.err.v);
    }
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_io_streams_read(void* ctx,
                                                       const SapWitIoInputStreamRead* payload,
                                                       SapWitIoStreamsReply* reply_out)
{
    (void)ctx;
    (void)payload;
    return croft_wit_wasi_io_streams_reply_read_closed(reply_out, 0u);
}

static int32_t croft_wit_wasi_dispatch_io_streams_blocking_read(
    void* ctx,
    const SapWitIoInputStreamBlockingRead* payload,
    SapWitIoStreamsReply* reply_out)
{
    (void)ctx;
    (void)payload;
    return croft_wit_wasi_io_streams_reply_read_closed(reply_out, 1u);
}

static int32_t croft_wit_wasi_dispatch_io_streams_skip(void* ctx,
                                                       const SapWitIoInputStreamSkip* payload,
                                                       SapWitIoStreamsReply* reply_out)
{
    (void)ctx;
    (void)payload;
    return croft_wit_wasi_io_streams_reply_skip_closed(reply_out, 0u);
}

static int32_t croft_wit_wasi_dispatch_io_streams_blocking_skip(
    void* ctx,
    const SapWitIoInputStreamBlockingSkip* payload,
    SapWitIoStreamsReply* reply_out)
{
    (void)ctx;
    (void)payload;
    return croft_wit_wasi_io_streams_reply_skip_closed(reply_out, 1u);
}

static int32_t croft_wit_wasi_dispatch_io_streams_input_stream_subscribe(
    void* ctx,
    const SapWitIoInputStreamSubscribe* payload,
    SapWitIoStreamsReply* reply_out)
{
    (void)ctx;
    (void)payload;
    return croft_wit_wasi_io_streams_reply_pollable_invalid(reply_out);
}

static int32_t croft_wit_wasi_dispatch_io_streams_check_write(
    void* ctx,
    const SapWitIoOutputStreamCheckWrite* payload,
    SapWitIoStreamsReply* reply_out)
{
    (void)ctx;
    (void)payload;
    return croft_wit_wasi_io_streams_reply_check_write_closed(reply_out);
}

static int32_t croft_wit_wasi_dispatch_io_streams_write(void* ctx,
                                                        const SapWitIoOutputStreamWrite* payload,
                                                        SapWitIoStreamsReply* reply_out)
{
    (void)ctx;
    (void)payload;
    return croft_wit_wasi_io_streams_reply_status_closed(reply_out);
}

static int32_t croft_wit_wasi_dispatch_io_streams_blocking_write_and_flush(
    void* ctx,
    const SapWitIoOutputStreamBlockingWriteAndFlush* payload,
    SapWitIoStreamsReply* reply_out)
{
    (void)ctx;
    (void)payload;
    return croft_wit_wasi_io_streams_reply_status_closed(reply_out);
}

static int32_t croft_wit_wasi_dispatch_io_streams_flush(void* ctx,
                                                        const SapWitIoOutputStreamFlush* payload,
                                                        SapWitIoStreamsReply* reply_out)
{
    (void)ctx;
    (void)payload;
    return croft_wit_wasi_io_streams_reply_status_closed(reply_out);
}

static int32_t croft_wit_wasi_dispatch_io_streams_blocking_flush(
    void* ctx,
    const SapWitIoOutputStreamBlockingFlush* payload,
    SapWitIoStreamsReply* reply_out)
{
    (void)ctx;
    (void)payload;
    return croft_wit_wasi_io_streams_reply_status_closed(reply_out);
}

static int32_t croft_wit_wasi_dispatch_io_streams_output_stream_subscribe(
    void* ctx,
    const SapWitIoOutputStreamSubscribe* payload,
    SapWitIoStreamsReply* reply_out)
{
    (void)ctx;
    (void)payload;
    return croft_wit_wasi_io_streams_reply_pollable_invalid(reply_out);
}

static int32_t croft_wit_wasi_dispatch_io_streams_write_zeroes(
    void* ctx,
    const SapWitIoOutputStreamWriteZeroes* payload,
    SapWitIoStreamsReply* reply_out)
{
    (void)ctx;
    (void)payload;
    return croft_wit_wasi_io_streams_reply_status_closed(reply_out);
}

static int32_t croft_wit_wasi_dispatch_io_streams_blocking_write_zeroes_and_flush(
    void* ctx,
    const SapWitIoOutputStreamBlockingWriteZeroesAndFlush* payload,
    SapWitIoStreamsReply* reply_out)
{
    (void)ctx;
    (void)payload;
    return croft_wit_wasi_io_streams_reply_status_closed(reply_out);
}

static int32_t croft_wit_wasi_dispatch_io_streams_splice(
    void* ctx,
    const SapWitIoOutputStreamSplice* payload,
    SapWitIoStreamsReply* reply_out)
{
    (void)ctx;
    (void)payload;
    return croft_wit_wasi_io_streams_reply_splice_closed(reply_out, 0u);
}

static int32_t croft_wit_wasi_dispatch_io_streams_blocking_splice(
    void* ctx,
    const SapWitIoOutputStreamBlockingSplice* payload,
    SapWitIoStreamsReply* reply_out)
{
    (void)ctx;
    (void)payload;
    return croft_wit_wasi_io_streams_reply_splice_closed(reply_out, 1u);
}

static int32_t croft_wit_wasi_dispatch_io_ready(void* ctx,
                                                const SapWitIoPollableReady* payload,
                                                SapWitIoPollReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_pollable_slot* slot;
    int ready = 0;
    int rc;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }

    slot = croft_wit_wasi_pollable_lookup(runtime, payload->pollable);
    if (!slot) {
        return ERR_INVALID;
    }
    rc = croft_wit_wasi_pollable_ready_now(slot, &ready);
    if (rc != ERR_OK) {
        return rc;
    }

    sap_wit_zero_io_poll_reply(reply_out);
    reply_out->case_tag = SAP_WIT_IO_POLL_REPLY_READY;
    reply_out->val.ready = (uint8_t)(ready ? 1u : 0u);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_io_block(void* ctx,
                                                const SapWitIoPollableBlock* payload,
                                                SapWitIoPollReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_pollable_slot* slot;
    int rc;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }

    slot = croft_wit_wasi_pollable_lookup(runtime, payload->pollable);
    if (!slot) {
        return ERR_INVALID;
    }
    rc = croft_wit_wasi_pollable_block_until_ready(slot);
    if (rc != ERR_OK) {
        return rc;
    }

    sap_wit_zero_io_poll_reply(reply_out);
    reply_out->case_tag = SAP_WIT_IO_POLL_REPLY_BLOCK;
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_io_poll(void* ctx,
                                               const SapWitIoPoll* payload,
                                               SapWitIoPollReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    ThatchRegion view;
    ThatchCursor cursor = 0u;
    uint32_t i;
    int rc;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }

    rc = thatch_region_init_readonly(&view, payload->in_data, payload->in_byte_len);
    if (rc != ERR_OK) {
        return rc;
    }

    runtime->poll_indexes_blob.len = 0u;
    for (i = 0u; i < payload->in_len; i++) {
        uint8_t tag = 0u;
        uint32_t handle = 0u;
        croft_wit_wasi_pollable_slot* slot;
        int ready = 0;

        rc = thatch_read_tag(&view, &cursor, &tag);
        if (rc != ERR_OK) {
            return rc;
        }
        if (tag != SAP_WIT_TAG_RESOURCE) {
            return ERR_TYPE;
        }
        rc = thatch_read_data(&view, &cursor, (uint32_t)sizeof(handle), &handle);
        if (rc != ERR_OK) {
            return rc;
        }

        slot = croft_wit_wasi_pollable_lookup(runtime, handle);
        if (!slot) {
            return ERR_INVALID;
        }
        rc = croft_wit_wasi_pollable_ready_now(slot, &ready);
        if (rc != ERR_OK) {
            return rc;
        }
        if (ready) {
            rc = croft_wit_wasi_buffer_encode_u32(&runtime->poll_indexes_blob, i);
            if (rc != ERR_OK) {
                return rc;
            }
        }
    }
    if (cursor != payload->in_byte_len) {
        return ERR_TYPE;
    }

    sap_wit_zero_io_poll_reply(reply_out);
    reply_out->case_tag = SAP_WIT_IO_POLL_REPLY_POLL;
    reply_out->val.poll.data = runtime->poll_indexes_blob.data;
    reply_out->val.poll.len = 0u;

    if (runtime->poll_indexes_blob.len > 0u) {
        ThatchRegion view;
        ThatchCursor cursor = 0u;
        rc = thatch_region_init_readonly(&view,
                                         runtime->poll_indexes_blob.data,
                                         runtime->poll_indexes_blob.len);
        if (rc != ERR_OK) {
            return rc;
        }
        while (cursor < runtime->poll_indexes_blob.len) {
            uint8_t tag = 0u;
            uint32_t ignored = 0u;

            rc = thatch_read_tag(&view, &cursor, &tag);
            if (rc != ERR_OK || tag != SAP_WIT_TAG_U32) {
                return rc != ERR_OK ? rc : ERR_TYPE;
            }
            rc = thatch_read_data(&view, &cursor, (uint32_t)sizeof(ignored), &ignored);
            if (rc != ERR_OK) {
                return rc;
            }
            reply_out->val.poll.len++;
        }
    }

    reply_out->val.poll.byte_len = runtime->poll_indexes_blob.len;
    return ERR_OK;
}

#if !defined(CROFT_OS_WINDOWS)
#if defined(CROFT_OS_MACOS)
#define CROFT_WIT_WASI_STAT_ATIME_NSEC(st) ((st).st_atimespec.tv_nsec)
#define CROFT_WIT_WASI_STAT_MTIME_NSEC(st) ((st).st_mtimespec.tv_nsec)
#define CROFT_WIT_WASI_STAT_CTIME_NSEC(st) ((st).st_ctimespec.tv_nsec)
#else
#define CROFT_WIT_WASI_STAT_ATIME_NSEC(st) ((st).st_atim.tv_nsec)
#define CROFT_WIT_WASI_STAT_MTIME_NSEC(st) ((st).st_mtim.tv_nsec)
#define CROFT_WIT_WASI_STAT_CTIME_NSEC(st) ((st).st_ctim.tv_nsec)
#endif

static uint8_t croft_wit_wasi_fs_error_from_errno(int err)
{
    switch (err) {
        case 0:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_INVALID;
        case EACCES:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_ACCESS;
        case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
        case EWOULDBLOCK:
#endif
            return SAP_WIT_FILESYSTEM_ERROR_CODE_WOULD_BLOCK;
        case EALREADY:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_ALREADY;
        case EBADF:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR;
        case EBUSY:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_BUSY;
        case EDEADLK:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_DEADLOCK;
        case EDQUOT:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_QUOTA;
        case EEXIST:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_EXIST;
        case EFBIG:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_FILE_TOO_LARGE;
        case EILSEQ:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_ILLEGAL_BYTE_SEQUENCE;
        case EINPROGRESS:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_IN_PROGRESS;
        case EINTR:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_INTERRUPTED;
        case EINVAL:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_INVALID;
        case EIO:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_IO;
        case EISDIR:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_IS_DIRECTORY;
        case ELOOP:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_LOOP;
        case EMLINK:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_TOO_MANY_LINKS;
        case EMSGSIZE:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_MESSAGE_SIZE;
        case ENAMETOOLONG:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_NAME_TOO_LONG;
        case ENODEV:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_NO_DEVICE;
        case ENOENT:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_NO_ENTRY;
        case ENOLCK:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_NO_LOCK;
        case ENOMEM:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_INSUFFICIENT_MEMORY;
        case ENOSPC:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_INSUFFICIENT_SPACE;
        case ENOTDIR:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_NOT_DIRECTORY;
        case ENOTEMPTY:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_NOT_EMPTY;
#ifdef ENOTRECOVERABLE
        case ENOTRECOVERABLE:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_NOT_RECOVERABLE;
#endif
        case ENOTSUP:
#ifdef ENOSYS
        case ENOSYS:
#endif
            return SAP_WIT_FILESYSTEM_ERROR_CODE_UNSUPPORTED;
#ifdef ENOTTY
        case ENOTTY:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_NO_TTY;
#endif
        case ENXIO:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_NO_SUCH_DEVICE;
        case EOVERFLOW:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_OVERFLOW;
        case EPERM:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_NOT_PERMITTED;
        case EPIPE:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_PIPE;
        case EROFS:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_READ_ONLY;
        case ESPIPE:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_INVALID_SEEK;
        case ETXTBSY:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_TEXT_FILE_BUSY;
        case EXDEV:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_CROSS_DEVICE;
        default:
            return SAP_WIT_FILESYSTEM_ERROR_CODE_IO;
    }
}

static uint8_t croft_wit_wasi_fs_descriptor_type_from_mode(mode_t mode)
{
    if (S_ISBLK(mode)) {
        return SAP_WIT_FILESYSTEM_DESCRIPTOR_TYPE_BLOCK_DEVICE;
    }
    if (S_ISCHR(mode)) {
        return SAP_WIT_FILESYSTEM_DESCRIPTOR_TYPE_CHARACTER_DEVICE;
    }
    if (S_ISDIR(mode)) {
        return SAP_WIT_FILESYSTEM_DESCRIPTOR_TYPE_DIRECTORY;
    }
    if (S_ISFIFO(mode)) {
        return SAP_WIT_FILESYSTEM_DESCRIPTOR_TYPE_FIFO;
    }
    if (S_ISLNK(mode)) {
        return SAP_WIT_FILESYSTEM_DESCRIPTOR_TYPE_SYMBOLIC_LINK;
    }
    if (S_ISREG(mode)) {
        return SAP_WIT_FILESYSTEM_DESCRIPTOR_TYPE_REGULAR_FILE;
    }
    if (S_ISSOCK(mode)) {
        return SAP_WIT_FILESYSTEM_DESCRIPTOR_TYPE_SOCKET;
    }
    return SAP_WIT_FILESYSTEM_DESCRIPTOR_TYPE_UNKNOWN;
}

static int croft_wit_wasi_fs_copy_relative_path(const uint8_t* data,
                                                uint32_t len,
                                                char* out,
                                                size_t out_cap,
                                                uint8_t* fs_error_out)
{
    uint32_t i;
    uint32_t segment_start = 0u;

    if (!data || !out || out_cap == 0u) {
        return ERR_INVALID;
    }
    if (fs_error_out) {
        *fs_error_out = SAP_WIT_FILESYSTEM_ERROR_CODE_INVALID;
    }
    if (len == 0u || len >= out_cap) {
        return ERR_RANGE;
    }
    if (data[0] == '/') {
        if (fs_error_out) {
            *fs_error_out = SAP_WIT_FILESYSTEM_ERROR_CODE_NOT_PERMITTED;
        }
        return ERR_INVALID;
    }
    if (memchr(data, '\0', len) != NULL) {
        return ERR_INVALID;
    }

    for (i = 0u; i <= len; i++) {
        if (i == len || data[i] == '/') {
            uint32_t segment_len = i - segment_start;
            if (segment_len == 0u) {
                return ERR_INVALID;
            }
            if (segment_len == 2u
                && data[segment_start] == '.'
                && data[segment_start + 1u] == '.') {
                if (fs_error_out) {
                    *fs_error_out = SAP_WIT_FILESYSTEM_ERROR_CODE_NOT_PERMITTED;
                }
                return ERR_INVALID;
            }
            segment_start = i + 1u;
        }
    }

    memcpy(out, data, len);
    out[len] = '\0';
    return ERR_OK;
}

static int croft_wit_wasi_fs_descriptors_reserve(croft_wit_wasi_machine_runtime* runtime,
                                                 size_t needed)
{
    croft_wit_wasi_descriptor_slot* next_slots;
    size_t next_cap;

    if (!runtime) {
        return ERR_INVALID;
    }
    if (runtime->descriptor_cap >= needed) {
        return ERR_OK;
    }

    next_cap = runtime->descriptor_cap ? runtime->descriptor_cap * 2u : 8u;
    while (next_cap < needed) {
        next_cap *= 2u;
    }

    next_slots = (croft_wit_wasi_descriptor_slot*)realloc(runtime->descriptors,
                                                          next_cap * sizeof(*next_slots));
    if (!next_slots) {
        return ERR_OOM;
    }
    memset(next_slots + runtime->descriptor_cap,
           0,
           (next_cap - runtime->descriptor_cap) * sizeof(*next_slots));
    runtime->descriptors = next_slots;
    runtime->descriptor_cap = next_cap;
    return ERR_OK;
}

static int croft_wit_wasi_fs_descriptor_insert(croft_wit_wasi_machine_runtime* runtime,
                                               int fd,
                                               uint32_t descriptor_flags,
                                               uint8_t is_directory,
                                               uint32_t* handle_out)
{
    size_t i;
    int rc;

    if (!runtime || !handle_out || fd < 0) {
        return ERR_INVALID;
    }
    for (i = 0u; i < runtime->descriptor_count; i++) {
        if (!runtime->descriptors[i].live) {
            runtime->descriptors[i].live = 1u;
            runtime->descriptors[i].fd = fd;
            runtime->descriptors[i].descriptor_flags = descriptor_flags;
            runtime->descriptors[i].is_directory = is_directory;
            *handle_out = (uint32_t)(i + 1u);
            return ERR_OK;
        }
    }

    rc = croft_wit_wasi_fs_descriptors_reserve(runtime, runtime->descriptor_count + 1u);
    if (rc != ERR_OK) {
        return rc;
    }
    runtime->descriptors[runtime->descriptor_count].live = 1u;
    runtime->descriptors[runtime->descriptor_count].fd = fd;
    runtime->descriptors[runtime->descriptor_count].descriptor_flags = descriptor_flags;
    runtime->descriptors[runtime->descriptor_count].is_directory = is_directory;
    runtime->descriptor_count++;
    *handle_out = (uint32_t)runtime->descriptor_count;
    return ERR_OK;
}

static croft_wit_wasi_descriptor_slot* croft_wit_wasi_fs_descriptor_lookup(
    croft_wit_wasi_machine_runtime* runtime,
    uint32_t handle)
{
    size_t index;

    if (!runtime || handle == 0u) {
        return NULL;
    }
    index = (size_t)handle - 1u;
    if (index >= runtime->descriptor_count || !runtime->descriptors[index].live) {
        return NULL;
    }
    return &runtime->descriptors[index];
}

static int croft_wit_wasi_fs_directory_streams_reserve(croft_wit_wasi_machine_runtime* runtime,
                                                       size_t needed)
{
    croft_wit_wasi_directory_stream_slot* next_slots;
    size_t next_cap;

    if (!runtime) {
        return ERR_INVALID;
    }
    if (runtime->directory_stream_cap >= needed) {
        return ERR_OK;
    }

    next_cap = runtime->directory_stream_cap ? runtime->directory_stream_cap * 2u : 4u;
    while (next_cap < needed) {
        next_cap *= 2u;
    }

    next_slots = (croft_wit_wasi_directory_stream_slot*)realloc(
        runtime->directory_streams, next_cap * sizeof(*next_slots));
    if (!next_slots) {
        return ERR_OOM;
    }
    memset(next_slots + runtime->directory_stream_cap,
           0,
           (next_cap - runtime->directory_stream_cap) * sizeof(*next_slots));
    runtime->directory_streams = next_slots;
    runtime->directory_stream_cap = next_cap;
    return ERR_OK;
}

static int croft_wit_wasi_fs_directory_stream_insert(croft_wit_wasi_machine_runtime* runtime,
                                                     DIR* dir,
                                                     uint32_t* handle_out)
{
    size_t i;
    int rc;

    if (!runtime || !dir || !handle_out) {
        return ERR_INVALID;
    }
    for (i = 0u; i < runtime->directory_stream_count; i++) {
        if (!runtime->directory_streams[i].live) {
            runtime->directory_streams[i].live = 1u;
            runtime->directory_streams[i].dir = dir;
            *handle_out = (uint32_t)(i + 1u);
            return ERR_OK;
        }
    }

    rc = croft_wit_wasi_fs_directory_streams_reserve(runtime,
                                                     runtime->directory_stream_count + 1u);
    if (rc != ERR_OK) {
        return rc;
    }
    runtime->directory_streams[runtime->directory_stream_count].live = 1u;
    runtime->directory_streams[runtime->directory_stream_count].dir = dir;
    runtime->directory_stream_count++;
    *handle_out = (uint32_t)runtime->directory_stream_count;
    return ERR_OK;
}

static croft_wit_wasi_directory_stream_slot* croft_wit_wasi_fs_directory_stream_lookup(
    croft_wit_wasi_machine_runtime* runtime,
    uint32_t handle)
{
    size_t index;

    if (!runtime || handle == 0u) {
        return NULL;
    }
    index = (size_t)handle - 1u;
    if (index >= runtime->directory_stream_count || !runtime->directory_streams[index].live) {
        return NULL;
    }
    return &runtime->directory_streams[index];
}

static int croft_wit_wasi_fs_preopens_reserve(croft_wit_wasi_machine_runtime* runtime,
                                              size_t needed)
{
    croft_wit_wasi_preopen_slot* next_slots;
    size_t next_cap;

    if (!runtime) {
        return ERR_INVALID;
    }
    if (runtime->preopen_cap >= needed) {
        return ERR_OK;
    }

    next_cap = runtime->preopen_cap ? runtime->preopen_cap * 2u : 4u;
    while (next_cap < needed) {
        next_cap *= 2u;
    }

    next_slots = (croft_wit_wasi_preopen_slot*)realloc(runtime->preopens,
                                                       next_cap * sizeof(*next_slots));
    if (!next_slots) {
        return ERR_OOM;
    }
    memset(next_slots + runtime->preopen_cap,
           0,
           (next_cap - runtime->preopen_cap) * sizeof(*next_slots));
    runtime->preopens = next_slots;
    runtime->preopen_cap = next_cap;
    return ERR_OK;
}

static int croft_wit_wasi_fs_add_preopen(croft_wit_wasi_machine_runtime* runtime,
                                         const char* host_path,
                                         const char* guest_path)
{
    char* guest_copy = NULL;
    uint32_t descriptor_flags = 0u;
    uint32_t handle = 0u;
    int fd = -1;
    int rc;

    if (!runtime || !host_path || !guest_path || guest_path[0] == '\0') {
        return ERR_INVALID;
    }

    fd = open(host_path, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        return ERR_INVALID;
    }

    if (access(host_path, R_OK) == 0) {
        descriptor_flags |= SAP_WIT_FILESYSTEM_DESCRIPTOR_FLAGS_READ;
    }
    if (access(host_path, W_OK) == 0) {
        descriptor_flags |= SAP_WIT_FILESYSTEM_DESCRIPTOR_FLAGS_MUTATE_DIRECTORY;
    }

    rc = croft_wit_wasi_fs_descriptor_insert(runtime, fd, descriptor_flags, 1u, &handle);
    if (rc != ERR_OK) {
        close(fd);
        return rc;
    }

    guest_copy = croft_wit_wasi_copy_string(guest_path);
    if (!guest_copy) {
        croft_wit_wasi_fs_descriptor_lookup(runtime, handle)->live = 0u;
        close(fd);
        return ERR_OOM;
    }

    rc = croft_wit_wasi_fs_preopens_reserve(runtime, runtime->preopen_count + 1u);
    if (rc != ERR_OK) {
        free(guest_copy);
        croft_wit_wasi_fs_descriptor_lookup(runtime, handle)->live = 0u;
        close(fd);
        return rc;
    }

    runtime->preopens[runtime->preopen_count].descriptor_handle = handle;
    runtime->preopens[runtime->preopen_count].guest_path = guest_copy;
    runtime->preopen_count++;
    return ERR_OK;
}

static int croft_wit_wasi_fs_build_preopen_blob(croft_wit_wasi_machine_runtime* runtime)
{
    size_t i;

    if (!runtime) {
        return ERR_INVALID;
    }
    runtime->preopen_blob.len = 0u;
    for (i = 0u; i < runtime->preopen_count; i++) {
        const char* guest_path = runtime->preopens[i].guest_path;
        uint32_t guest_len;
        int rc;

        if (!guest_path) {
            return ERR_INVALID;
        }
        if (strlen(guest_path) > UINT32_MAX) {
            return ERR_RANGE;
        }
        guest_len = (uint32_t)strlen(guest_path);
        rc = croft_wit_wasi_buffer_encode_resource_string_pair(
            &runtime->preopen_blob,
            runtime->preopens[i].descriptor_handle,
            guest_path,
            guest_len);
        if (rc != ERR_OK) {
            return rc;
        }
    }
    return ERR_OK;
}

static int croft_wit_wasi_fs_fill_descriptor_stat(const struct stat* st,
                                                  SapWitFilesystemDescriptorStat* out)
{
    if (!st || !out) {
        return ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    out->type = croft_wit_wasi_fs_descriptor_type_from_mode(st->st_mode);
    out->link_count = (uint64_t)st->st_nlink;
    out->size = (uint64_t)st->st_size;
    out->has_data_access_timestamp = 1u;
    out->data_access_timestamp.seconds = (uint64_t)st->st_atime;
    out->data_access_timestamp.nanoseconds = (uint32_t)CROFT_WIT_WASI_STAT_ATIME_NSEC(*st);
    out->has_data_modification_timestamp = 1u;
    out->data_modification_timestamp.seconds = (uint64_t)st->st_mtime;
    out->data_modification_timestamp.nanoseconds =
        (uint32_t)CROFT_WIT_WASI_STAT_MTIME_NSEC(*st);
    out->has_status_change_timestamp = 1u;
    out->status_change_timestamp.seconds = (uint64_t)st->st_ctime;
    out->status_change_timestamp.nanoseconds = (uint32_t)CROFT_WIT_WASI_STAT_CTIME_NSEC(*st);
    return ERR_OK;
}

static void croft_wit_wasi_fs_hash_mix(uint64_t* state, uint64_t value)
{
    *state ^= value;
    *state *= 1099511628211ull;
}

static int croft_wit_wasi_fs_fill_metadata_hash(const struct stat* st,
                                                SapWitFilesystemMetadataHashValue* out)
{
    uint64_t lower = 1469598103934665603ull;
    uint64_t upper = 1099511628211ull;

    if (!st || !out) {
        return ERR_INVALID;
    }

    croft_wit_wasi_fs_hash_mix(&lower, (uint64_t)st->st_dev);
    croft_wit_wasi_fs_hash_mix(&lower, (uint64_t)st->st_ino);
    croft_wit_wasi_fs_hash_mix(&lower, (uint64_t)st->st_size);
    croft_wit_wasi_fs_hash_mix(&lower, (uint64_t)st->st_mtime);
    croft_wit_wasi_fs_hash_mix(&lower, (uint64_t)CROFT_WIT_WASI_STAT_MTIME_NSEC(*st));
    croft_wit_wasi_fs_hash_mix(&upper, (uint64_t)st->st_mode);
    croft_wit_wasi_fs_hash_mix(&upper, (uint64_t)st->st_nlink);
    croft_wit_wasi_fs_hash_mix(&upper, (uint64_t)st->st_ctime);
    croft_wit_wasi_fs_hash_mix(&upper, (uint64_t)CROFT_WIT_WASI_STAT_CTIME_NSEC(*st));
    croft_wit_wasi_fs_hash_mix(&upper, (uint64_t)st->st_atime);
    croft_wit_wasi_fs_hash_mix(&upper, (uint64_t)CROFT_WIT_WASI_STAT_ATIME_NSEC(*st));

    out->lower = lower;
    out->upper = upper;
    return ERR_OK;
}

static int croft_wit_wasi_fs_apply_new_timestamp(const SapWitFilesystemNewTimestamp* in,
                                                 struct timespec* out)
{
    if (!in || !out) {
        return ERR_INVALID;
    }

    switch (in->case_tag) {
        case SAP_WIT_FILESYSTEM_NEW_TIMESTAMP_NO_CHANGE:
            out->tv_sec = 0;
            out->tv_nsec = UTIME_OMIT;
            return ERR_OK;
        case SAP_WIT_FILESYSTEM_NEW_TIMESTAMP_NOW:
            out->tv_sec = 0;
            out->tv_nsec = UTIME_NOW;
            return ERR_OK;
        case SAP_WIT_FILESYSTEM_NEW_TIMESTAMP_TIMESTAMP:
            if (in->val.timestamp.seconds > (uint64_t)LONG_MAX
                || in->val.timestamp.nanoseconds >= 1000000000u) {
                return ERR_RANGE;
            }
            out->tv_sec = (time_t)in->val.timestamp.seconds;
            out->tv_nsec = (long)in->val.timestamp.nanoseconds;
            return ERR_OK;
        default:
            return ERR_INVALID;
    }
}

static void croft_wit_wasi_fs_types_reply_status_ok(SapWitFilesystemTypesReply* reply_out)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_STATUS;
    reply_out->val.status.is_v_ok = 1u;
}

static void croft_wit_wasi_fs_types_reply_status_err(SapWitFilesystemTypesReply* reply_out,
                                                     uint8_t error_code)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_STATUS;
    reply_out->val.status.is_v_ok = 0u;
    reply_out->val.status.v_val.err.v = error_code;
}

static void croft_wit_wasi_fs_types_reply_input_stream_err(SapWitFilesystemTypesReply* reply_out,
                                                           uint8_t error_code)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_INPUT_STREAM;
    reply_out->val.input_stream.is_v_ok = 0u;
    reply_out->val.input_stream.v_val.err.v = error_code;
}

static void croft_wit_wasi_fs_types_reply_output_stream_err(SapWitFilesystemTypesReply* reply_out,
                                                            uint8_t error_code)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_OUTPUT_STREAM;
    reply_out->val.output_stream.is_v_ok = 0u;
    reply_out->val.output_stream.v_val.err.v = error_code;
}

static void croft_wit_wasi_fs_types_reply_get_flags_ok(SapWitFilesystemTypesReply* reply_out,
                                                       uint32_t flags)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_GET_FLAGS;
    reply_out->val.get_flags.is_v_ok = 1u;
    reply_out->val.get_flags.v_val.ok.v = flags;
}

static void croft_wit_wasi_fs_types_reply_get_flags_err(SapWitFilesystemTypesReply* reply_out,
                                                        uint8_t error_code)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_GET_FLAGS;
    reply_out->val.get_flags.is_v_ok = 0u;
    reply_out->val.get_flags.v_val.err.v = error_code;
}

static void croft_wit_wasi_fs_types_reply_get_type_ok(SapWitFilesystemTypesReply* reply_out,
                                                      uint8_t type_code)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_GET_TYPE;
    reply_out->val.get_type.is_v_ok = 1u;
    reply_out->val.get_type.v_val.ok.v = type_code;
}

static void croft_wit_wasi_fs_types_reply_get_type_err(SapWitFilesystemTypesReply* reply_out,
                                                       uint8_t error_code)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_GET_TYPE;
    reply_out->val.get_type.is_v_ok = 0u;
    reply_out->val.get_type.v_val.err.v = error_code;
}

static void croft_wit_wasi_fs_types_reply_read_ok(SapWitFilesystemTypesReply* reply_out,
                                                  const uint8_t* data,
                                                  uint32_t len,
                                                  uint8_t eof)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_READ;
    reply_out->val.read.is_v_ok = 1u;
    reply_out->val.read.v_val.ok.v_0_data = data;
    reply_out->val.read.v_val.ok.v_0_len = len;
    reply_out->val.read.v_val.ok.v_1 = eof;
}

static void croft_wit_wasi_fs_types_reply_read_err(SapWitFilesystemTypesReply* reply_out,
                                                   uint8_t error_code)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_READ;
    reply_out->val.read.is_v_ok = 0u;
    reply_out->val.read.v_val.err.v = error_code;
}

static void croft_wit_wasi_fs_types_reply_write_ok(SapWitFilesystemTypesReply* reply_out,
                                                   uint64_t bytes_written)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_WRITE;
    reply_out->val.write.is_v_ok = 1u;
    reply_out->val.write.v_val.ok.v = bytes_written;
}

static void croft_wit_wasi_fs_types_reply_write_err(SapWitFilesystemTypesReply* reply_out,
                                                    uint8_t error_code)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_WRITE;
    reply_out->val.write.is_v_ok = 0u;
    reply_out->val.write.v_val.err.v = error_code;
}

static void croft_wit_wasi_fs_types_reply_directory_stream_ok(
    SapWitFilesystemTypesReply* reply_out,
    SapWitFilesystemDirectoryEntryStreamResource handle)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_DIRECTORY_ENTRY_STREAM;
    reply_out->val.directory_entry_stream.is_v_ok = 1u;
    reply_out->val.directory_entry_stream.v_val.ok.v = handle;
}

static void croft_wit_wasi_fs_types_reply_directory_stream_err(
    SapWitFilesystemTypesReply* reply_out,
    uint8_t error_code)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_DIRECTORY_ENTRY_STREAM;
    reply_out->val.directory_entry_stream.is_v_ok = 0u;
    reply_out->val.directory_entry_stream.v_val.err.v = error_code;
}

static void croft_wit_wasi_fs_types_reply_stat_ok(SapWitFilesystemTypesReply* reply_out,
                                                  uint8_t case_tag,
                                                  const SapWitFilesystemDescriptorStat* stat)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = case_tag;
    if (case_tag == SAP_WIT_FILESYSTEM_TYPES_REPLY_STAT) {
        reply_out->val.stat.is_v_ok = 1u;
        reply_out->val.stat.v_val.ok.v = *stat;
    } else {
        reply_out->val.stat_at.is_v_ok = 1u;
        reply_out->val.stat_at.v_val.ok.v = *stat;
    }
}

static void croft_wit_wasi_fs_types_reply_stat_err(SapWitFilesystemTypesReply* reply_out,
                                                   uint8_t case_tag,
                                                   uint8_t error_code)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = case_tag;
    if (case_tag == SAP_WIT_FILESYSTEM_TYPES_REPLY_STAT) {
        reply_out->val.stat.is_v_ok = 0u;
        reply_out->val.stat.v_val.err.v = error_code;
    } else {
        reply_out->val.stat_at.is_v_ok = 0u;
        reply_out->val.stat_at.v_val.err.v = error_code;
    }
}

static void croft_wit_wasi_fs_types_reply_descriptor_ok(SapWitFilesystemTypesReply* reply_out,
                                                        SapWitFilesystemDescriptorResource handle)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_DESCRIPTOR;
    reply_out->val.descriptor.is_v_ok = 1u;
    reply_out->val.descriptor.v_val.ok.v = handle;
}

static void croft_wit_wasi_fs_types_reply_descriptor_err(SapWitFilesystemTypesReply* reply_out,
                                                         uint8_t error_code)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_DESCRIPTOR;
    reply_out->val.descriptor.is_v_ok = 0u;
    reply_out->val.descriptor.v_val.err.v = error_code;
}

static void croft_wit_wasi_fs_types_reply_readlink_ok(SapWitFilesystemTypesReply* reply_out,
                                                      const uint8_t* data,
                                                      uint32_t len)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_READLINK_AT;
    reply_out->val.readlink_at.is_v_ok = 1u;
    reply_out->val.readlink_at.v_val.ok.v_data = data;
    reply_out->val.readlink_at.v_val.ok.v_len = len;
}

static void croft_wit_wasi_fs_types_reply_readlink_err(SapWitFilesystemTypesReply* reply_out,
                                                       uint8_t error_code)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_READLINK_AT;
    reply_out->val.readlink_at.is_v_ok = 0u;
    reply_out->val.readlink_at.v_val.err.v = error_code;
}

static void croft_wit_wasi_fs_types_reply_metadata_hash_ok(
    SapWitFilesystemTypesReply* reply_out,
    uint8_t case_tag,
    const SapWitFilesystemMetadataHashValue* value)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = case_tag;
    if (case_tag == SAP_WIT_FILESYSTEM_TYPES_REPLY_METADATA_HASH) {
        reply_out->val.metadata_hash.is_v_ok = 1u;
        reply_out->val.metadata_hash.v_val.ok.v = *value;
    } else {
        reply_out->val.metadata_hash_at.is_v_ok = 1u;
        reply_out->val.metadata_hash_at.v_val.ok.v = *value;
    }
}

static void croft_wit_wasi_fs_types_reply_metadata_hash_err(
    SapWitFilesystemTypesReply* reply_out,
    uint8_t case_tag,
    uint8_t error_code)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = case_tag;
    if (case_tag == SAP_WIT_FILESYSTEM_TYPES_REPLY_METADATA_HASH) {
        reply_out->val.metadata_hash.is_v_ok = 0u;
        reply_out->val.metadata_hash.v_val.err.v = error_code;
    } else {
        reply_out->val.metadata_hash_at.is_v_ok = 0u;
        reply_out->val.metadata_hash_at.v_val.err.v = error_code;
    }
}

static void croft_wit_wasi_fs_types_reply_is_same_object(SapWitFilesystemTypesReply* reply_out,
                                                         uint8_t same_object)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_IS_SAME_OBJECT;
    reply_out->val.is_same_object = same_object;
}

static void croft_wit_wasi_fs_types_reply_read_directory_entry_ok(
    SapWitFilesystemTypesReply* reply_out,
    const SapWitFilesystemDirectoryEntry* entry,
    uint8_t has_v)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_READ_DIRECTORY_ENTRY;
    reply_out->val.read_directory_entry.is_v_ok = 1u;
    reply_out->val.read_directory_entry.v_val.ok.has_v = has_v;
    if (has_v && entry) {
        reply_out->val.read_directory_entry.v_val.ok.v = *entry;
    }
}

static void croft_wit_wasi_fs_types_reply_read_directory_entry_err(
    SapWitFilesystemTypesReply* reply_out,
    uint8_t error_code)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_READ_DIRECTORY_ENTRY;
    reply_out->val.read_directory_entry.is_v_ok = 0u;
    reply_out->val.read_directory_entry.v_val.err.v = error_code;
}

static void croft_wit_wasi_fs_types_reply_filesystem_error_code(
    SapWitFilesystemTypesReply* reply_out,
    uint8_t has_v,
    uint8_t error_code)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_FILESYSTEM_ERROR_CODE;
    reply_out->val.filesystem_error_code.has_v = has_v;
    reply_out->val.filesystem_error_code.v = error_code;
}

static void croft_wit_wasi_fs_preopens_reply(SapWitFilesystemPreopensReply* reply_out,
                                             const uint8_t* data,
                                             uint32_t len,
                                             uint32_t byte_len)
{
    sap_wit_zero_filesystem_preopens_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_PREOPENS_REPLY_GET_DIRECTORIES;
    reply_out->val.get_directories.data = data;
    reply_out->val.get_directories.len = len;
    reply_out->val.get_directories.byte_len = byte_len;
}

static int croft_wit_wasi_fs_apply_timespec_pair(const SapWitFilesystemNewTimestamp* atime,
                                                 const SapWitFilesystemNewTimestamp* mtime,
                                                 struct timespec out[2])
{
    int rc;

    rc = croft_wit_wasi_fs_apply_new_timestamp(atime, &out[0]);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_wit_wasi_fs_apply_new_timestamp(mtime, &out[1]);
}

static int croft_wit_wasi_fs_reply_path_blob(croft_wit_wasi_machine_runtime* runtime,
                                             const char* text,
                                             uint32_t len)
{
    int rc;

    if (!runtime || (!text && len > 0u)) {
        return ERR_INVALID;
    }
    runtime->filesystem_blob.len = 0u;
    rc = croft_wit_wasi_buffer_reserve(&runtime->filesystem_blob, len ? len : 1u);
    if (rc != ERR_OK) {
        return rc;
    }
    if (len > 0u) {
        memcpy(runtime->filesystem_blob.data, text, len);
    }
    runtime->filesystem_blob.len = len;
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_read_via_stream(
    void* ctx,
    const SapWitFilesystemDescriptorReadViaStream* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    (void)ctx;
    (void)payload;
    croft_wit_wasi_fs_types_reply_input_stream_err(reply_out,
                                                   SAP_WIT_FILESYSTEM_ERROR_CODE_UNSUPPORTED);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_write_via_stream(
    void* ctx,
    const SapWitFilesystemDescriptorWriteViaStream* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    (void)ctx;
    (void)payload;
    croft_wit_wasi_fs_types_reply_output_stream_err(reply_out,
                                                    SAP_WIT_FILESYSTEM_ERROR_CODE_UNSUPPORTED);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_append_via_stream(
    void* ctx,
    const SapWitFilesystemDescriptorAppendViaStream* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    (void)ctx;
    (void)payload;
    croft_wit_wasi_fs_types_reply_output_stream_err(reply_out,
                                                    SAP_WIT_FILESYSTEM_ERROR_CODE_UNSUPPORTED);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_advise(
    void* ctx,
    const SapWitFilesystemDescriptorAdvise* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!slot) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    (void)slot;
    croft_wit_wasi_fs_types_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_sync_data(
    void* ctx,
    const SapWitFilesystemDescriptorSyncData* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!slot) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    if (fdatasync(slot->fd) != 0) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    croft_wit_wasi_fs_types_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_get_flags(
    void* ctx,
    const SapWitFilesystemDescriptorGetFlags* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!slot) {
        croft_wit_wasi_fs_types_reply_get_flags_err(reply_out,
                                                    SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    croft_wit_wasi_fs_types_reply_get_flags_ok(reply_out, slot->descriptor_flags);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_get_type(
    void* ctx,
    const SapWitFilesystemDescriptorGetType* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;
    struct stat st;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!slot) {
        croft_wit_wasi_fs_types_reply_get_type_err(reply_out,
                                                   SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    if (fstat(slot->fd, &st) != 0) {
        croft_wit_wasi_fs_types_reply_get_type_err(reply_out,
                                                   croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    croft_wit_wasi_fs_types_reply_get_type_ok(
        reply_out, croft_wit_wasi_fs_descriptor_type_from_mode(st.st_mode));
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_set_size(
    void* ctx,
    const SapWitFilesystemDescriptorSetSize* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;

    if (!runtime || !payload || !reply_out || payload->size > (uint64_t)INT64_MAX) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!slot) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    if (ftruncate(slot->fd, (off_t)payload->size) != 0) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    croft_wit_wasi_fs_types_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_set_times(
    void* ctx,
    const SapWitFilesystemDescriptorSetTimes* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;
    struct timespec times[2];
    int rc;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!slot) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    rc = croft_wit_wasi_fs_apply_timespec_pair(&payload->data_access_timestamp,
                                               &payload->data_modification_timestamp,
                                               times);
    if (rc != ERR_OK) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 SAP_WIT_FILESYSTEM_ERROR_CODE_INVALID);
        return ERR_OK;
    }
    if (futimens(slot->fd, times) != 0) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    croft_wit_wasi_fs_types_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_read(
    void* ctx,
    const SapWitFilesystemDescriptorRead* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;
    struct stat st;
    ssize_t nread;
    uint8_t eof = 0u;
    int rc;

    if (!runtime || !payload || !reply_out || payload->length > UINT32_MAX
        || payload->offset > (uint64_t)INT64_MAX) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!slot) {
        croft_wit_wasi_fs_types_reply_read_err(reply_out,
                                               SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }

    runtime->filesystem_blob.len = 0u;
    rc = croft_wit_wasi_buffer_reserve(&runtime->filesystem_blob,
                                       payload->length > 0u ? (uint32_t)payload->length : 1u);
    if (rc != ERR_OK) {
        return rc;
    }

    nread = pread(slot->fd,
                  runtime->filesystem_blob.data,
                  (size_t)payload->length,
                  (off_t)payload->offset);
    if (nread < 0) {
        croft_wit_wasi_fs_types_reply_read_err(reply_out,
                                               croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    runtime->filesystem_blob.len = (uint32_t)nread;

    if (fstat(slot->fd, &st) == 0 && payload->offset <= (uint64_t)INT64_MAX) {
        uint64_t end_offset = payload->offset + (uint64_t)runtime->filesystem_blob.len;
        eof = (uint8_t)(end_offset >= (uint64_t)st.st_size ? 1u : 0u);
    }
    croft_wit_wasi_fs_types_reply_read_ok(reply_out,
                                          runtime->filesystem_blob.data,
                                          runtime->filesystem_blob.len,
                                          eof);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_write(
    void* ctx,
    const SapWitFilesystemDescriptorWrite* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;
    ssize_t nwritten;

    if (!runtime || !payload || !reply_out || (!payload->buffer_data && payload->buffer_len > 0u)
        || payload->offset > (uint64_t)INT64_MAX) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!slot) {
        croft_wit_wasi_fs_types_reply_write_err(reply_out,
                                                SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }

    nwritten = pwrite(slot->fd,
                      payload->buffer_data,
                      (size_t)payload->buffer_len,
                      (off_t)payload->offset);
    if (nwritten < 0) {
        croft_wit_wasi_fs_types_reply_write_err(reply_out,
                                                croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    croft_wit_wasi_fs_types_reply_write_ok(reply_out, (uint64_t)nwritten);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_read_directory(
    void* ctx,
    const SapWitFilesystemDescriptorReadDirectory* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;
    uint32_t handle = 0u;
    DIR* dir = NULL;
    int dup_fd;
    int rc;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!slot) {
        croft_wit_wasi_fs_types_reply_directory_stream_err(
            reply_out, SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    dup_fd = dup(slot->fd);
    if (dup_fd < 0) {
        croft_wit_wasi_fs_types_reply_directory_stream_err(
            reply_out, croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    dir = fdopendir(dup_fd);
    if (!dir) {
        close(dup_fd);
        croft_wit_wasi_fs_types_reply_directory_stream_err(
            reply_out, croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    rc = croft_wit_wasi_fs_directory_stream_insert(runtime, dir, &handle);
    if (rc != ERR_OK) {
        closedir(dir);
        return rc;
    }
    croft_wit_wasi_fs_types_reply_directory_stream_ok(reply_out,
                                                      (SapWitFilesystemDirectoryEntryStreamResource)
                                                          handle);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_sync(
    void* ctx,
    const SapWitFilesystemDescriptorSync* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!slot) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    if (fsync(slot->fd) != 0) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    croft_wit_wasi_fs_types_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_create_directory_at(
    void* ctx,
    const SapWitFilesystemDescriptorCreateDirectoryAt* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;
    char path[PATH_MAX];
    uint8_t fs_error = SAP_WIT_FILESYSTEM_ERROR_CODE_INVALID;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!slot) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    if (croft_wit_wasi_fs_copy_relative_path(payload->path_data,
                                             payload->path_len,
                                             path,
                                             sizeof(path),
                                             &fs_error)
        != ERR_OK) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out, fs_error);
        return ERR_OK;
    }
    if (mkdirat(slot->fd, path, 0777) != 0) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    croft_wit_wasi_fs_types_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_stat(
    void* ctx,
    const SapWitFilesystemDescriptorStatRequest* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;
    struct stat st;
    SapWitFilesystemDescriptorStat out = {0};

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!slot) {
        croft_wit_wasi_fs_types_reply_stat_err(reply_out,
                                               SAP_WIT_FILESYSTEM_TYPES_REPLY_STAT,
                                               SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    if (fstat(slot->fd, &st) != 0 || croft_wit_wasi_fs_fill_descriptor_stat(&st, &out) != ERR_OK) {
        croft_wit_wasi_fs_types_reply_stat_err(reply_out,
                                               SAP_WIT_FILESYSTEM_TYPES_REPLY_STAT,
                                               croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    croft_wit_wasi_fs_types_reply_stat_ok(reply_out,
                                          SAP_WIT_FILESYSTEM_TYPES_REPLY_STAT,
                                          &out);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_stat_at(
    void* ctx,
    const SapWitFilesystemDescriptorStatAt* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;
    char path[PATH_MAX];
    uint8_t fs_error = SAP_WIT_FILESYSTEM_ERROR_CODE_INVALID;
    struct stat st;
    SapWitFilesystemDescriptorStat out = {0};
    int flags = 0;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!slot) {
        croft_wit_wasi_fs_types_reply_stat_err(reply_out,
                                               SAP_WIT_FILESYSTEM_TYPES_REPLY_STAT_AT,
                                               SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    if (croft_wit_wasi_fs_copy_relative_path(payload->path_data,
                                             payload->path_len,
                                             path,
                                             sizeof(path),
                                             &fs_error)
        != ERR_OK) {
        croft_wit_wasi_fs_types_reply_stat_err(reply_out,
                                               SAP_WIT_FILESYSTEM_TYPES_REPLY_STAT_AT,
                                               fs_error);
        return ERR_OK;
    }
    if ((payload->path_flags & SAP_WIT_FILESYSTEM_PATH_FLAGS_SYMLINK_FOLLOW) == 0u) {
        flags |= AT_SYMLINK_NOFOLLOW;
    }
    if (fstatat(slot->fd, path, &st, flags) != 0
        || croft_wit_wasi_fs_fill_descriptor_stat(&st, &out) != ERR_OK) {
        croft_wit_wasi_fs_types_reply_stat_err(reply_out,
                                               SAP_WIT_FILESYSTEM_TYPES_REPLY_STAT_AT,
                                               croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    croft_wit_wasi_fs_types_reply_stat_ok(reply_out,
                                          SAP_WIT_FILESYSTEM_TYPES_REPLY_STAT_AT,
                                          &out);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_set_times_at(
    void* ctx,
    const SapWitFilesystemDescriptorSetTimesAt* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;
    char path[PATH_MAX];
    uint8_t fs_error = SAP_WIT_FILESYSTEM_ERROR_CODE_INVALID;
    struct timespec times[2];
    int flags = 0;
    int rc;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!slot) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    if (croft_wit_wasi_fs_copy_relative_path(payload->path_data,
                                             payload->path_len,
                                             path,
                                             sizeof(path),
                                             &fs_error)
        != ERR_OK) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out, fs_error);
        return ERR_OK;
    }
    rc = croft_wit_wasi_fs_apply_timespec_pair(&payload->data_access_timestamp,
                                               &payload->data_modification_timestamp,
                                               times);
    if (rc != ERR_OK) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 SAP_WIT_FILESYSTEM_ERROR_CODE_INVALID);
        return ERR_OK;
    }
    if ((payload->path_flags & SAP_WIT_FILESYSTEM_PATH_FLAGS_SYMLINK_FOLLOW) == 0u) {
        flags |= AT_SYMLINK_NOFOLLOW;
    }
    if (utimensat(slot->fd, path, times, flags) != 0) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    croft_wit_wasi_fs_types_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_link_at(
    void* ctx,
    const SapWitFilesystemDescriptorLinkAt* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;
    croft_wit_wasi_descriptor_slot* target_slot;
    char old_path[PATH_MAX];
    char new_path[PATH_MAX];
    uint8_t fs_error = SAP_WIT_FILESYSTEM_ERROR_CODE_INVALID;
    int flags = 0;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    target_slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->new_descriptor);
    if (!slot || !target_slot) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    if (croft_wit_wasi_fs_copy_relative_path(payload->old_path_data,
                                             payload->old_path_len,
                                             old_path,
                                             sizeof(old_path),
                                             &fs_error)
        != ERR_OK
        || croft_wit_wasi_fs_copy_relative_path(payload->new_path_data,
                                                payload->new_path_len,
                                                new_path,
                                                sizeof(new_path),
                                                &fs_error)
               != ERR_OK) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out, fs_error);
        return ERR_OK;
    }
    if ((payload->old_path_flags & SAP_WIT_FILESYSTEM_PATH_FLAGS_SYMLINK_FOLLOW) == 0u) {
        flags |= AT_SYMLINK_NOFOLLOW;
    }
    if (linkat(slot->fd, old_path, target_slot->fd, new_path, flags) != 0) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    croft_wit_wasi_fs_types_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_open_at(
    void* ctx,
    const SapWitFilesystemDescriptorOpenAt* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;
    char path[PATH_MAX];
    uint8_t fs_error = SAP_WIT_FILESYSTEM_ERROR_CODE_INVALID;
    struct stat st;
    uint32_t descriptor_flags = payload->flags;
    uint32_t handle = 0u;
    int oflags = 0;
    int fd;
    int rc;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!slot) {
        croft_wit_wasi_fs_types_reply_descriptor_err(reply_out,
                                                     SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    if (croft_wit_wasi_fs_copy_relative_path(payload->path_data,
                                             payload->path_len,
                                             path,
                                             sizeof(path),
                                             &fs_error)
        != ERR_OK) {
        croft_wit_wasi_fs_types_reply_descriptor_err(reply_out, fs_error);
        return ERR_OK;
    }

    if ((payload->flags & SAP_WIT_FILESYSTEM_DESCRIPTOR_FLAGS_WRITE) != 0u) {
        oflags |= O_RDWR;
    } else {
        oflags |= O_RDONLY;
    }
    if ((payload->open_flags & SAP_WIT_FILESYSTEM_OPEN_FLAGS_CREATE) != 0u) {
        oflags |= O_CREAT;
    }
    if ((payload->open_flags & SAP_WIT_FILESYSTEM_OPEN_FLAGS_EXCLUSIVE) != 0u) {
        oflags |= O_EXCL;
    }
    if ((payload->open_flags & SAP_WIT_FILESYSTEM_OPEN_FLAGS_TRUNCATE) != 0u) {
        oflags |= O_TRUNC;
    }
    if ((payload->open_flags & SAP_WIT_FILESYSTEM_OPEN_FLAGS_DIRECTORY) != 0u) {
        oflags |= O_DIRECTORY;
    }
    if ((payload->path_flags & SAP_WIT_FILESYSTEM_PATH_FLAGS_SYMLINK_FOLLOW) == 0u) {
        oflags |= O_NOFOLLOW;
    }

    fd = openat(slot->fd, path, oflags, 0666);
    if (fd < 0) {
        croft_wit_wasi_fs_types_reply_descriptor_err(reply_out,
                                                     croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    if (fstat(fd, &st) != 0) {
        close(fd);
        croft_wit_wasi_fs_types_reply_descriptor_err(reply_out,
                                                     croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    if ((payload->flags & SAP_WIT_FILESYSTEM_DESCRIPTOR_FLAGS_MUTATE_DIRECTORY) != 0u
        && !S_ISDIR(st.st_mode)) {
        close(fd);
        croft_wit_wasi_fs_types_reply_descriptor_err(reply_out,
                                                     SAP_WIT_FILESYSTEM_ERROR_CODE_NOT_DIRECTORY);
        return ERR_OK;
    }
    if (!S_ISDIR(st.st_mode)) {
        descriptor_flags &= ~SAP_WIT_FILESYSTEM_DESCRIPTOR_FLAGS_MUTATE_DIRECTORY;
    }
    rc = croft_wit_wasi_fs_descriptor_insert(runtime,
                                             fd,
                                             descriptor_flags,
                                             (uint8_t)(S_ISDIR(st.st_mode) ? 1u : 0u),
                                             &handle);
    if (rc != ERR_OK) {
        close(fd);
        return rc;
    }
    croft_wit_wasi_fs_types_reply_descriptor_ok(reply_out,
                                                (SapWitFilesystemDescriptorResource)handle);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_readlink_at(
    void* ctx,
    const SapWitFilesystemDescriptorReadlinkAt* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;
    char path[PATH_MAX];
    uint8_t fs_error = SAP_WIT_FILESYSTEM_ERROR_CODE_INVALID;
    ssize_t nread;
    int rc;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!slot) {
        croft_wit_wasi_fs_types_reply_readlink_err(reply_out,
                                                   SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    if (croft_wit_wasi_fs_copy_relative_path(payload->path_data,
                                             payload->path_len,
                                             path,
                                             sizeof(path),
                                             &fs_error)
        != ERR_OK) {
        croft_wit_wasi_fs_types_reply_readlink_err(reply_out, fs_error);
        return ERR_OK;
    }
    runtime->filesystem_blob.len = 0u;
    rc = croft_wit_wasi_buffer_reserve(&runtime->filesystem_blob, PATH_MAX);
    if (rc != ERR_OK) {
        return rc;
    }
    nread = readlinkat(slot->fd, path, (char*)runtime->filesystem_blob.data, PATH_MAX);
    if (nread < 0) {
        croft_wit_wasi_fs_types_reply_readlink_err(reply_out,
                                                   croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    runtime->filesystem_blob.len = (uint32_t)nread;
    croft_wit_wasi_fs_types_reply_readlink_ok(reply_out,
                                              runtime->filesystem_blob.data,
                                              runtime->filesystem_blob.len);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_remove_directory_at(
    void* ctx,
    const SapWitFilesystemDescriptorRemoveDirectoryAt* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;
    char path[PATH_MAX];
    uint8_t fs_error = SAP_WIT_FILESYSTEM_ERROR_CODE_INVALID;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!slot) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    if (croft_wit_wasi_fs_copy_relative_path(payload->path_data,
                                             payload->path_len,
                                             path,
                                             sizeof(path),
                                             &fs_error)
        != ERR_OK) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out, fs_error);
        return ERR_OK;
    }
    if (unlinkat(slot->fd, path, AT_REMOVEDIR) != 0) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    croft_wit_wasi_fs_types_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_rename_at(
    void* ctx,
    const SapWitFilesystemDescriptorRenameAt* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;
    croft_wit_wasi_descriptor_slot* target_slot;
    char old_path[PATH_MAX];
    char new_path[PATH_MAX];
    uint8_t fs_error = SAP_WIT_FILESYSTEM_ERROR_CODE_INVALID;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    target_slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->new_descriptor);
    if (!slot || !target_slot) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    if (croft_wit_wasi_fs_copy_relative_path(payload->old_path_data,
                                             payload->old_path_len,
                                             old_path,
                                             sizeof(old_path),
                                             &fs_error)
        != ERR_OK
        || croft_wit_wasi_fs_copy_relative_path(payload->new_path_data,
                                                payload->new_path_len,
                                                new_path,
                                                sizeof(new_path),
                                                &fs_error)
               != ERR_OK) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out, fs_error);
        return ERR_OK;
    }
    if (renameat(slot->fd, old_path, target_slot->fd, new_path) != 0) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    croft_wit_wasi_fs_types_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_symlink_at(
    void* ctx,
    const SapWitFilesystemDescriptorSymlinkAt* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;
    char old_path[PATH_MAX];
    char new_path[PATH_MAX];
    uint8_t fs_error = SAP_WIT_FILESYSTEM_ERROR_CODE_INVALID;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!slot) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    if (croft_wit_wasi_fs_copy_relative_path(payload->old_path_data,
                                             payload->old_path_len,
                                             old_path,
                                             sizeof(old_path),
                                             &fs_error)
        != ERR_OK
        || croft_wit_wasi_fs_copy_relative_path(payload->new_path_data,
                                                payload->new_path_len,
                                                new_path,
                                                sizeof(new_path),
                                                &fs_error)
               != ERR_OK) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out, fs_error);
        return ERR_OK;
    }
    if (symlinkat(old_path, slot->fd, new_path) != 0) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    croft_wit_wasi_fs_types_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_unlink_file_at(
    void* ctx,
    const SapWitFilesystemDescriptorUnlinkFileAt* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;
    char path[PATH_MAX];
    uint8_t fs_error = SAP_WIT_FILESYSTEM_ERROR_CODE_INVALID;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!slot) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    if (croft_wit_wasi_fs_copy_relative_path(payload->path_data,
                                             payload->path_len,
                                             path,
                                             sizeof(path),
                                             &fs_error)
        != ERR_OK) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out, fs_error);
        return ERR_OK;
    }
    if (unlinkat(slot->fd, path, 0) != 0) {
        croft_wit_wasi_fs_types_reply_status_err(reply_out,
                                                 croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    croft_wit_wasi_fs_types_reply_status_ok(reply_out);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_is_same_object(
    void* ctx,
    const SapWitFilesystemDescriptorIsSameObject* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* lhs;
    croft_wit_wasi_descriptor_slot* rhs;
    struct stat lhs_st;
    struct stat rhs_st;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    lhs = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    rhs = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->other);
    if (!lhs || !rhs) {
        croft_wit_wasi_fs_types_reply_is_same_object(reply_out, 0u);
        return ERR_OK;
    }
    if (fstat(lhs->fd, &lhs_st) != 0 || fstat(rhs->fd, &rhs_st) != 0) {
        croft_wit_wasi_fs_types_reply_is_same_object(reply_out, 0u);
        return ERR_OK;
    }
    croft_wit_wasi_fs_types_reply_is_same_object(
        reply_out,
        (uint8_t)((lhs_st.st_dev == rhs_st.st_dev && lhs_st.st_ino == rhs_st.st_ino) ? 1u : 0u));
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_metadata_hash(
    void* ctx,
    const SapWitFilesystemDescriptorMetadataHash* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;
    struct stat st;
    SapWitFilesystemMetadataHashValue hash = {0};

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!slot) {
        croft_wit_wasi_fs_types_reply_metadata_hash_err(
            reply_out,
            SAP_WIT_FILESYSTEM_TYPES_REPLY_METADATA_HASH,
            SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    if (fstat(slot->fd, &st) != 0 || croft_wit_wasi_fs_fill_metadata_hash(&st, &hash) != ERR_OK) {
        croft_wit_wasi_fs_types_reply_metadata_hash_err(
            reply_out,
            SAP_WIT_FILESYSTEM_TYPES_REPLY_METADATA_HASH,
            croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    croft_wit_wasi_fs_types_reply_metadata_hash_ok(
        reply_out, SAP_WIT_FILESYSTEM_TYPES_REPLY_METADATA_HASH, &hash);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_metadata_hash_at(
    void* ctx,
    const SapWitFilesystemDescriptorMetadataHashAt* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* slot;
    char path[PATH_MAX];
    uint8_t fs_error = SAP_WIT_FILESYSTEM_ERROR_CODE_INVALID;
    struct stat st;
    SapWitFilesystemMetadataHashValue hash = {0};
    int flags = 0;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!slot) {
        croft_wit_wasi_fs_types_reply_metadata_hash_err(
            reply_out,
            SAP_WIT_FILESYSTEM_TYPES_REPLY_METADATA_HASH_AT,
            SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    if (croft_wit_wasi_fs_copy_relative_path(payload->path_data,
                                             payload->path_len,
                                             path,
                                             sizeof(path),
                                             &fs_error)
        != ERR_OK) {
        croft_wit_wasi_fs_types_reply_metadata_hash_err(
            reply_out, SAP_WIT_FILESYSTEM_TYPES_REPLY_METADATA_HASH_AT, fs_error);
        return ERR_OK;
    }
    if ((payload->path_flags & SAP_WIT_FILESYSTEM_PATH_FLAGS_SYMLINK_FOLLOW) == 0u) {
        flags |= AT_SYMLINK_NOFOLLOW;
    }
    if (fstatat(slot->fd, path, &st, flags) != 0
        || croft_wit_wasi_fs_fill_metadata_hash(&st, &hash) != ERR_OK) {
        croft_wit_wasi_fs_types_reply_metadata_hash_err(
            reply_out,
            SAP_WIT_FILESYSTEM_TYPES_REPLY_METADATA_HASH_AT,
            croft_wit_wasi_fs_error_from_errno(errno));
        return ERR_OK;
    }
    croft_wit_wasi_fs_types_reply_metadata_hash_ok(
        reply_out, SAP_WIT_FILESYSTEM_TYPES_REPLY_METADATA_HASH_AT, &hash);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_read_directory_entry(
    void* ctx,
    const SapWitFilesystemDirectoryEntryStreamReadDirectoryEntry* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_directory_stream_slot* slot;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    slot = croft_wit_wasi_fs_directory_stream_lookup(runtime, payload->directory_entry_stream);
    if (!slot || !slot->dir) {
        croft_wit_wasi_fs_types_reply_read_directory_entry_err(
            reply_out, SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }

    for (;;) {
        errno = 0;
        struct dirent* ent = readdir(slot->dir);
        SapWitFilesystemDirectoryEntry entry = {0};

        if (!ent) {
            if (errno != 0) {
                croft_wit_wasi_fs_types_reply_read_directory_entry_err(
                    reply_out, croft_wit_wasi_fs_error_from_errno(errno));
            } else {
                croft_wit_wasi_fs_types_reply_read_directory_entry_ok(reply_out, NULL, 0u);
            }
            return ERR_OK;
        }
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        entry.type = SAP_WIT_FILESYSTEM_DESCRIPTOR_TYPE_UNKNOWN;
#ifdef DT_DIR
        switch (ent->d_type) {
            case DT_DIR:
                entry.type = SAP_WIT_FILESYSTEM_DESCRIPTOR_TYPE_DIRECTORY;
                break;
            case DT_REG:
                entry.type = SAP_WIT_FILESYSTEM_DESCRIPTOR_TYPE_REGULAR_FILE;
                break;
            case DT_LNK:
                entry.type = SAP_WIT_FILESYSTEM_DESCRIPTOR_TYPE_SYMBOLIC_LINK;
                break;
            case DT_FIFO:
                entry.type = SAP_WIT_FILESYSTEM_DESCRIPTOR_TYPE_FIFO;
                break;
            case DT_CHR:
                entry.type = SAP_WIT_FILESYSTEM_DESCRIPTOR_TYPE_CHARACTER_DEVICE;
                break;
            case DT_BLK:
                entry.type = SAP_WIT_FILESYSTEM_DESCRIPTOR_TYPE_BLOCK_DEVICE;
                break;
            case DT_SOCK:
                entry.type = SAP_WIT_FILESYSTEM_DESCRIPTOR_TYPE_SOCKET;
                break;
            default:
                break;
        }
#endif
        entry.name_data = (const uint8_t*)ent->d_name;
        entry.name_len = (uint32_t)strlen(ent->d_name);
        croft_wit_wasi_fs_types_reply_read_directory_entry_ok(reply_out, &entry, 1u);
        return ERR_OK;
    }
}

static int32_t croft_wit_wasi_dispatch_fs_filesystem_error_code(
    void* ctx,
    const SapWitFilesystemErrorCode* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    (void)ctx;
    (void)payload;
    if (!reply_out) {
        return ERR_INVALID;
    }
    croft_wit_wasi_fs_types_reply_filesystem_error_code(reply_out, 0u, 0u);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_preopens_get_directories(
    void* ctx,
    SapWitFilesystemPreopensReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;

    if (!runtime || !reply_out) {
        return ERR_INVALID;
    }
    croft_wit_wasi_fs_preopens_reply(reply_out,
                                     runtime->preopen_blob.data,
                                     (uint32_t)runtime->preopen_count,
                                     runtime->preopen_blob.len);
    return ERR_OK;
}

static const SapWitFilesystemTypesDispatchOps g_croft_wit_wasi_filesystem_types_ops = {
    .read_via_stream = croft_wit_wasi_dispatch_fs_read_via_stream,
    .write_via_stream = croft_wit_wasi_dispatch_fs_write_via_stream,
    .append_via_stream = croft_wit_wasi_dispatch_fs_append_via_stream,
    .advise = croft_wit_wasi_dispatch_fs_advise,
    .sync_data = croft_wit_wasi_dispatch_fs_sync_data,
    .get_flags = croft_wit_wasi_dispatch_fs_get_flags,
    .get_type = croft_wit_wasi_dispatch_fs_get_type,
    .set_size = croft_wit_wasi_dispatch_fs_set_size,
    .set_times = croft_wit_wasi_dispatch_fs_set_times,
    .read = croft_wit_wasi_dispatch_fs_read,
    .write = croft_wit_wasi_dispatch_fs_write,
    .read_directory = croft_wit_wasi_dispatch_fs_read_directory,
    .sync = croft_wit_wasi_dispatch_fs_sync,
    .create_directory_at = croft_wit_wasi_dispatch_fs_create_directory_at,
    .stat = croft_wit_wasi_dispatch_fs_stat,
    .stat_at = croft_wit_wasi_dispatch_fs_stat_at,
    .set_times_at = croft_wit_wasi_dispatch_fs_set_times_at,
    .link_at = croft_wit_wasi_dispatch_fs_link_at,
    .open_at = croft_wit_wasi_dispatch_fs_open_at,
    .readlink_at = croft_wit_wasi_dispatch_fs_readlink_at,
    .remove_directory_at = croft_wit_wasi_dispatch_fs_remove_directory_at,
    .rename_at = croft_wit_wasi_dispatch_fs_rename_at,
    .symlink_at = croft_wit_wasi_dispatch_fs_symlink_at,
    .unlink_file_at = croft_wit_wasi_dispatch_fs_unlink_file_at,
    .is_same_object = croft_wit_wasi_dispatch_fs_is_same_object,
    .metadata_hash = croft_wit_wasi_dispatch_fs_metadata_hash,
    .metadata_hash_at = croft_wit_wasi_dispatch_fs_metadata_hash_at,
    .read_directory_entry = croft_wit_wasi_dispatch_fs_read_directory_entry,
    .filesystem_error_code = croft_wit_wasi_dispatch_fs_filesystem_error_code,
};

static const SapWitFilesystemPreopensDispatchOps g_croft_wit_wasi_filesystem_preopens_ops = {
    .get_directories = croft_wit_wasi_dispatch_fs_preopens_get_directories,
};
#endif

static const SapWitCliEnvironmentDispatchOps g_croft_wit_wasi_cli_environment_ops = {
    .get_environment = croft_wit_wasi_dispatch_cli_get_environment,
    .get_arguments = croft_wit_wasi_dispatch_cli_get_arguments,
    .initial_cwd = croft_wit_wasi_dispatch_cli_initial_cwd,
};

static const SapWitRandomDispatchOps g_croft_wit_wasi_random_ops = {
    .get_random_bytes = croft_wit_wasi_dispatch_get_random_bytes,
    .get_random_u64 = croft_wit_wasi_dispatch_get_random_u64,
};

static const SapWitRandomInsecureDispatchOps g_croft_wit_wasi_random_insecure_ops = {
    .get_insecure_random_bytes = croft_wit_wasi_dispatch_get_insecure_random_bytes,
    .get_insecure_random_u64 = croft_wit_wasi_dispatch_get_insecure_random_u64,
};

static const SapWitRandomInsecureSeedDispatchOps g_croft_wit_wasi_random_insecure_seed_ops = {
    .insecure_seed = croft_wit_wasi_dispatch_insecure_seed,
};

static const SapWitClocksMonotonicClockDispatchOps g_croft_wit_wasi_monotonic_ops = {
    .now = croft_wit_wasi_dispatch_monotonic_now,
    .subscribe = croft_wit_wasi_dispatch_monotonic_subscribe,
};

static const SapWitClocksWallClockDispatchOps g_croft_wit_wasi_wall_clock_ops = {
    .now = croft_wit_wasi_dispatch_wall_clock_now,
    .resolution = croft_wit_wasi_dispatch_wall_clock_resolution,
};

static const SapWitClocksTimezoneDispatchOps g_croft_wit_wasi_timezone_ops = {
    .display = croft_wit_wasi_dispatch_timezone_display,
};

static const SapWitIoStreamsDispatchOps g_croft_wit_wasi_io_streams_ops = {
    .read = croft_wit_wasi_dispatch_io_streams_read,
    .blocking_read = croft_wit_wasi_dispatch_io_streams_blocking_read,
    .skip = croft_wit_wasi_dispatch_io_streams_skip,
    .blocking_skip = croft_wit_wasi_dispatch_io_streams_blocking_skip,
    .input_stream_subscribe = croft_wit_wasi_dispatch_io_streams_input_stream_subscribe,
    .check_write = croft_wit_wasi_dispatch_io_streams_check_write,
    .write = croft_wit_wasi_dispatch_io_streams_write,
    .blocking_write_and_flush = croft_wit_wasi_dispatch_io_streams_blocking_write_and_flush,
    .flush = croft_wit_wasi_dispatch_io_streams_flush,
    .blocking_flush = croft_wit_wasi_dispatch_io_streams_blocking_flush,
    .output_stream_subscribe = croft_wit_wasi_dispatch_io_streams_output_stream_subscribe,
    .write_zeroes = croft_wit_wasi_dispatch_io_streams_write_zeroes,
    .blocking_write_zeroes_and_flush =
        croft_wit_wasi_dispatch_io_streams_blocking_write_zeroes_and_flush,
    .splice = croft_wit_wasi_dispatch_io_streams_splice,
    .blocking_splice = croft_wit_wasi_dispatch_io_streams_blocking_splice,
};

static const SapWitIoPollDispatchOps g_croft_wit_wasi_io_poll_ops = {
    .ready = croft_wit_wasi_dispatch_io_ready,
    .block = croft_wit_wasi_dispatch_io_block,
    .poll = croft_wit_wasi_dispatch_io_poll,
};

static int croft_wit_wasi_bind_endpoint(void* bindings,
                                        const SapWitWorldEndpointDescriptor* endpoints,
                                        uint32_t count,
                                        const char* item_name,
                                        void* ctx,
                                        const void* ops)
{
    const SapWitWorldEndpointDescriptor* endpoint =
        sap_wit_find_world_endpoint_descriptor(endpoints, count, item_name);
    if (!endpoint) {
        return ERR_INVALID;
    }
    return sap_wit_world_endpoint_bind(bindings, endpoint, ctx, ops);
}

#if !defined(CROFT_OS_WINDOWS)
static int croft_wit_wasi_init_preopens(
    croft_wit_wasi_machine_runtime* runtime,
    const croft_wit_wasi_machine_runtime_options* options)
{
    size_t i;

    if (!runtime || !options) {
        return ERR_INVALID;
    }
    if (options->preopen_count > 0u) {
        if (!options->preopens) {
            return ERR_INVALID;
        }
        for (i = 0u; i < options->preopen_count; i++) {
            const char* host_path = options->preopens[i].host_path;
            const char* guest_path = options->preopens[i].guest_path;
            int rc;

            if (!host_path) {
                return ERR_INVALID;
            }
            rc = croft_wit_wasi_fs_add_preopen(runtime, host_path, guest_path ? guest_path : host_path);
            if (rc != ERR_OK) {
                return rc;
            }
        }
    } else if (options->inherit_preopen_cwd) {
        char cwd[PATH_MAX];
        int rc;

        if (getcwd(cwd, sizeof(cwd))) {
            rc = croft_wit_wasi_fs_add_preopen(runtime, cwd, ".");
            if (rc != ERR_OK) {
                return rc;
            }
        }
    }
    return croft_wit_wasi_fs_build_preopen_blob(runtime);
}
#endif

void croft_wit_wasi_machine_runtime_options_default(
    croft_wit_wasi_machine_runtime_options* options)
{
    if (!options) {
        return;
    }
    memset(options, 0, sizeof(*options));
    options->inherit_environment = 1u;
    options->inherit_preopen_cwd = 1u;
}

croft_wit_wasi_machine_runtime* croft_wit_wasi_machine_runtime_create(
    const croft_wit_wasi_machine_runtime_options* options)
{
    croft_wit_wasi_machine_runtime_options local = {0};
    croft_wit_wasi_machine_runtime* runtime;
    const char* const* envp = NULL;
    uint32_t envc = 0u;
    int rc;

    croft_wit_wasi_machine_runtime_options_default(&local);
    if (options) {
        local = *options;
    }

    runtime = (croft_wit_wasi_machine_runtime*)calloc(1u, sizeof(*runtime));
    if (!runtime) {
        return NULL;
    }
    runtime->next_pollable_handle = 1u;

    rc = croft_wit_wasi_build_argv_blob(runtime, local.argv, local.argc);
    if (rc != ERR_OK) {
        croft_wit_wasi_machine_runtime_destroy(runtime);
        return NULL;
    }

    if (local.envp) {
        envp = local.envp;
        envc = local.envc;
    } else if (local.inherit_environment) {
        envp = (const char* const*)environ;
        envc = 0u;
    }

    rc = croft_wit_wasi_build_env_blob(runtime, envp, envc);
    if (rc != ERR_OK) {
        croft_wit_wasi_machine_runtime_destroy(runtime);
        return NULL;
    }

    rc = croft_wit_wasi_init_initial_cwd(runtime, local.initial_cwd);
    if (rc != ERR_OK) {
        croft_wit_wasi_machine_runtime_destroy(runtime);
        return NULL;
    }

#if !defined(CROFT_OS_WINDOWS)
    rc = croft_wit_wasi_init_preopens(runtime, &local);
    if (rc != ERR_OK) {
        croft_wit_wasi_machine_runtime_destroy(runtime);
        return NULL;
    }
#endif

    return runtime;
}

void croft_wit_wasi_machine_runtime_destroy(croft_wit_wasi_machine_runtime* runtime)
{
    size_t i;

    if (!runtime) {
        return;
    }

#if !defined(CROFT_OS_WINDOWS)
    for (i = 0u; i < runtime->directory_stream_count; i++) {
        if (runtime->directory_streams[i].live && runtime->directory_streams[i].dir) {
            closedir(runtime->directory_streams[i].dir);
        }
    }
    for (i = 0u; i < runtime->descriptor_count; i++) {
        if (runtime->descriptors[i].live && runtime->descriptors[i].fd >= 0) {
            close(runtime->descriptors[i].fd);
        }
    }
    for (i = 0u; i < runtime->preopen_count; i++) {
        free(runtime->preopens[i].guest_path);
    }
#endif
    free(runtime->env_blob.data);
    free(runtime->argv_blob.data);
    free(runtime->random_blob.data);
    free(runtime->poll_indexes_blob.data);
    free(runtime->filesystem_blob.data);
    free(runtime->preopen_blob.data);
    free(runtime->initial_cwd);
    free(runtime->pollables);
    free(runtime->descriptors);
    free(runtime->directory_streams);
    free(runtime->preopens);
    free(runtime);
}

int croft_wit_wasi_machine_runtime_bind_cli_command_imports(
    croft_wit_wasi_machine_runtime* runtime,
    SapWitCliCommandWorldImports* bindings)
{
    int rc;

    if (!runtime || !bindings) {
        return ERR_INVALID;
    }
    memset(bindings, 0, sizeof(*bindings));
    rc = croft_wit_wasi_bind_endpoint(bindings,
                                      sap_wit_cli_command_import_endpoints,
                                      sap_wit_cli_command_import_endpoints_count,
                                      "environment",
                                      runtime,
                                      &g_croft_wit_wasi_cli_environment_ops);
    return rc;
}

int croft_wit_wasi_machine_runtime_bind_cli_imports(
    croft_wit_wasi_machine_runtime* runtime,
    SapWitCliImportsWorldImports* bindings)
{
    int rc;

    if (!runtime || !bindings) {
        return ERR_INVALID;
    }
    memset(bindings, 0, sizeof(*bindings));
    rc = croft_wit_wasi_bind_endpoint(bindings,
                                      sap_wit_cli_imports_import_endpoints,
                                      sap_wit_cli_imports_import_endpoints_count,
                                      "environment",
                                      runtime,
                                      &g_croft_wit_wasi_cli_environment_ops);
    return rc;
}

int croft_wit_wasi_machine_runtime_bind_random_imports(
    croft_wit_wasi_machine_runtime* runtime,
    SapWitRandomImportsWorldImports* bindings)
{
    int rc;

    if (!runtime || !bindings) {
        return ERR_INVALID;
    }
    memset(bindings, 0, sizeof(*bindings));

    rc = croft_wit_wasi_bind_endpoint(bindings,
                                      sap_wit_random_imports_import_endpoints,
                                      sap_wit_random_imports_import_endpoints_count,
                                      "random",
                                      runtime,
                                      &g_croft_wit_wasi_random_ops);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_wit_wasi_bind_endpoint(bindings,
                                      sap_wit_random_imports_import_endpoints,
                                      sap_wit_random_imports_import_endpoints_count,
                                      "insecure",
                                      runtime,
                                      &g_croft_wit_wasi_random_insecure_ops);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_wit_wasi_bind_endpoint(bindings,
                                        sap_wit_random_imports_import_endpoints,
                                        sap_wit_random_imports_import_endpoints_count,
                                        "insecure-seed",
                                        runtime,
                                        &g_croft_wit_wasi_random_insecure_seed_ops);
}

int croft_wit_wasi_machine_runtime_bind_clocks_imports(
    croft_wit_wasi_machine_runtime* runtime,
    SapWitClocksImportsWorldImports* bindings)
{
    int rc;

    if (!runtime || !bindings) {
        return ERR_INVALID;
    }
    memset(bindings, 0, sizeof(*bindings));

    rc = croft_wit_wasi_bind_endpoint(bindings,
                                      sap_wit_clocks_imports_import_endpoints,
                                      sap_wit_clocks_imports_import_endpoints_count,
                                      "monotonic-clock",
                                      runtime,
                                      &g_croft_wit_wasi_monotonic_ops);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_wit_wasi_bind_endpoint(bindings,
                                      sap_wit_clocks_imports_import_endpoints,
                                      sap_wit_clocks_imports_import_endpoints_count,
                                      "wall-clock",
                                      runtime,
                                      &g_croft_wit_wasi_wall_clock_ops);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_wit_wasi_bind_endpoint(bindings,
                                        sap_wit_clocks_imports_import_endpoints,
                                        sap_wit_clocks_imports_import_endpoints_count,
                                        "timezone",
                                        runtime,
                                        &g_croft_wit_wasi_timezone_ops);
}

int croft_wit_wasi_machine_runtime_bind_io_imports(
    croft_wit_wasi_machine_runtime* runtime,
    SapWitIoImportsWorldImports* bindings)
{
    int rc;

    if (!runtime || !bindings) {
        return ERR_INVALID;
    }
    memset(bindings, 0, sizeof(*bindings));
    rc = croft_wit_wasi_bind_endpoint(bindings,
                                      sap_wit_io_imports_import_endpoints,
                                      sap_wit_io_imports_import_endpoints_count,
                                      "streams",
                                      runtime,
                                      &g_croft_wit_wasi_io_streams_ops);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_wit_wasi_bind_endpoint(bindings,
                                        sap_wit_io_imports_import_endpoints,
                                        sap_wit_io_imports_import_endpoints_count,
                                        "poll",
                                        runtime,
                                        &g_croft_wit_wasi_io_poll_ops);
}

int croft_wit_wasi_machine_runtime_bind_filesystem_imports(
    croft_wit_wasi_machine_runtime* runtime,
    SapWitFilesystemImportsWorldImports* bindings)
{
    int rc;

    if (!runtime || !bindings) {
        return ERR_INVALID;
    }
#if defined(CROFT_OS_WINDOWS)
    (void)runtime;
    (void)bindings;
    return ERR_UNSUPPORTED;
#else
    memset(bindings, 0, sizeof(*bindings));
    rc = croft_wit_wasi_bind_endpoint(bindings,
                                      sap_wit_filesystem_imports_import_endpoints,
                                      sap_wit_filesystem_imports_import_endpoints_count,
                                      "types",
                                      runtime,
                                      &g_croft_wit_wasi_filesystem_types_ops);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_wit_wasi_bind_endpoint(bindings,
                                        sap_wit_filesystem_imports_import_endpoints,
                                        sap_wit_filesystem_imports_import_endpoints_count,
                                        "preopens",
                                        runtime,
                                        &g_croft_wit_wasi_filesystem_preopens_ops);
#endif
}
