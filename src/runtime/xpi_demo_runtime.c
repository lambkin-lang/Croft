#include "croft/xpi_demo_runtime.h"

#include "croft/host_wasm.h"
#include "sapling/err.h"
#include "sapling/thatch_json.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int string_is_empty(const char *text)
{
    return !text || text[0] == '\0';
}

static void copy_text_slice(char *dest, size_t cap, const char *text, uint32_t len)
{
    if (!dest || cap == 0u) {
        return;
    }
    if (!text) {
        dest[0] = '\0';
        return;
    }
    if ((size_t)len >= cap) {
        len = (uint32_t)(cap - 1u);
    }
    memcpy(dest, text, len);
    dest[len] = '\0';
}

static char *read_file(const char *path)
{
    FILE *f = NULL;
    char *buf = NULL;
    long size = 0;
    size_t nr = 0u;

    if (!path) {
        return NULL;
    }
    f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    buf = (char *)malloc((size_t)size + 1u);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    nr = fread(buf, 1u, (size_t)size, f);
    fclose(f);
    if ((long)nr != size) {
        free(buf);
        return NULL;
    }
    buf[size] = '\0';
    return buf;
}

static uint8_t *read_binary_file(const char *path, uint32_t *len_out)
{
    FILE *f = NULL;
    uint8_t *buf = NULL;
    long size = 0;
    size_t nr = 0u;

    if (len_out) {
        *len_out = 0u;
    }
    if (!path) {
        return NULL;
    }
    f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    buf = (uint8_t *)malloc((size_t)size);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    nr = fread(buf, 1u, (size_t)size, f);
    fclose(f);
    if ((long)nr != size) {
        free(buf);
        return NULL;
    }
    if (len_out) {
        *len_out = (uint32_t)size;
    }
    return buf;
}

static int json_copy_string_path_optional(ThatchVal root, const char *path, char *dest, size_t cap)
{
    ThatchVal out = {0};
    const char *text = NULL;
    uint32_t len = 0u;

    if (!dest || cap == 0u || !path) {
        return ERR_INVALID;
    }
    dest[0] = '\0';
    if (tj_path(root, path, &out) != ERR_OK) {
        return ERR_OK;
    }
    if (tj_string(out, &text, &len) != ERR_OK) {
        return ERR_PARSE;
    }
    copy_text_slice(dest, cap, text, len);
    return ERR_OK;
}

static int json_copy_first_string_path_optional(ThatchVal root,
                                                const char *const *paths,
                                                uint32_t path_count,
                                                char *dest,
                                                size_t cap)
{
    uint32_t i;

    if (!paths || !dest || cap == 0u) {
        return ERR_INVALID;
    }
    dest[0] = '\0';
    for (i = 0u; i < path_count; i++) {
        ThatchVal out = {0};
        const char *text = NULL;
        uint32_t len = 0u;

        if (!paths[i] || tj_path(root, paths[i], &out) != ERR_OK) {
            continue;
        }
        if (tj_string(out, &text, &len) != ERR_OK) {
            return ERR_PARSE;
        }
        copy_text_slice(dest, cap, text, len);
        return ERR_OK;
    }
    return ERR_OK;
}

static int json_copy_string_key_optional(ThatchVal object,
                                         const char *key,
                                         char *dest,
                                         size_t cap)
{
    ThatchVal value = {0};
    const char *text = NULL;
    uint32_t len = 0u;

    if (!dest || cap == 0u || !key) {
        return ERR_INVALID;
    }
    dest[0] = '\0';
    if (tj_get_str(object, key, &value) != ERR_OK) {
        return ERR_OK;
    }
    if (tj_string(value, &text, &len) != ERR_OK) {
        return ERR_PARSE;
    }
    copy_text_slice(dest, cap, text, len);
    return ERR_OK;
}

