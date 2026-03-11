#include "croft/host_wasm.h"
#include "croft/host_log.h"
#include "croft/host_time.h"
#include "croft/host_fs.h"
#include "sapling/err.h"

#include <inttypes.h>
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

typedef struct {
    const SapWitWorldEndpointDescriptor *endpoint;
    int32_t handle;
} host_wasm_wit_guest_export_handle_t;

struct host_wasm_ctx {
    IM3Environment env;
    IM3Runtime     runtime;
    IM3Module      module;
    host_wasm_wit_endpoint_binding_t *wit_endpoints;
    uint32_t wit_endpoint_count;
    uint32_t wit_endpoint_cap;
    host_wasm_wit_guest_export_handle_t *wit_guest_export_handles;
    uint32_t wit_guest_export_handle_count;
    uint32_t wit_guest_export_handle_cap;
    uint8_t *wit_guest_reply_buffer;
    uint32_t wit_guest_reply_capacity;
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

static int host_wasm_reserve_guest_export_handle_capacity(host_wasm_ctx_t *ctx, uint32_t additional)
{
    host_wasm_wit_guest_export_handle_t *grown = NULL;
    uint32_t needed;
    uint32_t cap;

    if (!ctx) {
        return ERR_INVALID;
    }
    if (additional == 0u) {
        return ERR_OK;
    }
    if (ctx->wit_guest_export_handle_count > UINT32_MAX - additional) {
        return ERR_RANGE;
    }
    needed = ctx->wit_guest_export_handle_count + additional;
    if (needed <= ctx->wit_guest_export_handle_cap) {
        return ERR_OK;
    }

    cap = ctx->wit_guest_export_handle_cap ? ctx->wit_guest_export_handle_cap : 8u;
    while (cap < needed) {
        if (cap > UINT32_MAX / 2u) {
            cap = needed;
            break;
        }
        cap *= 2u;
    }

    grown = (host_wasm_wit_guest_export_handle_t *)realloc(
        ctx->wit_guest_export_handles,
        (size_t)cap * sizeof(*grown));
    if (!grown) {
        return ERR_OOM;
    }
    ctx->wit_guest_export_handles = grown;
    ctx->wit_guest_export_handle_cap = cap;
    return ERR_OK;
}

static int host_wasm_reserve_guest_reply_buffer(host_wasm_ctx_t *ctx, uint32_t need)
{
    uint8_t *grown;

    if (!ctx) {
        return ERR_INVALID;
    }
    if (need == 0u) {
        need = 1u;
    }
    if (ctx->wit_guest_reply_capacity >= need) {
        return ERR_OK;
    }

    grown = (uint8_t *)realloc(ctx->wit_guest_reply_buffer, need);
    if (!grown) {
        return ERR_OOM;
    }
    ctx->wit_guest_reply_buffer = grown;
    ctx->wit_guest_reply_capacity = need;
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

    {
        IM3Function ctors = NULL;

        res = m3_FindFunction(&ctors, ctx->runtime, "__wasm_call_ctors");
        if (!res) {
            res = m3_CallV(ctors);
            if (res) {
                host_log(CROFT_LOG_ERROR, res, (uint32_t)strlen(res));
                goto fail;
            }
        }
    }

    return ctx;

fail:
    host_wasm_destroy(ctx);
    return NULL;
}

void host_wasm_destroy(host_wasm_ctx_t *ctx) {
    if (!ctx) return;
    free(ctx->wit_endpoints);
    free(ctx->wit_guest_export_handles);
    free(ctx->wit_guest_reply_buffer);
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

static int host_wasm_invoke_argv(host_wasm_ctx_t *ctx,
                                 const char *func_name,
                                 int argc,
                                 const char *argv[],
                                 int32_t *out_result)
{
    IM3Function func;
    M3Result res;

    if (!ctx || !ctx->runtime || !func_name) {
        return ERR_INVALID;
    }

    res = m3_FindFunction(&func, ctx->runtime, func_name);
    if (res) {
        return ERR_NOT_FOUND;
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
        return ERR_INVALID;
    }

    if (out_result) {
        *out_result = 0;
    }
    if (m3_GetRetCount(func) == 1 && m3_GetRetType(func, 0) == c_m3Type_i32) {
        int32_t ret = 0;
        const void *ret_ptrs[] = { &ret };

        m3_GetResults(func, 1, ret_ptrs);
        if (out_result) {
            *out_result = ret;
        }
    }

    return ERR_OK;
}

static int host_wasm_invoke_i32(host_wasm_ctx_t *ctx,
                                const char *func_name,
                                uint32_t argc,
                                const int32_t *args,
                                int32_t *out_result)
{
    const char *argv[8];
    char storage[8][16];
    uint32_t i;

    if (argc > (uint32_t)(sizeof(argv) / sizeof(argv[0]))) {
        return ERR_RANGE;
    }
    for (i = 0u; i < argc; i++) {
        snprintf(storage[i], sizeof(storage[i]), "%" PRId32, args ? args[i] : 0);
        argv[i] = storage[i];
    }
    return host_wasm_invoke_argv(ctx,
                                 func_name,
                                 (int)argc,
                                 argc > 0u ? argv : NULL,
                                 out_result);
}

int32_t host_wasm_call(host_wasm_ctx_t *ctx, const char *func_name, int argc, const char *argv[]) {
    int32_t result = 0;
    int rc = host_wasm_invoke_argv(ctx, func_name, argc, argv, &result);

    return rc == ERR_OK ? result : -1;
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

static int host_wasm_guest_memory_range(host_wasm_ctx_t *ctx,
                                        uint32_t ptr,
                                        uint32_t len,
                                        uint8_t **out_mem)
{
    uint32_t mem_size = 0u;
    uint8_t *mem = host_wasm_get_memory(ctx, &mem_size);

    if (!ctx || !out_mem) {
        return ERR_INVALID;
    }
    if ((len > 0u && ptr == 0u) || !mem) {
        return ERR_INVALID;
    }
    if (len > 0u && (ptr > mem_size || len > mem_size - ptr)) {
        return ERR_RANGE;
    }

    *out_mem = mem;
    return ERR_OK;
}

static int host_wasm_guest_alloc_buffer(host_wasm_ctx_t *ctx, uint32_t size, uint32_t *ptr_out)
{
    int32_t arg = (int32_t)size;
    int32_t result = 0;
    int rc;

    if (!ctx || !ptr_out) {
        return ERR_INVALID;
    }
    if (size == 0u) {
        *ptr_out = 0u;
        return ERR_OK;
    }

    rc = host_wasm_invoke_i32(ctx, "croft_wit_guest_alloc", 1u, &arg, &result);
    if (rc != ERR_OK) {
        return rc;
    }
    if (result <= 0) {
        return ERR_OOM;
    }

    *ptr_out = (uint32_t)result;
    return ERR_OK;
}

static void host_wasm_guest_free_buffer(host_wasm_ctx_t *ctx, uint32_t ptr)
{
    int32_t arg = (int32_t)ptr;
    int32_t ignored = 0;

    if (!ctx || ptr == 0u) {
        return;
    }
    (void)host_wasm_invoke_i32(ctx, "croft_wit_guest_free", 1u, &arg, &ignored);
}

static int host_wasm_guest_copy_in(host_wasm_ctx_t *ctx,
                                   const uint8_t *data,
                                   uint32_t len,
                                   uint32_t *ptr_out)
{
    uint8_t *mem = NULL;
    int rc;

    if (!ctx || !ptr_out || (len > 0u && !data)) {
        return ERR_INVALID;
    }
    if (len == 0u) {
        *ptr_out = 0u;
        return ERR_OK;
    }

    rc = host_wasm_guest_alloc_buffer(ctx, len, ptr_out);
    if (rc != ERR_OK) {
        return rc;
    }

    rc = host_wasm_guest_memory_range(ctx, *ptr_out, len, &mem);
    if (rc != ERR_OK) {
        host_wasm_guest_free_buffer(ctx, *ptr_out);
        *ptr_out = 0u;
        return rc;
    }

    memcpy(mem + *ptr_out, data, len);
    return ERR_OK;
}

static int32_t host_wasm_guest_find_export_handle(host_wasm_ctx_t *ctx,
                                                  const SapWitWorldEndpointDescriptor *endpoint)
{
    char qualified_name[256];
    size_t needed;
    uint8_t *name_bytes = NULL;
    uint32_t name_ptr = 0u;
    int32_t args[2];
    int32_t handle = 0;
    uint32_t i;
    int rc;

    if (!ctx || !endpoint) {
        return ERR_INVALID;
    }

    for (i = 0u; i < ctx->wit_guest_export_handle_count; i++) {
        if (ctx->wit_guest_export_handles[i].endpoint == endpoint) {
            return ctx->wit_guest_export_handles[i].handle;
        }
    }

    needed = sap_wit_world_endpoint_name(endpoint, qualified_name, sizeof(qualified_name));
    if (needed == 0u) {
        return ERR_INVALID;
    }

    if (needed < sizeof(qualified_name)) {
        name_bytes = (uint8_t *)qualified_name;
    } else {
        name_bytes = (uint8_t *)malloc(needed + 1u);
        if (!name_bytes) {
            return ERR_OOM;
        }
        sap_wit_world_endpoint_name(endpoint, (char *)name_bytes, needed + 1u);
    }

    rc = host_wasm_guest_copy_in(ctx, name_bytes, (uint32_t)needed, &name_ptr);
    if (needed >= sizeof(qualified_name)) {
        free(name_bytes);
    }
    if (rc != ERR_OK) {
        return rc;
    }

    args[0] = (int32_t)name_ptr;
    args[1] = (int32_t)needed;
    rc = host_wasm_invoke_i32(ctx,
                              "croft_wit_guest_find_export_endpoint",
                              2u,
                              args,
                              &handle);
    host_wasm_guest_free_buffer(ctx, name_ptr);
    if (rc != ERR_OK) {
        return rc;
    }
    if (handle <= 0) {
        return handle < 0 ? -handle : ERR_NOT_FOUND;
    }

    rc = host_wasm_reserve_guest_export_handle_capacity(ctx, 1u);
    if (rc != ERR_OK) {
        return rc;
    }
    ctx->wit_guest_export_handles[ctx->wit_guest_export_handle_count].endpoint = endpoint;
    ctx->wit_guest_export_handles[ctx->wit_guest_export_handle_count].handle = handle;
    ctx->wit_guest_export_handle_count++;
    return handle;
}

int32_t host_wasm_call_wit_export_endpoint(host_wasm_ctx_t *ctx,
                                           const SapWitWorldEndpointDescriptor *endpoint,
                                           const void *command,
                                           void *reply_out)
{
    ThatchRegion region;
    ThatchRegion view;
    ThatchCursor cursor = 0u;
    uint8_t *command_buf = NULL;
    uint32_t command_cap = 0u;
    uint32_t command_len = 0u;
    uint32_t command_need;
    uint32_t reply_need;
    uint32_t guest_command_ptr = 0u;
    uint32_t guest_reply_ptr = 0u;
    uint32_t guest_reply_len_ptr = 0u;
    uint32_t guest_reply_len = 0u;
    uint8_t *guest_mem = NULL;
    uint8_t *reply_data = NULL;
    int32_t guest_handle;
    int32_t guest_rc = ERR_INVALID;
    int32_t args[6];
    int rc;

    if (!ctx || !endpoint || !command || !reply_out
            || !endpoint->write_command || !endpoint->read_reply
            || endpoint->command_size == 0u || endpoint->reply_size == 0u) {
        return ERR_INVALID;
    }

    guest_handle = host_wasm_guest_find_export_handle(ctx, endpoint);
    if (guest_handle <= 0) {
        return guest_handle == 0 ? ERR_NOT_FOUND : guest_handle;
    }

    command_need = endpoint->command_size > 64u ? (uint32_t)endpoint->command_size : 64u;
    for (;;) {
        uint8_t *grown;

        if (command_cap < command_need) {
            grown = (uint8_t *)realloc(command_buf, command_need);
            if (!grown) {
                rc = ERR_OOM;
                goto cleanup;
            }
            command_buf = grown;
            command_cap = command_need;
        }

        memset(&region, 0, sizeof(region));
        region.page_ptr = command_buf;
        region.capacity = command_cap;
        rc = endpoint->write_command(&region, command);
        if (rc == ERR_FULL || rc == ERR_OOM) {
            if (command_need >= UINT32_MAX / 2u) {
                rc = rc == ERR_FULL ? ERR_FULL : ERR_OOM;
                goto cleanup;
            }
            command_need *= 2u;
            continue;
        }
        if (rc != ERR_OK) {
            goto cleanup;
        }
        command_len = thatch_region_used(&region);
        break;
    }

    rc = host_wasm_guest_copy_in(ctx, command_buf, command_len, &guest_command_ptr);
    if (rc != ERR_OK) {
        goto cleanup;
    }

    reply_need = endpoint->reply_size > 64u ? (uint32_t)endpoint->reply_size : 64u;
    for (;;) {
        rc = host_wasm_guest_alloc_buffer(ctx, reply_need, &guest_reply_ptr);
        if (rc != ERR_OK) {
            goto cleanup;
        }
        rc = host_wasm_guest_alloc_buffer(ctx, sizeof(uint32_t), &guest_reply_len_ptr);
        if (rc != ERR_OK) {
            goto cleanup;
        }

        rc = host_wasm_guest_memory_range(ctx, guest_reply_len_ptr, sizeof(uint32_t), &guest_mem);
        if (rc != ERR_OK) {
            goto cleanup;
        }
        memset(guest_mem + guest_reply_len_ptr, 0, sizeof(uint32_t));

        args[0] = guest_handle;
        args[1] = (int32_t)guest_command_ptr;
        args[2] = (int32_t)command_len;
        args[3] = (int32_t)guest_reply_ptr;
        args[4] = (int32_t)reply_need;
        args[5] = (int32_t)guest_reply_len_ptr;
        rc = host_wasm_invoke_i32(ctx,
                                  "croft_wit_guest_call_export_endpoint",
                                  6u,
                                  args,
                                  &guest_rc);
        if (rc != ERR_OK) {
            goto cleanup;
        }
        if (guest_rc == ERR_FULL || guest_rc == ERR_OOM) {
            host_wasm_guest_free_buffer(ctx, guest_reply_len_ptr);
            host_wasm_guest_free_buffer(ctx, guest_reply_ptr);
            guest_reply_len_ptr = 0u;
            guest_reply_ptr = 0u;
            if (reply_need >= UINT32_MAX / 2u) {
                rc = guest_rc;
                goto cleanup;
            }
            reply_need *= 2u;
            continue;
        }
        if (guest_rc != ERR_OK) {
            rc = guest_rc;
            goto cleanup;
        }
        break;
    }

    rc = host_wasm_guest_memory_range(ctx, guest_reply_len_ptr, sizeof(uint32_t), &guest_mem);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    memcpy(&guest_reply_len, guest_mem + guest_reply_len_ptr, sizeof(uint32_t));
    if (guest_reply_len > reply_need) {
        rc = ERR_CORRUPT;
        goto cleanup;
    }

    rc = host_wasm_guest_memory_range(ctx, guest_reply_ptr, guest_reply_len, &guest_mem);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    rc = host_wasm_reserve_guest_reply_buffer(ctx, guest_reply_len);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    reply_data = ctx->wit_guest_reply_buffer;
    if (guest_reply_len > 0u) {
        memcpy(reply_data, guest_mem + guest_reply_ptr, guest_reply_len);
    }

    memset(reply_out, 0, endpoint->reply_size);
    rc = thatch_region_init_readonly(&view, reply_data, guest_reply_len);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    cursor = 0u;
    rc = endpoint->read_reply(&view, &cursor, reply_out);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    if (cursor != guest_reply_len) {
        rc = ERR_CORRUPT;
        goto cleanup;
    }

cleanup:
    host_wasm_guest_free_buffer(ctx, guest_reply_len_ptr);
    host_wasm_guest_free_buffer(ctx, guest_reply_ptr);
    host_wasm_guest_free_buffer(ctx, guest_command_ptr);
    free(command_buf);
    return rc;
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
