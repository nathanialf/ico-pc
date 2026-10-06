/* rd_crt.c: the CRT filter (packages CRT and CRT2; docs/port/DISPLAY.md
 * "CRT filter", RENDER_API.md "The CRT pass"; the shader is
 * port/shaders/crt.hlsl).
 *
 * A present-time pass in place of the line doubling and the box blit of
 * rd_present.c, and the last pass of the present: SCENE and DISPLAY are
 * read, never written, so frame dumps and rd_ReadDisplay are what they are
 * with the filter off, and with RdSettings.crtMode RD_CRT_OFF (or strength
 * 0) nothing here runs.
 *
 * The filter works on the source grid: the PS2 picture's pixels, 512
 * across at 4:3 (wider with the Enhanced aspect, as DISPLAY is) and
 * DISPLAY's lines (the PS2's field lines; 512 with full_height).  With the
 * filter on the scene renders at 1x (rd__ApplyDisplay), so DISPLAY is that
 * grid; a larger one is box-reduced to it all the same.  Then:
 *   0. the overlay (the port's popups, hints, photo HUD) when it has prims:
 *      the source line-doubled into a layer of the frame's lines (nearest,
 *      flipped by the mirror mode), the overlay drawn into it at the
 *      frame's 1x scale, the layer box-reduced back to the grid: the UI is
 *      part of the picture the tube shows;
 *   1. crt_bloom_ps  grid -> A (half its size, RGBA16F): linear light,
 *                    9-tap horizontal Gaussian;
 *   2. crt_blur_ps   A -> B, the vertical Gaussian;
 *   3. crt_ps        the box of the output (cleared black outside), per
 *                    output pixel: the curvature warps its position, which
 *                    falls in one source pixel and line; the phosphor of
 *                    its column (the stripe or dot its position across the
 *                    source pixel lands in) passes that one channel of that
 *                    pixel, under the beam of its line at its height
 *                    (rd__CrtMaskWeight); halation and bloom from B,
 *                    vignette, rounded corners, gamma.  Nothing resamples
 *                    the mask: each output pixel is one channel of one
 *                    source pixel.
 */
#include <math.h>
#include <string.h>
#include "rd_internal.h"
#include "shader_consts.h"

/* The modes (RdCrtMode order, DISPLAY.md "CRT filter" holds the same table
 * and says what each imitates) */
static const RdCrtParams s_modes[RD_CRT_MODE_COUNT] = {
    /* RD_CRT_OFF: unused */
    {0},
    /* scanlines: the beam alone, on a flat, maskless screen */
    {.scanline = 0.50f,
     .beamMin = 0.6f,
     .beamMax = 1.0f,
     .mask = RD_CRT_MASK_NONE,
     .gammaIn = 2.2f,
     .gammaOut = 2.2f},
    /* consumer: a period television, slot mask, glow, a curved face */
    {.scanline = 0.35f,
     .beamMin = 0.7f,
     .beamMax = 1.2f,
     .mask = RD_CRT_MASK_SLOT,
     .maskStrength = 0.60f,
     .halation = 0.12f,
     .bloom = 0.15f,
     .curvX = 0.030f,
     .curvY = 0.045f,
     .corner = 0.03f,
     .vignette = 0.15f,
     .gammaIn = 2.4f,
     .gammaOut = 2.2f},
    /* trinitron: an aperture grille, flat vertically (cylindrical) */
    {.scanline = 0.45f,
     .beamMin = 0.5f,
     .beamMax = 1.0f,
     .mask = RD_CRT_MASK_GRILLE,
     .maskStrength = 0.50f,
     .halation = 0.05f,
     .bloom = 0.10f,
     .curvX = 0.030f,
     .curvY = 0.0f,
     .corner = 0.02f,
     .vignette = 0.08f,
     .gammaIn = 2.2f,
     .gammaOut = 2.2f},
    /* pvm: a studio monitor, a grille with darker gaps, sharp, flat */
    {.scanline = 0.60f,
     .beamMin = 0.4f,
     .beamMax = 0.9f,
     .mask = RD_CRT_MASK_GRILLE,
     .maskStrength = 0.80f,
     .halation = 0.03f,
     .bloom = 0.05f,
     .corner = 0.01f,
     .vignette = 0.05f,
     .gammaIn = 2.2f,
     .gammaOut = 2.2f},
    /* shadow: a dot-triad (delta) shadow mask, a gently curved face */
    {.scanline = 0.40f,
     .beamMin = 0.6f,
     .beamMax = 1.1f,
     .mask = RD_CRT_MASK_DOTS,
     .maskStrength = 0.60f,
     .halation = 0.08f,
     .bloom = 0.10f,
     .curvX = 0.020f,
     .curvY = 0.030f,
     .corner = 0.02f,
     .vignette = 0.10f,
     .gammaIn = 2.2f,
     .gammaOut = 2.2f},
};

