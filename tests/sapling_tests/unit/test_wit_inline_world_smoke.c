#include "tests/generated/world_inline_command.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WIT_WORLD_INLINE_MANIFEST_PATH
#error "WIT_WORLD_INLINE_MANIFEST_PATH must be defined"
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
    for (uint32_t i = 0; i < sap_wit_inline_world_interfaces_count; i++) {
        if (strcmp(sap_wit_inline_world_interfaces[i].interface_name, name) == 0) {
            return &sap_wit_inline_world_interfaces[i];
        }
    }
    return NULL;
}

static const SapWitWorldDescriptor *find_world_desc(const char *name)
{
    for (uint32_t i = 0; i < sap_wit_inline_world_worlds_count; i++) {
        if (strcmp(sap_wit_inline_world_worlds[i].world_name, name) == 0) {
            return &sap_wit_inline_world_worlds[i];
        }
    }
    return NULL;
}

static const SapWitWorldBindingDescriptor *find_world_binding_desc(const char *item_name,
                                                                   SapWitWorldItemKind kind)
{
    for (uint32_t i = 0; i < sap_wit_inline_world_world_bindings_count; i++) {
        if (sap_wit_inline_world_world_bindings[i].kind == kind
                && strcmp(sap_wit_inline_world_world_bindings[i].world_name, "command") == 0
                && strcmp(sap_wit_inline_world_world_bindings[i].item_name, item_name) == 0) {
            return &sap_wit_inline_world_world_bindings[i];
        }
    }
    return NULL;
}

static int32_t env_get_environment(void *ctx, SapWitInlineWorldEnvironmentReply *reply_out)
{
    uint32_t *calls = (uint32_t *)ctx;

    if (calls) {
        (*calls)++;
    }
    sap_wit_zero_inline_world_environment_reply(reply_out);
    reply_out->case_tag = SAP_WIT_INLINE_WORLD_ENVIRONMENT_REPLY_GET_ENVIRONMENT;
    reply_out->val.get_environment.data = (const uint8_t *)"";
    reply_out->val.get_environment.len = 0u;
    reply_out->val.get_environment.byte_len = 0u;
    return 0;
}

static int32_t run_run(void *ctx, SapWitInlineWorldRunReply *reply_out)
{
    uint32_t *calls = (uint32_t *)ctx;

    if (calls) {
        (*calls)++;
    }
    sap_wit_zero_inline_world_run_reply(reply_out);
    reply_out->case_tag = SAP_WIT_INLINE_WORLD_RUN_REPLY_STATUS;
    reply_out->val.status.is_v_ok = 1u;
    return 0;
}

