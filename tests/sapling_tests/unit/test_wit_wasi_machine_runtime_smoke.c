#include "croft/wit_wasi_machine_runtime.h"
#include "croft/platform.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(CROFT_OS_WINDOWS)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static int expect_true(const char* label, int value)
{
    if (value) {
        return 1;
    }
    fprintf(stderr, "%s: expected true\n", label);
    return 0;
}

static int expect_u32(const char* label, uint32_t actual, uint32_t expected)
{
    if (actual == expected) {
        return 1;
    }
    fprintf(stderr, "%s: expected %u, got %u\n", label, expected, actual);
    return 0;
}

static int expect_u64(const char* label, uint64_t actual, uint64_t expected)
{
    if (actual == expected) {
        return 1;
    }
    fprintf(stderr,
            "%s: expected %llu, got %llu\n",
            label,
            (unsigned long long)expected,
            (unsigned long long)actual);
    return 0;
}

static int expect_str(const char* label,
                      const uint8_t* data,
                      uint32_t len,
                      const char* expected)
{
    size_t expected_len = strlen(expected);

    if (expected_len == len && memcmp(data, expected, len) == 0) {
        return 1;
    }
    fprintf(stderr,
            "%s: expected '%s', got '%.*s'\n",
            label,
            expected,
            (int)len,
            data ? (const char*)data : "");
    return 0;
}

static int decode_string(const uint8_t* data,
                         uint32_t byte_len,
                         ThatchCursor* cursor,
                         const uint8_t** out_data,
                         uint32_t* out_len)
{
    ThatchRegion view;
    uint8_t tag = 0u;
    int rc;

    rc = thatch_region_init_readonly(&view, data, byte_len);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = thatch_read_tag(&view, cursor, &tag);
    if (rc != ERR_OK) {
        return rc;
    }
    if (tag != SAP_WIT_TAG_STRING) {
        return ERR_TYPE;
    }
    rc = thatch_read_data(&view, cursor, 4u, out_len);
    if (rc != ERR_OK) {
        return rc;
    }
    return thatch_read_ptr(&view, cursor, *out_len, (const void**)out_data);
}

static int decode_resource(const uint8_t* data,
                           uint32_t byte_len,
                           ThatchCursor* cursor,
                           uint32_t* out)
{
    ThatchRegion view;
    uint8_t tag = 0u;
    int rc;

    rc = thatch_region_init_readonly(&view, data, byte_len);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = thatch_read_tag(&view, cursor, &tag);
    if (rc != ERR_OK) {
        return rc;
    }
    if (tag != SAP_WIT_TAG_RESOURCE) {
        return ERR_TYPE;
    }
    return thatch_read_data(&view, cursor, 4u, out);
}

static int decode_env_pair(const uint8_t* data,
                           uint32_t byte_len,
                           ThatchCursor* cursor,
                           const uint8_t** key_data,
                           uint32_t* key_len,
                           const uint8_t** value_data,
                           uint32_t* value_len)
{
    ThatchRegion view;
    uint8_t tag = 0u;
    uint32_t skip_len = 0u;
    ThatchCursor segment_end;
    int rc;

    rc = thatch_region_init_readonly(&view, data, byte_len);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = thatch_read_tag(&view, cursor, &tag);
    if (rc != ERR_OK) {
        return rc;
    }
    if (tag != SAP_WIT_TAG_TUPLE) {
        return ERR_TYPE;
    }
    rc = thatch_read_skip_len(&view, cursor, &skip_len);
    if (rc != ERR_OK) {
        return rc;
    }
    segment_end = *cursor + skip_len;

    rc = decode_string(data, byte_len, cursor, key_data, key_len);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = decode_string(data, byte_len, cursor, value_data, value_len);
    if (rc != ERR_OK) {
        return rc;
    }

    return *cursor == segment_end ? ERR_OK : ERR_TYPE;
}

