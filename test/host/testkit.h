#ifndef TESTKIT_H
#define TESTKIT_H

/* testkit.h -- the smallest possible test harness (as in Reflector). */

#include <stdio.h>

static int tk_run = 0;
static int tk_fail = 0;

#define CHECK(cond) \
    do { tk_run++; if (!(cond)) { tk_fail++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define CHECK_INT(got, want) \
    do { long g_ = (long) (got), w_ = (long) (want); tk_run++; if (g_ != w_) { tk_fail++; \
        printf("FAIL %s:%d: %s = %ld, want %ld\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)

#define CHECK_HEX(got, want) \
    do { unsigned long g_ = (unsigned long) (got), w_ = (unsigned long) (want); tk_run++; \
        if (g_ != w_) { tk_fail++; \
        printf("FAIL %s:%d: %s = 0x%08lx, want 0x%08lx\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)

#ifdef __GNUC__
__attribute__((unused))
#endif
static int tk_done(const char *name)
{
    if (tk_fail)
        printf("%s: %d of %d checks FAILED\n", name, tk_fail, tk_run);
    else
        printf("%s: OK (%d checks)\n", name, tk_run);
    return tk_fail ? 1 : 0;
}

#endif
