#ifndef KF_NATIVE_UNITY
#include "kf_runtime.h"
/* core's own generated header: the prototypes callers see for what this
 * file defines. */
#include "core.h"
#endif

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* core's HOSTED tier.
 *
 * `core.c` next to this file is written against the three freestanding
 * headers and leaves four things declared but undefined: where memory comes
 * from, and how to stop, print and be told about the process. This file is
 * the set of answers that is right when there is a C library underneath.
 *
 * It is listed under `[native] hosted_c_sources`, so a program built with
 * `freestanding = true` does not get it and supplies its own answers
 * instead. Every symbol here is therefore a seam and not an internal: the
 * allocator trio stays weak (a hosted program can still replace it without
 * going freestanding), and the rest are strong because a program that wants
 * a different `panic` on a hosted target is describing a freestanding
 * build.
 *
 * The include list is the honest statement of what this half costs:
 * <stdio.h> for a console, <stdlib.h> for a heap and exit, <sys/wait.h> and
 * <unistd.h> for a process that can fork. None of it exists on a target
 * with no operating system, which is why it is not in the other file.
 */

/*
 * The FALLIBLE primitives. Everything above them in `core.c` — `kf_alloc`,
 * `kf_realloc`, and the failure policy between them — is derived from this
 * pair, so replacing malloc here is the whole of replacing the allocator.
 *
 * Weak, so a hosted program can define its own without editing this
 * checkout: `libcore.a` is linked into every program and its member is
 * pulled in for `runtime_println` whatever else the program does, so a
 * strong definition would collide with a replacement rather than yield to
 * it.
 */
__attribute__((weak)) void* kf_try_alloc(size_t size) {
    return malloc(size);
}

__attribute__((weak)) void* kf_try_realloc(void* ptr, size_t size) {
    return realloc(ptr, size);
}

__attribute__((weak)) void kf_free(void* ptr) {
    free(ptr);
}

/* Stopping, and putting bytes somewhere. The freestanding boundary that
 * `core.assert` is written against: an assertion needs only `Equal`, these
 * two, and nothing else.
 */
void runtime_print(const char* value) {
    fputs(value, stdout);
    fflush(stdout);
}

void runtime_println(const char* value) {
    puts(value);
    fflush(stdout);
}

/* A panic the program expects: its code, or "any". A matching panic exits
 * 0, so a test that should panic passes by panicking. */
static const char* kf_expected_panic = NULL;

void kf_expect_panic(const char* code) {
    kf_expected_panic = code;
}

static bool kf_panic_expected(const char* message) {
    const char* code = kf_expected_panic;
    if (code == NULL) return false;
    if (strcmp(code, "any") == 0) return true;
    size_t n = strlen(code);
    return strncmp(message, "panic[", 6) == 0 && strncmp(message + 6, code, n) == 0 && message[6 + n] == ']';
}

void panic(const char* message) {
    puts(message);
    fflush(stdout);
    exit(kf_panic_expected(message) ? 0 : 1);
}

/* Floats print with the FEWEST digits that read back as the same value.
 *
 * A fixed precision is wrong in both directions: "%g" turns 0.1 + 0.2 into
 * "0.3" and quietly loses the thing the reader is usually looking for, while
 * "%.17g" turns 0.1 into "0.10000000000000001" and makes ordinary numbers
 * unreadable. Trying the short form and keeping it only when it round-trips
 * gives "0.1" for 0.1 and "0.30000000000000004" for 0.1 + 0.2 -- exact
 * either way, and short whenever short is honest.
 *
 * The digits are handed back as a borrow into the ring below rather than as an
 * owned String: `Display` writes into a sink, and a number should not need an
 * allocator to print. Formatting one needs somewhere to put the characters,
 * KFlat has no stack arrays, so the scratch space lives here.
 *
 * CONSTRAINT: a returned pointer stays valid until the KF_FLOAT_SLOTS-th call
 * after it. Every caller in core hands it straight to `write_str`, which
 * copies; a sink that formats another float from inside its own `write_str`
 * is what the slots are for, and past that depth the bytes are rewritten
 * underneath the older borrow.
 */
