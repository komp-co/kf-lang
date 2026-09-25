/* Deterministic allocation counter for komp's self-leak scorecard.
 *
 * Linked into the compiled stage.c with:
 *   -Wl,--wrap=malloc,--wrap=free,--wrap=realloc,--wrap=calloc
 * Counts allocation-producing vs free-producing calls and prints, at exit:
 *   ALLOC=<n> FREE=<n> NET=<allocs-frees>
 * NET is "objects still live at exit" = allocations komp never freed
 * (komp leaks by design, so NET is the self-leak volume we drive down).
 *
 * realloc edge cases are handled so NET tracks object count exactly:
 *   realloc(NULL, n>0)  behaves as malloc  -> alloc
 *   realloc(p, 0)       behaves as free    -> free
 *   realloc(p, n>0)     in-place resize     -> neutral
 */
#include <stddef.h>
#include <stdio.h>

extern void *__real_malloc(size_t);
extern void  __real_free(void *);
extern void *__real_realloc(void *, size_t);
extern void *__real_calloc(size_t, size_t);

static unsigned long g_alloc = 0;
static unsigned long g_free  = 0;

void *__wrap_malloc(size_t n)            { g_alloc++; return __real_malloc(n); }
void *__wrap_calloc(size_t a, size_t b)  { g_alloc++; return __real_calloc(a, b); }
void  __wrap_free(void *p)               { if (p) g_free++; __real_free(p); }

void *__wrap_realloc(void *p, size_t n) {
    if (p == NULL) { if (n) g_alloc++; }   /* malloc */
    else if (n == 0) { g_free++; }          /* free   */
    /* else: neutral, same live object */
    return __real_realloc(p, n);
}

__attribute__((destructor))
static void report(void) {
    fprintf(stderr, "ALLOC=%lu FREE=%lu NET=%lu\n",
            g_alloc, g_free, g_alloc - g_free);
}
