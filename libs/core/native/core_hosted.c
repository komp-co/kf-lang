#ifndef KF_NATIVE_UNITY
#include "kf_runtime.h"
/* core's own generated header: the prototypes callers see for what this
 * file defines. */
#include "core.h"
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

void panic(const char* message) {
    puts(message);
    fflush(stdout);
    exit(1);
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

// Run one `@test` in a child process: an assertion calls panic, which exits,
// so a failing test must not share a process with the rest of the suite.
//
// The child is this same binary re-exec'd with `--run-test <name>`, this
// process's argv forwarded so a flag a test reads still reaches it. Returns
// the child's exit code, or 128 if it died on a signal — a segfault or an
// ASan abort is a failed test, not a dead suite.
//
// argv comes back through the accessors rather than the statics that hold
// it: those live in the freestanding half, where storing an argv needs no
// library, and static means static.
/* Where the suite's wall clock goes.
 *
 * Recorded here rather than in the generated main so no test source and no
 * codegen has to know about it: the runner already forks each test and knows
 * its name, which is exactly the pair a report needs. Names are string
 * literals in the binary, so holding the pointer is safe for the run.
 */
static int32_t kf_test_run_child_timed(const char* name);

#define KF_TEST_SLOTS 8192
static const char*    kf_test_names[KF_TEST_SLOTS];
static unsigned long  kf_test_elapsed[KF_TEST_SLOTS];
static int            kf_test_recorded = 0;
static unsigned long  kf_test_total_ns = 0;

/* The tests that failed, listed again under the summary: a count alone sends
 * whoever reads it back to the full log, which on CI is usually truncated. */
static const char*    kf_test_failed_names[KF_TEST_SLOTS];
static int            kf_test_failed_count = 0;

void kf_test_note_failure(const char* name) {
    if (kf_test_failed_count < KF_TEST_SLOTS) kf_test_failed_names[kf_test_failed_count++] = name;
}

static unsigned long kf_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)ts.tv_sec * 1000000000UL + (unsigned long)ts.tv_nsec;
}

int32_t kf_test_run_child(const char* name) {
    unsigned long started = kf_now_ns();
    int32_t code = kf_test_run_child_timed(name);
    unsigned long elapsed = kf_now_ns() - started;
    kf_test_total_ns += elapsed;
    if (kf_test_recorded < KF_TEST_SLOTS) {
        kf_test_names[kf_test_recorded] = name;
        kf_test_elapsed[kf_test_recorded] = elapsed;
        kf_test_recorded++;
    }
    return code;
}

/* Forks this binary as `--run-test <name>`. `out_fd`, when not -1, takes the
 * child's stdout and stderr. `slot`, when not -1, gives the child a scratch
 * root of its own under an inherited one: tests that run at once may use the
 * same fixture names. */
static pid_t kf_test_spawn(const char* name, int out_fd, int slot) {
    fflush(stdout);
    fflush(stderr);
    pid_t child = fork();
    if (child == 0) {
        if (out_fd >= 0) {
            dup2(out_fd, 1);
            dup2(out_fd, 2);
        }
        const char* shared = getenv("KOMP_SCRATCH_ROOT");
        if (slot >= 0 && shared != NULL && shared[0] != '\0') {
            char own[4096];
            snprintf(own, sizeof own, "%s/t%d", shared, slot);
            mkdir(shared, 0700);
            mkdir(own, 0700);
            setenv("KOMP_SCRATCH_ROOT", own, 1);
        }
        int forwarded = kf_arg_count();
        char** values = (char**)kf_alloc((size_t)(forwarded + 5) * sizeof(char*));
        int n = 0;
        values[n++] = (char*)"/proc/self/exe";
        values[n++] = (char*)"--run-test";
        values[n++] = (char*)name;
        for (int i = 0; i < forwarded; i++) values[n++] = (char*)kf_arg_get(i);
        values[n] = NULL;
        execv("/proc/self/exe", values);
        _exit(127);
    }
    return child;
}

static int32_t kf_test_exit_code(int status) {
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}

static int32_t kf_test_run_child_timed(const char* name) {
    pid_t child = kf_test_spawn(name, -1, -1);
    int status = 0;
    if (child < 0 || waitpid(child, &status, 0) < 0) return -1;
    return kf_test_exit_code(status);
}

/* Tests queued by the generated main, run by `kf_test_run_queued`. */
static const char* kf_test_queue_names[KF_TEST_SLOTS];
static int         kf_test_queued = 0;

void kf_test_queue(const char* name) {
    if (kf_test_queued < KF_TEST_SLOTS) kf_test_queue_names[kf_test_queued++] = name;
}

int32_t kf_test_queued_count(void) {
    return kf_test_queued;
}

/* `KOMP_TEST_JOBS`, or 2: each test is its own process, and one at a time
 * leaves the host's other cores idle. */
static int kf_test_jobs(void) {
    const char* value = getenv("KOMP_TEST_JOBS");
    int jobs = (value != NULL && value[0] != '\0') ? atoi(value) : 2;
    return jobs < 1 ? 1 : jobs;
}

static void kf_test_record(const char* name, unsigned long elapsed) {
    kf_test_total_ns += elapsed;
    if (kf_test_recorded < KF_TEST_SLOTS) {
        kf_test_names[kf_test_recorded] = name;
        kf_test_elapsed[kf_test_recorded] = elapsed;
        kf_test_recorded++;
    }
}