void rd_CrtSettings(RdSettings *s, RdCrtMode mode, float strength)
{
    if (!s) {
        return;
    }
    s->crtMode = (uint8_t)((unsigned)mode < RD_CRT_MODE_COUNT ? mode : RD_CRT_OFF);
    s->crtStrength = strength > 1.0f ? 1.0f : (strength > 0.0f ? strength : 0.0f);
    s->crtScanlines = s->crtMask = s->crtHalation = s->crtBloom = s->crtCurvature = -1.0f;
}

bool rd__CrtPreset(RdCrtMode mode, RdCrtParams *p)
{
    if (mode == RD_CRT_OFF || (unsigned)mode >= RD_CRT_MODE_COUNT) {
        return false;
    }
    *p = s_modes[mode];
    return true;
}

static float unit(float v)
{
    return v > 1.0f ? 1.0f : v;
}

bool rd__CrtResolve(const RdSettings *s, RdCrtParams *p)
{
    if (!rd__CrtPreset((RdCrtMode)s->crtMode, p)) {
        return false;
    }
    /* the config-only overrides (CONFIG.md [video] crt_*), < 0 = the mode's */
    if (s->crtScanlines >= 0.0f) {
        p->scanline = unit(s->crtScanlines);
    }
    if (s->crtMask >= 0.0f) {
        p->maskStrength = unit(s->crtMask);
        if (p->mask == RD_CRT_MASK_NONE) {
            p->mask = RD_CRT_MASK_GRILLE; /* the scanlines mode has none: a grille */
        }
    }
    if (s->crtHalation >= 0.0f) {
        p->halation = unit(s->crtHalation);
    }
    if (s->crtBloom >= 0.0f) {
        p->bloom = unit(s->crtBloom);
    }
    if (s->crtCurvature >= 0.0f) {
        /* x as given, y half as much again, but never on the cylindrical
         * (Trinitron) face */
        const int cylinder = p->curvX > 0.0f && p->curvY == 0.0f;
        p->curvX = s->crtCurvature > 0.25f ? 0.25f : s->crtCurvature;
        p->curvY = cylinder ? 0.0f : p->curvX * 1.5f;
    }
    return true;
}

float rd__CrtMaskFade(uint32_t boxH)
{
    if (boxH <= 720) {
        return 0.0f;
    }
    return boxH >= 1080 ? 1.0f : (float)(boxH - 720) / 360.0f;
}

/* The geometry, per output pixel (crt.hlsl maskOf is the same).  Across a
 * source pixel of r output pixels, u = f r: the last g = rd__CrtGapColumns(r)
 * pixels are a gap (all three channels at 1 - gap), the rest three equal
 * stripes, R, G, B from the left, each passing its own channel only.
 *   grille: the stripes run down the whole line;
 *   slot:   a bridge (all at 1 - gap) over the last third of the line,
 *           half a line later in the odd source columns (the slots
 *           staggered);
 *   dots:   the line's second half is a second row of dots, shifted half a
 *           triad rounded down to whole stripes (one stripe: f + 1/3, so
 *           at 3 output pixels a source pixel the stripes stay on whole
 *           pixels). */
uint32_t rd__CrtGapColumns(float r)
{
    return r >= 6.0f ? 2u : (r >= 4.0f ? 1u : 0u);
}

static float fract(float x)
{
    return x - floorf(x);
}

float rd__CrtMaskWeight(int mask, float r, float gap, float f, float v, int odd, int ch)
{
    if (mask == RD_CRT_MASK_NONE || !(r > 0.0f)) {
        return 1.0f;
    }
    float dim = 1.0f;
    if (mask == RD_CRT_MASK_DOTS) {
        if (v >= 0.5f) {
            f = fract(f + 1.0f / 3.0f);
        }
    } else if (mask == RD_CRT_MASK_SLOT) {
        if (fract(v + (odd ? 0.5f : 0.0f)) >= 2.0f / 3.0f) {
            dim = 1.0f - gap;
        }
    }
    const float g = (float)rd__CrtGapColumns(r), u = f * r;
    if (u >= r - g) {
        return (1.0f - gap) * dim;
    }
    int s = (int)(u * 3.0f / (r - g));
    s = s > 2 ? 2 : s;
    return s == ch ? dim : 0.0f;
}