static int json_copy_string_array_path_optional(ThatchVal root,
                                                const char *path,
                                                CroftXpiDemoStringList *list)
{
    ThatchVal array = {0};
    TjIter iter;
    ThatchVal item = {0};

    if (!path || !list) {
        return ERR_INVALID;
    }
    memset(list, 0, sizeof(*list));
    if (tj_path(root, path, &array) != ERR_OK) {
        return ERR_OK;
    }
    if (tj_iter_array(array, &iter) != ERR_OK) {
        return ERR_PARSE;
    }
    while (tj_iter_next(&iter, &item) == ERR_OK) {
        const char *text = NULL;
        uint32_t len = 0u;

        if (list->count >= CROFT_XPI_DEMO_MAX_LIST_ITEMS) {
            return ERR_RANGE;
        }
        if (tj_string(item, &text, &len) != ERR_OK) {
            return ERR_PARSE;
        }
        copy_text_slice(list->items[list->count], sizeof(list->items[0]), text, len);
        list->count++;
    }
    return ERR_OK;
}

static int json_copy_string_array_value(ThatchVal array, CroftXpiDemoStringList *list)
{
    TjIter iter;
    ThatchVal item = {0};

    if (!list) {
        return ERR_INVALID;
    }
    memset(list, 0, sizeof(*list));
    if (tj_iter_array(array, &iter) != ERR_OK) {
        return ERR_PARSE;
    }
    while (tj_iter_next(&iter, &item) == ERR_OK) {
        const char *text = NULL;
        uint32_t len = 0u;

        if (list->count >= CROFT_XPI_DEMO_MAX_LIST_ITEMS) {
            return ERR_RANGE;
        }
        if (tj_string(item, &text, &len) != ERR_OK) {
            return ERR_PARSE;
        }
        copy_text_slice(list->items[list->count], sizeof(list->items[0]), text, len);
        list->count++;
    }
    return ERR_OK;
}

static int json_copy_slot_preferences_optional(ThatchVal root,
                                               const char *path,
                                               CroftXpiDemoSlotPreferenceList *prefs)
{
    ThatchVal object = {0};
    TjIter iter;
    const char *key = NULL;
    uint32_t key_len = 0u;
    ThatchVal value = {0};

    if (!path || !prefs) {
        return ERR_INVALID;
    }
    memset(prefs, 0, sizeof(*prefs));
    if (tj_path(root, path, &object) != ERR_OK) {
        return ERR_OK;
    }
    if (tj_iter_object(object, &iter) != ERR_OK) {
        return ERR_PARSE;
    }
    while (tj_iter_next_kv(&iter, &key, &key_len, &value) == ERR_OK) {
        CroftXpiDemoSlotPreference *pref = NULL;

        if (prefs->count >= CROFT_XPI_DEMO_MAX_SLOT_ITEMS) {
            return ERR_RANGE;
        }
        pref = &prefs->items[prefs->count];
        copy_text_slice(pref->slot, sizeof(pref->slot), key, key_len);
        if (json_copy_string_array_value(value, &pref->bundles) != ERR_OK) {
            return ERR_PARSE;
        }
        prefs->count++;
    }
    return ERR_OK;
}

static int json_copy_slot_bindings_optional(ThatchVal root,
                                            const char *path,
                                            CroftXpiDemoSlotBindingList *bindings)
{
    ThatchVal array = {0};
    TjIter iter;
    ThatchVal item = {0};

    if (!path || !bindings) {
        return ERR_INVALID;
    }
    memset(bindings, 0, sizeof(*bindings));
    if (tj_path(root, path, &array) != ERR_OK) {
        return ERR_OK;
    }
    if (tj_iter_array(array, &iter) != ERR_OK) {
        return ERR_PARSE;
    }
    while (tj_iter_next(&iter, &item) == ERR_OK) {
        CroftXpiDemoSlotBinding *binding = NULL;
        int rc = ERR_OK;

        if (bindings->count >= CROFT_XPI_DEMO_MAX_SLOT_ITEMS) {
            return ERR_RANGE;
        }
        binding = &bindings->items[bindings->count];
        rc = json_copy_string_key_optional(item, "slot", binding->slot, sizeof(binding->slot));
        if (rc != ERR_OK || string_is_empty(binding->slot)) {
            return ERR_PARSE;
        }
        rc = json_copy_string_key_optional(item, "bundle", binding->bundle, sizeof(binding->bundle));
        if (rc != ERR_OK || string_is_empty(binding->bundle)) {
            return ERR_PARSE;
        }
        bindings->count++;
    }
    return ERR_OK;
}

