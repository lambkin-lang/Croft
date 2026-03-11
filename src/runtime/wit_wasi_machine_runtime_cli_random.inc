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

static int32_t croft_wit_wasi_dispatch_cli_get_stdin(void* ctx,
                                                     SapWitCliStdinReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;

    if (!runtime || !reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_cli_stdin_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLI_STDIN_REPLY_INPUT_STREAM;
    reply_out->val.input_stream =
        (SapWitCliInputStreamResource)runtime->stdin_stream_handle;
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_cli_get_stdout(void* ctx,
                                                      SapWitCliStdoutReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;

    if (!runtime || !reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_cli_stdout_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLI_STDOUT_REPLY_OUTPUT_STREAM;
    reply_out->val.output_stream =
        (SapWitCliOutputStreamResource)runtime->stdout_stream_handle;
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_cli_get_stderr(void* ctx,
                                                      SapWitCliStderrReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;

    if (!runtime || !reply_out) {
        return ERR_INVALID;
    }
    sap_wit_zero_cli_stderr_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLI_STDERR_REPLY_OUTPUT_STREAM;
    reply_out->val.output_stream =
        (SapWitCliOutputStreamResource)runtime->stderr_stream_handle;
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_cli_get_terminal_stdin(
    void* ctx,
    SapWitCliTerminalStdinReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* descriptor = NULL;

    if (!runtime || !reply_out) {
        return ERR_INVALID;
    }
    if (runtime->stdin_descriptor_handle != 0u) {
        descriptor = croft_wit_wasi_fs_descriptor_lookup(runtime, runtime->stdin_descriptor_handle);
    }

    sap_wit_zero_cli_terminal_stdin_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLI_TERMINAL_STDIN_REPLY_GET_TERMINAL_STDIN;
    if (descriptor && descriptor->is_terminal) {
        reply_out->val.get_terminal_stdin.has_v = 1u;
        reply_out->val.get_terminal_stdin.v =
            (SapWitCliTerminalInputResource)runtime->stdin_descriptor_handle;
    }
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_cli_get_terminal_stdout(
    void* ctx,
    SapWitCliTerminalStdoutReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* descriptor = NULL;

    if (!runtime || !reply_out) {
        return ERR_INVALID;
    }
    if (runtime->stdout_descriptor_handle != 0u) {
        descriptor =
            croft_wit_wasi_fs_descriptor_lookup(runtime, runtime->stdout_descriptor_handle);
    }

    sap_wit_zero_cli_terminal_stdout_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLI_TERMINAL_STDOUT_REPLY_GET_TERMINAL_STDOUT;
    if (descriptor && descriptor->is_terminal) {
        reply_out->val.get_terminal_stdout.has_v = 1u;
        reply_out->val.get_terminal_stdout.v =
            (SapWitCliTerminalOutputResource)runtime->stdout_descriptor_handle;
    }
    return ERR_OK;
}

static int32_t croft_wit_wasi_dispatch_cli_get_terminal_stderr(
    void* ctx,
    SapWitCliTerminalStderrReply* reply_out)
{
    croft_wit_wasi_machine_runtime* runtime = (croft_wit_wasi_machine_runtime*)ctx;
    croft_wit_wasi_descriptor_slot* descriptor = NULL;

    if (!runtime || !reply_out) {
        return ERR_INVALID;
    }
    if (runtime->stderr_descriptor_handle != 0u) {
        descriptor =
            croft_wit_wasi_fs_descriptor_lookup(runtime, runtime->stderr_descriptor_handle);
    }

    sap_wit_zero_cli_terminal_stderr_reply(reply_out);
    reply_out->case_tag = SAP_WIT_CLI_TERMINAL_STDERR_REPLY_GET_TERMINAL_STDERR;
    if (descriptor && descriptor->is_terminal) {
        reply_out->val.get_terminal_stderr.has_v = 1u;
        reply_out->val.get_terminal_stderr.v =
            (SapWitCliTerminalOutputResource)runtime->stderr_descriptor_handle;
    }
    return ERR_OK;
}


static const SapWitCliEnvironmentDispatchOps g_croft_wit_wasi_cli_environment_ops = {
    .get_environment = croft_wit_wasi_dispatch_cli_get_environment,
    .get_arguments = croft_wit_wasi_dispatch_cli_get_arguments,
    .initial_cwd = croft_wit_wasi_dispatch_cli_initial_cwd,
};

static const SapWitCliStdinDispatchOps g_croft_wit_wasi_cli_stdin_ops = {
    .get_stdin = croft_wit_wasi_dispatch_cli_get_stdin,
};

static const SapWitCliStdoutDispatchOps g_croft_wit_wasi_cli_stdout_ops = {
    .get_stdout = croft_wit_wasi_dispatch_cli_get_stdout,
};

static const SapWitCliStderrDispatchOps g_croft_wit_wasi_cli_stderr_ops = {
    .get_stderr = croft_wit_wasi_dispatch_cli_get_stderr,
};

static const SapWitCliTerminalStdinDispatchOps g_croft_wit_wasi_cli_terminal_stdin_ops = {
    .get_terminal_stdin = croft_wit_wasi_dispatch_cli_get_terminal_stdin,
};

static const SapWitCliTerminalStdoutDispatchOps g_croft_wit_wasi_cli_terminal_stdout_ops = {
    .get_terminal_stdout = croft_wit_wasi_dispatch_cli_get_terminal_stdout,
};

static const SapWitCliTerminalStderrDispatchOps g_croft_wit_wasi_cli_terminal_stderr_ops = {
    .get_terminal_stderr = croft_wit_wasi_dispatch_cli_get_terminal_stderr,
};

static const croft_wit_wasi_endpoint_binding g_croft_wit_wasi_cli_bundle_bindings[] = {
    {"environment", &g_croft_wit_wasi_cli_environment_ops},
    {"stdin", &g_croft_wit_wasi_cli_stdin_ops},
    {"stdout", &g_croft_wit_wasi_cli_stdout_ops},
    {"stderr", &g_croft_wit_wasi_cli_stderr_ops},
    {"terminal-stdin", &g_croft_wit_wasi_cli_terminal_stdin_ops},
    {"terminal-stdout", &g_croft_wit_wasi_cli_terminal_stdout_ops},
    {"terminal-stderr", &g_croft_wit_wasi_cli_terminal_stderr_ops},
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

static const croft_wit_wasi_endpoint_binding g_croft_wit_wasi_random_bundle_bindings[] = {
    {"random", &g_croft_wit_wasi_random_ops},
    {"insecure", &g_croft_wit_wasi_random_insecure_ops},
    {"insecure-seed", &g_croft_wit_wasi_random_insecure_seed_ops},
};


int croft_wit_wasi_machine_runtime_bind_cli_command_imports(
    croft_wit_wasi_machine_runtime* runtime,
    SapWitCliCommandWorldImports* bindings)
{
    if (!runtime || !bindings) {
        return ERR_INVALID;
    }
    memset(bindings, 0, sizeof(*bindings));
    return croft_wit_wasi_bind_endpoint_group(
        bindings,
        sap_wit_cli_command_import_endpoints,
        sap_wit_cli_command_import_endpoints_count,
        runtime,
        g_croft_wit_wasi_cli_bundle_bindings,
        sizeof(g_croft_wit_wasi_cli_bundle_bindings)
            / sizeof(g_croft_wit_wasi_cli_bundle_bindings[0]));
}

int croft_wit_wasi_machine_runtime_bind_cli_imports(
    croft_wit_wasi_machine_runtime* runtime,
    SapWitCliImportsWorldImports* bindings)
{
    if (!runtime || !bindings) {
        return ERR_INVALID;
    }
    memset(bindings, 0, sizeof(*bindings));
    return croft_wit_wasi_bind_endpoint_group(
        bindings,
        sap_wit_cli_imports_import_endpoints,
        sap_wit_cli_imports_import_endpoints_count,
        runtime,
        g_croft_wit_wasi_cli_bundle_bindings,
        sizeof(g_croft_wit_wasi_cli_bundle_bindings)
            / sizeof(g_croft_wit_wasi_cli_bundle_bindings[0]));
}

int croft_wit_wasi_machine_runtime_bind_random_imports(
    croft_wit_wasi_machine_runtime* runtime,
    SapWitRandomImportsWorldImports* bindings)
{
    if (!runtime || !bindings) {
        return ERR_INVALID;
    }
    memset(bindings, 0, sizeof(*bindings));
    return croft_wit_wasi_bind_endpoint_group(
        bindings,
        sap_wit_random_imports_import_endpoints,
        sap_wit_random_imports_import_endpoints_count,
        runtime,
        g_croft_wit_wasi_random_bundle_bindings,
        sizeof(g_croft_wit_wasi_random_bundle_bindings)
            / sizeof(g_croft_wit_wasi_random_bundle_bindings[0]));
}
