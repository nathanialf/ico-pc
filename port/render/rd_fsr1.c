/* rd_fsr1.c: AMD FidelityFX Super Resolution 1.0 at the present (issue 99;
 * the shaders are port/shaders/fsr1.hlsl over AMD's own ffx_a.h and
 * ffx_fsr1.h, vendored unmodified in port/third_party/fsr1, MIT,
 * Copyright (c) 2021 Advanced Micro Devices, Inc.).
 *
 * Options > Picture > Upscaling: FSR 1.0 (RdSettings.upscale
 * RD_UPSCALE_FSR1) and Sharpness (upscaleSharpness, 0..100).  A step of
 * rd_present.c's rd__present_record after the window's reduce and before
 * the box blit: where the picture (DISPLAY, line-doubled or full height,
 * after Downsampling when that ran) has fewer texels than the presentation
 * box on both axes, two passes of the box's size replace the box blit's one
 * bilinear stretch:
 *   1. fsr1_easu_ps  the source into A (RGBA8, box->w x box->h): EASU,
 *                    AMD's edge-adaptive Lanczos-like upscale; the columns
 *                    past the exact width (sourceCrop) left out through the
 *                    viewport and offset of FsrEasuConOffset;
 *   2. fsr1_rcas_ps  A into B (the same size): RCAS, AMD's
 *                    contrast-adaptive sharpening, the Sharpness row's stops.
 * The box blit then draws B into the box one texel a pixel (nearest), with
 * the border mask, the mirror (a nearest flip of B), the colour adjustment
 * and the effects depth as without it.  Never under the CRT filter (it
 * draws its own box from the PS2's pixels), never at the "window"
 * resolution (one texel a pixel already), and not where the reduce ran
 * (more texels than pixels) or the source is the box's size.  Off, nothing
 * here runs and the present is the one without the row byte for byte.
 *
 * The CPU half of AMD's header (the constant set-up, FsrEasuConOffset and
 * FsrRcasCon) is written again here in C, the same float operations in the
 * same order: the header is HLSL/GLSL-and-C through macros, and only the
 * shaders include it. */
#include "rd_fsr1.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rd_internal.h"
#include "shader_consts.h"

_Static_assert(sizeof(IcoFsrCB) == sizeof(IcoDrawCB), "rd__draw_group uploads IcoFsrCB whole");

static uint32_t fbits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

void rd__fsr1_easu_con(uint32_t con[4][4], float vpW, float vpH, float inW, float inH, float outW,
                       float outH, float offX, float offY)
{
    /* ffx_fsr1.h FsrEasuCon, then FsrEasuConOffset's con0[2], con0[3]
     * (ARcpF1 on the CPU is 1.0f / a) */
    const float rOutW = 1.0f / outW, rOutH = 1.0f / outH;
    const float rInW = 1.0f / inW, rInH = 1.0f / inH;
    /* the output's integer pixel to the input's position */
    con[0][0] = fbits(vpW * rOutW);
    con[0][1] = fbits(vpH * rOutH);
    con[0][2] = fbits(0.5f * vpW * rOutW - 0.5f + offX);
    con[0][3] = fbits(0.5f * vpH * rOutH - 0.5f + offY);
    /* the input's texel size, and the four gathers' centres (the first
     * from the upper left of 'F', the others from the first) */
    con[1][0] = fbits(rInW);
    con[1][1] = fbits(rInH);
    con[1][2] = fbits(1.0f * rInW);
    con[1][3] = fbits(-1.0f * rInH);
    con[2][0] = fbits(-1.0f * rInW);
    con[2][1] = fbits(2.0f * rInH);
    con[2][2] = fbits(1.0f * rInW);
    con[2][3] = fbits(2.0f * rInH);
    con[3][0] = fbits(0.0f * rInW);
    con[3][1] = fbits(4.0f * rInH);
    con[3][2] = con[3][3] = 0;
}

/* ffx_a.h AU1_AH1_AF1 on the CPU: a float to a half by its 512-entry base
 * and shift tables (truncating, the denormals kept, infinity and NaN to
 * +-65504), written as the rule the tables hold */
static uint32_t amdHalf(float f)
{
    const uint32_t u = fbits(f);
    const uint32_t sign = (u >> 31) ? 0x8000u : 0u;
    const int e = (int)((u >> 23) & 0xFFu) - 127;
    const uint32_t m = u & 0x7FFFFFu;
    if (e < -24) {
        return sign; /* below the smallest denormal: 0 */
    }
    if (e < -14) {
        return sign + (1u << (e + 24)) + (m >> (-e - 1)); /* a denormal */
    }
    if (e <= 15) {
        return sign + ((uint32_t)(e + 15) << 10) + (m >> 13);
    }
    return sign + 0x7BFFu; /* too large, infinity, NaN */
}

