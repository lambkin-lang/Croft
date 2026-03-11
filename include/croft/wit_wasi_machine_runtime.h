#ifndef CROFT_WIT_WASI_MACHINE_RUNTIME_H
#define CROFT_WIT_WASI_MACHINE_RUNTIME_H

#include "croft/wit_world_runtime.h"
#include "generated/wit_wasi_cli_command.h"
#include "generated/wit_wasi_clocks_world.h"
#include "generated/wit_wasi_filesystem_world.h"
#include "generated/wit_wasi_io_world.h"
#include "generated/wit_wasi_random_world.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct croft_wit_wasi_machine_runtime croft_wit_wasi_machine_runtime;

typedef enum croft_wit_wasi_machine_bundle_kind {
    CROFT_WIT_WASI_MACHINE_BUNDLE_CLI_STDIO_TERMINAL = 0,
    CROFT_WIT_WASI_MACHINE_BUNDLE_RANDOM = 1,
    CROFT_WIT_WASI_MACHINE_BUNDLE_CLOCKS_POLL = 2,
    CROFT_WIT_WASI_MACHINE_BUNDLE_FILESYSTEM_STREAMS = 3,
} croft_wit_wasi_machine_bundle_kind;

typedef struct croft_wit_wasi_machine_substrate_descriptor {
    const char* name;
    const char* kind;
    const char* applicability;
    const char* description;
} croft_wit_wasi_machine_substrate_descriptor;

typedef struct croft_wit_wasi_machine_bundle_descriptor {
    croft_wit_wasi_machine_bundle_kind kind;
    const char* name;
    const char* support_status;
    const char* applicability;
    const char* description;
    const char* const* substrates;
    uint32_t substrate_count;
    const char* const* declared_worlds;
    uint32_t declared_world_count;
    const char* const* expanded_surfaces;
    uint32_t expanded_surface_count;
    const char* const* helper_interfaces;
    uint32_t helper_interface_count;
} croft_wit_wasi_machine_bundle_descriptor;

typedef struct croft_wit_wasi_machine_preopen {
    const char* host_path;
    const char* guest_path;
} croft_wit_wasi_machine_preopen;

typedef struct croft_wit_wasi_machine_runtime_options {
    const char* const* argv;
    uint32_t argc;
    const char* const* envp;
    uint32_t envc;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
    const char* initial_cwd;
    const croft_wit_wasi_machine_preopen* preopens;
    uint32_t preopen_count;
    uint8_t inherit_environment;
    uint8_t inherit_stdio;
    uint8_t inherit_preopen_cwd;
} croft_wit_wasi_machine_runtime_options;

void croft_wit_wasi_machine_runtime_options_default(
    croft_wit_wasi_machine_runtime_options* options);

extern const croft_wit_wasi_machine_substrate_descriptor croft_wit_wasi_machine_substrates[];
extern const uint32_t croft_wit_wasi_machine_substrates_count;

extern const croft_wit_wasi_machine_bundle_descriptor croft_wit_wasi_machine_bundles[];
extern const uint32_t croft_wit_wasi_machine_bundles_count;

const croft_wit_wasi_machine_substrate_descriptor* croft_wit_wasi_machine_find_substrate_descriptor(
    const char* name);

const croft_wit_wasi_machine_bundle_descriptor* croft_wit_wasi_machine_find_bundle_descriptor(
    const char* name);

croft_wit_wasi_machine_runtime* croft_wit_wasi_machine_runtime_create(
    const croft_wit_wasi_machine_runtime_options* options);

void croft_wit_wasi_machine_runtime_destroy(croft_wit_wasi_machine_runtime* runtime);

/*
 * Bind the current-machine host capability bundle into generated WIT world
 * import structs. The runtime owns all pointed-to reply storage; callers should
 * treat reply payload pointers as valid until the next dispatch on the same
 * runtime or until destroy.
 */
int croft_wit_wasi_machine_runtime_bind_cli_command_imports(
    croft_wit_wasi_machine_runtime* runtime,
    SapWitCliCommandWorldImports* bindings);

int croft_wit_wasi_machine_runtime_bind_cli_imports(
    croft_wit_wasi_machine_runtime* runtime,
    SapWitCliImportsWorldImports* bindings);

int croft_wit_wasi_machine_runtime_bind_random_imports(
    croft_wit_wasi_machine_runtime* runtime,
    SapWitRandomImportsWorldImports* bindings);

int croft_wit_wasi_machine_runtime_bind_clocks_imports(
    croft_wit_wasi_machine_runtime* runtime,
    SapWitClocksImportsWorldImports* bindings);

int croft_wit_wasi_machine_runtime_bind_io_imports(
    croft_wit_wasi_machine_runtime* runtime,
    SapWitIoImportsWorldImports* bindings);

int croft_wit_wasi_machine_runtime_bind_filesystem_imports(
    croft_wit_wasi_machine_runtime* runtime,
    SapWitFilesystemImportsWorldImports* bindings);

/*
 * Direct interface dispatch for imported resource helper interfaces which are
 * not necessarily present as world items in the current guest.
 */
int32_t croft_wit_wasi_machine_runtime_dispatch_io_error(
    croft_wit_wasi_machine_runtime* runtime,
    const SapWitIoErrorCommand* command,
    SapWitIoErrorReply* reply_out);

int32_t croft_wit_wasi_machine_runtime_dispatch_filesystem_error(
    croft_wit_wasi_machine_runtime* runtime,
    const SapWitFilesystemErrorCommand* command,
    SapWitFilesystemErrorReply* reply_out);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_WIT_WASI_MACHINE_RUNTIME_H */
