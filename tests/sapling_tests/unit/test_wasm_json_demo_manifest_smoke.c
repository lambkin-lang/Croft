#include "croft/host_wasm.h"
#include "sapling/arena.h"
#include "sapling/thatch.h"
#include "sapling/thatch_json.h"
#include "sapling/txn.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CROFT_WASM_JSON_DEMO_MANIFEST_PATH
#error "CROFT_WASM_JSON_DEMO_MANIFEST_PATH must be defined"
#endif

#define CHECK(expr)                                                                             \
    do {                                                                                        \
        if (!(expr)) {                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                   \
            return 1;                                                                           \
        }                                                                                       \
    } while (0)

typedef struct {
    char schema[64];
    char name[128];
    char kind[64];
    char applicability[128];
    char guest_kind[64];
    char wasm_path[1024];
    char guest_target[128];
    char entrypoint_family[128];
    char announce_export[64];
    char json_pointer_export[64];
    char json_length_export[64];
    char data_contract[128];
    char view_contract[128];
    char required_bundles[8][128];
    uint32_t required_bundle_count;
    char expanded_paths[8][128];
    uint32_t expanded_path_count;
    char preferred_slot[128];
    char preferred_slot_bundles[8][128];
    uint32_t preferred_slot_bundle_count;
} JsonDemoManifest;

