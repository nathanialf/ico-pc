/*
 * port/platform/state_port.c
 *
 * The port's blocks in the save state registry, in one fixed order
 * (state_port.h).  Linked into the program only: it names globals of
 * libraries the platform layer does not depend on.
 */
#include "state_port.h"

#include <stdio.h>

#include "state_registry.h"

int ico_state_register_port(void)
{
    static int done;
    static int result;

    if (done) {
        return result;
    }
    done = 1;
    result |= ico_kernel_state_register();
    result |= ico_clock_state_register();
    result |= ico_hwregs_state_register();
    result |= ico_rand_state_register();
    result |= ico_matrix_state_register();
    result |= ico_matrix_stack_state_register();
    result |= ico_cdvd_host_state_register();
    result |= ico_iop_ram_state_register();
    result |= ico_sif_host_state_register();
    result |= ico_trace_state_register();
    result |= ico_pad_script_state_register();
    result |= ico_door_grace_state_register();
    result |= ico_gfx_null_state_register();
    result |= ico_host_loop_state_register();
    result |= ico_audio_state_register();
    result |= ico_sound_state_register();
    if (result != 0) {
        result = -1;
        fprintf(stderr, "state: the port's blocks are not all registered (%d of them are)\n",
                ico_state_registry_count());
    }
    return result;
}
