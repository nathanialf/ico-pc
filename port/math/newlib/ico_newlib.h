/*
 * port/math/newlib/ico_newlib.h
 *
 * Host copies of the newlib functions the game calls, so their results are
 * the PS2's on every host instead of the host C library's. The sources are
 * this repository's reconstructions of the newlib members the game linked
 * (sce/libc/stdlib, sce/libm); each .c file here says which member it
 * follows. port/compat/ico_libc.h maps the game's calls (rand, qsort,
 * atan2f, ...) onto these names when ICO_HOST is defined.
 *
 * Nothing here calls the host libm. The float functions assume the
 * caller's floating-point environment is the simulation's
 * (port/platform/fpenv.h, round toward zero with flush-to-zero), as on the
 * EE, and that float maths is not evaluated in excess precision
 * (FLT_EVAL_METHOD 0: SSE on x86, see cmake/IcoFlags.cmake).
 */
#ifndef ICO_MATH_NEWLIB_ICO_NEWLIB_H
#define ICO_MATH_NEWLIB_ICO_NEWLIB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* newlib's RAND_MAX; the Windows CRTs define 0x7FFF. */
#define ICO_RAND_MAX 0x7FFFFFFF

/* sce/libc/stdlib/rand.c: the 32-bit LCG, seed 1 at start-up. */
int ico_rand(void);
void ico_srand(unsigned int seed);
/* the LCG's state word as it is now, for the hand probe's diagnostic lines */
unsigned int ico_rand_state(void);

/* sce/libc/stdlib/qsort.c (BSD qsort). The EE prototype takes 32-bit
   unsigned counts; the host one takes size_t, as the game's call sites are
   compiled against the host <stdlib.h>. */
void ico_qsort(void *base, size_t n, size_t es, int (*cmp)(const void *, const void *));

/* sce/libm: fdlibm's single-precision functions with newlib's X/Open
   wrappers (_LIB_VERSION == _XOPEN_, sce/libm/common/s_lib_ver.c). */
float ico_atan2f(float y, float x);
float ico_acosf(float x);
float ico_asinf(float x);
float ico_sinf(float x);
float ico_cosf(float x); /* not linked in the EE build; see ico_libm.c */
float ico_fmodf(float x, float y);
float ico_sqrtf(float x); /* the EE build links only the core; see ico_libm.c */

#ifdef __cplusplus
}
#endif

#endif /* ICO_MATH_NEWLIB_ICO_NEWLIB_H */
