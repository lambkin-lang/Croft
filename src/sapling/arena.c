#include "sapling/arena.h"
#include "common/arena_alloc_internal.h"

#include <stdlib.h>
#include <string.h>

#ifndef SAPLING_PAGE_SIZE
#define SAPLING_PAGE_SIZE 4096
#endif

struct SapMemArena {
    SapArenaOptions opts;
    SapArenaAllocStats stats;
    SapArenaAllocBudget budget;

    /* Slot table for page/node references. */
    void **malloc_chunks;
    uint32_t *chunk_sizes;
    uint32_t chunk_count;
    uint32_t chunk_capacity;

    /* Simple free stack for page numbers. */
    uint32_t *free_pgnos;
    uint32_t free_pgno_count;
    uint32_t free_pgno_capacity;

    /* Next available pgno if the free list is empty. */
    uint32_t next_pgno;

    /* Stable contiguous backing for SAP_ARENA_BACKING_LINEAR. */
    uint8_t *linear_storage;
    uint64_t linear_used_bytes;
    uint64_t linear_capacity_bytes;
};

static int arena_is_slot_backed(const SapMemArena *arena)
{
    return arena && (arena->opts.type == SAP_ARENA_BACKING_MALLOC ||
                     arena->opts.type == SAP_ARENA_BACKING_CUSTOM ||
                     arena->opts.type == SAP_ARENA_BACKING_LINEAR);
}

static uint64_t arena_active_slots_raw(const SapMemArena *arena)
{
    uint64_t reserved_slots = 0;
    uint64_t live_slots = 0;

    if (!arena)
        return 0;

    if (arena_is_slot_backed(arena))
        reserved_slots = 1; /* slot 0 is reserved */
    if (arena->chunk_count <= reserved_slots)
        return 0;

    live_slots = (uint64_t)arena->chunk_count - reserved_slots;
    if (arena->free_pgno_count >= live_slots)
        return 0;
    return live_slots - arena->free_pgno_count;
}

static void arena_refresh_active_stats(SapMemArena *arena)
{
    uint64_t active = 0;

    if (!arena)
        return;
    active = arena_active_slots_raw(arena);
    arena->stats.active_slots_current = active;
    if (active > arena->stats.active_slots_high_water)
        arena->stats.active_slots_high_water = active;
}

static int arena_budget_reject_active_slots(SapMemArena *arena)
{
    uint64_t max_slots = 0;

    if (!arena)
        return 0;
    max_slots = arena->budget.max_active_slots;
    if (max_slots == 0)
        return 0;
    if (arena_active_slots_raw(arena) + 1 > max_slots) {
        arena->stats.budget_reject_active_slots++;
        return 1;
    }
    return 0;
}

static int arena_resize_slot_tables(SapMemArena *arena, uint32_t min_capacity)
{
    void **new_chunks = NULL;
    uint32_t *new_sizes = NULL;
    uint32_t old_capacity = 0;
    uint32_t new_capacity = 0;

    if (!arena)
        return ERR_INVALID;
    if (arena->chunk_capacity >= min_capacity)
        return ERR_OK;

    old_capacity = arena->chunk_capacity;
    new_capacity = old_capacity == 0u ? 16u : old_capacity;
    while (new_capacity < min_capacity) {
        if (new_capacity > UINT32_MAX / 2u) {
            new_capacity = min_capacity;
            break;
        }
        new_capacity *= 2u;
    }

    new_chunks = calloc((size_t)new_capacity, sizeof(void *));
    if (!new_chunks)
        return ERR_OOM;
    new_sizes = calloc((size_t)new_capacity, sizeof(uint32_t));
    if (!new_sizes) {
        free(new_chunks);
        return ERR_OOM;
    }

    if (arena->malloc_chunks) {
        memcpy(new_chunks, arena->malloc_chunks, (size_t)old_capacity * sizeof(void *));
        free(arena->malloc_chunks);
    }
    if (arena->chunk_sizes) {
        memcpy(new_sizes, arena->chunk_sizes, (size_t)old_capacity * sizeof(uint32_t));
        free(arena->chunk_sizes);
    }

    arena->malloc_chunks = new_chunks;
    arena->chunk_sizes = new_sizes;
    arena->chunk_capacity = new_capacity;
    return ERR_OK;
}