static int decode_resource_string_pair(const uint8_t* data,
                                       uint32_t byte_len,
                                       ThatchCursor* cursor,
                                       uint32_t* resource_handle,
                                       const uint8_t** text_data,
                                       uint32_t* text_len)
{
    ThatchRegion view;
    uint8_t tag = 0u;
    uint32_t skip_len = 0u;
    ThatchCursor segment_end;
    int rc;

    rc = thatch_region_init_readonly(&view, data, byte_len);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = thatch_read_tag(&view, cursor, &tag);
    if (rc != ERR_OK) {
        return rc;
    }
    if (tag != SAP_WIT_TAG_TUPLE) {
        return ERR_TYPE;
    }
    rc = thatch_read_skip_len(&view, cursor, &skip_len);
    if (rc != ERR_OK) {
        return rc;
    }
    segment_end = *cursor + skip_len;

    rc = decode_resource(data, byte_len, cursor, resource_handle);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = decode_string(data, byte_len, cursor, text_data, text_len);
    if (rc != ERR_OK) {
        return rc;
    }

    return *cursor == segment_end ? ERR_OK : ERR_TYPE;
}

static int decode_u32(const uint8_t* data,
                      uint32_t byte_len,
                      ThatchCursor* cursor,
                      uint32_t* out)
{
    ThatchRegion view;
    uint8_t tag = 0u;
    int rc;

    rc = thatch_region_init_readonly(&view, data, byte_len);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = thatch_read_tag(&view, cursor, &tag);
    if (rc != ERR_OK) {
        return rc;
    }
    if (tag != SAP_WIT_TAG_U32) {
        return ERR_TYPE;
    }
    return thatch_read_data(&view, cursor, 4u, out);
}

static int buffer_has_nonzero(const uint8_t* data, uint32_t len)
{
    uint32_t i;

    for (i = 0u; i < len; i++) {
        if (data[i] != 0u) {
            return 1;
        }
    }
    return 0;
}

#if !defined(CROFT_OS_WINDOWS)
static int write_test_file(const char* path, const char* text)
{
    size_t text_len = strlen(text);
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);

    if (fd < 0) {
        return 0;
    }
    if (write(fd, text, text_len) != (ssize_t)text_len) {
        close(fd);
        return 0;
    }
    return close(fd) == 0;
}
#endif

static uint32_t encode_resource_list(uint8_t out[16], uint32_t handle)
{
    size_t offset = 0u;

    out[offset++] = SAP_WIT_TAG_RESOURCE;
    memcpy(out + offset, &handle, sizeof(handle));
    offset += sizeof(handle);
    return (uint32_t)offset;
}

