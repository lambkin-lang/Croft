#include "croft/host_wasm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #expr, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

#ifndef DUMMY_GUEST_WASM_PATH
#define DUMMY_GUEST_WASM_PATH "dummy_guest.wasm"
#endif

static uint8_t *slurp_file(const char *path, uint32_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = malloc(sz);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    
    size_t nr = fread(buf, 1, sz, f);
    fclose(f);
    if ((long)nr != sz) {
        free(buf);
        return NULL;
    }

    if (out_len) *out_len = (uint32_t)sz;
    return buf;
}

void run_test_wasm_guest(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("Loading WASM module from: %s\n", DUMMY_GUEST_WASM_PATH);
    uint32_t wasm_len = 0;
    uint8_t *wasm_bytes = slurp_file(DUMMY_GUEST_WASM_PATH, &wasm_len);
    
    if (!wasm_bytes) {
        printf("SKIP: %s not found (likely compiler was not available).\n", DUMMY_GUEST_WASM_PATH);
        return;
    }
    
    /* 64k stack size */
    host_wasm_ctx_t *ctx = host_wasm_create(wasm_bytes, wasm_len, 64 * 1024);
    CHECK(ctx != NULL);
    
    /* Call test_function_a(10) -> expects 52 back and 'host_log' to have been called */
    /* Since we pass arguments as void*, Wasm3 requires us to pass pointer to string representations */
    const char *args[] = { "10" };
    int32_t rc = host_wasm_call(ctx, "test_function_a", 1, args);
    
    /* Note: Wasm3 `m3_Call` stringizes arguments natively on its C-API, but we wrote `host_wasm_call`
       to use it. We should ensure rc is 52. */
    CHECK(rc == 52);
    
    /* Test runner integration stub */
    uint32_t reply_len = 0;
    rc = host_wasm_runner_logic(ctx, NULL, (const uint8_t*)"1", 1, NULL, 0, &reply_len);
    CHECK(rc == 0);

    host_wasm_destroy(ctx);
    free(wasm_bytes);
    printf("WASM integration test OK.\n");
}
