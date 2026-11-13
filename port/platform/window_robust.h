/*
 * port/platform/window_robust.h
 *
 * The window's part of the port's own [dev] gpu_robust turning itself off
 * (robust_persist.h has the rule).  Its own file because window_host.c is
 * at its file size gate.
 */
#ifndef ICO_PLATFORM_WINDOW_ROBUST_H
#define ICO_PLATFORM_WINDOW_ROBUST_H

/* At the start, after the previous start's end was read
   (ico_diag_startup_activity_begin) and before the device is made: a
   [dev] gpu_robust the game wrote (its mark gpu_robust_by) counts the
   previous start, logs one line, and is removed from config.toml (and from
   this start's environment) when the rule says so.  A gpu_robust = true
   without the mark (a game's that wrote none, or the player's) is removed
   at once, as a marked key of another version is, and logged once; false
   and ico-pc.ini's gpu_robust are not touched (the ini's line said every
   start).  The attempt ledger's old robust-access row says which. */
void window_robust_start(const char *cfgName);
/* [dev] gpu_robust = true with the game's mark, written for good by the
   start that lost the device twice in a row (window_host.c
   gpu_robust_persist); 0 when written */
int window_robust_persist(void);
/* This start reached the game (the start-up set made, or the self-tests
   done on a run that makes its pipelines on first use): the mark says so,
   for the next start to judge */
void window_robust_reached(void);

#endif
