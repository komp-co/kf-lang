#ifndef KF_NATIVE_UNITY
#include "kf_runtime.h"
/* For the `String` layout, which alloc declares; its header includes core's. */
#include "alloc.h"
#endif

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    String* data;
    uint64_t len;
    uint64_t cap;
} KfStringArgs;

int32_t kf_process_status(const char* program, void* raw_args, const char* cwd, void* raw_environment, const char* stdout_path) {
    KfStringArgs* args = (KfStringArgs*)raw_args;
    KfStringArgs* environment = (KfStringArgs*)raw_environment;
    char** values = (char**)malloc((args->len + 2) * sizeof(char*));
    values[0] = (char*)program;
    for (uint64_t i = 0; i < args->len; i++) values[i + 1] = (char*)args->data[i].data;
    values[args->len + 1] = NULL;
    pid_t child = fork();
    if (child == 0) {
        for (uint64_t i = 0; i < environment->len; i++) {
            char* entry = strdup((char*)environment->data[i].data);
            if (!entry || putenv(entry) != 0) _exit(126);
        }
        if (cwd[0] && chdir(cwd) != 0) _exit(126);
        if (stdout_path[0]) {
            int fd = open(stdout_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (fd < 0 || dup2(fd, STDOUT_FILENO) < 0) _exit(126);
            close(fd);
        }
        execvp(program, values);
        _exit(127);
    }
    free(values);
    int status = 0;
    if (child < 0 || waitpid(child, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}
