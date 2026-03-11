#if !defined(CROFT_OS_WINDOWS)
static int croft_wit_wasi_init_preopens(
    croft_wit_wasi_machine_runtime* runtime,
    const croft_wit_wasi_machine_runtime_options* options)
{
    size_t i;

    if (!runtime || !options) {
        return ERR_INVALID;
    }
    if (options->preopen_count > 0u) {
        if (!options->preopens) {
            return ERR_INVALID;
        }
        for (i = 0u; i < options->preopen_count; i++) {
            const char* host_path = options->preopens[i].host_path;
            const char* guest_path = options->preopens[i].guest_path;
            int rc;

            if (!host_path) {
                return ERR_INVALID;
            }
            rc = croft_wit_wasi_fs_add_preopen(runtime, host_path, guest_path ? guest_path : host_path);
            if (rc != ERR_OK) {
                return rc;
            }
        }
    } else if (options->inherit_preopen_cwd) {
        char cwd[PATH_MAX];
        int rc;

        if (getcwd(cwd, sizeof(cwd))) {
            rc = croft_wit_wasi_fs_add_preopen(runtime, cwd, ".");
            if (rc != ERR_OK) {
                return rc;
            }
        }
    }
    return croft_wit_wasi_fs_build_preopen_blob(runtime);
}
#endif

void croft_wit_wasi_machine_runtime_options_default(
    croft_wit_wasi_machine_runtime_options* options)
{
    if (!options) {
        return;
    }
    memset(options, 0, sizeof(*options));
    options->inherit_environment = 1u;
    options->inherit_preopen_cwd = 1u;
}

croft_wit_wasi_machine_runtime* croft_wit_wasi_machine_runtime_create(
    const croft_wit_wasi_machine_runtime_options* options)
{
    croft_wit_wasi_machine_runtime_options local = {0};
    croft_wit_wasi_machine_runtime* runtime;
    const char* const* envp = NULL;
    uint32_t envc = 0u;
    int rc;

    croft_wit_wasi_machine_runtime_options_default(&local);
    if (options) {
        local = *options;
    }

    runtime = (croft_wit_wasi_machine_runtime*)calloc(1u, sizeof(*runtime));
    if (!runtime) {
        return NULL;
    }
    runtime->next_pollable_handle = 1u;

    rc = croft_wit_wasi_build_argv_blob(runtime, local.argv, local.argc);
    if (rc != ERR_OK) {
        croft_wit_wasi_machine_runtime_destroy(runtime);
        return NULL;
    }

    if (local.envp) {
        envp = local.envp;
        envc = local.envc;
    } else if (local.inherit_environment) {
        envp = (const char* const*)environ;
        envc = 0u;
    }

    rc = croft_wit_wasi_build_env_blob(runtime, envp, envc);
    if (rc != ERR_OK) {
        croft_wit_wasi_machine_runtime_destroy(runtime);
        return NULL;
    }

    rc = croft_wit_wasi_init_initial_cwd(runtime, local.initial_cwd);
    if (rc != ERR_OK) {
        croft_wit_wasi_machine_runtime_destroy(runtime);
        return NULL;
    }

#if !defined(CROFT_OS_WINDOWS)
    rc = croft_wit_wasi_init_preopens(runtime, &local);
    if (rc != ERR_OK) {
        croft_wit_wasi_machine_runtime_destroy(runtime);
        return NULL;
    }
#endif

    return runtime;
}

void croft_wit_wasi_machine_runtime_destroy(croft_wit_wasi_machine_runtime* runtime)
{
    size_t i;

    if (!runtime) {
        return;
    }

#if !defined(CROFT_OS_WINDOWS)
    for (i = 0u; i < runtime->directory_stream_count; i++) {
        if (runtime->directory_streams[i].live && runtime->directory_streams[i].dir) {
            closedir(runtime->directory_streams[i].dir);
        }
    }
    for (i = 0u; i < runtime->descriptor_count; i++) {
        if (runtime->descriptors[i].live && runtime->descriptors[i].fd >= 0) {
            close(runtime->descriptors[i].fd);
        }
    }
    for (i = 0u; i < runtime->preopen_count; i++) {
        free(runtime->preopens[i].guest_path);
    }
#endif
    for (i = 0u; i < runtime->error_count; i++) {
        free(runtime->errors[i].debug_string);
    }
    free(runtime->env_blob.data);
    free(runtime->argv_blob.data);
    free(runtime->random_blob.data);
    free(runtime->poll_indexes_blob.data);
    free(runtime->filesystem_blob.data);
    free(runtime->preopen_blob.data);
    free(runtime->initial_cwd);
    free(runtime->pollables);
    free(runtime->descriptors);
    free(runtime->input_streams);
    free(runtime->output_streams);
    free(runtime->directory_streams);
    free(runtime->errors);
    free(runtime->preopens);
    free(runtime);
}
