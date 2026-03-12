#include "croft/xpi_demo_runtime.h"
#include "sapling/arena.h"
#include "sapling/err.h"
#include "sapling/thatch.h"
#include "sapling/txn.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef CROFT_WASM_JSON_DEMO_MANIFEST_PATH
#error "CROFT_WASM_JSON_DEMO_MANIFEST_PATH must be defined"
#endif

#ifndef CROFT_WASM_JSON_DEMO_PLAN_PATH
#error "CROFT_WASM_JSON_DEMO_PLAN_PATH must be defined"
#endif

#define CHECK(expr)                                                                             \
    do {                                                                                        \
        if (!(expr)) {                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                   \
            return 1;                                                                           \
        }                                                                                       \
    } while (0)

int main(void)
{
    CroftXpiDemoManifest manifest;
    CroftXpiDemoPlan plan;
    CroftXpiDemoPayload payload;
    const CroftXpiDemoSlotPreference *preferred = NULL;
    const CroftXpiDemoSlotBinding *selected = NULL;
    SapMemArena *arena = NULL;
    SapEnv *env = NULL;
    SapTxnCtx *txn = NULL;
    ThatchRegion *region = NULL;
    ThatchVal root = {0};
    uint32_t err_pos = 0u;
    char collapsed[2048];
    SapArenaOptions opts = {
        .type = SAP_ARENA_BACKING_MALLOC,
        .cfg.mmap.max_size = 1024u * 1024u,
    };

    memset(&manifest, 0, sizeof(manifest));
    memset(&plan, 0, sizeof(plan));
    memset(&payload, 0, sizeof(payload));

    sap_arena_init(&arena, &opts);
    CHECK(arena != NULL);
    env = sap_env_create(arena, SAPLING_PAGE_SIZE);
    CHECK(env != NULL);
    CHECK(sap_thatch_subsystem_init(env) == ERR_OK);
    txn = sap_txn_begin(env, NULL, 0u);
    CHECK(txn != NULL);

    CHECK(croft_xpi_demo_manifest_load(txn, CROFT_WASM_JSON_DEMO_MANIFEST_PATH, &manifest) == ERR_OK);
    CHECK(croft_xpi_demo_plan_load(txn, CROFT_WASM_JSON_DEMO_PLAN_PATH, &plan) == ERR_OK);
    CHECK(croft_xpi_demo_validate(&manifest, &plan) == ERR_OK);

    preferred = croft_xpi_demo_find_slot_preference(&manifest,
                                                    "croft-editor-shell-slot-current-machine");
    selected = croft_xpi_demo_find_slot_binding(&plan,
                                                "croft-editor-shell-slot-current-machine");

    CHECK(strcmp(manifest.schema, "croft-wasm-demo-v1") == 0);
    CHECK(strcmp(manifest.kind, "wasm-demo") == 0);
    CHECK(strcmp(manifest.entrypoint_family, "croft_json_tree_text_view_family_current_machine") == 0);
    CHECK(strcmp(manifest.data_contract, "host-json-to-thatch-v0") == 0);
    CHECK(strcmp(manifest.view_contract, "collapsed-json-text-v0") == 0);
    CHECK(strcmp(manifest.guest_kind, "wat") == 0);
    CHECK(strcmp(manifest.guest_target, "wasm_json_source_guest_gen") == 0);
    CHECK(strcmp(manifest.applicability, "current-machine-windowed") == 0);
    CHECK(manifest.required_bundles.count == 2u);
    CHECK(strcmp(manifest.required_bundles.items[0], "croft-host-fs-current-machine") == 0);
    CHECK(strcmp(manifest.required_bundles.items[1], "croft-host-file-dialog-current-machine") == 0);
    CHECK(manifest.expanded_paths.count == 1u);
    CHECK(strcmp(manifest.expanded_paths.items[0], ".features") == 0);
    CHECK(preferred != NULL);
    CHECK(preferred->bundles.count == 2u);
    CHECK(strcmp(preferred->bundles.items[0], "croft-editor-appkit-current-machine") == 0);
    CHECK(strcmp(preferred->bundles.items[1], "croft-editor-scene-metal-native-current-machine") == 0);

    CHECK(strcmp(plan.schema, manifest.schema) == 0);
    CHECK(strcmp(plan.family, manifest.entrypoint_family) == 0);
    CHECK(strcmp(plan.manifest_name, manifest.name) == 0);
    CHECK(strcmp(plan.applicability, manifest.applicability) == 0);
    CHECK(strcmp(plan.guest_kind, manifest.guest_kind) == 0);
    CHECK(strcmp(plan.guest_target, manifest.guest_target) == 0);
    CHECK(strcmp(plan.wasm_path, manifest.wasm_path) == 0);
    CHECK(strcmp(plan.announce_export, manifest.announce_export) == 0);
    CHECK(strcmp(plan.payload_pointer_export, manifest.payload_pointer_export) == 0);
    CHECK(strcmp(plan.payload_length_export, manifest.payload_length_export) == 0);
    CHECK(strcmp(plan.data_contract, manifest.data_contract) == 0);
    CHECK(strcmp(plan.view_contract, manifest.view_contract) == 0);
    CHECK(plan.expanded_paths.count == manifest.expanded_paths.count);
    CHECK(strcmp(plan.expanded_paths.items[0], manifest.expanded_paths.items[0]) == 0);
    CHECK(plan.provider_artifacts.count >= 1u);
    CHECK(croft_xpi_demo_string_list_contains(&plan.provider_artifacts, "croft_editor_appkit"));
    CHECK(croft_xpi_demo_string_list_contains(&plan.provider_artifacts, "croft_fs"));
    CHECK(selected != NULL);
    CHECK(strcmp(selected->bundle, preferred->bundles.items[0]) == 0);

    CHECK(croft_xpi_demo_execute_payload(&plan, &payload) == ERR_OK);
    CHECK(croft_xpi_demo_parse_json_payload(txn, &payload, &region, &root, &err_pos) == ERR_OK);
    CHECK(croft_xpi_demo_render_collapsed_json_view(root,
                                                    &plan.expanded_paths,
                                                    collapsed,
                                                    sizeof(collapsed))
          == ERR_OK);

    CHECK(strstr(collapsed, "project: \"Croft\"\n") != NULL);
    CHECK(strstr(collapsed, "features: {2 keys}\n") != NULL);
    CHECK(strstr(collapsed, "  solver: true\n") != NULL);
    CHECK(strstr(collapsed, "  thatch: \"json\"\n") != NULL);
    CHECK(strstr(collapsed, "items: [3 items]\n") != NULL);

    croft_xpi_demo_payload_dispose(&payload);
    sap_txn_abort(txn);
    sap_env_destroy(env);
    sap_arena_destroy(arena);
    return 0;
}
