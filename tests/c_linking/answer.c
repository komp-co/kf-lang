#include <stdint.h>

#include "c_linking.h"

/* A trivial C shim shipped by the crate itself and linked via
   [native] c_sources in kf.toml. */
int32_t answer(void) {
    return 42;
}

/* `Pair` is written `@no_mangle`, so its C name is the one KFlat declares. */
int32_t pair_sum(Pair* p) {
    return p->a + p->b;
}
