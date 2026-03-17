/*
 * thatch.c — implementation of the Thatch packed data subsystem
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 lambkin-lang
 */

#include "sapling/thatch.h"
#include "sapling/arena.h"
#include "sapling/txn.h"
/* #include <string.h> removed for Lambkin -nostdlib */

/* ------------------------------------------------------------------ */
/* Internal State Structures                                          */
/* ------------------------------------------------------------------ */

/*
 * ThatchTxnState tracks all regions allocated during a transaction.
 * This is the crucial link for zero-overhead GC.
 *
 * For nested transactions, parent_state links to the parent txn's
 * ThatchTxnState so that on commit, child regions are merged into the
 * parent's tracking list (making parent-abort clean them up correctly).
 */
typedef struct ThatchTxnState {
    ThatchRegion *active_regions;
    struct ThatchTxnState *parent_state;
} ThatchTxnState;

/* ------------------------------------------------------------------ */
/* Subsystem Callbacks                                                */
/* ------------------------------------------------------------------ */

static int thatch_on_begin(SapTxnCtx *txn, void *parent_state, void **state_out) {
    ThatchTxnState *state = sap_txn_scratch_alloc(txn, sizeof(ThatchTxnState));
    if (!state) return ERR_OOM;

    state->active_regions = NULL;
    state->parent_state = (ThatchTxnState *)parent_state;
    *state_out = state;
    return ERR_OK;
}

static int thatch_on_commit(SapTxnCtx *txn, void *state_ptr) {
    (void)txn;
    ThatchTxnState *state = (ThatchTxnState *)state_ptr;

    /* Seal all regions created in this transaction */
    ThatchRegion *curr = state->active_regions;
    ThatchRegion *tail = NULL;
    while (curr) {
        curr->sealed = 1;
        tail = curr;
        curr = curr->next;
    }

    if (state->parent_state && state->active_regions) {
        /*
         * Nested transaction: transfer ownership to parent so that a
         * subsequent parent abort will free these regions correctly.
         * Prepend child's list to parent's list via the child's tail.
         */
        tail->next = state->parent_state->active_regions;
        state->parent_state->active_regions = state->active_regions;
    }

    state->active_regions = NULL;
    return ERR_OK;
}

static void thatch_on_abort(SapTxnCtx *txn, void *state_ptr) {
    ThatchTxnState *state = (ThatchTxnState *)state_ptr;
    if (!state) return;
    SapMemArena *arena = sap_txn_arena(txn);

    /*
     * Instantaneous GC: Drop all regions allocated in this failed transaction.
     * No walking ASTs, no tracing pointers.
     */
    ThatchRegion *curr = state->active_regions;
    while (curr) {
        ThatchRegion *next = curr->next;
        sap_arena_free_page(arena, curr->pgno);
        sap_arena_free_node(arena, curr->nodeno, sizeof(ThatchRegion));
        curr = next;
    }
}

static void thatch_on_env_destroy(void *env_state) {
    (void)env_state;
}

static const SapTxnSubsystemCallbacks thatch_cbs = {
    .on_begin      = thatch_on_begin,
    .on_commit     = thatch_on_commit,
    .on_abort      = thatch_on_abort,
    .on_env_destroy = thatch_on_env_destroy
};

int sap_thatch_subsystem_init(SapEnv *env) {
    return sap_env_register_subsystem(env, SAP_SUBSYSTEM_THATCH, &thatch_cbs);
}

int thatch_region_new(SapTxnCtx *txn, ThatchRegion **region_out) {
    if (!txn || !region_out) return ERR_INVALID;

    ThatchTxnState *state = sap_txn_subsystem_state(txn, SAP_SUBSYSTEM_THATCH);
    if (!state) return ERR_INVALID;

    SapMemArena *arena = sap_txn_arena(txn);

    /*
     * Allocate the ThatchRegion struct from the arena (not txn scratch)
     * so it survives transaction commit and remains valid for readers.
     */
    ThatchRegion *region = NULL;
    uint32_t region_nodeno = 0;
    int rc = sap_arena_alloc_node(arena, sizeof(ThatchRegion),
                                  (void **)&region, &region_nodeno);
    if (rc != 0) return ERR_OOM;

    rc = sap_arena_alloc_page(arena, &region->page_ptr, &region->pgno);
    if (rc != 0) {
        sap_arena_free_node(arena, region_nodeno, sizeof(ThatchRegion));
        return ERR_OOM;
    }

    region->arena    = arena;
    region->nodeno   = region_nodeno;
    region->capacity = sap_env_get_page_size(sap_txn_env(txn));
    region->head     = 0;
    region->sealed   = 0;

    /* Track for transaction lifecycle */
    region->next            = state->active_regions;
    state->active_regions   = region;

    *region_out = region;
    return ERR_OK;
}

int thatch_seal(SapTxnCtx *txn, ThatchRegion *region) {
    (void)txn;
    if (!region) return ERR_INVALID;
    region->sealed = 1;
    return ERR_OK;
}

int thatch_region_release(SapTxnCtx *txn, ThatchRegion *region) {
    if (!txn || !region) return ERR_INVALID;

    ThatchTxnState *state = sap_txn_subsystem_state(txn, SAP_SUBSYSTEM_THATCH);
    if (!state) return ERR_INVALID;

    /* Unlink from the active region list — only free if actually found */
    int found = 0;
    ThatchRegion **pp = &state->active_regions;
    while (*pp) {
        if (*pp == region) {
            *pp = region->next;
            found = 1;
            break;
        }
        pp = &(*pp)->next;
    }

    if (!found) return ERR_INVALID;

    SapMemArena *arena = region->arena;
    sap_arena_free_page(arena, region->pgno);
    sap_arena_free_node(arena, region->nodeno, sizeof(ThatchRegion));
    return ERR_OK;
}