static int arena_push_free_pgno(SapMemArena *arena, uint32_t pgno)
{
    uint32_t new_cap = 0;
    uint32_t *new_free = NULL;

    if (!arena)
        return ERR_INVALID;

    if (arena->free_pgno_count == arena->free_pgno_capacity) {
        new_cap = arena->free_pgno_capacity == 0u ? 16u : arena->free_pgno_capacity * 2u;
        new_free = realloc(arena->free_pgnos, (size_t)new_cap * sizeof(uint32_t));
        if (!new_free)
            return ERR_OOM;
        arena->free_pgnos = new_free;
        arena->free_pgno_capacity = new_cap;
    }

    arena->free_pgnos[arena->free_pgno_count++] = pgno;
    return ERR_OK;
}

static void arena_release_slot_claim(SapMemArena *arena, uint32_t slot, int from_free_list)
{
    if (!arena)
        return;

    if (!from_free_list && slot + 1u == arena->next_pgno) {
        arena->next_pgno--;
        return;
    }

    (void)arena_push_free_pgno(arena, slot);
}

static int arena_linear_alloc_bytes(SapMemArena *arena, uint32_t size, uint32_t alignment, void **ptr_out)
{
    uint64_t mask = 0;
    uint64_t aligned = 0;

    if (!arena || !ptr_out || !arena->linear_storage)
        return ERR_INVALID;

    if (alignment == 0u)
        alignment = 1u;
    mask = (uint64_t)alignment - 1u;
    aligned = (arena->linear_used_bytes + mask) & ~mask;
    if (aligned > arena->linear_capacity_bytes ||
        (uint64_t)size > arena->linear_capacity_bytes - aligned) {
        return ERR_OOM;
    }

    *ptr_out = arena->linear_storage + aligned;
    memset(*ptr_out, 0, size);
    arena->linear_used_bytes = aligned + (uint64_t)size;
    return ERR_OK;
}

int sap_arena_init(SapMemArena **arena_out, const SapArenaOptions *opts)
{
    SapMemArena *a = NULL;

    if (!arena_out || !opts)
        return ERR_INVALID;

    a = malloc(sizeof(SapMemArena));
    if (!a)
        return ERR_OOM;

    memcpy(&a->opts, opts, sizeof(*opts));
    memset(&a->stats, 0, sizeof(a->stats));
    memset(&a->budget, 0, sizeof(a->budget));
    a->malloc_chunks = NULL;
    a->chunk_sizes = NULL;
    a->chunk_count = 0;
    a->chunk_capacity = 0;
    a->free_pgnos = NULL;
    a->free_pgno_count = 0;
    a->free_pgno_capacity = 0;
    a->next_pgno = 1; /* pgno 0 is typically reserved or root */
    a->linear_storage = NULL;
    a->linear_used_bytes = 0u;
    a->linear_capacity_bytes = 0u;

    if (a->opts.type == SAP_ARENA_BACKING_LINEAR) {
        uint32_t eff_page_size = a->opts.page_size ? a->opts.page_size : SAPLING_PAGE_SIZE;
        uint64_t initial_bytes = a->opts.cfg.linear.initial_bytes;
        uint64_t max_bytes = a->opts.cfg.linear.max_bytes;

        if (max_bytes != 0u && initial_bytes > max_bytes) {
            free(a);
            return ERR_INVALID;
        }
        if (max_bytes == 0u)
            max_bytes = initial_bytes;
        if (max_bytes == 0u)
            max_bytes = (uint64_t)eff_page_size;
        if (max_bytes > (uint64_t)SIZE_MAX) {
            free(a);
            return ERR_OOM;
        }

        a->linear_storage = malloc((size_t)max_bytes);
        if (!a->linear_storage) {
            free(a);
            return ERR_OOM;
        }
        memset(a->linear_storage, 0, (size_t)max_bytes);
        a->linear_capacity_bytes = max_bytes;
    }

    arena_refresh_active_stats(a);
    *arena_out = a;
    return ERR_OK;
}

