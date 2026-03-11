#ifndef CROFT_WIT_CROFT_WASM_GUEST_H
#define CROFT_WIT_CROFT_WASM_GUEST_H

#include "croft/wit_guest_runtime.h"
#include "sapling/err.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t (*SapWitCroftWasmGuestFindEndpointFn)(const uint8_t *name_ptr,
                                                      uint32_t name_len);

typedef int32_t (*SapWitCroftWasmGuestCallEndpointFn)(int32_t endpoint_handle,
                                                      const uint8_t *command_ptr,
                                                      uint32_t command_len,
                                                      uint8_t *reply_ptr,
                                                      uint32_t reply_cap,
                                                      uint32_t *reply_len_out);

typedef struct {
    SapWitCroftWasmGuestFindEndpointFn find_endpoint;
    SapWitCroftWasmGuestCallEndpointFn call_endpoint;
} SapWitCroftWasmGuestImports;

typedef struct {
    const SapWitWorldEndpointDescriptor *endpoint;
    int32_t handle;
} SapWitCroftWasmGuestEndpointHandle;

typedef struct {
    SapWitCroftWasmGuestImports        imports;
    SapWitCroftWasmGuestEndpointHandle *handles;
    uint32_t                           handle_count;
    uint32_t                           handle_capacity;
} SapWitCroftWasmGuestContext;

#if defined(__wasm32__)
__attribute__((import_module("env"), import_name("croft_wit_find_endpoint")))
int32_t sap_wit_croft_wasm_import_find_endpoint(const uint8_t *name_ptr, uint32_t name_len);

__attribute__((import_module("env"), import_name("croft_wit_call_endpoint")))
int32_t sap_wit_croft_wasm_import_call_endpoint(int32_t endpoint_handle,
                                                const uint8_t *command_ptr,
                                                uint32_t command_len,
                                                uint8_t *reply_ptr,
                                                uint32_t reply_cap,
                                                uint32_t *reply_len_out);
#endif

static inline int32_t sap_wit_croft_wasm_guest_normalize_rc(int32_t rc)
{
    return rc < 0 ? -rc : rc;
}

static inline void sap_wit_croft_wasm_guest_context_init(SapWitCroftWasmGuestContext *ctx,
                                                         SapWitCroftWasmGuestImports imports)
{
    if (!ctx) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->imports = imports;
}

#if defined(__wasm32__)
static inline void sap_wit_croft_wasm_guest_context_init_default(SapWitCroftWasmGuestContext *ctx)
{
    SapWitCroftWasmGuestImports imports;

    imports.find_endpoint = sap_wit_croft_wasm_import_find_endpoint;
    imports.call_endpoint = sap_wit_croft_wasm_import_call_endpoint;
    sap_wit_croft_wasm_guest_context_init(ctx, imports);
}
#endif

static inline void sap_wit_croft_wasm_guest_context_dispose(SapWitCroftWasmGuestContext *ctx)
{
    if (!ctx) {
        return;
    }
    free(ctx->handles);
    memset(ctx, 0, sizeof(*ctx));
}

static inline int sap_wit_croft_wasm_guest_context_reserve(SapWitCroftWasmGuestContext *ctx,
                                                           uint32_t need)
{
    SapWitCroftWasmGuestEndpointHandle *grown;
    uint32_t capacity;

    if (!ctx) {
        return ERR_INVALID;
    }
    if (need <= ctx->handle_capacity) {
        return ERR_OK;
    }

    capacity = ctx->handle_capacity > 0u ? ctx->handle_capacity : 8u;
    while (capacity < need) {
        if (capacity > UINT32_MAX / 2u) {
            capacity = need;
            break;
        }
        capacity *= 2u;
    }

    grown = (SapWitCroftWasmGuestEndpointHandle *)realloc(
        ctx->handles,
        (size_t)capacity * sizeof(*grown));
    if (!grown) {
        return ERR_OOM;
    }

    ctx->handles = grown;
    ctx->handle_capacity = capacity;
    return ERR_OK;
}

static inline int32_t sap_wit_croft_wasm_guest_find_handle(SapWitCroftWasmGuestContext *ctx,
                                                           const SapWitWorldEndpointDescriptor *endpoint,
                                                           const char *qualified_name)
{
    uint32_t i;
    int32_t handle;
    int rc;

    if (!ctx || !endpoint || !qualified_name || !ctx->imports.find_endpoint) {
        return ERR_INVALID;
    }

    for (i = 0u; i < ctx->handle_count; i++) {
        if (ctx->handles[i].endpoint == endpoint) {
            return ctx->handles[i].handle;
        }
    }

    handle = ctx->imports.find_endpoint((const uint8_t *)qualified_name,
                                        (uint32_t)strlen(qualified_name));
    if (handle <= 0) {
        return handle == 0 ? ERR_NOT_FOUND : sap_wit_croft_wasm_guest_normalize_rc(handle);
    }

    rc = sap_wit_croft_wasm_guest_context_reserve(ctx, ctx->handle_count + 1u);
    if (rc != ERR_OK) {
        return rc;
    }

    ctx->handles[ctx->handle_count].endpoint = endpoint;
    ctx->handles[ctx->handle_count].handle = handle;
    ctx->handle_count++;
    return handle;
}

static inline int32_t sap_wit_croft_wasm_guest_invoke(void *ctx_ptr,
                                                      const SapWitWorldEndpointDescriptor *endpoint,
                                                      const char *qualified_name,
                                                      const uint8_t *command_data,
                                                      uint32_t command_len,
                                                      uint8_t *reply_data,
                                                      uint32_t reply_cap,
                                                      uint32_t *reply_len_out)
{
    SapWitCroftWasmGuestContext *ctx = (SapWitCroftWasmGuestContext *)ctx_ptr;
    int32_t handle;
    int32_t rc;

    if (!ctx || !endpoint || !qualified_name || !ctx->imports.call_endpoint || !reply_len_out) {
        return ERR_INVALID;
    }

    handle = sap_wit_croft_wasm_guest_find_handle(ctx, endpoint, qualified_name);
    if (handle <= 0) {
        return handle == 0 ? ERR_NOT_FOUND : handle;
    }

    rc = ctx->imports.call_endpoint(handle,
                                    command_data,
                                    command_len,
                                    reply_data,
                                    reply_cap,
                                    reply_len_out);
    return sap_wit_croft_wasm_guest_normalize_rc(rc);
}

static inline void sap_wit_croft_wasm_guest_transport_init(SapWitGuestTransport *transport,
                                                           SapWitCroftWasmGuestContext *ctx)
{
    sap_wit_guest_transport_init(transport, ctx, sap_wit_croft_wasm_guest_invoke);
}

#ifdef __cplusplus
}
#endif

#endif /* CROFT_WIT_CROFT_WASM_GUEST_H */