int croft_xpi_demo_string_list_contains(const CroftXpiDemoStringList *list,
                                        const char *expected)
{
    uint32_t i;

    if (!list || !expected) {
        return 0;
    }
    for (i = 0u; i < list->count; i++) {
        if (strcmp(list->items[i], expected) == 0) {
            return 1;
        }
    }
    return 0;
}

const CroftXpiDemoSlotPreference *croft_xpi_demo_find_slot_preference(
    const CroftXpiDemoManifest *manifest,
    const char *slot)
{
    uint32_t i;

    if (!manifest || !slot) {
        return NULL;
    }
    for (i = 0u; i < manifest->preferred_slots.count; i++) {
        if (strcmp(manifest->preferred_slots.items[i].slot, slot) == 0) {
            return &manifest->preferred_slots.items[i];
        }
    }
    return NULL;
}

const CroftXpiDemoSlotBinding *croft_xpi_demo_find_slot_binding(const CroftXpiDemoPlan *plan,
                                                                const char *slot)
{
    uint32_t i;

    if (!plan || !slot) {
        return NULL;
    }
    for (i = 0u; i < plan->selected_slots.count; i++) {
        if (strcmp(plan->selected_slots.items[i].slot, slot) == 0) {
            return &plan->selected_slots.items[i];
        }
    }
    return NULL;
}

int croft_xpi_demo_manifest_load(SapTxnCtx *txn,
                                 const char *path,
                                 CroftXpiDemoManifest *manifest_out)
{
    static const char *const payload_pointer_paths[] = {
        ".guest.exports.payload_pointer",
        ".guest.exports.json_pointer",
    };
    static const char *const payload_length_paths[] = {
        ".guest.exports.payload_length",
        ".guest.exports.json_length",
    };
    char *raw = NULL;
    ThatchRegion *region = NULL;
    ThatchVal root = {0};
    uint32_t err_pos = 0u;
    int rc = ERR_OK;

    if (!txn || !path || !manifest_out) {
        return ERR_INVALID;
    }
    memset(manifest_out, 0, sizeof(*manifest_out));

    raw = read_file(path);
    if (!raw) {
        return ERR_NOT_FOUND;
    }
    rc = tj_parse(txn, raw, (uint32_t)strlen(raw), &region, &root, &err_pos);
    if (rc != ERR_OK) {
        free(raw);
        return rc;
    }

    rc = json_copy_string_path_optional(root, ".schema", manifest_out->schema, sizeof(manifest_out->schema));
    if (rc == ERR_OK) rc = json_copy_string_path_optional(root, ".name", manifest_out->name, sizeof(manifest_out->name));
    if (rc == ERR_OK) rc = json_copy_string_path_optional(root, ".kind", manifest_out->kind, sizeof(manifest_out->kind));
    if (rc == ERR_OK) rc = json_copy_string_path_optional(root, ".applicability", manifest_out->applicability, sizeof(manifest_out->applicability));
    if (rc == ERR_OK) rc = json_copy_string_path_optional(root, ".guest.kind", manifest_out->guest_kind, sizeof(manifest_out->guest_kind));
    if (rc == ERR_OK) rc = json_copy_string_path_optional(root, ".guest.target", manifest_out->guest_target, sizeof(manifest_out->guest_target));
    if (rc == ERR_OK) rc = json_copy_string_path_optional(root, ".guest.wasm_path", manifest_out->wasm_path, sizeof(manifest_out->wasm_path));
    if (rc == ERR_OK) rc = json_copy_string_path_optional(root, ".guest.exports.announce", manifest_out->announce_export, sizeof(manifest_out->announce_export));
    if (rc == ERR_OK) rc = json_copy_first_string_path_optional(root, payload_pointer_paths, 2u, manifest_out->payload_pointer_export, sizeof(manifest_out->payload_pointer_export));
    if (rc == ERR_OK) rc = json_copy_first_string_path_optional(root, payload_length_paths, 2u, manifest_out->payload_length_export, sizeof(manifest_out->payload_length_export));
    if (rc == ERR_OK) rc = json_copy_string_path_optional(root, ".contracts.data", manifest_out->data_contract, sizeof(manifest_out->data_contract));
    if (rc == ERR_OK) rc = json_copy_string_path_optional(root, ".contracts.view", manifest_out->view_contract, sizeof(manifest_out->view_contract));
    if (rc == ERR_OK) rc = json_copy_string_path_optional(root, ".solver_request.entrypoint_family", manifest_out->entrypoint_family, sizeof(manifest_out->entrypoint_family));
    if (rc == ERR_OK) rc = json_copy_string_array_path_optional(root, ".solver_request.require_bundles", &manifest_out->required_bundles);
    if (rc == ERR_OK) rc = json_copy_string_array_path_optional(root, ".solver_request.expanded_paths", &manifest_out->expanded_paths);
    if (rc == ERR_OK) rc = json_copy_slot_preferences_optional(root, ".solver_request.prefer_slot_bundles", &manifest_out->preferred_slots);

    free(raw);
    return rc;
}

