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
    uint8_t is_seekable;
    uint8_t is_terminal;
    uint8_t error_domain;
} croft_wit_wasi_descriptor_slot;

typedef struct {
    uint8_t live;
    uint32_t descriptor_handle;
    uint64_t offset;
    uint32_t error_handle;
    uint8_t closed;
} croft_wit_wasi_input_stream_slot;

typedef struct {
    uint8_t live;
    uint8_t append;
    uint32_t descriptor_handle;
    uint64_t offset;
    uint32_t error_handle;
    uint8_t closed;
} croft_wit_wasi_output_stream_slot;

typedef struct {
    uint8_t live;
    DIR* dir;
} croft_wit_wasi_directory_stream_slot;

enum croft_wit_wasi_error_domain {
    CROFT_WIT_WASI_ERROR_DOMAIN_NONE = 0,
    CROFT_WIT_WASI_ERROR_DOMAIN_IO = 1,
    CROFT_WIT_WASI_ERROR_DOMAIN_FILESYSTEM = 2,
};

typedef struct {
    uint8_t live;
    uint8_t domain;
    uint8_t filesystem_error_code;
    uint8_t reserved;
    char* debug_string;
} croft_wit_wasi_error_slot;

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
    croft_wit_wasi_input_stream_slot* input_streams;
    size_t input_stream_count;
    size_t input_stream_cap;
    croft_wit_wasi_output_stream_slot* output_streams;
    size_t output_stream_count;
    size_t output_stream_cap;
    croft_wit_wasi_directory_stream_slot* directory_streams;
    size_t directory_stream_count;
    size_t directory_stream_cap;
    croft_wit_wasi_error_slot* errors;
    size_t error_count;
    size_t error_cap;
    croft_wit_wasi_preopen_slot* preopens;
    size_t preopen_count;
    size_t preopen_cap;
    uint32_t stdin_descriptor_handle;
    uint32_t stdout_descriptor_handle;
    uint32_t stderr_descriptor_handle;
    uint32_t stdin_stream_handle;
    uint32_t stdout_stream_handle;
    uint32_t stderr_stream_handle;
    uint32_t next_pollable_handle;
};

static croft_wit_wasi_descriptor_slot* croft_wit_wasi_fs_descriptor_lookup(
    croft_wit_wasi_machine_runtime* runtime,
    uint32_t handle);
static croft_wit_wasi_input_stream_slot* croft_wit_wasi_input_stream_lookup(
    croft_wit_wasi_machine_runtime* runtime,
    uint32_t handle);
static croft_wit_wasi_output_stream_slot* croft_wit_wasi_output_stream_lookup(
    croft_wit_wasi_machine_runtime* runtime,
    uint32_t handle);
static croft_wit_wasi_error_slot* croft_wit_wasi_error_lookup(
    croft_wit_wasi_machine_runtime* runtime,
    uint32_t handle);

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

#include "wit_wasi_machine_runtime_common.inc"
#include "wit_wasi_machine_runtime_filesystem.inc"
#include "wit_wasi_machine_runtime_runtime.inc"
#include "wit_wasi_machine_runtime_cli_random.inc"
#include "wit_wasi_machine_runtime_clocks_io.inc"