static void kf_test_print_result(const char* name, int32_t code, int* failed) {
    if (code == 0) {
        printf("ok\n");
    } else {
        printf("FAILED\n");
        (*failed)++;
        kf_test_note_failure(name);
    }
    fflush(stdout);
}

static void kf_test_copy_out(FILE* captured) {
    char buffer[4096];
    size_t n;
    fflush(stdout);
    rewind(captured);
    while ((n = fread(buffer, 1, sizeof buffer, captured)) > 0) fwrite(buffer, 1, n, stdout);
    fclose(captured);
}

/* Runs every queued test, `kf_test_jobs()` at a time, and answers how many
 * failed. Each line of the report comes out in queue order: a test's output
 * is captured and printed when it is that test's turn. One job streams
 * instead, exactly as a single child always has. */
int32_t kf_test_run_queued(void) {
    int jobs = kf_test_jobs();
    int failed = 0;
    if (jobs == 1) {
        for (int i = 0; i < kf_test_queued; i++) {
            printf("test %s ...\n", kf_test_queue_names[i]);
            int32_t code = kf_test_run_child(kf_test_queue_names[i]);
            kf_test_print_result(kf_test_queue_names[i], code, &failed);
        }
        return failed;
    }
    int total = kf_test_queued;
    pid_t* pids = (pid_t*)kf_alloc((size_t)(total + 1) * sizeof(pid_t));
    FILE** outs = (FILE**)kf_alloc((size_t)(total + 1) * sizeof(FILE*));
    int32_t* codes = (int32_t*)kf_alloc((size_t)(total + 1) * sizeof(int32_t));
    unsigned long* started = (unsigned long*)kf_alloc((size_t)(total + 1) * sizeof(unsigned long));
    bool* done = (bool*)kf_alloc((size_t)(total + 1) * sizeof(bool));
    int next_start = 0, next_print = 0, running = 0;
    while (next_print < total) {
        while (running < jobs && next_start < total) {
            int i = next_start++;
            done[i] = false;
            outs[i] = tmpfile();
            started[i] = kf_now_ns();
            pids[i] = kf_test_spawn(kf_test_queue_names[i], outs[i] != NULL ? fileno(outs[i]) : -1, i);
            if (pids[i] < 0) {
                codes[i] = -1;
                done[i] = true;
                kf_test_record(kf_test_queue_names[i], 0);
            } else {
                running++;
            }
        }
        while (next_print < total && done[next_print]) {
            int i = next_print++;
            printf("test %s ...\n", kf_test_queue_names[i]);
            if (outs[i] != NULL) kf_test_copy_out(outs[i]);
            kf_test_print_result(kf_test_queue_names[i], codes[i], &failed);
        }
        if (next_print >= total || running == 0) continue;
        int status = 0;
        pid_t reaped = waitpid(-1, &status, 0);
        if (reaped < 0) break;
        for (int i = 0; i < next_start; i++) {
            if (!done[i] && pids[i] == reaped) {
                codes[i] = kf_test_exit_code(status);
                done[i] = true;
                running--;
                kf_test_record(kf_test_queue_names[i], kf_now_ns() - started[i]);
                break;
            }
        }
    }
    return failed;
}

// The summary line, and the failures above it. Formatted here so the generated
// test main needs no string vocabulary of its own: it may be testing core,
// which has no String.
/* The slowest few, because a total alone says the suite is slow without
 * saying which part to look at. Selection rather than a sort: ten passes over
 * a few thousand entries costs nothing and needs no scratch space. */
static void kf_test_print_slowest(void) {
    if (kf_test_recorded == 0) return;
    printf("  total %.1fs across %d tests\n", (double)kf_test_total_ns / 1e9, kf_test_recorded);
    int shown = kf_test_recorded < 10 ? kf_test_recorded : 10;
    unsigned long ceiling = (unsigned long)-1;
    for (int rank = 0; rank < shown; rank++) {
        int best = -1;
        for (int i = 0; i < kf_test_recorded; i++) {
            if (kf_test_elapsed[i] > ceiling) continue;
            if (best < 0 || kf_test_elapsed[i] > kf_test_elapsed[best]) best = i;
        }
        if (best < 0) break;
        /* Below a tenth of a second nothing here is worth a reader's time. */
        if (kf_test_elapsed[best] < 100000000UL) break;
        printf("  %6.2fs  %s\n", (double)kf_test_elapsed[best] / 1e9, kf_test_names[best]);
        ceiling = kf_test_elapsed[best];
        kf_test_elapsed[best] = ceiling + 1; /* consumed; excluded from the next pass */
    }
}

void kf_test_report(int32_t passed, int32_t failed) {
    if (kf_test_failed_count > 0) {
        printf("failures:\n");
        for (int i = 0; i < kf_test_failed_count; i++) printf("  %s\n", kf_test_failed_names[i]);
        printf("\n");
    }
    if (failed == 0) {
        printf("test result: ok. %d passed\n", (int)passed);
    } else {
        printf("test result: FAILED. %d passed, %d failed\n", (int)passed, (int)failed);
    }
    kf_test_print_slowest();
    fflush(stdout);
}
