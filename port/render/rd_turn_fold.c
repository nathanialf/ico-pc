/* rd_turn_fold.c: the present's turn drawn by its own box blit
 * (rd_turn_fold.h). */
#include "rd_turn_fold.h"

#include <stdlib.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_internal.h"

static const char *const kWords[RD_TF_COUNT] = {
    [RD_TF_FOLD] = "folded",
    [RD_TF_CRT] = "the CRT filter draws the picture",
    [RD_TF_DEPTH] = "the effects depth's blit",
    [RD_TF_SCENE] = "the box blit from SCENE",
    [RD_TF_SCALED] = "the box blit not one texel a pixel",
    [RD_TF_OVERLAY] = "the port's overlay or the deferred text drawn",
    [RD_TF_CAPTURE] = "a capture, a shot or the icon hold",
    [RD_TF_FORMAT] = "the image's format not the self-test's",
};

int rd__turn_fold_why(const RdTurnFoldPresent *p)
{
    return p->crt         ? RD_TF_CRT
           : p->depth     ? RD_TF_DEPTH
           : p->scene     ? RD_TF_SCENE
           : !p->oneToOne ? RD_TF_SCALED
           : p->batches   ? RD_TF_OVERLAY
           : p->capture   ? RD_TF_CAPTURE
           : !p->formatOk ? RD_TF_FORMAT
                          : RD_TF_FOLD;
}

const char *rd__turn_fold_words(int why)
{
    return why >= 0 && why < RD_TF_COUNT ? kWords[why] : "?";
}

RhiRect rd__turn_rect(uint32_t turns, uint32_t w, uint32_t h, RhiRect r)
{
    /* the picture's pixel (x, y) at the image's (X, Y): 1 (h - 1 - y, x),
       2 (w - 1 - x, h - 1 - y), 3 (y, w - 1 - x); a rect's corners in whole
       pixels, so every value exact */
    const int32_t W = (int32_t)w, H = (int32_t)h;
    switch (turns & 3u) {
    case 1u:
        return (RhiRect){H - (r.y + (int32_t)r.h), r.x, r.h, r.w};
    case 2u:
        return (RhiRect){W - (r.x + (int32_t)r.w), H - (r.y + (int32_t)r.h), r.w, r.h};
    case 3u:
        return (RhiRect){r.y, W - (r.x + (int32_t)r.w), r.h, r.w};
    default:
        return r;
    }
}

/* the run's state: the switch read once, the self-test's verdict (-1 not
   run, 0 failed, 1 passed) and its format, the reasons said once */
static struct {
    int env; /* -2 not read */
    int selftest;
    RhiFormat fmt;
    bool said[RD_TF_COUNT];
} s_tf = {-2, -1, RHI_FMT_UNKNOWN, {false}};

int rd__turn_fold_env(void)
{
    if (s_tf.env == -2) {
        const char *e = getenv("ICO_RD_TURN_FOLD");
        s_tf.env = e == NULL || e[0] == '\0'                      ? -1
                   : strcmp(e, "1") == 0 || strcmp(e, "on") == 0  ? 1
                   : strcmp(e, "0") == 0 || strcmp(e, "off") == 0 ? 0
                                                                  : -1;
        if (e != NULL && e[0] != '\0' && s_tf.env < 0) {
            rd__log("present: ICO_RD_TURN_FOLD=%s is none of 0, 1: unset", e);
        }
    }
    return s_tf.env;
}

static bool asked(void)
{
    const int e = rd__turn_fold_env();
    return e == 1 || (e < 0 && RD_TURN_FOLD_DEFAULT);
}

bool rd__turn_fold_on(bool turned)
{
    return ico_attempt_decide_turn_fold(turned, rd__turn_fold_env(), RD_TURN_FOLD_DEFAULT,
                                        s_tf.selftest) != 0;
}

RhiFormat rd__turn_fold_format(void)
{
    return s_tf.fmt;
}

/* the self-test's sizes: the picture, the pattern (the box, one texel a
   pixel) and two boxes at odd places, one with a scissor inset */
enum { TF_W = 48, TF_H = 30, TF_PW = 11, TF_PH = 7 };

/* the pattern's texel (x, y): every channel a different function of both
   coordinates, so a turn the wrong way, a texel off or a mirror lost moves
   some byte */
static void patColour(uint32_t x, uint32_t y, float c[4])
{
    c[0] = (float)((x * 37u + y * 101u + 3u) & 255u) / 255.0f;
    c[1] = (float)((x * 13u + y * 59u + 50u) & 255u) / 255.0f;
    c[2] = (float)((x * 89u + y * 29u + 100u) & 255u) / 255.0f;
    c[3] = (float)((x * 7u + y * 17u + 200u) & 255u) / 255.0f;
}

