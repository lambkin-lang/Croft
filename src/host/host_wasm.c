#include "croft/host_wasm.h"
#include "croft/host_log.h"
#include "croft/host_time.h"
#include "croft/host_fs.h"
#include "sapling/err.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Wasm3 API */
#include "wasm3.h"
#include "m3_env.h"

typedef struct {
    const SapWitWorldEndpointDescriptor *endpoint;
    const void *bindings;
} host_wasm_wit_endpoint_binding_t;

struct host_wasm_ctx {
    IM3Environment env;
    IM3Runtime     runtime;
    IM3Module      module;
    host_wasm_wit_endpoint_binding_t *wit_endpoints;
    uint32_t wit_endpoint_count;
    uint32_t wit_endpoint_cap;
};

static int host_wasm_find_registered_endpoint(const host_wasm_ctx_t *ctx, const char *qualified_name)
{
    uint32_t i;

    if (!ctx || !qualified_name) {
        return -1;
    }
    for (i = 0u; i < ctx->wit_endpoint_count; i++) {
        if (sap_wit_world_endpoint_name_equals(ctx->wit_endpoints[i].endpoint, qualified_name)) {
            return (int)i;
        }
    }
    return -1;
}

static int host_wasm_reserve_endpoint_capacity(host_wasm_ctx_t *ctx, uint32_t additional)
{
    host_wasm_wit_endpoint_binding_t *grown = NULL;
    uint32_t needed;
    uint32_t cap;

    if (!ctx) {
        return ERR_INVALID;
    }
    if (additional == 0u) {
        return ERR_OK;
    }
    if (ctx->wit_endpoint_count > UINT32_MAX - additional) {
        return ERR_RANGE;
    }
    needed = ctx->wit_endpoint_count + additional;
    if (needed <= ctx->wit_endpoint_cap) {
        return ERR_OK;
    }

    cap = ctx->wit_endpoint_cap ? ctx->wit_endpoint_cap : 8u;
    while (cap < needed) {
        if (cap > UINT32_MAX / 2u) {
            cap = needed;
            break;
        }
        cap *= 2u;
    }

    grown = (host_wasm_wit_endpoint_binding_t *)realloc(
        ctx->wit_endpoints,
        (size_t)cap * sizeof(*grown));
    if (!grown) {
        return ERR_OOM;
    }
    ctx->wit_endpoints = grown;
    ctx->wit_endpoint_cap = cap;
    return ERR_OK;
}

/* ------------------------------------------------------------------ */
/* Wasm Import Wrappers (Trampolines)                                 */
/* ------------------------------------------------------------------ */

m3ApiRawFunction(m3_croft_host_log) {
    m3ApiGetArg(int32_t, level);
    m3ApiGetArgMem(const uint8_t *, ptr);
    m3ApiGetArg(uint32_t, len);

    /* Validate bounds */
    m3ApiCheckMem(ptr, len);

    host_log(level, (const char *)ptr, len);
    m3ApiSuccess();
}

m3ApiRawFunction(m3_croft_host_time_millis) {
    m3ApiReturnType(uint64_t);
    uint64_t now = host_time_millis();
    m3ApiReturn(now);
}

m3ApiRawFunction(m3_croft_host_fs_write) {
    m3ApiReturnType(int32_t);
    m3ApiGetArgMem(uint32_t *, out_written);
    m3ApiGetArg(uint32_t, len);
    m3ApiGetArgMem(const uint8_t *, ptr);
    m3ApiGetArg(uint64_t, fd);

    m3ApiCheckMem(ptr, len);
    m3ApiCheckMem(out_written, sizeof(uint32_t));

    int32_t rc = host_fs_write(fd, ptr, len, out_written);
    m3ApiReturn(rc);
}

m3ApiRawFunction(m3_croft_wit_find_endpoint) {
    m3ApiReturnType(int32_t);
    m3ApiGetArgMem(const uint8_t *, name_ptr);
    m3ApiGetArg(uint32_t, name_len);
    host_wasm_ctx_t *ctx = (host_wasm_ctx_t *)m3_GetUserData(runtime);
    char *qualified_name = NULL;
    int index = -1;

    if (!ctx || !name_len) {
        m3ApiReturn(-(int32_t)ERR_INVALID);
    }

    m3ApiCheckMem(name_ptr, name_len);
    qualified_name = (char *)malloc((size_t)name_len + 1u);
    if (!qualified_name) {
        m3ApiReturn(-(int32_t)ERR_OOM);
    }
    memcpy(qualified_name, name_ptr, name_len);
    qualified_name[name_len] = '\0';

    index = host_wasm_find_registered_endpoint(ctx, qualified_name);
    free(qualified_name);
    if (index < 0) {
        m3ApiReturn(-(int32_t)ERR_NOT_FOUND);
    }
    m3ApiReturn((int32_t)index + 1);
}

