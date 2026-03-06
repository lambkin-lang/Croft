#include "sapling/thatch.h"

#include <string.h>

int thatch_write_tag(ThatchRegion *region, uint8_t tag)
{
    if (!region || region->sealed)
        return ERR_INVALID;
    if (region->head + 1 > region->capacity)
        return ERR_OOM;

    uint8_t *mem = (uint8_t *)region->page_ptr;
    mem[region->head] = tag;
    region->head += 1;
    return ERR_OK;
}

int thatch_write_data(ThatchRegion *region, const void *data, uint32_t len)
{
    if (!region || region->sealed)
        return ERR_INVALID;
    if (!data || len == 0)
        return len == 0 ? ERR_OK : ERR_INVALID;
    if (region->head + len > region->capacity)
        return ERR_OOM;

    uint8_t *mem = (uint8_t *)region->page_ptr;
    memcpy(mem + region->head, data, len);
    region->head += len;
    return ERR_OK;
}

int thatch_reserve_skip(ThatchRegion *region, ThatchCursor *skip_loc_out)
{
    if (!region || region->sealed || !skip_loc_out)
        return ERR_INVALID;
    if (region->head + sizeof(uint32_t) > region->capacity)
        return ERR_OOM;

    *skip_loc_out = region->head;
    region->head += sizeof(uint32_t);
    return ERR_OK;
}

int thatch_commit_skip(ThatchRegion *region, ThatchCursor skip_loc)
{
    if (!region || region->sealed)
        return ERR_INVALID;
    if (skip_loc > region->head || region->head - skip_loc < sizeof(uint32_t))
        return ERR_RANGE;

    uint32_t skip_len = region->head - skip_loc - sizeof(uint32_t);
    uint8_t *mem = (uint8_t *)region->page_ptr;
    memcpy(mem + skip_loc, &skip_len, sizeof(uint32_t));
    return ERR_OK;
}

int thatch_read_tag(const ThatchRegion *region, ThatchCursor *cursor, uint8_t *tag_out)
{
    if (!region || !cursor || !tag_out)
        return ERR_INVALID;
    if (*cursor > region->head)
        return ERR_RANGE;
    if (1u > (region->head - *cursor))
        return ERR_RANGE;

    uint8_t *mem = (uint8_t *)region->page_ptr;
    *tag_out = mem[*cursor];
    *cursor += 1;
    return ERR_OK;
}

int thatch_read_data(const ThatchRegion *region, ThatchCursor *cursor, uint32_t len, void *data_out)
{
    if (!region || !cursor || !data_out)
        return ERR_INVALID;
    if (*cursor > region->head)
        return ERR_RANGE;
    if (len > (region->head - *cursor))
        return ERR_RANGE;

    uint8_t *mem = (uint8_t *)region->page_ptr;
    memcpy(data_out, mem + *cursor, len);
    *cursor += len;
    return ERR_OK;
}

int thatch_read_skip_len(const ThatchRegion *region, ThatchCursor *cursor, uint32_t *skip_len_out)
{
    if (!region || !cursor || !skip_len_out)
        return ERR_INVALID;
    if (*cursor > region->head)
        return ERR_RANGE;
    if ((uint32_t)sizeof(uint32_t) > (region->head - *cursor))
        return ERR_RANGE;

    uint8_t *mem = (uint8_t *)region->page_ptr;
    memcpy(skip_len_out, mem + *cursor, sizeof(uint32_t));
    *cursor += sizeof(uint32_t);
    return ERR_OK;
}

int thatch_advance_cursor(const ThatchRegion *region, ThatchCursor *cursor, uint32_t skip_len)
{
    if (!region || !cursor)
        return ERR_INVALID;
    if (*cursor > region->head)
        return ERR_RANGE;
    if (skip_len > (region->head - *cursor))
        return ERR_RANGE;

    *cursor += skip_len;
    return ERR_OK;
}

int thatch_read_ptr(const ThatchRegion *region, ThatchCursor *cursor, uint32_t len,
                    const void **ptr_out)
{
    if (!region || !cursor || !ptr_out)
        return ERR_INVALID;
    if (*cursor > region->head)
        return ERR_RANGE;
    if (len > (region->head - *cursor))
        return ERR_RANGE;

    uint8_t *mem = (uint8_t *)region->page_ptr;
    *ptr_out = mem + *cursor;
    *cursor += len;
    return ERR_OK;
}

uint32_t thatch_region_used(const ThatchRegion *region)
{
    return region ? region->head : 0;
}

int thatch_region_init_readonly(ThatchRegion *out, const void *data, uint32_t len)
{
    if (!out)
        return ERR_INVALID;
    if (!data && len)
        return ERR_INVALID;

    memset(out, 0, sizeof(*out));
    out->page_ptr = (void *)data;
    out->capacity = len;
    out->head = len;
    out->sealed = 1;
    return ERR_OK;
}
