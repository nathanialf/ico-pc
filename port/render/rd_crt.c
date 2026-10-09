/* rd_crt.c: the CRT filter (the shader is port/shaders/crt.hlsl).
 *
 * A present-time pass in place of the line doubling and the box blit of
 * rd_present.c, and the last pass of the present: SCENE and DISPLAY are
 * read, never written, so frame dumps and rd_read_display are what they are
 * with the filter off, and with RdSettings.crtMode RD_CRT_OFF (or strength
 * 0) nothing here runs.
 *
 * The filter works on the source grid: the PS2 picture's pixels, 512
 * across at 4:3 (wider with the Enhanced aspect, as DISPLAY is) and
 * DISPLAY's lines (the PS2's field lines; 512 with full_height).  With the
 * filter on the scene renders at 1x (rd__apply_display), so DISPLAY is that
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
 *                    source pixel lands in) passes that channel of that
 *                    pixel in full and the other two dimmed by the mask
 *                    strength, under the beam of its line at its height
 *                    (rd__crt_mask_weight); halation and bloom from B,
 *                    mixed in rather than added, vignette, rounded
 *                    corners, a soft shoulder, gamma (the highlights:
 *                    rd__crt_strength_at and the functions after it).  Nothing resamples
 *                    the mask: each output pixel is one source pixel seen
 *                    through one phosphor.
 */
#include <math.h>
#include <string.h>
#include "rd_internal.h"
#include "shader_consts.h"

/* The modes, in RdCrtMode order */
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
     .maskStrength = 0.40f,
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
     .maskStrength = 0.60f,
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
     .maskStrength = 0.45f,
     .halation = 0.08f,
     .bloom = 0.10f,
     .curvX = 0.020f,
     .curvY = 0.030f,
     .corner = 0.02f,
     .vignette = 0.10f,
     .gammaIn = 2.2f,
     .gammaOut = 2.2f},
};

void rd_crt_settings(RdSettings *s, RdCrtMode mode, float strength)
{
    if (!s) {
        return;
    }
    s->crtMode = (uint8_t)((unsigned)mode < RD_CRT_MODE_COUNT ? mode : RD_CRT_OFF);
    s->crtStrength = strength > 1.0f ? 1.0f : (strength > 0.0f ? strength : 0.0f);
    s->crtScanlines = s->crtMask = s->crtHalation = s->crtBloom = s->crtCurvature = -1.0f;
}

bool rd__crt_preset(RdCrtMode mode, RdCrtParams *p)
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

