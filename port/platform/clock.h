/*
 * port/platform/clock.h
 *
 * The host's clocks (docs/port/CONFIG.md): the wall clock sceCdReadClock
 * reports, and the EE timers T0..T3 (docs/port/PLATFORM.md, "EE timers").
 */
#ifndef ICO_PLATFORM_CLOCK_H
#define ICO_PLATFORM_CLOCK_H

#include <stdint.h>

/* The record libcdvd's sceCdReadClock fills (sceCdCLOCK): a status byte, then
   BCD fields; year is the two low digits of the year (2000-based). */
typedef struct IcoClockBcd {
    uint8_t stat;
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t pad;
    uint8_t day;
    uint8_t month;
    uint8_t year;
} IcoClockBcd;

/* Fills *out from decimal fields (year four digits). */
void ico_clock_pack(IcoClockBcd *out, int year, int month, int day, int hour, int minute,
                    int second);
/* The clock as the game reads it: the host's local wall clock, or when the
   clock is fixed (ICO_FIXED_CLOCK=1 in the environment, which the config layer
   sets from [dev] fixed_clock; 1 when unset) 2002-01-01 00:00:00, so that
   trace runs and the save serial (layout_action.c mcMakeSerial packs this
   clock) are reproducible. */
void ico_clock_now(IcoClockBcd *out);
/* Overrides the environment's choice (1 fixed, 0 real). */
void ico_clock_set_fixed(int fixed);

/* The EE timers: the counters in ico_hw_eeio (T0_COUNT at +0x0000, T1 at
   +0x0800, T2 at +0x1000, T3 at +0x1800; the mode word is 0x10 above each).
   They advance in simulated time while the mode's CUE bit (0x80) is set, at
   the rate the mode's CLKS field selects (0 bus clock 147.456 MHz, 1 /16,
   2 /256, 3 the horizontal blank), 16 bits wide; a wrap sets the overflow
   flag (mode bit 0x800). The vsync hook ico_clock_timers_attach registers
   steps them by one vsync; a write to a counter or mode word by the game is
   respected. */
/* Registers the vsync hook (once). sceScfGetLanguage does it on its first
   call, during the game's boot; the host's main may call it earlier. Not a
   static constructor: tests that supply their own vsync callback list count
   its registrations. */
void ico_clock_timers_attach(void);
void ico_clock_set_vsync_hz(int hz); /* 50 (the default, PAL) or 60 (59.94) */
/* Advances the timers by frac/65536 of a vsync (65536: a whole one): the
   hook's step, and the way for a caller inside a frame to give a sub-vsync
   estimate. */
void ico_clock_timers_step(unsigned frac_q16);
/* Zeroes the counters, the modes and the fractions of a tick carried over. */
void ico_clock_timers_reset(void);

#endif /* ICO_PLATFORM_CLOCK_H */
