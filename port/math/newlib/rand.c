/*
 * port/math/newlib/rand.c
 *
 * ico_rand, ico_srand: host copies of newlib's rand and srand as the game
 * linked them (sce/libc/stdlib/rand.c, libc.a member rand.o).
 *
 * newlib's libc/stdlib/rand.c carries no notice of its own; it is covered
 * by item (1) of newlib's COPYING.NEWLIB:
 *
 *   (1) Red Hat Incorporated
 *
 *   Copyright (c) 1994-2009  Red Hat, Inc. All rights reserved.
 *
 *   This copyrighted material is made available to anyone wishing to use,
 *   modify, copy, or redistribute it subject to the terms and conditions
 *   of the BSD License.   This program is distributed in the hope that
 *   it will be useful, but WITHOUT ANY WARRANTY expressed or implied,
 *   including the implied warranties of MERCHANTABILITY or FITNESS FOR
 *   A PARTICULAR PURPOSE.  A copy of this license is available at
 *   http://www.opensource.org/licenses. Any Red Hat trademarks that are
 *   incorporated in the source code or documentation are not subject to
 *   the BSD License and may only be used or replicated with the express
 *   permission of Red Hat, Inc.
 *
 * The EE keeps the state in the reentrancy record (_impure_ptr + 0x58,
 * rand_next), initialised to 1 by _REENT_INIT (sce/libc/reent/impure.c).
 * The game has one thread of simulation and never calls srand, so one
 * static word stands in for it. The arithmetic is unsigned so the wrap is
 * defined; the EE's 32-bit multiply-low gives the same bits.
 */
#include <stdint.h>

#include "ico_newlib.h"

static uint32_t ico_rand_next = 1;

void ico_srand(unsigned int seed)
{
    ico_rand_next = (uint32_t)seed;
}

int ico_rand(void)
{
    uint32_t s = ico_rand_next * UINT32_C(0x41C64E6D) + UINT32_C(0x3039);
    ico_rand_next = s;
    return (int)(s & UINT32_C(0x7fffffff));
}
