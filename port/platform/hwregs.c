/*
 * port/platform/hwregs.c
 *
 * The memory blocks port/compat/eeregs.h points the EE register names into:
 * the EE I/O page (0x10000000) and the GS privileged page (0x12000000).
 * Nothing reacts to them; see eeregs.h.
 */
#include <eeregs.h>

__attribute__((aligned(16))) volatile unsigned char ico_hw_eeio[0x10000];

__attribute__((aligned(16))) volatile unsigned char ico_hw_gs[0x2000];
