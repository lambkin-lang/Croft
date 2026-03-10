#include "tests/generated/world_meta_command.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WIT_WORLD_META_MANIFEST_PATH
#error "WIT_WORLD_META_MANIFEST_PATH must be defined"
#endif

static int expect_contains(const char *haystack, const char *needle)
{
    if (strstr(haystack, needle)) {
        return 1;
    }
    fprintf(stderr, "missing manifest fragment: %s\n", needle);
    return 0;
}

static int expect_u32(const char *label, uint32_t actual, uint32_t expected)
{
    if (actual == expected) {
        return 1;
    }
    fprintf(stderr, "%s: expected %u, got %u\n", label, expected, actual);
    return 0;
}

static int expect_str(const char *label, const char *actual, const char *expected)
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

static const SapWitInterfaceDescriptor *find_interface_desc(const char *name)
{
    for (uint32_t i = 0; i < sap_wit_cli_interfaces_count; i++) {
        if (strcmp(sap_wit_cli_interfaces[i].interface_name, name) == 0) {
            return &sap_wit_cli_interfaces[i];
        }
    }
    return NULL;
}

static const SapWitWorldDescriptor *find_world_desc(const char *name)
{
    for (uint32_t i = 0; i < sap_wit_cli_worlds_count; i++) {
        if (strcmp(sap_wit_cli_worlds[i].world_name, name) == 0) {
            return &sap_wit_cli_worlds[i];
        }
    }
    return NULL;
}

static const SapWitWorldBindingDescriptor *find_world_binding_desc(const char *world_name,
                                                                   const char *item_name,
                                                                   SapWitWorldItemKind kind)
{
    for (uint32_t i = 0; i < sap_wit_cli_world_bindings_count; i++) {
        if (sap_wit_cli_world_bindings[i].kind == kind
                && strcmp(sap_wit_cli_world_bindings[i].world_name, world_name) == 0
                && strcmp(sap_wit_cli_world_bindings[i].item_name, item_name) == 0) {
            return &sap_wit_cli_world_bindings[i];
        }
    }
    return NULL;
}

