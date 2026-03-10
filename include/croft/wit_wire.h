#ifndef CROFT_WIT_WIRE_H
#define CROFT_WIT_WIRE_H

#include <stddef.h>
#include <stdint.h>

#include "sapling/thatch.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Thatch WIT tags (0x10+, coexists with JSON 0x01-0x09). */
#define SAP_WIT_TAG_RECORD       0x10
#define SAP_WIT_TAG_VARIANT      0x11
#define SAP_WIT_TAG_ENUM         0x12
#define SAP_WIT_TAG_FLAGS        0x13
#define SAP_WIT_TAG_OPTION_NONE  0x14
#define SAP_WIT_TAG_OPTION_SOME  0x15
#define SAP_WIT_TAG_TUPLE        0x16
#define SAP_WIT_TAG_LIST         0x17
#define SAP_WIT_TAG_RESULT_OK    0x18
#define SAP_WIT_TAG_RESULT_ERR   0x19

#define SAP_WIT_TAG_S8           0x20
#define SAP_WIT_TAG_U8           0x21
#define SAP_WIT_TAG_S16          0x22
#define SAP_WIT_TAG_U16          0x23
#define SAP_WIT_TAG_S32          0x24
#define SAP_WIT_TAG_U32          0x25
#define SAP_WIT_TAG_S64          0x26
#define SAP_WIT_TAG_U64          0x27
#define SAP_WIT_TAG_F32          0x28
#define SAP_WIT_TAG_F64          0x29
#define SAP_WIT_TAG_BOOL_FALSE   0x2A
#define SAP_WIT_TAG_BOOL_TRUE    0x2B
#define SAP_WIT_TAG_BYTES        0x2C
#define SAP_WIT_TAG_STRING       0x2D
#define SAP_WIT_TAG_RESOURCE     0x2E

/* Generated WIT graph metadata shared by interface/world bindings. */
typedef enum {
    SAP_WIT_WORLD_ITEM_INCLUDE = 0,
    SAP_WIT_WORLD_ITEM_IMPORT = 1,
    SAP_WIT_WORLD_ITEM_EXPORT = 2,
} SapWitWorldItemKind;

typedef enum {
    SAP_WIT_WORLD_TARGET_UNKNOWN = 0,
    SAP_WIT_WORLD_TARGET_INTERFACE = 1,
    SAP_WIT_WORLD_TARGET_WORLD = 2,
    SAP_WIT_WORLD_TARGET_FUNCTION = 3,
} SapWitWorldTargetKind;

typedef struct {
    const char *package_id;
    const char *interface_name;
    const char *attributes;
    const char *origin_world_name;
    const char *origin_item_name;
    uint8_t imported;
} SapWitInterfaceDescriptor;

typedef struct {
    const char *package_id;
    const char *world_name;
    const char *attributes;
    uint8_t imported;
    uint32_t binding_offset;
    uint32_t binding_count;
} SapWitWorldDescriptor;

typedef struct {
    SapWitWorldItemKind   kind;
    SapWitWorldTargetKind target_kind;
    const char           *package_id;
    const char           *world_name;
    const char           *item_name;
    const char           *attributes;
    uint8_t               imported;
    const char           *target_package_id;
    const char           *target_name;
    const char           *lowered_package_id;
    const char           *lowered_name;
} SapWitWorldBindingDescriptor;

typedef int32_t (*SapWitWorldEndpointInvokeFn)(const void *bindings,
                                               const void *command,
                                               void *reply_out);

typedef int (*SapWitWorldEndpointReadFn)(const ThatchRegion *region,
                                         ThatchCursor *cursor,
                                         void *out);

typedef int (*SapWitWorldEndpointWriteFn)(ThatchRegion *region, const void *value);

typedef void (*SapWitWorldEndpointDisposeFn)(void *value);

typedef struct {
    SapWitWorldItemKind        kind;
    SapWitWorldTargetKind      target_kind;
    const char                *package_id;
    const char                *world_name;
    const char                *item_name;
    const char                *target_package_id;
    const char                *target_name;
    const char                *lowered_package_id;
    const char                *lowered_name;
    const char                *bindings_c_type;
    const char                *ops_c_type;
    const char                *command_c_type;
    const char                *reply_c_type;
    size_t                     command_size;
    size_t                     reply_size;
    size_t                     ctx_offset;
    size_t                     ops_offset;
    SapWitWorldEndpointReadFn  read_command;
    SapWitWorldEndpointWriteFn write_reply;
    SapWitWorldEndpointDisposeFn dispose_reply;
    SapWitWorldEndpointInvokeFn invoke;
} SapWitWorldEndpointDescriptor;

int sap_wit_skip_value(const ThatchRegion* region, ThatchCursor* cursor);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_WIT_WIRE_H */
