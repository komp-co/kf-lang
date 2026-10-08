#ifndef KF_NATIVE_UNITY
#include "kf_runtime.h"
/* core's own generated header: the prototypes callers see for what this
 * file defines. Under KF_NATIVE_UNITY everything is already one translation
 * unit. */
#include "core.h"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* core's FREESTANDING tier.
 *
 * The three headers above are the ones C guarantees a freestanding
 * implementation provides — they declare types and limits and nothing that
 * has to exist at runtime. Everything below is written against them alone,
 * so this file compiles for a target with no operating system, no console
 * and no heap.
 *
 * Its hosted half is `core_hosted.c`, listed separately in kf.toml, and a
 * program built with `freestanding = true` does not get it. What that
 * leaves undefined is the point rather than an oversight:
 *
 *     panic                          stop, however this target stops
 *     runtime_print/runtime_println  put bytes somewhere, if anywhere
 *     kf_try_alloc/kf_try_realloc    where memory comes from
 *     kf_free                        and where it goes back to
 *
 * Those are the seams. A freestanding program defines them — a fault
 * handler, a UART, a bump allocator over a fixed buffer — and the linker
 * names any it forgot. `core_hosted.c` is only the definition set that
 * happens to be right when there IS a libc underneath.
 *
 * The declarations below are therefore promises, not omissions.
 */
void panic(const char* message);
void* kf_try_alloc(size_t size);
void* kf_try_realloc(void* ptr, size_t size);
void kf_free(void* ptr);

/*
 * What an INFALLIBLE allocation does when there is no memory.
 *
 * Through `panic`, not a bare trap: panic is already this runtime's "stop
 * with something legible" boundary, and an allocation failure that says
 * nothing is indistinguishable from a crash. Weak, so a target can replace
 * it with whatever it has — a fault handler, a reset, a blinking LED —
 * without replacing the allocator itself.
 *
 * It must not return. A caller that reaches it has already failed to get
 * memory and has nothing to continue with; the wrappers below rely on that
 * to guarantee their own result is non-null. `__builtin_trap` rather than
 * `abort`, which is libc's and this file has none — it is here so the
 * no-return promise holds even against a replaced `panic` that returns.
 */
__attribute__((weak)) void kf_alloc_failed(size_t size) {
    (void)size;
    panic("panic[out_of_memory]: out of memory");
    __builtin_trap();
}

/*
 * The FALLIBLE operations are the primitives, and the infallible ones are
 * wrappers over them. That direction is deliberate: Rust went the other way
 * — infallible first, `try_reserve` bolted on years later and still not
 * everywhere — and the cost of that order is two implementations that drift.
 * Deriving one from the other means there is only ever one allocator to
 * replace, and replacing it makes both halves fallible-correct at once.
 *
 * `kf_try_alloc` and `kf_try_realloc` return NULL on failure. Null, rather
 * than an `Option<Ptr<uint8>>`: that type is emitted as `Option__Ptr__uint8`,
 * a tagged struct whose layout comes out of komp's generic mangling, and
 * hand-written C must not be coupled to it. The KFlat side wraps the null
 * with `ptr_as_option` from `core.ptr`, which is exactly what that function
 * is for.
 *
 * The infallible pair never returns NULL — it either hands back memory or
 * does not come back — which is what lets every caller below, and every
 * allocation komp generates, use the result without a check.
 *
 * Weak, so a program replacing `kf_alloc` outright keeps working; a program
 * replacing only `kf_try_alloc` gets the failure policy for free.
 */
__attribute__((weak)) void* kf_alloc(size_t size) {
    void* memory = kf_try_alloc(size);
    if (!memory) kf_alloc_failed(size);
    return memory;
}

__attribute__((weak)) void* kf_realloc(void* ptr, size_t size) {
    void* memory = kf_try_realloc(ptr, size);
    if (!memory) kf_alloc_failed(size);
    return memory;
}

#define KF_PRIMITIVE_INTEGER_ARITH(T, name) \
    T kf_prim_##name##_add(T left, T right) { return left + right; } \
    T kf_prim_##name##_sub(T left, T right) { return left - right; } \
    T kf_prim_##name##_mul(T left, T right) { return left * right; } \
    T kf_prim_##name##_div(T left, T right) { return left / right; } \
    T kf_prim_##name##_mod(T left, T right) { return left % right; }

#define KF_PRIMITIVE_FLOAT_ARITH(T, name) \
    T kf_prim_##name##_add(T left, T right) { return left + right; } \
    T kf_prim_##name##_sub(T left, T right) { return left - right; } \
    T kf_prim_##name##_mul(T left, T right) { return left * right; } \
    T kf_prim_##name##_div(T left, T right) { return left / right; }

KF_PRIMITIVE_INTEGER_ARITH(int8_t, int8)
KF_PRIMITIVE_INTEGER_ARITH(int16_t, int16)
KF_PRIMITIVE_INTEGER_ARITH(int32_t, int32)
KF_PRIMITIVE_INTEGER_ARITH(int64_t, int64)
KF_PRIMITIVE_INTEGER_ARITH(uint8_t, uint8)
KF_PRIMITIVE_INTEGER_ARITH(uint16_t, uint16)
KF_PRIMITIVE_INTEGER_ARITH(uint32_t, uint32)
KF_PRIMITIVE_INTEGER_ARITH(uint64_t, uint64)
KF_PRIMITIVE_FLOAT_ARITH(float, float32)
KF_PRIMITIVE_FLOAT_ARITH(double, float64)

#define KF_PRIMITIVE_EQUAL(T, name) \
    bool kf_prim_##name##_equals(T left, T right) { return left == right; }