#define KF_FLOAT_SLOTS 8
#define KF_FLOAT_WIDTH 40

static char kf_float_ring[KF_FLOAT_SLOTS][KF_FLOAT_WIDTH];
static unsigned kf_float_next = 0;

static char* kf_float_slot(void) {
    char* slot = kf_float_ring[kf_float_next];
    kf_float_next = (kf_float_next + 1) % KF_FLOAT_SLOTS;
    return slot;
}

const char* float64_display_str(double value) {
    char* buffer = kf_float_slot();
    snprintf(buffer, KF_FLOAT_WIDTH, "%.15g", value);
    if (strtod(buffer, NULL) != value) {
        snprintf(buffer, KF_FLOAT_WIDTH, "%.17g", value);
    }
    return buffer;
}

const char* float32_display_str(float value) {
    char* buffer = kf_float_slot();
    snprintf(buffer, KF_FLOAT_WIDTH, "%.6g", (double)value);
    if ((float)strtod(buffer, NULL) != value) {
        snprintf(buffer, KF_FLOAT_WIDTH, "%.9g", (double)value);
    }
    return buffer;
}

/*
 * Reading a float out of text, for `FromText`. strtod also skips leading
 * space and reads hex floats; both are refused at their first byte so the
 * text means what it says in decimal.
 */
uint64_t kf_float_text_prefix(const char* text) {
    if (isspace((unsigned char)text[0])) return 0;
    const char* digits = text;
    if (*digits == '+' || *digits == '-') digits++;
    if (digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) return (uint64_t)(digits + 1 - text);
    char* end;
    strtod(text, &end);
    return (uint64_t)(end - text);
}

double kf_float64_from_text(const char* text) {
    return strtod(text, NULL);
}

float kf_float32_from_text(const char* text) {
    return strtof(text, NULL);
}

/*
 * A float64 in fixed or exponential notation with a chosen number of
 * decimals, at most KF_DECIMALS_MAX. The integer part of a float64 has at
 * most 309 digits, so a slot holds the longest.
 *
 * CONSTRAINT: shares the float ring's lifetime rule.
 */
#define KF_DECIMALS_MAX 100
#define KF_NOTATION_WIDTH 420

static char kf_notation_ring[KF_FLOAT_SLOTS][KF_NOTATION_WIDTH];
static unsigned kf_notation_next = 0;

static char* kf_notation_slot(void) {
    char* slot = kf_notation_ring[kf_notation_next];
    kf_notation_next = (kf_notation_next + 1) % KF_FLOAT_SLOTS;
    return slot;
}

const char* kf_float64_fixed_str(double value, uint32_t decimals) {
    char* buffer = kf_notation_slot();
    if (decimals > KF_DECIMALS_MAX) decimals = KF_DECIMALS_MAX;
    snprintf(buffer, KF_NOTATION_WIDTH, "%.*f", (int)decimals, value);
    return buffer;
}

const char* kf_float64_exponential_str(double value, uint32_t decimals) {
    char* buffer = kf_notation_slot();
    if (decimals > KF_DECIMALS_MAX) decimals = KF_DECIMALS_MAX;
    snprintf(buffer, KF_NOTATION_WIDTH, "%.*e", (int)decimals, value);
    return buffer;
}

// Asking the process about itself, alongside the argv `core.c` holds. Both
// exist for output that should look different when a person is reading it
// than when a pipe is: which of the two is a policy question, so it is
// answered by the caller and not here.

// Present AND non-empty, which is what the NO_COLOR convention specifies:
// `NO_COLOR=` set to nothing is not a request to disable anything.
bool kf_env_is_set(const char* name) {
    const char* value = getenv(name);
    return value != NULL && value[0] != '\0';
}

// The variable's value, or "" when it is unset.
const char* kf_env_get(const char* name) {
    const char* value = getenv(name);
    return value != NULL ? value : "";
}

bool kf_stdout_is_tty(void) {
    return isatty(1) == 1;
}
