#include "croft/wit_wasi_machine_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CROFT_XPI_JSON_PATH
#error "CROFT_XPI_JSON_PATH must be defined"
#endif

#ifndef CROFT_ARTIFACT_JSON_PATH
#error "CROFT_ARTIFACT_JSON_PATH must be defined"
#endif

static int expect_true(const char* label, int actual)
{
    if (actual) {
        return 1;
    }
    fprintf(stderr, "%s: expected true\n", label);
    return 0;
}

static int expect_u32(const char* label, uint32_t actual, uint32_t expected)
{
    if (actual == expected) {
        return 1;
    }
    fprintf(stderr, "%s: expected %u, got %u\n", label, expected, actual);
    return 0;
}

static int expect_str(const char* label, const char* actual, const char* expected)
{
    if (actual && expected && strcmp(actual, expected) == 0) {
        return 1;
    }
    fprintf(stderr,
            "%s: expected '%s', got '%s'\n",
            label,
            expected ? expected : "<null>",
            actual ? actual : "<null>");
    return 0;
}

static int expect_array_contains(const char* label,
                                 const char* const* items,
                                 uint32_t count,
                                 const char* expected)
{
    uint32_t i;

    if (!items || !expected) {
        fprintf(stderr, "%s: missing array or expected value\n", label);
        return 0;
    }
    for (i = 0u; i < count; i++) {
        if (items[i] && strcmp(items[i], expected) == 0) {
            return 1;
        }
    }
    fprintf(stderr, "%s: missing '%s'\n", label, expected);
    return 0;
}

static int expect_contains(const char* label, const char* haystack, const char* needle)
{
    if (haystack && needle && strstr(haystack, needle)) {
        return 1;
    }
    fprintf(stderr, "%s: missing fragment '%s'\n", label, needle ? needle : "<null>");
    return 0;
}

static char* read_file(const char* path)
{
    FILE* f;
    long size;
    char* buf;
    size_t nread;

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
    buf = (char*)malloc((size_t)size + 1u);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    nread = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (nread != (size_t)size) {
        free(buf);
        return NULL;
    }
    buf[size] = '\0';
    return buf;
}