float rd__CrtMaskGain(int mask, float r, float gap)
{
    if (mask == RD_CRT_MASK_NONE || !(r > 0.0f)) {
        return 1.0f;
    }
    const float gf = (float)rd__CrtGapColumns(r) / r;
    const float cols = (1.0f - gf) / 3.0f + gf * (1.0f - gap);
    const float rows = mask == RD_CRT_MASK_SLOT ? 1.0f - gap / 3.0f : 1.0f;
    return cols * rows > 0.0f ? 1.0f / (cols * rows) : 1.0f;
}

void rd__CrtGrid(uint32_t *vw, uint32_t *vh)
{
    const float wide = g_rd.wideX > 0.0f ? g_rd.wideX : 1.0f;
    const uint32_t gw = g_rd.gsW ? g_rd.gsW : 512, gh = g_rd.gsH ? g_rd.gsH : 512;
    const uint32_t w = (uint32_t)((float)gw / wide + 0.5f);
    const uint32_t h = (gh / 2) * (g_rd.fullHeight ? 2u : 1u);
    *vw = w ? w : 1;
    *vh = h ? h : 1;
}

bool rd__CrtOn(void)
{
    const RdSettings *s = &g_rd.settings;
    return s->crtMode != RD_CRT_OFF && s->crtMode < RD_CRT_MODE_COUNT && s->crtStrength > 0.0f;
}

/* the textures: the reduced source (only when DISPLAY is not the grid),
 * the overlay's layer and the grid with the overlay in it, the two glow
 * targets */
typedef struct CrtTex {
    RhiTexture t;
    RhiState state;
    uint32_t w, h;
    RhiFormat fmt;
} CrtTex;

static CrtTex s_src, s_layer, s_comp, s_glowA, s_glowB;

static bool ensure(CrtTex *c, uint32_t w, uint32_t h, RhiFormat fmt, const char *name)
{
    if (c->t.id && c->w == w && c->h == h && c->fmt == fmt) {
        return true;
    }
    if (c->t.id) {
        rhi_DestroyTexture(c->t);
    }
    c->t = rhi_CreateTexture(
        &(RhiTextureDesc){w, h, 1, fmt, RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED, name});
    c->state = RHI_STATE_UNDEFINED;
    c->w = w;
    c->h = h;
    c->fmt = fmt;
    return c->t.id != 0;
}

void rd__CrtShutdown(void)
{
    CrtTex *all[5] = {&s_src, &s_layer, &s_comp, &s_glowA, &s_glowB};
    for (int i = 0; i < 5; i++) {
        if (all[i]->t.id) {
            rhi_DestroyTexture(all[i]->t);
        }
        memset(all[i], 0, sizeof(*all[i]));
    }
}

static void beginPass(RhiCommandList cl, RhiTexture t, uint32_t w, uint32_t h, RhiLoadOp load,
                      const RhiRect *area)
{
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = t;
    p.color[0].load = load;
    p.colorCount = 1;
    p.width = w;
    p.height = h;
    rhi_CmdBeginRenderPass(cl, &p);
    RhiViewport vp = {(float)area->x, (float)area->y, (float)area->w, (float)area->h, 0.0f, 1.0f};
    rhi_CmdSetViewport(cl, &vp);
    rhi_CmdSetScissor(cl, area);
}

