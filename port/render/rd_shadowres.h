/* rd_shadowres.h: the shadow's levels at the scene's scale above 1x
 * (rd_shadowres.c; docs/port/RENDER_POST.md "The shadow's levels above 1x").
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
/* The release's cap (v0.11.7): the levels at the scene's scale only up to 3
 * texels a GS pixel on each axis (rd_sr_level_scale at most 3: a scene's
 * scale up to 3), the largest a device ran (issue 123's tablet at 3x, the
 * owner's Pixel at 2.8125 x 2.41) and the tests' 3 x 3; the levels' cost
 * grows about as the scale's fourth power above it and nothing measured it
 * on hardware.  Above it the PS2's sizes, as v0.11.6 drew them
 * (RD_SR_WHY_SCALE) */
#define RD_SR_LEVEL_CAP 3u
/* the count's scales cx, cy within the cap (and RD_SR_SCALE_MAX) */
bool rd_sr_within_cap(float cx, float cy);

/* why a shadow above 1x is drawn at the PS2's size (RdPerfRecord.shadowFineWhy,
 * the 10 s clause's and --stats' words, rd__shadow_fine_why_word) */
enum {
    RD_SR_WHY_PRIM = 0, /* not one two-vertex UV sprite in the world's space */
    RD_SR_WHY_FLAGS,    /* the frame camera, the ST shift or PRIM.AA1 */
    RD_SR_WHY_TEXFN,    /* not MODULATE with TCC RGBA */
    RD_SR_WHY_BLEND,    /* a blend, FBA or a colour mask */
    RD_SR_WHY_TEST,     /* an alpha test, DATE or a Z test */
    RD_SR_WHY_FILTER,   /* not bilinear on both filters */
    RD_SR_WHY_COLOUR,   /* two vertex colours */
    RD_SR_WHY_MAP,      /* the UVs not 2 a pixel, or the level not covered */
    RD_SR_WHY_COUNT,    /* level 0's source not the count kept at its full size */
    RD_SR_WHY_PIPES,    /* the shaders or pipelines could not be made */
    RD_SR_WHY_TEXTURES, /* the textures could not be made */
    RD_SR_WHY_SCALE,    /* a scale above RD_SR_LEVEL_CAP (or RD_SR_SCALE_MAX) */
    RD_SR_WHY_N
};

_Static_assert(RD_SR_WHY_N <= RD_SHADOW_FINE_WHYS, "rd.h RD_SHADOW_FINE_WHYS");
/* the reason's short word ("filter", "map", ...) */
const char *rd__shadow_fine_why_word(int why);
/* the reason's words for the log line */
const char *rd_sr_why_text(int why);

/* A level sprite as plain values (the recogniser's input; rd_shadowres.c
 * fills it from the draw, the CPU test and the harness from the dumps'
 * listings) */
typedef struct RdSrSprite {
    uint8_t prim, space, uvFixed, frameCam, stShift, aa1; /* RdCmd b[0], b[1], b[2], b[4], b[5] */
    uint32_t n;                                           /* vertices */
    uint8_t tcc, texFn, abe, fba, colorMask, ate, atst, date, zte, ztst, magF, minF;
    uint8_t rgba[2][4];     /* the two vertices' colours */
    int32_t x16[2], y16[2]; /* their 12.4 positions (after XYOFFSET's 0x8000 base) */
    float s16[2], t16[2];   /* their fixed UVs in 1/16 texels */
    float uOff, vOff;       /* the state's UV offset in texels */
    uint32_t gsW, gsH;      /* the frame the sprite is drawn in (its origin) */
    uint32_t dstW, dstH;    /* the level's GS size */
    uint32_t srcW, srcH;    /* the source's GS size */
} RdSrSprite;

/* -1 when sp is the game's level sprite (*ox, *oy the first tap's whole
 * offset, *fx, *fy the second tap's weight), else the RD_SR_WHY_* reason
 * with the field's values in detail */
