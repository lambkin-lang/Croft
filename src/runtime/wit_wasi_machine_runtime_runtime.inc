#if !defined(CROFT_OS_WINDOWS)
static int croft_wit_wasi_init_stdio_input(croft_wit_wasi_machine_runtime* runtime,
                                           int source_fd,
                                           uint32_t descriptor_flags,
                                           uint32_t* descriptor_handle_out,
                                           uint32_t* stream_handle_out)
{
    int dup_fd;
    uint32_t descriptor_handle = 0u;
    uint32_t stream_handle = 0u;
    int rc;

    if (!runtime || !descriptor_handle_out || !stream_handle_out || source_fd < 0) {
        return ERR_INVALID;
    }

    dup_fd = dup(source_fd);
    if (dup_fd < 0) {
        return ERR_INVALID;
    }

    rc = croft_wit_wasi_fs_descriptor_insert(runtime, dup_fd, descriptor_flags, 0u, &descriptor_handle);
    if (rc != ERR_OK) {
        close(dup_fd);
        return rc;
    }
    croft_wit_wasi_fs_descriptor_lookup(runtime, descriptor_handle)->error_domain =
        CROFT_WIT_WASI_ERROR_DOMAIN_IO;
    rc = croft_wit_wasi_input_stream_insert(runtime, descriptor_handle, 0u, &stream_handle);
    if (rc != ERR_OK) {
        croft_wit_wasi_fs_descriptor_lookup(runtime, descriptor_handle)->live = 0u;
        close(dup_fd);
        return rc;
    }

    *descriptor_handle_out = descriptor_handle;
    *stream_handle_out = stream_handle;
    return ERR_OK;
}

static int croft_wit_wasi_init_stdio_output(croft_wit_wasi_machine_runtime* runtime,
                                            int source_fd,
                                            uint32_t descriptor_flags,
                                            uint32_t* descriptor_handle_out,
                                            uint32_t* stream_handle_out)
{
    int dup_fd;
    uint32_t descriptor_handle = 0u;
    uint32_t stream_handle = 0u;
    int rc;

    if (!runtime || !descriptor_handle_out || !stream_handle_out || source_fd < 0) {
        return ERR_INVALID;
    }

    dup_fd = dup(source_fd);
    if (dup_fd < 0) {
        return ERR_INVALID;
    }

    rc = croft_wit_wasi_fs_descriptor_insert(runtime, dup_fd, descriptor_flags, 0u, &descriptor_handle);
    if (rc != ERR_OK) {
        close(dup_fd);
        return rc;
    }
    croft_wit_wasi_fs_descriptor_lookup(runtime, descriptor_handle)->error_domain =
        CROFT_WIT_WASI_ERROR_DOMAIN_IO;
    rc = croft_wit_wasi_output_stream_insert(runtime, descriptor_handle, 0u, 0u, &stream_handle);
    if (rc != ERR_OK) {
        croft_wit_wasi_fs_descriptor_lookup(runtime, descriptor_handle)->live = 0u;
        close(dup_fd);
        return rc;
    }

    *descriptor_handle_out = descriptor_handle;
    *stream_handle_out = stream_handle;
    return ERR_OK;
}

static int croft_wit_wasi_init_stdio(croft_wit_wasi_machine_runtime* runtime,
                                     const croft_wit_wasi_machine_runtime_options* options)
{
    int stdin_fd = -1;
    int stdout_fd = -1;
    int stderr_fd = -1;
    int rc;

    if (!runtime || !options) {
        return ERR_INVALID;
    }

    if (options->stdin_fd >= 0) {
        stdin_fd = options->stdin_fd;
    } else if (options->inherit_stdio) {
        stdin_fd = STDIN_FILENO;
    }
    if (options->stdout_fd >= 0) {
        stdout_fd = options->stdout_fd;
    } else if (options->inherit_stdio) {
        stdout_fd = STDOUT_FILENO;
    }
    if (options->stderr_fd >= 0) {
        stderr_fd = options->stderr_fd;
    } else if (options->inherit_stdio) {
        stderr_fd = STDERR_FILENO;
    }

    if (stdin_fd >= 0) {
        rc = croft_wit_wasi_init_stdio_input(runtime,
                                             stdin_fd,
                                             SAP_WIT_FILESYSTEM_DESCRIPTOR_FLAGS_READ,
                                             &runtime->stdin_descriptor_handle,
                                             &runtime->stdin_stream_handle);
        if (rc != ERR_OK) {
            return rc;
        }
    }
    if (stdout_fd >= 0) {
        rc = croft_wit_wasi_init_stdio_output(runtime,
                                              stdout_fd,
                                              SAP_WIT_FILESYSTEM_DESCRIPTOR_FLAGS_WRITE,
                                              &runtime->stdout_descriptor_handle,
                                              &runtime->stdout_stream_handle);
        if (rc != ERR_OK) {
            return rc;
        }
    }
    if (stderr_fd >= 0) {
        rc = croft_wit_wasi_init_stdio_output(runtime,
                                              stderr_fd,
                                              SAP_WIT_FILESYSTEM_DESCRIPTOR_FLAGS_WRITE,
                                              &runtime->stderr_descriptor_handle,
                                              &runtime->stderr_stream_handle);
        if (rc != ERR_OK) {
            return rc;
        }
    }

    return ERR_OK;
}

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
    options->stdin_fd = -1;
    options->stdout_fd = -1;
    options->stderr_fd = -1;
    options->inherit_environment = 1u;
    options->inherit_stdio = 1u;
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

    rc = croft_wit_wasi_init_stdio(runtime, &local);
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
