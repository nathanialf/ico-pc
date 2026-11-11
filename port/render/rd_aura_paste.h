/* rd_aura_paste.h: the aura's paste as the alpha alone where it reads
 * only rows of FEED128 proven alpha 0, the rest as before
 * (port/render/rd_aura_paste.c; docs/port/RENDER_POST.md, "The aura's
 * paste as its constant part").
 *
 * The record: a target whose rows (every column) are proven to hold alpha
 * 0, made by a staticBlur sprite drawn in a cleared source's colour
 * (rd_const_read.c: AURA_WORK only its clear, into WORK0), carried by the
 * next action's sprite reading it (WORK0 into FEED128), and taken by the
 * next action's paste into SCENE (pasteFeedBackAreaToFB): the SCENE texel
 * rows whose every tap lies in proven rows draw as the alpha alone
 * (rd_fx_plan.c's constant fold, alpha 0, RGB kept: what the paste writes
 * where As is 0), the others as the paste, scissored (the band).
 * ICO_RD_AURA_PASTE=0|1 (tests only, docs/port/TESTING.md; no config key);
 * on on every class and backend where the paste is drawn by the local
 * read. */
#ifndef PORT_RENDER_RD_AURA_PASTE_H
#define PORT_RENDER_RD_AURA_PASTE_H

#include <stdbool.h>
#include <stdint.h>

#include "rd_internal.h"

struct Replay;
struct RdFxDraw;

/* the rows a record holds (FEED128 128, WORK0 128; more: no record) */
#define RD_AP_ROWS 1024

typedef struct RdApRows {
    uint32_t n; /* the target's rows */
    uint64_t w[RD_AP_ROWS / 64];
} RdApRows;

void rd__ap_rows_set(RdApRows *r, int32_t row);
bool rd__ap_rows_has(const RdApRows *r, int32_t row);

/* The GS's V at window Y (12.4) of a sprite with rect and uv (12.4):
 * v0 + trunc((Y - y0) (v1 - v0) / (y1 - y0)), fx_sprite_ps's */
int32_t rd__ap_v(int32_t y, const float rect[4], const float uv[4]);

/* The rows [*lo, *hi] an axis kernel taps at c16 (12.4 texels of the
 * source, before addressing): common.hlsli gs_rs_first / gs_rs_count with
 * step32 (0: the GS's own taps; FXF_ABOVE1X's steps otherwise), bilinear
 * (FXF_LINEAR); a bilinear tap at a fraction of 0 is weighted 0 and left
 * out (gs_bilerp: c + ((d - c) 0 >> 4) = c) */
void rd__ap_taps(int32_t c16, int32_t step32, bool bilinear, int32_t *lo, int32_t *hi);

/* Every addressed row of [lo, hi] proven (fx_sprite.hlsl fx_axis: CLAMP
 * to the TEX0 size lsz, or REPEAT on it, then clamped to the asz rows of
 * the backing texture).  A row REPEAT moves is proven only when every row
 * of the texture is */
bool rd__ap_proven(const RdApRows *p, int32_t lo, int32_t hi, bool clampT, int32_t lsz,
                   int32_t asz);

/* The window Y range (12.4) fx_sprite_ps can compute for texel row t of a
 * target sy texels a GS pixel at origin oy16: gp = floor(((t + 0.5) / sy
 * - 0.5) 16 + 0.5), one step either way for the float's rounding */
void rd__ap_texel_y(uint32_t t, float sy, int32_t oy16, int32_t *ylo, int32_t *yhi);

/* The paste's split: SCENE's th texel rows at sy and origin oy16 over a
 * source proven rows p (its TEX0 rows lsz, its texture's asz, clampT, its
 * scale ssy), sprite rect and uv, kernel step32 (y) and bilinear: the
 * longest run [*c0, *c1) of rows whose taps are all proven (rows outside
 * the rect's coverage count as proven: neither form draws them); false
 * when none */
typedef struct RdApPaste {
    uint32_t th;
    float sy, ssy;
    int32_t oy16;
    float rect[4], uv[4];
    int32_t step32;
    bool bilinear, clampT;
    int32_t lsz, asz;
    const RdApRows *proven;
} RdApPaste;

bool rd__ap_paste_rows(const RdApPaste *in, uint32_t *c0, uint32_t *c1);

/* why the paste was drawn whole (RdPerfRecord.auraPasteWhy) */
enum {
    RD_AP_WHY_NONE,
    RD_AP_WHY_OFF,   /* ICO_RD_AURA_PASTE=0 */
    RD_AP_WHY_CHAIN, /* no record of alpha 0 rows in its source (the aura drew, an action between) */
    RD_AP_WHY_FORM,  /* the paste's form (its alpha at a 0 texel, the blend, a test, FBA, PABE) */
    RD_AP_WHY_SCISSOR, /* its scissor not the whole target */
    RD_AP_WHY_LOCAL,   /* not drawn by the local read here */
    RD_AP_WHY_ROWS,    /* no row of SCENE reads proven rows only */
    RD_AP_WHY_COUNT
};

const char *rd__aura_paste_why_text(int why);
bool rd__aura_paste_on(void);
void rd__aura_paste_force(int on); /* tests: -1 the environment's */

/* rd_fx_plan.c rd__fx_plan_draw, every staticBlur sprite (r's state, x as
 * doBlurSpriteDraw has it): the record started, carried or taken.
 * rd__aura_paste_due: x is the paste a split was planned for (*c0, *c1 the
 * SCENE rows drawn as the alpha alone).  rd__aura_paste_kept: the split
 * not drawn after all (why); rd__aura_paste_split: drawn */
void rd__aura_paste_observe(struct Replay *r, const struct RdFxDraw *x);
bool rd__aura_paste_due(const struct RdFxDraw *x, uint32_t *c0, uint32_t *c1);
void rd__aura_paste_kept(int why);
void rd__aura_paste_split(uint32_t c0, uint32_t c1, uint32_t th);

#endif
