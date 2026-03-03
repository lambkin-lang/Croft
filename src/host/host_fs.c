#include "croft/host_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <sys/syslimits.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static char g_exe_dir[PATH_MAX] = {0};

/* ------------------------------------------------------------------ */
/* I/O Wrappers                                                       */
/* ------------------------------------------------------------------ */

static const char *flags_to_mode(uint32_t flags) {
    if (flags & HOST_FS_O_RDWR) {
        if (flags & HOST_FS_O_CREAT) {
            if (flags & HOST_FS_O_TRUNC) return "w+b";
            if (flags & HOST_FS_O_APPEND) return "a+b";
            return "r+b"; 
        } else {
            return "r+b";
        }
    } else if (flags & HOST_FS_O_WRONLY) {
        if (flags & HOST_FS_O_APPEND) return "ab";
        if (flags & HOST_FS_O_TRUNC) return "wb";
        if (flags & HOST_FS_O_CREAT) return "wb";
        return "wb"; /* fallback */
    } else {
        /* Default to read only */
        return "rb";
    }
}

int32_t host_fs_open(const char *path, uint32_t path_len, uint32_t flags, uint64_t *out_fd) {
    if (!path || !out_fd || path_len == 0 || path_len >= PATH_MAX) return HOST_FS_ERR_INVALID;
    
    char cpath[PATH_MAX];
    memcpy(cpath, path, path_len);
    cpath[path_len] = '\0';

    const char *mode = flags_to_mode(flags);
    FILE *f = fopen(cpath, mode);
    
    if (!f) {
        if (errno == ENOENT) return HOST_FS_ERR_NOT_FOUND;
        if (errno == EACCES) return HOST_FS_ERR_ACCES;
        return HOST_FS_ERR_IO;
    }
    
    *out_fd = (uint64_t)(uintptr_t)f;
    return HOST_FS_OK;
}

int32_t host_fs_read(uint64_t fd, uint8_t *buf, uint32_t len, uint32_t *out_read) {
    FILE *f = (FILE *)(uintptr_t)fd;
    if (!f || !buf) return HOST_FS_ERR_INVALID;
    if (len == 0) {
        if (out_read) *out_read = 0;
        return HOST_FS_OK;
    }
    
    size_t nr = fread(buf, 1, (size_t)len, f);
    if (out_read) *out_read = (uint32_t)nr;
    
    if (ferror(f)) return HOST_FS_ERR_IO;
    return HOST_FS_OK;
}

int32_t host_fs_write(uint64_t fd, const uint8_t *buf, uint32_t len, uint32_t *out_written) {
    FILE *f = (FILE *)(uintptr_t)fd;
    if (!f || !buf) return HOST_FS_ERR_INVALID;
    if (len == 0) {
        if (out_written) *out_written = 0;
        return HOST_FS_OK;
    }
    
    size_t nw = fwrite(buf, 1, (size_t)len, f);
    if (out_written) *out_written = (uint32_t)nw;
    
    if (nw < len) return HOST_FS_ERR_IO;
    return HOST_FS_OK;
}

int32_t host_fs_close(uint64_t fd) {
    FILE *f = (FILE *)(uintptr_t)fd;
    if (!f) return HOST_FS_ERR_INVALID;
    
    if (fclose(f) != 0) return HOST_FS_ERR_IO;
    return HOST_FS_OK;
}

int32_t host_fs_file_size(uint64_t fd, uint64_t *out_size) {
    FILE *f = (FILE *)(uintptr_t)fd;
    if (!f || !out_size) return HOST_FS_ERR_INVALID;
    
    long cur = ftell(f);
    if (cur < 0) return HOST_FS_ERR_IO;
    
    if (fseek(f, 0, SEEK_END) != 0) return HOST_FS_ERR_IO;
    long end = ftell(f);
    if (end < 0) {
        fseek(f, cur, SEEK_SET); /* best effort restore */
        return HOST_FS_ERR_IO;
    }
    
    if (fseek(f, cur, SEEK_SET) != 0) return HOST_FS_ERR_IO;
    
    *out_size = (uint64_t)end;
    return HOST_FS_OK;
}

/* ------------------------------------------------------------------ */
/* Directory Path Helpers                                              */
/* ------------------------------------------------------------------ */

void host_fs_init(const char *exe_path) {
    if (!exe_path) {
        g_exe_dir[0] = '.';
        g_exe_dir[1] = '\0';
        return;
    }

    size_t len = strlen(exe_path);
    if (len >= PATH_MAX) return;

    /* find last slash and terminate there for directory */
    const char *last_slash = exe_path + len;
    while (last_slash > exe_path && *last_slash != '/' && *last_slash != '\\') {
        last_slash--;
    }

    if (last_slash == exe_path && *last_slash != '/' && *last_slash != '\\') {
        /* no slash found, assume current directory */
        g_exe_dir[0] = '.';
        g_exe_dir[1] = '\0';
    } else {
        size_t dirlen = (size_t)(last_slash - exe_path);
        if (dirlen == 0) dirlen = 1; /* keep the root slash */
        memcpy(g_exe_dir, exe_path, dirlen);
        g_exe_dir[dirlen] = '\0';
    }
}

