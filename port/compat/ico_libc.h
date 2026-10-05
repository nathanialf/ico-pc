/*
 * port/compat/ico_libc.h
 *
 * The C library functions the game calls, sorted by whether the host's
 * implementation can stand in for the newlib build the game linked
 * (sce/libc, sce/libm). Phase 0 links the host libc for all of them; package
 * 1A replaces the first group with ico_* copies of the newlib code so the
 * results are the PS2's on every host.
 *
 * Hookup: port/compat/math.h and port/compat/stdlib.h stand in for the host
 * headers on the game's include path. Each includes the host header with
 * #include_next and then, when ICO_HOST is defined, this header, which
 * declares the copies (port/math/newlib/ico_newlib.h) and maps the game's
 * names onto them with object-like macros: after <stdlib.h>, rand, srand,
 * qsort and RAND_MAX; after <math.h>, atan2f, acosf, asinf, sinf, cosf,
 * fmodf and sqrtf. The host header is complete before its group of macros
 * is defined, so the host's own declarations keep their names; a later
 * declaration of one of these names (a game header's own prototype) is
 * renamed with the calls. Code outside the game's include path (port/,
 * SDL) keeps the host functions.
 *
 * Counts are call sites in ico2/ (grep, 2026-10-05). The host build's object
 * files reference the functions marked [obj]; the rest are only called from
 * sources still excluded from the build (docs/port/BUILD_STATUS.md).
 *
 * 1. Results differ between C libraries: must become newlib copies (1A).
 *    Done for rand, qsort, atan2f, acosf, asinf, sinf, fmodf (and cosf, sqrtf,
 *    which the EE build does not link): port/math/newlib.
 *      rand        35 [obj]  newlib's LCG and RAND_MAX 0x7FFFFFFF; glibc and
 *                            the Windows CRTs use other generators
 *      qsort        6 [obj]  order of equal elements is unspecified
 *      atan2f      21 [obj]  newlib's fdlibm float code; host libms differ
 *      acosf        7 [obj]    in the last bits and in the PS2 FPU's
 *      asinf        3 [obj]    round-toward-zero, flush-to-zero mode
 *      sinf         1 [obj]
 *      fmodf        1 [obj]
 *      sqrt         3        double, soft-float on the EE (libgcc)
 *      floor        1        double
 *      sprintf    167 [obj]  float formatting (%f, %g) and %p differ
 *      vsprintf     7          the EE's va_list is a char *; the callers'
 *                              va_list handling needs a host review
 *      sscanf       4 [obj]  float parsing
 *      strtol, atoi 4 [obj]  identical for valid input; overflow differs
 *      strtok       6 [obj]  newlib keeps its state in the reent record
 *      toupper      1 [obj]  locale tables; the game's "C" locale only
 *
 * 2. Identical results on any conforming libc: may stay the host's.
 *      memcpy 31, memset 180, memcmp 1, strcat 9, strcmp 42, strncmp 6,
 *      strcpy 28, strncpy 4, strlen 37, strrchr 4, strstr 1, abs 4,
 *      printf 3, free 1
 *    Several sources redeclare these with newlib's or K&R prototypes
 *    (`extern void *memset(void *, int, int)`, `extern void memcpy()`)
 *    that conflict with the host's <string.h>; package 0B removes them.
 *
 * 3. newlib internals the game names.
 *      __assert            port/compat/assert.h renames it ico_assert
 *                          (port/platform/assert_host.c)
 *
 * The EE's `long` is 64 bits and its size_t 32. Prototypes the game spells
 * with `unsigned int` sizes do not match the host's `size_t`.
 */
#ifndef ICO_COMPAT_ICO_LIBC_H
#define ICO_COMPAT_ICO_LIBC_H
#include "../math/newlib/ico_newlib.h"
#endif /* ICO_COMPAT_ICO_LIBC_H */

/* The macro groups sit outside the include guard: math.h and stdlib.h may
   arrive in either order, and each group is defined once its host header
   is complete (ICO_COMPAT_HOST_*_DONE, set by the compat headers). */
#ifdef ICO_HOST

#if defined(ICO_COMPAT_HOST_STDLIB_DONE) && !defined(ICO_LIBC_STDLIB_MAPPED)
#define ICO_LIBC_STDLIB_MAPPED 1
#define rand ico_rand
#define srand ico_srand
#define qsort ico_qsort
#undef RAND_MAX
#define RAND_MAX ICO_RAND_MAX
#endif

#if defined(ICO_COMPAT_HOST_MATH_DONE) && !defined(ICO_LIBC_MATH_MAPPED)
#define ICO_LIBC_MATH_MAPPED 1
#define atan2f ico_atan2f
#define acosf ico_acosf
#define asinf ico_asinf
#define sinf ico_sinf
#define cosf ico_cosf
#define fmodf ico_fmodf
#define sqrtf ico_sqrtf
#endif

#endif /* ICO_HOST */
