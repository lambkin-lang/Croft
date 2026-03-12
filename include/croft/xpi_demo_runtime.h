#ifndef CROFT_XPI_DEMO_RUNTIME_H
#define CROFT_XPI_DEMO_RUNTIME_H

#include "sapling/thatch.h"
#include "sapling/thatch_json.h"
#include "sapling/txn.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CROFT_XPI_DEMO_MAX_NAME 128u
#define CROFT_XPI_DEMO_MAX_SHORT_NAME 64u
#define CROFT_XPI_DEMO_MAX_PATH 1024u
#define CROFT_XPI_DEMO_MAX_LIST_ITEMS 16u
#define CROFT_XPI_DEMO_MAX_SLOT_ITEMS 8u

typedef struct {
    char items[CROFT_XPI_DEMO_MAX_LIST_ITEMS][CROFT_XPI_DEMO_MAX_NAME];
    uint32_t count;
} CroftXpiDemoStringList;

typedef struct {
    char slot[CROFT_XPI_DEMO_MAX_NAME];
    CroftXpiDemoStringList bundles;
} CroftXpiDemoSlotPreference;

typedef struct {
    CroftXpiDemoSlotPreference items[CROFT_XPI_DEMO_MAX_SLOT_ITEMS];
    uint32_t count;
} CroftXpiDemoSlotPreferenceList;

typedef struct {
    char slot[CROFT_XPI_DEMO_MAX_NAME];
    char bundle[CROFT_XPI_DEMO_MAX_NAME];
} CroftXpiDemoSlotBinding;

typedef struct {
    CroftXpiDemoSlotBinding items[CROFT_XPI_DEMO_MAX_SLOT_ITEMS];
    uint32_t count;
} CroftXpiDemoSlotBindingList;

typedef struct {
    char schema[CROFT_XPI_DEMO_MAX_SHORT_NAME];
    char name[CROFT_XPI_DEMO_MAX_NAME];
    char kind[CROFT_XPI_DEMO_MAX_SHORT_NAME];
    char applicability[CROFT_XPI_DEMO_MAX_NAME];
    char guest_kind[CROFT_XPI_DEMO_MAX_SHORT_NAME];
    char guest_target[CROFT_XPI_DEMO_MAX_NAME];
    char wasm_path[CROFT_XPI_DEMO_MAX_PATH];
    char entrypoint_family[CROFT_XPI_DEMO_MAX_NAME];
    char announce_export[CROFT_XPI_DEMO_MAX_SHORT_NAME];
    char payload_pointer_export[CROFT_XPI_DEMO_MAX_SHORT_NAME];
    char payload_length_export[CROFT_XPI_DEMO_MAX_SHORT_NAME];
    char data_contract[CROFT_XPI_DEMO_MAX_NAME];
    char view_contract[CROFT_XPI_DEMO_MAX_NAME];
    CroftXpiDemoStringList required_bundles;
    CroftXpiDemoStringList expanded_paths;
    CroftXpiDemoSlotPreferenceList preferred_slots;
} CroftXpiDemoManifest;

typedef struct {
    char schema[CROFT_XPI_DEMO_MAX_SHORT_NAME];
    char family[CROFT_XPI_DEMO_MAX_NAME];
    char manifest_name[CROFT_XPI_DEMO_MAX_NAME];
    char applicability[CROFT_XPI_DEMO_MAX_NAME];
    char guest_kind[CROFT_XPI_DEMO_MAX_SHORT_NAME];
    char guest_target[CROFT_XPI_DEMO_MAX_NAME];
    char wasm_path[CROFT_XPI_DEMO_MAX_PATH];
    char announce_export[CROFT_XPI_DEMO_MAX_SHORT_NAME];
    char payload_pointer_export[CROFT_XPI_DEMO_MAX_SHORT_NAME];
    char payload_length_export[CROFT_XPI_DEMO_MAX_SHORT_NAME];
    char data_contract[CROFT_XPI_DEMO_MAX_NAME];
    char view_contract[CROFT_XPI_DEMO_MAX_NAME];
    CroftXpiDemoStringList applicability_traits;
    CroftXpiDemoStringList requires_bundles;
    CroftXpiDemoStringList provider_artifacts;
    CroftXpiDemoStringList shared_substrates;
    CroftXpiDemoStringList helper_interfaces;
    CroftXpiDemoStringList declared_worlds;
    CroftXpiDemoStringList expanded_surfaces;
    CroftXpiDemoStringList expanded_paths;
    CroftXpiDemoStringList unresolved_slots;
    CroftXpiDemoStringList rationale;
    CroftXpiDemoSlotBindingList selected_slots;
} CroftXpiDemoPlan;

typedef struct {
    uint8_t *bytes;
    uint32_t len;
    char data_contract[CROFT_XPI_DEMO_MAX_NAME];
    char view_contract[CROFT_XPI_DEMO_MAX_NAME];
} CroftXpiDemoPayload;

int croft_xpi_demo_manifest_load(SapTxnCtx *txn,
                                 const char *path,
                                 CroftXpiDemoManifest *manifest_out);
int croft_xpi_demo_plan_load(SapTxnCtx *txn, const char *path, CroftXpiDemoPlan *plan_out);

int croft_xpi_demo_validate(const CroftXpiDemoManifest *manifest,
                            const CroftXpiDemoPlan *plan);

int croft_xpi_demo_execute_payload(const CroftXpiDemoPlan *plan,
                                   CroftXpiDemoPayload *payload_out);
void croft_xpi_demo_payload_dispose(CroftXpiDemoPayload *payload);

int croft_xpi_demo_parse_json_payload(SapTxnCtx *txn,
                                      const CroftXpiDemoPayload *payload,
                                      ThatchRegion **region_out,
                                      ThatchVal *root_out,
                                      uint32_t *err_pos_out);

int croft_xpi_demo_render_collapsed_json_view(ThatchVal root,
                                              const CroftXpiDemoStringList *expanded_paths,
                                              char *buf,
                                              size_t cap);

int croft_xpi_demo_string_list_contains(const CroftXpiDemoStringList *list,
                                        const char *expected);

const CroftXpiDemoSlotPreference *croft_xpi_demo_find_slot_preference(
    const CroftXpiDemoManifest *manifest,
    const char *slot);
const CroftXpiDemoSlotBinding *croft_xpi_demo_find_slot_binding(const CroftXpiDemoPlan *plan,
                                                                const char *slot);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_XPI_DEMO_RUNTIME_H */
