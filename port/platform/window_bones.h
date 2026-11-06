/*
 * port/platform/window_bones.h
 *
 * The window's part of issue 123's ladder (bones_ladder.h has the rule;
 * docs/port/VU_SKIN_CHECK.md "The ladder").  Its own file because
 * window_host.c is at its file size gate.
 */
#ifndef ICO_PLATFORM_WINDOW_BONES_H
#define ICO_PLATFORM_WINDOW_BONES_H

/* From the window's pump, every pump (cheap: a few atomic loads while the
   skin check has no twins): at the first call the start's line when a key
   of the ladder is set; then the skin check's evidence (rd.h
   rd_vu_bones_evidence) through bones_ladder_step, and the action's key
   with its mark written to config.toml and its line logged, once each. */
void window_bones_pump(void);

#endif
