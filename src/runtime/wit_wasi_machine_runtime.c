#include "croft/wit_wasi_machine_runtime.h"

#include "croft/platform.h"

#include "sapling/err.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

struct croft_wit_wasi_machine_runtime {
    croft_wit_wasi_byte_buffer env_blob;
    croft_wit_wasi_byte_buffer argv_blob;
    croft_wit_wasi_byte_buffer random_blob;
    croft_wit_wasi_byte_buffer poll_indexes_blob;
    char* initial_cwd;
    char timezone_name[64];
    uint32_t env_count;
    uint32_t argc;
    croft_wit_wasi_pollable_slot* pollables;
    size_t pollable_count;
    size_t pollable_cap;
    uint32_t next_pollable_handle;
};

#if !defined(CROFT_OS_WINDOWS)
extern char** environ;
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

void croft_wit_wasi_machine_runtime_options_default(
    croft_wit_wasi_machine_runtime_options* options)
{
    if (!options) {
        return;
    }
    memset(options, 0, sizeof(*options));
    options->inherit_environment = 1u;
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

    return runtime;
}

void croft_wit_wasi_machine_runtime_destroy(croft_wit_wasi_machine_runtime* runtime)
{
    if (!runtime) {
        return;
    }

    free(runtime->env_blob.data);
    free(runtime->argv_blob.data);
    free(runtime->random_blob.data);
    free(runtime->poll_indexes_blob.data);
    free(runtime->initial_cwd);
    free(runtime->pollables);
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
    if (!runtime || !bindings) {
        return ERR_INVALID;
    }
    memset(bindings, 0, sizeof(*bindings));
    return croft_wit_wasi_bind_endpoint(bindings,
                                        sap_wit_io_imports_import_endpoints,
                                        sap_wit_io_imports_import_endpoints_count,
                                        "poll",
                                        runtime,
                                        &g_croft_wit_wasi_io_poll_ops);
}
