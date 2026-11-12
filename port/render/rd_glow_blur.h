/* rd_glow_blur.h: the glow's blur chain in two passes (port/render/rd_glow_blur.c;
 * docs/port/RENDER_POST.md, "The glow's blur in two passes"; DIVERGENCES.md
 * VD-33).
 *
 * staticBlur.c makeFullScreenFlareAfter blurs WORK0 with ten pairs blur(10)
 * .. blur(19): pass 1 WORK0 -> WORK1 with the sprite's rect shrunk by n + 6
 * sixteenths, pass 2 WORK1 -> WORK0 with its UVs shrunk back, each a GS
 * bilinear resample (4-bit fractions, every step truncated), pass 2's
 * colour (139, 136, 132) under HIGHLIGHT a gain of 1.086, 1.0625 and 1.031
 * a pair.  Twenty dependent passes on 424 x 128 buffers on the PowerVR
 * phone (the blur fold draws nine pairs as one pass each: eleven).
 *
 * The chain is linear but for its roundings, separable (every sprite's U
 * depends on X alone, V on Y alone) and position-dependent (the shrinks put
 * a different sub-texel phase on every column and row: no blur at the 4:3
 * part's left edge and top, a spread of 2.2 texels at its widest).  So its
 * net operator is, per axis, one kernel per position: the composition of
 * the passes' two-tap filters, at most 14 texels wide (a weight above
 * 1e-3), shifted by up to 1.5 texels.  rd__glow_blur_axis computes it on
 * the CPU from the recorded commands (the same integer U, the same clamps
 * and coverage, the glow's edge strip on a wide frame), as RD_GLOW_BLUR_TAPS
 * hardware-bilinear taps a position (adjacent texels merged), and a model
 * of the roundings' loss (each pass loses RD_GLOW_BLUR_LOSS_LERP levels on
 * an axis where its fraction is not 0, a colour multiply below or above
 * 128 RD_GLOW_BLUR_LOSS_MUL more, carried through the later passes' filters
 * and gains: the chain is 20 to 33 levels darker than its float form in a
 * lit texel).  Two passes of fx_glowblur_ps then draw it: across (WORK0 ->
 * WORK1, the taps of each column) and down (WORK1 -> WORK0, the taps of
 * each row, the gains, the loss subtracted).  The eye blur and the alpha
 * sprite after it are drawn as recorded.  Plain float filtering with a
 * handful of taps: no new kind of GPU work.
 *
 * Fitted on the two lamps dumps here (build-host evidence glowblur/): WORK3
 * at most 14 and 18 levels from the chain's, a mean of 0.09 and 0.35 levels,
 * 53 and 231 texels more than 8 apart; synthetic lamps at the phone's 424 x
 * 128 at most 20.
 *
 * Used where ICO_RD_LAMPS names the word glowblur (the tests) or the build's
 * RD_GLOW_BLUR_DEFAULT is 1, on every class and backend with the shader,
 * only in a frame whose glow light plan is LAMPS or WHOLE (rd_glow_light.c;
 * a WHOLE plan drawn whole because a later command reads WORK1, WORK2,
 * WORK3 or SCENE's alpha, or for an unknown form, is refused), above 1x
 * (Original 1x keeps the game's chain), and where the chain has the
 * recognised form (rd_replay_tiler.c rd__replay_blur_chain: the fold's
 * conditions on every pass, pairs of WORK0 <-> WORK1, the eye blur next,
 * pass 2 covering WORK0, pass 1's colour 128 and no constant added).
 * Anything else: today's chain. */
#ifndef PORT_RENDER_RD_GLOW_BLUR_H
#define PORT_RENDER_RD_GLOW_BLUR_H

#include <stdbool.h>
#include <stdint.h>

#include "rd_internal.h"

struct Replay;

