#include "croft/wit_world_runtime.h"

#include "sapling/err.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int sap_wit_name_equals(const char *actual, const char *expected)
{
    return actual && expected && strcmp(actual, expected) == 0;
}

const SapWitInterfaceDescriptor *sap_wit_find_interface_descriptor(
    const SapWitInterfaceDescriptor *items,
    uint32_t count,
    const char *interface_name)
{
    uint32_t i;

    if (!items || !interface_name) {
        return NULL;
    }
    for (i = 0; i < count; i++) {
        if (sap_wit_name_equals(items[i].interface_name, interface_name)) {
            return &items[i];
        }
    }
    return NULL;
}

const SapWitWorldDescriptor *sap_wit_find_world_descriptor(const SapWitWorldDescriptor *items,
                                                           uint32_t count,
                                                           const char *world_name)
{
    uint32_t i;

    if (!items || !world_name) {
        return NULL;
    }
    for (i = 0; i < count; i++) {
        if (sap_wit_name_equals(items[i].world_name, world_name)) {
            return &items[i];
        }
    }
    return NULL;
}

const SapWitWorldBindingDescriptor *sap_wit_find_world_binding_descriptor(
    const SapWitWorldBindingDescriptor *items,
    uint32_t count,
    const char *world_name,
    const char *item_name,
    SapWitWorldItemKind kind)
{
    uint32_t i;

    if (!items || !world_name || !item_name) {
        return NULL;
    }
    for (i = 0; i < count; i++) {
        if (items[i].kind == kind
                && sap_wit_name_equals(items[i].world_name, world_name)
                && sap_wit_name_equals(items[i].item_name, item_name)) {
            return &items[i];
        }
    }
    return NULL;
}

const SapWitWorldEndpointDescriptor *sap_wit_find_world_endpoint_descriptor(
    const SapWitWorldEndpointDescriptor *items,
    uint32_t count,
    const char *item_name)
{
    uint32_t i;

    if (!items || !item_name) {
        return NULL;
    }
    for (i = 0; i < count; i++) {
        if (sap_wit_name_equals(items[i].item_name, item_name)) {
            return &items[i];
        }
    }
    return NULL;
}

int sap_wit_world_endpoint_bind(void *bindings,
                                const SapWitWorldEndpointDescriptor *endpoint,
                                void *ctx,
                                const void *ops)
{
    uint8_t *base;
    void **ctx_slot;
    const void **ops_slot;

    if (!bindings || !endpoint) {
        return ERR_INVALID;
    }

    base = (uint8_t *)bindings;
    ctx_slot = (void **)(base + endpoint->ctx_offset);
    ops_slot = (const void **)(base + endpoint->ops_offset);
    *ctx_slot = ctx;
    *ops_slot = ops;
    return ERR_OK;
}

void *sap_wit_world_endpoint_ctx(void *bindings, const SapWitWorldEndpointDescriptor *endpoint)
{
    uint8_t *base;

    if (!bindings || !endpoint) {
        return NULL;
    }

    base = (uint8_t *)bindings;
    return *(void **)(base + endpoint->ctx_offset);
}

const void *sap_wit_world_endpoint_ops(const void *bindings,
                                       const SapWitWorldEndpointDescriptor *endpoint)
{
    const uint8_t *base;

    if (!bindings || !endpoint) {
        return NULL;
    }

    base = (const uint8_t *)bindings;
    return *(const void * const *)(base + endpoint->ops_offset);
}

int32_t sap_wit_world_endpoint_invoke(const SapWitWorldEndpointDescriptor *endpoint,
                                      const void *bindings,
                                      const void *command,
                                      void *reply_out)
{
    if (!endpoint || !endpoint->invoke) {
        return ERR_INVALID;
    }
    return endpoint->invoke(bindings, command, reply_out);
}