/* one case (turns, filter, mirror, box, scissor): the upright blit and the
   turn pass against the blit drawn turned; NULL the same bytes */
static const char *oneCase(RhiTexture pat, RhiTexture pic, RhiTexture a, RhiTexture b,
                           RhiFormat fmt, uint32_t turns, int filter, int mirror,
                           const RhiRect *box, const RhiRect *sc, uint8_t *bufA, uint8_t *bufB,
                           size_t bufSize)
{
    const uint32_t tw = (turns & 1u) ? TF_H : TF_W, th = (turns & 1u) ? TF_W : TF_H;
    RhiCommandList cl =
        rd__begin_own_frame(64u * 1024u) ? rhi_begin_commands() : (RhiCommandList){0};
    if (!cl.id) {
        return "no command list";
    }
    RhiState picS = RHI_STATE_UNDEFINED, as = RHI_STATE_UNDEFINED, bs = RHI_STATE_UNDEFINED;
    rd__transition(cl, pic, &picS, RHI_STATE_RENDER_TARGET);
    rd__present_blit_turned(cl, pat, TF_PW, TF_PH, pic, fmt, TF_W, TF_H, box, sc, filter, mirror,
                            0u);
    rd__prerotate_turn_pass(cl, pic, &picS, TF_W, TF_H, a, &as, fmt, turns);
    rd__transition(cl, b, &bs, RHI_STATE_RENDER_TARGET);
    rd__present_blit_turned(cl, pat, TF_PW, TF_PH, b, fmt, TF_W, TF_H, box, sc, filter, mirror,
                            turns);
    rd__transition(cl, a, &as, RHI_STATE_COPY_SRC);
    rd__transition(cl, b, &bs, RHI_STATE_COPY_SRC);
    rhi_end_commands(cl);
    rhi_submit(cl);
    rhi_wait_idle();
    uint32_t pa = 0, pb = 0;
    if (!rhi_readback_texture(a, RHI_ASPECT_COLOR, bufA, bufSize, &pa) ||
        !rhi_readback_texture(b, RHI_ASPECT_COLOR, bufB, bufSize, &pb) || pa != pb ||
        pa < tw * 4u || (size_t)pa * th > bufSize) {
        return "its readback failed";
    }
    for (uint32_t y = 0; y < th; y++) {
        const uint8_t *ra = bufA + (size_t)y * pa, *rb = bufB + (size_t)y * pb;
        if (memcmp(ra, rb, (size_t)tw * 4u) != 0) {
            for (uint32_t x = 0; x < tw; x++) {
                if (memcmp(ra + x * 4u, rb + x * 4u, 4u) != 0) {
                    rd__log("present: turn in the blit: self-test: %u degrees, %s%s: pixel %u,%u "
                            "of the turned %ux%u reads %u,%u,%u,%u for the turn pass's "
                            "%u,%u,%u,%u",
                            turns * 90u, filter ? "linear" : "nearest", mirror ? ", mirrored" : "",
                            x, y, tw, th, rb[x * 4u], rb[x * 4u + 1u], rb[x * 4u + 2u],
                            rb[x * 4u + 3u], ra[x * 4u], ra[x * 4u + 1u], ra[x * 4u + 2u],
                            ra[x * 4u + 3u]);
                    break;
                }
            }
            return "the turned blit's bytes differ from the turn pass's";
        }
    }
    return NULL;
}