void sap_arena_destroy(SapMemArena *arena)
{
    if (!arena)
        return;

    if (arena->opts.type == SAP_ARENA_BACKING_MALLOC) {
        uint32_t i;
        for (i = 0; i < arena->chunk_count; i++) {
            free(arena->malloc_chunks[i]);
        }
    } else if (arena->opts.type == SAP_ARENA_BACKING_CUSTOM) {
        uint32_t eff_page_size = arena->opts.page_size ? arena->opts.page_size : SAPLING_PAGE_SIZE;
        uint32_t i;
        for (i = 0; i < arena->chunk_count; i++) {
            if (arena->malloc_chunks[i] && arena->opts.cfg.custom.free_page) {
                arena->opts.cfg.custom.free_page(arena->opts.cfg.custom.ctx,
                                                 arena->malloc_chunks[i],
                                                 eff_page_size);
            }
        }
    }

    free(arena->malloc_chunks);
    free(arena->chunk_sizes);
    free(arena->free_pgnos);
    free(arena->linear_storage);
    free(arena);
}

int sap_arena_alloc_page(SapMemArena *arena, void **page_out, uint32_t *pgno_out)
{
    uint32_t pgno = 0u;
    uint32_t eff_page_size = 0u;
    int from_free_list = 0;
    int rc = ERR_OK;
    void *page = NULL;
    uint64_t linear_used_before = 0u;

    if (!arena || !page_out || !pgno_out)
        return ERR_INVALID;

    arena->stats.page_alloc_calls++;
    if (arena_budget_reject_active_slots(arena)) {
        arena->stats.page_alloc_oom++;
        return ERR_OOM;
    }

    if (arena->free_pgno_count > 0u) {
        pgno = arena->free_pgnos[--arena->free_pgno_count];
        from_free_list = 1;
    } else {
        pgno = arena->next_pgno++;
    }

    eff_page_size = arena->opts.page_size ? arena->opts.page_size : SAPLING_PAGE_SIZE;
    linear_used_before = arena->linear_used_bytes;

    if (arena->opts.type == SAP_ARENA_BACKING_MALLOC ||
        arena->opts.type == SAP_ARENA_BACKING_CUSTOM ||
        arena->opts.type == SAP_ARENA_BACKING_LINEAR) {
        if (from_free_list &&
            pgno < arena->chunk_count &&
            arena->malloc_chunks[pgno] &&
            arena->chunk_sizes[pgno] >= eff_page_size) {
            page = arena->malloc_chunks[pgno];
            memset(page, 0, eff_page_size);
        } else if (arena->opts.type == SAP_ARENA_BACKING_MALLOC) {
            page = calloc(1, eff_page_size);
        } else if (arena->opts.type == SAP_ARENA_BACKING_CUSTOM) {
            if (!arena->opts.cfg.custom.alloc_page)
                return ERR_INVALID;
            page = arena->opts.cfg.custom.alloc_page(arena->opts.cfg.custom.ctx, eff_page_size);
        } else {
            rc = arena_linear_alloc_bytes(arena, eff_page_size, eff_page_size, &page);
            if (rc != ERR_OK)
                page = NULL;
        }

        if (!page) {
            arena_release_slot_claim(arena, pgno, from_free_list);
            arena->stats.page_alloc_oom++;
            return ERR_OOM;
        }

        if (!(from_free_list &&
              pgno < arena->chunk_count &&
              arena->malloc_chunks[pgno] == page &&
              arena->chunk_sizes[pgno] >= eff_page_size)) {
            rc = arena_resize_slot_tables(arena, pgno + 1u);
            if (rc != ERR_OK) {
                if (arena->opts.type == SAP_ARENA_BACKING_MALLOC) {
                    free(page);
                } else if (arena->opts.type == SAP_ARENA_BACKING_CUSTOM &&
                           arena->opts.cfg.custom.free_page) {
                    arena->opts.cfg.custom.free_page(arena->opts.cfg.custom.ctx, page, eff_page_size);
                } else if (arena->opts.type == SAP_ARENA_BACKING_LINEAR) {
                    arena->linear_used_bytes = linear_used_before;
                }
                arena_release_slot_claim(arena, pgno, from_free_list);
                arena->stats.page_alloc_oom++;
                return rc;
            }
            arena->malloc_chunks[pgno] = page;
            arena->chunk_sizes[pgno] = eff_page_size;
            if (pgno >= arena->chunk_count)
                arena->chunk_count = pgno + 1u;
        }

        *page_out = page;
        *pgno_out = pgno;
        arena->stats.page_alloc_ok++;
        arena_refresh_active_stats(arena);
        return ERR_OK;
    }

    return ERR_INVALID;
}

