#ifndef KF_NATIVE_UNITY
#include "kf_runtime.h"
/* alloc's own generated header, for the `String` these return. */
#include "alloc.h"
#endif

#include <stdint.h>
#include <stdio.h>

/* Rendering a number is hosted because snprintf is. The String it returns is
 * built through `__kf_v2_str_from_cstr` rather than the freestanding file's
 * own `kf_string_from`, which is static: one entry point across the tier
 * boundary, and it is the one generated code already uses.
 */
String __kf_v2_int32_to_str(int32_t value) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%d", value);
    return __kf_v2_str_from_cstr(buffer);
}

String int64_display(int64_t value) {
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%lld", (long long)value);
    return __kf_v2_str_from_cstr(buffer);
}

String uint64_display(uint64_t value) {
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)value);
    return __kf_v2_str_from_cstr(buffer);
}
