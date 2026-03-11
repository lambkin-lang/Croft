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
    const char* item_name;
    const void* ops;
} croft_wit_wasi_endpoint_binding;

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

static int croft_wit_wasi_bind_endpoint_group(
    void* bindings,
    const SapWitWorldEndpointDescriptor* endpoints,
    uint32_t count,
    void* ctx,
    const croft_wit_wasi_endpoint_binding* items,
    size_t item_count)
{
    size_t i;
    int rc;

    if (!bindings || !endpoints || !items) {
        return ERR_INVALID;
    }
    for (i = 0u; i < item_count; i++) {
        rc = croft_wit_wasi_bind_endpoint(bindings,
                                          endpoints,
                                          count,
                                          items[i].item_name,
                                          ctx,
                                          items[i].ops);
        if (rc != ERR_OK) {
            return rc;
        }
    }
    return ERR_OK;
}

#include "wit_wasi_machine_runtime_common.inc"
#include "wit_wasi_machine_runtime_filesystem.inc"
#include "wit_wasi_machine_runtime_runtime.inc"
#include "wit_wasi_machine_runtime_cli_random.inc"
#include "wit_wasi_machine_runtime_clocks_io.inc"

static const char* g_croft_wit_wasi_cli_bundle_substrates[] = {
    "wasi-process-context",
    "wasi-byte-streams",
    "wasi-descriptor-table",
};

static const char* g_croft_wit_wasi_cli_bundle_worlds[] = {
    "wasi:cli@0.2.9/imports",
    "wasi:cli@0.2.9/command",
};

static const char* g_croft_wit_wasi_cli_bundle_surfaces[] = {
    "wasi:cli@0.2.9/environment",
    "wasi:cli@0.2.9/stdio",
    "wasi:cli@0.2.9/terminal",
    "wasi:cli@0.2.9/run",
};

static const char* g_croft_wit_wasi_random_bundle_substrates[] = {
    "wasi-system-random",
};

static const char* g_croft_wit_wasi_random_bundle_worlds[] = {
    "wasi:random@0.2.9/world",
};

static const char* g_croft_wit_wasi_random_bundle_surfaces[] = {
    "wasi:random@0.2.9/random",
    "wasi:random@0.2.9/insecure",
    "wasi:random@0.2.9/insecure-seed",
};

static const char* g_croft_wit_wasi_clocks_bundle_substrates[] = {
    "wasi-time-base",
    "wasi-pollables",
};

static const char* g_croft_wit_wasi_clocks_bundle_worlds[] = {
    "wasi:clocks@0.2.9/world",
    "wasi:io@0.2.9/world",
};

static const char* g_croft_wit_wasi_clocks_bundle_surfaces[] = {
    "wasi:clocks@0.2.9/monotonic-clock",
    "wasi:clocks@0.2.9/wall-clock",
    "wasi:clocks@0.2.9/timezone",
    "wasi:io@0.2.9/poll",
};

static const char* g_croft_wit_wasi_filesystem_bundle_substrates[] = {
    "wasi-byte-streams",
    "wasi-descriptor-table",
};

static const char* g_croft_wit_wasi_filesystem_bundle_worlds[] = {
    "wasi:filesystem@0.2.9/world",
    "wasi:io@0.2.9/world",
};

static const char* g_croft_wit_wasi_filesystem_bundle_surfaces[] = {
    "wasi:filesystem@0.2.9/types",
    "wasi:filesystem@0.2.9/preopens",
    "wasi:io@0.2.9/streams",
};

static const char* g_croft_wit_wasi_filesystem_bundle_helpers[] = {
    "wasi:io@0.2.9/error",
    "wasi:filesystem@0.2.9/error",
};

const croft_wit_wasi_machine_substrate_descriptor croft_wit_wasi_machine_substrates[] = {
    {
        "wasi-process-context",
        "process-context",
        "current-machine-unix",
        "Process arguments, environment, cwd, and run-status views shared by CLI surfaces.",
    },
    {
        "wasi-byte-streams",
        "byte-streams",
        "current-machine-unix",
        "Shared byte-stream substrate backing CLI stdio, filesystem streams, and io/streams.",
    },
    {
        "wasi-descriptor-table",
        "descriptor-table",
        "current-machine-unix",
        "Shared descriptor/resource table for stdio, terminal authority, filesystem descriptors, and streams.",
    },
    {
        "wasi-pollables",
        "pollable-set",
        "current-machine-unix",
        "Shared pollable handle table used by monotonic subscriptions and io/poll readiness.",
    },
    {
        "wasi-system-random",
        "entropy-source",
        "current-machine",
        "System randomness source used by secure and insecure random interfaces on the current machine.",
    },
    {
        "wasi-time-base",
        "clock-source",
        "current-machine-unix",
        "Host clock/timezone substrate used by monotonic, wall-clock, and timezone surfaces.",
    },
};

const uint32_t croft_wit_wasi_machine_substrates_count =
    (uint32_t)(sizeof(croft_wit_wasi_machine_substrates)
               / sizeof(croft_wit_wasi_machine_substrates[0]));