int sap_arena_free_page(SapMemArena *arena, uint32_t pgno)
{
    if (!arena)
        return ERR_INVALID;
    arena->stats.page_free_calls++;

    if (arena_push_free_pgno(arena, pgno) != ERR_OK)
        return ERR_OOM;

    arena->stats.page_free_ok++;
    arena_refresh_active_stats(arena);
    return ERR_OK;
}

int sap_arena_free_page_ptr(SapMemArena *arena, void *page)
{
    uint32_t i = 0u;

    if (!arena || !page)
        return ERR_INVALID;

    if (arena->opts.type == SAP_ARENA_BACKING_MALLOC ||
        arena->opts.type == SAP_ARENA_BACKING_CUSTOM ||
        arena->opts.type == SAP_ARENA_BACKING_LINEAR) {
        for (i = 1u; i < arena->chunk_count; i++) {
            if (arena->malloc_chunks[i] == page)
                return sap_arena_free_page(arena, i);
        }
    }
    return ERR_NOT_FOUND;
}

int sap_arena_alloc_node(SapMemArena *arena, uint32_t size, void **node_out, uint32_t *nodeno_out)
{
    void *node = NULL;
    uint32_t nodeno = 0u;
    int rc = ERR_OK;
    uint64_t linear_used_before = 0u;

    if (!arena || !node_out || !nodeno_out)
        return ERR_INVALID;

    arena->stats.node_alloc_calls++;
    if (arena_budget_reject_active_slots(arena)) {
        arena->stats.node_alloc_oom++;
        return ERR_OOM;
    }

    linear_used_before = arena->linear_used_bytes;
    if (arena->opts.type == SAP_ARENA_BACKING_MALLOC) {
        node = calloc(1, size);
    } else if (arena->opts.type == SAP_ARENA_BACKING_CUSTOM) {
        if (!arena->opts.cfg.custom.alloc_page)
            return ERR_INVALID;
        node = arena->opts.cfg.custom.alloc_page(arena->opts.cfg.custom.ctx, size);
        if (node)
            memset(node, 0, size);
    } else if (arena->opts.type == SAP_ARENA_BACKING_LINEAR) {
        rc = arena_linear_alloc_bytes(arena, size, (uint32_t)_Alignof(max_align_t), &node);
        if (rc != ERR_OK)
            node = NULL;
    } else {
        return ERR_INVALID;
    }

    if (!node) {
        arena->stats.node_alloc_oom++;
        return ERR_OOM;
    }

    nodeno = arena->next_pgno++;
    rc = arena_resize_slot_tables(arena, nodeno + 1u);
    if (rc != ERR_OK) {
        if (arena->opts.type == SAP_ARENA_BACKING_MALLOC) {
            free(node);
        } else if (arena->opts.type == SAP_ARENA_BACKING_CUSTOM &&
                   arena->opts.cfg.custom.free_page) {
            arena->opts.cfg.custom.free_page(arena->opts.cfg.custom.ctx, node, size);
        } else if (arena->opts.type == SAP_ARENA_BACKING_LINEAR) {
            arena->linear_used_bytes = linear_used_before;
        }
        arena->next_pgno--;
        arena->stats.node_alloc_oom++;
        return rc;
    }

    arena->malloc_chunks[nodeno] = node;
    arena->chunk_sizes[nodeno] = size;
    if (nodeno >= arena->chunk_count)
        arena->chunk_count = nodeno + 1u;

    *node_out = node;
    *nodeno_out = nodeno;
    arena->stats.node_alloc_ok++;
    arena_refresh_active_stats(arena);
    return ERR_OK;
}