m3ApiRawFunction(m3_croft_wit_call_endpoint) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, endpoint_handle);
    m3ApiGetArgMem(const uint8_t *, command_ptr);
    m3ApiGetArg(uint32_t, command_len);
    m3ApiGetArgMem(uint8_t *, reply_ptr);
    m3ApiGetArg(uint32_t, reply_cap);
    m3ApiGetArgMem(uint32_t *, reply_len_out);
    host_wasm_ctx_t *ctx = (host_wasm_ctx_t *)m3_GetUserData(runtime);
    uint32_t reply_len = 0u;
    int32_t rc = ERR_INVALID;
    uint32_t index = 0u;

    if (!ctx || endpoint_handle <= 0) {
        m3ApiReturn(ERR_INVALID);
    }

    m3ApiCheckMem(reply_len_out, sizeof(*reply_len_out));
    if (command_len > 0u) {
        m3ApiCheckMem(command_ptr, command_len);
    }
    if (reply_cap > 0u) {
        m3ApiCheckMem(reply_ptr, reply_cap);
    }

    index = (uint32_t)(endpoint_handle - 1);
    if (index >= ctx->wit_endpoint_count) {
        m3ApiReturn(ERR_NOT_FOUND);
    }

    rc = sap_wit_world_endpoint_invoke_bytes(ctx->wit_endpoints[index].endpoint,
                                             ctx->wit_endpoints[index].bindings,
                                             command_ptr,
                                             command_len,
                                             reply_ptr,
                                             reply_cap,
                                             &reply_len);
    *reply_len_out = reply_len;
    m3ApiReturn(rc);
}

/* ------------------------------------------------------------------ */
/* Lifecycle and Bridging                                             */
/* ------------------------------------------------------------------ */

static M3Result link_host_api(IM3Module module) {
    M3Result res = m3_LinkRawFunction(module, "env", "host_log", "v(i*i)", &m3_croft_host_log);
    if (res && res != m3Err_functionLookupFailed) return res;

    res = m3_LinkRawFunction(module, "env", "host_time_millis", "I()", &m3_croft_host_time_millis);
    if (res && res != m3Err_functionLookupFailed) return res;

    res = m3_LinkRawFunction(module, "env", "host_fs_write", "i(I*i*)", &m3_croft_host_fs_write);
    if (res && res != m3Err_functionLookupFailed) return res;

    res = m3_LinkRawFunction(module,
                             "env",
                             "croft_wit_find_endpoint",
                             "i(*i)",
                             &m3_croft_wit_find_endpoint);
    if (res && res != m3Err_functionLookupFailed) return res;

    res = m3_LinkRawFunction(module,
                             "env",
                             "croft_wit_call_endpoint",
                             "i(i*i*i*)",
                             &m3_croft_wit_call_endpoint);
    if (res && res != m3Err_functionLookupFailed) return res;

    return m3Err_none;
}

host_wasm_ctx_t *host_wasm_create(const uint8_t *wasm_bytes, uint32_t wasm_len, uint32_t stack_size) {
    if (!wasm_bytes || wasm_len == 0) return NULL;

    host_wasm_ctx_t *ctx = calloc(1, sizeof(host_wasm_ctx_t));
    if (!ctx) return NULL;

    ctx->env = m3_NewEnvironment();
    if (!ctx->env) goto fail;

    ctx->runtime = m3_NewRuntime(ctx->env, stack_size, ctx);
    if (!ctx->runtime) goto fail;

    M3Result res = m3_ParseModule(ctx->env, &ctx->module, wasm_bytes, wasm_len);
    if (res) {
        host_log(CROFT_LOG_ERROR, res, (uint32_t)strlen(res));
        goto fail;
    }

    res = m3_LoadModule(ctx->runtime, ctx->module);
    if (res) {
        host_log(CROFT_LOG_ERROR, res, (uint32_t)strlen(res));
        goto fail;
    }

    /* Link all Croft API imports */
    res = link_host_api(ctx->module);
    if (res) {
        host_log(CROFT_LOG_ERROR, res, (uint32_t)strlen(res));
        goto fail;
    }

    return ctx;

fail:
    host_wasm_destroy(ctx);
    return NULL;
}

