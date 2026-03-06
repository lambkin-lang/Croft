#ifndef CROFT_HOST_WASM_H
#define CROFT_HOST_WASM_H

#include "croft/host_queue.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct host_wasm_ctx host_wasm_ctx_t;

/**
 * Initialize a new Wasm3 environment and load the provided Wasm byte code.
 * Replaces the `test_alloc` with the provided `allocator` (e.g. SapMemArena integration if desired).
 * 
 * Returns a new context on success, NULL on failure.
 */
host_wasm_ctx_t *host_wasm_create(const uint8_t *wasm_bytes, uint32_t wasm_len, uint32_t stack_size);

/**
 * Clean up the Wasm environment, freeing all internal Wasm3 structures.
 */
void host_wasm_destroy(host_wasm_ctx_t *ctx);

/**
 * Get a pointer to the Wasm instance's linear memory.
 */
uint8_t *host_wasm_get_memory(host_wasm_ctx_t *ctx, uint32_t *out_size);

/**
 * Call a Wasm exported function by name using Wasm3 argv-style string arguments.
 * `argc` specifies the number of string arguments in `argv`.
 */
int32_t host_wasm_call(host_wasm_ctx_t *ctx, const char *func_name, int argc, const char *argv[]);

/**
 * Implementation of `SapWasiGuestLogicV0` compliant callback.
 * When SapRunner executes `sap_runner_v0_worker_tick`, it invokes this function.
 * This internally bridges into the Wasm `wasm_handle_event` or a dynamically mapped
 * intent receiver in the `.wasm` file.
 */
int host_wasm_runner_logic(void *userdata_ctx, void *host_api, const uint8_t *req,
                           uint32_t req_len, uint8_t *reply, uint32_t reply_cap,
                           uint32_t *out_reply_len);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_HOST_WASM_H */