bool rd__turn_fold_selftest(void)
{
    if (!asked() || !g_rd.hasDevice) {
        return false; /* not asked: the row says off (rd__turn_fold_on) */
    }
    const RhiFormat sw = rhi_swapchain_format();
    const RhiFormat fmt = sw != RHI_FMT_UNKNOWN ? sw : RHI_FMT_RGBA8_UNORM;
    const char *why = NULL;
    const uint32_t use = RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC;
    RhiTexture pat = rhi_create_texture(
        &(RhiTextureDesc){TF_PW, TF_PH, 1, RHI_FMT_RGBA8_UNORM, use, "rd turn fold pattern", 0});
    RhiTexture pic =
        rhi_create_texture(&(RhiTextureDesc){TF_W, TF_H, 1, fmt, use, "rd turn fold picture", 0});
    RhiTexture ta = rhi_create_texture(
        &(RhiTextureDesc){TF_H, TF_W, 1, fmt, use, "rd turn fold turn pass (odd)", 0});
    RhiTexture tb = rhi_create_texture(
        &(RhiTextureDesc){TF_H, TF_W, 1, fmt, use, "rd turn fold blit (odd)", 0});
    RhiTexture ua = rhi_create_texture(
        &(RhiTextureDesc){TF_W, TF_H, 1, fmt, use, "rd turn fold turn pass (half)", 0});
    RhiTexture ub = rhi_create_texture(
        &(RhiTextureDesc){TF_W, TF_H, 1, fmt, use, "rd turn fold blit (half)", 0});
    const size_t bufSize = (size_t)TF_W * TF_H * 4u + 4096u * TF_W;
    uint8_t *bufA = malloc(bufSize), *bufB = malloc(bufSize);
    uint32_t cases = 0;
    if (!pat.id || !pic.id || !ta.id || !tb.id || !ua.id || !ub.id || !bufA || !bufB) {
        why = "its textures not made";
    } else {
        /* the pattern: one cleared area a texel */
        RhiCommandList cl =
            rd__begin_own_frame(64u * 1024u) ? rhi_begin_commands() : (RhiCommandList){0};
        if (!cl.id) {
            why = "no command list";
        } else {
            RhiState s = RHI_STATE_UNDEFINED;
            rd__transition(cl, pat, &s, RHI_STATE_RENDER_TARGET);
            for (uint32_t y = 0; y < TF_PH; y++) {
                for (uint32_t x = 0; x < TF_PW; x++) {
                    RhiRenderPassDesc p;
                    memset(&p, 0, sizeof(p));
                    p.color[0].texture = pat;
                    p.color[0].load = RHI_LOAD_CLEAR;
                    patColour(x, y, p.color[0].clear);
                    p.colorCount = 1;
                    p.width = TF_PW;
                    p.height = TF_PH;
                    p.area = (RhiRect){(int32_t)x, (int32_t)y, 1u, 1u};
                    rhi_cmd_begin_render_pass(cl, &p);
                    rhi_cmd_end_render_pass(cl);
                }
            }
            rd__transition(cl, pat, &s, RHI_STATE_SHADER_READ);
            rhi_end_commands(cl);
            rhi_submit(cl);
            rhi_wait_idle();
        }
        /* nearest with a scissor inset, then linear mirrored: the box at odd
           places (the bars cleared around it) */
        const RhiRect boxA = {7, 5, TF_PW, TF_PH}, scA = {8, 6, TF_PW - 3u, TF_PH - 2u};
        const RhiRect boxB = {29, 17, TF_PW, TF_PH};
        for (uint32_t turns = 1u; turns <= 3u && !why; turns++) {
            const RhiTexture a = (turns & 1u) ? ta : ua, b = (turns & 1u) ? tb : ub;
            for (int k = 0; k < 2 && !why; k++) {
                why = oneCase(pat, pic, a, b, fmt, turns, k ? RD_FILTER_LINEAR : RD_FILTER_NEAREST,
                              k, k ? &boxB : &boxA, k ? NULL : &scA, bufA, bufB, bufSize);
                cases += why ? 0u : 1u;
            }
        }
    }
    rhi_wait_idle();
    const RhiTexture made[6] = {pat, pic, ta, tb, ua, ub};
    for (int i = 0; i < 6; i++) {
        if (made[i].id) {
            rhi_destroy_texture(made[i]);
        }
    }
    free(bufA);
    free(bufB);
    s_tf.selftest = why ? 0 : 1;
    s_tf.fmt = fmt;
    if (why) {
        rd__log("present: turn in the blit: refused by its self-test (%s): every present on a "
                "turned swapchain keeps the turn pass",
                why);
    } else {
        rd__log("present: turn in the blit: self-test passed (%u cases: 90, 180 and 270 degrees, "
                "nearest with a scissor inset and linear mirrored, one texel a pixel, the turned "
                "blit byte for byte the turn pass's; format %d)",
                cases, (int)fmt);
    }
    return why == NULL;
}

void rd__turn_fold_note(int why, uint32_t w, uint32_t h, uint32_t turns)
{
    if (why == RD_TF_FOLD) {
        ico_attempt_fire(ICO_ATT_TURN_FOLD, 1);
    } else {
        ico_attempt_fire2(ICO_ATT_TURN_FOLD, 1);
    }
    if (why < 0 || why >= RD_TF_COUNT || s_tf.said[why]) {
        return;
    }
    s_tf.said[why] = true;
    if (why == RD_TF_FOLD) {
        rd__log("present: turn in the blit: the %ux%u picture's box blit drawn turned %u degrees "
                "into the %ux%u image, no turn pass (reported once)",
                w, h, turns * 90u, (turns & 1u) ? h : w, (turns & 1u) ? w : h);
    } else {
        rd__log("present: turn in the blit: a present keeps the turn pass: %s (reported once for "
                "each reason)",
                kWords[why]);
    }
}

void rd__turn_fold_reset(void)
{
    s_tf.env = -2;
    s_tf.selftest = -1;
    s_tf.fmt = RHI_FMT_UNKNOWN;
    memset(s_tf.said, 0, sizeof s_tf.said);
}
