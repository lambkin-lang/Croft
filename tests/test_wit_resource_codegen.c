#include "generated/wit_common_core.h"

#include "sapling/arena.h"
#include "sapling/thatch.h"
#include "sapling/txn.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void wit_codegen_setup(SapMemArena** arena_out,
                              SapEnv** env_out,
                              SapTxnCtx** txn_out,
                              ThatchRegion** region_out)
{
    SapArenaOptions opts;

    memset(&opts, 0, sizeof(opts));
    opts.type = SAP_ARENA_BACKING_MALLOC;
    opts.page_size = SAPLING_PAGE_SIZE;
    opts.cfg.mmap.max_size = 1024u * 1024u;

    *arena_out = NULL;
    *env_out = NULL;
    *txn_out = NULL;
    *region_out = NULL;

    if (sap_arena_init(arena_out, &opts) != ERR_OK) {
        return;
    }
    *env_out = sap_env_create(*arena_out, SAPLING_PAGE_SIZE);
    if (!*env_out) {
        return;
    }
    if (sap_thatch_subsystem_init(*env_out) != ERR_OK) {
        return;
    }
    *txn_out = sap_txn_begin(*env_out, NULL, 0u);
    if (!*txn_out) {
        return;
    }
    (void)thatch_region_new(*txn_out, region_out);
}

static void wit_codegen_teardown(SapMemArena* arena, SapEnv* env, SapTxnCtx* txn)
{
    if (txn) {
        sap_txn_abort(txn);
    }
    if (env) {
        sap_env_destroy(env);
    }
    if (arena) {
        sap_arena_destroy(arena);
    }
}

int test_wit_resource_open_command_roundtrip(void)
{
    SapMemArena* arena = NULL;
    SapEnv* env = NULL;
    SapTxnCtx* txn = NULL;
    ThatchRegion* region = NULL;
    SapWitTextCommand in;
    SapWitTextCommand out;
    ThatchCursor cursor = 0u;

    wit_codegen_setup(&arena, &env, &txn, &region);
    if (!arena || !env || !txn || !region) {
        wit_codegen_teardown(arena, env, txn);
        return 1;
    }

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.case_tag = SAP_WIT_TEXT_COMMAND_OPEN;
    in.val.open.initial_data = (const uint8_t*)"small binaries";
    in.val.open.initial_len = 14u;

    if (sap_wit_write_text_command(region, &in) != ERR_OK) {
        wit_codegen_teardown(arena, env, txn);
        return 1;
    }
    if (sap_wit_read_text_command(region, &cursor, &out) != ERR_OK) {
        wit_codegen_teardown(arena, env, txn);
        return 1;
    }
    if (cursor != thatch_region_used(region)) {
        wit_codegen_teardown(arena, env, txn);
        return 1;
    }
    if (out.case_tag != SAP_WIT_TEXT_COMMAND_OPEN
            || out.val.open.initial_len != 14u
            || memcmp(out.val.open.initial_data, "small binaries", 14u) != 0) {
        wit_codegen_teardown(arena, env, txn);
        return 1;
    }

    wit_codegen_teardown(arena, env, txn);
    return 0;
}

int test_wit_resource_handle_roundtrip(void)
{
    SapMemArena* arena = NULL;
    SapEnv* env = NULL;
    SapTxnCtx* txn = NULL;
    ThatchRegion* region = NULL;
    SapWitTextCommand in;
    SapWitTextCommand out;
    ThatchCursor cursor = 0u;

    wit_codegen_setup(&arena, &env, &txn, &region);
    if (!arena || !env || !txn || !region) {
        wit_codegen_teardown(arena, env, txn);
        return 1;
    }

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.case_tag = SAP_WIT_TEXT_COMMAND_CLONE;
    in.val.clone.source = 37u;

    if (sap_wit_write_text_command(region, &in) != ERR_OK) {
        wit_codegen_teardown(arena, env, txn);
        return 1;
    }
    if (sap_wit_read_text_command(region, &cursor, &out) != ERR_OK) {
        wit_codegen_teardown(arena, env, txn);
        return 1;
    }
    if (cursor != thatch_region_used(region)) {
        wit_codegen_teardown(arena, env, txn);
        return 1;
    }
    if (out.case_tag != SAP_WIT_TEXT_COMMAND_CLONE || out.val.clone.source != 37u) {
        wit_codegen_teardown(arena, env, txn);
        return 1;
    }

    wit_codegen_teardown(arena, env, txn);
    return 0;
}
