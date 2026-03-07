#include "croft/host_wasm.h"
#include "croft/wit_text_program.h"
#include "croft/wit_text_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef TEXT_BRIDGE_GUEST_WASM_PATH
#define TEXT_BRIDGE_GUEST_WASM_PATH "text_bridge_guest.wasm"
#endif

static uint8_t* slurp_file(const char* path, uint32_t* out_len)
{
    FILE* file = NULL;
    uint8_t* bytes = NULL;
    long size = 0;
    size_t read_count = 0u;

    if (!path) {
        return NULL;
    }

    file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    bytes = (uint8_t*)malloc((size_t)size);
    if (!bytes) {
        fclose(file);
        return NULL;
    }

    read_count = fread(bytes, 1u, (size_t)size, file);
    fclose(file);
    if ((long)read_count != size) {
        free(bytes);
        return NULL;
    }

    if (out_len) {
        *out_len = (uint32_t)size;
    }
    return bytes;
}

int main(void)
{
    const char* base = "small binaries";
    const char* prefix = "Big analysis, ";
    croft_wit_text_runtime* text_runtime = NULL;
    croft_wit_text_program_host text_host = {0};
    croft_wit_owned_bytes message = {0};
    uint8_t* wasm_bytes = NULL;
    uint32_t wasm_len = 0u;
    host_wasm_ctx_t* wasm = NULL;
    uint8_t* memory = NULL;
    uint32_t memory_size = 0u;
    const uint32_t message_offset = 2048u;
    char offset_arg[32];
    char len_arg[32];
    const char* argv[2];
    int32_t rc = 1;
    int32_t wasm_rc = 0;

    text_runtime = croft_wit_text_runtime_create(NULL);
    if (!text_runtime) {
        goto cleanup;
    }

    text_host.userdata = text_runtime;
    text_host.dispatch = (croft_wit_text_program_dispatch_fn)croft_wit_text_runtime_dispatch;
    text_host.dispose_reply = croft_wit_text_reply_dispose;
    if (croft_wit_text_program_prepend(&text_host,
                                       (const uint8_t*)base,
                                       (uint32_t)strlen(base),
                                       (const uint8_t*)prefix,
                                       (uint32_t)strlen(prefix),
                                       &message) != 0) {
        goto cleanup;
    }

    wasm_bytes = slurp_file(TEXT_BRIDGE_GUEST_WASM_PATH, &wasm_len);
    if (!wasm_bytes) {
        fprintf(stderr, "example_wit_text_wasm_host: unable to read %s\n", TEXT_BRIDGE_GUEST_WASM_PATH);
        goto cleanup;
    }

    wasm = host_wasm_create(wasm_bytes, wasm_len, 64u * 1024u);
    if (!wasm) {
        fprintf(stderr, "example_wit_text_wasm_host: host_wasm_create failed\n");
        goto cleanup;
    }

    memory = host_wasm_get_memory(wasm, &memory_size);
    if (!memory || memory_size < message_offset + message.len) {
        fprintf(stderr, "example_wit_text_wasm_host: guest memory too small\n");
        goto cleanup;
    }
    memcpy(memory + message_offset, message.data, message.len);

    snprintf(offset_arg, sizeof(offset_arg), "%u", message_offset);
    snprintf(len_arg, sizeof(len_arg), "%u", message.len);
    argv[0] = offset_arg;
    argv[1] = len_arg;

    wasm_rc = host_wasm_call(wasm, "text_metric", 2, argv);
    if (wasm_rc != (int32_t)message.len + 42) {
        fprintf(stderr, "example_wit_text_wasm_host: unexpected return value %d\n", wasm_rc);
        goto cleanup;
    }

    printf("wasm-text=\"%.*s\" wasm_return=%d\n", (int)message.len, (const char*)message.data, wasm_rc);
    rc = 0;

cleanup:
    host_wasm_destroy(wasm);
    free(wasm_bytes);
    croft_wit_owned_bytes_dispose(&message);
    croft_wit_text_runtime_destroy(text_runtime);
    return rc;
}
