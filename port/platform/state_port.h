/*
 * port/platform/state_port.h
 *
 * The port's simulation globals in the save state registry
 * (state_registry.h): each owner registers its own blocks with the
 * function below, and ico_state_register_port calls them all once, in this
 * order (the registry's order is part of the build id, state_file.h).
 *
 * What is registered and why (plan "156 design", Registry; the game's own
 * .data/.bss is not here: it is saved whole, game_data_range.h):
 *
 *   kernel_host.c   the INTC/DMAC handler tables (function pointers: SCAN),
 *                   the masks, the next handler id, the SetVSyncFlag
 *                   pointers (SCAN) and the GS interrupt mask
 *   clock.c         the EE timers' rate and carried fractions
 *   hwregs.c        the EE I/O and GS privileged pages
 *   rand.c          rand()'s state
 *   matrix.c        VU0's R register
 *   matrix_stack.c  the current matrix, the matrix and VU0 save stacks and
 *                   the rotate-translate cache
 *   cdvd_host.c     the drive's command, error and stream state (the host
 *                   objects in the same record, the mounted disc, the
 *                   identification pointer and the clock callback, are
 *                   left out)
 *   iop_ram.c       IOP RAM and its heap table
 *   sif_host.c      the RPC counters and the module and DMA ids
 *   trace_host.c    the Main tick count (CLOCK)
 *   pad_script.c    the pad script's tick (CLOCK)
 *   door_grace.c    the doorway fix's arm and grace
 *   gfx_null.c      the DMA channel register blocks
 *   host_loop.c     the vsync count and the next field
 *
 * Not registered: the scheduler's tables (ico_sched_state_save/restore
 * move their pointers themselves: a scan of them here would move them
 * twice), the vsync hook list and clock.c's "attached" flag that mirrors
 * it (host callbacks set up at start-up), FILE pointers, the trace file's
 * bookkeeping, the SIF server table (host callbacks), audio device state.
 */
#ifndef ICO_PLATFORM_STATE_PORT_H
#define ICO_PLATFORM_STATE_PORT_H

/* Registers every block below, once (later calls return the first call's
   result).  0, or -1 when a block could not be registered (the log names
   it): a state would then be incomplete, so save states stay off. */
int ico_state_register_port(void);

/* The owners' registrations: 0, or -1 when ico_state_register refused a
   block. */
int ico_kernel_state_register(void);       /* kernel_host.c */
int ico_clock_state_register(void);        /* clock.c */
int ico_hwregs_state_register(void);       /* hwregs.c */
int ico_rand_state_register(void);         /* port/math/newlib/rand.c */
int ico_matrix_state_register(void);       /* port/math/matrix.c */
int ico_matrix_stack_state_register(void); /* port/math/matrix_stack.c */
int ico_cdvd_host_state_register(void);    /* port/data/cdvd_host.c */
int ico_iop_ram_state_register(void);      /* port/data/iop_ram.c */
int ico_sif_host_state_register(void);     /* port/data/sif_host.c */
int ico_trace_state_register(void);        /* trace_host.c */
int ico_pad_script_state_register(void);   /* port/input/pad_script.c */
int ico_door_grace_state_register(void);   /* port/game/door_grace.c */
int ico_gfx_null_state_register(void);     /* port/null/gfx_null.c */
int ico_host_loop_state_register(void);    /* host_loop.c */

#endif /* ICO_PLATFORM_STATE_PORT_H */
