/* rd_jitter.h: a sub-pixel offset of the scene's picture, the temporal
 * upscaler's jitter (port/render/rd_jitter.c; docs/port/RENDER.md
 * "Jitter").  The GS position is made as the PS2 makes it (the divide, the
 * 1/16 snap, the region test); the jitter moves it after, in texels of the
 * bound target, inside the frame group of a scene-class pass
 * (rd_replay.c frameGroupOf -> rd__frame_group_ex: FrameCB vpo.xy, and
 * jitter.xy for the pixel side's helpers to take it out again).  Every
 * other frame group, every pass of a frame with no jitter set, and every
 * caller of rd__frame_group are unchanged.  Off (nothing set) adds nothing;
 * a jitter of 0, 0 adds 0.0, which leaves every finite float as it was. */
#ifndef ICO_RD_JITTER_H
#define ICO_RD_JITTER_H

#include <stdbool.h>
#include <stdint.h>

struct RdTargetRec;

/* The jitter of the frames replayed from now on, x right and y down in
 * texels of the bound target (a temporal upscaler's offsets are within
 * -0.5..0.5); set (even to 0, 0) until rd__frame_jitter_clear.  Not to be
 * called while a frame replays (the frame groups of one replay share it). */
void rd__frame_jitter_set(float x, float y);
/* No jitter from now on: the frame groups are as before the jitter existed */
void rd__frame_jitter_clear(void);
/* The jitter set (true, out x, y), or false (out 0, 0); the first call with
 * nothing set reads the developer switch ICO_RD_JITTER_TEST=x,y ([dev]
 * jitter_test = x,y, port/platform/host_config.c) and logs it once */
bool rd__frame_jitter_get(float out[2]);
/* rd_replay.c frameGroupOf: the jitter of a frame group made for target
 * tc, NULL when it takes none (no jitter set, or a target outside the
 * scene class the jitter moves: SCENE, WORK2 (drawn with SCENE's Z, read
 * pixel for pixel onto it) and AURA_WORK (SCENE's Z); not DISPLAY, the
 * reduction's target, nor DISPLAY_HELD, the work buffers, the shadow levels
 * or a temporary target).  Counts the frame groups it jitters. */
const float *rd__frame_jitter_of(const struct RdTargetRec *tc);
/* The frame groups rd__frame_jitter_of jittered since rd__frame_jitter_set
 * (--stats' "jitter" line) */
uint32_t rd__frame_jitter_groups(void);
/* "x,y" (two numbers -0.5..0.5, a comma between, spaces allowed) into
 * out; false (out untouched) for anything else */
bool rd__frame_jitter_parse(const char *s, float out[2]);

/* The Halton (2, 3) sequence as a temporal upscaler's jitter: out[0] the
 * base-2 radical inverse of index + 1, out[1] the base-3 one, each less 0.5
 * (-0.5..0.5), the double value rounded to float once.  index 0 gives
 * (0, -1/6), 1 gives (-1/4, 1/6), 2 gives (1/4, -7/18).  FSR's own jitter
 * (ffxFsr3GetJitterOffset) is this sequence at (frame index % phase count),
 * the caller's reduction. */
void rd__jitter_halton(uint32_t index, float out[2]);

#endif
