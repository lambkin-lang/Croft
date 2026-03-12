#ifndef CROFT_XPI_REGISTRY_H
#define CROFT_XPI_REGISTRY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *build_system;
    const char *current_machine_traits;
} CroftXpiBuildContext;

typedef struct {
    const char *name;
    const char *kind;
    const char *profile;
    const char *upstream_package;
    const char *upstream_version;
    const char *support_status;
    const char *applicability;
    const char *applicability_traits;
    const char *capability_bundles;
    const char *shared_substrates;
    const char *helper_interfaces;
    const char *declared_worlds;
    const char *expanded_surfaces;
    const char *requires_bundles;
    const char *selected_slot_bindings;
    const char *open_slots;
    const char *tags;
    const char *depends;
} CroftXpiArtifactDescriptor;

typedef struct {
    const char *name;
    const char *kind;
    const char *applicability;
    const char *applicability_traits;
    const char *description;
    const char *artifacts;
    const char *interfaces;
} CroftXpiSubstrateDescriptor;

typedef struct {
    const char *name;
    const char *mode;
    const char *applicability;
    const char *applicability_traits;
    const char *description;
    const char *bundles;
} CroftXpiSlotDescriptor;

typedef struct {
    const char *name;
    const char *support_status;
    const char *applicability;
    const char *applicability_traits;
    const char *description;
    const char *artifacts;
    const char *substrates;
    const char *declared_worlds;
    const char *expanded_surfaces;
    const char *helper_interfaces;
    const char *requires_bundles;
    const char *compatible_bundles;
    const char *conflicts_with;
    const char *roles;
    const char *slots;
} CroftXpiBundleDescriptor;

typedef struct {
    const char *name;
    const char *kind;
    const char *focus;
    const char *applicability;
    const char *applicability_traits;
    const char *tags;
    const char *requires;
    const char *requires_bundles;
    const char *selected_slot_bindings;
    const char *open_slots;
} CroftXpiEntrypointDescriptor;

typedef struct {
    const CroftXpiBuildContext *context;
    const CroftXpiArtifactDescriptor *artifacts;
    uint32_t artifact_count;
    const CroftXpiSubstrateDescriptor *substrates;
    uint32_t substrate_count;
    const CroftXpiSlotDescriptor *slots;
    uint32_t slot_count;
    const CroftXpiBundleDescriptor *bundles;
    uint32_t bundle_count;
    const CroftXpiEntrypointDescriptor *entrypoints;
    uint32_t entrypoint_count;
} CroftXpiRegistry;

typedef struct {
    const char *cursor;
} CroftXpiListCursor;

const CroftXpiRegistry *croft_xpi_registry_current(void);

void croft_xpi_list_cursor_init(CroftXpiListCursor *cursor, const char *items);
int croft_xpi_list_cursor_next(CroftXpiListCursor *cursor,
                               const char **item_out,
                               size_t *item_len_out);
int croft_xpi_list_contains(const char *items, const char *needle);
int croft_xpi_binding_lookup(const char *bindings,
                             const char *slot_name,
                             const char **bundle_out,
                             size_t *bundle_len_out);

const CroftXpiArtifactDescriptor *croft_xpi_find_artifact(const CroftXpiRegistry *registry,
                                                          const char *name);
const CroftXpiSubstrateDescriptor *croft_xpi_find_substrate(const CroftXpiRegistry *registry,
                                                            const char *name);
const CroftXpiSlotDescriptor *croft_xpi_find_slot(const CroftXpiRegistry *registry,
                                                  const char *name);
const CroftXpiBundleDescriptor *croft_xpi_find_bundle(const CroftXpiRegistry *registry,
                                                      const char *name);
const CroftXpiEntrypointDescriptor *croft_xpi_find_entrypoint(const CroftXpiRegistry *registry,
                                                              const char *name);

int croft_xpi_applicability_matches(const char *required_traits,
                                    const char *actual_traits);
int croft_xpi_applicability_spec_matches(const char *applicability,
                                         const char *actual_traits);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_XPI_REGISTRY_H */