int croft_xpi_demo_plan_load(SapTxnCtx *txn, const char *path, CroftXpiDemoPlan *plan_out)
{
    static const char *const payload_pointer_paths[] = {
        ".guest.exports.payload_pointer",
        ".guest.exports.json_pointer",
    };
    static const char *const payload_length_paths[] = {
        ".guest.exports.payload_length",
        ".guest.exports.json_length",
    };
    char *raw = NULL;
    ThatchRegion *region = NULL;
    ThatchVal root = {0};
    uint32_t err_pos = 0u;
    int rc = ERR_OK;

    if (!txn || !path || !plan_out) {
        return ERR_INVALID;
    }
    memset(plan_out, 0, sizeof(*plan_out));

    raw = read_file(path);
    if (!raw) {
        return ERR_NOT_FOUND;
    }
    rc = tj_parse(txn, raw, (uint32_t)strlen(raw), &region, &root, &err_pos);
    if (rc != ERR_OK) {
        free(raw);
        return rc;
    }

    rc = json_copy_string_path_optional(root, ".schema", plan_out->schema, sizeof(plan_out->schema));
    if (rc == ERR_OK) rc = json_copy_string_path_optional(root, ".family", plan_out->family, sizeof(plan_out->family));
    if (rc == ERR_OK) rc = json_copy_string_path_optional(root, ".manifest", plan_out->manifest_name, sizeof(plan_out->manifest_name));
    if (rc == ERR_OK) rc = json_copy_string_path_optional(root, ".applicability", plan_out->applicability, sizeof(plan_out->applicability));
    if (rc == ERR_OK) rc = json_copy_string_path_optional(root, ".guest.kind", plan_out->guest_kind, sizeof(plan_out->guest_kind));
    if (rc == ERR_OK) rc = json_copy_string_path_optional(root, ".guest.target", plan_out->guest_target, sizeof(plan_out->guest_target));
    if (rc == ERR_OK) rc = json_copy_string_path_optional(root, ".guest.wasm_path", plan_out->wasm_path, sizeof(plan_out->wasm_path));
    if (rc == ERR_OK) rc = json_copy_string_path_optional(root, ".guest.exports.announce", plan_out->announce_export, sizeof(plan_out->announce_export));
    if (rc == ERR_OK) rc = json_copy_first_string_path_optional(root, payload_pointer_paths, 2u, plan_out->payload_pointer_export, sizeof(plan_out->payload_pointer_export));
    if (rc == ERR_OK) rc = json_copy_first_string_path_optional(root, payload_length_paths, 2u, plan_out->payload_length_export, sizeof(plan_out->payload_length_export));
    if (rc == ERR_OK) rc = json_copy_string_path_optional(root, ".data_contract", plan_out->data_contract, sizeof(plan_out->data_contract));
    if (rc == ERR_OK && string_is_empty(plan_out->data_contract)) rc = json_copy_string_path_optional(root, ".contracts.data", plan_out->data_contract, sizeof(plan_out->data_contract));
    if (rc == ERR_OK) rc = json_copy_string_path_optional(root, ".view_contract", plan_out->view_contract, sizeof(plan_out->view_contract));
    if (rc == ERR_OK && string_is_empty(plan_out->view_contract)) rc = json_copy_string_path_optional(root, ".contracts.view", plan_out->view_contract, sizeof(plan_out->view_contract));
    if (rc == ERR_OK) rc = json_copy_string_array_path_optional(root, ".applicability_traits", &plan_out->applicability_traits);
    if (rc == ERR_OK) rc = json_copy_string_array_path_optional(root, ".requires_bundles", &plan_out->requires_bundles);
    if (rc == ERR_OK) rc = json_copy_string_array_path_optional(root, ".provider_artifacts", &plan_out->provider_artifacts);
    if (rc == ERR_OK) rc = json_copy_string_array_path_optional(root, ".shared_substrates", &plan_out->shared_substrates);
    if (rc == ERR_OK) rc = json_copy_string_array_path_optional(root, ".helper_interfaces", &plan_out->helper_interfaces);
    if (rc == ERR_OK) rc = json_copy_string_array_path_optional(root, ".declared_worlds", &plan_out->declared_worlds);
    if (rc == ERR_OK) rc = json_copy_string_array_path_optional(root, ".expanded_surfaces", &plan_out->expanded_surfaces);
    if (rc == ERR_OK) rc = json_copy_string_array_path_optional(root, ".expanded_paths", &plan_out->expanded_paths);
    if (rc == ERR_OK) rc = json_copy_string_array_path_optional(root, ".unresolved_slots", &plan_out->unresolved_slots);
    if (rc == ERR_OK) rc = json_copy_string_array_path_optional(root, ".rationale", &plan_out->rationale);
    if (rc == ERR_OK) rc = json_copy_slot_bindings_optional(root, ".selected_slot_bindings", &plan_out->selected_slots);

    free(raw);
    return rc;
}

