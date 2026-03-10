#ifndef CROFT_WIT_WORLD_RUNTIME_H
#define CROFT_WIT_WORLD_RUNTIME_H

#include "croft/wit_wire.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

const SapWitInterfaceDescriptor *sap_wit_find_interface_descriptor(
    const SapWitInterfaceDescriptor *items,
    uint32_t count,
    const char *interface_name);

const SapWitWorldDescriptor *sap_wit_find_world_descriptor(
    const SapWitWorldDescriptor *items,
    uint32_t count,
    const char *world_name);

const SapWitWorldBindingDescriptor *sap_wit_find_world_binding_descriptor(
    const SapWitWorldBindingDescriptor *items,
    uint32_t count,
    const char *world_name,
    const char *item_name,
    SapWitWorldItemKind kind);

const SapWitWorldEndpointDescriptor *sap_wit_find_world_endpoint_descriptor(
    const SapWitWorldEndpointDescriptor *items,
    uint32_t count,
    const char *item_name);

const SapWitWorldEndpointDescriptor *sap_wit_find_world_endpoint_descriptor_qualified(
    const SapWitWorldEndpointDescriptor *items,
    uint32_t count,
    const char *qualified_name);

size_t sap_wit_world_endpoint_name(const SapWitWorldEndpointDescriptor *endpoint,
                                   char *out,
                                   size_t out_size);

int sap_wit_world_endpoint_name_equals(const SapWitWorldEndpointDescriptor *endpoint,
                                       const char *qualified_name);

int sap_wit_world_endpoint_bind(void *bindings,
                                const SapWitWorldEndpointDescriptor *endpoint,
                                void *ctx,
                                const void *ops);

void *sap_wit_world_endpoint_ctx(void *bindings,
                                 const SapWitWorldEndpointDescriptor *endpoint);

const void *sap_wit_world_endpoint_ops(const void *bindings,
                                       const SapWitWorldEndpointDescriptor *endpoint);

int32_t sap_wit_world_endpoint_invoke(const SapWitWorldEndpointDescriptor *endpoint,
                                      const void *bindings,
                                      const void *command,
                                      void *reply_out);

int32_t sap_wit_world_endpoint_invoke_bytes(const SapWitWorldEndpointDescriptor *endpoint,
                                            const void *bindings,
                                            const uint8_t *command_data,
                                            uint32_t command_len,
                                            uint8_t *reply_data,
                                            uint32_t reply_cap,
                                            uint32_t *reply_len_out);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_WIT_WORLD_RUNTIME_H */