#ifdef __cplusplus
extern "C" {
#endif

/* 1: the two passes wherever they are eligible and ICO_RD_LAMPS names no
 * words of its own (a build variant's choice); 0 (v0.11.5): by the word
 * glowblur alone, until the owner has seen it */
#ifndef RD_GLOW_BLUR_DEFAULT
#define RD_GLOW_BLUR_DEFAULT 0
#endif

/* the taps a position (fx_glowblur.hlsl GB_TAPS: the same number) */
#define RD_GLOW_BLUR_TAPS 7
/* the widest kernel row the CPU composes (texels) */
#define RD_GLOW_BLUR_SUPPORT 40
/* the most passes a chain may have (ten pairs is the game's) */
#define RD_GLOW_BLUR_PASSES_MAX 32
/* the most positions an axis may have (WORK0's width at 60:9 is 1280) */
#define RD_GLOW_BLUR_MAXN 2048
/* the loss model (levels; fitted on the dumps: see the header) */
#define RD_GLOW_BLUR_LOSS_LERP 0.40f
#define RD_GLOW_BLUR_LOSS_MUL 0.50f
/* a row's weights outside the taps' window, at most (else refused) */
#define RD_GLOW_BLUR_LOST_MAX 0.01f
/* the weight a row may put on WORK1's bytes from before the chain */
#define RD_GLOW_BLUR_OLD_MAX 0.001f
/* the table's rows: per axis RD_GLOW_BLUR_TAPS rows of taps, then two of
 * the loss (its whole levels, then its 1/256ths) */
#define RD_GLOW_BLUR_AXIS_ROWS (RD_GLOW_BLUR_TAPS + 2)
#define RD_GLOW_BLUR_TABLE_ROWS (2 * RD_GLOW_BLUR_AXIS_ROWS)

/* one pass of the chain as the replay will draw it (rd_replay_tiler.c
 * blurFoldPass's fields): 12.4 rect and UVs, colour, the TEX0 size (the
 * clamp, lsz) and the source texture's texels (asz), the target's origin
 * (12.4), its covered texels (inclusive), the first WORK1 column holding
 * a pass 1's bytes (0 with the glow's edge strip, else c0[0]), the fx flags
 * (TFX, TCC) and TEXFMT */
typedef struct RdBlurChainPass {
    int32_t rect[4], uv[4];
    uint8_t rgba[4];
    int32_t lsz[2], asz[2];
    int32_t o[2];
    int32_t c0[2], c1[2];
    int32_t first;
    uint8_t toWork1; /* pass 1 (WORK0 -> WORK1); else pass 2 */
    uint32_t flags, texfmt;
} RdBlurChainPass;

typedef struct RdBlurChain {
    uint32_t n; /* passes (two a pair) */
    RdBlurChainPass p[RD_GLOW_BLUR_PASSES_MAX];
    uint32_t w0, h0, w1, h1; /* WORK0's and WORK1's texels */
    uint32_t work0, work1;   /* their target ids */
    uint32_t last;           /* the list loop's index of the last pass 2 */
    uint32_t kind;           /* RD_POST_FLARE or RD_POST_BLOOM */
} RdBlurChain;

/* why a chain is drawn as recorded (the 10 s line, --stats, the log once) */
enum {
    RD_GLOW_BLUR_WHY_NONE,
    RD_GLOW_BLUR_WHY_PLAN,   /* the glow light plan is not LAMPS or WHOLE */
    RD_GLOW_BLUR_WHY_READER, /* WHOLE: a later command reads WORK1, WORK2, WORK3 or SCENE's alpha */
    RD_GLOW_BLUR_WHY_ONE,    /* Original 1x (SCENE at scale 1) */
    RD_GLOW_BLUR_WHY_FORM,   /* a pass not of the fold's form, or no eye blur after the pairs */
    RD_GLOW_BLUR_WHY_COLOUR, /* a constant added, pass 1's colour not 128, no TCC, not RGBA32 */
    RD_GLOW_BLUR_WHY_COVER,  /* a pass 2 leaves texels of WORK0, or WORK1 of another width */
    RD_GLOW_BLUR_WHY_WIDE,   /* a kernel row wider than the taps hold, or an offset too far */
    RD_GLOW_BLUR_WHY_OLD,    /* a row weighs WORK1's bytes from before the chain */
    RD_GLOW_BLUR_WHY_PIPE,   /* no pipeline or no module */
    RD_GLOW_BLUR_WHY_MEMORY, /* the table texture, the ring or the CPU arrays */
    RD_GLOW_BLUR_WHY_WINDOW, /* a pass window (the loss bisect counts the passes as recorded) */
    RD_GLOW_BLUR_WHY_COUNT
};

const char *rd__glow_blur_why_text(int why);

/* One axis of the chain's net operator (0 across, 1 down), pure: for each
 * of WORK0's positions along it (n: w0 or h0) RD_GLOW_BLUR_TAPS taps, off
 * (texels from the position's own centre, where the hardware's bilinear
 * samples) and w (weights summing to 1), and loss[4 * i + c] the loss
 * model's levels of channel c (RGBA) on this axis; gain[4] the chain's
 * colour gains (the product of its passes'), lc[4] the loss common to every
 * texel (axis 0 only; 0 for axis 1); *lost the most weight a row left
 * outside its taps.  off and w hold n * RD_GLOW_BLUR_TAPS floats, loss 4 n.
 * RD_GLOW_BLUR_WHY_NONE or why not. */
int rd__glow_blur_axis(const RdBlurChain *ch, int axis, float *off, float *w, float *loss,
                       float gain[4], float lc[4], float *lost);

/* the table's texels (RGBA8, tw x RD_GLOW_BLUR_TABLE_ROWS, row-major, tw >=
 * max(n0, n1)) from both axes' taps and losses: per axis a, rows a *
 * RD_GLOW_BLUR_AXIS_ROWS + k (k < RD_GLOW_BLUR_TAPS) the tap k of each
 * position as (weight * 65535 high byte, low byte, (off + 64) * 256 high
 * byte, low byte), then the loss's whole levels (RGBA) and its 1/256ths;
 * false where an offset is outside -64 .. 64 */
bool rd__glow_blur_encode(uint8_t *texels, uint32_t tw, const uint32_t n[2], float *const off[2],
                          float *const w[2], float *const loss[2]);

/* the replay (rd_blur_border.c blurPostSprite, before the blur fold): at
 * the chain's first pass 1, its pairs drawn as two passes; true: drawn,
 * the list loop skips to the last pass 2 with its state applied */
bool rd__glow_blur_chain(struct Replay *r, const RdFrame *f, const RdCmd *c);

/* rd_replay_tiler.c: the chain read from the list loop's commands at c (a
 * pass 1 under r's state), WORK0 <-> WORK1 pairs to the eye blur, each pass
 * as the blur fold takes it (blurFoldPass, blurFoldPair, blurFoldFirst);
 * RD_GLOW_BLUR_WHY_NONE with *ch filled, else why.  _take: r's state as the
 * loop leaves it after the last pass 2, the loop told to skip to it */
int rd__replay_blur_chain(struct Replay *r, const RdFrame *f, const RdCmd *c, RdBlurChain *ch);
void rd__replay_blur_chain_take(struct Replay *r, const RdBlurChain *ch);

/* in force: asked for (rd_lamps.h RD_LAMPS_GLOWBLUR), a device, the module */
bool rd__glow_blur_on(void);
/* the start-up set's key (rd_pipeline.c), from the blur fold's: fx_glowblur_ps */
RdPipeKeyInt rd__glow_blur_key(const RdStateBlock *s);
/* rd_lamps.c rd__lamps_init / rd__lamps_shutdown: the run's state, the
 * table texture freed */
void rd__glow_blur_init(void);
void rd__glow_blur_shutdown(void);
/* the 10 s line (rd_lamps.c rd_lamps_line) */
void rd__glow_blur_line(void);
/* the counts since rd_init (rd_replay_tool --stats, the tests): chains
 * drawn in two passes, their pairs, chains refused and the last reason */
void rd__glow_blur_counts(uint64_t *chains, uint64_t *pairs, uint64_t *refused, int *lastWhy);
/* the last replay's (the tool's --stats, the A/B): its chain drawn in two
 * passes (1) or not (0), its pairs, and why not (NONE: drawn, or no chain
 * looked at) */
void rd__glow_blur_replay(uint32_t *chains, uint32_t *pairs, int *why);

#ifdef __cplusplus
}
#endif

#endif
