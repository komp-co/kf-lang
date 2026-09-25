#ifndef KF_NATIVE_UNITY
#include "kf_runtime.h"
#endif

#include <stdint.h>
#include <time.h>

/* Clocks.
 *
 * Two of them, because they answer different questions and neither
 * substitutes for the other. CLOCK_REALTIME is the wall clock: it names an
 * instant everyone agrees on, and it can jump when the machine is corrected
 * or the user changes it. CLOCK_MONOTONIC only ever moves forward at one
 * second per second, from an unspecified origin -- useless for saying WHEN
 * something happened, and the only correct choice for saying HOW LONG it
 * took.
 *
 * Measuring a duration against the wall clock is the classic bug: an NTP
 * correction mid-measurement produces a negative elapsed time, and code that
 * assumed otherwise divides by it.
 */

int64_t kf_time_unix_seconds(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec;
}

int64_t kf_time_unix_nanos(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * 1000000000 + (int64_t)ts.tv_nsec;
}

int64_t kf_time_monotonic_nanos(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * 1000000000 + (int64_t)ts.tv_nsec;
}

/* Sleeps at least `nanos`, resuming the wait if a signal interrupts it. A
 * bare nanosleep returns early on EINTR, which turns "wait 100ms" into
 * "wait until something happens, but no longer than 100ms". */
void kf_time_sleep_nanos(int64_t nanos) {
    if (nanos <= 0) return;
    struct timespec req;
    req.tv_sec = (time_t)(nanos / 1000000000);
    req.tv_nsec = (long)(nanos % 1000000000);
    struct timespec rem;
    while (nanosleep(&req, &rem) != 0) {
        req = rem;
    }
}
