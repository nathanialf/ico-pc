/*
 * port/compat/math.h
 *
 * Stands in for <math.h> on the game's include path: the host's header
 * first, then, in the host build (ICO_HOST), ico_libc.h's macros that send
 * the game's newlib libm calls (atan2f, acosf, asinf, sinf, cosf, fmodf,
 * sqrtf) to the copies in port/math/newlib. The host header is included
 * before the macros exist, so its declarations keep their names.
 */
#ifndef ICO_COMPAT_MATH_H
#define ICO_COMPAT_MATH_H

#include_next <math.h>
/* set only now: a host header that includes the other one must not see
   ico_libc.h map these names while this host header is still open */
#define ICO_COMPAT_HOST_MATH_DONE 1

#ifdef ICO_HOST
#include "ico_libc.h"
#endif

#endif /* ICO_COMPAT_MATH_H */