bool rd__crt_resolve(const RdSettings *s, RdCrtParams *p)
{
    if (!rd__crt_preset((RdCrtMode)s->crtMode, p)) {
        return false;
    }
    /* the config-only overrides ([video] crt_*), < 0 = the mode's */
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

float rd__crt_mask_fade(uint32_t boxH)
{
    if (boxH <= 720) {
        return 0.0f;
    }
    return boxH >= 1080 ? 1.0f : (float)(boxH - 720) / 360.0f;
}

/* The geometry, per output pixel (crt.hlsl maskOf is the same).  Across a
 * source pixel of r output pixels, u = f r: the last g = rd__crt_gap_columns(r)
 * pixels are a gap (all three channels at 1 - gap), the rest three equal
 * stripes, R, G, B from the left, each passing its own channel in full and
 * the other two at 1 - gap (the mask strength: the leak between the
 * phosphors; at 1 each stripe passes its own channel only).
 * The light is kept per triad: the stripes' weights of a channel over the
 * output columns its source pixel actually has (2 or 3 at 1440 x 1080, the
 * stripes 1 or 2 pixels wide) average to 1 after rd__crt_triad_gain, and the
 * slot's bridges over the line after rd__crt_row_gain, so every source
 * pixel keeps its own colour and light (with one gain for the whole box
 * the triads short of a stripe would tint in bands), up to where a lit
 * stripe would pass 1: in the highlights the gains fade (rd__crt_gain_fade)
 * and the triad is darker than its pixel, as on a tube.
 *   grille: the stripes run down the whole line;
 *   slot:   a bridge (all at 1 - gap) over the last third of the line,
 *           half a line later in the odd source columns (the slots
 *           staggered);
 *   dots:   the line's second half is a second row of dots, shifted half a
 *           triad rounded down to whole stripes (one stripe: f + 1/3, so
 *           at 3 output pixels a source pixel the stripes stay on whole
 *           pixels). */
uint32_t rd__crt_gap_columns(float r)
{
    return r >= 6.0f ? 2u : (r >= 4.0f ? 1u : 0u);
}

static float fract(float x)
{
    return x - floorf(x);
}

/* the column part of the weight: the stripes (the dots' second row
 * shifted), without the slot's bridge */
static float stripeWeight(int mask, float r, float gap, float f, float v, int ch)
{
    if (mask == RD_CRT_MASK_DOTS && v >= 0.5f) {
        f = fract(f + 1.0f / 3.0f);
    }
    const float g = (float)rd__crt_gap_columns(r), u = f * r, leak = 1.0f - gap;
    if (u >= r - g) {
        return leak;
    }
    int s = (int)(u * 3.0f / (r - g));
    s = s > 2 ? 2 : s;
    return s == ch ? 1.0f : leak;
}

float rd__crt_mask_weight(int mask, float r, float gap, float f, float v, int odd, int ch)
{
    if (mask == RD_CRT_MASK_NONE || !(r > 0.0f)) {
        return 1.0f;
    }
    float dim = 1.0f;
    if (mask == RD_CRT_MASK_SLOT && fract(v + (odd ? 0.5f : 0.0f)) >= 2.0f / 3.0f) {
        dim = 1.0f - gap;
    }
    return stripeWeight(mask, r, gap, f, v, ch) * dim;
}

float rd__crt_triad_gain(int mask, float r, float gap, int sx, float v, int ch)
{
    if (mask == RD_CRT_MASK_NONE || !(r > 0.0f)) {
        return 1.0f;
    }
    /* the output columns whose centres fall in source pixel sx */
    float sum = 0.0f;
    int n = 0;
    for (int x = (int)floorf((float)sx * r) - 1; x <= (int)ceilf((float)(sx + 1) * r) + 1; x++) {
        const float px = ((float)x + 0.5f) / r;
        if (x >= 0 && (int)floorf(px) == sx) {
            sum += stripeWeight(mask, r, gap, px - (float)sx, v, ch);
            n++;
        }
    }
    const float mean = n ? sum / (float)n : 1.0f;
    return 1.0f / (mean > RD_CRT_MEAN_MIN ? mean : RD_CRT_MEAN_MIN);
}

float rd__crt_row_gain(int mask, float gap)
{
    return mask == RD_CRT_MASK_SLOT && gap < 1.0f ? 1.0f / (1.0f - gap / 3.0f) : 1.0f;
}

/* The highlights (crt.hlsl strengthAt, gainFadeOf, the glow, shoulderOf;
 * the same functions, for the tests' model of a flat field).  The strength
 * eases off as the brightest channel goes from 0.5 to 1; the gains fade
 * before any lit stripe passes 1, so a white keeps its mask instead of
 * clipping flat; halation and bloom are mixed in (they replace part of the
 * colour, their weights scaled down when they sum past 1) rather than added
 * on top; and the shoulder compresses the top smoothly, scaling all three
 * channels by one factor, instead of clamping each channel, so light
 * colours keep their hue. */
static float smooth01(float e0, float e1, float x)
{
    float t = (x - e0) / (e1 - e0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

float rd__crt_strength_at(float strength, const float p[3])
{
    const float m = fmaxf(p[0], fmaxf(p[1], p[2]));
    return strength * (1.0f - 0.5f * smooth01(0.5f, 1.0f, m));
}

float rd__crt_triad_top(int mask, float r, float gap, int sx, float v, int ch)
{
    if (mask == RD_CRT_MASK_NONE || !(r > 0.0f)) {
        return 1.0f;
    }
    float top = 0.0f;
    int n = 0;
    for (int x = (int)floorf((float)sx * r) - 1; x <= (int)ceilf((float)(sx + 1) * r) + 1; x++) {
        const float px = ((float)x + 0.5f) / r;
        if (x >= 0 && (int)floorf(px) == sx) {
            top = fmaxf(top, stripeWeight(mask, r, gap, px - (float)sx, v, ch));
            n++;
        }
    }
    return n ? top : 1.0f;
}

float rd__crt_gain_fade(const float col[3], const float gain[3], const float top[3], float fade)
{
    float t = 1.0f;
    for (int k = 0; k < 3; k++) {
        const float over = col[k] * fade * top[k] * (gain[k] - 1.0f);
        if (over > 1e-6f) {
            t = fminf(t, (1.0f - col[k] * (1.0f + fade * (top[k] - 1.0f))) / over);
        }
    }
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

void rd__crt_glow_mix(float col[3], const float halo[3], const float glow[3], float halation,
                      float bloom, float st)
{
    const float l = 0.299f * glow[0] + 0.587f * glow[1] + 0.114f * glow[2];
    float kh = st * halation, kb = st * bloom * smooth01(0.2f, 1.0f, l);
    const float k = kh + kb;
    if (k > 1.0f) {
        kh /= k;
        kb /= k;
    }
    for (int c = 0; c < 3; c++) {
        col[c] = col[c] * (1.0f - kh - kb) + halo[c] * kh + glow[c] * kb;
    }
}

void rd__crt_shoulder(float c[3], float st)
{
    const float m = fmaxf(c[0], fmaxf(c[1], c[2])), k = 1.0f - 0.1f * st;
    if (m <= k) {
        return;
    }
    const float y = k >= 0.9999f ? 1.0f : k + (1.0f - k) * (1.0f - expf(-(m - k) / (1.0f - k)));
    for (int i = 0; i < 3; i++) {
        c[i] *= y / m;
    }
}

float rd__crt_beam(float c, float d, float beamMin, float beamMax)
{
    const float cl = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
    const float width = beamMin + (beamMax - beamMin) * cl;
    float sig = width / 2.3548f;
    sig = sig > 0.05f ? sig : 0.05f;
    return c * expf(-0.5f * d * d / (sig * sig)) / (sig * 2.5066283f);
}

void rd__crt_grid(uint32_t *vw, uint32_t *vh)
{
    const float wide = g_rd.wideX > 0.0f ? g_rd.wideX : 1.0f;
    const uint32_t gw = g_rd.gsW ? g_rd.gsW : 512, gh = g_rd.gsH ? g_rd.gsH : 512;
    const uint32_t w = (uint32_t)((float)gw / wide + 0.5f);
    const uint32_t h = (gh / 2) * (g_rd.fullHeight ? 2u : 1u);
    *vw = w ? w : 1;
    *vh = h ? h : 1;
}

bool rd__crt_on(void)
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
/* the films' own (rd__crt_record_film): their grid is not the game's, so
 * the game's targets are not resized at every film's start and end */
static CrtTex s_filmSrc, s_filmGlowA, s_filmGlowB;

/* the tests' view of the last composite (rd__crt_last_pass) */
static uint32_t s_passes, s_lastW, s_lastH;

static bool ensure(CrtTex *c, uint32_t w, uint32_t h, RhiFormat fmt, const char *name)
{
    if (c->t.id && c->w == w && c->h == h && c->fmt == fmt) {
        return true;
    }
    if (c->t.id) {
        rhi_destroy_texture(c->t);
    }
    c->t = rhi_create_texture(
        &(RhiTextureDesc){w, h, 1, fmt, RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED, name});
    c->state = RHI_STATE_UNDEFINED;
    c->w = w;
    c->h = h;
    c->fmt = fmt;
    return c->t.id != 0;
}

void rd__crt_shutdown(void)
{
    CrtTex *all[8] = {&s_src,   &s_layer,   &s_comp,      &s_glowA,
                      &s_glowB, &s_filmSrc, &s_filmGlowA, &s_filmGlowB};
    for (int i = 0; i < 8; i++) {
        if (all[i]->t.id) {
            rhi_destroy_texture(all[i]->t);
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
    rhi_cmd_begin_render_pass(cl, &p);
    RhiViewport vp = {(float)area->x, (float)area->y, (float)area->w, (float)area->h, 0.0f, 1.0f};
    rhi_cmd_set_viewport(cl, &vp);
    rhi_cmd_set_scissor(cl, area);
}

/* one CRT pass of fs into area of dst, CrtCB cb, t1 src, t2 glow */
static void crtPass(RhiCommandList cl, RhiPipeline pipe, uint32_t dw, uint32_t dh,
                    const IcoCrtCB *cb, RhiTexture src, RhiTexture glow)
{
    const RhiSampler lin =
        rd__sampler(RD_FILTER_LINEAR, RD_FILTER_LINEAR, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rhi_cmd_set_pipeline(cl, pipe);
    rd__bind_uniform(cl, 0, rd__frame_group(dw, dh, 0.0f, 0.0f));
    rd__bind_uniform(cl, 1, rd__crt_group(cb));
    rhi_cmd_set_bind_group(cl, 2,
                           glow.id ? rd__tex_group_date(src, lin, glow) : rd__tex_group(src, lin));
    rhi_cmd_draw(cl, 3, 0, 1);
}

/* the exact box average of src (sw x sh) into dst (dw x dh, RGBA8) */
static bool boxReduce(RhiCommandList cl, RhiTexture src, uint32_t sw, uint32_t sh, CrtTex *dst)
{
    const RdPipeKeyInt kRed = rd__post_key(RD_VS_BLIT, RD_FS_BOX_REDUCE, RHI_FMT_RGBA8_UNORM);
    const RhiPipeline pRed = rd__get_pipeline(&kRed);
    if (!pRed.id) {
        return false;
    }
    rd__transition(cl, dst->t, &dst->state, RHI_STATE_RENDER_TARGET);
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
    rhi_cmd_set_pipeline(cl, pRed);
    rd__bind_uniform(cl, 0, rd__frame_group(dst->w, dst->h, 0.0f, 0.0f));
    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(cl, 2,
                           rd__tex_group(src, rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST,
                                                          RD_WRAP_CLAMP, RD_WRAP_CLAMP)));
    rhi_cmd_draw(cl, 3, 0, 1);
    rhi_cmd_end_render_pass(cl);
    rd__transition(cl, dst->t, &dst->state, RHI_STATE_SHADER_READ);
    return true;
}

/* steps 1 to 3: src (vw x vh, the grid, SHADER_READ) through the glow
 * targets ga and gb into box of out, black around it */
static bool compose(RhiCommandList cl, const RdCrtParams *pp, RhiTexture src, uint32_t vw,
                    uint32_t vh, CrtTex *ga, CrtTex *gb, RhiTexture out, RhiFormat outFmt,
                    uint32_t outW, uint32_t outH, const RhiRect *box, int mirror)
{
    const RdCrtParams p = *pp;
    const RdPipeKeyInt kBloom = rd__post_key(RD_VS_CRT, RD_FS_CRT_BLOOM, RHI_FMT_RGBA16F);
    const RdPipeKeyInt kBlur = rd__post_key(RD_VS_CRT, RD_FS_CRT_BLUR, RHI_FMT_RGBA16F);
    const RdPipeKeyInt kCrt = rd__post_key(RD_VS_CRT, RD_FS_CRT, outFmt);
    const RhiPipeline pBloom = rd__get_pipeline(&kBloom), pBlur = rd__get_pipeline(&kBlur),
                      pCrt = rd__get_pipeline(&kCrt);
    if (!pBloom.id || !pBlur.id || !pCrt.id) {
        return false;
    }
    const uint32_t gw = (vw + 1) / 2, gh = (vh + 1) / 2;
    /* output pixels a source pixel across */
    const float r = (float)box->w / (float)vw;
    if (!ensure(ga, gw, gh, RHI_FMT_RGBA16F, "rd crt glow A") ||
        !ensure(gb, gw, gh, RHI_FMT_RGBA16F, "rd crt glow B")) {
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
    cb.beam[3] = (float)rd__crt_gap_columns(r);
    cb.mask[0] = (float)p.mask;
    cb.mask[1] = unit(p.maskStrength);
    cb.mask[2] = p.mask != RD_CRT_MASK_NONE ? rd__crt_mask_fade(box->h) : 0.0f;
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
    rd__transition(cl, ga->t, &ga->state, RHI_STATE_RENDER_TARGET);
    beginPass(cl, ga->t, gw, gh, RHI_LOAD_DONT_CARE, &glowArea);
    crtPass(cl, pBloom, gw, gh, &cb, src, (RhiTexture){0});
    rhi_cmd_end_render_pass(cl);
    rd__transition(cl, ga->t, &ga->state, RHI_STATE_SHADER_READ);
    cb.pass[2] = 1.0f / (float)gw;
    cb.pass[3] = 1.0f / (float)gh;
    rd__transition(cl, gb->t, &gb->state, RHI_STATE_RENDER_TARGET);
    beginPass(cl, gb->t, gw, gh, RHI_LOAD_DONT_CARE, &glowArea);
    crtPass(cl, pBlur, gw, gh, &cb, ga->t, (RhiTexture){0});
    rhi_cmd_end_render_pass(cl);
    rd__transition(cl, gb->t, &gb->state, RHI_STATE_SHADER_READ);

    /* 3. the composite into the box, black around it (the caller left out
     * in RENDER_TARGET): t1 the grid, t2 the glow */
    beginPass(cl, out, outW, outH, RHI_LOAD_CLEAR, box);
    crtPass(cl, pCrt, outW, outH, &cb, src, gb->t);
    rhi_cmd_end_render_pass(cl);
    s_passes++;
    s_lastW = vw;
    s_lastH = vh;
    return true;
}

bool rd__crt_record(RhiCommandList cl, const RdTargetRec *disp, RhiTexture out, RhiFormat outFmt,
                    uint32_t outW, uint32_t outH, const RhiRect *box, int mirror, bool overlay)
{
    RdCrtParams p;
    if (!rd__crt_resolve(&g_rd.settings, &p) || !disp || !disp->color.id) {
        return false;
    }
    /* the source grid: DISPLAY's GS width widened with the aspect, its
     * lines (the full-height scene's 512) */
    uint32_t vw, vh;
    rd__crt_grid(&vw, &vh);
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
    if (overlay && rd__overlay_grid_pending()) {
        const uint32_t lh = g_rd.gsH ? g_rd.gsH : 2 * vh;
        if (!ensure(&s_layer, vw, lh, RHI_FMT_RGBA8_UNORM, "rd crt overlay layer")) {
            return false;
        }
        rd__transition(cl, s_layer.t, &s_layer.state, RHI_STATE_RENDER_TARGET);
        const RhiRect all = {0, 0, vw, lh};
        rd__present_blit(cl, src, vw, vh, s_layer.t, RHI_FMT_RGBA8_UNORM, vw, lh,
                         RHI_LOAD_DONT_CARE, &all, RD_FILTER_NEAREST, mirror);
        rd__overlay_grid_draw(cl, s_layer.t, RHI_FMT_RGBA8_UNORM, vw, lh);
        rd__transition(cl, s_layer.t, &s_layer.state, RHI_STATE_SHADER_READ);
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
    return compose(cl, &p, src, vw, vh, &s_glowA, &s_glowB, out, outFmt, outW, outH, box, mirror);
}

void rd__crt_film_grid(uint32_t dispH, uint32_t *vw, uint32_t *vh)
{
    const uint32_t lines = dispH ? dispH : 576;
    *vw = g_rd.gsW ? g_rd.gsW : 512;
    *vh = g_rd.fullHeight ? lines : (lines + 1) / 2;
}

bool rd__crt_record_film(RhiCommandList cl, RhiTexture pic, uint32_t pw, uint32_t ph, uint32_t vw,
                         uint32_t vh, RhiTexture out, RhiFormat outFmt, uint32_t outW,
                         uint32_t outH, const RhiRect *box)
{
    RdCrtParams p;
    if (!rd__crt_resolve(&g_rd.settings, &p) || !pic.id || !vw || !vh) {
        return false;
    }
    RhiTexture src = pic;
    if (pw != vw || ph != vh) {
        if (!ensure(&s_filmSrc, vw, vh, RHI_FMT_RGBA8_UNORM, "rd crt film grid") ||
            !boxReduce(cl, pic, pw, ph, &s_filmSrc)) {
            return false;
        }
        src = s_filmSrc.t;
    }
    return compose(cl, &p, src, vw, vh, &s_filmGlowA, &s_filmGlowB, out, outFmt, outW, outH, box,
                   0);
}

uint32_t rd__crt_last_pass(uint32_t *vw, uint32_t *vh)
{
    if (vw) {
        *vw = s_lastW;
    }
    if (vh) {
        *vh = s_lastH;
    }
    return s_passes;
}