static int32_t status_check_current(void *ctx, SapWitInlineWorldStatusCheckReply *reply_out)
{
    uint32_t *calls = (uint32_t *)ctx;

    if (calls) {
        (*calls)++;
    }
    sap_wit_zero_inline_world_status_check_reply(reply_out);
    reply_out->case_tag = SAP_WIT_INLINE_WORLD_STATUS_CHECK_REPLY_STATUS;
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
    const SapWitInterfaceDescriptor *shared = NULL;
    const SapWitInterfaceDescriptor *environment = NULL;
    const SapWitInterfaceDescriptor *run = NULL;
    const SapWitInterfaceDescriptor *status_check = NULL;
    const SapWitWorldDescriptor *command_world = NULL;
    const SapWitWorldBindingDescriptor *environment_binding = NULL;
    const SapWitWorldBindingDescriptor *run_binding = NULL;
    const SapWitWorldBindingDescriptor *status_check_binding = NULL;
    SapWitInlineWorldEnvironmentCommand environment_command = {0};
    SapWitInlineWorldRunCommand run_command = {0};
    SapWitInlineWorldStatusCheckCommand status_check_command = {0};
    SapWitInlineWorldEnvironmentReply environment_reply = {0};
    SapWitInlineWorldRunReply run_reply = {0};
    SapWitInlineWorldStatusCheckReply status_check_reply = {0};
    uint32_t import_calls = 0u;
    uint32_t run_calls = 0u;
    uint32_t status_check_calls = 0u;
    char *manifest = read_file(WIT_WORLD_INLINE_MANIFEST_PATH);
    int ok = 1;

    if (!manifest) {
        fprintf(stderr, "unable to read manifest %s\n", WIT_WORLD_INLINE_MANIFEST_PATH);
        return 1;
    }

    environment_command.case_tag = SAP_WIT_INLINE_WORLD_ENVIRONMENT_COMMAND_GET_ENVIRONMENT;
    run_command.case_tag = SAP_WIT_INLINE_WORLD_RUN_COMMAND_RUN;
    status_check_command.case_tag = SAP_WIT_INLINE_WORLD_STATUS_CHECK_COMMAND_CURRENT;

    ok &= expect_contains(manifest,
                          "interface\tdemo:inline-world@0.1.0/environment\tenvironment\t<none>\t<none>\timported=0; attrs=@since(version = 0.1.0); origin=command::environment");
    ok &= expect_contains(manifest,
                          "interface\tdemo:inline-world@0.1.0/run\trun\t<none>\t<none>\timported=0; attrs=@since(version = 0.1.0); origin=command::run");
    ok &= expect_contains(manifest,
                          "interface\tdemo:inline-world@0.1.0/status-check\tstatus-check\t<none>\t<none>\timported=0; attrs=@since(version = 0.1.0); origin=command::status-check");
    ok &= expect_contains(manifest,
                          "world-import\tdemo:inline-world@0.1.0/command::environment\tenvironment\t<none>\t<none>\timported=0; target-kind=interface; target=demo:inline-world@0.1.0/environment; attrs=@since(version = 0.1.0)");
    ok &= expect_contains(manifest,
                          "world-export\tdemo:inline-world@0.1.0/command::run\trun\t<none>\t<none>\timported=0; target-kind=function; target=demo:inline-world@0.1.0/run; attrs=@since(version = 0.1.0); lowered=demo:inline-world@0.1.0/run");

    ok &= expect_u32("interface descriptor count", sap_wit_inline_world_interfaces_count, 4u);
    ok &= expect_u32("world descriptor count", sap_wit_inline_world_worlds_count, 1u);
    ok &= expect_u32("world binding descriptor count", sap_wit_inline_world_world_bindings_count, 3u);

    shared = find_interface_desc("shared");
    environment = find_interface_desc("environment");
    run = find_interface_desc("run");
    status_check = find_interface_desc("status-check");
    command_world = find_world_desc("command");
    environment_binding = find_world_binding_desc("environment", SAP_WIT_WORLD_ITEM_IMPORT);
    run_binding = find_world_binding_desc("run", SAP_WIT_WORLD_ITEM_EXPORT);
    status_check_binding = find_world_binding_desc("status-check", SAP_WIT_WORLD_ITEM_EXPORT);

    ok &= shared != NULL;
    ok &= environment != NULL;
    ok &= run != NULL;
    ok &= status_check != NULL;
    ok &= command_world != NULL;
    ok &= environment_binding != NULL;
    ok &= run_binding != NULL;
    ok &= status_check_binding != NULL;

    if (shared) {
        ok &= expect_str("shared.origin_world_name", shared->origin_world_name, "");
        ok &= expect_str("shared.origin_item_name", shared->origin_item_name, "");
    }
    if (environment) {
        ok &= expect_str("environment.origin_world_name", environment->origin_world_name, "command");
        ok &= expect_str("environment.origin_item_name", environment->origin_item_name, "environment");
    }
    if (run) {
        ok &= expect_str("run.origin_world_name", run->origin_world_name, "command");
        ok &= expect_str("run.origin_item_name", run->origin_item_name, "run");
    }
    if (status_check) {
        ok &= expect_str("status_check.origin_world_name", status_check->origin_world_name, "command");
        ok &= expect_str("status_check.origin_item_name", status_check->origin_item_name, "status-check");
    }
    if (command_world) {
        ok &= expect_u32("command.binding_count", command_world->binding_count, 3u);
    }
    if (environment_binding) {
        ok &= expect_u32("environment_binding.target_kind",
                         environment_binding->target_kind,
                         SAP_WIT_WORLD_TARGET_INTERFACE);
        ok &= expect_str("environment_binding.target_name", environment_binding->target_name, "environment");
        ok &= expect_str("environment_binding.lowered_name", environment_binding->lowered_name, "environment");
    }
    if (run_binding) {
        ok &= expect_u32("run_binding.target_kind",
                         run_binding->target_kind,
                         SAP_WIT_WORLD_TARGET_FUNCTION);
        ok &= expect_str("run_binding.target_name", run_binding->target_name, "run");
        ok &= expect_str("run_binding.lowered_name", run_binding->lowered_name, "run");
    }
    if (status_check_binding) {
        ok &= expect_u32("status_check_binding.target_kind",
                         status_check_binding->target_kind,
                         SAP_WIT_WORLD_TARGET_INTERFACE);
        ok &= expect_str("status_check_binding.target_name", status_check_binding->target_name, "status-check");
        ok &= expect_str("status_check_binding.lowered_name", status_check_binding->lowered_name, "status-check");
    }

    {
        SapWitInlineWorldEnvironmentDispatchOps environment_ops = {
            .get_environment = env_get_environment,
        };
        SapWitInlineWorldRunDispatchOps run_ops = {
            .run = run_run,
        };
        SapWitInlineWorldStatusCheckDispatchOps status_check_ops = {
            .current = status_check_current,
        };
        SapWitInlineWorldCommandWorldImports imports = {
            .environment_ctx = &import_calls,
            .environment_ops = &environment_ops,
        };
        SapWitInlineWorldCommandWorldExports exports = {
            .run_ctx = &run_calls,
            .run_ops = &run_ops,
            .status_check_ctx = &status_check_calls,
            .status_check_ops = &status_check_ops,
        };

        ok &= expect_u32("environment wrapper rc",
                         (uint32_t)sap_wit_world_inline_world_command_import_environment(&imports,
                                                                                        &environment_command,
                                                                                        &environment_reply),
                         0u);
        ok &= expect_u32("run wrapper rc",
                         (uint32_t)sap_wit_world_inline_world_command_export_run(&exports,
                                                                                &run_command,
                                                                                &run_reply),
                         0u);
        ok &= expect_u32("status-check wrapper rc",
                         (uint32_t)sap_wit_world_inline_world_command_export_status_check(&exports,
                                                                                         &status_check_command,
                                                                                         &status_check_reply),
                         0u);
        ok &= expect_u32("environment callback count", import_calls, 1u);
        ok &= expect_u32("run callback count", run_calls, 1u);
        ok &= expect_u32("status-check callback count", status_check_calls, 1u);
        ok &= expect_u32("environment reply case",
                         environment_reply.case_tag,
                         SAP_WIT_INLINE_WORLD_ENVIRONMENT_REPLY_GET_ENVIRONMENT);
        ok &= expect_u32("run reply case",
                         run_reply.case_tag,
                         SAP_WIT_INLINE_WORLD_RUN_REPLY_STATUS);
        ok &= expect_u32("status-check reply case",
                         status_check_reply.case_tag,
                         SAP_WIT_INLINE_WORLD_STATUS_CHECK_REPLY_STATUS);
    }

    free(manifest);
    return ok ? 0 : 1;
}