int main(void)
{
    const croft_wit_wasi_machine_substrate_descriptor* streams = NULL;
    const croft_wit_wasi_machine_bundle_descriptor* filesystem = NULL;
    const croft_wit_wasi_machine_bundle_descriptor* clocks = NULL;
    char* xpi_json = read_file(CROFT_XPI_JSON_PATH);
    char* artifact_json = read_file(CROFT_ARTIFACT_JSON_PATH);
    int ok = 1;

    if (!xpi_json) {
        fprintf(stderr, "unable to read %s\n", CROFT_XPI_JSON_PATH);
        return 1;
    }
    if (!artifact_json) {
        fprintf(stderr, "unable to read %s\n", CROFT_ARTIFACT_JSON_PATH);
        free(xpi_json);
        return 1;
    }

    ok &= expect_u32("substrate count", croft_wit_wasi_machine_substrates_count, 6u);
    ok &= expect_u32("bundle count", croft_wit_wasi_machine_bundles_count, 4u);

    streams = croft_wit_wasi_machine_find_substrate_descriptor("wasi-byte-streams");
    filesystem = croft_wit_wasi_machine_find_bundle_descriptor("wasi-filesystem-streams-current-machine");
    clocks = croft_wit_wasi_machine_find_bundle_descriptor("wasi-clocks-poll-current-machine");

    ok &= expect_true("find streams substrate", streams != NULL);
    ok &= expect_true("find filesystem bundle", filesystem != NULL);
    ok &= expect_true("find clocks bundle", clocks != NULL);

    if (streams) {
        ok &= expect_str("streams.kind", streams->kind, "byte-streams");
        ok &= expect_str("streams.applicability", streams->applicability, "current-machine-unix");
    }
    if (filesystem) {
        ok &= expect_u32("filesystem.kind",
                         (uint32_t)filesystem->kind,
                         CROFT_WIT_WASI_MACHINE_BUNDLE_FILESYSTEM_STREAMS);
        ok &= expect_str("filesystem.support_status", filesystem->support_status, "host-implemented-subset");
        ok &= expect_u32("filesystem.substrate_count", filesystem->substrate_count, 2u);
        ok &= expect_u32("filesystem.world_count", filesystem->declared_world_count, 2u);
        ok &= expect_u32("filesystem.helper_count", filesystem->helper_interface_count, 2u);
        ok &= expect_array_contains("filesystem.substrates",
                                    filesystem->substrates,
                                    filesystem->substrate_count,
                                    "wasi-byte-streams");
        ok &= expect_array_contains("filesystem.surfaces",
                                    filesystem->expanded_surfaces,
                                    filesystem->expanded_surface_count,
                                    "wasi:io@0.2.9/streams");
        ok &= expect_array_contains("filesystem.helpers",
                                    filesystem->helper_interfaces,
                                    filesystem->helper_interface_count,
                                    "wasi:filesystem@0.2.9/error");
    }
    if (clocks) {
        ok &= expect_u32("clocks.substrate_count", clocks->substrate_count, 2u);
        ok &= expect_array_contains("clocks.substrates",
                                    clocks->substrates,
                                    clocks->substrate_count,
                                    "wasi-pollables");
        ok &= expect_array_contains("clocks.worlds",
                                    clocks->declared_worlds,
                                    clocks->declared_world_count,
                                    "wasi:io@0.2.9/world");
    }

    ok &= expect_contains("xpi substrate name", xpi_json, "\"name\": \"wasi-descriptor-table\"");
    ok &= expect_contains("xpi filesystem bundle", xpi_json, "\"name\": \"wasi-filesystem-streams-current-machine\"");
    ok &= expect_contains("xpi filesystem bundle requirements",
                          xpi_json,
                          "\"requires_bundles\": [\"wasi-clocks-poll-current-machine\"]");
    ok &= expect_contains("xpi clocks bundle compatibility",
                          xpi_json,
                          "\"compatible_bundles\": [\"wasi-filesystem-streams-current-machine\"]");
    ok &= expect_contains("xpi artifacts section", xpi_json, "\"artifacts\": [");
    ok &= expect_contains("xpi entrypoints section", xpi_json, "\"entrypoints\": [");
    ok &= expect_contains("xpi slots section", xpi_json, "\"slots\": [");
    ok &= expect_contains("xpi runtime artifact", xpi_json, "\"name\": \"croft_wit_wasi_machine_runtime\"");
    ok &= expect_contains("xpi runtime bundles",
                          xpi_json,
                          "\"capability_bundles\": [\"wasi-cli-stdio-terminal-current-machine\", \"wasi-random-current-machine\", \"wasi-clocks-poll-current-machine\", \"wasi-filesystem-streams-current-machine\"]");
    ok &= expect_contains("xpi host window substrate", xpi_json, "\"name\": \"croft-window-system\"");
    ok &= expect_contains("xpi host window bundle", xpi_json, "\"name\": \"croft-host-window-current-machine\"");
    ok &= expect_contains("xpi render backend bundle", xpi_json, "\"name\": \"croft-render-tgfx-metal-current-machine\"");
    ok &= expect_contains("xpi render backend slot", xpi_json, "\"name\": \"croft-render-backend-slot-current-machine\"");
    ok &= expect_contains("xpi render backend slot mode", xpi_json, "\"mode\": \"exclusive\"");
    ok &= expect_contains("xpi render backend conflicts",
                          xpi_json,
                          "\"conflicts_with\": [\"croft-render-metal-native-current-machine\"]");
    ok &= expect_contains("xpi render backend slot membership",
                          xpi_json,
                          "\"slots\": [\"croft-render-backend-slot-current-machine\"]");
    ok &= expect_contains("xpi render family entrypoint",
                          xpi_json,
                          "\"name\": \"croft_render_canvas_family_current_machine\"");
    ok &= expect_contains("xpi render family kind",
                          xpi_json,
                          "\"kind\": \"family\"");
    ok &= expect_contains("xpi render family open slot",
                          xpi_json,
                          "\"open_slots\": [\"croft-render-backend-slot-current-machine\"]");
    ok &= expect_contains("xpi editor shell bundle", xpi_json, "\"name\": \"croft-editor-appkit-current-machine\"");
    ok &= expect_contains("xpi editor shell slot",
                          xpi_json,
                          "\"name\": \"croft-editor-shell-slot-current-machine\"");
    ok &= expect_contains("xpi editor shell conflicts",
                          xpi_json,
                          "\"conflicts_with\": [\"croft-editor-scene-tgfx-metal-current-machine\", \"croft-editor-scene-metal-native-current-machine\"]");
    ok &= expect_contains("xpi editor shell slot membership",
                          xpi_json,
                          "\"slots\": [\"croft-editor-shell-slot-current-machine\"]");
    ok &= expect_contains("xpi editor family entrypoint",
                          xpi_json,
                          "\"name\": \"croft_text_editor_family_current_machine\"");
    ok &= expect_contains("xpi editor family bundles",
                          xpi_json,
                          "\"requires_bundles\": [\"croft-host-fs-current-machine\", \"croft-host-file-dialog-current-machine\"]");
    ok &= expect_contains("xpi editor family open slot",
                          xpi_json,
                          "\"open_slots\": [\"croft-editor-shell-slot-current-machine\"]");
    ok &= expect_contains("xpi file dialog bundle", xpi_json, "\"name\": \"croft-host-file-dialog-current-machine\"");
    ok &= expect_contains("xpi gesture bundle", xpi_json, "\"name\": \"croft-host-gesture-current-machine\"");
    ok &= expect_contains("xpi host editor-input bundle", xpi_json, "\"name\": \"croft-host-editor-input-normalization\"");
    ok &= expect_contains("xpi host editor-input helpers",
                          xpi_json,
                          "\"helper_interfaces\": [\"lambkin:host-window@0.1.0/host-window\", \"lambkin:host-menu@0.1.0/host-menu\"]");
    ok &= expect_contains("xpi host editor-input requirements",
                          xpi_json,
                          "\"requires_bundles\": [\"croft-host-window-current-machine\", \"croft-host-menu-current-machine\"]");
    ok &= expect_contains("xpi host editor-input compatibility",
                          xpi_json,
                          "\"compatible_bundles\": [\"croft-host-clipboard-current-machine\", \"croft-host-popup-menu-current-machine\"]");
    ok &= expect_contains("xpi aggregate artifact", xpi_json, "\"name\": \"croft\"");
    ok &= expect_contains("xpi aggregate artifact bundles",
                          xpi_json,
                          "\"requires_bundles\": [\"croft-host-clock-current-machine\", \"croft-host-fs-current-machine\"]");
    ok &= expect_contains("xpi editor document aggregate", xpi_json, "\"name\": \"croft_editor_document\"");
    ok &= expect_contains("xpi editor document aggregate bundles",
                          xpi_json,
                          "\"requires_bundles\": [\"croft-host-fs-current-machine\"]");
    ok &= expect_contains("xpi textpad entrypoint", xpi_json, "\"name\": \"example_wit_textpad_window\"");
    ok &= expect_contains("xpi textpad entrypoint kind", xpi_json, "\"kind\": \"example\"");
    ok &= expect_contains("xpi textpad required bundles",
                          xpi_json,
                          "\"requires_bundles\": [\"croft-host-window-current-machine\", \"croft-host-gpu2d-current-machine\", \"croft-host-clock-current-machine\", \"croft-host-menu-current-machine\", \"croft-host-clipboard-current-machine\", \"croft-host-editor-input-normalization\"]");
    ok &= expect_contains("xpi editor entrypoint",
                          xpi_json,
                          "\"name\": \"example_editor_text_metal_native\"");
    ok &= expect_contains("xpi editor entrypoint bundles",
                          xpi_json,
                          "\"requires_bundles\": [\"croft-editor-scene-metal-native-current-machine\", \"croft-host-window-current-machine\", \"croft-host-gpu2d-current-machine\", \"croft-render-metal-native-current-machine\", \"croft-host-fs-current-machine\", \"croft-host-file-dialog-current-machine\", \"croft-host-clock-current-machine\", \"croft-host-menu-current-machine\", \"croft-host-popup-menu-current-machine\", \"croft-host-clipboard-current-machine\", \"croft-host-editor-input-normalization\", \"croft-host-a11y-current-machine\"]");
    ok &= expect_contains("xpi direct editor entrypoint",
                          xpi_json,
                          "\"name\": \"example_editor_text\"");
    ok &= expect_contains("xpi direct editor bundles",
                          xpi_json,
                          "\"requires_bundles\": [\"croft-host-clipboard-current-machine\", \"croft-editor-scene-tgfx-metal-current-machine\", \"croft-host-window-current-machine\", \"croft-render-tgfx-metal-current-machine\", \"croft-host-fs-current-machine\", \"croft-host-file-dialog-current-machine\", \"croft-host-popup-menu-current-machine\"]");
    ok &= expect_contains("xpi direct editor slot binding",
                          xpi_json,
                          "{ \"slot\": \"croft-editor-shell-slot-current-machine\", \"bundle\": \"croft-editor-scene-tgfx-metal-current-machine\" }");
    ok &= expect_contains("xpi direct appkit editor slot binding",
                          xpi_json,
                          "{ \"slot\": \"croft-editor-shell-slot-current-machine\", \"bundle\": \"croft-editor-appkit-current-machine\" }");
    ok &= expect_contains("xpi zoom entrypoint",
                          xpi_json,
                          "\"name\": \"example_zoom_canvas\"");
    ok &= expect_contains("xpi zoom bundles",
                          xpi_json,
                          "\"requires_bundles\": [\"croft-host-window-current-machine\", \"croft-render-tgfx-metal-current-machine\", \"croft-host-gesture-current-machine\"]");
    ok &= expect_contains("xpi ui window entrypoint",
                          xpi_json,
                          "\"name\": \"example_ui_window_metal\"");
    ok &= expect_contains("xpi ui window bundles",
                          xpi_json,
                          "\"requires_bundles\": [\"croft-host-window-current-machine\"]");
    ok &= expect_contains("xpi render example slot binding",
                          xpi_json,
                          "{ \"slot\": \"croft-render-backend-slot-current-machine\", \"bundle\": \"croft-render-tgfx-metal-current-machine\" }");
    ok &= expect_contains("xpi direct metal editor render slot binding",
                          xpi_json,
                          "{ \"slot\": \"croft-render-backend-slot-current-machine\", \"bundle\": \"croft-render-metal-native-current-machine\" }");
    ok &= expect_contains("xpi host window runtime artifact",
                          xpi_json,
                          "\"name\": \"croft_wit_host_window_runtime\"");
    ok &= expect_contains("xpi file dialog artifact", xpi_json, "\"name\": \"croft_file_dialog_macos\"");
    ok &= expect_contains("xpi filesystem helpers",
                          xpi_json,
                          "\"helper_interfaces\": [\"wasi:io@0.2.9/error\", \"wasi:filesystem@0.2.9/error\"]");
    ok &= expect_contains("xpi io world overlap", xpi_json, "\"wasi:io@0.2.9/world\"");
    ok &= expect_contains("artifact runtime bundles",
                          artifact_json,
                          "\"capability_bundles\": [\"wasi-cli-stdio-terminal-current-machine\", \"wasi-random-current-machine\", \"wasi-clocks-poll-current-machine\", \"wasi-filesystem-streams-current-machine\"]");
    ok &= expect_contains("artifact io world substrates",
                          artifact_json,
                          "\"shared_substrates\": [\"wasi-byte-streams\", \"wasi-descriptor-table\", \"wasi-pollables\"]");
    ok &= expect_contains("artifact filesystem helper",
                          artifact_json,
                          "\"helper_interfaces\": [\"wasi:filesystem@0.2.9/error\"]");
    ok &= expect_contains("artifact host window bundle",
                          artifact_json,
                          "\"capability_bundles\": [\"croft-host-window-current-machine\"]");
    ok &= expect_contains("artifact render backend bundle",
                          artifact_json,
                          "\"capability_bundles\": [\"croft-render-tgfx-metal-current-machine\"]");
    ok &= expect_contains("artifact render backend requires bundles",
                          artifact_json,
                          "\"requires_bundles\": [\"croft-host-window-current-machine\"]");
    ok &= expect_contains("artifact render backend selected slot",
                          artifact_json,
                          "\"selected_slot_bindings\": [{ \"slot\": \"croft-render-backend-slot-current-machine\", \"bundle\": \"croft-render-tgfx-metal-current-machine\" }]");
    ok &= expect_contains("artifact host window substrate",
                          artifact_json,
                          "\"shared_substrates\": [\"croft-window-system\"]");
    ok &= expect_contains("artifact host editor-input helper",
                          artifact_json,
                          "\"helper_interfaces\": [\"lambkin:host-window@0.1.0/host-window\", \"lambkin:host-menu@0.1.0/host-menu\"]");
    ok &= expect_contains("artifact host editor-input requires bundles",
                          artifact_json,
                          "\"requires_bundles\": [\"croft-host-window-current-machine\", \"croft-host-menu-current-machine\"]");
    ok &= expect_contains("artifact file dialog bundle",
                          artifact_json,
                          "\"capability_bundles\": [\"croft-host-file-dialog-current-machine\"]");
    ok &= expect_contains("artifact gesture bundle",
                          artifact_json,
                          "\"capability_bundles\": [\"croft-host-gesture-current-machine\"]");

    free(xpi_json);
    free(artifact_json);
    return ok ? 0 : 1;
}
