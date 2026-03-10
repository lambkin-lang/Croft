#include "croft/wit_world_runtime.h"

#include "sapling/err.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sap_wit_name_equals(const char *actual, const char *expected)
{
    return actual && expected && strcmp(actual, expected) == 0;
}

static const char *sap_wit_world_item_kind_name(SapWitWorldItemKind kind)
{
    switch (kind) {
    case SAP_WIT_WORLD_ITEM_INCLUDE:
        return "include";
    case SAP_WIT_WORLD_ITEM_IMPORT:
        return "import";
    case SAP_WIT_WORLD_ITEM_EXPORT:
        return "export";
    default:
        return "unknown";
    }
}

static void sap_wit_world_region_init_writable(ThatchRegion *region, void *data, uint32_t cap)
{
    if (!region) {
        return;
    }
    memset(region, 0, sizeof(*region));
    region->page_ptr = data;
    region->capacity = cap;
    region->head = 0u;
    region->sealed = 0;
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

size_t sap_wit_world_endpoint_name(const SapWitWorldEndpointDescriptor *endpoint,
                                   char *out,
                                   size_t out_size)
{
    const char *package_id;
    const char *world_name;
    const char *item_name;
    const char *kind_name;
    int needed;

    if (!endpoint) {
        return 0u;
    }

    package_id = endpoint->package_id ? endpoint->package_id : "";
    world_name = endpoint->world_name ? endpoint->world_name : "";
    item_name = endpoint->item_name ? endpoint->item_name : "";
    kind_name = sap_wit_world_item_kind_name(endpoint->kind);
    needed = snprintf(NULL, 0, "%s/%s#%s:%s", package_id, world_name, kind_name, item_name);
    if (needed < 0) {
        return 0u;
    }
    if (out && out_size > 0u) {
        (void)snprintf(out, out_size, "%s/%s#%s:%s", package_id, world_name, kind_name, item_name);
    }
    return (size_t)needed;
}

int sap_wit_world_endpoint_name_equals(const SapWitWorldEndpointDescriptor *endpoint,
                                       const char *qualified_name)
{
    char stack_name[256];
    size_t needed;

    if (!endpoint || !qualified_name) {
        return 0;
    }

    needed = sap_wit_world_endpoint_name(endpoint, stack_name, sizeof(stack_name));
    if (needed == 0u) {
        return 0;
    }
    if (needed < sizeof(stack_name)) {
        return strcmp(stack_name, qualified_name) == 0;
    }

    {
        char *heap_name = (char *)malloc(needed + 1u);
        int matches = 0;

        if (!heap_name) {
            return 0;
        }
        sap_wit_world_endpoint_name(endpoint, heap_name, needed + 1u);
        matches = strcmp(heap_name, qualified_name) == 0;
        free(heap_name);
        return matches;
    }
}

const SapWitWorldEndpointDescriptor *sap_wit_find_world_endpoint_descriptor_qualified(
    const SapWitWorldEndpointDescriptor *items,
    uint32_t count,
    const char *qualified_name)
{
    uint32_t i;

    if (!items || !qualified_name) {
        return NULL;
    }
    for (i = 0; i < count; i++) {
        if (sap_wit_world_endpoint_name_equals(&items[i], qualified_name)) {
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

int32_t sap_wit_world_endpoint_invoke_bytes(const SapWitWorldEndpointDescriptor *endpoint,
                                            const void *bindings,
                                            const uint8_t *command_data,
                                            uint32_t command_len,
                                            uint8_t *reply_data,
                                            uint32_t reply_cap,
                                            uint32_t *reply_len_out)
{
    ThatchRegion command_region;
    ThatchRegion reply_region;
    ThatchCursor cursor = 0u;
    void *command = NULL;
    void *reply = NULL;
    int32_t rc = ERR_OK;

    if (!endpoint || !bindings || !reply_len_out || !endpoint->invoke || !endpoint->read_command
            || !endpoint->write_reply || !endpoint->dispose_reply || endpoint->command_size == 0u
            || endpoint->reply_size == 0u) {
        return ERR_INVALID;
    }
    if (!command_data && command_len > 0u) {
        return ERR_INVALID;
    }
    if (!reply_data && reply_cap > 0u) {
        return ERR_INVALID;
    }

    *reply_len_out = 0u;
    command = calloc(1u, endpoint->command_size);
    reply = calloc(1u, endpoint->reply_size);
    if (!command || !reply) {
        rc = ERR_OOM;
        goto cleanup;
    }

    rc = thatch_region_init_readonly(&command_region, command_data, command_len);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    rc = endpoint->read_command(&command_region, &cursor, command);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    if (cursor != command_len) {
        rc = ERR_CORRUPT;
        goto cleanup;
    }

    rc = endpoint->invoke(bindings, command, reply);
    if (rc != ERR_OK) {
        goto cleanup;
    }

    sap_wit_world_region_init_writable(&reply_region, reply_data, reply_cap);
    rc = endpoint->write_reply(&reply_region, reply);
    if (rc == ERR_OOM) {
        rc = ERR_FULL;
    }
    if (rc != ERR_OK) {
        goto cleanup;
    }
    *reply_len_out = thatch_region_used(&reply_region);

cleanup:
    if (reply && endpoint->dispose_reply) {
        endpoint->dispose_reply(reply);
    }
    free(reply);
    free(command);
    return rc;
}
