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