int main(void)
{
    const char* argv[] = {"lambkin", "--demo", NULL};
    const char* envp[] = {"USER=tester", "EMPTY=", "NOSEP", NULL};
    croft_wit_wasi_machine_runtime_options options = {0};
    croft_wit_wasi_machine_runtime* runtime = NULL;
    SapWitCliCommandWorldImports cli_imports = {0};
    SapWitRandomImportsWorldImports random_imports = {0};
    SapWitClocksImportsWorldImports clocks_imports = {0};
    SapWitIoImportsWorldImports io_imports = {0};
    SapWitCliEnvironmentCommand cli_command = {0};
    SapWitCliEnvironmentReply cli_reply = {0};
    SapWitRandomCommand random_command = {0};
    SapWitRandomReply random_reply = {0};
    SapWitRandomInsecureSeedCommand seed_command = {0};
    SapWitRandomInsecureSeedReply seed_reply = {0};
    SapWitClocksMonotonicClockCommand monotonic_command = {0};
    SapWitClocksMonotonicClockReply monotonic_reply = {0};
    SapWitClocksWallClockCommand wall_command = {0};
    SapWitClocksWallClockReply wall_reply = {0};
    SapWitClocksDatetime wall_now = {0};
    SapWitClocksTimezoneCommand timezone_command = {0};
    SapWitClocksTimezoneReply timezone_reply = {0};
    SapWitIoStreamsCommand io_streams_command = {0};
    SapWitIoStreamsReply io_streams_reply = {0};
    SapWitIoPollCommand io_command = {0};
    SapWitIoPollReply io_reply = {0};
    SapWitFilesystemImportsWorldImports filesystem_imports = {0};
    SapWitFilesystemPreopensCommand filesystem_preopens_command = {0};
    SapWitFilesystemPreopensReply filesystem_preopens_reply = {0};
    SapWitFilesystemTypesCommand filesystem_types_command = {0};
    SapWitFilesystemTypesReply filesystem_types_reply = {0};
    ThatchCursor cursor = 0u;
    const uint8_t* text_data = NULL;
    uint32_t text_len = 0u;
    const uint8_t* value_data = NULL;
    uint32_t value_len = 0u;
    uint32_t ready_index = UINT32_MAX;
    uint8_t poll_blob[16] = {0};
    uint32_t poll_blob_len = 0u;
    uint32_t pollable_handle = 0u;
#if !defined(CROFT_OS_WINDOWS)
    croft_wit_wasi_machine_preopen preopen = {0};
    char fs_dir_template[] = "/tmp/croft-wasi-fs-smoke-XXXXXX";
    char* fs_dir = NULL;
    char fs_file_path[PATH_MAX] = {0};
    const char fs_file_name[] = "hello.txt";
    const char fs_file_contents[] = "hello filesystem";
    SapWitFilesystemDescriptorResource root_descriptor = 0u;
    SapWitFilesystemDescriptorResource file_descriptor = 0u;
    SapWitFilesystemDirectoryEntryStreamResource directory_stream = 0u;
    int found_directory_entry = 0;
#endif
    int ok = 1;

    croft_wit_wasi_machine_runtime_options_default(&options);
    options.argv = argv;
    options.argc = 2u;
    options.envp = envp;
    options.envc = 3u;
    options.inherit_environment = 0u;
    options.initial_cwd = "/virtual/cwd";
#if !defined(CROFT_OS_WINDOWS)
    fs_dir = mkdtemp(fs_dir_template);
    ok &= expect_true("filesystem temp dir", fs_dir != NULL);
    if (!fs_dir) {
        return 1;
    }
    snprintf(fs_file_path, sizeof(fs_file_path), "%s/%s", fs_dir, fs_file_name);
    ok &= expect_true("filesystem temp file", write_test_file(fs_file_path, fs_file_contents));
    preopen.host_path = fs_dir;
    preopen.guest_path = "/workspace";
    options.preopens = &preopen;
    options.preopen_count = 1u;
    options.inherit_preopen_cwd = 0u;
#endif

    runtime = croft_wit_wasi_machine_runtime_create(&options);
    ok &= expect_true("runtime", runtime != NULL);
    if (!runtime) {
#if !defined(CROFT_OS_WINDOWS)
        unlink(fs_file_path);
        rmdir(fs_dir);
#endif
        return 1;
    }

    ok &= expect_u32("bind cli",
                     (uint32_t)croft_wit_wasi_machine_runtime_bind_cli_command_imports(runtime,
                                                                                       &cli_imports),
                     0u);
    ok &= expect_u32("bind random",
                     (uint32_t)croft_wit_wasi_machine_runtime_bind_random_imports(runtime,
                                                                                  &random_imports),
                     0u);
    ok &= expect_u32("bind clocks",
                     (uint32_t)croft_wit_wasi_machine_runtime_bind_clocks_imports(runtime,
                                                                                  &clocks_imports),
                     0u);
    ok &= expect_u32("bind io",
                     (uint32_t)croft_wit_wasi_machine_runtime_bind_io_imports(runtime, &io_imports),
                     0u);
#if !defined(CROFT_OS_WINDOWS)
    ok &= expect_u32("bind filesystem",
                     (uint32_t)croft_wit_wasi_machine_runtime_bind_filesystem_imports(
                         runtime,
                         &filesystem_imports),
                     0u);
#endif

    cli_command.case_tag = SAP_WIT_CLI_ENVIRONMENT_COMMAND_GET_ARGUMENTS;
    ok &= expect_u32("cli get arguments",
                     (uint32_t)sap_wit_world_cli_command_import_environment(&cli_imports,
                                                                            &cli_command,
                                                                            &cli_reply),
                     0u);
    ok &= expect_u32("cli get arguments case",
                     cli_reply.case_tag,
                     SAP_WIT_CLI_ENVIRONMENT_REPLY_GET_ARGUMENTS);
    ok &= expect_u32("argv len", cli_reply.val.get_arguments.len, 2u);
    cursor = 0u;
    ok &= expect_u32("decode argv 0",
                     (uint32_t)decode_string(cli_reply.val.get_arguments.data,
                                             cli_reply.val.get_arguments.byte_len,
                                             &cursor,
                                             &text_data,
                                             &text_len),
                     0u);
    ok &= expect_str("argv 0", text_data, text_len, "lambkin");
    ok &= expect_u32("decode argv 1",
                     (uint32_t)decode_string(cli_reply.val.get_arguments.data,
                                             cli_reply.val.get_arguments.byte_len,
                                             &cursor,
                                             &text_data,
                                             &text_len),
                     0u);
    ok &= expect_str("argv 1", text_data, text_len, "--demo");

    cli_command.case_tag = SAP_WIT_CLI_ENVIRONMENT_COMMAND_GET_ENVIRONMENT;
    ok &= expect_u32("cli get environment",
                     (uint32_t)sap_wit_world_cli_command_import_environment(&cli_imports,
                                                                            &cli_command,
                                                                            &cli_reply),
                     0u);
    ok &= expect_u32("cli get environment case",
                     cli_reply.case_tag,
                     SAP_WIT_CLI_ENVIRONMENT_REPLY_GET_ENVIRONMENT);
    ok &= expect_u32("env len", cli_reply.val.get_environment.len, 3u);
    cursor = 0u;
    ok &= expect_u32("decode env 0",
                     (uint32_t)decode_env_pair(cli_reply.val.get_environment.data,
                                               cli_reply.val.get_environment.byte_len,
                                               &cursor,
                                               &text_data,
                                               &text_len,
                                               &value_data,
                                               &value_len),
                     0u);
    ok &= expect_str("env 0 key", text_data, text_len, "USER");
    ok &= expect_str("env 0 value", value_data, value_len, "tester");
    ok &= expect_u32("decode env 1",
                     (uint32_t)decode_env_pair(cli_reply.val.get_environment.data,
                                               cli_reply.val.get_environment.byte_len,
                                               &cursor,
                                               &text_data,
                                               &text_len,
                                               &value_data,
                                               &value_len),
                     0u);
    ok &= expect_str("env 1 key", text_data, text_len, "EMPTY");
    ok &= expect_str("env 1 value", value_data, value_len, "");
    ok &= expect_u32("decode env 2",
                     (uint32_t)decode_env_pair(cli_reply.val.get_environment.data,
                                               cli_reply.val.get_environment.byte_len,
                                               &cursor,
                                               &text_data,
                                               &text_len,
                                               &value_data,
                                               &value_len),
                     0u);
    ok &= expect_str("env 2 key", text_data, text_len, "NOSEP");
    ok &= expect_str("env 2 value", value_data, value_len, "");

    cli_command.case_tag = SAP_WIT_CLI_ENVIRONMENT_COMMAND_INITIAL_CWD;
    ok &= expect_u32("cli initial cwd",
                     (uint32_t)sap_wit_world_cli_command_import_environment(&cli_imports,
                                                                            &cli_command,
                                                                            &cli_reply),
                     0u);
    ok &= expect_u32("cli initial cwd case",
                     cli_reply.case_tag,
                     SAP_WIT_CLI_ENVIRONMENT_REPLY_INITIAL_CWD);
    ok &= expect_true("cwd present", cli_reply.val.initial_cwd.has_v == 1u);
    ok &= expect_str("cwd value",
                     cli_reply.val.initial_cwd.v_data,
                     cli_reply.val.initial_cwd.v_len,
                     "/virtual/cwd");

    random_command.case_tag = SAP_WIT_RANDOM_COMMAND_GET_RANDOM_BYTES;
    random_command.val.get_random_bytes.len = 32u;
    ok &= expect_u32("random bytes",
                     (uint32_t)sap_wit_world_random_imports_import_random(&random_imports,
                                                                          &random_command,
                                                                          &random_reply),
                     0u);
    ok &= expect_u32("random bytes case",
                     random_reply.case_tag,
                     SAP_WIT_RANDOM_REPLY_GET_RANDOM_BYTES);
    ok &= expect_u32("random bytes len", random_reply.val.get_random_bytes.len, 32u);
    ok &= expect_true("random bytes nonzero",
                      buffer_has_nonzero(random_reply.val.get_random_bytes.data,
                                         random_reply.val.get_random_bytes.len));

    random_command.case_tag = SAP_WIT_RANDOM_COMMAND_GET_RANDOM_U64;
    ok &= expect_u32("random u64",
                     (uint32_t)sap_wit_world_random_imports_import_random(&random_imports,
                                                                          &random_command,
                                                                          &random_reply),
                     0u);
    ok &= expect_u32("random u64 case",
                     random_reply.case_tag,
                     SAP_WIT_RANDOM_REPLY_GET_RANDOM_U64);

    seed_command.case_tag = SAP_WIT_RANDOM_INSECURE_SEED_COMMAND_INSECURE_SEED;
    ok &= expect_u32("seed",
                     (uint32_t)sap_wit_world_random_imports_import_insecure_seed(&random_imports,
                                                                                 &seed_command,
                                                                                 &seed_reply),
                     0u);
    ok &= expect_u32("seed case",
                     seed_reply.case_tag,
                     SAP_WIT_RANDOM_INSECURE_SEED_REPLY_INSECURE_SEED);
    ok &= expect_true("seed nonzero",
                      seed_reply.val.insecure_seed.v_0 != 0u
                          || seed_reply.val.insecure_seed.v_1 != 0u);

    monotonic_command.case_tag = SAP_WIT_CLOCKS_MONOTONIC_CLOCK_COMMAND_NOW;
    ok &= expect_u32("monotonic now",
                     (uint32_t)sap_wit_world_clocks_imports_import_monotonic_clock(
                         &clocks_imports,
                         &monotonic_command,
                         &monotonic_reply),
                     0u);
    ok &= expect_u32("monotonic now case",
                     monotonic_reply.case_tag,
                     SAP_WIT_CLOCKS_MONOTONIC_CLOCK_REPLY_NOW);
    ok &= expect_true("monotonic now value", monotonic_reply.val.now > 0u);

    monotonic_command.case_tag = SAP_WIT_CLOCKS_MONOTONIC_CLOCK_COMMAND_SUBSCRIBE;
    ok &= expect_u32("monotonic subscribe",
                     (uint32_t)sap_wit_world_clocks_imports_import_monotonic_clock(
                         &clocks_imports,
                         &monotonic_command,
                         &monotonic_reply),
                     0u);
    ok &= expect_u32("monotonic subscribe case",
                     monotonic_reply.case_tag,
                     SAP_WIT_CLOCKS_MONOTONIC_CLOCK_REPLY_POLLABLE);
    pollable_handle = monotonic_reply.val.pollable;
    ok &= expect_true("pollable handle", pollable_handle != 0u);

    wall_command.case_tag = SAP_WIT_CLOCKS_WALL_CLOCK_COMMAND_NOW;
    ok &= expect_u32("wall now",
                     (uint32_t)sap_wit_world_clocks_imports_import_wall_clock(&clocks_imports,
                                                                              &wall_command,
                                                                              &wall_reply),
                     0u);
    ok &= expect_u32("wall now case",
                     wall_reply.case_tag,
                     SAP_WIT_CLOCKS_WALL_CLOCK_REPLY_DATETIME);
    ok &= expect_true("wall now seconds", wall_reply.val.datetime.seconds > 0u);
    wall_now = wall_reply.val.datetime;

    wall_command.case_tag = SAP_WIT_CLOCKS_WALL_CLOCK_COMMAND_RESOLUTION;
    ok &= expect_u32("wall resolution",
                     (uint32_t)sap_wit_world_clocks_imports_import_wall_clock(&clocks_imports,
                                                                              &wall_command,
                                                                              &wall_reply),
                     0u);
    ok &= expect_u32("wall resolution case",
                     wall_reply.case_tag,
                     SAP_WIT_CLOCKS_WALL_CLOCK_REPLY_DATETIME);

    timezone_command.case_tag = SAP_WIT_CLOCKS_TIMEZONE_COMMAND_DISPLAY;
    timezone_command.val.display = wall_now;
    ok &= expect_u32("timezone display",
                     (uint32_t)sap_wit_world_clocks_imports_import_timezone(&clocks_imports,
                                                                            &timezone_command,
                                                                            &timezone_reply),
                     0u);
    ok &= expect_u32("timezone case",
                     timezone_reply.case_tag,
                     SAP_WIT_CLOCKS_TIMEZONE_REPLY_TIMEZONE_DISPLAY);
    ok &= expect_true("timezone name",
                      timezone_reply.val.timezone_display.name_data != NULL
                          && timezone_reply.val.timezone_display.name_len > 0u);
    ok &= expect_true("timezone offset range",
                      timezone_reply.val.timezone_display.utc_offset > -86400
                          && timezone_reply.val.timezone_display.utc_offset < 86400);

    io_streams_command.case_tag = SAP_WIT_IO_STREAMS_COMMAND_READ;
    io_streams_command.val.read.input_stream = 1u;
    io_streams_command.val.read.len = 16u;
    ok &= expect_u32("io streams read",
                     (uint32_t)sap_wit_world_io_imports_import_streams(&io_imports,
                                                                       &io_streams_command,
                                                                       &io_streams_reply),
                     0u);
    ok &= expect_u32("io streams read case",
                     io_streams_reply.case_tag,
                     SAP_WIT_IO_STREAMS_REPLY_READ);
    ok &= expect_true("io streams read err", io_streams_reply.val.read.is_v_ok == 0u);
    ok &= expect_u32("io streams read closed",
                     io_streams_reply.val.read.v_val.err.v.case_tag,
                     SAP_WIT_IO_STREAM_ERROR_CLOSED);

    io_command.case_tag = SAP_WIT_IO_POLL_COMMAND_READY;
    io_command.val.ready.pollable = (SapWitIoPollableResource)pollable_handle;
    ok &= expect_u32("io ready",
                     (uint32_t)sap_wit_world_io_imports_import_poll(&io_imports,
                                                                    &io_command,
                                                                    &io_reply),
                     0u);
    ok &= expect_u32("io ready case", io_reply.case_tag, SAP_WIT_IO_POLL_REPLY_READY);
    ok &= expect_true("io ready value", io_reply.val.ready == 1u);

    poll_blob_len = encode_resource_list(poll_blob, pollable_handle);
    io_command.case_tag = SAP_WIT_IO_POLL_COMMAND_POLL;
    io_command.val.poll.in_data = poll_blob;
    io_command.val.poll.in_len = 1u;
    io_command.val.poll.in_byte_len = poll_blob_len;
    ok &= expect_u32("io poll",
                     (uint32_t)sap_wit_world_io_imports_import_poll(&io_imports,
                                                                    &io_command,
                                                                    &io_reply),
                     0u);
    ok &= expect_u32("io poll case", io_reply.case_tag, SAP_WIT_IO_POLL_REPLY_POLL);
    ok &= expect_u32("io poll len", io_reply.val.poll.len, 1u);
    cursor = 0u;
    ok &= expect_u32("decode ready index",
                     (uint32_t)decode_u32(io_reply.val.poll.data,
                                          io_reply.val.poll.byte_len,
                                          &cursor,
                                          &ready_index),
                     0u);
    ok &= expect_u32("ready index", ready_index, 0u);

#if !defined(CROFT_OS_WINDOWS)
    filesystem_preopens_command.case_tag = SAP_WIT_FILESYSTEM_PREOPENS_COMMAND_GET_DIRECTORIES;
    ok &= expect_u32("filesystem get directories",
                     (uint32_t)sap_wit_world_filesystem_imports_import_preopens(
                         &filesystem_imports,
                         &filesystem_preopens_command,
                         &filesystem_preopens_reply),
                     0u);
    ok &= expect_u32("filesystem get directories case",
                     filesystem_preopens_reply.case_tag,
                     SAP_WIT_FILESYSTEM_PREOPENS_REPLY_GET_DIRECTORIES);
    ok &= expect_u32("filesystem preopen count",
                     filesystem_preopens_reply.val.get_directories.len,
                     1u);
    cursor = 0u;
    ok &= expect_u32("decode filesystem preopen",
                     (uint32_t)decode_resource_string_pair(
                         filesystem_preopens_reply.val.get_directories.data,
                         filesystem_preopens_reply.val.get_directories.byte_len,
                         &cursor,
                         &root_descriptor,
                         &text_data,
                         &text_len),
                     0u);
    ok &= expect_true("filesystem root descriptor", root_descriptor != 0u);
    ok &= expect_str("filesystem guest path", text_data, text_len, "/workspace");

    memset(&filesystem_types_command, 0, sizeof(filesystem_types_command));
    filesystem_types_command.case_tag = SAP_WIT_FILESYSTEM_TYPES_COMMAND_OPEN_AT;
    filesystem_types_command.val.open_at.descriptor = root_descriptor;
    filesystem_types_command.val.open_at.path_data = (const uint8_t*)"/etc/passwd";
    filesystem_types_command.val.open_at.path_len = 11u;
    filesystem_types_command.val.open_at.flags = SAP_WIT_FILESYSTEM_DESCRIPTOR_FLAGS_READ;
    ok &= expect_u32("filesystem reject absolute path",
                     (uint32_t)sap_wit_world_filesystem_imports_import_types(
                         &filesystem_imports,
                         &filesystem_types_command,
                         &filesystem_types_reply),
                     0u);
    ok &= expect_u32("filesystem reject absolute path case",
                     filesystem_types_reply.case_tag,
                     SAP_WIT_FILESYSTEM_TYPES_REPLY_DESCRIPTOR);
    ok &= expect_true("filesystem reject absolute path err",
                      filesystem_types_reply.val.descriptor.is_v_ok == 0u);
    ok &= expect_u32("filesystem reject absolute path code",
                     filesystem_types_reply.val.descriptor.v_val.err.v,
                     SAP_WIT_FILESYSTEM_ERROR_CODE_NOT_PERMITTED);

    memset(&filesystem_types_command, 0, sizeof(filesystem_types_command));
    filesystem_types_command.case_tag = SAP_WIT_FILESYSTEM_TYPES_COMMAND_OPEN_AT;
    filesystem_types_command.val.open_at.descriptor = root_descriptor;
    filesystem_types_command.val.open_at.path_data = (const uint8_t*)fs_file_name;
    filesystem_types_command.val.open_at.path_len = (uint32_t)strlen(fs_file_name);
    filesystem_types_command.val.open_at.flags = SAP_WIT_FILESYSTEM_DESCRIPTOR_FLAGS_READ;
    ok &= expect_u32("filesystem open file",
                     (uint32_t)sap_wit_world_filesystem_imports_import_types(
                         &filesystem_imports,
                         &filesystem_types_command,
                         &filesystem_types_reply),
                     0u);
    ok &= expect_u32("filesystem open file case",
                     filesystem_types_reply.case_tag,
                     SAP_WIT_FILESYSTEM_TYPES_REPLY_DESCRIPTOR);
    ok &= expect_true("filesystem open file ok",
                      filesystem_types_reply.val.descriptor.is_v_ok == 1u);
    file_descriptor = filesystem_types_reply.val.descriptor.v_val.ok.v;
    ok &= expect_true("filesystem file descriptor", file_descriptor != 0u);

    memset(&filesystem_types_command, 0, sizeof(filesystem_types_command));
    filesystem_types_command.case_tag = SAP_WIT_FILESYSTEM_TYPES_COMMAND_STAT;
    filesystem_types_command.val.stat.descriptor = file_descriptor;
    ok &= expect_u32("filesystem stat",
                     (uint32_t)sap_wit_world_filesystem_imports_import_types(
                         &filesystem_imports,
                         &filesystem_types_command,
                         &filesystem_types_reply),
                     0u);
    ok &= expect_u32("filesystem stat case",
                     filesystem_types_reply.case_tag,
                     SAP_WIT_FILESYSTEM_TYPES_REPLY_STAT);
    ok &= expect_true("filesystem stat ok", filesystem_types_reply.val.stat.is_v_ok == 1u);
    ok &= expect_u32("filesystem stat type",
                     filesystem_types_reply.val.stat.v_val.ok.v.type,
                     SAP_WIT_FILESYSTEM_DESCRIPTOR_TYPE_REGULAR_FILE);
    ok &= expect_u64("filesystem stat size",
                     filesystem_types_reply.val.stat.v_val.ok.v.size,
                     (uint64_t)strlen(fs_file_contents));

    memset(&filesystem_types_command, 0, sizeof(filesystem_types_command));
    filesystem_types_command.case_tag = SAP_WIT_FILESYSTEM_TYPES_COMMAND_READ;
    filesystem_types_command.val.read.descriptor = file_descriptor;
    filesystem_types_command.val.read.length = (uint64_t)strlen(fs_file_contents);
    filesystem_types_command.val.read.offset = 0u;
    ok &= expect_u32("filesystem read",
                     (uint32_t)sap_wit_world_filesystem_imports_import_types(
                         &filesystem_imports,
                         &filesystem_types_command,
                         &filesystem_types_reply),
                     0u);
    ok &= expect_u32("filesystem read case",
                     filesystem_types_reply.case_tag,
                     SAP_WIT_FILESYSTEM_TYPES_REPLY_READ);
    ok &= expect_true("filesystem read ok", filesystem_types_reply.val.read.is_v_ok == 1u);
    ok &= expect_str("filesystem read data",
                     filesystem_types_reply.val.read.v_val.ok.v_0_data,
                     filesystem_types_reply.val.read.v_val.ok.v_0_len,
                     fs_file_contents);
    ok &= expect_true("filesystem read eof", filesystem_types_reply.val.read.v_val.ok.v_1 == 1u);

    memset(&filesystem_types_command, 0, sizeof(filesystem_types_command));
    filesystem_types_command.case_tag = SAP_WIT_FILESYSTEM_TYPES_COMMAND_READ_DIRECTORY;
    filesystem_types_command.val.read_directory.descriptor = root_descriptor;
    ok &= expect_u32("filesystem read directory",
                     (uint32_t)sap_wit_world_filesystem_imports_import_types(
                         &filesystem_imports,
                         &filesystem_types_command,
                         &filesystem_types_reply),
                     0u);
    ok &= expect_u32("filesystem read directory case",
                     filesystem_types_reply.case_tag,
                     SAP_WIT_FILESYSTEM_TYPES_REPLY_DIRECTORY_ENTRY_STREAM);
    ok &= expect_true("filesystem read directory ok",
                      filesystem_types_reply.val.directory_entry_stream.is_v_ok == 1u);
    directory_stream = filesystem_types_reply.val.directory_entry_stream.v_val.ok.v;
    ok &= expect_true("filesystem directory stream", directory_stream != 0u);

    for (int attempt = 0; attempt < 8 && !found_directory_entry; attempt++) {
        memset(&filesystem_types_command, 0, sizeof(filesystem_types_command));
        filesystem_types_command.case_tag = SAP_WIT_FILESYSTEM_TYPES_COMMAND_READ_DIRECTORY_ENTRY;
        filesystem_types_command.val.read_directory_entry.directory_entry_stream = directory_stream;
        ok &= expect_u32("filesystem read directory entry",
                         (uint32_t)sap_wit_world_filesystem_imports_import_types(
                             &filesystem_imports,
                             &filesystem_types_command,
                             &filesystem_types_reply),
                         0u);
        ok &= expect_u32("filesystem read directory entry case",
                         filesystem_types_reply.case_tag,
                         SAP_WIT_FILESYSTEM_TYPES_REPLY_READ_DIRECTORY_ENTRY);
        ok &= expect_true("filesystem read directory entry ok",
                          filesystem_types_reply.val.read_directory_entry.is_v_ok == 1u);
        if (!filesystem_types_reply.val.read_directory_entry.v_val.ok.has_v) {
            break;
        }
        if (filesystem_types_reply.val.read_directory_entry.v_val.ok.v.name_len
                == strlen(fs_file_name)
            && memcmp(filesystem_types_reply.val.read_directory_entry.v_val.ok.v.name_data,
                      fs_file_name,
                      strlen(fs_file_name))
                   == 0) {
            found_directory_entry = 1;
        }
    }
    ok &= expect_true("filesystem directory entry found", found_directory_entry);
#endif

    croft_wit_wasi_machine_runtime_destroy(runtime);
#if !defined(CROFT_OS_WINDOWS)
    unlink(fs_file_path);
    rmdir(fs_dir);
#endif
    return ok ? 0 : 1;
}
