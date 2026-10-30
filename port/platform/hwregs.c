/*
 * port/platform/hwregs.c
 *
 * The memory blocks port/compat/eeregs.h points the EE register names into:
 * the EE I/O page (0x10000000) and the GS privileged page (0x12000000).
 * Nothing reacts to them; see eeregs.h.
 */
#include <eeregs.h>

#include "state_port.h"
#include "state_registry.h"

__attribute__((aligned(16))) volatile unsigned char ico_hw_eeio[0x10000];

__attribute__((aligned(16))) volatile unsigned char ico_hw_gs[0x2000];

/* The save state's blocks (state_port.h): register values the game wrote
   and the timers count in (EE addresses and counts, no host pointers). */
int ico_hwregs_state_register(void)
{
    int rc = 0;

    rc |= ico_state_register("hw.eeio", (void *)ico_hw_eeio, sizeof ico_hw_eeio, 0);
    rc |= ico_state_register("hw.gs", (void *)ico_hw_gs, sizeof ico_hw_gs, 0);
    return rc != 0 ? -1 : 0;
}
