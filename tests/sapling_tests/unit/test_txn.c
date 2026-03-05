/*
 * test_txn.c - transaction manager invariants
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 lambkin-lang
 */

#include "sapling/arena.h"
#include "sapling/sapling.h"
#include "sapling/txn.h"

#include <stdio.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(expr)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (expr)                                                                                  \
        {                                                                                          \
            g_pass++;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s  (%s:%d)\n", #expr, __FILE__, __LINE__);                     \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

#define SECTION(name) printf("--- %s ---\n", name)

static int setup_env(SapMemArena **arena_out, SapEnv **env_out)
{
    if (!arena_out || !env_out)
        return ERR_INVALID;
    *arena_out = NULL;
    *env_out = NULL;
    SapArenaOptions opts = { .type = SAP_ARENA_BACKING_MALLOC, .page_size = 4096 };
    int rc = sap_arena_init(arena_out, &opts);
    if (rc != ERR_OK)
        return rc;
    *env_out = sap_env_create(*arena_out, 4096);
    if (!*env_out)
    {
        sap_arena_destroy(*arena_out);
        *arena_out = NULL;
        return ERR_OOM;
    }
    return ERR_OK;
}

static void teardown_env(SapMemArena *arena, SapEnv *env)
{
    if (env)
        sap_env_destroy(env);
    if (arena)
        sap_arena_destroy(arena);
}

static void test_top_level_write_exclusive(void)
{
    SECTION("top-level write exclusivity");
    SapMemArena *arena = NULL;
    SapEnv *env = NULL;
    CHECK(setup_env(&arena, &env) == ERR_OK);
    if (!env)
        return;

    SapTxnCtx *w1 = sap_txn_begin(env, NULL, 0);
    CHECK(w1 != NULL);
    SapTxnCtx *w2 = sap_txn_begin(env, NULL, 0);
    CHECK(w2 == NULL);

    SapTxnCtx *r1 = sap_txn_begin(env, NULL, TXN_RDONLY);
    CHECK(r1 != NULL);

    SapTxnCtx *child = sap_txn_begin(env, w1, 0);
    CHECK(child != NULL);
    if (child)
        CHECK(sap_txn_commit(child) == ERR_OK);

    SapTxnCtx *bad_child = sap_txn_begin(env, r1, 0);
    CHECK(bad_child == NULL);

    if (r1)
        CHECK(sap_txn_commit(r1) == ERR_OK);
    if (w1)
        CHECK(sap_txn_commit(w1) == ERR_OK);

    SapTxnCtx *w3 = sap_txn_begin(env, NULL, 0);
    CHECK(w3 != NULL);
    if (w3)
        CHECK(sap_txn_commit(w3) == ERR_OK);

    teardown_env(arena, env);
}

static void test_parent_env_mismatch_rejected(void)
{
    SECTION("parent env mismatch rejected");
    SapMemArena *arena_a = NULL;
    SapMemArena *arena_b = NULL;
    SapEnv *env_a = NULL;
    SapEnv *env_b = NULL;

    CHECK(setup_env(&arena_a, &env_a) == ERR_OK);
    CHECK(setup_env(&arena_b, &env_b) == ERR_OK);
    if (!env_a || !env_b)
    {
        teardown_env(arena_a, env_a);
        teardown_env(arena_b, env_b);
        return;
    }

    SapTxnCtx *parent = sap_txn_begin(env_a, NULL, 0);
    CHECK(parent != NULL);

    SapTxnCtx *mismatch = sap_txn_begin(env_b, parent, 0);
    CHECK(mismatch == NULL);

    if (parent)
        CHECK(sap_txn_commit(parent) == ERR_OK);

    teardown_env(arena_a, env_a);
    teardown_env(arena_b, env_b);
}

int main(void)
{
    test_top_level_write_exclusive();
    test_parent_env_mismatch_rejected();
    printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