static int string_fields_match(const char *lhs, const char *rhs)
{
    if (string_is_empty(lhs) || string_is_empty(rhs)) {
        return 1;
    }
    return strcmp(lhs, rhs) == 0;
}

static int string_list_is_subset(const CroftXpiDemoStringList *required,
                                 const CroftXpiDemoStringList *provided)
{
    uint32_t i;

    if (!required || !provided) {
        return 0;
    }
    if (required->count == 0u) {
        return 1;
    }
    if (provided->count == 0u || required->count > provided->count) {
        return 0;
    }
    for (i = 0u; i < required->count; i++) {
        if (!croft_xpi_demo_string_list_contains(provided, required->items[i])) {
            return 0;
        }
    }
    return 1;
}

int croft_xpi_demo_validate(const CroftXpiDemoManifest *manifest,
                            const CroftXpiDemoPlan *plan)
{
    if (!manifest || !plan) {
        return ERR_INVALID;
    }
    if (!string_fields_match(manifest->schema, plan->schema)
        || !string_fields_match(manifest->entrypoint_family, plan->family)
        || !string_fields_match(manifest->name, plan->manifest_name)
        || !string_fields_match(manifest->applicability, plan->applicability)
        || !string_fields_match(manifest->guest_kind, plan->guest_kind)
        || !string_fields_match(manifest->guest_target, plan->guest_target)
        || !string_fields_match(manifest->wasm_path, plan->wasm_path)
        || !string_fields_match(manifest->announce_export, plan->announce_export)
        || !string_fields_match(manifest->payload_pointer_export, plan->payload_pointer_export)
        || !string_fields_match(manifest->payload_length_export, plan->payload_length_export)
        || !string_fields_match(manifest->data_contract, plan->data_contract)
        || !string_fields_match(manifest->view_contract, plan->view_contract)
        || !string_list_is_subset(&manifest->expanded_paths, &plan->expanded_paths)) {
        return ERR_INVALID;
    }
    return ERR_OK;
}

