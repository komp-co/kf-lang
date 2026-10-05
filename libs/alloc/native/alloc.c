#ifndef KF_NATIVE_UNITY
#include "kf_runtime.h"
/* alloc's own generated header, for the `String` layout and the prototypes
 * of what this file defines. It includes core's.
 *
 * Under KF_NATIVE_UNITY everything is already one translation unit. */
#include "alloc.h"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* `String`'s runtime half, freestanding like core.c: written against the
 * three headers above, with `memcpy` and `memmove` through `__builtin_`.
 * Its allocator is core's, which a freestanding program supplies. */
void panic(const char* message);
void* kf_alloc(size_t size);
void* kf_realloc(void* ptr, size_t size);
void kf_free(void* ptr);

/* CONSTRAINT: not `kf_cstr_len`. A unity build compiles every crate's native
 * C as one translation unit, where core.c already defines that static. */
static size_t kf_string_cstr_len(const char* s) {
    size_t n = 0;
    while (s[n] != '\0') n++;
    return n;
}

static String kf_string_from(const char* s) {
    size_t len = kf_string_cstr_len(s);
    String out;
    out.len = len;
    out.cap = len + 1;
    out.data = (uint8_t*)kf_alloc(out.cap);
    __builtin_memcpy(out.data, s, len + 1);
    return out;
}

static String kf_string_clone(const String* value) {
    return kf_string_from((const char*)value->data);
}

static void kf_string_append(String* out, const char* suffix) {
    size_t add = kf_string_cstr_len(suffix);
    if (add > SIZE_MAX - out->len - 1) panic("panic[capacity_overflow]: capacity overflow");
    size_t needed = out->len + add + 1;
    size_t offset = 0;
    uintptr_t source = (uintptr_t)suffix;
    uintptr_t begin = (uintptr_t)out->data;
    bool aliases = source >= begin && source <= begin + out->len;
    if (aliases) offset = (size_t)(source - begin);
    if (out->cap == 0) {
        /* Borrowed bytes, a literal's, or none: owned before the first write. */
        uint8_t* owned = (uint8_t*)kf_alloc(out->len + 1);
        if (out->len > 0) __builtin_memcpy(owned, out->data, out->len);
        owned[out->len] = 0;
        out->data = owned;
        out->cap = out->len + 1;
    }
    if (needed > out->cap) {
        size_t cap = out->cap ? out->cap : 1;
        while (cap < needed) {
            if (cap > SIZE_MAX / 2) {
                cap = needed;
                break;
            }
            cap *= 2;
        }
        uint8_t* grown = (uint8_t*)kf_realloc(out->data, cap);
        out->data = grown;
        out->cap = cap;
    }
    if (aliases) suffix = (const char*)out->data + offset;
    __builtin_memmove(out->data + out->len, suffix, add + 1);
    out->len += add;
}

String __kf_v2_str_from_cstr(const char* value) {
    return kf_string_from(value);
}

String __kf_v2_str_concat(String left, String right) {
    String out = kf_string_clone(&left);
    kf_string_append(&out, (const char*)right.data);
    return out;
}

void __kf_v2_string_drop(String value) {
    if (value.cap > 0) kf_free(value.data);
}

String kf_str_slice_to_string(const char* value, uint64_t start, uint64_t end) {
    if (end < start) return kf_string_from("");
    size_t len = (size_t)(end - start);
    char* buffer = (char*)kf_alloc(len + 1);
    __builtin_memcpy(buffer, value + start, len);
    buffer[len] = '\0';
    String out = kf_string_from(buffer);
    kf_free(buffer);
    return out;
}