KF_PRIMITIVE_EQUAL(int8_t, int8)
KF_PRIMITIVE_EQUAL(int16_t, int16)
KF_PRIMITIVE_EQUAL(int32_t, int32)
KF_PRIMITIVE_EQUAL(int64_t, int64)
KF_PRIMITIVE_EQUAL(uint8_t, uint8)
KF_PRIMITIVE_EQUAL(uint16_t, uint16)
KF_PRIMITIVE_EQUAL(uint32_t, uint32)
KF_PRIMITIVE_EQUAL(uint64_t, uint64)
KF_PRIMITIVE_EQUAL(float, float32)
KF_PRIMITIVE_EQUAL(double, float64)
KF_PRIMITIVE_EQUAL(bool, bool)
KF_PRIMITIVE_EQUAL(uint32_t, char)

#define KF_PRIMITIVE_COMPARE(T, name) \
    int32_t kf_prim_##name##_compare(T left, T right) { return (left > right) - (left < right); }

#define KF_PRIMITIVE_FLOAT_COMPARE(T, name) \
    int32_t kf_prim_##name##_compare(T left, T right) { \
        bool left_nan = left != left; \
        bool right_nan = right != right; \
        if (left_nan) return right_nan ? 0 : 1; \
        if (right_nan) return -1; \
        return (left > right) - (left < right); \
    }

KF_PRIMITIVE_COMPARE(int8_t, int8)
KF_PRIMITIVE_COMPARE(int16_t, int16)
KF_PRIMITIVE_COMPARE(int32_t, int32)
KF_PRIMITIVE_COMPARE(int64_t, int64)
KF_PRIMITIVE_COMPARE(uint8_t, uint8)
KF_PRIMITIVE_COMPARE(uint16_t, uint16)
KF_PRIMITIVE_COMPARE(uint32_t, uint32)
KF_PRIMITIVE_COMPARE(uint64_t, uint64)
KF_PRIMITIVE_FLOAT_COMPARE(float, float32)
KF_PRIMITIVE_FLOAT_COMPARE(double, float64)
KF_PRIMITIVE_COMPARE(bool, bool)
KF_PRIMITIVE_COMPARE(uint32_t, char)

#define KF_PRIMITIVE_BITWISE(T, name) \
    T kf_prim_##name##_bit_and(T left, T right) { return left & right; } \
    T kf_prim_##name##_bit_or(T left, T right) { return left | right; } \
    T kf_prim_##name##_bit_xor(T left, T right) { return left ^ right; } \
    T kf_prim_##name##_shl(T left, T right) { return left << right; } \
    T kf_prim_##name##_shr(T left, T right) { return left >> right; }

KF_PRIMITIVE_BITWISE(int32_t, int32)
KF_PRIMITIVE_BITWISE(uint32_t, uint32)
KF_PRIMITIVE_BITWISE(uint64_t, uint64)
KF_PRIMITIVE_BITWISE(int8_t, int8)
KF_PRIMITIVE_BITWISE(int16_t, int16)
KF_PRIMITIVE_BITWISE(int64_t, int64)
KF_PRIMITIVE_BITWISE(uint8_t, uint8)
KF_PRIMITIVE_BITWISE(uint16_t, uint16)

/* <string.h> is not freestanding. Of its functions only memcpy, memmove,
 * memset and memcmp are ones a freestanding target is expected to supply
 * anyway — the compiler may emit calls to those from an ordinary struct
 * assignment — so those go through `__builtin_`, which lets it inline them
 * and otherwise calls what the target already has to provide. strlen and
 * strcmp carry no such expectation, so they are spelled out.
 */
static size_t kf_cstr_len(const char* s) {
    size_t n = 0;
    while (s[n] != '\0') n++;
    return n;
}

/* Equal when they agree up to and including the terminator — which is the
 * same walk strcmp does, minus the ordering nobody here asks for.
 */
bool str_equals(const char* left, const char* right) {
    size_t i = 0;
    while (left[i] != '\0' && left[i] == right[i]) i++;
    return left[i] == right[i];
}

const char* str_as_str(const char* value) {
    return value;
}

uint64_t kf_str_byte_len(const char* value) {
    return kf_cstr_len(value);
}

uint8_t kf_str_byte_at(const char* value, uint64_t index) {
    return (uint8_t)value[index];
}

/* argv, held rather than read. Storing what `main` was handed needs no
 * library — whoever had an argv passed it in — so this stays on the
 * freestanding side and the generated `main` can call kf_arg_init
 * unconditionally. A target with no command line calls it with (0, NULL),
 * or not at all, and the accessors answer for an empty one.
 */
static int kf_argc = 0;
static char** kf_argv = NULL;

void kf_arg_init(int argc, char** argv) {
    kf_argc = argc;
    kf_argv = argv;
}

int32_t kf_arg_count(void) {
    return kf_argc > 0 ? kf_argc - 1 : 0;
}

const char* kf_arg_get(int32_t index) {
    return kf_argv[index + 1];
}

const char* kf_program_name(void) {
    return kf_argc > 0 && kf_argv[0] != NULL ? kf_argv[0] : "";
}

// Reinterpret a float's bits as an integer, for hashing. A union rather than
// memcpy so this stays in the freestanding tier — `Hash` must not pull in
// libc (see libs/core/src/hash.kf).
//
// Casting (`(uint64_t)v`) would be a VALUE conversion: 1.5 and 1.9 both
// truncate to 1 and would hash alike, and every fraction below 1 would
// collide with zero.
uint64_t kf_float64_bits(double v) {
    union { double d; uint64_t u; } x;
    x.d = v;
    return x.u;
}

uint32_t kf_float32_bits(float v) {
    union { float f; uint32_t u; } x;
    x.f = v;
    return x.u;
}

