#ifndef CROFT_WIT_GUEST_RUNTIME_H
#define CROFT_WIT_GUEST_RUNTIME_H

#include "croft/wit_world_runtime.h"
#include "sapling/err.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t (*SapWitGuestTransportInvokeFn)(void *ctx,
                                                const SapWitWorldEndpointDescriptor *endpoint,
                                                const char *qualified_name,
                                                const uint8_t *command_data,
                                                uint32_t command_len,
                                                uint8_t *reply_data,
                                                uint32_t reply_cap,
                                                uint32_t *reply_len_out);

typedef struct {
    const SapWitWorldEndpointDescriptor *endpoints;
    uint32_t endpoint_count;
    const void *bindings;
} SapWitGuestEndpointSet;

typedef struct {
    const SapWitGuestEndpointSet *sets;
    uint32_t set_count;
} SapWitGuestLoopbackContext;

typedef struct {
    void *ctx;
    SapWitGuestTransportInvokeFn invoke;
    uint8_t *command_buffer;
    uint32_t command_capacity;
    uint8_t *reply_buffer;
    uint32_t reply_capacity;
} SapWitGuestTransport;

static inline void sap_wit_guest_region_init_writable(ThatchRegion *region, void *data, uint32_t cap)
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

static inline int sap_wit_guest_transport_reserve(uint8_t **buffer,
                                                  uint32_t *capacity,
                                                  uint32_t need)
{
    uint32_t new_capacity;
    uint8_t *grown;

    if (!buffer || !capacity) {
        return ERR_INVALID;
    }

    if (need == 0u) {
        need = 64u;
    }
    if (*capacity >= need) {
        return ERR_OK;
    }

    new_capacity = *capacity > 0u ? *capacity : 64u;
    while (new_capacity < need) {
        if (new_capacity > UINT32_MAX / 2u) {
            new_capacity = need;
            break;
        }
        new_capacity *= 2u;
    }
    if (new_capacity < need) {
        return ERR_OOM;
    }

    grown = (uint8_t *)realloc(*buffer, (size_t)new_capacity);
    if (!grown) {
        return ERR_OOM;
    }

    *buffer = grown;
    *capacity = new_capacity;
    return ERR_OK;
}

static inline void sap_wit_guest_transport_init(SapWitGuestTransport *transport,
                                                void *ctx,
                                                SapWitGuestTransportInvokeFn invoke)
{
    if (!transport) {
        return;
    }
    memset(transport, 0, sizeof(*transport));
    transport->ctx = ctx;
    transport->invoke = invoke;
}

static inline void sap_wit_guest_transport_dispose(SapWitGuestTransport *transport)
{
    if (!transport) {
        return;
    }
    free(transport->reply_buffer);
    free(transport->command_buffer);
    memset(transport, 0, sizeof(*transport));
}

/*
 * Decoded reply pointers borrow from transport->reply_buffer and stay valid
 * until the next call on the same transport or transport disposal.
 */
