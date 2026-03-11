static int32_t croft_wit_wasi_dispatch_get_random_bytes(void* ctx,
                                                        const SapWitRandomGetRandomBytes* payload,
                                                        SapWitRandomReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    int rc;

    if (!runtime || !payload || !reply_out || payload->len > UINT32_MAX) {
        return ERR_INVALID;
    }

    runtime->random_blob.len = 0u;
    rc = croft_wit_wasi_buffer_reserve(&runtime->random_blob, (uint32_t)payload->len);
    if (rc != ERR_OK) {
        return rc;
    }
    runtime->random_blob.len = (uint32_t)payload->len;

    rc = croft_wit_wasi_random_fill(runtime->random_blob.data, runtime->random_blob.len);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_wit_wasi_reply_random_bytes(reply_out, &runtime->random_blob);
}

static int32_t croft_wit_wasi_dispatch_get_random_u64(void* ctx, SapWitRandomReply* reply_out)
{
    uint64_t value = 0u;
    int rc;

    if (!reply_out) {
        return ERR_INVALID;
    }
    rc = croft_wit_wasi_random_fill(&value, (uint32_t)sizeof(value));
    if (rc != ERR_OK) {
        return rc;
    }

    sap_wit_zero_random_reply(reply_out);
    reply_out->case_tag = SAP_WIT_RANDOM_REPLY_GET_RANDOM_U64;
    reply_out->val.get_random_u64 = value;
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_get_insecure_random_bytes(
    void* ctx,
    const SapWitRandomGetInsecureRandomBytes* payload,
    SapWitRandomInsecureReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    int rc;

    if (!runtime || !payload || !reply_out || payload->len > UINT32_MAX) {
        return ERR_INVALID;
    }

    runtime->random_blob.len = 0u;
    rc = croft_wit_wasi_buffer_reserve(&runtime->random_blob, (uint32_t)payload->len);
    if (rc != ERR_OK) {
        return rc;
    }
    runtime->random_blob.len = (uint32_t)payload->len;

    rc = croft_wit_wasi_random_fill(runtime->random_blob.data, runtime->random_blob.len);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_wit_wasi_reply_insecure_random_bytes(reply_out, &runtime->random_blob);
}

static int32_t croft_wit_wasi_dispatch_get_insecure_random_u64(void* ctx,
                                                               SapWitRandomInsecureReply* reply_out)
{
    uint64_t value = 0u;
    int rc;

    (void)ctx;
    if (!reply_out) {
        return ERR_INVALID;
    }
    rc = croft_wit_wasi_random_fill(&value, (uint32_t)sizeof(value));
    if (rc != ERR_OK) {
        return rc;
    }

    sap_wit_zero_random_insecure_reply(reply_out);
    reply_out->case_tag = SAP_WIT_RANDOM_INSECURE_REPLY_GET_INSECURE_RANDOM_U64;
    reply_out->val.get_insecure_random_u64 = value;
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_insecure_seed(void* ctx,
                                                     SapWitRandomInsecureSeedReply* reply_out)
{
    uint64_t seed[2] = {0u, 0u};
    int rc;

    (void)ctx;
    if (!reply_out) {
        return ERR_INVALID;
    }
    rc = croft_wit_wasi_random_fill(seed, (uint32_t)sizeof(seed));
    if (rc != ERR_OK) {
        return rc;
    }

    sap_wit_zero_random_insecure_seed_reply(reply_out);
    reply_out->case_tag = SAP_WIT_RANDOM_INSECURE_SEED_REPLY_INSECURE_SEED;
    reply_out->val.insecure_seed.v_0 = seed[0];
    reply_out->val.insecure_seed.v_1 = seed[1];
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_cli_get_environment(void* ctx,
                                                           SapWitCliEnvironmentReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;

    if (!runtime || !reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_cli_environment_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLI_ENVIRONMENT_REPLY_GET_ENVIRONMENT;
    reply_out->val.get_environment.data = runtime->env_blob.data;
    reply_out->val.get_environment.len = runtime->env_count;
    reply_out->val.get_environment.byte_len = runtime->env_blob.len;
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_cli_get_arguments(void* ctx,
                                                         SapWitCliEnvironmentReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;

    if (!runtime || !reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_cli_environment_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLI_ENVIRONMENT_REPLY_GET_ARGUMENTS;
    reply_out->val.get_arguments.data = runtime->argv_blob.data;
    reply_out->val.get_arguments.len = runtime->argc;
    reply_out->val.get_arguments.byte_len = runtime->argv_blob.len;
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_cli_initial_cwd(void* ctx,
                                                       SapWitCliEnvironmentReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;

    if (!runtime || !reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_cli_environment_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLI_ENVIRONMENT_REPLY_INITIAL_CWD;
    if (runtime->initial_cwd) {
        reply_out->val.initial_cwd.has_v = 1u;
        reply_out->val.initial_cwd.v_data = (const uint8_t*)runtime->initial_cwd;
        reply_out->val.initial_cwd.v_len = (uint32_t)strlen(runtime->initial_cwd);
    }
    return ERR_OK;
}


static const SapWitCliEnvironmentDispatchOps g_croft_wit_wasi_cli_environment_ops = {
    .get_environment = croft_wit_wasi_dispatch_cli_get_environment,
    .get_arguments = croft_wit_wasi_dispatch_cli_get_arguments,
    .initial_cwd = croft_wit_wasi_dispatch_cli_initial_cwd,
};

static const SapWitRandomDispatchOps g_croft_wit_wasi_random_ops = {
    .get_random_bytes = croft_wit_wasi_dispatch_get_random_bytes,
    .get_random_u64 = croft_wit_wasi_dispatch_get_random_u64,
};

static const SapWitRandomInsecureDispatchOps g_croft_wit_wasi_random_insecure_ops = {
    .get_insecure_random_bytes = croft_wit_wasi_dispatch_get_insecure_random_bytes,
    .get_insecure_random_u64 = croft_wit_wasi_dispatch_get_insecure_random_u64,
};

static const SapWitRandomInsecureSeedDispatchOps g_croft_wit_wasi_random_insecure_seed_ops = {
    .insecure_seed = croft_wit_wasi_dispatch_insecure_seed,
};


int croft_wit_wasi_machine_runtime_bind_cli_command_imports(
    croft_wit_wasi_machine_runtime* runtime,
    SapWitCliCommandWorldImports* bindings)
{
    int rc;

    if (!runtime || !bindings) {
        return ERR_INVALID;
    }
    memset(bindings, 0, sizeof(*bindings));
    rc = croft_wit_wasi_bind_endpoint(bindings,
                                      sap_wit_cli_command_import_endpoints,
                                      sap_wit_cli_command_import_endpoints_count,
                                      "environment",
                                      runtime,
                                      &g_croft_wit_wasi_cli_environment_ops);
    return rc;
}

int croft_wit_wasi_machine_runtime_bind_cli_imports(
    croft_wit_wasi_machine_runtime* runtime,
    SapWitCliImportsWorldImports* bindings)
{
    int rc;

    if (!runtime || !bindings) {
        return ERR_INVALID;
    }
    memset(bindings, 0, sizeof(*bindings));
    rc = croft_wit_wasi_bind_endpoint(bindings,
                                      sap_wit_cli_imports_import_endpoints,
                                      sap_wit_cli_imports_import_endpoints_count,
                                      "environment",
                                      runtime,
                                      &g_croft_wit_wasi_cli_environment_ops);
    return rc;
}

int croft_wit_wasi_machine_runtime_bind_random_imports(
    croft_wit_wasi_machine_runtime* runtime,
    SapWitRandomImportsWorldImports* bindings)
{
    int rc;

    if (!runtime || !bindings) {
        return ERR_INVALID;
    }
    memset(bindings, 0, sizeof(*bindings));

    rc = croft_wit_wasi_bind_endpoint(bindings,
                                      sap_wit_random_imports_import_endpoints,
                                      sap_wit_random_imports_import_endpoints_count,
                                      "random",
                                      runtime,
                                      &g_croft_wit_wasi_random_ops);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = croft_wit_wasi_bind_endpoint(bindings,
                                      sap_wit_random_imports_import_endpoints,
                                      sap_wit_random_imports_import_endpoints_count,
                                      "insecure",
                                      runtime,
                                      &g_croft_wit_wasi_random_insecure_ops);
    if (rc != ERR_OK) {
        return rc;
    }
    return croft_wit_wasi_bind_endpoint(bindings,
                                        sap_wit_random_imports_import_endpoints,
                                        sap_wit_random_imports_import_endpoints_count,
                                        "insecure-seed",
                                        runtime,
                                        &g_croft_wit_wasi_random_insecure_seed_ops);
}

