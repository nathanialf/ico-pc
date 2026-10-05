/*
 * port/platform/kernel_host.h
 *
 * The host side of the EE kernel calls kernel_host.c defines (their game
 * side is port/compat/eekernel.h): raising an INTC cause and the state the
 * kernel keeps for later layers.
 */
#ifndef ICO_PLATFORM_KERNEL_HOST_H
#define ICO_PLATFORM_KERNEL_HOST_H

/* INTC causes (EE INTC_STAT bits). */
#define ICO_INTC_GS 0
#define ICO_INTC_SBUS 1
#define ICO_INTC_VBLANK_S 2
#define ICO_INTC_VBLANK_E 3

/* Clears every handler, mask and recorded value (tests). */
void ico_kernel_reset(void);
/* Raises an INTC cause on the host context: runs its handlers, in the order
   AddIntcHandler built, when the cause is enabled, as interrupt code (no
   thread switch inside). For ICO_INTC_VBLANK_S it also completes a pending
   SetVSyncFlag. Returns the number of handlers run. */
int ico_kernel_raise_intc(int cause);
/* 1 when the cause is enabled (EnableIntc). */
int ico_kernel_intc_enabled(int cause);
/* scePrintf output: off unless the ICO_TTY environment variable is set. */
void ico_kernel_set_tty(int on);
/* The last SetGsCrt arguments, for the presentation layer. */
void ico_kernel_gs_crt(short *interlace, short *omode, short *ffmd);

#endif /* ICO_PLATFORM_KERNEL_HOST_H */
