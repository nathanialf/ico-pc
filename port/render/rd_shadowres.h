/* rd_shadowres.h: Options > Effects > Shadow detail: High (rd_shadowres.c;
 * docs/port/RENDER_POST.md "Shadow detail: High").
 *
 * The arithmetic (pure, the CPU test's): one axis at a time, positions in
 * GS pixels of the level drawn, the source's texels at s_src texels per GS
 * pixel of the source, the level's at s_dst.  A level texel m stands for
 * the GS position m / s_dst (the PS2's texel x for x); its two taps are the
 * source at 2 m / s_dst and one GS pixel further, weighted 0.75 and 0.25
 * (the GS's bilinear tap at u = 2 x + 0.75 of the game's sprites).  The
 * first level's tap is the box of count texels its GS pixel covers; a later
 * level's the level before at that place, linear between its texels.  The
 * tent a level then takes on each axis: weights s - |t - c| over t = n - m,
 * c = s / 2 - 1/2, normalised. */
#ifndef ICO_RD_SHADOWRES_H
#define ICO_RD_SHADOWRES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rd.h"
#include "rhi.h"

struct Replay;
struct RdStateBlock;
struct RdCmd;
struct RdTargetRec;
struct RdPerfRecord;

#define RD_SR_LEVELS 3
/* the step's and the tent's loop limits (shadowres.hlsl SR_SPAN_MAX,
 * SR_TENT_MAX): a scale above RD_SR_SCALE_MAX on an axis keeps Original */
#define RD_SR_SPAN_MAX 64
#define RD_SR_SCALE_MAX 31.0f

/* ----------------------------------------------------------- arithmetic */
/* the count's texels per GS pixel on an axis of w GS pixels held in tw
 * texels (sx the target's scale, ox its Full pixel left-out texels: the
 * scale is sx where ox > 0, else tw / w) */
float rd_sr_count_scale(uint32_t w, uint32_t tw, float sx, float ox);
/* a level's texels per GS pixel on an axis whose count is at scale s: 1 at
 * a scale of 1 or less (the axis keeps the PS2's level), else the smallest
 * odd whole number at or above s (3 at 2x, 2.41x and 3x, 5 at 4x and
 * 4.82x): at an odd whole scale the step's taps fall on texels and the tent
 * is centred on one, so every stage is exact (rd_shadowres.c) */
uint32_t rd_sr_level_scale(float s);
/* the box weight of source texel i for a first-level tap at a: [a, a + s)
 * at 0.75 / s, [a + s, a + 2 s) at 0.25 / s (shadowres.hlsl sr_box_w) */
float rd_sr_box_w(float i, float a, float s);
/* the tent's weight at t = n - m on an axis of scale s (unnormalised) */
float rd_sr_tent_w(int t, float s);
/* the tent's first and last t on an axis of scale s */
void rd_sr_tent_range(float s, int *t0, int *t1);

/* The reference of the two shaders over RGBA8 images (the CPU test's): a
 * level texel (mx, my) of the step from src (sw x sh texels): first 1 the
 * box mode (the count), else the point mode; ax, ay the taps' origin (texels,
 * shadowres.hlsl's g_scale), kx, ky the texels a destination texel moves the
 * taps (g_param.xy), sx, sy the tap's offset (g_param.zw), col the sprite's
 * colour; and a texel n of the tent along one axis (down 0 across) of a w x
 * h level of scale s */
void rd_sr_step_texel(const uint8_t *src, uint32_t sw, uint32_t sh, int first, float kx, float ky,
                      float ax, float ay, float sx, float sy, const uint8_t col[4], uint32_t mx,
                      uint32_t my, uint8_t out[4]);
void rd_sr_tent_texel(const uint8_t *src, uint32_t w, uint32_t h, int down, float s, uint32_t x,
                      uint32_t y, uint8_t out[4]);

/* ------------------------------------------------------------ the replay */
/* rd_no_volume.c rd__no_volume_skip, at the top of every screen command
 * (doScreen): the replay the command is drawn in */
void rd__shadow_high_replay(struct Replay *r);
/* rd_shadow_box.c rd__shadow_box_reset: a new count (a new chain) */
void rd__shadow_high_reset(uint32_t color);
/* rd_shadow_one.c: the count target tc (scaled) is to be drawn at its full
 * size too (the option High, a scale above 1 on an axis, not refused) */
bool rd__shadow_high_count_wanted(const struct RdTargetRec *tc);
/* the count of target color holds the full size (the seven draws, MSAA,
 * the one draw's full-size pass) */
void rd__shadow_high_count_full(uint32_t color);
/* rd_shadow_box.c rd__shadow_box_screen, before the draw's pass: a level
 * sprite of the chain is drawn at the scene's scale too (and the tent
 * after the third) */
void rd__shadow_high_screen(const struct RdStateBlock *st, const struct RdCmd *c,
                            const RdScreenVtx *v, uint32_t n);
/* rd_replay.c resolveTexture: the fine level that stands for level target
 * `target` this replay, read by a draw into drawTarget; {0} none */
RhiTexture rd__shadow_fine(uint32_t target, uint32_t drawTarget);
/* the texture is one of the fine levels (rd_fx_plan.c: no B-spline) */
bool rd__shadow_high_tex(uint32_t texId);
/* the level target holds a fine twin this replay (rd_shadow_tile.c steps
 * aside) */
bool rd__shadow_high_level(uint32_t target);
/* the option's line at the renderer's start and at a change */
void rd__shadow_high_log(const char *when);
/* the fine levels freed (the option back to Original; logged when held) */
void rd__shadow_high_free(const char *why);
void rd__shadow_high_shutdown(void);
/* the 10 s clause and the --stats words */
void rd__shadow_high_pop(const struct RdPerfRecord *rec);
void rd__shadow_high_text(char *out, size_t size, size_t *at);
void rd__shadow_high_stats(const struct RdPerfRecord *rec, char *out, size_t size);

#endif