int sap_arena_free_node(SapMemArena *arena, uint32_t nodeno, uint32_t size)
{
    void *p = NULL;

    (void)size;
    if (!arena)
        return ERR_INVALID;
    arena->stats.node_free_calls++;

    if (arena->opts.type == SAP_ARENA_BACKING_MALLOC ||
        arena->opts.type == SAP_ARENA_BACKING_CUSTOM ||
        arena->opts.type == SAP_ARENA_BACKING_LINEAR) {
        if (nodeno < arena->chunk_count && arena->malloc_chunks[nodeno]) {
            p = arena->malloc_chunks[nodeno];
            if (arena->opts.type == SAP_ARENA_BACKING_MALLOC) {
                arena->malloc_chunks[nodeno] = NULL;
                arena->chunk_sizes[nodeno] = 0u;
                free(p);
            } else if (arena->opts.type == SAP_ARENA_BACKING_CUSTOM) {
                arena->malloc_chunks[nodeno] = NULL;
                arena->chunk_sizes[nodeno] = 0u;
                if (arena->opts.cfg.custom.free_page)
                    arena->opts.cfg.custom.free_page(arena->opts.cfg.custom.ctx, p, size);
            }
            if (arena_push_free_pgno(arena, nodeno) != ERR_OK)
                return ERR_OOM;
            arena->stats.node_free_ok++;
            arena_refresh_active_stats(arena);
            return ERR_OK;
        }
    }

    return ERR_NOT_FOUND;
}

int sap_arena_free_node_ptr(SapMemArena *arena, void *node, uint32_t size)
{
    uint32_t i = 0u;

    if (!arena || !node)
        return ERR_INVALID;

    if (arena->opts.type == SAP_ARENA_BACKING_MALLOC ||
        arena->opts.type == SAP_ARENA_BACKING_CUSTOM ||
        arena->opts.type == SAP_ARENA_BACKING_LINEAR) {
        for (i = 1u; i < arena->chunk_count; i++) {
            if (arena->malloc_chunks[i] == node)
                return sap_arena_free_node(arena, i, size);
        }
        return ERR_NOT_FOUND;
    }

    return ERR_INVALID;
}

void *sap_arena_resolve(SapMemArena *arena, uint32_t p_or_n_no)
{
    if (!arena)
        return NULL;

    if (arena->opts.type == SAP_ARENA_BACKING_MALLOC ||
        arena->opts.type == SAP_ARENA_BACKING_CUSTOM ||
        arena->opts.type == SAP_ARENA_BACKING_LINEAR) {
        if (p_or_n_no < arena->chunk_count)
            return arena->malloc_chunks[p_or_n_no];
    }
    return NULL;
}

uint32_t sap_arena_active_pages(const SapMemArena *arena)
{
    return (uint32_t)arena_active_slots_raw(arena);
}

int sap_arena_alloc_stats(const SapMemArena *arena, SapArenaAllocStats *out)
{
    if (!arena || !out)
        return ERR_INVALID;
    *out = arena->stats;
    return ERR_OK;
}

int sap_arena_alloc_stats_reset(SapMemArena *arena)
{
    if (!arena)
        return ERR_INVALID;
    memset(&arena->stats, 0, sizeof(arena->stats));
    arena_refresh_active_stats(arena);
    return ERR_OK;
}

#define DIFF_FIELD(name)                                                                           \
    do {                                                                                           \
        if (end->name >= start->name)                                                              \
            delta_out->name = end->name - start->name;                                             \
        else                                                                                       \
            delta_out->name = 0;                                                                   \
    } while (0)

