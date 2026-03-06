#include "croft/host_fs.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_path(const char* label, int32_t rc, const char* path, uint32_t len) {
    if (rc == HOST_FS_OK) {
        printf("%s=%.*s\n", label, (int)len, path);
    } else {
        printf("%s=<unavailable rc=%d>\n", label, rc);
    }
}

int main(int argc, char** argv) {
    const char* target_file = argc > 1 ? argv[1] : "README.md";
    uint64_t fd = 0;
    uint64_t size = 0;
    uint32_t read_bytes = 0;
    uint8_t preview[97];
    char config_dir[512];
    char cache_dir[512];
    char resource_dir[512];
    uint32_t out_len = 0;
    int32_t rc;

    memset(preview, 0, sizeof(preview));
    host_fs_init(argc > 0 ? argv[0] : NULL);

    rc = host_fs_get_config_dir(config_dir, sizeof(config_dir), &out_len);
    print_path("config_dir", rc, config_dir, out_len);

    rc = host_fs_get_cache_dir(cache_dir, sizeof(cache_dir), &out_len);
    print_path("cache_dir", rc, cache_dir, out_len);

    rc = host_fs_get_resource_dir(resource_dir, sizeof(resource_dir), &out_len);
    print_path("resource_dir", rc, resource_dir, out_len);

    rc = host_fs_open(target_file, (uint32_t)strlen(target_file), HOST_FS_O_RDONLY, &fd);
    if (rc != HOST_FS_OK) {
        fprintf(stderr, "example_fs_inspect: failed to open %s (%d)\n", target_file, rc);
        return 1;
    }

    rc = host_fs_file_size(fd, &size);
    if (rc != HOST_FS_OK) {
        fprintf(stderr, "example_fs_inspect: failed to stat %s (%d)\n", target_file, rc);
        host_fs_close(fd);
        return 1;
    }

    rc = host_fs_read(fd, preview, (uint32_t)(sizeof(preview) - 1), &read_bytes);
    host_fs_close(fd);
    if (rc != HOST_FS_OK) {
        fprintf(stderr, "example_fs_inspect: failed to read %s (%d)\n", target_file, rc);
        return 1;
    }

    printf("file=%s size=%llu preview_bytes=%u\n",
           target_file,
           (unsigned long long)size,
           read_bytes);
    printf("preview=\"%s\"\n", preview);
    return 0;
}
