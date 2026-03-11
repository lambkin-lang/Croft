#ifndef CROFT_WIT_GUEST_EXPORTS_RUNTIME_H
#define CROFT_WIT_GUEST_EXPORTS_RUNTIME_H

#include "croft/wit_world_runtime.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t sap_wit_guest_exports_register(const SapWitWorldEndpointDescriptor *endpoints,
                                       uint32_t count,
                                       const void *bindings);
void sap_wit_guest_exports_reset(void);
uint32_t sap_wit_guest_exports_count(void);

int32_t sap_wit_guest_find_export_endpoint(const uint8_t *name_ptr, uint32_t name_len);
int32_t sap_wit_guest_call_export_endpoint(int32_t endpoint_handle,
                                           const uint8_t *command_ptr,
                                           uint32_t command_len,
                                           uint8_t *reply_ptr,
                                           uint32_t reply_cap,
                                           uint32_t *reply_len_out);
int32_t sap_wit_guest_alloc(uint32_t size);
int32_t sap_wit_guest_free(uint32_t ptr);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_WIT_GUEST_EXPORTS_RUNTIME_H */
