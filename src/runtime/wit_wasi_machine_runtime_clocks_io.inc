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

static int32_t croft_wit_wasi_dispatch_monotonic_resolution(
    void* ctx,
    SapWitClocksMonotonicClockReply* reply_out)
{
    struct timespec ts = {0};
    uint64_t resolution_ns = 0u;

    (void)ctx;
    if (!reply_out) {
        return ERR_INVALID;
    }
    if (clock_getres(CLOCK_MONOTONIC, &ts) != 0) {
        return ERR_INVALID;
    }
    resolution_ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;

    sap_wit_zero_clocks_monotonic_clock_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLOCKS_MONOTONIC_CLOCK_REPLY_RESOLUTION;
    reply_out->val.resolution = resolution_ns;
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_monotonic_subscribe_instant(
    void* ctx,
    const SapWitClocksSubscribeInstant* payload,
    SapWitClocksMonotonicClockReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    uint32_t handle = 0u;
    int rc;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    rc = croft_wit_wasi_pollable_insert(runtime, payload->when, &handle);
    if (rc != ERR_OK) {
        return rc;
    }

    sap_wit_zero_clocks_monotonic_clock_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLOCKS_MONOTONIC_CLOCK_REPLY_POLLABLE;
    reply_out->val.pollable = (SapWitClocksPollableResource)handle;
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_monotonic_subscribe_duration(
    void* ctx,
    const SapWitClocksSubscribeDuration* payload,
    SapWitClocksMonotonicClockReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    uint64_t now_ns = 0u;
    uint64_t due_ns = 0u;
    uint32_t handle = 0u;
    int rc;

    if (!runtime || !payload || !reply_out) {
        return ERR_INVALID;
    }
    rc = croft_wit_wasi_clock_get_ns(CLOCK_MONOTONIC, &now_ns);
    if (rc != ERR_OK) {
        return rc;
    }
    due_ns = (UINT64_MAX - now_ns < payload->when) ? UINT64_MAX : (now_ns + payload->when);
    rc = croft_wit_wasi_pollable_insert(runtime, due_ns, &handle);
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

static int croft_wit_wasi_u64_to_off_t(uint64_t offset, off_t* out)
{
    if (!out) {
        return ERR_INVALID;
    }
    if (offset > (uint64_t)LLONG_MAX) {
        return ERR_RANGE;
    }
    *out = (off_t)offset;
    return ERR_OK;
}

static int croft_wit_wasi_fd_size(int fd, uint64_t* out, int* err_out)
{
    struct stat st;

    if (err_out) {
        *err_out = 0;
    }
    if (fd < 0 || !out) {
        return ERR_INVALID;
    }
    if (fstat(fd, &st) != 0) {
        if (err_out) {
            *err_out = errno;
        }
        return ERR_INVALID;
    }
    if (st.st_size < 0) {
        return ERR_RANGE;
    }
    *out = (uint64_t)st.st_size;
    return ERR_OK;
}

static int croft_wit_wasi_make_ready_pollable(croft_wit_wasi_machine_runtime* runtime,
                                              uint32_t* handle_out)
{
    uint64_t now_ns = 0u;
    int rc;

    if (!runtime || !handle_out) {
        return ERR_INVALID;
    }
    rc = croft_wit_wasi_clock_get_ns(CLOCK_MONOTONIC, &now_ns);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_wit_wasi_pollable_insert(runtime, now_ns, handle_out);
}

static int croft_wit_wasi_stream_error_insert_errno(croft_wit_wasi_machine_runtime* runtime,
                                                    const croft_wit_wasi_descriptor_slot* descriptor,
                                                    const char* op_name,
                                                    int err,
                                                    uint32_t* handle_out)
{
    if (descriptor && descriptor->error_domain == CROFT_WIT_WASI_ERROR_DOMAIN_IO) {
        return croft_wit_wasi_error_insert_io_errno(runtime, op_name, err, handle_out);
    }
    return croft_wit_wasi_error_insert_filesystem_errno(runtime, op_name, err, handle_out);
}

static int croft_wit_wasi_stream_error_insert_message(
    croft_wit_wasi_machine_runtime* runtime,
    const croft_wit_wasi_descriptor_slot* descriptor,
    uint8_t filesystem_error_code,
    const char* debug_string,
    uint32_t* handle_out)
{
    if (descriptor && descriptor->error_domain == CROFT_WIT_WASI_ERROR_DOMAIN_IO) {
        return croft_wit_wasi_error_insert_io_message(runtime, debug_string, handle_out);
    }
    return croft_wit_wasi_error_insert_filesystem_code(runtime,
                                                       filesystem_error_code,
                                                       debug_string,
                                                       handle_out);
}

static void croft_wit_wasi_set_stream_error_closed(SapWitIoStreamError* out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->case_tag = SAP_WIT_IO_STREAM_ERROR_CLOSED;
}

static void croft_wit_wasi_set_stream_error_failed(SapWitIoStreamError* out, uint32_t error_handle)
{
    if (!out || error_handle == 0u) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->case_tag = SAP_WIT_IO_STREAM_ERROR_LAST_OPERATION_FAILED;
    out->val.last_operation_failed = (SapWitIoErrorResource)error_handle;
}

static int32_t croft_wit_wasi_io_streams_reply_read_ok(SapWitIoStreamsReply* reply_out,
                                                        uint8_t blocking,
                                                        const uint8_t* data,
                                                        uint32_t len)
{
    if (!reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_io_streams_reply(reply_out);
    reply_out->case_tag = blocking ? SAP_WIT_IO_STREAMS_REPLY_BLOCKING_READ
                                   : SAP_WIT_IO_STREAMS_REPLY_READ;
    if (blocking) {
        reply_out->val.blocking_read.is_v_ok = 1u;
        reply_out->val.blocking_read.v_val.ok.v_data = data;
        reply_out->val.blocking_read.v_val.ok.v_len = len;
    } else {
        reply_out->val.read.is_v_ok = 1u;
        reply_out->val.read.v_val.ok.v_data = data;
        reply_out->val.read.v_val.ok.v_len = len;
    }
    return ERR_OK;
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

static int32_t croft_wit_wasi_io_streams_reply_read_error(SapWitIoStreamsReply* reply_out,
                                                           uint8_t blocking,
                                                           uint32_t error_handle)
{
    if (!reply_out || error_handle == 0u) {
        return ERR_INVALID;
    }
    sap_wit_zero_io_streams_reply(reply_out);
    reply_out->case_tag = blocking ? SAP_WIT_IO_STREAMS_REPLY_BLOCKING_READ
                                   : SAP_WIT_IO_STREAMS_REPLY_READ;
    if (blocking) {
        reply_out->val.blocking_read.is_v_ok = 0u;
        croft_wit_wasi_set_stream_error_failed(&reply_out->val.blocking_read.v_val.err.v,
                                               error_handle);
    } else {
        reply_out->val.read.is_v_ok = 0u;
        croft_wit_wasi_set_stream_error_failed(&reply_out->val.read.v_val.err.v, error_handle);
    }
    return ERR_OK;
}

static int32_t croft_wit_wasi_io_streams_reply_skip_ok(SapWitIoStreamsReply* reply_out,
                                                        uint8_t blocking,
                                                        uint64_t skipped)
{
    if (!reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_io_streams_reply(reply_out);
    reply_out->case_tag = blocking ? SAP_WIT_IO_STREAMS_REPLY_BLOCKING_SKIP
                                   : SAP_WIT_IO_STREAMS_REPLY_SKIP;
    if (blocking) {
        reply_out->val.blocking_skip.is_v_ok = 1u;
        reply_out->val.blocking_skip.v_val.ok.v = skipped;
    } else {
        reply_out->val.skip.is_v_ok = 1u;
        reply_out->val.skip.v_val.ok.v = skipped;
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

static int32_t croft_wit_wasi_io_streams_reply_skip_error(SapWitIoStreamsReply* reply_out,
                                                           uint8_t blocking,
                                                           uint32_t error_handle)
{
    if (!reply_out || error_handle == 0u) {
        return ERR_INVALID;
    }
    sap_wit_zero_io_streams_reply(reply_out);
    reply_out->case_tag = blocking ? SAP_WIT_IO_STREAMS_REPLY_BLOCKING_SKIP
                                   : SAP_WIT_IO_STREAMS_REPLY_SKIP;
    if (blocking) {
        reply_out->val.blocking_skip.is_v_ok = 0u;
        croft_wit_wasi_set_stream_error_failed(&reply_out->val.blocking_skip.v_val.err.v,
                                               error_handle);
    } else {
        reply_out->val.skip.is_v_ok = 0u;
        croft_wit_wasi_set_stream_error_failed(&reply_out->val.skip.v_val.err.v, error_handle);
    }
    return ERR_OK;
}

static int32_t croft_wit_wasi_io_streams_reply_pollable(SapWitIoStreamsReply* reply_out,
                                                         uint32_t handle)
{
    if (!reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_io_streams_reply(reply_out);
    reply_out->case_tag = SAP_WIT_IO_STREAMS_REPLY_POLLABLE;
    reply_out->val.pollable = (SapWitIoPollableResource)handle;
    return ERR_OK;
}

static int32_t croft_wit_wasi_io_streams_reply_pollable_invalid(SapWitIoStreamsReply* reply_out)
{
    return croft_wit_wasi_io_streams_reply_pollable(reply_out,
                                                    SAP_WIT_IO_POLLABLE_RESOURCE_INVALID);
}

static int32_t croft_wit_wasi_io_streams_reply_check_write_ok(SapWitIoStreamsReply* reply_out,
                                                              uint64_t permitted)
{
    if (!reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_io_streams_reply(reply_out);
    reply_out->case_tag = SAP_WIT_IO_STREAMS_REPLY_CHECK_WRITE;
    reply_out->val.check_write.is_v_ok = 1u;
    reply_out->val.check_write.v_val.ok.v = permitted;
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

static int32_t croft_wit_wasi_io_streams_reply_check_write_error(SapWitIoStreamsReply* reply_out,
                                                                  uint32_t error_handle)
{
    if (!reply_out || error_handle == 0u) {
        return ERR_INVALID;
    }
    sap_wit_zero_io_streams_reply(reply_out);
    reply_out->case_tag = SAP_WIT_IO_STREAMS_REPLY_CHECK_WRITE;
    reply_out->val.check_write.is_v_ok = 0u;
    croft_wit_wasi_set_stream_error_failed(&reply_out->val.check_write.v_val.err.v, error_handle);
    return ERR_OK;
}

static int32_t croft_wit_wasi_io_streams_reply_status_ok(SapWitIoStreamsReply* reply_out)
{
    if (!reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_io_streams_reply(reply_out);
    reply_out->case_tag = SAP_WIT_IO_STREAMS_REPLY_STATUS;
    reply_out->val.status.is_v_ok = 1u;
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

static int32_t croft_wit_wasi_io_streams_reply_status_error(SapWitIoStreamsReply* reply_out,
                                                             uint32_t error_handle)
{
    if (!reply_out || error_handle == 0u) {
        return ERR_INVALID;
    }
    sap_wit_zero_io_streams_reply(reply_out);
    reply_out->case_tag = SAP_WIT_IO_STREAMS_REPLY_STATUS;
    reply_out->val.status.is_v_ok = 0u;
    croft_wit_wasi_set_stream_error_failed(&reply_out->val.status.v_val.err.v, error_handle);
    return ERR_OK;
}

static int32_t croft_wit_wasi_io_streams_reply_splice_ok(SapWitIoStreamsReply* reply_out,
                                                          uint8_t blocking,
                                                          uint64_t amount)
{
    if (!reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_io_streams_reply(reply_out);
    reply_out->case_tag = blocking ? SAP_WIT_IO_STREAMS_REPLY_BLOCKING_SPLICE
                                   : SAP_WIT_IO_STREAMS_REPLY_SPLICE;
    if (blocking) {
        reply_out->val.blocking_splice.is_v_ok = 1u;
        reply_out->val.blocking_splice.v_val.ok.v = amount;
    } else {
        reply_out->val.splice.is_v_ok = 1u;
        reply_out->val.splice.v_val.ok.v = amount;
    }
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

static int32_t croft_wit_wasi_io_streams_reply_splice_error(SapWitIoStreamsReply* reply_out,
                                                             uint8_t blocking,
                                                             uint32_t error_handle)
{
    if (!reply_out || error_handle == 0u) {
        return ERR_INVALID;
    }
    sap_wit_zero_io_streams_reply(reply_out);
    reply_out->case_tag = blocking ? SAP_WIT_IO_STREAMS_REPLY_BLOCKING_SPLICE
                                   : SAP_WIT_IO_STREAMS_REPLY_SPLICE;
    if (blocking) {
        reply_out->val.blocking_splice.is_v_ok = 0u;
        croft_wit_wasi_set_stream_error_failed(&reply_out->val.blocking_splice.v_val.err.v,
                                               error_handle);
    } else {
        reply_out->val.splice.is_v_ok = 0u;
        croft_wit_wasi_set_stream_error_failed(&reply_out->val.splice.v_val.err.v, error_handle);
    }
    return ERR_OK;
}

static int croft_wit_wasi_input_stream_read_into_blob(croft_wit_wasi_machine_runtime* runtime,
                                                      croft_wit_wasi_input_stream_slot* stream,
                                                      uint64_t len,
                                                      uint32_t* error_handle_out)
{
    croft_wit_wasi_descriptor_slot* descriptor;
    off_t offset;
    size_t request_len;
    ssize_t nread;
    int rc;

    if (error_handle_out) {
        *error_handle_out = 0u;
    }
    if (!runtime || !stream) {
        return ERR_INVALID;
    }
    descriptor = croft_wit_wasi_fs_descriptor_lookup(runtime, stream->descriptor_handle);
    if (!descriptor || !descriptor->live || descriptor->is_directory) {
        return ERR_INVALID;
    }
    request_len = len > (uint64_t)UINT32_MAX ? UINT32_MAX : (size_t)len;
    runtime->filesystem_blob.len = 0u;
    rc = croft_wit_wasi_buffer_reserve(&runtime->filesystem_blob,
                                       request_len > 0u ? (uint32_t)request_len : 1u);
    if (rc != ERR_OK) {
        return rc;
    }
    if (request_len == 0u) {
        return ERR_OK;
    }

    if (descriptor->is_seekable) {
        rc = croft_wit_wasi_u64_to_off_t(stream->offset, &offset);
        if (rc != ERR_OK) {
            if (error_handle_out && rc == ERR_RANGE) {
                rc = croft_wit_wasi_stream_error_insert_message(
                    runtime,
                    descriptor,
                    SAP_WIT_FILESYSTEM_ERROR_CODE_INVALID_SEEK,
                    "pread: invalid seek",
                    error_handle_out);
                if (rc == ERR_OK) {
                    stream->closed = 1u;
                    stream->error_handle = *error_handle_out;
                    return ERR_INVALID;
                }
            }
            return rc;
        }
        nread = pread(descriptor->fd, runtime->filesystem_blob.data, request_len, offset);
    } else {
        nread = read(descriptor->fd, runtime->filesystem_blob.data, request_len);
    }
    if (nread < 0) {
        if (error_handle_out) {
            rc = croft_wit_wasi_stream_error_insert_errno(runtime,
                                                          descriptor,
                                                          descriptor->is_seekable ? "pread"
                                                                                  : "read",
                                                          errno,
                                                          error_handle_out);
            if (rc == ERR_OK) {
                stream->closed = 1u;
                stream->error_handle = *error_handle_out;
                return ERR_INVALID;
            }
            return rc;
        }
        return ERR_INVALID;
    }
    runtime->filesystem_blob.len = (uint32_t)nread;
    stream->offset += (uint64_t)runtime->filesystem_blob.len;
    return ERR_OK;
}

static int croft_wit_wasi_input_stream_skip_bytes(croft_wit_wasi_machine_runtime* runtime,
                                                  croft_wit_wasi_input_stream_slot* stream,
                                                  uint64_t len,
                                                  uint64_t* skipped_out,
                                                  uint32_t* error_handle_out)
{
    croft_wit_wasi_descriptor_slot* descriptor;
    uint8_t discard[4096];
    uint64_t size = 0u;
    uint64_t available = 0u;
    uint64_t skipped = 0u;
    int sys_err = 0;
    int rc;

    if (error_handle_out) {
        *error_handle_out = 0u;
    }
    if (!runtime || !stream || !skipped_out) {
        return ERR_INVALID;
    }
    descriptor = croft_wit_wasi_fs_descriptor_lookup(runtime, stream->descriptor_handle);
    if (!descriptor || !descriptor->live || descriptor->is_directory) {
        return ERR_INVALID;
    }
    if (!descriptor->is_seekable) {
        while (skipped < len) {
            size_t chunk_len = (size_t)((len - skipped) > sizeof(discard) ? sizeof(discard)
                                                                          : (len - skipped));
            ssize_t nread = read(descriptor->fd, discard, chunk_len);

            if (nread < 0) {
                if (error_handle_out) {
                    rc = croft_wit_wasi_stream_error_insert_errno(runtime,
                                                                  descriptor,
                                                                  "read",
                                                                  errno,
                                                                  error_handle_out);
                    if (rc == ERR_OK) {
                        stream->closed = 1u;
                        stream->error_handle = *error_handle_out;
                        return ERR_INVALID;
                    }
                }
                return ERR_INVALID;
            }
            if (nread == 0) {
                break;
            }
            skipped += (uint64_t)nread;
        }
        stream->offset += skipped;
        *skipped_out = skipped;
        return ERR_OK;
    }
    rc = croft_wit_wasi_fd_size(descriptor->fd, &size, &sys_err);
    if (rc != ERR_OK) {
        if (error_handle_out) {
            if (sys_err != 0) {
                rc = croft_wit_wasi_stream_error_insert_errno(runtime,
                                                              descriptor,
                                                              "fstat",
                                                              sys_err,
                                                              error_handle_out);
            } else if (rc == ERR_RANGE) {
                rc = croft_wit_wasi_stream_error_insert_message(runtime,
                                                                descriptor,
                                                                SAP_WIT_FILESYSTEM_ERROR_CODE_OVERFLOW,
                                                                "fstat: overflow",
                                                                error_handle_out);
            }
            if (rc == ERR_OK) {
                stream->closed = 1u;
                stream->error_handle = *error_handle_out;
                return ERR_INVALID;
            }
        }
        return rc;
    }
    if (stream->offset < size) {
        available = size - stream->offset;
    }
    skipped = len < available ? len : available;
    stream->offset += skipped;
    *skipped_out = skipped;
    return ERR_OK;
}

static int croft_wit_wasi_output_stream_write_bytes(croft_wit_wasi_machine_runtime* runtime,
                                                    croft_wit_wasi_output_stream_slot* stream,
                                                    const uint8_t* data,
                                                    uint32_t len,
                                                    uint32_t* error_handle_out)
{
    croft_wit_wasi_descriptor_slot* descriptor;
    uint64_t base_offset;
    uint32_t written = 0u;
    int sys_err = 0;
    int rc;

    if (error_handle_out) {
        *error_handle_out = 0u;
    }
    if (!runtime || !stream || (!data && len > 0u)) {
        return ERR_INVALID;
    }
    descriptor = croft_wit_wasi_fs_descriptor_lookup(runtime, stream->descriptor_handle);
    if (!descriptor || !descriptor->live || descriptor->is_directory) {
        return ERR_INVALID;
    }

    base_offset = stream->offset;
    if (stream->append && descriptor->is_seekable) {
        rc = croft_wit_wasi_fd_size(descriptor->fd, &base_offset, &sys_err);
        if (rc != ERR_OK) {
            if (error_handle_out) {
                if (sys_err != 0) {
                    rc = croft_wit_wasi_stream_error_insert_errno(runtime,
                                                                  descriptor,
                                                                  "fstat",
                                                                  sys_err,
                                                                  error_handle_out);
                } else if (rc == ERR_RANGE) {
                    rc = croft_wit_wasi_stream_error_insert_message(runtime,
                                                                    descriptor,
                                                                    SAP_WIT_FILESYSTEM_ERROR_CODE_OVERFLOW,
                                                                    "fstat: overflow",
                                                                    error_handle_out);
                }
                if (rc == ERR_OK) {
                    stream->closed = 1u;
                    stream->error_handle = *error_handle_out;
                    return ERR_INVALID;
                }
            }
            return rc;
        }
    }

    while (written < len) {
        size_t chunk_len = (size_t)(len - written);
        ssize_t nwritten;

        if (descriptor->is_seekable) {
            off_t write_offset;

            rc = croft_wit_wasi_u64_to_off_t(base_offset + written, &write_offset);
            if (rc != ERR_OK) {
                if (error_handle_out && rc == ERR_RANGE) {
                    rc = croft_wit_wasi_stream_error_insert_message(
                        runtime,
                        descriptor,
                        SAP_WIT_FILESYSTEM_ERROR_CODE_INVALID_SEEK,
                        "pwrite: invalid seek",
                        error_handle_out);
                    if (rc == ERR_OK) {
                        stream->closed = 1u;
                        stream->error_handle = *error_handle_out;
                        return ERR_INVALID;
                    }
                }
                return rc;
            }
            nwritten = pwrite(descriptor->fd, data + written, chunk_len, write_offset);
        } else {
            nwritten = write(descriptor->fd, data + written, chunk_len);
        }
        if (nwritten <= 0) {
            if (error_handle_out) {
                rc = croft_wit_wasi_stream_error_insert_errno(runtime,
                                                              descriptor,
                                                              descriptor->is_seekable ? "pwrite"
                                                                                      : "write",
                                                              nwritten < 0 ? errno : EIO,
                                                              error_handle_out);
                if (rc == ERR_OK) {
                    stream->closed = 1u;
                    stream->error_handle = *error_handle_out;
                    return ERR_INVALID;
                }
                return rc;
            }
            return ERR_INVALID;
        }
        written += (uint32_t)nwritten;
    }

    stream->offset = base_offset + written;
    return ERR_OK;
}

static int croft_wit_wasi_output_stream_write_zeroes_bytes(
    croft_wit_wasi_machine_runtime* runtime,
    croft_wit_wasi_output_stream_slot* stream,
    uint64_t len,
    uint32_t* error_handle_out)
{
    static const uint8_t zeros[4096] = {0};
    uint64_t remaining = len;
    int rc;

    if (error_handle_out) {
        *error_handle_out = 0u;
    }
    while (remaining > 0u) {
        uint32_t chunk_len = remaining > sizeof(zeros) ? (uint32_t)sizeof(zeros)
                                                       : (uint32_t)remaining;
        rc = croft_wit_wasi_output_stream_write_bytes(runtime,
                                                      stream,
                                                      zeros,
                                                      chunk_len,
                                                      error_handle_out);
        if (rc != ERR_OK) {
            return rc;
        }
        remaining -= chunk_len;
    }
    return ERR_OK;
}

static int croft_wit_wasi_output_stream_flush(croft_wit_wasi_machine_runtime* runtime,
                                              croft_wit_wasi_output_stream_slot* stream,
                                              uint32_t* error_handle_out)
{
    croft_wit_wasi_descriptor_slot* descriptor;
    int rc;

    if (error_handle_out) {
        *error_handle_out = 0u;
    }
    if (!runtime || !stream) {
        return ERR_INVALID;
    }
    descriptor = croft_wit_wasi_fs_descriptor_lookup(runtime, stream->descriptor_handle);
    if (!descriptor || !descriptor->live || descriptor->is_directory) {
        return ERR_INVALID;
    }
    if (!descriptor->is_seekable) {
        return ERR_OK;
    }
    if (fsync(descriptor->fd) == 0) {
        return ERR_OK;
    }
    if (!error_handle_out) {
        return ERR_INVALID;
    }
    rc = croft_wit_wasi_stream_error_insert_errno(runtime,
                                                  descriptor,
                                                  "fsync",
                                                  errno,
                                                  error_handle_out);
    if (rc != ERR_OK) {
        return rc;
    }
    stream->closed = 1u;
    stream->error_handle = *error_handle_out;
    return ERR_INVALID;
}

static int croft_wit_wasi_output_stream_splice_bytes(croft_wit_wasi_machine_runtime* runtime,
                                                     croft_wit_wasi_output_stream_slot* output,
                                                     croft_wit_wasi_input_stream_slot* input,
                                                     uint64_t len,
                                                     uint64_t* amount_out,
                                                     uint32_t* error_handle_out)
{
    uint8_t buffer[4096];
    uint64_t remaining = len;
    uint64_t total = 0u;

    if (error_handle_out) {
        *error_handle_out = 0u;
    }
    if (!runtime || !output || !input || !amount_out) {
        return ERR_INVALID;
    }

    while (remaining > 0u) {
        uint64_t chunk_limit = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
        croft_wit_wasi_descriptor_slot* input_descriptor;
        ssize_t nread;
        int rc;

        input_descriptor = croft_wit_wasi_fs_descriptor_lookup(runtime, input->descriptor_handle);
        if (!input_descriptor || !input_descriptor->live || input_descriptor->is_directory) {
            return ERR_INVALID;
        }
        if (input_descriptor->is_seekable) {
            off_t read_offset;

            rc = croft_wit_wasi_u64_to_off_t(input->offset, &read_offset);
            if (rc != ERR_OK) {
                if (error_handle_out && rc == ERR_RANGE) {
                    rc = croft_wit_wasi_stream_error_insert_message(
                        runtime,
                        input_descriptor,
                        SAP_WIT_FILESYSTEM_ERROR_CODE_INVALID_SEEK,
                        "pread: invalid seek",
                        error_handle_out);
                    if (rc == ERR_OK) {
                        input->closed = 1u;
                        input->error_handle = *error_handle_out;
                        return ERR_INVALID;
                    }
                }
                return rc;
            }
            nread = pread(input_descriptor->fd, buffer, (size_t)chunk_limit, read_offset);
        } else {
            nread = read(input_descriptor->fd, buffer, (size_t)chunk_limit);
        }
        if (nread < 0) {
            if (error_handle_out) {
                rc = croft_wit_wasi_stream_error_insert_errno(runtime,
                                                              input_descriptor,
                                                              input_descriptor->is_seekable
                                                                  ? "pread"
                                                                  : "read",
                                                              errno,
                                                              error_handle_out);
                if (rc == ERR_OK) {
                    input->closed = 1u;
                    input->error_handle = *error_handle_out;
                    return ERR_INVALID;
                }
                return rc;
            }
            return ERR_INVALID;
        }
        if (nread == 0) {
            break;
        }

        input->offset += (uint64_t)nread;
        rc = croft_wit_wasi_output_stream_write_bytes(runtime,
                                                      output,
                                                      buffer,
                                                      (uint32_t)nread,
                                                      error_handle_out);
        if (rc != ERR_OK) {
            return rc;
        }

        total += (uint64_t)nread;
        remaining -= (uint64_t)nread;
    }

    *amount_out = total;
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_io_streams_read(void* ctx,
                                                       const SapWitIoInputStreamRead* payload,
                                                       SapWitIoStreamsReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_input_stream_slot* stream;
    uint32_t error_handle = 0u;
    int rc;

    if (!runtime || !payload) {
        return ERR_INVALID;
    }
    stream = croft_wit_wasi_input_stream_lookup(runtime, payload->input_stream);
    if (!stream || stream->closed) {
        return croft_wit_wasi_io_streams_reply_read_closed(reply_out, 0u);
    }
    rc = croft_wit_wasi_input_stream_read_into_blob(runtime, stream, payload->len, &error_handle);
    if (rc != ERR_OK) {
        if (error_handle != 0u) {
            return croft_wit_wasi_io_streams_reply_read_error(reply_out, 0u, error_handle);
        }
        return croft_wit_wasi_io_streams_reply_read_closed(reply_out, 0u);
    }
    return croft_wit_wasi_io_streams_reply_read_ok(reply_out,
                                                   0u,
                                                   runtime->filesystem_blob.data,
                                                   runtime->filesystem_blob.len);
}

static int32_t croft_wit_wasi_dispatch_io_streams_blocking_read(
    void* ctx,
    const SapWitIoInputStreamBlockingRead* payload,
    SapWitIoStreamsReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_input_stream_slot* stream;
    uint32_t error_handle = 0u;
    int rc;

    if (!runtime || !payload) {
        return ERR_INVALID;
    }
    stream = croft_wit_wasi_input_stream_lookup(runtime, payload->input_stream);
    if (!stream || stream->closed) {
        return croft_wit_wasi_io_streams_reply_read_closed(reply_out, 1u);
    }
    rc = croft_wit_wasi_input_stream_read_into_blob(runtime, stream, payload->len, &error_handle);
    if (rc != ERR_OK) {
        if (error_handle != 0u) {
            return croft_wit_wasi_io_streams_reply_read_error(reply_out, 1u, error_handle);
        }
        return croft_wit_wasi_io_streams_reply_read_closed(reply_out, 1u);
    }
    return croft_wit_wasi_io_streams_reply_read_ok(reply_out,
                                                   1u,
                                                   runtime->filesystem_blob.data,
                                                   runtime->filesystem_blob.len);
}

static int32_t croft_wit_wasi_dispatch_io_streams_skip(void* ctx,
                                                       const SapWitIoInputStreamSkip* payload,
                                                       SapWitIoStreamsReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_input_stream_slot* stream;
    uint64_t skipped = 0u;
    uint32_t error_handle = 0u;
    int rc;

    if (!runtime || !payload) {
        return ERR_INVALID;
    }
    stream = croft_wit_wasi_input_stream_lookup(runtime, payload->input_stream);
    if (!stream || stream->closed) {
        return croft_wit_wasi_io_streams_reply_skip_closed(reply_out, 0u);
    }
    rc = croft_wit_wasi_input_stream_skip_bytes(runtime,
                                                stream,
                                                payload->len,
                                                &skipped,
                                                &error_handle);
    if (rc != ERR_OK) {
        if (error_handle != 0u) {
            return croft_wit_wasi_io_streams_reply_skip_error(reply_out, 0u, error_handle);
        }
        return croft_wit_wasi_io_streams_reply_skip_closed(reply_out, 0u);
    }
    return croft_wit_wasi_io_streams_reply_skip_ok(reply_out, 0u, skipped);
}

static int32_t croft_wit_wasi_dispatch_io_streams_blocking_skip(
    void* ctx,
    const SapWitIoInputStreamBlockingSkip* payload,
    SapWitIoStreamsReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_input_stream_slot* stream;
    uint64_t skipped = 0u;
    uint32_t error_handle = 0u;
    int rc;

    if (!runtime || !payload) {
        return ERR_INVALID;
    }
    stream = croft_wit_wasi_input_stream_lookup(runtime, payload->input_stream);
    if (!stream || stream->closed) {
        return croft_wit_wasi_io_streams_reply_skip_closed(reply_out, 1u);
    }
    rc = croft_wit_wasi_input_stream_skip_bytes(runtime,
                                                stream,
                                                payload->len,
                                                &skipped,
                                                &error_handle);
    if (rc != ERR_OK) {
        if (error_handle != 0u) {
            return croft_wit_wasi_io_streams_reply_skip_error(reply_out, 1u, error_handle);
        }
        return croft_wit_wasi_io_streams_reply_skip_closed(reply_out, 1u);
    }
    return croft_wit_wasi_io_streams_reply_skip_ok(reply_out, 1u, skipped);
}

static int32_t croft_wit_wasi_dispatch_io_streams_input_stream_subscribe(
    void* ctx,
    const SapWitIoInputStreamSubscribe* payload,
    SapWitIoStreamsReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    uint32_t pollable_handle = 0u;
    int rc;

    if (!runtime || !payload) {
        return ERR_INVALID;
    }
    {
        croft_wit_wasi_input_stream_slot* stream =
            croft_wit_wasi_input_stream_lookup(runtime, payload->input_stream);
        if (!stream) {
            return croft_wit_wasi_io_streams_reply_pollable_invalid(reply_out);
        }
    }
    rc = croft_wit_wasi_make_ready_pollable(runtime, &pollable_handle);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_wit_wasi_io_streams_reply_pollable(reply_out, pollable_handle);
}

static int32_t croft_wit_wasi_dispatch_io_streams_check_write(
    void* ctx,
    const SapWitIoOutputStreamCheckWrite* payload,
    SapWitIoStreamsReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_output_stream_slot* stream;

    if (!runtime || !payload) {
        return ERR_INVALID;
    }
    stream = croft_wit_wasi_output_stream_lookup(runtime, payload->output_stream);
    if (!stream || stream->closed) {
        return croft_wit_wasi_io_streams_reply_check_write_closed(reply_out);
    }
    return croft_wit_wasi_io_streams_reply_check_write_ok(reply_out, UINT32_MAX);
}

static int32_t croft_wit_wasi_dispatch_io_streams_write(void* ctx,
                                                        const SapWitIoOutputStreamWrite* payload,
                                                        SapWitIoStreamsReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_output_stream_slot* stream;
    uint32_t error_handle = 0u;
    int rc;

    if (!runtime || !payload) {
        return ERR_INVALID;
    }
    stream = croft_wit_wasi_output_stream_lookup(runtime, payload->output_stream);
    if (!stream || stream->closed) {
        return croft_wit_wasi_io_streams_reply_status_closed(reply_out);
    }
    rc = croft_wit_wasi_output_stream_write_bytes(runtime,
                                                  stream,
                                                  payload->contents_data,
                                                  payload->contents_len,
                                                  &error_handle);
    if (rc != ERR_OK) {
        if (error_handle != 0u) {
            return croft_wit_wasi_io_streams_reply_status_error(reply_out, error_handle);
        }
        return croft_wit_wasi_io_streams_reply_status_closed(reply_out);
    }
    return croft_wit_wasi_io_streams_reply_status_ok(reply_out);
}

static int32_t croft_wit_wasi_dispatch_io_streams_blocking_write_and_flush(
    void* ctx,
    const SapWitIoOutputStreamBlockingWriteAndFlush* payload,
    SapWitIoStreamsReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_output_stream_slot* stream;
    uint32_t error_handle = 0u;
    int rc;

    if (!runtime || !payload) {
        return ERR_INVALID;
    }
    stream = croft_wit_wasi_output_stream_lookup(runtime, payload->output_stream);
    if (!stream || stream->closed) {
        return croft_wit_wasi_io_streams_reply_status_closed(reply_out);
    }
    rc = croft_wit_wasi_output_stream_write_bytes(runtime,
                                                  stream,
                                                  payload->contents_data,
                                                  payload->contents_len,
                                                  &error_handle);
    if (rc != ERR_OK) {
        if (error_handle != 0u) {
            return croft_wit_wasi_io_streams_reply_status_error(reply_out, error_handle);
        }
        return croft_wit_wasi_io_streams_reply_status_closed(reply_out);
    }
    rc = croft_wit_wasi_output_stream_flush(runtime, stream, &error_handle);
    if (rc != ERR_OK) {
        if (error_handle != 0u) {
            return croft_wit_wasi_io_streams_reply_status_error(reply_out, error_handle);
        }
        return croft_wit_wasi_io_streams_reply_status_closed(reply_out);
    }
    return croft_wit_wasi_io_streams_reply_status_ok(reply_out);
}

static int32_t croft_wit_wasi_dispatch_io_streams_flush(void* ctx,
                                                        const SapWitIoOutputStreamFlush* payload,
                                                        SapWitIoStreamsReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_output_stream_slot* stream;
    uint32_t error_handle = 0u;
    int rc;

    if (!runtime || !payload) {
        return ERR_INVALID;
    }
    stream = croft_wit_wasi_output_stream_lookup(runtime, payload->output_stream);
    if (!stream || stream->closed) {
        return croft_wit_wasi_io_streams_reply_status_closed(reply_out);
    }
    rc = croft_wit_wasi_output_stream_flush(runtime, stream, &error_handle);
    if (rc != ERR_OK) {
        if (error_handle != 0u) {
            return croft_wit_wasi_io_streams_reply_status_error(reply_out, error_handle);
        }
        return croft_wit_wasi_io_streams_reply_status_closed(reply_out);
    }
    return croft_wit_wasi_io_streams_reply_status_ok(reply_out);
}

static int32_t croft_wit_wasi_dispatch_io_streams_blocking_flush(
    void* ctx,
    const SapWitIoOutputStreamBlockingFlush* payload,
    SapWitIoStreamsReply* reply_out)
{
    return croft_wit_wasi_dispatch_io_streams_flush(ctx,
                                                    (const SapWitIoOutputStreamFlush*)payload,
                                                    reply_out);
}

static int32_t croft_wit_wasi_dispatch_io_streams_output_stream_subscribe(
    void* ctx,
    const SapWitIoOutputStreamSubscribe* payload,
    SapWitIoStreamsReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    uint32_t pollable_handle = 0u;
    int rc;

    if (!runtime || !payload) {
        return ERR_INVALID;
    }
    if (!croft_wit_wasi_output_stream_lookup(runtime, payload->output_stream)) {
        return croft_wit_wasi_io_streams_reply_pollable_invalid(reply_out);
    }
    rc = croft_wit_wasi_make_ready_pollable(runtime, &pollable_handle);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_wit_wasi_io_streams_reply_pollable(reply_out, pollable_handle);
}

static int32_t croft_wit_wasi_dispatch_io_streams_write_zeroes(
    void* ctx,
    const SapWitIoOutputStreamWriteZeroes* payload,
    SapWitIoStreamsReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_output_stream_slot* stream;
    uint32_t error_handle = 0u;
    int rc;

    if (!runtime || !payload) {
        return ERR_INVALID;
    }
    stream = croft_wit_wasi_output_stream_lookup(runtime, payload->output_stream);
    if (!stream || stream->closed) {
        return croft_wit_wasi_io_streams_reply_status_closed(reply_out);
    }
    rc = croft_wit_wasi_output_stream_write_zeroes_bytes(runtime,
                                                         stream,
                                                         payload->len,
                                                         &error_handle);
    if (rc != ERR_OK) {
        if (error_handle != 0u) {
            return croft_wit_wasi_io_streams_reply_status_error(reply_out, error_handle);
        }
        return croft_wit_wasi_io_streams_reply_status_closed(reply_out);
    }
    return croft_wit_wasi_io_streams_reply_status_ok(reply_out);
}

static int32_t croft_wit_wasi_dispatch_io_streams_blocking_write_zeroes_and_flush(
    void* ctx,
    const SapWitIoOutputStreamBlockingWriteZeroesAndFlush* payload,
    SapWitIoStreamsReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_output_stream_slot* stream;
    uint32_t error_handle = 0u;
    int rc;

    if (!runtime || !payload) {
        return ERR_INVALID;
    }
    stream = croft_wit_wasi_output_stream_lookup(runtime, payload->output_stream);
    if (!stream || stream->closed) {
        return croft_wit_wasi_io_streams_reply_status_closed(reply_out);
    }
    rc = croft_wit_wasi_output_stream_write_zeroes_bytes(runtime,
                                                         stream,
                                                         payload->len,
                                                         &error_handle);
    if (rc != ERR_OK) {
        if (error_handle != 0u) {
            return croft_wit_wasi_io_streams_reply_status_error(reply_out, error_handle);
        }
        return croft_wit_wasi_io_streams_reply_status_closed(reply_out);
    }
    rc = croft_wit_wasi_output_stream_flush(runtime, stream, &error_handle);
    if (rc != ERR_OK) {
        if (error_handle != 0u) {
            return croft_wit_wasi_io_streams_reply_status_error(reply_out, error_handle);
        }
        return croft_wit_wasi_io_streams_reply_status_closed(reply_out);
    }
    return croft_wit_wasi_io_streams_reply_status_ok(reply_out);
}

static int32_t croft_wit_wasi_dispatch_io_streams_splice(
    void* ctx,
    const SapWitIoOutputStreamSplice* payload,
    SapWitIoStreamsReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_output_stream_slot* output;
    croft_wit_wasi_input_stream_slot* input;
    uint64_t amount = 0u;
    uint32_t error_handle = 0u;
    int rc;

    if (!runtime || !payload) {
        return ERR_INVALID;
    }
    output = croft_wit_wasi_output_stream_lookup(runtime, payload->output_stream);
    input = croft_wit_wasi_input_stream_lookup(runtime, payload->src);
    if (!output || !input || output->closed || input->closed) {
        return croft_wit_wasi_io_streams_reply_splice_closed(reply_out, 0u);
    }
    rc = croft_wit_wasi_output_stream_splice_bytes(runtime,
                                                   output,
                                                   input,
                                                   payload->len,
                                                   &amount,
                                                   &error_handle);
    if (rc != ERR_OK) {
        if (error_handle != 0u) {
            return croft_wit_wasi_io_streams_reply_splice_error(reply_out, 0u, error_handle);
        }
        return croft_wit_wasi_io_streams_reply_splice_closed(reply_out, 0u);
    }
    return croft_wit_wasi_io_streams_reply_splice_ok(reply_out, 0u, amount);
}

static int32_t croft_wit_wasi_dispatch_io_streams_blocking_splice(
    void* ctx,
    const SapWitIoOutputStreamBlockingSplice* payload,
    SapWitIoStreamsReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_output_stream_slot* output;
    croft_wit_wasi_input_stream_slot* input;
    uint64_t amount = 0u;
    uint32_t error_handle = 0u;
    int rc;

    if (!runtime || !payload) {
        return ERR_INVALID;
    }
    output = croft_wit_wasi_output_stream_lookup(runtime, payload->output_stream);
    input = croft_wit_wasi_input_stream_lookup(runtime, payload->src);
    if (!output || !input || output->closed || input->closed) {
        return croft_wit_wasi_io_streams_reply_splice_closed(reply_out, 1u);
    }
    rc = croft_wit_wasi_output_stream_splice_bytes(runtime,
                                                   output,
                                                   input,
                                                   payload->len,
                                                   &amount,
                                                   &error_handle);
    if (rc != ERR_OK) {
        if (error_handle != 0u) {
            return croft_wit_wasi_io_streams_reply_splice_error(reply_out, 1u, error_handle);
        }
        return croft_wit_wasi_io_streams_reply_splice_closed(reply_out, 1u);
    }
    return croft_wit_wasi_io_streams_reply_splice_ok(reply_out, 1u, amount);
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

static int32_t croft_wit_wasi_dispatch_io_error_to_debug_string(
    void* ctx,
    const SapWitIoErrorToDebugString* payload,
    SapWitIoErrorReply* reply_out)
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

    sap_wit_zero_io_error_reply(reply_out);
    reply_out->case_tag = SAP_WIT_IO_ERROR_REPLY_TO_DEBUG_STRING;
    reply_out->val.to_debug_string.data = (const uint8_t*)debug_string;
    reply_out->val.to_debug_string.len = (uint32_t)strlen(debug_string);
    return ERR_OK;
}


static const SapWitClocksMonotonicClockDispatchOps g_croft_wit_wasi_monotonic_ops = {
    .now = croft_wit_wasi_dispatch_monotonic_now,
    .resolution = croft_wit_wasi_dispatch_monotonic_resolution,
    .subscribe_instant = croft_wit_wasi_dispatch_monotonic_subscribe_instant,
    .subscribe_duration = croft_wit_wasi_dispatch_monotonic_subscribe_duration,
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

static const SapWitIoErrorDispatchOps g_croft_wit_wasi_io_error_ops = {
    .to_debug_string = croft_wit_wasi_dispatch_io_error_to_debug_string,
};

static const SapWitIoPollDispatchOps g_croft_wit_wasi_io_poll_ops = {
    .ready = croft_wit_wasi_dispatch_io_ready,
    .block = croft_wit_wasi_dispatch_io_block,
    .poll = croft_wit_wasi_dispatch_io_poll,
};

int32_t croft_wit_wasi_machine_runtime_dispatch_io_error(
    croft_wit_wasi_machine_runtime* runtime,
    const SapWitIoErrorCommand* command,
    SapWitIoErrorReply* reply_out)
{
    if (!runtime || !command || !reply_out) {
        return ERR_INVALID;
    }
    return sap_wit_dispatch_io_error(runtime, &g_croft_wit_wasi_io_error_ops, command, reply_out);
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
