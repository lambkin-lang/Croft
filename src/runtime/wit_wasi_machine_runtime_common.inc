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

static int croft_wit_wasi_error_slots_reserve(croft_wit_wasi_machine_runtime* runtime, size_t needed)
{
    croft_wit_wasi_error_slot* next_slots;
    size_t next_cap;

    if (!runtime) {
        return ERR_INVALID;
    }
    if (runtime->error_cap >= needed) {
        return ERR_OK;
    }

    next_cap = runtime->error_cap ? runtime->error_cap * 2u : 4u;
    while (next_cap < needed) {
        next_cap *= 2u;
    }

    next_slots =
        (croft_wit_wasi_error_slot*)realloc(runtime->errors, next_cap * sizeof(*next_slots));
    if (!next_slots) {
        return ERR_OOM;
    }
    memset(next_slots + runtime->error_cap, 0, (next_cap - runtime->error_cap) * sizeof(*next_slots));
    runtime->errors = next_slots;
    runtime->error_cap = next_cap;
    return ERR_OK;
}

static croft_wit_wasi_error_slot* croft_wit_wasi_error_lookup(
    croft_wit_wasi_machine_runtime* runtime,
    uint32_t handle)
{
    size_t index;

    if (!runtime || handle == 0u) {
        return NULL;
    }
    index = (size_t)handle - 1u;
    if (index >= runtime->error_count || !runtime->errors[index].live) {
        return NULL;
    }
    return &runtime->errors[index];
}

static int croft_wit_wasi_error_insert(croft_wit_wasi_machine_runtime* runtime,
                                       uint8_t domain,
                                       uint8_t filesystem_error_code,
                                       const char* debug_string,
                                       uint32_t* handle_out)
{
    char* debug_copy;
    size_t i;
    int rc;

    if (!runtime || !handle_out) {
        return ERR_INVALID;
    }

    debug_copy = croft_wit_wasi_copy_string(debug_string ? debug_string : "");
    if (!debug_copy) {
        return ERR_OOM;
    }

    for (i = 0u; i < runtime->error_count; i++) {
        if (!runtime->errors[i].live) {
            runtime->errors[i].live = 1u;
            runtime->errors[i].domain = domain;
            runtime->errors[i].filesystem_error_code = filesystem_error_code;
            free(runtime->errors[i].debug_string);
            runtime->errors[i].debug_string = debug_copy;
            *handle_out = (uint32_t)(i + 1u);
            return ERR_OK;
        }
    }

    rc = croft_wit_wasi_error_slots_reserve(runtime, runtime->error_count + 1u);
    if (rc != ERR_OK) {
        free(debug_copy);
        return rc;
    }
    runtime->errors[runtime->error_count].live = 1u;
    runtime->errors[runtime->error_count].domain = domain;
    runtime->errors[runtime->error_count].filesystem_error_code = filesystem_error_code;
    runtime->errors[runtime->error_count].debug_string = debug_copy;
    runtime->error_count++;
    *handle_out = (uint32_t)runtime->error_count;
    return ERR_OK;
}

static int croft_wit_wasi_error_insert_io_message(croft_wit_wasi_machine_runtime* runtime,
                                                  const char* debug_string,
                                                  uint32_t* handle_out)
{
    return croft_wit_wasi_error_insert(runtime,
                                       CROFT_WIT_WASI_ERROR_DOMAIN_IO,
                                       0u,
                                       debug_string,
                                       handle_out);
}

static int croft_wit_wasi_error_insert_io_errno(croft_wit_wasi_machine_runtime* runtime,
                                                const char* op_name,
                                                int err,
                                                uint32_t* handle_out)
{
    char debug_string[256];
    const char* label = op_name ? op_name : "operation";
    const char* err_text = strerror(err);
    int wrote;

    if (!err_text) {
        err_text = "unknown error";
    }
    wrote = snprintf(debug_string, sizeof(debug_string), "%s: %s", label, err_text);
    if (wrote < 0) {
        return ERR_INVALID;
    }
    return croft_wit_wasi_error_insert_io_message(runtime, debug_string, handle_out);
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