static char *read_file(const char *path)
{
    FILE *f;
    long size;
    char *buf;
    size_t nread;

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "failed to open %s\n", path);
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
    SapWitCliEnvironmentCommand env_command = {0};
    SapWitCliRunCommand run_command = {0};
    const SapWitInterfaceDescriptor *environment = NULL;
    const SapWitInterfaceDescriptor *run = NULL;
    const SapWitWorldDescriptor *command_world = NULL;
    const SapWitWorldDescriptor *imports_world = NULL;
    const SapWitWorldBindingDescriptor *include_imports = NULL;
    const SapWitWorldBindingDescriptor *import_environment = NULL;
    const SapWitWorldBindingDescriptor *export_run = NULL;
    char *manifest = read_file(WIT_WORLD_META_MANIFEST_PATH);
    int ok = 1;

    if (!manifest) {
        fprintf(stderr, "unable to read manifest %s\n", WIT_WORLD_META_MANIFEST_PATH);
        return 1;
    }

    env_command.case_tag = SAP_WIT_CLI_ENVIRONMENT_COMMAND_GET_ENVIRONMENT;
    run_command.case_tag = SAP_WIT_CLI_RUN_COMMAND_START;

    ok &= expect_contains(manifest, "# Columns: kind\twit-name\tnormalized\tc-name\tmacro-or-function\tmetadata");
    ok &= expect_contains(manifest, "world\tdemo:cli@0.1.0/command\tcommand\t<none>\t<none>\timported=0; attrs=@since(version = 0.1.0)");
    ok &= expect_contains(manifest, "world\tdemo:cli@0.1.0/imports\timports\t<none>\t<none>\timported=1; attrs=@since(version = 0.1.0)");
    ok &= expect_contains(manifest, "world-include\tdemo:cli@0.1.0/command::imports\timports\t<none>\t<none>\timported=0; target-kind=world; target=demo:cli@0.1.0/imports; attrs=@since(version = 0.1.0)");
    ok &= expect_contains(manifest, "world-import\tdemo:cli@0.1.0/imports::environment\tenvironment\t<none>\t<none>\timported=1; target-kind=interface; target=demo:cli@0.1.0/environment; attrs=@since(version = 0.1.0)");
    ok &= expect_contains(manifest, "world-export\tdemo:cli@0.1.0/command::run\trun\t<none>\t<none>\timported=0; target-kind=interface; target=demo:cli@0.1.0/run; attrs=@since(version = 0.1.0)");
    ok &= expect_contains(manifest, "interface\tdemo:cli@0.1.0/environment\tenvironment\t<none>\t<none>\timported=1; attrs=@since(version = 0.1.0)");
    ok &= expect_contains(manifest, "interface\tdemo:cli@0.1.0/run\trun\t<none>\t<none>\timported=1; attrs=@since(version = 0.1.0)");

    ok &= expect_u32("interface descriptor count", sap_wit_cli_interfaces_count, 2u);
    ok &= expect_u32("world descriptor count", sap_wit_cli_worlds_count, 2u);
    ok &= expect_u32("world binding descriptor count", sap_wit_cli_world_bindings_count, 3u);

    environment = find_interface_desc("environment");
    run = find_interface_desc("run");
    command_world = find_world_desc("command");
    imports_world = find_world_desc("imports");
    include_imports = find_world_binding_desc("command", "imports", SAP_WIT_WORLD_ITEM_INCLUDE);
    import_environment = find_world_binding_desc("imports", "environment", SAP_WIT_WORLD_ITEM_IMPORT);
    export_run = find_world_binding_desc("command", "run", SAP_WIT_WORLD_ITEM_EXPORT);

    ok &= environment != NULL;
    ok &= run != NULL;
    ok &= command_world != NULL;
    ok &= imports_world != NULL;
    ok &= include_imports != NULL;
    ok &= import_environment != NULL;
    ok &= export_run != NULL;

    if (environment) {
        ok &= expect_str("environment.package_id", environment->package_id, "demo:cli@0.1.0");
        ok &= expect_str("environment.attributes", environment->attributes, "@since(version = 0.1.0)");
        ok &= expect_u32("environment.imported", environment->imported, 1u);
    }
    if (run) {
        ok &= expect_str("run.package_id", run->package_id, "demo:cli@0.1.0");
        ok &= expect_str("run.attributes", run->attributes, "@since(version = 0.1.0)");
        ok &= expect_u32("run.imported", run->imported, 1u);
    }
    if (command_world) {
        ok &= expect_str("command.package_id", command_world->package_id, "demo:cli@0.1.0");
        ok &= expect_str("command.attributes", command_world->attributes, "@since(version = 0.1.0)");
        ok &= expect_u32("command.imported", command_world->imported, 0u);
        ok &= expect_u32("command.binding_offset", command_world->binding_offset, 0u);
        ok &= expect_u32("command.binding_count", command_world->binding_count, 2u);
    }
    if (imports_world) {
        ok &= expect_str("imports.package_id", imports_world->package_id, "demo:cli@0.1.0");
        ok &= expect_str("imports.attributes", imports_world->attributes, "@since(version = 0.1.0)");
        ok &= expect_u32("imports.imported", imports_world->imported, 1u);
        ok &= expect_u32("imports.binding_offset", imports_world->binding_offset, 2u);
        ok &= expect_u32("imports.binding_count", imports_world->binding_count, 1u);
    }
    if (include_imports) {
        ok &= expect_u32("include_imports.target_kind",
                         include_imports->target_kind,
                         SAP_WIT_WORLD_TARGET_WORLD);
        ok &= expect_str("include_imports.target_package_id",
                         include_imports->target_package_id,
                         "demo:cli@0.1.0");
        ok &= expect_str("include_imports.target_name", include_imports->target_name, "imports");
        ok &= expect_u32("include_imports.imported", include_imports->imported, 0u);
    }
    if (import_environment) {
        ok &= expect_u32("import_environment.target_kind",
                         import_environment->target_kind,
                         SAP_WIT_WORLD_TARGET_INTERFACE);
        ok &= expect_str("import_environment.target_package_id",
                         import_environment->target_package_id,
                         "demo:cli@0.1.0");
        ok &= expect_str("import_environment.target_name", import_environment->target_name, "environment");
        ok &= expect_u32("import_environment.imported", import_environment->imported, 1u);
    }
    if (export_run) {
        ok &= expect_u32("export_run.target_kind",
                         export_run->target_kind,
                         SAP_WIT_WORLD_TARGET_INTERFACE);
        ok &= expect_str("export_run.target_package_id",
                         export_run->target_package_id,
                         "demo:cli@0.1.0");
        ok &= expect_str("export_run.target_name", export_run->target_name, "run");
        ok &= expect_u32("export_run.imported", export_run->imported, 0u);
    }

    free(manifest);
    (void)env_command;
    (void)run_command;
    return ok ? 0 : 1;
}
