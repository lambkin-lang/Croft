#include "croft/wit_guest_exports_runtime.h"

#include "croft/wit_runtime_support.h"
#include "sapling/err.h"

#include <stdint.h>

typedef struct {
    const SapWitWorldEndpointDescriptor *endpoint;
    const void *bindings;
} SapWitGuestExportBinding;

static SapWitGuestExportBinding *g_sap_wit_guest_export_bindings = NULL;
static uint32_t g_sap_wit_guest_export_binding_count = 0u;
static uint32_t g_sap_wit_guest_export_binding_capacity = 0u;

#if defined(__wasm32__)
#define SAP_WIT_GUEST_EXPORT(name) __attribute__((export_name(name)))
#else
#define SAP_WIT_GUEST_EXPORT(name)
#endif

static int sap_wit_guest_exports_reserve(uint32_t need)
{
    SapWitGuestExportBinding *grown;
    uint32_t capacity;

    if (need <= g_sap_wit_guest_export_binding_capacity) {
        return ERR_OK;
    }

    capacity = g_sap_wit_guest_export_binding_capacity > 0u
             ? g_sap_wit_guest_export_binding_capacity
             : 8u;
    while (capacity < need) {
        if (capacity > UINT32_MAX / 2u) {
            capacity = need;
            break;
        }
        capacity *= 2u;
    }

    grown = (SapWitGuestExportBinding *)sap_wit_rt_realloc(
        g_sap_wit_guest_export_bindings,
        (size_t)capacity * sizeof(*grown));
    if (!grown) {
        return ERR_OOM;
    }

    g_sap_wit_guest_export_bindings = grown;
    g_sap_wit_guest_export_binding_capacity = capacity;
    return ERR_OK;
}

static int32_t sap_wit_guest_exports_find_index(const char *qualified_name)
{
    uint32_t i;

    if (!qualified_name || qualified_name[0] == '\0') {
        return -ERR_INVALID;
    }

    for (i = 0u; i < g_sap_wit_guest_export_binding_count; i++) {
        if (sap_wit_world_endpoint_name_equals(g_sap_wit_guest_export_bindings[i].endpoint,
                                               qualified_name)) {
            return (int32_t)i;
        }
    }
    return -ERR_NOT_FOUND;
}

int32_t sap_wit_guest_exports_register(const SapWitWorldEndpointDescriptor *endpoints,
                                       uint32_t count,
                                       const void *bindings)
{
    uint32_t i;
    int rc;

    if ((!endpoints && count > 0u) || !bindings) {
        return ERR_INVALID;
    }
    if (count == 0u) {
        return ERR_OK;
    }

    rc = sap_wit_guest_exports_reserve(g_sap_wit_guest_export_binding_count + count);
    if (rc != ERR_OK) {
        return rc;
    }

    for (i = 0u; i < count; i++) {
        int32_t existing;
        char qualified_name[256];
        size_t needed;

        needed = sap_wit_world_endpoint_name(&endpoints[i],
                                             qualified_name,
                                             sizeof(qualified_name));
        if (needed == 0u) {
            return ERR_INVALID;
        }

        if (needed < sizeof(qualified_name)) {
            existing = sap_wit_guest_exports_find_index(qualified_name);
        } else {
            char *heap_name = (char *)sap_wit_rt_malloc(needed + 1u);

            if (!heap_name) {
                return ERR_OOM;
            }
            sap_wit_world_endpoint_name(&endpoints[i], heap_name, needed + 1u);
            existing = sap_wit_guest_exports_find_index(heap_name);
            sap_wit_rt_free(heap_name);
        }

        if (existing >= 0) {
            if (g_sap_wit_guest_export_bindings[existing].endpoint == &endpoints[i]
                    && g_sap_wit_guest_export_bindings[existing].bindings == bindings) {
                continue;
            }
            return ERR_EXISTS;
        }

        g_sap_wit_guest_export_bindings[g_sap_wit_guest_export_binding_count].endpoint = &endpoints[i];
        g_sap_wit_guest_export_bindings[g_sap_wit_guest_export_binding_count].bindings = bindings;
        g_sap_wit_guest_export_binding_count++;
    }

    return ERR_OK;
}

void sap_wit_guest_exports_reset(void)
{
    sap_wit_rt_free(g_sap_wit_guest_export_bindings);
    g_sap_wit_guest_export_bindings = NULL;
    g_sap_wit_guest_export_binding_count = 0u;
    g_sap_wit_guest_export_binding_capacity = 0u;
}

uint32_t sap_wit_guest_exports_count(void)
{
    return g_sap_wit_guest_export_binding_count;
}

SAP_WIT_GUEST_EXPORT("croft_wit_guest_find_export_endpoint")
int32_t sap_wit_guest_find_export_endpoint(const uint8_t *name_ptr, uint32_t name_len)
{
    char *qualified_name;
    int32_t handle;

    if (!name_ptr || name_len == 0u) {
        return -ERR_INVALID;
    }

    qualified_name = (char *)sap_wit_rt_malloc((size_t)name_len + 1u);
    if (!qualified_name) {
        return -ERR_OOM;
    }
    sap_wit_rt_memcpy(qualified_name, name_ptr, name_len);
    qualified_name[name_len] = '\0';

    handle = sap_wit_guest_exports_find_index(qualified_name);
    sap_wit_rt_free(qualified_name);
    if (handle < 0) {
        return handle;
    }
    return handle + 1;
}

SAP_WIT_GUEST_EXPORT("croft_wit_guest_call_export_endpoint")
int32_t sap_wit_guest_call_export_endpoint(int32_t endpoint_handle,
                                           const uint8_t *command_ptr,
                                           uint32_t command_len,
                                           uint8_t *reply_ptr,
                                           uint32_t reply_cap,
                                           uint32_t *reply_len_out)
{
    uint32_t index;

    if (!reply_len_out || endpoint_handle <= 0) {
        return ERR_INVALID;
    }
    if (command_len > 0u && !command_ptr) {
        return ERR_INVALID;
    }
    if (reply_cap > 0u && !reply_ptr) {
        return ERR_INVALID;
    }

    index = (uint32_t)(endpoint_handle - 1);
    if (index >= g_sap_wit_guest_export_binding_count) {
        return ERR_NOT_FOUND;
    }

    *reply_len_out = 0u;
    return sap_wit_world_endpoint_invoke_bytes(g_sap_wit_guest_export_bindings[index].endpoint,
                                               g_sap_wit_guest_export_bindings[index].bindings,
                                               command_ptr,
                                               command_len,
                                               reply_ptr,
                                               reply_cap,
                                               reply_len_out);
}

SAP_WIT_GUEST_EXPORT("croft_wit_guest_alloc")
int32_t sap_wit_guest_alloc(uint32_t size)
{
    void *ptr = sap_wit_rt_malloc(size > 0u ? (size_t)size : 1u);

    return ptr ? (int32_t)(uintptr_t)ptr : 0;
}

SAP_WIT_GUEST_EXPORT("croft_wit_guest_free")
int32_t sap_wit_guest_free(uint32_t ptr)
{
    if (ptr != 0u) {
        sap_wit_rt_free((void *)(uintptr_t)ptr);
    }
    return ERR_OK;
}
