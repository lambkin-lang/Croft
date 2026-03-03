#ifndef CROFT_HOST_FS_H
#define CROFT_HOST_FS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/// host_fs configuration
/// To be called at startup, usually `argc > 0 ? argv[0] : NULL` to initialize resource paths.
void host_fs_init(const char *exe_path);

/// Wasm-shaped Error Codes
#define HOST_FS_OK                0
#define HOST_FS_ERR_NOT_FOUND    -1
#define HOST_FS_ERR_ACCES        -2
#define HOST_FS_ERR_IO           -3
#define HOST_FS_ERR_INVALID      -4

/// File Open Flags
#define HOST_FS_O_RDONLY          (1 << 0)
#define HOST_FS_O_WRONLY          (1 << 1)
#define HOST_FS_O_RDWR            (HOST_FS_O_RDONLY | HOST_FS_O_WRONLY)
#define HOST_FS_O_CREAT           (1 << 2)
#define HOST_FS_O_TRUNC           (1 << 3)
#define HOST_FS_O_APPEND          (1 << 4)

/**
 * Open a file and return a native file handle representation.
 * 
 * NOTE TO WASM GUEST BINDINGS: This uint64_t 'out_fd' is an unsandboxed native C memory
 * pointer cast to an integer. The WASM boundary shim MUST translate this pointer into
 * a secure integer-based handle table to prevent malicious intra-process memory access!
 */
int32_t host_fs_open(const char *path, uint32_t path_len, uint32_t flags, uint64_t *out_fd);

/**
 * Read from a file handle into a buffer.
 */
int32_t host_fs_read(uint64_t fd, uint8_t *buf, uint32_t len, uint32_t *out_read);

/**
 * Write to a file handle from a buffer.
 */
int32_t host_fs_write(uint64_t fd, const uint8_t *buf, uint32_t len, uint32_t *out_written);

/**
 * Close the file handle.
 */
int32_t host_fs_close(uint64_t fd);

/**
 * Stat the file size in bytes for a given handle.
 */
int32_t host_fs_file_size(uint64_t fd, uint64_t *out_size);

/**
 * Populate standard directory paths to Wasm callers.
 * The string will be written into `out_path` space up to `max_len` bytes.
 * The exact `out_len` of the written string (excluding a null terminator, but a 
 * null terminator will safely be written if space permits) is populated.
 */
int32_t host_fs_get_config_dir(char *out_path, uint32_t max_len, uint32_t *out_len);
int32_t host_fs_get_cache_dir(char *out_path, uint32_t max_len, uint32_t *out_len);
int32_t host_fs_get_resource_dir(char *out_path, uint32_t max_len, uint32_t *out_len);


/**
 * Ensure the directory at `path` exists. Nested directories are NOT recursively created.
 */
int32_t host_fs_mkdir(const char *path, uint32_t path_len);


#ifdef __cplusplus
}
#endif

#endif /* CROFT_HOST_FS_H */
