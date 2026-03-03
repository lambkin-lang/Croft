#include "croft/host_wasm.h"
#include "croft/host_log.h"
#include "croft/host_time.h"
#include "croft/host_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Wasm3 API */
#include "wasm3.h"
#include "m3_env.h"

struct host_wasm_ctx {
    IM3Environment env;
    IM3Runtime     runtime;
    IM3Module      module;
};

/* ------------------------------------------------------------------ */
/* Wasm Import Wrappers (Trampolines)                                 */
/* ------------------------------------------------------------------ */

m3ApiRawFunction(m3_croft_host_log) {
    m3ApiGetArgMem(const uint8_t *, ptr);
    m3ApiGetArg(uint32_t, len);
    m3ApiGetArg(int32_t, level);

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

    return m3Err_none;
}

host_wasm_ctx_t *host_wasm_create(const uint8_t *wasm_bytes, uint32_t wasm_len, uint32_t stack_size) {
    if (!wasm_bytes || wasm_len == 0) return NULL;

    host_wasm_ctx_t *ctx = calloc(1, sizeof(host_wasm_ctx_t));
    if (!ctx) return NULL;

    ctx->env = m3_NewEnvironment();
    if (!ctx->env) goto fail;

    ctx->runtime = m3_NewRuntime(ctx->env, stack_size, NULL);
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

int32_t host_wasm_call(host_wasm_ctx_t *ctx, const char *func_name, int argc, const void *argv_ptrs[]) {
    if (!ctx || !ctx->runtime || !func_name) return -1;
    
    IM3Function func;
    M3Result res = m3_FindFunction(&func, ctx->runtime, func_name);
    if (res) {
        /* Silently fail if exported func missing, or log if desired */
        return -1;
    }

    res = m3_Call(func, argc, argv_ptrs);
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
