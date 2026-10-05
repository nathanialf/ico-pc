/*
 * port/null/null_devices.h
 *
 * Controls of the null devices the headless build links in place of the
 * memory card and system configuration (mc_null.c, scf_null.c).  gfx_null.c
 * (package 1B) has its own.  The null sound driver (snd_null.c) is gone: the
 * real one is port/audio/sndn2_host.h (Phase 4B); so is the null pad: the
 * libpad is port/input/pad_host.h's (Phase 4C).
 */
#ifndef ICO_PORT_NULL_DEVICES_H
#define ICO_PORT_NULL_DEVICES_H

/* sceScfGetLanguage's answer, in libscf's numbering (0 Japanese, 1 English,
   2 French, 3 Spanish, 4 German, 5 Italian, 6 Dutch, 7 Portuguese).  The
   config layer sets it later from the system locale (plan, "Boot"). */
#define ICO_SCF_LANGUAGE_JAPANESE 0
#define ICO_SCF_LANGUAGE_ENGLISH 1
#define ICO_SCF_LANGUAGE_FRENCH 2
#define ICO_SCF_LANGUAGE_SPANISH 3
#define ICO_SCF_LANGUAGE_GERMAN 4
#define ICO_SCF_LANGUAGE_ITALIAN 5

extern int ico_scf_language;

/* The memory card result the null card reports for every request: "no card
   in the slot" (sceMcGetInfo's -10 and below). */
#define ICO_MC_NULL_RESULT (-10)

#endif /* ICO_PORT_NULL_DEVICES_H */