int sap_arena_alloc_stats_diff(const SapArenaAllocStats *start, const SapArenaAllocStats *end,
                               SapArenaAllocStats *delta_out)
{
    if (!start || !end || !delta_out)
        return ERR_INVALID;
    memset(delta_out, 0, sizeof(*delta_out));

    DIFF_FIELD(page_alloc_calls);
    DIFF_FIELD(page_alloc_ok);
    DIFF_FIELD(page_alloc_oom);
    DIFF_FIELD(page_free_calls);
    DIFF_FIELD(page_free_ok);

    DIFF_FIELD(node_alloc_calls);
    DIFF_FIELD(node_alloc_ok);
    DIFF_FIELD(node_alloc_oom);
    DIFF_FIELD(node_free_calls);
    DIFF_FIELD(node_free_ok);

    DIFF_FIELD(scratch_alloc_calls);
    DIFF_FIELD(scratch_alloc_ok);
    DIFF_FIELD(scratch_alloc_fail);
    DIFF_FIELD(scratch_bytes_requested);
    DIFF_FIELD(scratch_bytes_granted);

    DIFF_FIELD(txn_vec_reserve_calls);
    DIFF_FIELD(txn_vec_reserve_ok);
    DIFF_FIELD(txn_vec_reserve_oom);
    DIFF_FIELD(txn_vec_bytes_requested);
    DIFF_FIELD(txn_vec_bytes_allocated);

    DIFF_FIELD(budget_reject_active_slots);
    DIFF_FIELD(budget_reject_scratch_bytes);
    DIFF_FIELD(budget_reject_txn_vec_bytes);

    delta_out->active_slots_current = end->active_slots_current;
    delta_out->active_slots_high_water = end->active_slots_high_water;
    return ERR_OK;
}

#undef DIFF_FIELD

int sap_arena_set_alloc_budget(SapMemArena *arena, const SapArenaAllocBudget *budget)
{
    if (!arena || !budget)
        return ERR_INVALID;
    arena->budget = *budget;
    return ERR_OK;
}

int sap_arena_get_alloc_budget(const SapMemArena *arena, SapArenaAllocBudget *budget_out)
{
    if (!arena || !budget_out)
        return ERR_INVALID;
    *budget_out = arena->budget;
    return ERR_OK;
}

int sap_arena_alloc_budget_check_scratch(SapMemArena *arena, uint64_t requested_bytes)
{
    if (!arena)
        return ERR_INVALID;
    if (arena->budget.max_scratch_request_bytes != 0 &&
        requested_bytes > arena->budget.max_scratch_request_bytes) {
        arena->stats.budget_reject_scratch_bytes++;
        return ERR_OOM;
    }
    return ERR_OK;
}

int sap_arena_alloc_budget_check_txn_vec(SapMemArena *arena, uint64_t requested_bytes)
{
    if (!arena)
        return ERR_INVALID;
    if (arena->budget.max_txn_vec_reserve_bytes != 0 &&
        requested_bytes > arena->budget.max_txn_vec_reserve_bytes) {
        arena->stats.budget_reject_txn_vec_bytes++;
        return ERR_OOM;
    }
    return ERR_OK;
}

void sap_arena_alloc_note_scratch(SapMemArena *arena, uint64_t requested_bytes,
                                  uint64_t granted_bytes, int ok)
{
    if (!arena)
        return;
    arena->stats.scratch_alloc_calls++;
    arena->stats.scratch_bytes_requested += requested_bytes;
    if (ok) {
        arena->stats.scratch_alloc_ok++;
        arena->stats.scratch_bytes_granted += granted_bytes;
    } else {
        arena->stats.scratch_alloc_fail++;
    }
}

void sap_arena_alloc_note_txn_vec(SapMemArena *arena, uint64_t requested_bytes,
                                  uint64_t allocated_bytes, int ok)
{
    if (!arena)
        return;
    arena->stats.txn_vec_reserve_calls++;
    arena->stats.txn_vec_bytes_requested += requested_bytes;
    if (ok) {
        arena->stats.txn_vec_reserve_ok++;
        arena->stats.txn_vec_bytes_allocated += allocated_bytes;
    } else {
        arena->stats.txn_vec_reserve_oom++;
    }
}