static inline int32_t sap_wit_guest_transport_call(SapWitGuestTransport *transport,
                                                   const SapWitWorldEndpointDescriptor *endpoint,
                                                   const void *command,
                                                   void *reply_out)
{
    ThatchRegion region;
    ThatchRegion view;
    ThatchCursor cursor = 0u;
    char stack_name[256];
    char *heap_name = NULL;
    const char *qualified_name = stack_name;
    size_t qualified_name_len;
    uint32_t command_len = 0u;
    uint32_t reply_len = 0u;
    uint32_t command_need;
    uint32_t reply_need;
    int32_t rc;

    if (!transport || !transport->invoke || !endpoint || !command || !reply_out
            || !endpoint->write_command || !endpoint->read_reply
            || endpoint->command_size == 0u || endpoint->reply_size == 0u) {
        return ERR_INVALID;
    }

    qualified_name_len = sap_wit_world_endpoint_name(endpoint, stack_name, sizeof(stack_name));
    if (qualified_name_len == 0u) {
        return ERR_INVALID;
    }
    if (qualified_name_len >= sizeof(stack_name)) {
        heap_name = (char *)malloc(qualified_name_len + 1u);
        if (!heap_name) {
            return ERR_OOM;
        }
        sap_wit_world_endpoint_name(endpoint, heap_name, qualified_name_len + 1u);
        qualified_name = heap_name;
    }

    command_need = endpoint->command_size > 64u ? (uint32_t)endpoint->command_size : 64u;
    for (;;) {
        rc = sap_wit_guest_transport_reserve(&transport->command_buffer,
                                             &transport->command_capacity,
                                             command_need);
        if (rc != ERR_OK) {
            goto cleanup;
        }

        sap_wit_guest_region_init_writable(&region,
                                           transport->command_buffer,
                                           transport->command_capacity);
        rc = endpoint->write_command(&region, command);
        if (rc == ERR_OOM || rc == ERR_FULL) {
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

    reply_need = endpoint->reply_size > 64u ? (uint32_t)endpoint->reply_size : 64u;
    for (;;) {
        rc = sap_wit_guest_transport_reserve(&transport->reply_buffer,
                                             &transport->reply_capacity,
                                             reply_need);
        if (rc != ERR_OK) {
            goto cleanup;
        }

        rc = transport->invoke(transport->ctx,
                               endpoint,
                               qualified_name,
                               transport->command_buffer,
                               command_len,
                               transport->reply_buffer,
                               transport->reply_capacity,
                               &reply_len);
        if (rc == ERR_FULL || rc == ERR_OOM) {
            if (reply_need >= UINT32_MAX / 2u) {
                rc = rc == ERR_FULL ? ERR_FULL : ERR_OOM;
                goto cleanup;
            }
            reply_need *= 2u;
            continue;
        }
        if (rc != ERR_OK) {
            goto cleanup;
        }
        break;
    }

    memset(reply_out, 0, endpoint->reply_size);
    rc = thatch_region_init_readonly(&view, transport->reply_buffer, reply_len);
    if (rc != ERR_OK) {
        goto cleanup;
    }

    cursor = 0u;
    rc = endpoint->read_reply(&view, &cursor, reply_out);
    if (rc != ERR_OK) {
        goto cleanup;
    }
    if (cursor != reply_len) {
        rc = ERR_CORRUPT;
        goto cleanup;
    }

cleanup:
    free(heap_name);
    return rc;
}

static inline int32_t sap_wit_guest_loopback_invoke(void *ctx,
                                                    const SapWitWorldEndpointDescriptor *endpoint,
                                                    const char *qualified_name,
                                                    const uint8_t *command_data,
                                                    uint32_t command_len,
                                                    uint8_t *reply_data,
                                                    uint32_t reply_cap,
                                                    uint32_t *reply_len_out)
{
    const SapWitGuestLoopbackContext *loopback = (const SapWitGuestLoopbackContext *)ctx;
    uint32_t i;

    (void)endpoint;
    if (!loopback || !qualified_name || !reply_len_out) {
        return ERR_INVALID;
    }

    for (i = 0u; i < loopback->set_count; i++) {
        const SapWitGuestEndpointSet *set = &loopback->sets[i];
        const SapWitWorldEndpointDescriptor *matched;

        matched = sap_wit_find_world_endpoint_descriptor_qualified(set->endpoints,
                                                                   set->endpoint_count,
                                                                   qualified_name);
        if (!matched) {
            continue;
        }
        return sap_wit_world_endpoint_invoke_bytes(matched,
                                                   set->bindings,
                                                   command_data,
                                                   command_len,
                                                   reply_data,
                                                   reply_cap,
                                                   reply_len_out);
    }

    return ERR_NOT_FOUND;
}

#ifdef __cplusplus
}
#endif

#endif /* CROFT_WIT_GUEST_RUNTIME_H */
