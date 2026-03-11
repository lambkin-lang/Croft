#include "croft/wit_runtime_support.h"

#include <stdint.h>

#ifndef CROFT_WIT_FREESTANDING_HEAP_BYTES
#define CROFT_WIT_FREESTANDING_HEAP_BYTES (64u * 1024u)
#endif

#define SAP_WIT_RT_INVALID_OFFSET UINT32_MAX
#define SAP_WIT_RT_MIN_SPLIT_PAYLOAD 8u

typedef struct {
    uint32_t size;
    uint32_t next;
    uint32_t used;
    uint32_t reserved;
} SapWitRtBlockHeader;

typedef union {
    uint64_t align_u64;
    uint8_t bytes[CROFT_WIT_FREESTANDING_HEAP_BYTES];
} SapWitRtHeapStorage;

static SapWitRtHeapStorage g_sap_wit_rt_heap;
static uint32_t g_sap_wit_rt_heap_initialized = 0u;

_Static_assert(sizeof(SapWitRtBlockHeader) == 16u, "WIT freestanding block header must stay stable");
_Static_assert(CROFT_WIT_FREESTANDING_HEAP_BYTES > sizeof(SapWitRtBlockHeader),
               "WIT freestanding heap must fit at least one block");

static uint32_t sap_wit_rt_align_size(size_t size)
{
    uint32_t aligned;

    if (size == 0u) {
        size = 1u;
    }
    if (size > (size_t)(UINT32_MAX - 7u)) {
        return 0u;
    }

    aligned = (uint32_t)size;
    aligned = (aligned + 7u) & ~7u;
    return aligned;
}

static SapWitRtBlockHeader *sap_wit_rt_block_at(uint32_t offset)
{
    if (offset == SAP_WIT_RT_INVALID_OFFSET
            || offset > (uint32_t)(CROFT_WIT_FREESTANDING_HEAP_BYTES - sizeof(SapWitRtBlockHeader))) {
        return NULL;
    }
    return (SapWitRtBlockHeader *)(void *)(g_sap_wit_rt_heap.bytes + offset);
}

static uint32_t sap_wit_rt_block_offset(const SapWitRtBlockHeader *block)
{
    return (uint32_t)((const uint8_t *)block - g_sap_wit_rt_heap.bytes);
}

static uint8_t *sap_wit_rt_block_data(SapWitRtBlockHeader *block)
{
    return (uint8_t *)(void *)(block + 1);
}

static void sap_wit_rt_heap_init(void)
{
    SapWitRtBlockHeader *head;

    if (g_sap_wit_rt_heap_initialized) {
        return;
    }

    head = (SapWitRtBlockHeader *)(void *)g_sap_wit_rt_heap.bytes;
    head->size = (uint32_t)CROFT_WIT_FREESTANDING_HEAP_BYTES - (uint32_t)sizeof(*head);
    head->next = SAP_WIT_RT_INVALID_OFFSET;
    head->used = 0u;
    head->reserved = 0u;
    g_sap_wit_rt_heap_initialized = 1u;
}

static void sap_wit_rt_split_block(SapWitRtBlockHeader *block, uint32_t need)
{
    uint32_t remaining;
    uint32_t next_offset;
    SapWitRtBlockHeader *next;

    if (!block || block->size <= need) {
        return;
    }

    remaining = block->size - need;
    if (remaining <= (uint32_t)(sizeof(SapWitRtBlockHeader) + SAP_WIT_RT_MIN_SPLIT_PAYLOAD)) {
        return;
    }

    next_offset = sap_wit_rt_block_offset(block) + (uint32_t)sizeof(*block) + need;
    next = sap_wit_rt_block_at(next_offset);
    if (!next) {
        return;
    }

    next->size = remaining - (uint32_t)sizeof(*next);
    next->next = block->next;
    next->used = 0u;
    next->reserved = 0u;

    block->size = need;
    block->next = next_offset;
}

static void sap_wit_rt_coalesce_one(SapWitRtBlockHeader *block)
{
    SapWitRtBlockHeader *next;

    if (!block) {
        return;
    }

    while (block->next != SAP_WIT_RT_INVALID_OFFSET) {
        next = sap_wit_rt_block_at(block->next);
        if (!next || next->used) {
            break;
        }
        block->size += (uint32_t)sizeof(*next) + next->size;
        block->next = next->next;
    }
}

static void sap_wit_rt_coalesce_all(void)
{
    SapWitRtBlockHeader *block;

    sap_wit_rt_heap_init();
    block = sap_wit_rt_block_at(0u);
    while (block) {
        if (!block->used) {
            sap_wit_rt_coalesce_one(block);
        }
        if (block->next == SAP_WIT_RT_INVALID_OFFSET) {
            break;
        }
        block = sap_wit_rt_block_at(block->next);
    }
}