static char *read_file(const char *path)
{
    FILE *f = NULL;
    char *buf = NULL;
    long size = 0;
    size_t nr = 0u;

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

static int json_copy_string_path(ThatchVal root, const char *path, char *dest, size_t cap)
{
    ThatchVal out = {0};
    const char *text = NULL;
    uint32_t len = 0u;

    if (!path || !dest || cap == 0u) {
        return 1;
    }
    if (tj_path(root, path, &out) != ERR_OK) {
        return 1;
    }
    if (tj_string(out, &text, &len) != ERR_OK) {
        return 1;
    }
    copy_text_slice(dest, cap, text, len);
    return 0;
}

static int json_copy_string_array_path(ThatchVal root,
                                       const char *path,
                                       char items[][128],
                                       uint32_t max_items,
                                       uint32_t *count_out)
{
    ThatchVal array = {0};
    TjIter iter;
    ThatchVal item = {0};
    uint32_t count = 0u;
    const char *text = NULL;
    uint32_t len = 0u;

    if (!items || !count_out) {
        return 1;
    }
    *count_out = 0u;
    if (tj_path(root, path, &array) != ERR_OK) {
        return 1;
    }
    if (tj_iter_array(array, &iter) != ERR_OK) {
        return 1;
    }
    while (tj_iter_next(&iter, &item) == ERR_OK) {
        if (count >= max_items) {
            return 1;
        }
        if (tj_string(item, &text, &len) != ERR_OK) {
            return 1;
        }
        copy_text_slice(items[count], sizeof(items[0]), text, len);
        count++;
    }
    *count_out = count;
    return 0;
}

static int json_copy_string_array_key(ThatchVal object,
                                      const char *key,
                                      char items[][128],
                                      uint32_t max_items,
                                      uint32_t *count_out)
{
    ThatchVal array = {0};
    TjIter iter;
    ThatchVal item = {0};
    uint32_t count = 0u;
    const char *text = NULL;
    uint32_t len = 0u;

    if (!key || !items || !count_out) {
        return 1;
    }
    *count_out = 0u;
    if (tj_get_str(object, key, &array) != ERR_OK) {
        return 1;
    }
    if (tj_iter_array(array, &iter) != ERR_OK) {
        return 1;
    }
    while (tj_iter_next(&iter, &item) == ERR_OK) {
        if (count >= max_items) {
            return 1;
        }
        if (tj_string(item, &text, &len) != ERR_OK) {
            return 1;
        }
        copy_text_slice(items[count], sizeof(items[0]), text, len);
        count++;
    }
    *count_out = count;
    return 0;
}

static int parse_manifest(SapTxnCtx *txn, const char *path, JsonDemoManifest *manifest)
{
    char *raw = NULL;
    ThatchRegion *region = NULL;
    ThatchVal root = {0};
    ThatchVal preferred_slot_map = {0};
    uint32_t err_pos = 0u;

    if (!txn || !path || !manifest) {
        return 1;
    }
    memset(manifest, 0, sizeof(*manifest));

    raw = read_file(path);
    if (!raw) {
        return 1;
    }
    if (tj_parse(txn, raw, (uint32_t)strlen(raw), &region, &root, &err_pos) != ERR_OK) {
        free(raw);
        return 1;
    }

    if (json_copy_string_path(root, ".schema", manifest->schema, sizeof(manifest->schema)) != 0
        || json_copy_string_path(root, ".name", manifest->name, sizeof(manifest->name)) != 0
        || json_copy_string_path(root, ".kind", manifest->kind, sizeof(manifest->kind)) != 0
        || json_copy_string_path(root, ".applicability", manifest->applicability, sizeof(manifest->applicability)) != 0
        || json_copy_string_path(root, ".guest.kind", manifest->guest_kind, sizeof(manifest->guest_kind)) != 0
        || json_copy_string_path(root, ".guest.wasm_path", manifest->wasm_path, sizeof(manifest->wasm_path)) != 0
        || json_copy_string_path(root, ".guest.target", manifest->guest_target, sizeof(manifest->guest_target)) != 0
        || json_copy_string_path(root, ".guest.exports.announce", manifest->announce_export, sizeof(manifest->announce_export)) != 0
        || json_copy_string_path(root, ".guest.exports.json_pointer", manifest->json_pointer_export, sizeof(manifest->json_pointer_export)) != 0
        || json_copy_string_path(root, ".guest.exports.json_length", manifest->json_length_export, sizeof(manifest->json_length_export)) != 0
        || json_copy_string_path(root, ".contracts.data", manifest->data_contract, sizeof(manifest->data_contract)) != 0
        || json_copy_string_path(root, ".contracts.view", manifest->view_contract, sizeof(manifest->view_contract)) != 0
        || json_copy_string_path(root, ".solver_request.entrypoint_family", manifest->entrypoint_family, sizeof(manifest->entrypoint_family)) != 0
        || json_copy_string_array_path(root,
                                       ".solver_request.require_bundles",
                                       manifest->required_bundles,
                                       (uint32_t)(sizeof(manifest->required_bundles) / sizeof(manifest->required_bundles[0])),
                                       &manifest->required_bundle_count)
               != 0
        || json_copy_string_array_path(root,
                                       ".solver_request.expanded_paths",
                                       manifest->expanded_paths,
                                       (uint32_t)(sizeof(manifest->expanded_paths) / sizeof(manifest->expanded_paths[0])),
                                       &manifest->expanded_path_count)
               != 0) {
        free(raw);
        return 1;
    }

    if (tj_path(root, ".solver_request.prefer_slot_bundles", &preferred_slot_map) != ERR_OK) {
        free(raw);
        return 1;
    }
    strncpy(manifest->preferred_slot,
            "croft-editor-shell-slot-current-machine",
            sizeof(manifest->preferred_slot) - 1u);
    manifest->preferred_slot[sizeof(manifest->preferred_slot) - 1u] = '\0';
    if (json_copy_string_array_key(preferred_slot_map,
                                   manifest->preferred_slot,
                                   manifest->preferred_slot_bundles,
                                   (uint32_t)(sizeof(manifest->preferred_slot_bundles) / sizeof(manifest->preferred_slot_bundles[0])),
                                   &manifest->preferred_slot_bundle_count)
            != 0) {
        free(raw);
        return 1;
    }

    free(raw);
    return 0;
}

static int path_is_expanded(const JsonDemoManifest *manifest, const char *path)
{
    uint32_t i;

    if (!manifest || !path) {
        return 0;
    }
    for (i = 0u; i < manifest->expanded_path_count; i++) {
        if (strcmp(manifest->expanded_paths[i], path) == 0) {
            return 1;
        }
    }
    return 0;
}

static int append_text(char *buf, size_t cap, size_t *used, const char *fmt, ...)
{
    va_list args;
    int wrote;

    if (!buf || !used || *used >= cap) {
        return 1;
    }

    va_start(args, fmt);
    wrote = vsnprintf(buf + *used, cap - *used, fmt, args);
    va_end(args);
    if (wrote < 0) {
        return 1;
    }
    if ((size_t)wrote >= cap - *used) {
        *used = cap;
        return 1;
    }
    *used += (size_t)wrote;
    return 0;
}

static int render_value_summary(ThatchVal value,
                                const JsonDemoManifest *manifest,
                                const char *path,
                                uint32_t indent,
                                char *buf,
                                size_t cap,
                                size_t *used);

static int render_container_children(ThatchVal value,
                                     const JsonDemoManifest *manifest,
                                     const char *path,
                                     uint32_t indent,
                                     char *buf,
                                     size_t cap,
                                     size_t *used)
{
    TjIter iter;
    const char *key = NULL;
    uint32_t key_len = 0u;
    ThatchVal child;

    if (tj_iter_object(value, &iter) != ERR_OK) {
        return 1;
    }
    while (tj_iter_next_kv(&iter, &key, &key_len, &child) == ERR_OK) {
        char child_path[256];

        if (key_len + strlen(path) + 2u >= sizeof(child_path)) {
            return 1;
        }
        if (path[0] == '\0') {
            snprintf(child_path, sizeof(child_path), ".%.*s", (int)key_len, key);
        } else {
            snprintf(child_path, sizeof(child_path), "%s.%.*s", path, (int)key_len, key);
        }

        if (append_text(buf, cap, used, "%*s%.*s: ", (int)indent, "", (int)key_len, key) != 0) {
            return 1;
        }
        if (render_value_summary(child, manifest, child_path, indent + 2u, buf, cap, used) != 0) {
            return 1;
        }
    }
    return 0;
}

static int render_value_summary(ThatchVal value,
                                const JsonDemoManifest *manifest,
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
                return 1;
            }
            return append_text(buf, cap, used, "%s\n", b ? "true" : "false");
        case TJ_TYPE_INT:
            if (tj_int(value, &iv) != ERR_OK) {
                return 1;
            }
            return append_text(buf, cap, used, "%lld\n", (long long)iv);
        case TJ_TYPE_STRING:
            if (tj_string(value, &text, &text_len) != ERR_OK) {
                return 1;
            }
            return append_text(buf, cap, used, "\"%.*s\"\n", (int)text_len, text);
        case TJ_TYPE_ARRAY:
            if (tj_length(value, &len) != ERR_OK) {
                return 1;
            }
            return append_text(buf, cap, used, "[%u items]\n", len);
        case TJ_TYPE_OBJECT:
            if (tj_length(value, &len) != ERR_OK) {
                return 1;
            }
            if (append_text(buf, cap, used, "{%u keys}\n", len) != 0) {
                return 1;
            }
            if (path_is_expanded(manifest, path)) {
                return render_container_children(value, manifest, path, indent, buf, cap, used);
            }
            return 0;
        default:
            return append_text(buf, cap, used, "<invalid>\n");
    }
}

