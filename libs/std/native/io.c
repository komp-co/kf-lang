#ifndef KF_NATIVE_UNITY
#include "kf_runtime.h"
/* For the `String` layout, which alloc declares; its header includes core's. */
#include "alloc.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Whether the last read reached end of input. Held rather than asked of the
 * stream, because `feof` is only true once a read has already failed — a
 * caller needs to distinguish "read nothing because the line was empty" from
 * "read nothing because there is no more input", and that is a property of
 * the read that just happened. */
static int kf_stdin_hit_eof = 0;

int32_t kf_stdin_eof(void) {
    return kf_stdin_hit_eof;
}

/* Through the next newline, which is not included. */
String kf_stdin_read_line(void) {
    char* line = NULL;
    size_t cap = 0;
    ssize_t n = getline(&line, &cap, stdin);
    if (n < 0) {
        free(line);
        kf_stdin_hit_eof = 1;
        return __kf_v2_str_from_cstr("");
    }
    if (n > 0 && line[n - 1] == '\n') { line[n - 1] = '\0'; }
    String out = __kf_v2_str_from_cstr(line);
    free(line);
    return out;
}

/* Exactly `n` bytes, or fewer when input ends first. Bytes, not characters:
 * a length-framed protocol counts bytes and the body may hold anything. */
String kf_stdin_read_bytes(uint64_t n) {
    if (n == 0) return __kf_v2_str_from_cstr("");
    char* buf = (char*)malloc((size_t)n + 1);
    if (!buf) {
        kf_stdin_hit_eof = 1;
        return __kf_v2_str_from_cstr("");
    }
    size_t got = fread(buf, 1, (size_t)n, stdin);
    if (got < (size_t)n) { kf_stdin_hit_eof = 1; }
    buf[got] = '\0';
    String out = __kf_v2_str_from_cstr(buf);
    free(buf);
    return out;
}
