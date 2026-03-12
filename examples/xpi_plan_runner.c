#include "croft/xpi_demo_runtime.h"
#include "sapling/arena.h"
#include "sapling/err.h"
#include "sapling/thatch.h"
#include "sapling/thatch_json.h"
#include "sapling/txn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CROFT_DEFAULT_XPI_DEMO_MANIFEST_PATH
static const char *k_default_manifest_path = CROFT_DEFAULT_XPI_DEMO_MANIFEST_PATH;
#else
static const char *k_default_manifest_path = NULL;
#endif

#ifdef CROFT_DEFAULT_XPI_DEMO_PLAN_PATH
static const char *k_default_plan_path = CROFT_DEFAULT_XPI_DEMO_PLAN_PATH;
#else
static const char *k_default_plan_path = NULL;
#endif

static void print_usage(const char *argv0)
{
    fprintf(stderr, "usage: %s <manifest.json> <solution.json>\n", argv0 ? argv0 : "xpi_plan_runner");
    if (k_default_manifest_path && k_default_plan_path) {
        fprintf(stderr, "       %s            # uses generated demo defaults\n", argv0 ? argv0 : "xpi_plan_runner");
    }
}

static void print_string_list(const char *label, const CroftXpiDemoStringList *list)
{
    uint32_t i;

    if (!list || list->count == 0u) {
        return;
    }
    printf("%s:\n", label);
    for (i = 0u; i < list->count; i++) {
        printf("  - %s\n", list->items[i]);
    }
}

static void print_slot_bindings(const CroftXpiDemoSlotBindingList *bindings)
{
    uint32_t i;

    if (!bindings || bindings->count == 0u) {
        return;
    }
    printf("selected slots:\n");
    for (i = 0u; i < bindings->count; i++) {
        printf("  - %s -> %s\n", bindings->items[i].slot, bindings->items[i].bundle);
    }
}

int main(int argc, char **argv)
{
    const char *manifest_path = NULL;
    const char *plan_path = NULL;
    int exit_code = 1;
    SapMemArena *arena = NULL;
    SapEnv *env = NULL;
    SapTxnCtx *txn = NULL;
    ThatchRegion *region = NULL;
    ThatchVal root = {0};
    CroftXpiDemoManifest manifest;
    CroftXpiDemoPlan plan;
    CroftXpiDemoPayload payload;
    uint32_t err_pos = 0u;
    char rendered[4096];
    SapArenaOptions opts = {
        .type = SAP_ARENA_BACKING_MALLOC,
        .cfg.mmap.max_size = 1024u * 1024u,
    };

    memset(&manifest, 0, sizeof(manifest));
    memset(&plan, 0, sizeof(plan));
    memset(&payload, 0, sizeof(payload));

    if (argc == 1) {
        manifest_path = k_default_manifest_path;
        plan_path = k_default_plan_path;
    } else if (argc == 3) {
        manifest_path = argv[1];
        plan_path = argv[2];
    } else {
        print_usage(argv[0]);
        return 2;
    }

    if (!manifest_path || !plan_path) {
        fprintf(stderr, "missing manifest/plan paths and no generated defaults are available\n");
        print_usage(argv[0]);
        return 2;
    }

    sap_arena_init(&arena, &opts);
    if (!arena) {
        fprintf(stderr, "unable to create arena\n");
        goto cleanup;
    }
    env = sap_env_create(arena, SAPLING_PAGE_SIZE);
    if (!env || sap_thatch_subsystem_init(env) != ERR_OK) {
        fprintf(stderr, "unable to initialize Sapling/Thatch environment\n");
        goto cleanup;
    }
    txn = sap_txn_begin(env, NULL, 0u);
    if (!txn) {
        fprintf(stderr, "unable to create transaction context\n");
        goto cleanup;
    }

    if (croft_xpi_demo_manifest_load(txn, manifest_path, &manifest) != ERR_OK) {
        fprintf(stderr, "unable to load manifest: %s\n", manifest_path);
        goto cleanup;
    }
    if (croft_xpi_demo_plan_load(txn, plan_path, &plan) != ERR_OK) {
        fprintf(stderr, "unable to load solved plan: %s\n", plan_path);
        goto cleanup;
    }
    if (croft_xpi_demo_validate(&manifest, &plan) != ERR_OK) {
        fprintf(stderr, "manifest/plan mismatch: %s vs %s\n", manifest_path, plan_path);
        goto cleanup;
    }
    if (croft_xpi_demo_execute_payload(&plan, &payload) != ERR_OK) {
        fprintf(stderr, "unable to execute guest payload from %s\n", plan.wasm_path);
        goto cleanup;
    }

    if (strcmp(payload.data_contract, "host-json-to-thatch-v0") != 0
        || strcmp(payload.view_contract, "collapsed-json-text-v0") != 0) {
        fprintf(stderr,
                "unsupported contract pair data=%s view=%s\n",
                payload.data_contract,
                payload.view_contract);
        goto cleanup;
    }
    if (croft_xpi_demo_parse_json_payload(txn, &payload, &region, &root, &err_pos) != ERR_OK) {
        fprintf(stderr, "unable to parse guest JSON payload (err_pos=%u)\n", err_pos);
        goto cleanup;
    }
    if (croft_xpi_demo_render_collapsed_json_view(root,
                                                  &plan.expanded_paths,
                                                  rendered,
                                                  sizeof(rendered))
        != ERR_OK) {
        fprintf(stderr, "unable to render collapsed JSON text view\n");
        goto cleanup;
    }

    printf("manifest: %s\n", manifest.name);
    printf("family: %s\n", plan.family);
    printf("guest: %s (%s)\n", plan.guest_target, plan.wasm_path);
    print_slot_bindings(&plan.selected_slots);
    print_string_list("required bundles", &plan.requires_bundles);
    print_string_list("provider artifacts", &plan.provider_artifacts);
    print_string_list("shared substrates", &plan.shared_substrates);
    print_string_list("declared worlds", &plan.declared_worlds);
    printf("rendered view:\n%s", rendered);

    exit_code = 0;

cleanup:
    croft_xpi_demo_payload_dispose(&payload);
    if (txn) {
        sap_txn_abort(txn);
    }
    if (env) {
        sap_env_destroy(env);
    }
    if (arena) {
        sap_arena_destroy(arena);
    }
    return exit_code;
}