static int render_collapsed_json_view(ThatchVal root,
                                      const JsonDemoManifest *manifest,
                                      char *buf,
                                      size_t cap)
{
    size_t used = 0u;

    if (!tj_is_object(root)) {
        return 1;
    }
    return render_container_children(root, manifest, "", 0u, buf, cap, &used);
}

int main(void)
{
    JsonDemoManifest manifest;
    uint8_t *wasm_bytes = NULL;
    uint32_t wasm_len = 0u;
    host_wasm_ctx_t *ctx = NULL;
    uint8_t *memory = NULL;
    uint32_t memory_len = 0u;
    int32_t rc = 0;
    int32_t json_ptr = 0;
    int32_t json_len = 0;
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

    sap_arena_init(&arena, &opts);
    CHECK(arena != NULL);
    env = sap_env_create(arena, SAPLING_PAGE_SIZE);
    CHECK(env != NULL);
    CHECK(sap_thatch_subsystem_init(env) == ERR_OK);
    txn = sap_txn_begin(env, NULL, 0u);
    CHECK(txn != NULL);

    CHECK(parse_manifest(txn, CROFT_WASM_JSON_DEMO_MANIFEST_PATH, &manifest) == 0);
    CHECK(strcmp(manifest.schema, "croft-wasm-demo-v1") == 0);
    CHECK(strcmp(manifest.kind, "wasm-demo") == 0);
    CHECK(strcmp(manifest.entrypoint_family, "croft_json_tree_text_view_family_current_machine") == 0);
    CHECK(strcmp(manifest.data_contract, "host-json-to-thatch-v0") == 0);
    CHECK(strcmp(manifest.view_contract, "collapsed-json-text-v0") == 0);
    CHECK(strcmp(manifest.guest_kind, "wat") == 0);
    CHECK(strcmp(manifest.guest_target, "wasm_json_source_guest_gen") == 0);
    CHECK(strcmp(manifest.applicability, "current-machine-windowed") == 0);
    CHECK(manifest.required_bundle_count == 2u);
    CHECK(strcmp(manifest.required_bundles[0], "croft-host-fs-current-machine") == 0);
    CHECK(strcmp(manifest.required_bundles[1], "croft-host-file-dialog-current-machine") == 0);
    CHECK(manifest.expanded_path_count == 1u);
    CHECK(strcmp(manifest.expanded_paths[0], ".features") == 0);
    CHECK(strcmp(manifest.preferred_slot, "croft-editor-shell-slot-current-machine") == 0);
    CHECK(manifest.preferred_slot_bundle_count == 2u);
    CHECK(strcmp(manifest.preferred_slot_bundles[0], "croft-editor-appkit-current-machine") == 0);
    CHECK(strcmp(manifest.preferred_slot_bundles[1], "croft-editor-scene-metal-native-current-machine") == 0);

    wasm_bytes = read_binary_file(manifest.wasm_path, &wasm_len);
    CHECK(wasm_bytes != NULL);

    ctx = host_wasm_create(wasm_bytes, wasm_len, 64u * 1024u);
    CHECK(ctx != NULL);
    rc = host_wasm_call(ctx, manifest.announce_export, 0, NULL);
    CHECK(rc == 0);
    json_ptr = host_wasm_call(ctx, manifest.json_pointer_export, 0, NULL);
    json_len = host_wasm_call(ctx, manifest.json_length_export, 0, NULL);
    CHECK(json_ptr > 0);
    CHECK(json_len > 0);

    memory = host_wasm_get_memory(ctx, &memory_len);
    CHECK(memory != NULL);
    CHECK((uint32_t)json_ptr + (uint32_t)json_len <= memory_len);

    CHECK(tj_parse(txn,
                   (const char *)(memory + (uint32_t)json_ptr),
                   (uint32_t)json_len,
                   &region,
                   &root,
                   &err_pos)
          == ERR_OK);
    CHECK(render_collapsed_json_view(root, &manifest, collapsed, sizeof(collapsed)) == 0);

    CHECK(strstr(collapsed, "project: \"Croft\"\n") != NULL);
    CHECK(strstr(collapsed, "features: {2 keys}\n") != NULL);
    CHECK(strstr(collapsed, "  solver: true\n") != NULL);
    CHECK(strstr(collapsed, "  thatch: \"json\"\n") != NULL);
    CHECK(strstr(collapsed, "items: [3 items]\n") != NULL);

    sap_txn_abort(txn);
    sap_env_destroy(env);
    sap_arena_destroy(arena);
    host_wasm_destroy(ctx);
    free(wasm_bytes);
    return 0;
}
