/*
 * port/platform/window_thermal.h
 *
 * The window's side of the phone's heat and clock state (thermal_read.h,
 * issue 179): on Android the log thread reads it once a second beside the
 * battery (window_stall.c asks_tick: the system's thermal status and
 * headroom through libandroid.so's AThermal calls looked up at run time,
 * the chosen thermal zones, the CPU clusters' clocks), into a last-value
 * cell; the game thread takes each new reading as it drains records
 * (perf_secs' seconds, the stall rows) and writes the once line and the
 * block's lines with the pictures lines (window_perf_rank.c).  Elsewhere
 * nothing is read and one line says so.
 */
#ifndef ICO_PLATFORM_WINDOW_THERMAL_H
#define ICO_PLATFORM_WINDOW_THERMAL_H

#include <stdbool.h>
#include <stddef.h>

#include "thermal_read.h"

/* Android, the log thread, once a second: javaHeadroom the headroom the
   thermal ask just had from PowerManager (< 0 or NaN none: then the NDK's
   own is asked, so the two never both ask in one second) and javaStatus
   its status (< 0 none: used where the NDK's is not there) */
void window_thermal_tick(double atMs, double javaHeadroom, int javaStatus);
/* The game thread, at each drained record: a new reading into the
   pictures' seconds (perf_secs_window_phone) */
void window_thermal_take(void);
/* The reading of the last POWER_FRESH_MS (power_read.h), false none */
bool window_thermal_now(ThermalSample *out);
/* The clusters' cores once found (NULL before, or none) */
const ThermalCpus *window_thermal_cpus(void);
/* The once line ("window: thermal: ..."), the first call after it is
   known; elsewhere than Android "thermal: not read here (the thermal API,
   the thermal zones and the CPU clocks are read on Android alone)" */
void window_thermal_open_line(void);

#endif
