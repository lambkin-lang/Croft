#ifndef CROFT_WIT_WIRE_H
#define CROFT_WIT_WIRE_H

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

int sap_wit_skip_value(const ThatchRegion* region, ThatchCursor* cursor);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_WIT_WIRE_H */
