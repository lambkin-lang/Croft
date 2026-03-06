#include "croft/host_wasm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef DUMMY_GUEST_WASM_PATH
#define DUMMY_GUEST_WASM_PATH "dummy_guest.wasm"
#endif

static uint8_t* slurp_file(const char* path, uint32_t* out_len) {
    FILE* file = fopen(path, "rb");
    uint8_t* bytes = NULL;
    long size = 0;
    size_t read_count = 0;

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

    read_count = fread(bytes, 1, (size_t)size, file);
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

int main(void) {
    uint32_t wasm_len = 0;
    uint8_t* wasm_bytes = slurp_file(DUMMY_GUEST_WASM_PATH, &wasm_len);
    host_wasm_ctx_t* ctx = NULL;
    const char* args[] = { "10" };
    uint32_t reply_len = 0;
    int32_t rc;

    if (!wasm_bytes) {
        fprintf(stderr, "example_wasm_guest: unable to read %s\n", DUMMY_GUEST_WASM_PATH);
        return 1;
    }

    ctx = host_wasm_create(wasm_bytes, wasm_len, 64 * 1024);
    if (!ctx) {
        fprintf(stderr, "example_wasm_guest: host_wasm_create failed\n");
        free(wasm_bytes);
        return 1;
    }

    rc = host_wasm_call(ctx, "test_function_a", 1, args);
    if (rc != 52) {
        fprintf(stderr, "example_wasm_guest: unexpected return value %d\n", rc);
        host_wasm_destroy(ctx);
        free(wasm_bytes);
        return 1;
    }

    rc = host_wasm_runner_logic(ctx, NULL, (const uint8_t*)"1", 1, NULL, 0, &reply_len);
    if (rc != 0) {
        fprintf(stderr, "example_wasm_guest: host_wasm_runner_logic failed (%d)\n", rc);
        host_wasm_destroy(ctx);
        free(wasm_bytes);
        return 1;
    }

    printf("wasm_return=%d reply_len=%u\n", rc == 0 ? 52 : rc, reply_len);

    host_wasm_destroy(ctx);
    free(wasm_bytes);
    return 0;
}
