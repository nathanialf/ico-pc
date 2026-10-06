/* rd_crt.c: the CRT filter (package CRT; docs/port/DISPLAY.md "CRT filter",
 * RENDER_API.md "The CRT pass"; the shader is port/shaders/crt.hlsl).
 *
 * A present-time pass in place of the line doubling and the box blit of
 * rd_present.c: SCENE and DISPLAY are read, never written, so frame dumps,
 * rd_ReadDisplay and the deferred text's collection are what they are with
 * the filter off, and with RdSettings.crtMode RD_CRT_OFF (or strength 0)
 * nothing here runs.
 *
 * The filter works on the virtual source: the PS2 picture's pixel grid,
 * 512 pixels across at 4:3 (wider with the Enhanced aspect, as DISPLAY is)
 * and DISPLAY's lines (the PS2's field lines; 512 with full_height).  In the
 * Original preset that is DISPLAY itself; a larger DISPLAY (an Enhanced
 * resolution) is box-reduced to it first, since a CRT shows the picture at
 * the PS2's resolution whatever the scene was rendered at.  Then:
 *   1. crt_bloom_ps  virtual source -> A (half its size, RGBA16F): linear
 *                    light, 9-tap horizontal Gaussian;
 *   2. crt_blur_ps   A -> B, the vertical Gaussian;
 *   3. crt_ps        the box of the output (cleared black outside), from
 *                    the virtual source and B.
 * Three or four small passes (the glow targets are 256 x 128 in Original)
 * and one full-box pass of four taps on each of two lines.
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
     .sharpness = 1.0f,
     .mask = RD_CRT_MASK_NONE,
     .maskPitch = 3.0f,
     .gammaIn = 2.2f,
     .gammaOut = 2.2f},
    /* consumer: a period television, slot mask, glow, a curved face */
    {.scanline = 0.35f,
     .beamMin = 0.7f,
     .beamMax = 1.2f,
     .sharpness = 1.4f,
     .mask = RD_CRT_MASK_SLOT,
     .maskStrength = 0.35f,
     .maskPitch = 3.0f,
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
     .sharpness = 1.0f,
     .mask = RD_CRT_MASK_GRILLE,
     .maskStrength = 0.50f,
     .maskPitch = 3.0f,
     .halation = 0.05f,
     .bloom = 0.10f,
     .curvX = 0.030f,
     .curvY = 0.0f,
     .corner = 0.02f,
     .vignette = 0.08f,
     .gammaIn = 2.2f,
     .gammaOut = 2.2f},
    /* pvm: a studio monitor, a fine grille, sharp, flat */
    {.scanline = 0.60f,
     .beamMin = 0.4f,
     .beamMax = 0.9f,
     .sharpness = 0.7f,
     .mask = RD_CRT_MASK_GRILLE,
     .maskStrength = 0.30f,
     .maskPitch = 2.0f,
     .halation = 0.03f,
     .bloom = 0.05f,
     .corner = 0.01f,
     .vignette = 0.05f,
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

bool rd__CrtOn(void)
{
    const RdSettings *s = &g_rd.settings;
    return s->crtMode != RD_CRT_OFF && s->crtMode < RD_CRT_MODE_COUNT && s->crtStrength > 0.0f;
}

/* the textures: the virtual source (only when DISPLAY is not it) and the
 * two glow targets */
typedef struct CrtTex {
    RhiTexture t;
    RhiState state;
    uint32_t w, h;
    RhiFormat fmt;
} CrtTex;

static CrtTex s_src, s_glowA, s_glowB;

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
    CrtTex *all[3] = {&s_src, &s_glowA, &s_glowB};
    for (int i = 0; i < 3; i++) {
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
    /* the virtual source: DISPLAY's GS width widened with the aspect, its
     * lines (the full-height scene's 512) */
    const float wide = g_rd.wideX > 0.0f ? g_rd.wideX : 1.0f;
    uint32_t vw = (uint32_t)((float)disp->w / wide + 0.5f);
    uint32_t vh = disp->h * (g_rd.fullHeight ? 2u : 1u);
    vw = vw ? vw : 1;
    vh = vh ? vh : 1;
    RhiTexture src = disp->color;
    if (disp->tw != vw || disp->th != vh) {
        /* an Enhanced DISPLAY: its exact box average at the virtual size */
        const RdPipeKeyInt kRed = rd__PostKey(RD_VS_BLIT, RD_FS_BOX_REDUCE, RHI_FMT_RGBA8_UNORM);
        const RhiPipeline pRed = rd__GetPipeline(&kRed);
        if (!pRed.id || !ensure(&s_src, vw, vh, RHI_FMT_RGBA8_UNORM, "rd crt source")) {
            return false;
        }
        rd__Transition(cl, s_src.t, &s_src.state, RHI_STATE_RENDER_TARGET);
        const RhiRect all = {0, 0, vw, vh};
        beginPass(cl, s_src.t, vw, vh, RHI_LOAD_DONT_CARE, &all);
        IcoDrawCB cb;
        memset(&cb, 0, sizeof(cb));
        cb.uvRect[2] = cb.uvRect[3] = 1.0f;
        cb.tex[0] = cb.tex[1] = cb.tex[2] = cb.tex[3] = 1.0f;
        cb.param[0] = (float)disp->tw / (float)vw;
        cb.param[1] = (float)disp->th / (float)vh;
        cb.param[2] = (float)disp->tw;
        cb.param[3] = (float)disp->th;
        rhi_CmdSetPipeline(cl, pRed);
        rd__BindUniform(cl, 0, rd__FrameGroup(vw, vh, 0.0f, 0.0f));
        rd__BindUniform(cl, 1, rd__DrawGroup(&cb));
        rhi_CmdSetBindGroup(
            cl, 2,
            rd__TexGroup(disp->color, rd__Sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST,
                                                  RD_WRAP_CLAMP, RD_WRAP_CLAMP)));
        rhi_CmdDraw(cl, 3, 0, 1);
        rhi_CmdEndRenderPass(cl);
        rd__Transition(cl, s_src.t, &s_src.state, RHI_STATE_SHADER_READ);
        src = s_src.t;
    }
    const uint32_t gw = (vw + 1) / 2, gh = (vh + 1) / 2;
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
    cb.beam[3] = p.sharpness;
    cb.mask[0] = (float)p.mask;
    cb.mask[1] = p.mask != RD_CRT_MASK_NONE ? p.maskStrength * rd__CrtMaskFade(box->h) : 0.0f;
    cb.mask[2] = p.maskPitch < 2.0f ? 2.0f : p.maskPitch;
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
     * in RENDER_TARGET) */
    beginPass(cl, out, outW, outH, RHI_LOAD_CLEAR, box);
    crtPass(cl, pCrt, outW, outH, &cb, src, s_glowB.t);
    rhi_CmdEndRenderPass(cl);
    return true;
}