static int sap_wit_rt_ptr_in_heap(const void *ptr)
{
    const uint8_t *byte_ptr = (const uint8_t *)ptr;

    return byte_ptr >= g_sap_wit_rt_heap.bytes + sizeof(SapWitRtBlockHeader)
        && byte_ptr < g_sap_wit_rt_heap.bytes + CROFT_WIT_FREESTANDING_HEAP_BYTES;
}

static SapWitRtBlockHeader *sap_wit_rt_block_from_ptr(void *ptr)
{
    SapWitRtBlockHeader *block;

    if (!ptr || !sap_wit_rt_ptr_in_heap(ptr)) {
        return NULL;
    }

    block = ((SapWitRtBlockHeader *)ptr) - 1;
    if (!sap_wit_rt_ptr_in_heap(sap_wit_rt_block_data(block))) {
        return NULL;
    }
    return block;
}

void *sap_wit_rt_memcpy(void *dest, const void *src, size_t len)
{
    uint8_t *dst = (uint8_t *)dest;
    const uint8_t *from = (const uint8_t *)src;
    size_t i;

    if (len == 0u) {
        return dest;
    }
    if (!dst || !from) {
        return dest;
    }

    for (i = 0u; i < len; i++) {
        dst[i] = from[i];
    }
    return dest;
}

void *sap_wit_rt_memset(void *dest, int value, size_t len)
{
    uint8_t *dst = (uint8_t *)dest;
    size_t i;

    if (len == 0u) {
        return dest;
    }
    if (!dst) {
        return dest;
    }

    for (i = 0u; i < len; i++) {
        dst[i] = (uint8_t)value;
    }
    return dest;
}

size_t sap_wit_rt_strlen(const char *text)
{
    size_t len = 0u;

    if (!text) {
        return 0u;
    }
    while (text[len] != '\0') {
        len++;
    }
    return len;
}

void *sap_wit_rt_malloc(size_t size)
{
    SapWitRtBlockHeader *block;
    uint32_t need;

    need = sap_wit_rt_align_size(size);
    if (need == 0u) {
        return NULL;
    }

    sap_wit_rt_heap_init();
    block = sap_wit_rt_block_at(0u);
    while (block) {
        if (!block->used) {
            sap_wit_rt_coalesce_one(block);
            if (block->size >= need) {
                sap_wit_rt_split_block(block, need);
                block->used = 1u;
                return sap_wit_rt_block_data(block);
            }
        }
        if (block->next == SAP_WIT_RT_INVALID_OFFSET) {
            break;
        }
        block = sap_wit_rt_block_at(block->next);
    }
    return NULL;
}

void sap_wit_rt_free(void *ptr)
{
    SapWitRtBlockHeader *block;

    if (!ptr) {
        return;
    }

    block = sap_wit_rt_block_from_ptr(ptr);
    if (!block) {
        return;
    }

    block->used = 0u;
    sap_wit_rt_coalesce_all();
}

void *sap_wit_rt_realloc(void *ptr, size_t size)
{
    SapWitRtBlockHeader *block;
    uint32_t need;
    void *grown;

    if (!ptr) {
        return sap_wit_rt_malloc(size);
    }
    if (size == 0u) {
        sap_wit_rt_free(ptr);
        return NULL;
    }

    block = sap_wit_rt_block_from_ptr(ptr);
    if (!block) {
        return NULL;
    }

    need = sap_wit_rt_align_size(size);
    if (need == 0u) {
        return NULL;
    }
    if (block->size >= need) {
        sap_wit_rt_split_block(block, need);
        return ptr;
    }

    sap_wit_rt_coalesce_one(block);
    if (block->size >= need) {
        sap_wit_rt_split_block(block, need);
        return ptr;
    }

    grown = sap_wit_rt_malloc(size);
    if (!grown) {
        return NULL;
    }

    sap_wit_rt_memcpy(grown, ptr, block->size);
    sap_wit_rt_free(ptr);
    return grown;
}

void *memcpy(void *dest, const void *src, size_t len)
{
    return sap_wit_rt_memcpy(dest, src, len);
}

void *memset(void *dest, int value, size_t len)
{
    return sap_wit_rt_memset(dest, value, len);
}

size_t strlen(const char *text)
{
    return sap_wit_rt_strlen(text);
}

void *malloc(size_t size)
{
    return sap_wit_rt_malloc(size);
}

void free(void *ptr)
{
    sap_wit_rt_free(ptr);
}

void *realloc(void *ptr, size_t size)
{
    return sap_wit_rt_realloc(ptr, size);
}
