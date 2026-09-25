#ifndef KF_NATIVE_UNITY
#include "kf_runtime.h"
/* For the `String` layout, which alloc declares; its header includes core's. */
#include "alloc.h"
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

uint64_t kf_fs_dir_open(const char* path) {
    return (uint64_t)(uintptr_t)opendir(path);
}

const char* kf_fs_dir_next(uint64_t handle) {
    DIR* directory = (DIR*)(uintptr_t)handle;
    if (!directory) return NULL;
    for (;;) {
        struct dirent* entry = readdir(directory);
        if (!entry) return NULL;
        const char* name = entry->d_name;
        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) continue;
        return name;
    }
}

void kf_fs_dir_close(uint64_t handle) {
    DIR* directory = (DIR*)(uintptr_t)handle;
    if (directory) closedir(directory);
}

String read_file(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return __kf_v2_str_from_cstr("");
    size_t cap = 4096;
    size_t len = 0;
    char* buffer = (char*)malloc(cap);
    for (;;) {
        if (cap - len < 4096) {
            cap *= 2;
            buffer = (char*)realloc(buffer, cap);
        }
        long count = read(fd, buffer + len, cap - len - 1);
        if (count <= 0) break;
        len += (size_t)count;
    }
    close(fd);
    buffer[len] = '\0';
    String out = __kf_v2_str_from_cstr(buffer);
    free(buffer);
    return out;
}

bool is_dir(const char* path) {
    struct stat status;
    if (stat(path, &status) != 0) return false;
    return (status.st_mode & S_IFMT) == S_IFDIR;
}

String join(const char* left, const char* right) {
    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    char* buffer = (char*)malloc(left_len + right_len + 2);
    memcpy(buffer, left, left_len);
    buffer[left_len] = '/';
    memcpy(buffer + left_len + 1, right, right_len);
    buffer[left_len + right_len + 1] = '\0';
    String out = __kf_v2_str_from_cstr(buffer);
    free(buffer);
    return out;
}

void write_file(const char* path, const char* content) {
    FILE* file = fopen(path, "w");
    if (!file) return;
    fputs(content, file);
    fclose(file);
}

int32_t kf_create_dir_all(const char* path) {
    char* copy = strdup(path);
    if (!copy) return -1;
    for (char* cursor = copy + 1; *cursor; cursor++) {
        if (*cursor == '/') {
            *cursor = '\0';
            mkdir(copy, 0777);
            *cursor = '/';
        }
    }
    int result = mkdir(copy, 0777);
    free(copy);
    return result == 0 || errno == EEXIST ? 0 : -1;
}

String kf_temp_dir(const char* prefix) {
    size_t size = strlen(prefix) + 20;
    char* path = (char*)malloc(size);
    snprintf(path, size, "/tmp/%s-XXXXXX", prefix);
    if (!mkdtemp(path)) {
        free(path);
        return __kf_v2_str_from_cstr("");
    }
    String out = __kf_v2_str_from_cstr(path);
    free(path);
    return out;
}

int32_t kf_remove_dir_all(const char* path) {
    DIR* directory = opendir(path);
    if (!directory) return unlink(path);
    struct dirent* entry;
    int result = 0;
    while ((entry = readdir(directory))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        size_t size = strlen(path) + strlen(entry->d_name) + 2;
        char* child = (char*)malloc(size);
        snprintf(child, size, "%s/%s", path, entry->d_name);
        struct stat status;
        if (lstat(child, &status) != 0 || (S_ISDIR(status.st_mode) ? kf_remove_dir_all(child) : unlink(child)) != 0) result = -1;
        free(child);
    }
    closedir(directory);
    if (result == 0 && rmdir(path) != 0) result = -1;
    return result;
}

/* ─── Per-invocation scratch ──────────────────────────────────────── */

/* A fixture that writes to a fixed `/tmp` name is shared state: the same
 * fixture in another checkout, or a second run of the same suite, writes
 * the same files underneath it. These answer a private directory instead.
 *
 * The root is published in the environment so a forked or exec'd child
 * agrees with its parent about where it is, and removed at exit only by
 * the process that made it. KOMP_KEEP_SCRATCH leaves it for inspection.
 *
 * An unrelated process makes its own root rather than finding this one, so
 * "private" means private to a process TREE. komp's test harness forks per
 * test, which lands one root per test — measured at 5 roots for libs/std's
 * 5 scratch-using tests. That is finer isolation than the fixed paths ever
 * had, and it is why no test may depend on a fixture another one built.
 */

#define KF_SCRATCH_CACHE 512

static char kf_scratch_root_path[512];
static int kf_scratch_is_ours = 0;
static char* kf_scratch_names[KF_SCRATCH_CACHE];
static char* kf_scratch_values[KF_SCRATCH_CACHE];
static int kf_scratch_cached = 0;

static void kf_scratch_cleanup(void) {
    if (kf_scratch_is_ours && !getenv("KOMP_KEEP_SCRATCH")) {
        kf_remove_dir_all(kf_scratch_root_path);
    }
}

const char* kf_scratch_root(void) {
    if (kf_scratch_root_path[0]) return kf_scratch_root_path;
    const char* inherited = getenv("KOMP_SCRATCH_ROOT");
    if (inherited && *inherited) {
        snprintf(kf_scratch_root_path, sizeof kf_scratch_root_path, "%s", inherited);
        return kf_scratch_root_path;
    }
    const char* base = getenv("TMPDIR");
    if (!base || !*base) base = "/tmp";
    snprintf(kf_scratch_root_path, sizeof kf_scratch_root_path, "%s/komp-XXXXXX", base);
    if (!mkdtemp(kf_scratch_root_path)) {
        /* Nowhere to be private: the fixed path is wrong but it works. */
        snprintf(kf_scratch_root_path, sizeof kf_scratch_root_path, "%s", base);
        return kf_scratch_root_path;
    }
    kf_scratch_is_ours = 1;
    setenv("KOMP_SCRATCH_ROOT", kf_scratch_root_path, 1);
    atexit(kf_scratch_cleanup);
    return kf_scratch_root_path;
}

/* Interned, so the answer can be a `str` and sit wherever the fixed path
 * used to. Past the cache it is a fresh allocation each call, which only
 * a caller naming more than KF_SCRATCH_CACHE distinct subpaths reaches. */
const char* kf_scratch_path(const char* name) {
    for (int i = 0; i < kf_scratch_cached; i++) {
        if (strcmp(kf_scratch_names[i], name) == 0) return kf_scratch_values[i];
    }
    const char* root = kf_scratch_root();
    size_t size = strlen(root) + strlen(name) + 2;
    char* path = (char*)malloc(size);
    snprintf(path, size, "%s/%s", root, name);
    if (kf_scratch_cached < KF_SCRATCH_CACHE) {
        kf_scratch_names[kf_scratch_cached] = strdup(name);
        kf_scratch_values[kf_scratch_cached] = path;
        kf_scratch_cached++;
    }
    return path;
}