void rd__fsr1_rcas_con(uint32_t con[4], float stops)
{
    const float sharpness = exp2f(-stops);
    con[0] = fbits(sharpness);
    con[1] = amdHalf(sharpness) + (amdHalf(sharpness) << 16);
    con[2] = 0;
    con[3] = 0;
}

float rd__fsr1_stops(unsigned sharpness, float scale)
{
    const unsigned s = sharpness > 100u ? 100u : sharpness;
    float stops = 2.0f * (1.0f - (float)s / 100.0f);
    if (scale > 2.0f) {
        stops += 0.5f;
    }
    return stops;
}

/* ------------------------------------------------------------- the state */

/* rd__fsr1_apply's last: the value asked for, in force, the sharpness;
 * inited: a first apply since the start (or the shutdown) */
static uint8_t s_asked;
static bool s_on, s_inited;
static unsigned s_sharp;

/* the last present's pass (rd__last_present_fsr1): src w, h, dst w, h */
static uint32_t s_note[4];
static bool s_noted;

/* the two passes' targets */
typedef struct FsrTex {
    RhiTexture t;
    RhiState state;
    uint32_t w, h;
} FsrTex;

static FsrTex s_a, s_b;

/* the event lines: a change of sizes or stops (first 8, then every 100th),
 * a missing pipeline once */
static uint32_t s_lines;
static uint32_t s_lastLog[5];
static bool s_toldMissing;

bool rd__fsr1_apply(const RdSettings *st, bool window, char *note, size_t noteSize)
{
    const bool crt = rd__crt_on();
    const bool on = st->upscale == RD_UPSCALE_FSR1 && !crt && !window;
    const unsigned sharp = st->upscaleSharpness > 100u ? 100u : st->upscaleSharpness;
    if (note && noteSize) {
        if (on) {
            snprintf(note, noteSize, ", upscaling fsr1 (sharpness %u)", sharp);
        } else if (st->upscale == RD_UPSCALE_FSR1) {
            snprintf(note, noteSize, ", upscaling fsr1 off (%s)",
                     crt ? "the CRT filter draws its own box" : "the window resolution");
        } else if (st->upscale != RD_UPSCALE_OFF) {
            /* RD_UPSCALE_FSR3, the temporal upscaler, is not offered yet */
            snprintf(note, noteSize, ", upscaling %s not available (off)",
                     st->upscale == RD_UPSCALE_FSR3 ? "fsr3" : "of an unknown kind");
        } else {
            note[0] = '\0';
        }
    }
    const bool changed =
        !s_inited || st->upscale != s_asked || on != s_on || (on && sharp != s_sharp);
    s_inited = true;
    s_asked = st->upscale;
    s_on = on;
    s_sharp = sharp;
    return changed;
}

void rd__fsr1_present_begin(void)
{
    memset(s_note, 0, sizeof(s_note));
    s_noted = true;
}

bool rd__last_present_fsr1(uint32_t out[4])
{
    if (!s_noted || !s_note[2]) {
        return false;
    }
    memcpy(out, s_note, sizeof(s_note));
    return true;
}

static bool ensure(FsrTex *x, uint32_t w, uint32_t h, const char *name)
{
    if (x->t.id && x->w == w && x->h == h) {
        return true;
    }
    if (x->t.id) {
        rhi_destroy_texture(x->t);
    }
    x->t = rhi_create_texture(&(RhiTextureDesc){w, h, 1, RHI_FMT_RGBA8_UNORM,
                                                RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED, name, 0});
    x->state = RHI_STATE_UNDEFINED;
    x->w = w;
    x->h = h;
    return x->t.id != 0;
}