const croft_wit_wasi_machine_bundle_descriptor croft_wit_wasi_machine_bundles[] = {
    {
        CROFT_WIT_WASI_MACHINE_BUNDLE_CLI_STDIO_TERMINAL,
        "wasi-cli-stdio-terminal-current-machine",
        "host-implemented-subset",
        "current-machine-unix",
        "CLI world bindings backed by shared process-context, byte-stream, and descriptor substrates.",
        g_croft_wit_wasi_cli_bundle_substrates,
        (uint32_t)(sizeof(g_croft_wit_wasi_cli_bundle_substrates)
                   / sizeof(g_croft_wit_wasi_cli_bundle_substrates[0])),
        g_croft_wit_wasi_cli_bundle_worlds,
        (uint32_t)(sizeof(g_croft_wit_wasi_cli_bundle_worlds)
                   / sizeof(g_croft_wit_wasi_cli_bundle_worlds[0])),
        g_croft_wit_wasi_cli_bundle_surfaces,
        (uint32_t)(sizeof(g_croft_wit_wasi_cli_bundle_surfaces)
                   / sizeof(g_croft_wit_wasi_cli_bundle_surfaces[0])),
        NULL,
        0u,
    },
    {
        CROFT_WIT_WASI_MACHINE_BUNDLE_RANDOM,
        "wasi-random-current-machine",
        "host-implemented-subset",
        "current-machine",
        "Current-machine random bundle over one shared system entropy source.",
        g_croft_wit_wasi_random_bundle_substrates,
        (uint32_t)(sizeof(g_croft_wit_wasi_random_bundle_substrates)
                   / sizeof(g_croft_wit_wasi_random_bundle_substrates[0])),
        g_croft_wit_wasi_random_bundle_worlds,
        (uint32_t)(sizeof(g_croft_wit_wasi_random_bundle_worlds)
                   / sizeof(g_croft_wit_wasi_random_bundle_worlds[0])),
        g_croft_wit_wasi_random_bundle_surfaces,
        (uint32_t)(sizeof(g_croft_wit_wasi_random_bundle_surfaces)
                   / sizeof(g_croft_wit_wasi_random_bundle_surfaces[0])),
        NULL,
        0u,
    },
    {
        CROFT_WIT_WASI_MACHINE_BUNDLE_CLOCKS_POLL,
        "wasi-clocks-poll-current-machine",
        "host-implemented-subset",
        "current-machine-unix",
        "Current-machine clocks and poll bundle sharing time-base and pollable substrates.",
        g_croft_wit_wasi_clocks_bundle_substrates,
        (uint32_t)(sizeof(g_croft_wit_wasi_clocks_bundle_substrates)
                   / sizeof(g_croft_wit_wasi_clocks_bundle_substrates[0])),
        g_croft_wit_wasi_clocks_bundle_worlds,
        (uint32_t)(sizeof(g_croft_wit_wasi_clocks_bundle_worlds)
                   / sizeof(g_croft_wit_wasi_clocks_bundle_worlds[0])),
        g_croft_wit_wasi_clocks_bundle_surfaces,
        (uint32_t)(sizeof(g_croft_wit_wasi_clocks_bundle_surfaces)
                   / sizeof(g_croft_wit_wasi_clocks_bundle_surfaces[0])),
        NULL,
        0u,
    },
    {
        CROFT_WIT_WASI_MACHINE_BUNDLE_FILESYSTEM_STREAMS,
        "wasi-filesystem-streams-current-machine",
        "host-implemented-subset",
        "current-machine-unix",
        "Filesystem and io/streams bundle sharing descriptor and byte-stream substrates plus helper errors.",
        g_croft_wit_wasi_filesystem_bundle_substrates,
        (uint32_t)(sizeof(g_croft_wit_wasi_filesystem_bundle_substrates)
                   / sizeof(g_croft_wit_wasi_filesystem_bundle_substrates[0])),
        g_croft_wit_wasi_filesystem_bundle_worlds,
        (uint32_t)(sizeof(g_croft_wit_wasi_filesystem_bundle_worlds)
                   / sizeof(g_croft_wit_wasi_filesystem_bundle_worlds[0])),
        g_croft_wit_wasi_filesystem_bundle_surfaces,
        (uint32_t)(sizeof(g_croft_wit_wasi_filesystem_bundle_surfaces)
                   / sizeof(g_croft_wit_wasi_filesystem_bundle_surfaces[0])),
        g_croft_wit_wasi_filesystem_bundle_helpers,
        (uint32_t)(sizeof(g_croft_wit_wasi_filesystem_bundle_helpers)
                   / sizeof(g_croft_wit_wasi_filesystem_bundle_helpers[0])),
    },
};

const uint32_t croft_wit_wasi_machine_bundles_count =
    (uint32_t)(sizeof(croft_wit_wasi_machine_bundles)
               / sizeof(croft_wit_wasi_machine_bundles[0]));

const croft_wit_wasi_machine_substrate_descriptor* croft_wit_wasi_machine_find_substrate_descriptor(
    const char* name)
{
    uint32_t i;

    if (!name) {
        return NULL;
    }
    for (i = 0u; i < croft_wit_wasi_machine_substrates_count; i++) {
        if (strcmp(croft_wit_wasi_machine_substrates[i].name, name) == 0) {
            return &croft_wit_wasi_machine_substrates[i];
        }
    }
    return NULL;
}

const croft_wit_wasi_machine_bundle_descriptor* croft_wit_wasi_machine_find_bundle_descriptor(
    const char* name)
{
    uint32_t i;

    if (!name) {
        return NULL;
    }
    for (i = 0u; i < croft_wit_wasi_machine_bundles_count; i++) {
        if (strcmp(croft_wit_wasi_machine_bundles[i].name, name) == 0) {
            return &croft_wit_wasi_machine_bundles[i];
        }
    }
    return NULL;
}