static int32_t copy_path(const char *src, char *out_path, uint32_t max_len, uint32_t *out_len) {
    if (!src || !out_path || max_len == 0) return HOST_FS_ERR_INVALID;
    size_t len = strlen(src);
    if (len > max_len) len = max_len;
    
    memcpy(out_path, src, len);
    if (len < max_len) out_path[len] = '\0';
    if (out_len) *out_len = (uint32_t)len;
    
    return HOST_FS_OK;
}

#ifdef _WIN32
int32_t host_fs_get_config_dir(char *out_path, uint32_t max_len, uint32_t *out_len) {
    char path[PATH_MAX];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        strncat(path, "\\Croft", sizeof(path) - strlen(path) - 1);
        return copy_path(path, out_path, max_len, out_len);
    }
    return HOST_FS_ERR_IO;
}

int32_t host_fs_get_cache_dir(char *out_path, uint32_t max_len, uint32_t *out_len) {
    char path[PATH_MAX];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
        strncat(path, "\\Croft", sizeof(path) - strlen(path) - 1);
        return copy_path(path, out_path, max_len, out_len);
    }
    return HOST_FS_ERR_IO;
}

int32_t host_fs_mkdir(const char *path, uint32_t path_len) {
    if (!path || path_len >= PATH_MAX) return HOST_FS_ERR_INVALID;
    char cpath[PATH_MAX];
    memcpy(cpath, path, path_len);
    cpath[path_len] = '\0';
    
    if (CreateDirectoryA(cpath, NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
        return HOST_FS_OK;
    }
    return HOST_FS_ERR_IO;
}
#elif defined(__APPLE__)
int32_t host_fs_get_config_dir(char *out_path, uint32_t max_len, uint32_t *out_len) {
    const char *home = getenv("HOME");
    if (!home) return HOST_FS_ERR_NOT_FOUND;
    char buf[PATH_MAX];
    snprintf(buf, sizeof(buf), "%s/Library/Application Support/Croft", home);
    return copy_path(buf, out_path, max_len, out_len);
}

int32_t host_fs_get_cache_dir(char *out_path, uint32_t max_len, uint32_t *out_len) {
    const char *home = getenv("HOME");
    if (!home) return HOST_FS_ERR_NOT_FOUND;
    char buf[PATH_MAX];
    snprintf(buf, sizeof(buf), "%s/Library/Caches/Croft", home);
    return copy_path(buf, out_path, max_len, out_len);
}

int32_t host_fs_mkdir(const char *path, uint32_t path_len) {
    if (!path || path_len >= PATH_MAX) return HOST_FS_ERR_INVALID;
    char cpath[PATH_MAX];
    memcpy(cpath, path, path_len);
    cpath[path_len] = '\0';
    
    if (mkdir(cpath, 0755) == 0 || errno == EEXIST) {
        return HOST_FS_OK;
    }
    return HOST_FS_ERR_IO;
}
#else
/* Linux / POSIX fallback */
int32_t host_fs_get_config_dir(char *out_path, uint32_t max_len, uint32_t *out_len) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    char buf[PATH_MAX];
    if (xdg && xdg[0]) {
        snprintf(buf, sizeof(buf), "%s/croft", xdg);
    } else {
        const char *home = getenv("HOME");
        if (!home) return HOST_FS_ERR_NOT_FOUND;
        snprintf(buf, sizeof(buf), "%s/.config/croft", home);
    }
    return copy_path(buf, out_path, max_len, out_len);
}

int32_t host_fs_get_cache_dir(char *out_path, uint32_t max_len, uint32_t *out_len) {
    const char *xdg = getenv("XDG_CACHE_HOME");
    char buf[PATH_MAX];
    if (xdg && xdg[0]) {
        snprintf(buf, sizeof(buf), "%s/croft", xdg);
    } else {
        const char *home = getenv("HOME");
        if (!home) return HOST_FS_ERR_NOT_FOUND;
        snprintf(buf, sizeof(buf), "%s/.cache/croft", home);
    }
    return copy_path(buf, out_path, max_len, out_len);
}

int32_t host_fs_mkdir(const char *path, uint32_t path_len) {
    if (!path || path_len >= PATH_MAX) return HOST_FS_ERR_INVALID;
    char cpath[PATH_MAX];
    memcpy(cpath, path, path_len);
    cpath[path_len] = '\0';
    
    if (mkdir(cpath, 0755) == 0 || errno == EEXIST) {
        return HOST_FS_OK;
    }
    if (errno == EACCES) return HOST_FS_ERR_ACCES;
    return HOST_FS_ERR_IO;
}
#endif

int32_t host_fs_get_resource_dir(char *out_path, uint32_t max_len, uint32_t *out_len) {
#ifdef CROFT_RESOURCE_DIR
    /* Override via macro definition */
    return copy_path(CROFT_RESOURCE_DIR, out_path, max_len, out_len);
#else
    /* Default: alongside the executable path */
    if (g_exe_dir[0] == '\0') {
        host_fs_init(NULL); /* initialize to default "." if not manually initialized */
    }
    return copy_path(g_exe_dir, out_path, max_len, out_len);
#endif
}