/* one CRT pass of fs into area of dst, CrtCB cb, t1 src, t2 glow */
static void crtPass(RhiCommandList cl, RhiPipeline pipe, uint32_t dw, uint32_t dh,
                    const IcoCrtCB *cb, RhiTexture src, RhiTexture glow)
{
    const RhiSampler lin =
        rd__Sampler(RD_FILTER_LINEAR, RD_FILTER_LINEAR, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rhi_CmdSetPipeline(cl, pipe);
    rd__BindUniform(cl, 0, rd__FrameGroup(dw, dh, 0.0f, 0.0f));
    rd__BindUniform(cl, 1, rd__CrtGroup(cb));
    rhi_CmdSetBindGroup(cl, 2, glow.id ? rd__TexGroupDate(src, lin, glow) : rd__TexGroup(src, lin));
    rhi_CmdDraw(cl, 3, 0, 1);
}

/* the exact box average of src (sw x sh) into dst (dw x dh, RGBA8) */
static bool boxReduce(RhiCommandList cl, RhiTexture src, uint32_t sw, uint32_t sh, CrtTex *dst)
{
    const RdPipeKeyInt kRed = rd__PostKey(RD_VS_BLIT, RD_FS_BOX_REDUCE, RHI_FMT_RGBA8_UNORM);
    const RhiPipeline pRed = rd__GetPipeline(&kRed);
    if (!pRed.id) {
        return false;
    }
    rd__Transition(cl, dst->t, &dst->state, RHI_STATE_RENDER_TARGET);
    const RhiRect all = {0, 0, dst->w, dst->h};
    beginPass(cl, dst->t, dst->w, dst->h, RHI_LOAD_DONT_CARE, &all);
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.uvRect[2] = cb.uvRect[3] = 1.0f;
    cb.tex[0] = cb.tex[1] = cb.tex[2] = cb.tex[3] = 1.0f;
    cb.param[0] = (float)sw / (float)dst->w;
    cb.param[1] = (float)sh / (float)dst->h;
    cb.param[2] = (float)sw;
    cb.param[3] = (float)sh;
    rhi_CmdSetPipeline(cl, pRed);
    rd__BindUniform(cl, 0, rd__FrameGroup(dst->w, dst->h, 0.0f, 0.0f));
    rd__BindUniform(cl, 1, rd__DrawGroup(&cb));
    rhi_CmdSetBindGroup(cl, 2,
                        rd__TexGroup(src, rd__Sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST,
                                                      RD_WRAP_CLAMP, RD_WRAP_CLAMP)));
    rhi_CmdDraw(cl, 3, 0, 1);
    rhi_CmdEndRenderPass(cl);
    rd__Transition(cl, dst->t, &dst->state, RHI_STATE_SHADER_READ);
    return true;
}

