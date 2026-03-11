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
    ok &= expect_contains("xpi artifacts section", xpi_json, "\"artifacts\": [");
    ok &= expect_contains("xpi runtime artifact", xpi_json, "\"name\": \"croft_wit_wasi_machine_runtime\"");
    ok &= expect_contains("xpi runtime bundles",
                          xpi_json,
                          "\"capability_bundles\": [\"wasi-cli-stdio-terminal-current-machine\", \"wasi-random-current-machine\", \"wasi-clocks-poll-current-machine\", \"wasi-filesystem-streams-current-machine\"]");
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

    free(xpi_json);
    free(artifact_json);
    return ok ? 0 : 1;
}
