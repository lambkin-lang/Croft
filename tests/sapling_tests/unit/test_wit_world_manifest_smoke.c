#include "tests/generated/world_meta_command.h"
#include "croft/wit_world_runtime.h"

#include <stddef.h>
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

static int32_t world_import_get_environment(void *ctx, SapWitCliEnvironmentReply *reply_out)
{
    uint32_t *calls = (uint32_t *)ctx;

    if (calls) {
        (*calls)++;
    }
    sap_wit_zero_cli_environment_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLI_ENVIRONMENT_REPLY_GET_ENVIRONMENT;
    reply_out->val.get_environment.data = (const uint8_t *)"";
    reply_out->val.get_environment.len = 0u;
    reply_out->val.get_environment.byte_len = 0u;
    return 0;
}

static int32_t world_export_start(void *ctx, SapWitCliRunReply *reply_out)
{
    uint32_t *calls = (uint32_t *)ctx;

    if (calls) {
        (*calls)++;
    }
    sap_wit_zero_cli_run_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLI_RUN_REPLY_STATUS;
    reply_out->val.status.is_v_ok = 1u;
    return 0;
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
    const SapWitWorldEndpointDescriptor *import_environment_endpoint = NULL;
    const SapWitWorldEndpointDescriptor *export_run_endpoint = NULL;
    SapWitCliEnvironmentReply env_reply = {0};
    SapWitCliRunReply run_reply = {0};
    uint32_t import_calls = 0u;
    uint32_t export_calls = 0u;
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
    ok &= expect_u32("command import endpoint count", sap_wit_cli_command_import_endpoints_count, 1u);
    ok &= expect_u32("command export endpoint count", sap_wit_cli_command_export_endpoints_count, 1u);

    environment = sap_wit_find_interface_descriptor(sap_wit_cli_interfaces,
                                                    sap_wit_cli_interfaces_count,
                                                    "environment");
    run = sap_wit_find_interface_descriptor(sap_wit_cli_interfaces,
                                            sap_wit_cli_interfaces_count,
                                            "run");
    command_world = sap_wit_find_world_descriptor(sap_wit_cli_worlds,
                                                  sap_wit_cli_worlds_count,
                                                  "command");
    imports_world = sap_wit_find_world_descriptor(sap_wit_cli_worlds,
                                                  sap_wit_cli_worlds_count,
                                                  "imports");
    include_imports = sap_wit_find_world_binding_descriptor(sap_wit_cli_world_bindings,
                                                            sap_wit_cli_world_bindings_count,
                                                            "command",
                                                            "imports",
                                                            SAP_WIT_WORLD_ITEM_INCLUDE);
    import_environment = sap_wit_find_world_binding_descriptor(sap_wit_cli_world_bindings,
                                                               sap_wit_cli_world_bindings_count,
                                                               "imports",
                                                               "environment",
                                                               SAP_WIT_WORLD_ITEM_IMPORT);
    export_run = sap_wit_find_world_binding_descriptor(sap_wit_cli_world_bindings,
                                                       sap_wit_cli_world_bindings_count,
                                                       "command",
                                                       "run",
                                                       SAP_WIT_WORLD_ITEM_EXPORT);
    import_environment_endpoint =
        sap_wit_find_world_endpoint_descriptor(sap_wit_cli_command_import_endpoints,
                                              sap_wit_cli_command_import_endpoints_count,
                                              "environment");
    export_run_endpoint =
        sap_wit_find_world_endpoint_descriptor(sap_wit_cli_command_export_endpoints,
                                              sap_wit_cli_command_export_endpoints_count,
                                              "run");

    ok &= environment != NULL;
    ok &= run != NULL;
    ok &= command_world != NULL;
    ok &= imports_world != NULL;
    ok &= include_imports != NULL;
    ok &= import_environment != NULL;
    ok &= export_run != NULL;
    ok &= import_environment_endpoint != NULL;
    ok &= export_run_endpoint != NULL;

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
    if (import_environment_endpoint) {
        ok &= expect_u32("import_environment_endpoint.kind",
                         import_environment_endpoint->kind,
                         SAP_WIT_WORLD_ITEM_IMPORT);
        ok &= expect_u32("import_environment_endpoint.target_kind",
                         import_environment_endpoint->target_kind,
                         SAP_WIT_WORLD_TARGET_INTERFACE);
        ok &= expect_str("import_environment_endpoint.bindings_c_type",
                         import_environment_endpoint->bindings_c_type,
                         "SapWitCliCommandWorldImports");
        ok &= expect_str("import_environment_endpoint.ops_c_type",
                         import_environment_endpoint->ops_c_type,
                         "SapWitCliEnvironmentDispatchOps");
        ok &= expect_str("import_environment_endpoint.command_c_type",
                         import_environment_endpoint->command_c_type,
                         "SapWitCliEnvironmentCommand");
        ok &= expect_str("import_environment_endpoint.reply_c_type",
                         import_environment_endpoint->reply_c_type,
                         "SapWitCliEnvironmentReply");
        ok &= expect_u32("import_environment_endpoint.ctx_offset",
                         (uint32_t)import_environment_endpoint->ctx_offset,
                         (uint32_t)offsetof(SapWitCliCommandWorldImports, environment_ctx));
        ok &= expect_u32("import_environment_endpoint.ops_offset",
                         (uint32_t)import_environment_endpoint->ops_offset,
                         (uint32_t)offsetof(SapWitCliCommandWorldImports, environment_ops));
    }
    if (export_run_endpoint) {
        ok &= expect_u32("export_run_endpoint.kind",
                         export_run_endpoint->kind,
                         SAP_WIT_WORLD_ITEM_EXPORT);
        ok &= expect_u32("export_run_endpoint.target_kind",
                         export_run_endpoint->target_kind,
                         SAP_WIT_WORLD_TARGET_INTERFACE);
        ok &= expect_str("export_run_endpoint.bindings_c_type",
                         export_run_endpoint->bindings_c_type,
                         "SapWitCliCommandWorldExports");
        ok &= expect_str("export_run_endpoint.ops_c_type",
                         export_run_endpoint->ops_c_type,
                         "SapWitCliRunDispatchOps");
        ok &= expect_str("export_run_endpoint.command_c_type",
                         export_run_endpoint->command_c_type,
                         "SapWitCliRunCommand");
        ok &= expect_str("export_run_endpoint.reply_c_type",
                         export_run_endpoint->reply_c_type,
                         "SapWitCliRunReply");
        ok &= expect_u32("export_run_endpoint.ctx_offset",
                         (uint32_t)export_run_endpoint->ctx_offset,
                         (uint32_t)offsetof(SapWitCliCommandWorldExports, run_ctx));
        ok &= expect_u32("export_run_endpoint.ops_offset",
                         (uint32_t)export_run_endpoint->ops_offset,
                         (uint32_t)offsetof(SapWitCliCommandWorldExports, run_ops));
    }

    {
        SapWitCliEnvironmentDispatchOps env_ops = {
            .get_environment = world_import_get_environment,
        };
        SapWitCliRunDispatchOps run_ops = {
            .start = world_export_start,
        };
        SapWitCliCommandWorldImports command_imports = {0};
        SapWitCliCommandWorldExports command_exports = {0};

        ok &= expect_u32("bind import environment",
                         (uint32_t)sap_wit_world_endpoint_bind(&command_imports,
                                                               import_environment_endpoint,
                                                               &import_calls,
                                                               &env_ops),
                         0u);
        ok &= expect_u32("bind export run",
                         (uint32_t)sap_wit_world_endpoint_bind(&command_exports,
                                                               export_run_endpoint,
                                                               &export_calls,
                                                               &run_ops),
                         0u);
        ok &= expect_u32("import ctx getter",
                         (uint32_t)(sap_wit_world_endpoint_ctx(&command_imports,
                                                               import_environment_endpoint)
                                        == &import_calls),
                         1u);
        ok &= expect_u32("export ctx getter",
                         (uint32_t)(sap_wit_world_endpoint_ctx(&command_exports,
                                                               export_run_endpoint)
                                        == &export_calls),
                         1u);
        ok &= expect_u32("import ops getter",
                         (uint32_t)(sap_wit_world_endpoint_ops(&command_imports,
                                                               import_environment_endpoint)
                                        == &env_ops),
                         1u);
        ok &= expect_u32("export ops getter",
                         (uint32_t)(sap_wit_world_endpoint_ops(&command_exports,
                                                               export_run_endpoint)
                                        == &run_ops),
                         1u);

        ok &= expect_u32("world import wrapper rc",
                         (uint32_t)sap_wit_world_cli_command_import_environment(&command_imports,
                                                                               &env_command,
                                                                               &env_reply),
                         0u);
        ok &= expect_u32("world export wrapper rc",
                         (uint32_t)sap_wit_world_cli_command_export_run(&command_exports,
                                                                        &run_command,
                                                                        &run_reply),
                         0u);
        ok &= expect_u32("world import callback count", import_calls, 1u);
        ok &= expect_u32("world export callback count", export_calls, 1u);
        ok &= expect_u32("world import reply case",
                         env_reply.case_tag,
                         SAP_WIT_CLI_ENVIRONMENT_REPLY_GET_ENVIRONMENT);
        ok &= expect_u32("world export reply case",
                         run_reply.case_tag,
                         SAP_WIT_CLI_RUN_REPLY_STATUS);
        ok &= expect_u32("world export reply ok", run_reply.val.status.is_v_ok, 1u);

        import_calls = 0u;
        export_calls = 0u;
        memset(&env_reply, 0, sizeof(env_reply));
        memset(&run_reply, 0, sizeof(run_reply));

        ok &= expect_u32("world import endpoint invoke rc",
                         (uint32_t)sap_wit_world_endpoint_invoke(import_environment_endpoint,
                                                                 &command_imports,
                                                                 &env_command,
                                                                 &env_reply),
                         0u);
        ok &= expect_u32("world export endpoint invoke rc",
                         (uint32_t)sap_wit_world_endpoint_invoke(export_run_endpoint,
                                                                 &command_exports,
                                                                 &run_command,
                                                                 &run_reply),
                         0u);
        ok &= expect_u32("world import endpoint callback count", import_calls, 1u);
        ok &= expect_u32("world export endpoint callback count", export_calls, 1u);
    }

    free(manifest);
    (void)env_command;
    (void)run_command;
    return ok ? 0 : 1;
}
