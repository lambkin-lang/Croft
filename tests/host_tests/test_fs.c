#include "croft/host_fs.h"
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

#define TEST_FILE_PATH "test_fs_temp.txt"

static void test_file_io() {
    printf("Running file I/O test...\n");
    
    uint64_t fd;
    const char *payload = "Hello, Filesystem!";
    uint32_t payload_len = (uint32_t)strlen(payload);
    
    /* 1. Open for write */
    int32_t rc = host_fs_open(TEST_FILE_PATH, (uint32_t)strlen(TEST_FILE_PATH), HOST_FS_O_WRONLY | HOST_FS_O_CREAT | HOST_FS_O_TRUNC, &fd);
    CHECK(rc == HOST_FS_OK);
    
    /* 2. Write payload */
    uint32_t written = 0;
    rc = host_fs_write(fd, (const uint8_t*)payload, payload_len, &written);
    CHECK(rc == HOST_FS_OK);
    CHECK(written == payload_len);
    
    /* 3. Close */
    rc = host_fs_close(fd);
    CHECK(rc == HOST_FS_OK);
    
    /* 4. Open for read */
    rc = host_fs_open(TEST_FILE_PATH, (uint32_t)strlen(TEST_FILE_PATH), HOST_FS_O_RDONLY, &fd);
    CHECK(rc == HOST_FS_OK);
    
    /* 5. Check file size */
    uint64_t fsize = 0;
    rc = host_fs_file_size(fd, &fsize);
    CHECK(rc == HOST_FS_OK);
    CHECK(fsize == payload_len);
    
    /* 6. Read payload back */
    uint8_t buf[256] = {0};
    uint32_t read_bytes = 0;
    rc = host_fs_read(fd, buf, payload_len, &read_bytes);
    CHECK(rc == HOST_FS_OK);
    CHECK(read_bytes == payload_len);
    CHECK(memcmp(buf, payload, payload_len) == 0);
    
    /* 7. Close and delete */
    rc = host_fs_close(fd);
    CHECK(rc == HOST_FS_OK);
    remove(TEST_FILE_PATH);
}

static void test_not_found() {
    printf("Running file not found test...\n");
    
    uint64_t fd;
    int32_t rc = host_fs_open("does_not_exist_at_all.txt", 25, HOST_FS_O_RDONLY, &fd);
    CHECK(rc == HOST_FS_ERR_NOT_FOUND);
}

static void test_directories() {
    printf("Running directory path test...\n");
    char path[4096];
    uint32_t out_len;
    int32_t rc;

    rc = host_fs_get_config_dir(path, sizeof(path), &out_len);
    CHECK(rc == HOST_FS_OK);
    CHECK(out_len > 0);
    CHECK(path[0] != '\0');
    printf("  Config Dir: %s\n", path);

    rc = host_fs_get_cache_dir(path, sizeof(path), &out_len);
    CHECK(rc == HOST_FS_OK);
    CHECK(out_len > 0);
    CHECK(path[0] != '\0');
    printf("  Cache Dir: %s\n", path);

    rc = host_fs_get_resource_dir(path, sizeof(path), &out_len);
    CHECK(rc == HOST_FS_OK);
    CHECK(out_len > 0);
    CHECK(path[0] != '\0');
    printf("  Resource Dir: %s\n", path);
}

void run_test_fs(int argc, char **argv) {
    if (argc > 0) {
        /* Initialize the resource paths with our binary location */
        host_fs_init(argv[0]);
    }
    
    test_file_io();
    test_not_found();
    test_directories();
    
    printf("All host_fs tests passed successfully.\n");
}
