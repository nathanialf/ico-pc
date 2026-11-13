/* rd_turn_fold.h: the present's turn drawn by its own box blit (rd_turn_fold.c;
 * docs/port/RENDER_POST.md "The turn in the present's blit (off by default
 * until a phone rc)").
 *
 * On a pre-rotated swapchain (rd_prerotate.c) the present draws its picture
 * upright into a texture of the window's size and one more full-size pass
 * turns it into the swapchain image.  When the present is the box blit
 * alone (its CLEAR, the border mask's scissor, the colour adjustment, the
 * mirror), that blit can draw into the turned image itself: blit_vs already
 * turns its source position by DrawCB aux1.w (the turn pass's own
 * mechanism), the viewport and the scissor are the box's turned by whole
 * pixels (rd__turn_rect), and the bars are the same cleared black.  The
 * pass that turned the picture is then not drawn.
 *
 * Exact where every output pixel is one source texel (the snapped window,
 * or the supersampling's reduce and FSR 1.0's output drawn one texel a
 * pixel): the turned triangle's interpolated source position differs from
 * the upright one by rounding only, far inside the texel (half a texel from
 * any edge), so nearest takes the same texel and a linear tap at the texel
 * centre gives its value.  Everything else the present can draw reads its
 * own pixel position (the text and overlay shaders' DATE and dither reads,
 * the CRT filter, the effects depth, the box blit from SCENE) or rasterises
 * edges inside the picture (the overlay's and the text's triangles: the
 * fill rule's ties fall on other edges once turned), so a present that
 * draws any of them keeps the turn pass (rd__turn_fold_why).  A start-up
 * self-test (rd__turn_fold_selftest) draws a pattern both ways for each
 * quarter turn, nearest and linear, mirrored and not, with a scissor inset,
 * and compares the turned images byte for byte; a difference refuses the
 * fold for the run.
 *
 * Off by default (RD_TURN_FOLD_DEFAULT 0): an rc tries it on the owner's
 * phone first.  The tests' switch ICO_RD_TURN_FOLD=1 (on, the self-test
 * still decides) or 0; no ini key.  Lines: the self-test's verdict at the
 * start, the first present folded, each refusal reason once; counts in the
 * attempt ledger's row (presents folded, presents that kept the turn pass). */
#ifndef PORT_RENDER_RD_TURN_FOLD_H
#define PORT_RENDER_RD_TURN_FOLD_H

#include <stdbool.h>
#include <stdint.h>

#include "rhi.h"

#ifndef RD_TURN_FOLD_DEFAULT
#define RD_TURN_FOLD_DEFAULT 0
#endif

/* why a present keeps the turn pass (rd__turn_fold_why) */
enum {
    RD_TF_FOLD = 0, /* folded */
    RD_TF_CRT,      /* the CRT filter draws the picture */
    RD_TF_DEPTH,    /* the effects depth's blit */
    RD_TF_SCENE,    /* the box blit from SCENE (blit_scene_ps reads its pixel) */
    RD_TF_SCALED,   /* the box blit not one texel a pixel */
    RD_TF_OVERLAY,  /* the port's overlay or the deferred text drawn */
    RD_TF_CAPTURE,  /* a capture, a shot or the icon hold at this present */
    RD_TF_FORMAT,   /* the image's format not the self-test's */
    RD_TF_COUNT
};

/* one present's facts (rd_present.c rd__present_record) */
typedef struct RdTurnFoldPresent {
    bool crt, depth, scene;
    bool oneToOne;    /* every output pixel one source texel */
    uint32_t batches; /* the overlay's and the deferred text's batches */
    bool capture;     /* a capture or shot armed, the icon hold asked */
    bool formatOk;    /* the image's format is the self-test's */
} RdTurnFoldPresent;

/* the reason (RD_TF_*) a present with these facts keeps the turn pass, or
 * RD_TF_FOLD (pure) */
int rd__turn_fold_why(const RdTurnFoldPresent *p);
/* the reason's words ("the port's overlay or text drawn") */
const char *rd__turn_fold_words(int why);

/* r (in a picture w x h) where the picture turned turns quarter turns
 * clockwise into its image puts it (the image (h x w for odd turns); the
 * table of rd_prerotate.c: 1 (X, Y) = (h - 1 - y, x), 2 (w - 1 - x,
 * h - 1 - y), 3 (y, w - 1 - x)); turns 0: r (pure) */
RhiRect rd__turn_rect(uint32_t turns, uint32_t w, uint32_t h, RhiRect r);

/* ICO_RD_TURN_FOLD: -1 unset, 0, 1 */
int rd__turn_fold_env(void);
/* the fold may run at all this start (the switch or the default, and the
 * self-test passed): the ledger's row set for turned (a pre-rotated
 * swapchain or the tests' twin) */
bool rd__turn_fold_on(bool turned);
/* the self-test's format (rhi_swapchain_format at the start, else
 * RGBA8_UNORM) */
RhiFormat rd__turn_fold_format(void);
/* the start-up self-test (rd_fog_path.c, with the other self-tests): run
 * where the fold is asked for; false when it failed (or could not run) */
bool rd__turn_fold_selftest(void);
/* the present's count: why (RD_TF_*) for a present on a turned image; the
 * first fold and each refusal reason said once */
void rd__turn_fold_note(int why, uint32_t w, uint32_t h, uint32_t turns);
/* every count and the self-test's verdict back to their start (rd_shutdown) */
void rd__turn_fold_reset(void);

/* rd_present.c: the box blit of src (sw x sh) into box of a picture w x h,
 * drawn into dst, the picture's image turned turns quarter turns (dst
 * (h x w for odd turns); turns 0: the upright picture, dst w x h), in a
 * pass of its own with a CLEAR load; sc the scissor inside the box (NULL:
 * the box).  The present's own blit (no tone, not from SCENE): the
 * self-test's two ways */
void rd__present_blit_turned(RhiCommandList cl, RhiTexture src, uint32_t sw, uint32_t sh,
                             RhiTexture dst, RhiFormat fmt, uint32_t w, uint32_t h,
                             const RhiRect *box, const RhiRect *sc, int filter, int mirror,
                             uint32_t turns);
/* rd_prerotate.c: src (w x h) turned into dst in the turn pass, no line
 * (the self-test's reference) */
void rd__prerotate_turn_pass(RhiCommandList cl, RhiTexture src, RhiState *srcState, uint32_t w,
                             uint32_t h, RhiTexture dst, RhiState *dstState, RhiFormat dstFmt,
                             uint32_t turns);
/* rd_prerotate.c: the headless twin (ICO_VK_FAKE_TRANSFORM) as the folded
 * present's target: made at the turned size, marked drawn by this present
 * (rd__prerotate_twin_pending), its line said once */
bool rd__prerotate_twin_target(uint32_t w, uint32_t h, uint32_t turns, RhiTexture *tex,
                               RhiState **state);

#endif
