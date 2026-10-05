/*
 * port/compat/stdlib.h
 *
 * Stands in for <stdlib.h> on the game's include path: the host's header
 * first, then, in the host build (ICO_HOST), ico_libc.h's macros that send
 * the game's rand, srand and qsort calls, and RAND_MAX, to the newlib
 * copies in port/math/newlib. The host header is included before the
 * macros exist, so its declarations keep their names.
 */
#ifndef ICO_COMPAT_STDLIB_H
#define ICO_COMPAT_STDLIB_H

#include_next <stdlib.h>
/* set only now: a host header that includes the other one must not see
   ico_libc.h map these names while this host header is still open */
#define ICO_COMPAT_HOST_STDLIB_DONE 1

#ifdef ICO_HOST
#include "ico_libc.h"
#endif

#endif /* ICO_COMPAT_STDLIB_H */