int croft_xpi_demo_execute_payload(const CroftXpiDemoPlan *plan,
                                   CroftXpiDemoPayload *payload_out)
{
    uint8_t *wasm_bytes = NULL;
    uint32_t wasm_len = 0u;
    host_wasm_ctx_t *ctx = NULL;
    uint8_t *memory = NULL;
    uint8_t *copied = NULL;
    uint32_t memory_len = 0u;
    int32_t payload_ptr = 0;
    int32_t payload_len = 0;
    int32_t rc = ERR_OK;

    if (!plan || !payload_out) {
        return ERR_INVALID;
    }
    memset(payload_out, 0, sizeof(*payload_out));
    if (string_is_empty(plan->wasm_path)
        || string_is_empty(plan->payload_pointer_export)
        || string_is_empty(plan->payload_length_export)) {
        return ERR_INVALID;
    }

    wasm_bytes = read_binary_file(plan->wasm_path, &wasm_len);
    if (!wasm_bytes) {
        return ERR_NOT_FOUND;
    }
    ctx = host_wasm_create(wasm_bytes, wasm_len, 64u * 1024u);
    if (!ctx) {
        free(wasm_bytes);
        return ERR_INVALID;
    }
    if (!string_is_empty(plan->announce_export)) {
        rc = host_wasm_call(ctx, plan->announce_export, 0, NULL);
        if (rc != 0) {
            host_wasm_destroy(ctx);
            free(wasm_bytes);
            return ERR_INVALID;
        }
    }

    payload_ptr = host_wasm_call(ctx, plan->payload_pointer_export, 0, NULL);
    payload_len = host_wasm_call(ctx, plan->payload_length_export, 0, NULL);
    if (payload_ptr <= 0 || payload_len <= 0) {
        host_wasm_destroy(ctx);
        free(wasm_bytes);
        return ERR_INVALID;
    }

    memory = host_wasm_get_memory(ctx, &memory_len);
    if (!memory || (uint32_t)payload_ptr + (uint32_t)payload_len > memory_len) {
        host_wasm_destroy(ctx);
        free(wasm_bytes);
        return ERR_RANGE;
    }

    copied = (uint8_t *)malloc((uint32_t)payload_len);
    if (!copied) {
        host_wasm_destroy(ctx);
        free(wasm_bytes);
        return ERR_OOM;
    }
    memcpy(copied, memory + (uint32_t)payload_ptr, (uint32_t)payload_len);

    payload_out->bytes = copied;
    payload_out->len = (uint32_t)payload_len;
    copy_text_slice(payload_out->data_contract,
                    sizeof(payload_out->data_contract),
                    plan->data_contract,
                    (uint32_t)strlen(plan->data_contract));
    copy_text_slice(payload_out->view_contract,
                    sizeof(payload_out->view_contract),
                    plan->view_contract,
                    (uint32_t)strlen(plan->view_contract));

    host_wasm_destroy(ctx);
    free(wasm_bytes);
    return ERR_OK;
}

void croft_xpi_demo_payload_dispose(CroftXpiDemoPayload *payload)
{
    if (!payload) {
        return;
    }
    free(payload->bytes);
    memset(payload, 0, sizeof(*payload));
}

int croft_xpi_demo_parse_json_payload(SapTxnCtx *txn,
                                      const CroftXpiDemoPayload *payload,
                                      ThatchRegion **region_out,
                                      ThatchVal *root_out,
                                      uint32_t *err_pos_out)
{
    if (err_pos_out) {
        *err_pos_out = 0u;
    }
    if (!txn || !payload || !payload->bytes || payload->len == 0u || !region_out || !root_out) {
        return ERR_INVALID;
    }
    return tj_parse(txn,
                    (const char *)payload->bytes,
                    payload->len,
                    region_out,
                    root_out,
                    err_pos_out);
}

static int path_is_expanded(const CroftXpiDemoStringList *expanded_paths, const char *path)
{
    return croft_xpi_demo_string_list_contains(expanded_paths, path);
}

static int append_text(char *buf, size_t cap, size_t *used, const char *fmt, ...)
{
    va_list args;
    int wrote;

    if (!buf || !used || *used >= cap) {
        return ERR_RANGE;
    }
    va_start(args, fmt);
    wrote = vsnprintf(buf + *used, cap - *used, fmt, args);
    va_end(args);
    if (wrote < 0) {
        return ERR_INVALID;
    }
    if ((size_t)wrote >= cap - *used) {
        *used = cap;
        return ERR_RANGE;
    }
    *used += (size_t)wrote;
    return ERR_OK;
}

