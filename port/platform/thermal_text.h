/*
 * port/platform/thermal_text.h
 *
 * Issue 123: the phone's thermal state and whether the screen is being
 * recorded, in the log's words.  The Android app reads them through its
 * Java side (IcoActivity.thermalHeadroom, thermalStatus,
 * nativeThermalStatus, nativeScreenRecording; host_android.h); this file is
 * the pure part: the status names and the four texts
 * (port/platform/test/thermal_text_test.c).  The lines:
 *   ico_pc: thermal status changed: MODERATE (2) -> SEVERE (3) (run time 212.4 s)
 *   ico_pc: screen recording: visible (run time 40.1 s)
 *   window: thermal: headroom 0.72 (1.0 is the throttling point), status
 *     MODERATE (2); screen recording not visible
 *   window: thermal: not available (...)
 * and the end of the "kernel GPU driver around the present that blocked"
 * line: "; thermal headroom 0.98, status SEVERE; screen recording visible".
 * A part the phone does not report is left out.
 */
#ifndef ICO_PLATFORM_THERMAL_TEXT_H
#define ICO_PLATFORM_THERMAL_TEXT_H

#include <stddef.h>

/* What the app last learned.  headroom: PowerManager.getThermalHeadroom's
   forecast, 1.0 the throttling point, NaN when not reported; status:
   PowerManager's THERMAL_STATUS_* 0 (NONE) to 6 (SHUTDOWN), -1 when not
   reported; recording: 1 visible, 0 not visible, -1 never reported (the
   callback is Android 15 and later) */
typedef struct ThermalState {
    double headroom;
    int status;
    int recording;
} ThermalState;

/* nothing reported yet */
ThermalState thermal_state_none(void);
/* anything of s is reported */
int thermal_state_any(const ThermalState *s);

/* "NONE", "LIGHT", "MODERATE", "SEVERE", "CRITICAL", "EMERGENCY",
   "SHUTDOWN"; "UNKNOWN" for other values */
const char *thermal_status_name(int status);

/* "ico_pc: thermal status changed: MODERATE (2) -> SEVERE (3) (run time 212.4 s)" */
int thermal_change_text(int oldStatus, int newStatus, double runSeconds, char *buf, size_t size);
/* "ico_pc: screen recording: visible (run time 40.1 s)" (state 1) or "not visible" */
int thermal_recording_text(int state, double runSeconds, char *buf, size_t size);
/* "thermal: headroom 0.72 (1.0 is the throttling point), status MODERATE
   (2); screen recording not visible", the parts reported; with none
   "thermal: not available (<why>)" ("window: " goes before it) */
int thermal_block_text(const ThermalState *s, const char *why, char *buf, size_t size);
/* "; thermal headroom 0.98, status SEVERE; screen recording visible" (the
   parts reported, each after "; "), "" when none is */
int thermal_around_text(const ThermalState *s, char *buf, size_t size);

#endif /* ICO_PLATFORM_THERMAL_TEXT_H */
