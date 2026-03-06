#include "croft/wit_wire.h"

#include "sapling/err.h"

int sap_wit_skip_value(const ThatchRegion* region, ThatchCursor* cursor)
{
    uint8_t tag;

    if (!region || !cursor) {
        return ERR_INVALID;
    }

    if (thatch_read_tag(region, cursor, &tag) != ERR_OK) {
        return ERR_RANGE;
    }

    switch (tag) {
        case SAP_WIT_TAG_S8:
        case SAP_WIT_TAG_U8:
            return thatch_advance_cursor(region, cursor, 1u);

        case SAP_WIT_TAG_S16:
        case SAP_WIT_TAG_U16:
            return thatch_advance_cursor(region, cursor, 2u);

        case SAP_WIT_TAG_S32:
        case SAP_WIT_TAG_U32:
        case SAP_WIT_TAG_F32:
            return thatch_advance_cursor(region, cursor, 4u);

        case SAP_WIT_TAG_S64:
        case SAP_WIT_TAG_U64:
        case SAP_WIT_TAG_F64:
            return thatch_advance_cursor(region, cursor, 8u);

        case SAP_WIT_TAG_BOOL_TRUE:
        case SAP_WIT_TAG_BOOL_FALSE:
            return ERR_OK;

        case SAP_WIT_TAG_ENUM:
            return thatch_advance_cursor(region, cursor, 1u);

        case SAP_WIT_TAG_FLAGS:
        case SAP_WIT_TAG_RESOURCE:
            return thatch_advance_cursor(region, cursor, 4u);

        case SAP_WIT_TAG_STRING:
        case SAP_WIT_TAG_BYTES: {
            uint32_t len;
            int rc = thatch_read_data(region, cursor, 4u, &len);
            if (rc != ERR_OK) {
                return rc;
            }
            return thatch_advance_cursor(region, cursor, len);
        }

        case SAP_WIT_TAG_RECORD:
        case SAP_WIT_TAG_TUPLE:
        case SAP_WIT_TAG_LIST:
        case SAP_WIT_TAG_VARIANT: {
            uint32_t skip;
            int rc = thatch_read_skip_len(region, cursor, &skip);
            if (rc != ERR_OK) {
                return rc;
            }
            return thatch_advance_cursor(region, cursor, skip);
        }

        case SAP_WIT_TAG_OPTION_SOME:
        case SAP_WIT_TAG_RESULT_OK:
        case SAP_WIT_TAG_RESULT_ERR:
            return sap_wit_skip_value(region, cursor);

        case SAP_WIT_TAG_OPTION_NONE:
            return ERR_OK;

        default:
            return ERR_TYPE;
    }
}
