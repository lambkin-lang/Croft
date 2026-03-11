#include "croft/host_wasm.h"

#include <stdio.h>
#include <stdlib.h>

#include "world_inline_command.h"

#ifndef WIT_WORLD_EXPORT_GUEST_WASM_PATH
#define WIT_WORLD_EXPORT_GUEST_WASM_PATH "wit_world_export_guest.wasm"
#endif

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #expr, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

static uint8_t *slurp_file(const char *path, uint32_t *out_len)
{
    FILE *f = NULL;
    uint8_t *buf = NULL;
    long sz = 0;
    size_t nr = 0u;

    f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    nr = fread(buf, 1u, (size_t)sz, f);
    fclose(f);
    if ((long)nr != sz) {
        free(buf);
        return NULL;
    }

    if (out_len) {
        *out_len = (uint32_t)sz;
    }
    return buf;
}

static int32_t inline_world_get_environment(void *ctx, SapWitInlineWorldEnvironmentReply *reply_out)
{
    uint32_t *call_count = (uint32_t *)ctx;

    if (!call_count || !reply_out) {
        return ERR_INVALID;
    }

    (*call_count)++;
    sap_wit_zero_inline_world_environment_reply(reply_out);
    reply_out->case_tag = SAP_WIT_INLINE_WORLD_ENVIRONMENT_REPLY_GET_ENVIRONMENT;
    reply_out->val.get_environment.data = NULL;
    reply_out->val.get_environment.len = 0u;
    reply_out->val.get_environment.byte_len = 0u;
    return ERR_OK;
}

void run_test_wasm_wit_export_guest(int argc, char **argv)
{
    static const SapWitInlineWorldEnvironmentDispatchOps k_environment_ops = {
        .get_environment = inline_world_get_environment,
    };
    SapWitInlineWorldCommandWorldImports imports = {0};
    uint32_t environment_call_count = 0u;
    uint32_t wasm_len = 0u;
    uint8_t *wasm_bytes = NULL;
    host_wasm_ctx_t *ctx = NULL;
    SapWitInlineWorldStatusCheckCommand status_command = {0};
    SapWitInlineWorldStatusCheckReply status_reply;
    SapWitInlineWorldRunCommand run_command = {0};
    SapWitInlineWorldRunReply run_reply;
    int32_t rc;

    (void)argc;
    (void)argv;

    wasm_bytes = slurp_file(WIT_WORLD_EXPORT_GUEST_WASM_PATH, &wasm_len);
    if (!wasm_bytes) {
        printf("SKIP: %s not found (likely compiler was not available).\n",
               WIT_WORLD_EXPORT_GUEST_WASM_PATH);
        return;
    }

    imports.environment_ctx = &environment_call_count;
    imports.environment_ops = &k_environment_ops;

    ctx = host_wasm_create(wasm_bytes, wasm_len, 64u * 1024u);
    CHECK(ctx != NULL);
    CHECK(host_wasm_register_wit_world_endpoints(ctx,
                                                 sap_wit_inline_world_command_import_endpoints,
                                                 sap_wit_inline_world_command_import_endpoints_count,
                                                 &imports)
          == ERR_OK);

    status_command.case_tag = SAP_WIT_INLINE_WORLD_STATUS_CHECK_COMMAND_CURRENT;
    sap_wit_zero_inline_world_status_check_reply(&status_reply);
    rc = host_wasm_call_wit_export_endpoint(ctx,
                                            &sap_wit_inline_world_command_export_endpoints[1],
                                            &status_command,
                                            &status_reply);
    CHECK(rc == ERR_OK);
    CHECK(status_reply.case_tag == SAP_WIT_INLINE_WORLD_STATUS_CHECK_REPLY_STATUS);
    CHECK(status_reply.val.status.is_v_ok == 0u);

    run_command.case_tag = SAP_WIT_INLINE_WORLD_RUN_COMMAND_RUN;
    sap_wit_zero_inline_world_run_reply(&run_reply);
    rc = host_wasm_call_wit_export_endpoint(ctx,
                                            &sap_wit_inline_world_command_export_endpoints[0],
                                            &run_command,
                                            &run_reply);
    CHECK(rc == ERR_OK);
    CHECK(run_reply.case_tag == SAP_WIT_INLINE_WORLD_RUN_REPLY_STATUS);
    CHECK(run_reply.val.status.is_v_ok == 1u);
    CHECK(environment_call_count == 1u);

    sap_wit_zero_inline_world_status_check_reply(&status_reply);
    rc = host_wasm_call_wit_export_endpoint(ctx,
                                            &sap_wit_inline_world_command_export_endpoints[1],
                                            &status_command,
                                            &status_reply);
    CHECK(rc == ERR_OK);
    CHECK(status_reply.case_tag == SAP_WIT_INLINE_WORLD_STATUS_CHECK_REPLY_STATUS);
    CHECK(status_reply.val.status.is_v_ok == 1u);

    host_wasm_destroy(ctx);
    free(wasm_bytes);
    printf("WASM WIT export bridge test OK.\n");
}