bool rd__CrtRecord(RhiCommandList cl, const RdTargetRec *disp, RhiTexture out, RhiFormat outFmt,
                   uint32_t outW, uint32_t outH, const RhiRect *box, int mirror)
{
    RdCrtParams p;
    if (!rd__CrtResolve(&g_rd.settings, &p) || !disp || !disp->color.id) {
        return false;
    }
    const RdPipeKeyInt kBloom = rd__PostKey(RD_VS_CRT, RD_FS_CRT_BLOOM, RHI_FMT_RGBA16F);
    const RdPipeKeyInt kBlur = rd__PostKey(RD_VS_CRT, RD_FS_CRT_BLUR, RHI_FMT_RGBA16F);
    const RdPipeKeyInt kCrt = rd__PostKey(RD_VS_CRT, RD_FS_CRT, outFmt);
    const RhiPipeline pBloom = rd__GetPipeline(&kBloom), pBlur = rd__GetPipeline(&kBlur),
                      pCrt = rd__GetPipeline(&kCrt);
    if (!pBloom.id || !pBlur.id || !pCrt.id) {
        return false;
    }
    /* the source grid: DISPLAY's GS width widened with the aspect, its
     * lines (the full-height scene's 512) */
    uint32_t vw, vh;
    rd__CrtGrid(&vw, &vh);
    RhiTexture src = disp->color;
    if (disp->tw != vw || disp->th != vh) {
        /* a larger DISPLAY: its exact box average at the grid's size */
        if (!ensure(&s_src, vw, vh, RHI_FMT_RGBA8_UNORM, "rd crt source") ||
            !boxReduce(cl, disp->color, disp->tw, disp->th, &s_src)) {
            return false;
        }
        src = s_src.t;
    }
    /* 0. the overlay into the grid: the source line-doubled (and flipped by
     * the mirror mode) into the layer of the frame's lines, the prims over
     * it, the layer reduced back; the grid is then the shown orientation */
    if (rd__OverlayGridPending()) {
        const uint32_t lh = g_rd.gsH ? g_rd.gsH : 2 * vh;
        if (!ensure(&s_layer, vw, lh, RHI_FMT_RGBA8_UNORM, "rd crt overlay layer")) {
            return false;
        }
        rd__Transition(cl, s_layer.t, &s_layer.state, RHI_STATE_RENDER_TARGET);
        const RhiRect all = {0, 0, vw, lh};
        rd__PresentBlit(cl, src, vw, vh, s_layer.t, RHI_FMT_RGBA8_UNORM, vw, lh, RHI_LOAD_DONT_CARE,
                        &all, RD_FILTER_NEAREST, mirror);
        rd__OverlayGridDraw(cl, s_layer.t, RHI_FMT_RGBA8_UNORM, vw, lh);
        rd__Transition(cl, s_layer.t, &s_layer.state, RHI_STATE_SHADER_READ);
        if (lh == vh) {
            src = s_layer.t;
        } else {
            if (!ensure(&s_comp, vw, vh, RHI_FMT_RGBA8_UNORM, "rd crt grid") ||
                !boxReduce(cl, s_layer.t, vw, lh, &s_comp)) {
                return false;
            }
            src = s_comp.t;
        }
        mirror = 0;
    }
    const uint32_t gw = (vw + 1) / 2, gh = (vh + 1) / 2;
    /* output pixels a source pixel across */
    const float r = (float)box->w / (float)vw;
    if (!ensure(&s_glowA, gw, gh, RHI_FMT_RGBA16F, "rd crt glow A") ||
        !ensure(&s_glowB, gw, gh, RHI_FMT_RGBA16F, "rd crt glow B")) {
        return false;
    }
    IcoCrtCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.src[0] = (float)vw;
    cb.src[1] = (float)vh;
    cb.src[2] = 1.0f / (float)vw;
    cb.src[3] = 1.0f / (float)vh;
    cb.box[0] = (float)box->w;
    cb.box[1] = (float)box->h;
    cb.box[2] = (float)box->x;
    cb.box[3] = (float)box->y;
    cb.beam[0] = p.scanline;
    cb.beam[1] = p.beamMin;
    cb.beam[2] = p.beamMax;
    cb.beam[3] = (float)rd__CrtGapColumns(r);
    cb.mask[0] = (float)p.mask;
    cb.mask[1] = unit(p.maskStrength);
    cb.mask[2] = p.mask != RD_CRT_MASK_NONE ? rd__CrtMaskFade(box->h) : 0.0f;
    cb.mask[3] = p.halation;
    cb.glow[0] = p.bloom;
    cb.glow[1] = p.curvX;
    cb.glow[2] = p.curvY;
    cb.glow[3] = p.corner;
    cb.tone[0] = p.vignette;
    cb.tone[1] = p.gammaIn;
    cb.tone[2] = p.gammaOut > 0.1f ? p.gammaOut : 2.2f;
    cb.tone[3] = unit(g_rd.settings.crtStrength);
    cb.pass[0] = mirror ? 1.0f : 0.0f;
    cb.pass[1] = rd__CrtMaskGain(p.mask, r, unit(p.maskStrength));

    /* 1. the horizontal glow, 2. the vertical */
    const RhiRect glowArea = {0, 0, gw, gh};
    rd__Transition(cl, s_glowA.t, &s_glowA.state, RHI_STATE_RENDER_TARGET);
    beginPass(cl, s_glowA.t, gw, gh, RHI_LOAD_DONT_CARE, &glowArea);
    crtPass(cl, pBloom, gw, gh, &cb, src, (RhiTexture){0});
    rhi_CmdEndRenderPass(cl);
    rd__Transition(cl, s_glowA.t, &s_glowA.state, RHI_STATE_SHADER_READ);
    cb.pass[2] = 1.0f / (float)gw;
    cb.pass[3] = 1.0f / (float)gh;
    rd__Transition(cl, s_glowB.t, &s_glowB.state, RHI_STATE_RENDER_TARGET);
    beginPass(cl, s_glowB.t, gw, gh, RHI_LOAD_DONT_CARE, &glowArea);
    crtPass(cl, pBlur, gw, gh, &cb, s_glowA.t, (RhiTexture){0});
    rhi_CmdEndRenderPass(cl);
    rd__Transition(cl, s_glowB.t, &s_glowB.state, RHI_STATE_SHADER_READ);

    /* 3. the composite into the box, black around it (the caller left out
     * in RENDER_TARGET): t1 the grid, t2 the glow */
    beginPass(cl, out, outW, outH, RHI_LOAD_CLEAR, box);
    crtPass(cl, pCrt, outW, outH, &cb, src, s_glowB.t);
    rhi_CmdEndRenderPass(cl);
    return true;
}
