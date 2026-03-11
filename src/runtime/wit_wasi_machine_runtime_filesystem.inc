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

static int croft_wit_wasi_error_insert_filesystem_code(croft_wit_wasi_machine_runtime* runtime,
                                                       uint8_t filesystem_error_code,
                                                       const char* debug_string,
                                                       uint32_t* handle_out)
{
    return croft_wit_wasi_error_insert(runtime,
                                       CROFT_WIT_WASI_ERROR_DOMAIN_FILESYSTEM,
                                       filesystem_error_code,
                                       debug_string,
                                       handle_out);
}

static int croft_wit_wasi_error_insert_filesystem_errno(croft_wit_wasi_machine_runtime* runtime,
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
    return croft_wit_wasi_error_insert_filesystem_code(runtime,
                                                       croft_wit_wasi_fs_error_from_errno(err),
                                                       debug_string,
                                                       handle_out);
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

static void croft_wit_wasi_fd_detect_traits(int fd,
                                            uint8_t is_directory,
                                            uint8_t* is_seekable_out,
                                            uint8_t* is_terminal_out)
{
    uint8_t is_seekable = 0u;
    uint8_t is_terminal = 0u;

    if (fd >= 0 && !is_directory) {
        errno = 0;
        if (lseek(fd, 0, SEEK_CUR) >= 0 || errno != ESPIPE) {
            is_seekable = 1u;
        }
        is_terminal = (uint8_t)(isatty(fd) ? 1u : 0u);
    }

    if (is_seekable_out) {
        *is_seekable_out = is_seekable;
    }
    if (is_terminal_out) {
        *is_terminal_out = is_terminal;
    }
}

static int croft_wit_wasi_fs_descriptor_insert(croft_wit_wasi_machine_runtime* runtime,
                                               int fd,
                                               uint32_t descriptor_flags,
                                               uint8_t is_directory,
                                               uint32_t* handle_out)
{
    size_t i;
    uint8_t is_seekable = 0u;
    uint8_t is_terminal = 0u;
    int rc;

    if (!runtime || !handle_out || fd < 0) {
        return ERR_INVALID;
    }
    croft_wit_wasi_fd_detect_traits(fd, is_directory, &is_seekable, &is_terminal);
    for (i = 0u; i < runtime->descriptor_count; i++) {
        if (!runtime->descriptors[i].live) {
            runtime->descriptors[i].live = 1u;
            runtime->descriptors[i].fd = fd;
            runtime->descriptors[i].descriptor_flags = descriptor_flags;
            runtime->descriptors[i].is_directory = is_directory;
            runtime->descriptors[i].is_seekable = is_seekable;
            runtime->descriptors[i].is_terminal = is_terminal;
            runtime->descriptors[i].error_domain = CROFT_WIT_WASI_ERROR_DOMAIN_FILESYSTEM;
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
    runtime->descriptors[runtime->descriptor_count].is_seekable = is_seekable;
    runtime->descriptors[runtime->descriptor_count].is_terminal = is_terminal;
    runtime->descriptors[runtime->descriptor_count].error_domain = CROFT_WIT_WASI_ERROR_DOMAIN_FILESYSTEM;
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

static int croft_wit_wasi_input_streams_reserve(croft_wit_wasi_machine_runtime* runtime,
                                                size_t needed)
{
    croft_wit_wasi_input_stream_slot* next_slots;
    size_t next_cap;

    if (!runtime) {
        return ERR_INVALID;
    }
    if (runtime->input_stream_cap >= needed) {
        return ERR_OK;
    }

    next_cap = runtime->input_stream_cap ? runtime->input_stream_cap * 2u : 4u;
    while (next_cap < needed) {
        next_cap *= 2u;
    }

    next_slots = (croft_wit_wasi_input_stream_slot*)realloc(runtime->input_streams,
                                                            next_cap * sizeof(*next_slots));
    if (!next_slots) {
        return ERR_OOM;
    }
    memset(next_slots + runtime->input_stream_cap,
           0,
           (next_cap - runtime->input_stream_cap) * sizeof(*next_slots));
    runtime->input_streams = next_slots;
    runtime->input_stream_cap = next_cap;
    return ERR_OK;
}

static int croft_wit_wasi_input_stream_insert(croft_wit_wasi_machine_runtime* runtime,
                                              uint32_t descriptor_handle,
                                              uint64_t offset,
                                              uint32_t* handle_out)
{
    size_t i;
    int rc;

    if (!runtime || !handle_out || descriptor_handle == 0u) {
        return ERR_INVALID;
    }
    for (i = 0u; i < runtime->input_stream_count; i++) {
        if (!runtime->input_streams[i].live) {
            runtime->input_streams[i].live = 1u;
            runtime->input_streams[i].descriptor_handle = descriptor_handle;
            runtime->input_streams[i].offset = offset;
            runtime->input_streams[i].error_handle = 0u;
            runtime->input_streams[i].closed = 0u;
            *handle_out = (uint32_t)(i + 1u);
            return ERR_OK;
        }
    }

    rc = croft_wit_wasi_input_streams_reserve(runtime, runtime->input_stream_count + 1u);
    if (rc != ERR_OK) {
        return rc;
    }
    runtime->input_streams[runtime->input_stream_count].live = 1u;
    runtime->input_streams[runtime->input_stream_count].descriptor_handle = descriptor_handle;
    runtime->input_streams[runtime->input_stream_count].offset = offset;
    runtime->input_streams[runtime->input_stream_count].error_handle = 0u;
    runtime->input_streams[runtime->input_stream_count].closed = 0u;
    runtime->input_stream_count++;
    *handle_out = (uint32_t)runtime->input_stream_count;
    return ERR_OK;
}

static croft_wit_wasi_input_stream_slot* croft_wit_wasi_input_stream_lookup(
    croft_wit_wasi_machine_runtime* runtime,
    uint32_t handle)
{
    size_t index;

    if (!runtime || handle == 0u) {
        return NULL;
    }
    index = (size_t)handle - 1u;
    if (index >= runtime->input_stream_count || !runtime->input_streams[index].live) {
        return NULL;
    }
    return &runtime->input_streams[index];
}

static int croft_wit_wasi_output_streams_reserve(croft_wit_wasi_machine_runtime* runtime,
                                                 size_t needed)
{
    croft_wit_wasi_output_stream_slot* next_slots;
    size_t next_cap;

    if (!runtime) {
        return ERR_INVALID;
    }
    if (runtime->output_stream_cap >= needed) {
        return ERR_OK;
    }

    next_cap = runtime->output_stream_cap ? runtime->output_stream_cap * 2u : 4u;
    while (next_cap < needed) {
        next_cap *= 2u;
    }

    next_slots = (croft_wit_wasi_output_stream_slot*)realloc(runtime->output_streams,
                                                             next_cap * sizeof(*next_slots));
    if (!next_slots) {
        return ERR_OOM;
    }
    memset(next_slots + runtime->output_stream_cap,
           0,
           (next_cap - runtime->output_stream_cap) * sizeof(*next_slots));
    runtime->output_streams = next_slots;
    runtime->output_stream_cap = next_cap;
    return ERR_OK;
}

static int croft_wit_wasi_output_stream_insert(croft_wit_wasi_machine_runtime* runtime,
                                               uint32_t descriptor_handle,
                                               uint64_t offset,
                                               uint8_t append,
                                               uint32_t* handle_out)
{
    size_t i;
    int rc;

    if (!runtime || !handle_out || descriptor_handle == 0u) {
        return ERR_INVALID;
    }
    for (i = 0u; i < runtime->output_stream_count; i++) {
        if (!runtime->output_streams[i].live) {
            runtime->output_streams[i].live = 1u;
            runtime->output_streams[i].descriptor_handle = descriptor_handle;
            runtime->output_streams[i].offset = offset;
            runtime->output_streams[i].append = append;
            runtime->output_streams[i].error_handle = 0u;
            runtime->output_streams[i].closed = 0u;
            *handle_out = (uint32_t)(i + 1u);
            return ERR_OK;
        }
    }

    rc = croft_wit_wasi_output_streams_reserve(runtime, runtime->output_stream_count + 1u);
    if (rc != ERR_OK) {
        return rc;
    }
    runtime->output_streams[runtime->output_stream_count].live = 1u;
    runtime->output_streams[runtime->output_stream_count].descriptor_handle = descriptor_handle;
    runtime->output_streams[runtime->output_stream_count].offset = offset;
    runtime->output_streams[runtime->output_stream_count].append = append;
    runtime->output_streams[runtime->output_stream_count].error_handle = 0u;
    runtime->output_streams[runtime->output_stream_count].closed = 0u;
    runtime->output_stream_count++;
    *handle_out = (uint32_t)runtime->output_stream_count;
    return ERR_OK;
}

static croft_wit_wasi_output_stream_slot* croft_wit_wasi_output_stream_lookup(
    croft_wit_wasi_machine_runtime* runtime,
    uint32_t handle)
{
    size_t index;

    if (!runtime || handle == 0u) {
        return NULL;
    }
    index = (size_t)handle - 1u;
    if (index >= runtime->output_stream_count || !runtime->output_streams[index].live) {
        return NULL;
    }
    return &runtime->output_streams[index];
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

static void croft_wit_wasi_fs_types_reply_input_stream_ok(SapWitFilesystemTypesReply* reply_out,
                                                          uint32_t handle)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_INPUT_STREAM;
    reply_out->val.input_stream.is_v_ok = 1u;
    reply_out->val.input_stream.v_val.ok.v = (SapWitFilesystemInputStreamResource)handle;
}

static void croft_wit_wasi_fs_types_reply_output_stream_err(SapWitFilesystemTypesReply* reply_out,
                                                            uint8_t error_code)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_OUTPUT_STREAM;
    reply_out->val.output_stream.is_v_ok = 0u;
    reply_out->val.output_stream.v_val.err.v = error_code;
}

static void croft_wit_wasi_fs_types_reply_output_stream_ok(SapWitFilesystemTypesReply* reply_out,
                                                           uint32_t handle)
{
    sap_wit_zero_filesystem_types_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_TYPES_REPLY_OUTPUT_STREAM;
    reply_out->val.output_stream.is_v_ok = 1u;
    reply_out->val.output_stream.v_val.ok.v = (SapWitFilesystemOutputStreamResource)handle;
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
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* descriptor;
    uint32_t handle = 0u;
    int rc;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    descriptor = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!descriptor) {
        croft_wit_wasi_fs_types_reply_input_stream_err(reply_out,
                                                       SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    if (descriptor->is_directory
        || (descriptor->descriptor_flags & SAP_WIT_FILESYSTEM_DESCRIPTOR_FLAGS_READ) == 0u) {
        croft_wit_wasi_fs_types_reply_input_stream_err(reply_out,
                                                       SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    rc = croft_wit_wasi_input_stream_insert(runtime, payload->descriptor, payload->offset, &handle);
    if (rc != ERR_OK) {
        return rc;
    }
    croft_wit_wasi_fs_types_reply_input_stream_ok(reply_out, handle);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_write_via_stream(
    void* ctx,
    const SapWitFilesystemDescriptorWriteViaStream* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* descriptor;
    uint32_t handle = 0u;
    int rc;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    descriptor = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!descriptor) {
        croft_wit_wasi_fs_types_reply_output_stream_err(
            reply_out,
            SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    if (descriptor->is_directory
        || (descriptor->descriptor_flags & SAP_WIT_FILESYSTEM_DESCRIPTOR_FLAGS_WRITE) == 0u) {
        croft_wit_wasi_fs_types_reply_output_stream_err(
            reply_out,
            SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    rc = croft_wit_wasi_output_stream_insert(runtime,
                                             payload->descriptor,
                                             payload->offset,
                                             0u,
                                             &handle);
    if (rc != ERR_OK) {
        return rc;
    }
    croft_wit_wasi_fs_types_reply_output_stream_ok(reply_out, handle);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_append_via_stream(
    void* ctx,
    const SapWitFilesystemDescriptorAppendViaStream* payload,
    SapWitFilesystemTypesReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* descriptor;
    uint32_t handle = 0u;
    int rc;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    descriptor = croft_wit_wasi_fs_descriptor_lookup(runtime, payload->descriptor);
    if (!descriptor) {
        croft_wit_wasi_fs_types_reply_output_stream_err(
            reply_out,
            SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    if (descriptor->is_directory
        || (descriptor->descriptor_flags & SAP_WIT_FILESYSTEM_DESCRIPTOR_FLAGS_WRITE) == 0u) {
        croft_wit_wasi_fs_types_reply_output_stream_err(
            reply_out,
            SAP_WIT_FILESYSTEM_ERROR_CODE_BAD_DESCRIPTOR);
        return ERR_OK;
    }
    rc = croft_wit_wasi_output_stream_insert(runtime, payload->descriptor, 0u, 1u, &handle);
    if (rc != ERR_OK) {
        return rc;
    }
    croft_wit_wasi_fs_types_reply_output_stream_ok(reply_out, handle);
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
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_error_slot* error_slot;

    if (!runtime || !payload) {
        return ERR_INVALID;
    }
    if (!reply_out) {
        return ERR_INVALID;
    }
    error_slot = croft_wit_wasi_error_lookup(runtime, payload->err);
    if (!error_slot || error_slot->domain != CROFT_WIT_WASI_ERROR_DOMAIN_FILESYSTEM) {
        croft_wit_wasi_fs_types_reply_filesystem_error_code(reply_out, 0u, 0u);
        return ERR_OK;
    }
    croft_wit_wasi_fs_types_reply_filesystem_error_code(
        reply_out,
        1u,
        error_slot->filesystem_error_code);
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_fs_error_to_debug_string(
    void* ctx,
    const SapWitFilesystemErrorToDebugString* payload,
    SapWitFilesystemErrorReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_error_slot* error_slot;
    const char* debug_string = "";

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    error_slot = croft_wit_wasi_error_lookup(runtime, payload->error);
    if (!error_slot) {
        return ERR_INVALID;
    }
    if (error_slot->debug_string) {
        debug_string = error_slot->debug_string;
    }

    sap_wit_zero_filesystem_error_reply(reply_out);
    reply_out->case_tag = SAP_WIT_FILESYSTEM_ERROR_REPLY_TO_DEBUG_STRING;
    reply_out->val.to_debug_string.data = (const uint8_t*)debug_string;
    reply_out->val.to_debug_string.len = (uint32_t)strlen(debug_string);
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

static const croft_wit_wasi_endpoint_binding g_croft_wit_wasi_filesystem_bundle_bindings[] = {
    {"types", &g_croft_wit_wasi_filesystem_types_ops},
    {"preopens", &g_croft_wit_wasi_filesystem_preopens_ops},
};

static const SapWitFilesystemErrorDispatchOps g_croft_wit_wasi_filesystem_error_ops = {
    .to_debug_string = croft_wit_wasi_dispatch_fs_error_to_debug_string,
};
#endif

int32_t croft_wit_wasi_machine_runtime_dispatch_filesystem_error(
    croft_wit_wasi_machine_runtime* runtime,
    const SapWitFilesystemErrorCommand* command,
    SapWitFilesystemErrorReply* reply_out)
{
#if defined(CROFT_OS_WINDOWS)
    (void)runtime;
    (void)command;
    (void)reply_out;
    return ERR_UNSUPPORTED;
#else
    if (!runtime || !command || !reply_out) {
        return ERR_INVALID;
    }
    return sap_wit_dispatch_filesystem_error(runtime, &g_croft_wit_wasi_filesystem_error_ops, command, reply_out);
#endif
}


int croft_wit_wasi_machine_runtime_bind_filesystem_imports(
    croft_wit_wasi_machine_runtime* runtime,
    SapWitFilesystemImportsWorldImports* bindings)
{
    if (!runtime || !bindings) {
        return ERR_INVALID;
    }
#if defined(CROFT_OS_WINDOWS)
    (void)runtime;
    (void)bindings;
    return ERR_UNSUPPORTED;
#else
    memset(bindings, 0, sizeof(*bindings));
    return croft_wit_wasi_bind_endpoint_group(
        bindings,
        sap_wit_filesystem_imports_import_endpoints,
        sap_wit_filesystem_imports_import_endpoints_count,
        runtime,
        g_croft_wit_wasi_filesystem_bundle_bindings,
        sizeof(g_croft_wit_wasi_filesystem_bundle_bindings)
            / sizeof(g_croft_wit_wasi_filesystem_bundle_bindings[0]));
#endif
}
