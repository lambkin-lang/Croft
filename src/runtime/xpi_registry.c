#include "croft/xpi_registry.h"

#include <string.h>

static int croft_xpi_text_equals_len(const char *lhs,
                                     size_t lhs_len,
                                     const char *rhs)
{
    size_t rhs_len;

    if (!lhs || !rhs) {
        return 0;
    }
    rhs_len = strlen(rhs);
    return lhs_len == rhs_len && memcmp(lhs, rhs, lhs_len) == 0;
}

void croft_xpi_list_cursor_init(CroftXpiListCursor *cursor, const char *items)
{
    if (!cursor) {
        return;
    }
    cursor->cursor = items;
}

int croft_xpi_list_cursor_next(CroftXpiListCursor *cursor,
                               const char **item_out,
                               size_t *item_len_out)
{
    const char *start;
    const char *end;

    if (item_out) {
        *item_out = NULL;
    }
    if (item_len_out) {
        *item_len_out = 0u;
    }
    if (!cursor || !cursor->cursor || cursor->cursor[0] == '\0') {
        return 0;
    }

    start = cursor->cursor;
    end = start;
    while (*end != '\0' && *end != '|') {
        end++;
    }
    if (item_out) {
        *item_out = start;
    }
    if (item_len_out) {
        *item_len_out = (size_t)(end - start);
    }
    cursor->cursor = (*end == '|') ? (end + 1) : end;
    return 1;
}

int croft_xpi_list_contains(const char *items, const char *needle)
{
    CroftXpiListCursor cursor;
    const char *item = NULL;
    size_t item_len = 0u;

    if (!items || !needle || needle[0] == '\0') {
        return 0;
    }

    croft_xpi_list_cursor_init(&cursor, items);
    while (croft_xpi_list_cursor_next(&cursor, &item, &item_len)) {
        if (croft_xpi_text_equals_len(item, item_len, needle)) {
            return 1;
        }
    }
    return 0;
}

int croft_xpi_binding_lookup(const char *bindings,
                             const char *slot_name,
                             const char **bundle_out,
                             size_t *bundle_len_out)
{
    CroftXpiListCursor cursor;
    const char *item = NULL;
    size_t item_len = 0u;

    if (bundle_out) {
        *bundle_out = NULL;
    }
    if (bundle_len_out) {
        *bundle_len_out = 0u;
    }
    if (!bindings || !slot_name || slot_name[0] == '\0') {
        return 0;
    }

    croft_xpi_list_cursor_init(&cursor, bindings);
    while (croft_xpi_list_cursor_next(&cursor, &item, &item_len)) {
        const char *equals = memchr(item, '=', item_len);
        size_t key_len;
        size_t value_len;

        if (!equals) {
            continue;
        }
        key_len = (size_t)(equals - item);
        value_len = item_len - key_len - 1u;
        if (!croft_xpi_text_equals_len(item, key_len, slot_name)) {
            continue;
        }
        if (bundle_out) {
            *bundle_out = equals + 1;
        }
        if (bundle_len_out) {
            *bundle_len_out = value_len;
        }
        return 1;
    }
    return 0;
}

#define CROFT_XPI_FIND_BY_NAME(fn_name, type_name, field_name, count_field) \
    const type_name *fn_name(const CroftXpiRegistry *registry, const char *name) \
    { \
        uint32_t i; \
        if (!registry || !name || name[0] == '\0') { \
            return NULL; \
        } \
        for (i = 0u; i < registry->count_field; i++) { \
            if (registry->field_name[i].name && strcmp(registry->field_name[i].name, name) == 0) { \
                return &registry->field_name[i]; \
            } \
        } \
        return NULL; \
    }

CROFT_XPI_FIND_BY_NAME(croft_xpi_find_artifact, CroftXpiArtifactDescriptor, artifacts, artifact_count)
CROFT_XPI_FIND_BY_NAME(croft_xpi_find_substrate, CroftXpiSubstrateDescriptor, substrates, substrate_count)
CROFT_XPI_FIND_BY_NAME(croft_xpi_find_slot, CroftXpiSlotDescriptor, slots, slot_count)
CROFT_XPI_FIND_BY_NAME(croft_xpi_find_bundle, CroftXpiBundleDescriptor, bundles, bundle_count)
CROFT_XPI_FIND_BY_NAME(croft_xpi_find_entrypoint, CroftXpiEntrypointDescriptor, entrypoints, entrypoint_count)

#undef CROFT_XPI_FIND_BY_NAME

int croft_xpi_applicability_matches(const char *required_traits,
                                    const char *actual_traits)
{
    CroftXpiListCursor cursor;
    const char *item = NULL;
    size_t item_len = 0u;
    char trait[128];

    if (!required_traits || required_traits[0] == '\0') {
        return 1;
    }
    if (!actual_traits || actual_traits[0] == '\0') {
        return 0;
    }

    croft_xpi_list_cursor_init(&cursor, required_traits);
    while (croft_xpi_list_cursor_next(&cursor, &item, &item_len)) {
        if (item_len >= sizeof(trait)) {
            return 0;
        }
        memcpy(trait, item, item_len);
        trait[item_len] = '\0';
        if (!croft_xpi_list_contains(actual_traits, trait)) {
            return 0;
        }
    }

    return 1;
}

int croft_xpi_applicability_spec_matches(const char *applicability,
                                         const char *actual_traits)
{
    static const char k_current_machine_prefix[] = "current-machine";

    if (!applicability || applicability[0] == '\0') {
        return 1;
    }
    if (!actual_traits || actual_traits[0] == '\0') {
        return 0;
    }
    if (strcmp(applicability, "host-neutral") == 0) {
        return 1;
    }
    if (strncmp(applicability,
                k_current_machine_prefix,
                sizeof(k_current_machine_prefix) - 1u) == 0) {
        const char *cursor = applicability + (sizeof(k_current_machine_prefix) - 1u);

        if (!croft_xpi_list_contains(actual_traits, k_current_machine_prefix)) {
            return 0;
        }
        while (*cursor != '\0') {
            const char *next;
            char trait[128];
            size_t len;

            if (*cursor != '-') {
                return croft_xpi_list_contains(actual_traits, applicability);
            }
            cursor++;
            next = cursor;
            while (*next != '\0' && *next != '-') {
                next++;
            }
            len = (size_t)(next - cursor);
            if (len == 0u || len >= sizeof(trait)) {
                return 0;
            }
            memcpy(trait, cursor, len);
            trait[len] = '\0';
            if (!croft_xpi_list_contains(actual_traits, trait)) {
                return 0;
            }
            cursor = next;
        }
        return 1;
    }
    return croft_xpi_list_contains(actual_traits, applicability);
}

#include "generated/croft_xpi_registry.inc"