/* one pass of fs over all of dst, input in */
static void pass(RhiCommandList cl, RhiPipeline pipe, const RdPipeKeyInt *k, FsrTex *dst,
                 RhiTexture in, const IcoFsrCB *cb)
{
    rd__transition(cl, dst->t, &dst->state, RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = dst->t;
    p.color[0].load = RHI_LOAD_DONT_CARE; /* every pixel is written */
    p.colorCount = 1;
    p.width = dst->w;
    p.height = dst->h;
    rhi_cmd_begin_render_pass(cl, &p);
    const RhiViewport vp = {0.0f, 0.0f, (float)dst->w, (float)dst->h, 0.0f, 1.0f};
    rhi_cmd_set_viewport(cl, &vp);
    const RhiRect all = {0, 0, dst->w, dst->h};
    rhi_cmd_set_scissor(cl, &all);
    rd__set_pipeline(cl, pipe, k);
    rd__bind_uniform(cl, 0, rd__frame_group(dst->w, dst->h, 0.0f, 0.0f));
    /* the DrawCB slot: IcoFsrCB is DrawCB's size (the assert above) */
    rd__bind_uniform(cl, 1, rd__draw_group(cb));
    rhi_cmd_set_bind_group(cl, 2,
                           rd__tex_group(in, rd__sampler(RD_FILTER_LINEAR, RD_FILTER_LINEAR,
                                                         RD_WRAP_CLAMP, RD_WRAP_CLAMP)));
    rhi_cmd_draw(cl, 3, 0, 1);
    rhi_cmd_end_render_pass(cl);
    rd__transition(cl, dst->t, &dst->state, RHI_STATE_SHADER_READ);
}

bool rd__fsr1_record(RhiCommandList cl, RhiTexture *src, uint32_t *sw, uint32_t *sh,
                     const RhiRect *box, float crop, bool snapped)
{
    if (!s_on || rd__crt_on() || snapped || box->w == 0 || box->h == 0) {
        return false;
    }
    const float c = crop > 0.0f ? crop : 0.0f;
    const float ew = (float)*sw - 2.0f * c; /* the visible columns */
    if (!(ew < (float)box->w && (float)*sh < (float)box->h)) {
        return false; /* a texel a pixel or more on an axis: the box blit (or the reduce) */
    }
    const RdPipeKeyInt kE = rd__post_key(RD_VS_BLIT, RD_FS_FSR1_EASU, RHI_FMT_RGBA8_UNORM);
    const RdPipeKeyInt kR = rd__post_key(RD_VS_BLIT, RD_FS_FSR1_RCAS, RHI_FMT_RGBA8_UNORM);
    const RhiPipeline pE = rd__get_pipeline(&kE), pR = rd__get_pipeline(&kR);
    if (!pE.id || !pR.id || !ensure(&s_a, box->w, box->h, "rd fsr1 easu") ||
        !ensure(&s_b, box->w, box->h, "rd fsr1 rcas")) {
        if (!s_toldMissing) {
            s_toldMissing = true;
            rd__log("present: fsr1 cannot draw here (%s): the picture is stretched as with "
                    "Upscaling off",
                    !pE.id || !pR.id ? "no EASU or RCAS pipeline" : "no texture of the box's size");
        }
        return false;
    }
    const float sx = (float)box->w / ew, sy = (float)box->h / (float)*sh;
    const float stops = rd__fsr1_stops(s_sharp, sx > sy ? sx : sy);
    IcoFsrCB cb;
    memset(&cb, 0, sizeof(cb));
    uint32_t con[4][4];
    rd__fsr1_easu_con(con, ew, (float)*sh, (float)*sw, (float)*sh, (float)box->w, (float)box->h, c,
                      0.0f);
    memcpy(cb.con0, con[0], sizeof(cb.con0));
    memcpy(cb.con1, con[1], sizeof(cb.con1));
    memcpy(cb.con2, con[2], sizeof(cb.con2));
    memcpy(cb.con3, con[3], sizeof(cb.con3));
    rd__fsr1_rcas_con(cb.rcas, stops);
    cb.last[0] = box->w - 1u;
    cb.last[1] = box->h - 1u;
    pass(cl, pE, &kE, &s_a, *src, &cb);
    pass(cl, pR, &kR, &s_b, s_a.t, &cb);
    s_note[0] = *sw;
    s_note[1] = *sh;
    s_note[2] = box->w;
    s_note[3] = box->h;
    const uint32_t key[5] = {*sw, *sh, box->w, box->h, fbits(stops)};
    if (memcmp(key, s_lastLog, sizeof(key)) != 0) {
        memcpy(s_lastLog, key, sizeof(key));
        if (s_lines < 8u || s_lines % 100u == 0u) {
            rd__log("present: fsr1 %ux%u (%.1f columns shown) -> %ux%u, sharpness %u (RCAS %.2f "
                    "stops)%s",
                    *sw, *sh, (double)ew, box->w, box->h, s_sharp, (double)stops,
                    sx > 2.0f || sy > 2.0f ? ", past 2x a side: half a stop softer" : "");
        }
        s_lines++;
    }
    *src = s_b.t;
    *sw = box->w;
    *sh = box->h;
    return true;
}

bool rd__read_fsr1(void *dst, size_t dstSize, uint32_t *w, uint32_t *h)
{
    if (!g_rd.hasDevice || !s_b.t.id || s_b.state == RHI_STATE_UNDEFINED) {
        return false;
    }
    if (w) {
        *w = s_b.w;
    }
    if (h) {
        *h = s_b.h;
    }
    return rd__read_rhi_texture(s_b.t, &s_b.state, s_b.w, s_b.h, dst, dstSize);
}

void rd__fsr1_shutdown(void)
{
    FsrTex *all[2] = {&s_a, &s_b};
    for (int i = 0; i < 2; i++) {
        if (all[i]->t.id) {
            rhi_destroy_texture(all[i]->t);
        }
        memset(all[i], 0, sizeof(*all[i]));
    }
    s_on = s_inited = false;
    s_asked = 0;
    s_sharp = 0;
    s_lines = 0;
    memset(s_lastLog, 0, sizeof(s_lastLog));
    s_toldMissing = false;
}
