#include "sapling/arena.h"
#include "sapling/err.h"
#include "sapling/seq.h"
#include "sapling/text.h"
#include "sapling/txn.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int insert_ascii_prefix(SapTxnCtx* txn, Text* text, const char* prefix) {
    size_t i;
    size_t len = strlen(prefix);
    for (i = 0; i < len; ++i) {
        int rc = text_insert(txn, text, i, (uint32_t)(unsigned char)prefix[i]);
        if (rc != ERR_OK) {
            return rc;
        }
    }
    return ERR_OK;
}

static int text_to_owned_utf8(const Text* text, char** out) {
    size_t utf8_len = 0;
    uint8_t* buffer = NULL;
    int rc = text_utf8_length(text, &utf8_len);
    if (rc != ERR_OK) {
        return rc;
    }

    buffer = (uint8_t*)malloc(utf8_len + 1);
    if (!buffer) {
        return ERR_OOM;
    }

    rc = text_to_utf8(text, buffer, utf8_len + 1, &utf8_len);
    if (rc != ERR_OK) {
        free(buffer);
        return rc;
    }

    buffer[utf8_len] = '\0';
    *out = (char*)buffer;
    return ERR_OK;
}

int main(void) {
    SapMemArena* arena = NULL;
    SapEnv* env = NULL;
    SapArenaOptions opts;
    SapTxnCtx* txn = NULL;
    Text* base = NULL;
    Text* edited = NULL;
    char* base_utf8 = NULL;
    char* edited_utf8 = NULL;
    int rc;

    memset(&opts, 0, sizeof(opts));
    opts.type = SAP_ARENA_BACKING_MALLOC;
    opts.page_size = 4096;

    rc = sap_arena_init(&arena, &opts);
    if (rc != ERR_OK) {
        fprintf(stderr, "example_sapling_text: sap_arena_init failed (%d)\n", rc);
        return 1;
    }

    env = sap_env_create(arena, 4096);
    if (!env) {
        fprintf(stderr, "example_sapling_text: sap_env_create failed\n");
        sap_arena_destroy(arena);
        return 1;
    }

    rc = sap_seq_subsystem_init(env);
    if (rc != ERR_OK) {
        fprintf(stderr, "example_sapling_text: sap_seq_subsystem_init failed (%d)\n", rc);
        sap_env_destroy(env);
        sap_arena_destroy(arena);
        return 1;
    }

    base = text_new(env);
    if (!base) {
        fprintf(stderr, "example_sapling_text: text_new failed\n");
        sap_env_destroy(env);
        sap_arena_destroy(arena);
        return 1;
    }

    txn = sap_txn_begin(env, NULL, 0);
    if (!txn) {
        fprintf(stderr, "example_sapling_text: sap_txn_begin failed\n");
        text_free(env, base);
        sap_env_destroy(env);
        sap_arena_destroy(arena);
        return 1;
    }

    rc = text_from_utf8(txn, base, (const uint8_t*)"small binaries", 14);
    if (rc != ERR_OK) {
        fprintf(stderr, "example_sapling_text: text_from_utf8 failed (%d)\n", rc);
        sap_txn_abort(txn);
        text_free(env, base);
        sap_env_destroy(env);
        sap_arena_destroy(arena);
        return 1;
    }

    rc = sap_txn_commit(txn);
    if (rc != ERR_OK) {
        fprintf(stderr, "example_sapling_text: sap_txn_commit failed (%d)\n", rc);
        text_free(env, base);
        sap_env_destroy(env);
        sap_arena_destroy(arena);
        return 1;
    }

    edited = text_clone(env, base);
    if (!edited) {
        fprintf(stderr, "example_sapling_text: text_clone failed\n");
        text_free(env, base);
        sap_env_destroy(env);
        sap_arena_destroy(arena);
        return 1;
    }

    txn = sap_txn_begin(env, NULL, 0);
    if (!txn) {
        fprintf(stderr, "example_sapling_text: sap_txn_begin for clone failed\n");
        text_free(env, edited);
        text_free(env, base);
        sap_env_destroy(env);
        sap_arena_destroy(arena);
        return 1;
    }

    rc = insert_ascii_prefix(txn, edited, "Big analysis, ");
    if (rc != ERR_OK) {
        fprintf(stderr, "example_sapling_text: insert_ascii_prefix failed (%d)\n", rc);
        sap_txn_abort(txn);
        text_free(env, edited);
        text_free(env, base);
        sap_env_destroy(env);
        sap_arena_destroy(arena);
        return 1;
    }

    rc = sap_txn_commit(txn);
    if (rc != ERR_OK) {
        fprintf(stderr, "example_sapling_text: sap_txn_commit for clone failed (%d)\n", rc);
        text_free(env, edited);
        text_free(env, base);
        sap_env_destroy(env);
        sap_arena_destroy(arena);
        return 1;
    }

    rc = text_to_owned_utf8(base, &base_utf8);
    if (rc != ERR_OK) {
        fprintf(stderr, "example_sapling_text: text_to_owned_utf8(base) failed (%d)\n", rc);
        text_free(env, edited);
        text_free(env, base);
        sap_env_destroy(env);
        sap_arena_destroy(arena);
        return 1;
    }

    rc = text_to_owned_utf8(edited, &edited_utf8);
    if (rc != ERR_OK) {
        fprintf(stderr, "example_sapling_text: text_to_owned_utf8(edited) failed (%d)\n", rc);
        free(base_utf8);
        text_free(env, edited);
        text_free(env, base);
        sap_env_destroy(env);
        sap_arena_destroy(arena);
        return 1;
    }

    printf("base=\"%s\"\n", base_utf8);
    printf("edited=\"%s\"\n", edited_utf8);

    free(edited_utf8);
    free(base_utf8);
    text_free(env, edited);
    text_free(env, base);
    sap_env_destroy(env);
    sap_arena_destroy(arena);
    return 0;
}