int rd_sr_recognise(const RdSrSprite *sp, int *ox, int *oy, float *fx, float *fy, char *detail,
                    size_t dsize);

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
 * at (1 - f) / s, [a + s, a + 2 s) at f / s (shadowres.hlsl sr_box_w; f 0.25
 * for the game's sprites) */
float rd_sr_box_w(float i, float a, float s, float f);
/* index i of an axis of n texels wrapped (repeat) or held to its edge */
int rd_sr_index(int i, int n, int repeat);
/* a sprite's u at the level's pixel 0 as the GS's bilinear tap: the first
 * source texel's whole offset and the second tap's weight (0 and 0.25 for
 * the game's u = 2 x + 0.75); false when u0 is not a number */
bool rd_sr_offset(float u0, int *whole, float *frac);
/* the tent's weight at t = n - m on an axis of scale s (unnormalised) */
float rd_sr_tent_w(int t, float s);
/* the tent's first and last t on an axis of scale s */
void rd_sr_tent_range(float s, int *t0, int *t1);

/* The reference of the two shaders over RGBA8 images (the CPU test's): a
 * level texel (mx, my) of the step from src (sw x sh texels) under p, as
 * shadowres_step_ps's DrawCB carries it; and a texel n of the tent along one
 * axis (down 0 across) of a w x h level of scale s */
typedef struct RdSrStep {
    int first;      /* 1 the box mode (the count), else the point mode */
    float kx, ky;   /* the texels a destination texel moves the taps (g_param.xy) */
    float ax, ay;   /* the first tap's origin in source texels (g_scale.xy) */
    float sx, sy;   /* the second tap's offset, the box's width (g_param.zw) */
    float fx, fy;   /* the second tap's weight (g_uvRect.xy) */
    int repS, repT; /* the sprite's REPEAT (g_mode.y), else CLAMP */
    uint8_t col[4]; /* the sprite's colour */
} RdSrStep;

void rd_sr_step_texel(const uint8_t *src, uint32_t sw, uint32_t sh, const RdSrStep *p, uint32_t mx,
                      uint32_t my, uint8_t out[4]);
/* the step's value before its rounding (the shader's sum * 255: the byte is
 * floor(value + 0.5), then MODULATE) and the MODULATE of a rounded byte t by
 * the sprite colour's col ((t col) >> 7, held to 255): what rd_sr_step_texel
 * is made of (the replay tool's --fine-check judges a GPU byte one apart by
 * how near the value is to a rounding boundary) */
void rd_sr_step_value(const uint8_t *src, uint32_t sw, uint32_t sh, const RdSrStep *p, uint32_t mx,
                      uint32_t my, float val[4]);
uint8_t rd_sr_step_byte(uint32_t t, uint8_t col);
void rd_sr_tent_texel(const uint8_t *src, uint32_t w, uint32_t h, int down, float s, uint32_t x,
                      uint32_t y, uint8_t out[4]);

/* The cuts (rd_shadowres.c "The cuts"; each exact, the CPU test's):
 * the GS pixels lo..hi of a level of dstSize GS pixels that can be nonzero
 * when its source (srcSize GS pixels) is nonzero only in srcLo..srcHi, the
 * sprite's whole offset `whole`, its axis repeating or held; false when none */
bool rd_sr_step_box(int srcLo, int srcHi, int srcSize, int whole, int dstSize, int repeat, int *lo,
                    int *hi);
/* the fine texels the tent of scale L spreads lo..hi to, clipped to size */
void rd_sr_tent_box(int lo, int hi, int L, int size, int *flo, int *fhi);
/* shadowres_tent2_ps's reference: the tent across and down at (x, y) of a
 * w x h RGBA8 level at the odd whole scales lx, ly, in integers */
void rd_sr_tent2_texel(const uint8_t *src, uint32_t w, uint32_t h, int lx, int ly, uint32_t x,
                       uint32_t y, uint8_t out[4]);
/* shadowres_step_ps's empty-area test for a first-level texel whose taps
 * start at (ax, ay) count texels of the sw x sh count, the count at sx x sy
 * texels a GS pixel with ox left-out texels: true (never when a tap would
 * leave the count's texels: it wraps or is held) when every GS pixel of the mask (mw x mh
 * RGBA8, the count box-reduced) under the taps has alpha 0 */
bool rd_sr_mask_empty(const uint8_t *mask, uint32_t mw, uint32_t mh, uint32_t sw, uint32_t sh,
                      float ax, float ay, float sx, float sy, float ox);

/* The render area ("The render area"): a texture is 0 outside *last once
 * *known.  A pass that leaves it 0 outside box (NULL: draws every texel)
 * clears and draws the area rd_sr_area gives (the union of *last and box;
 * the whole texture until *known), then *last = box; false: no pass (the
 * texture is 0 and box holds nothing).  The tracking of the textures
 * rd_shadowres.c's level passes write, the CPU test's */
typedef struct RdSrTrack {
    RhiRect last;
    bool known;
} RdSrTrack;

RhiRect rd_sr_rect_union(RhiRect a, RhiRect b);
bool rd_sr_area(RhiRect *last, bool *known, const RhiRect *box, uint32_t w, uint32_t h,
                RhiRect *area);

/* rd_replay.c: the count of target color box-reduced to its GS size this
 * replay ({0} when it was not made: an unscaled count) */
RhiTexture rd__shadow_red_of(uint32_t color);

/* ------------------------------------------------------------ the replay */
/* rd_no_volume.c rd__no_volume_skip, at the top of every screen command
 * (doScreen): the replay the command is drawn in */
void rd__shadow_fine_replay(struct Replay *r);
/* rd_shadow_box.c rd__shadow_box_reset: a new count (a new chain) */
void rd__shadow_fine_reset(uint32_t color);
/* rd_shadow_one.c: the count target tc (scaled) is to be drawn at its full
 * size too (a scale above 1 on an axis, not refused) */
bool rd__shadow_fine_count_wanted(const struct RdTargetRec *tc);
/* the cuts are in force (ICO_RD_SHADOW_FINE_CUTS is not 0) */
bool rd__shadow_fine_cuts(void);
/* the count of target color holds the full size (the seven draws, MSAA,
 * the one draw's full-size pass) */
void rd__shadow_fine_count_full(uint32_t color);

/* The count from the stencil (rd_shadow_one.c, rd_shadow_count.h
 * RD_SC_STENCIL): the count of target color stands, at the count's texels,
 * in depth's stencil (its view's channel `channel`, RhiLimits
 * .stencilViewChannel) through the table (texel m the count's bytes of a
 * stencil m), 0 outside box (the texels the full-size pass would draw):
 * the first level's step reads it there (shadowres_step_stencil_ps) */
typedef struct RdSrStencil {
    RhiTexture depth, table;
    uint32_t channel;
    RhiRect box;
} RdSrStencil;

void rd__shadow_fine_count_stencil(uint32_t color, const RdSrStencil *src);
/* rd_shadow_one.c, the guard: rd_replay.c rd__transition first, for every
 * texture (the pass drawn before a touch of the depth or the count); the
 * step's take (1 the stencil's, 0 the full-size count drawn after all, -1
 * lost: the chain refused); the pass drawn now (why: RD_SC_WHY_*); the pass
 * drawn after a step from the stencil (the tests' readback); the watch
 * ended (rd_store_plan.c rd__store_plan_end, the walk's end) */
void rd__shadow_count_touch(RhiCommandList cl, uint32_t texId);
int rd__shadow_count_take(uint32_t color);
void rd__shadow_count_flush(uint32_t color, int why);
void rd__shadow_count_materialise(void);
void rd__shadow_count_end(void);
/* the check (rd_shadow_count.h): the check replay's first level step takes
 * the stencil's place, the depth and the count's step (false: not this
 * replay, or the depth was touched); the seven draws at the full size into
 * dst (w x h) against td's stencil (false: no draw made) */
bool rd__shadow_count_check_take(uint32_t color, RdSrStencil *src, struct RdTargetRec **td,
                                 uint32_t *step);
bool rd__shadow_count_seven_into(RhiTexture dst, RhiState *dstState, struct RdTargetRec *td,
                                 uint32_t w, uint32_t h, uint32_t step);
/* rd_shadow_box.c rd__shadow_box_screen, before the draw's pass: a level
 * sprite of the chain is drawn at the scene's scale too (and the tent
 * after the third) */
void rd__shadow_fine_screen(const struct RdStateBlock *st, const struct RdCmd *c,
                            const RdScreenVtx *v, uint32_t n);
/* rd_replay.c resolveTexture: the fine level that stands for level target
 * `target` this replay, read by a draw into drawTarget; {0} none */
RhiTexture rd__shadow_fine(uint32_t target, uint32_t drawTarget);
/* the texture is one of the fine levels (rd_fx_plan.c: no B-spline) */
bool rd__shadow_fine_tex(uint32_t texId);
/* the level target holds a fine twin this replay (rd_shadow_tile.c steps
 * aside) */
bool rd__shadow_fine_level(uint32_t target);

/* The tests' readback (rd_replay_tool --fine-check, tools/rd_replay_fine.c),
 * after a replay that drew the chain with the cuts: level k's step as it
 * was drawn (the reference's inputs: rd_sr_step_value over the source's sw x
 * sh texels, the count target `count` for level 0, the step of the level
 * before for the others), the level's w x h texels and its tent's whole
 * scales; false when this replay drew no chain in that form.  And the
 * level's texels read back: the step's output (final false) or the level
 * after its tent (true), w * h * 4 bytes */
typedef struct RdSrProbe {
    RdSrStep step;
    uint32_t sw, sh, w, h;
    int lx, ly;
    uint32_t count;
} RdSrProbe;

bool rd__shadow_fine_probe(int k, RdSrProbe *out);
bool rd__shadow_fine_read(int k, bool final, void *dst, size_t size);
/* the rule's line at the renderer's start (and the tests' switch) */
void rd__shadow_fine_log(void);
void rd__shadow_fine_shutdown(void);
/* the 10 s clause and the --stats words */
void rd__shadow_fine_pop(const struct RdPerfRecord *rec);
void rd__shadow_fine_text(char *out, size_t size, size_t *at);
void rd__shadow_fine_stats(const struct RdPerfRecord *rec, char *out, size_t size);
/* --stats' second line: the texels the chain's passes drew of their targets' */
void rd__shadow_fine_drawn(const struct RdPerfRecord *rec, char *out, size_t size);

#endif