void host_wasm_destroy(host_wasm_ctx_t *ctx) {
    if (!ctx) return;
    free(ctx->wit_endpoints);
    if (ctx->runtime) m3_FreeRuntime(ctx->runtime);
    if (ctx->env) m3_FreeEnvironment(ctx->env);
    free(ctx);
}

uint8_t *host_wasm_get_memory(host_wasm_ctx_t *ctx, uint32_t *out_size) {
    if (!ctx || !ctx->runtime) return NULL;
    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(ctx->runtime, &mem_size, 0);
    if (out_size) *out_size = mem_size;
    return mem;
}

int32_t host_wasm_call(host_wasm_ctx_t *ctx, const char *func_name, int argc, const char *argv[]) {
    if (!ctx || !ctx->runtime || !func_name) return -1;
    
    IM3Function func;
    M3Result res = m3_FindFunction(&func, ctx->runtime, func_name);
    if (res) {
        /* Silently fail if exported func missing, or log if desired */
        return -1;
    }

    res = m3_CallArgv(func, (uint32_t)argc, argv);
    if (res) {
        host_log(CROFT_LOG_ERROR, res, (uint32_t)strlen(res));
        /* Print backtrace */
        M3ErrorInfo info;
        m3_GetErrorInfo(ctx->runtime, &info);
        if (info.message) {
            host_log(CROFT_LOG_ERROR, info.message, (uint32_t)strlen(info.message));
        }
        return -1;
    }

    /* Grab return value if it's an int32 */
    if (m3_GetRetCount(func) == 1 && m3_GetRetType(func, 0) == c_m3Type_i32) {
        int32_t ret = 0;
        const void *ret_ptrs[] = { &ret };
        m3_GetResults(func, 1, ret_ptrs);
        return ret;
    }

    return 0;
}

int host_wasm_register_wit_world_endpoints(host_wasm_ctx_t *ctx,
                                           const SapWitWorldEndpointDescriptor *endpoints,
                                           uint32_t count,
                                           const void *bindings)
{
    uint32_t i;
    int rc;

    if (!ctx || (!endpoints && count > 0u) || !bindings) {
        return ERR_INVALID;
    }
    if (count == 0u) {
        return ERR_OK;
    }

    rc = host_wasm_reserve_endpoint_capacity(ctx, count);
    if (rc != ERR_OK) {
        return rc;
    }

    for (i = 0u; i < count; i++) {
        char qualified_name[256];
        size_t needed;
        int existing;

        needed = sap_wit_world_endpoint_name(&endpoints[i], qualified_name, sizeof(qualified_name));
        if (needed == 0u) {
            return ERR_INVALID;
        }
        if (needed >= sizeof(qualified_name)) {
            char *heap_name = (char *)malloc(needed + 1u);

            if (!heap_name) {
                return ERR_OOM;
            }
            sap_wit_world_endpoint_name(&endpoints[i], heap_name, needed + 1u);
            existing = host_wasm_find_registered_endpoint(ctx, heap_name);
            free(heap_name);
        } else {
            existing = host_wasm_find_registered_endpoint(ctx, qualified_name);
        }

        if (existing >= 0) {
            const host_wasm_wit_endpoint_binding_t *registered = &ctx->wit_endpoints[existing];

            if (registered->endpoint == &endpoints[i] && registered->bindings == bindings) {
                continue;
            }
            return ERR_EXISTS;
        }

        ctx->wit_endpoints[ctx->wit_endpoint_count].endpoint = &endpoints[i];
        ctx->wit_endpoints[ctx->wit_endpoint_count].bindings = bindings;
        ctx->wit_endpoint_count++;
    }

    return ERR_OK;
}

int host_wasm_runner_logic(void *userdata_ctx, void *host_api, const uint8_t *req,
                           uint32_t req_len, uint8_t *reply, uint32_t reply_cap,
                           uint32_t *out_reply_len) 
{
    /* To be implemented: copying `req` into the wasm memory and invoking `wasm_handle_event` */
    (void)userdata_ctx;
    (void)host_api;
    (void)req;
    (void)req_len;
    (void)reply;
    (void)reply_cap;
    if (out_reply_len) *out_reply_len = 0;
    return 0; /* OK */
}