static int render_value_summary(ThatchVal value,
                                const CroftXpiDemoStringList *expanded_paths,
                                const char *path,
                                uint32_t indent,
                                char *buf,
                                size_t cap,
                                size_t *used);

static int render_container_children(ThatchVal value,
                                     const CroftXpiDemoStringList *expanded_paths,
                                     const char *path,
                                     uint32_t indent,
                                     char *buf,
                                     size_t cap,
                                     size_t *used)
{
    TjIter iter;
    const char *key = NULL;
    uint32_t key_len = 0u;
    ThatchVal child = {0};

    if (tj_iter_object(value, &iter) != ERR_OK) {
        return ERR_PARSE;
    }
    while (tj_iter_next_kv(&iter, &key, &key_len, &child) == ERR_OK) {
        char child_path[256];

        if (key_len + strlen(path) + 2u >= sizeof(child_path)) {
            return ERR_RANGE;
        }
        if (path[0] == '\0') {
            snprintf(child_path, sizeof(child_path), ".%.*s", (int)key_len, key);
        } else {
            snprintf(child_path, sizeof(child_path), "%s.%.*s", path, (int)key_len, key);
        }
        if (append_text(buf, cap, used, "%*s%.*s: ", (int)indent, "", (int)key_len, key) != ERR_OK) {
            return ERR_RANGE;
        }
        if (render_value_summary(child, expanded_paths, child_path, indent + 2u, buf, cap, used) != ERR_OK) {
            return ERR_RANGE;
        }
    }
    return ERR_OK;
}

static int render_value_summary(ThatchVal value,
                                const CroftXpiDemoStringList *expanded_paths,
                                const char *path,
                                uint32_t indent,
                                char *buf,
                                size_t cap,
                                size_t *used)
{
    TjType type = tj_type(value);
    uint32_t len = 0u;
    int64_t iv = 0;
    int b = 0;
    const char *text = NULL;
    uint32_t text_len = 0u;

    switch (type) {
        case TJ_TYPE_NULL:
            return append_text(buf, cap, used, "null\n");
        case TJ_TYPE_TRUE:
        case TJ_TYPE_FALSE:
            if (tj_bool(value, &b) != ERR_OK) {
                return ERR_PARSE;
            }
            return append_text(buf, cap, used, "%s\n", b ? "true" : "false");
        case TJ_TYPE_INT:
            if (tj_int(value, &iv) != ERR_OK) {
                return ERR_PARSE;
            }
            return append_text(buf, cap, used, "%lld\n", (long long)iv);
        case TJ_TYPE_STRING:
            if (tj_string(value, &text, &text_len) != ERR_OK) {
                return ERR_PARSE;
            }
            return append_text(buf, cap, used, "\"%.*s\"\n", (int)text_len, text);
        case TJ_TYPE_ARRAY:
            if (tj_length(value, &len) != ERR_OK) {
                return ERR_PARSE;
            }
            return append_text(buf, cap, used, "[%u items]\n", len);
        case TJ_TYPE_OBJECT:
            if (tj_length(value, &len) != ERR_OK) {
                return ERR_PARSE;
            }
            if (append_text(buf, cap, used, "{%u keys}\n", len) != ERR_OK) {
                return ERR_RANGE;
            }
            if (path_is_expanded(expanded_paths, path)) {
                return render_container_children(value, expanded_paths, path, indent, buf, cap, used);
            }
            return ERR_OK;
        default:
            return append_text(buf, cap, used, "<invalid>\n");
    }
}

int croft_xpi_demo_render_collapsed_json_view(ThatchVal root,
                                              const CroftXpiDemoStringList *expanded_paths,
                                              char *buf,
                                              size_t cap)
{
    size_t used = 0u;

    if (!buf || cap == 0u || !tj_is_object(root)) {
        return ERR_INVALID;
    }
    buf[0] = '\0';
    return render_container_children(root, expanded_paths, "", 0u, buf, cap, &used);
}
