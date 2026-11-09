/* rd_upscale_capture.c: the upscaler's pictures for a bug report
 * (rd_upscale_capture.h; rd.h rd_capture_upscale; issue 203).
 *
 * Armed by F12 / the bug-report row (window_host.c frame_dump) or the
 * replay tool's --fsr3-dump; taken by the next dispatch of the upscale
 * point: SCENE's colour, VELOCITY and COMPOSITE copied texture to texture
 * and SCENE's depth copied whole (rd__depth_copy_take), all on the replay's
 * command list after the dispatch, outside any pass, into textures of this
 * file. Copies only read their sources, and each source goes back to the
 * state it was in, so the frame draws what it drew without the capture.
 * The copies are read back (the reads wait for the GPU) and the PNGs
 * written after the submit of the present that followed the dispatch
 * (rd_present.c rd__capture_finish), before that present's own picture is
 * written, so F12's line and the bug-report zip find all five. */
#include "rd_upscale_capture.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd.h"
#include "rd_fsr3.h"
#include "rd_thread.h"
#include "rd_velocity.h"

enum { PIC_SCENE = 0, PIC_VELOCITY, PIC_DEPTH, PIC_COMPOSITE };

static const char *const kSuffix[RD_UPSCALE_CAPTURE_PICTURES] = {"-scene.png", "-velocity.png",
                                                                 "-depth.png", "-composite.png"};

typedef struct CapTex {
    RhiTexture tex;
    RhiState state;
    uint32_t w, h;
    RhiFormat fmt;
} CapTex;

static struct {
    int armed;       /* waiting for a dispatch */
    int copied;      /* the copies are recorded: read them after the submit */
    int waitedTold;  /* the "no dispatch at this present" line given for this arm */
    char stem[1024]; /* the pictures' path without the suffix */
    uint32_t maxW;   /* halve wider pictures until they fit; 0 none */
    uint32_t frame;  /* the dispatch's frame */
    bool depthOk;    /* the depth was copied */
    CapTex col[3];   /* SCENE, VELOCITY, COMPOSITE (the depth in `depth`) */
    bool have[3];    /* this capture copied col[i] */
    bool given[3];   /* the upscaler was given col[i]'s source (else not a failure) */
    bool depthGiven;
    RdDepthCopy depth;
    RdUpscaleCaptureStats st;
} s_uc;

const char *rd__upscale_capture_suffix(int i)
{
    return i >= 0 && i < RD_UPSCALE_CAPTURE_PICTURES ? kSuffix[i] : "";
}

const RdUpscaleCaptureStats *rd__upscale_capture_stats(void)
{
    return &s_uc.st;
}

bool rd_capture_upscale(const char *stem, uint32_t maxW)
{
    if (!g_rd.hasDevice || !stem || !stem[0] || strlen(stem) + 16 >= sizeof(s_uc.stem)) {
        return false;
    }
    (void)rd_thread_drain(); /* no replay of the render thread reads the request now */
    snprintf(s_uc.stem, sizeof(s_uc.stem), "%s", stem);
    s_uc.maxW = maxW;
    s_uc.armed = 1;
    s_uc.copied = 0;
    s_uc.waitedTold = 0;
    s_uc.st.armed++;
    return true;
}

/* src (state *srcState) into the capture texture c (made or remade at w x
 * h, fmt); src goes back to the state it was in */
static bool copyInto(RhiCommandList cl, CapTex *c, RhiTexture src, RhiState *srcState, uint32_t w,
                     uint32_t h, RhiFormat fmt, const char *name)
{
    if (!src.id || !srcState || !w || !h) {
        return false;
    }
    if (!c->tex.id || c->w != w || c->h != h || c->fmt != fmt) {
        if (c->tex.id) {
            rd__retire_texture(c->tex);
        }
        c->tex = rhi_create_texture(
            &(RhiTextureDesc){w, h, 1, fmt, RHI_TEX_COPY_SRC | RHI_TEX_COPY_DST, name, 0});
        c->state = RHI_STATE_UNDEFINED;
        c->w = w;
        c->h = h;
        c->fmt = fmt;
        if (!c->tex.id) {
            return false;
        }
    }
    const RhiState was = *srcState;
    rd__transition(cl, src, srcState, RHI_STATE_COPY_SRC);
    rd__transition(cl, c->tex, &c->state, RHI_STATE_COPY_DST);
    rhi_cmd_copy_texture(cl, src, (RhiRect){0, 0, w, h}, c->tex, 0, 0);
    rd__transition(cl, src, srcState, was);
    return true;
}

void rd__upscale_capture_take(RhiCommandList cl, const RdUpscaleIn *in, RdTargetRec *depthRec)
{
    if (!s_uc.armed || !in || !cl.id) {
        return;
    }
    s_uc.armed = 0;
    s_uc.frame = in->frame;
    s_uc.given[0] = in->color.id != 0;
    s_uc.given[1] = in->velocity.id != 0;
    s_uc.given[2] = in->out.id != 0;
    s_uc.depthGiven = in->depth.id != 0;
    s_uc.have[0] = copyInto(cl, &s_uc.col[0], in->color, in->colorState, in->colorW, in->colorH,
                            RHI_FMT_RGBA8_UNORM, "rd upscale capture scene");
    s_uc.have[1] = copyInto(cl, &s_uc.col[1], in->velocity, in->velocityState, in->velocityW,
                            in->velocityH, RHI_FMT_RGBA16F, "rd upscale capture velocity");
    s_uc.have[2] = copyInto(cl, &s_uc.col[2], in->out, in->outState, in->outW, in->outH,
                            RHI_FMT_RGBA8_UNORM, "rd upscale capture composite");
    s_uc.depthOk = false;
    if (in->depth.id && depthRec && depthRec->depth.id &&
        depthRec->depthState != RHI_STATE_UNDEFINED) {
        const RhiState was = depthRec->depthState;
        s_uc.depthOk =
            rd__depth_copy_take(cl, depthRec, &s_uc.depth, "rd upscale capture depth").id != 0;
        if (depthRec->depthState != was) {
            rd__transition(cl, depthRec->depth, &depthRec->depthState, was);
        }
    }
    s_uc.copied = 1;
    s_uc.st.taken++;
}

/* a 2 x 2 box halving of an RGBA8 picture in place (*w, *h updated) */
static void halve(uint8_t *px, uint32_t *w, uint32_t *h)
{
    const uint32_t nw = *w / 2u ? *w / 2u : 1u, nh = *h / 2u ? *h / 2u : 1u;
    for (uint32_t y = 0; y < nh; y++) {
        for (uint32_t x = 0; x < nw; x++) {
            const uint32_t x1 = 2u * x + 1u < *w ? 2u * x + 1u : 2u * x;
            const uint32_t y1 = 2u * y + 1u < *h ? 2u * y + 1u : 2u * y;
            const uint8_t *a = px + ((size_t)(2u * y) * *w + 2u * x) * 4u;
            const uint8_t *b = px + ((size_t)(2u * y) * *w + x1) * 4u;
            const uint8_t *c = px + ((size_t)y1 * *w + 2u * x) * 4u;
            const uint8_t *d = px + ((size_t)y1 * *w + x1) * 4u;
            uint8_t *o = px + ((size_t)y * nw + x) * 4u;
            for (int k = 0; k < 4; k++) {
                o[k] = (uint8_t)((a[k] + b[k] + c[k] + d[k] + 2u) / 4u);
            }
        }
    }
    *w = nw;
    *h = nh;
}

static bool writePic(int i, uint8_t *px, uint32_t w, uint32_t h, char *line, size_t cap, size_t *at)
{
    char path[1100];
    snprintf(path, sizeof(path), "%s%s", s_uc.stem, kSuffix[i]);
    const uint32_t w0 = w;
    while (s_uc.maxW && w > s_uc.maxW && w > 1u) {
        halve(px, &w, &h);
    }
    const bool ok = rd_write_png(path, px, w, h, w * 4u, 0);
    if (ok) {
        s_uc.st.w[i] = w;
        s_uc.st.h[i] = h;
    }
    if (*at < cap) {
        const int n =
            snprintf(line + *at, cap - *at, "%s%s %ux%u%s%s", *at ? ", " : "", kSuffix[i] + 1, w, h,
                     w != w0 ? " (halved from the texture's size)" : "", ok ? "" : " NOT WRITTEN");
        *at += n > 0 ? (size_t)n : 0u;
    }
    return ok;
}

static float halfToFloat(uint16_t v)
{
    const uint32_t s = (uint32_t)(v >> 15) & 1u, e = (uint32_t)(v >> 10) & 31u, m = v & 1023u;
    float f;
    if (e == 0u) {
        f = ldexpf((float)m, -24);
    } else if (e == 31u) {
        f = m ? NAN : INFINITY;
    } else {
        f = ldexpf((float)(m | 1024u), (int)e - 25);
    }
    return s ? -f : f;
}

/* the colour pictures: SCENE and COMPOSITE opaque, VELOCITY mapped */
static bool readColour(int which, char *line, size_t cap, size_t *at)
{
    const int i = which == 0 ? PIC_SCENE : (which == 1 ? PIC_VELOCITY : PIC_COMPOSITE);
    CapTex *c = &s_uc.col[which];
    s_uc.st.w[i] = s_uc.st.h[i] = 0;
    if (!s_uc.given[which]) {
        if (*at < cap) {
            const int n = snprintf(line + *at, cap - *at, "%s%s none (not given to the upscaler)",
                                   *at ? ", " : "", kSuffix[i] + 1);
            *at += n > 0 ? (size_t)n : 0u;
        }
        return true; /* nothing to take: not a failure */
    }
    if (!s_uc.have[which] || !c->tex.id || c->state == RHI_STATE_UNDEFINED) {
        if (*at < cap) {
            const int n =
                snprintf(line + *at, cap - *at, "%s%s not copied", *at ? ", " : "", kSuffix[i] + 1);
            *at += n > 0 ? (size_t)n : 0u;
        }
        return false;
    }
    const size_t n = (size_t)c->w * c->h;
    uint8_t *px = malloc(n * 4u);
    if (!px) {
        return false;
    }
    bool ok;
    if (which == 1) {
        uint16_t *raw = malloc(n * 4u * sizeof(uint16_t));
        /* 8 bytes a texel: read as twice as many 4-byte texels */
        ok = raw && rd__read_rhi_texture(c->tex, &c->state, c->w * 2u, c->h, raw, n * 8u);
        for (size_t k = 0; ok && k < n; k++) {
            const float vx = halfToFloat(raw[k * 4]), vy = halfToFloat(raw[k * 4 + 1]);
            const float va = halfToFloat(raw[k * 4 + 3]);
            uint8_t *o = &px[k * 4];
            if (!(va > 0.5f)) {
                o[0] = o[1] = o[2] = 0;
                o[3] = 255;
                continue;
            }
            const double x = 128.0 + vx * 8.0, y = 128.0 + vy * 8.0;
            const double b = sqrt((double)vx * vx + (double)vy * vy) * 16.0;
            o[0] = (uint8_t)(x < 0.0 ? 0.0 : (x > 255.0 ? 255.0 : x));
            o[1] = (uint8_t)(y < 0.0 ? 0.0 : (y > 255.0 ? 255.0 : y));
            o[2] = (uint8_t)(b > 255.0 ? 255.0 : b);
            o[3] = 255;
        }
        free(raw);
    } else {
        ok = rd__read_rhi_texture(c->tex, &c->state, c->w, c->h, px, n * 4u);
        for (size_t k = 0; ok && k < n; k++) {
            px[k * 4 + 3] = 255;
        }
    }
    ok = ok && writePic(i, px, c->w, c->h, line, cap, at);
    free(px);
    return ok;
}

static bool readDepth(char *line, size_t cap, size_t *at)
{
    RdDepthCopy *d = &s_uc.depth;
    const char *why = NULL;
    bool fail = true;
    s_uc.st.w[PIC_DEPTH] = s_uc.st.h[PIC_DEPTH] = 0;
    if (!s_uc.depthGiven) {
        why = "none (not given to the upscaler)";
        fail = false;
    } else if (!s_uc.depthOk || !d->tex.id) {
        why = "not copied";
    } else if (!rhi_limits()->depthReadback) {
        why = "not written (the device's depth cannot be read back: D24S8)";
        fail = false; /* the device's limit, said in the line */
    }
    if (why) {
        if (*at < cap) {
            const int n = snprintf(line + *at, cap - *at, "%sdepth.png %s", *at ? ", " : "", why);
            *at += n > 0 ? (size_t)n : 0u;
        }
        return !fail;
    }
    const size_t n = (size_t)d->w * d->h;
    float *z = malloc(n * sizeof(float));
    uint8_t *px = malloc(n * 4u);
    bool ok = z && px;
    if (ok && d->state != RHI_STATE_COPY_SRC) {
        RhiCommandList cl = rhi_begin_commands();
        ok = cl.id != 0;
        if (ok) {
            rd__transition(cl, d->tex, &d->state, RHI_STATE_COPY_SRC);
            rhi_end_commands(cl);
            rhi_submit(cl);
        }
    }
    uint32_t pitch = 0;
    ok = ok && rhi_readback_texture(d->tex, RHI_ASPECT_DEPTH, z, n * sizeof(float), &pitch);
    if (ok && pitch != d->w * 4u && pitch) {
        uint8_t *p = (uint8_t *)z;
        for (uint32_t y = 1; y < d->h; y++) {
            memmove(p + (size_t)y * d->w * 4u, p + (size_t)y * pitch, (size_t)d->w * 4u);
        }
    }
    for (size_t k = 0; ok && k < n; k++) {
        float v = z[k];
        v = v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f;
        const uint32_t s = (uint32_t)lrintf(v * 16777215.0f);
        px[k * 4] = (uint8_t)(s >> 16);
        px[k * 4 + 1] = (uint8_t)(s >> 8);
        px[k * 4 + 2] = (uint8_t)s;
        px[k * 4 + 3] = 255;
    }
    ok = ok && writePic(PIC_DEPTH, px, d->w, d->h, line, cap, at);
    free(z);
    free(px);
    return ok;
}

int rd__upscale_capture_finish(void)
{
    if (!s_uc.copied) {
        if (s_uc.armed && !rd__upscale_point_on()) {
            s_uc.armed = 0;
            s_uc.st.dropped++;
            rd__log("upscale capture: no upscaler in force at the present after the request: "
                    "%s-scene.png and the rest not written (Upscaling off, or the Original "
                    "preset)",
                    s_uc.stem);
        } else if (s_uc.armed && !s_uc.waitedTold) {
            s_uc.waitedTold = 1;
            rd__log("upscale capture: the present after the request showed a picture again "
                    "without a replay (no dispatch): the upscaler's pictures are taken at the "
                    "next replay, beside the present's picture but after it (not in a "
                    "bug-report zip already written)");
        }
        return 0;
    }
    s_uc.copied = 0;
    const double t0 = rd__now_ms();
    char line[640];
    size_t at = 0;
    line[0] = '\0';
    bool ok = readColour(0, line, sizeof(line), &at);
    ok = readColour(1, line, sizeof(line), &at) && ok;
    ok = readDepth(line, sizeof(line), &at) && ok;
    ok = readColour(2, line, sizeof(line), &at) && ok;
    if (ok) {
        s_uc.st.written++;
    } else {
        s_uc.st.failed++;
    }
    rd__log("upscale capture: frame %u: %s%s, %s, %s, %s: %s (read back in %.1f ms; the "
            "upscaler's inputs and output: lines in -scene.png are the port's, lines only in "
            "-composite.png are the upscaler's)",
            s_uc.frame, s_uc.stem, kSuffix[0], kSuffix[1], kSuffix[2], kSuffix[3], line,
            rd__now_ms() - t0);
    return ok ? 1 : -1;
}

void rd__upscale_capture_free(void)
{
    for (int i = 0; i < 3; i++) {
        if (s_uc.col[i].tex.id) {
            rd__retire_texture(s_uc.col[i].tex);
        }
        memset(&s_uc.col[i], 0, sizeof(s_uc.col[i]));
        s_uc.have[i] = false;
    }
    rd__depth_copy_free(&s_uc.depth);
    s_uc.copied = 0;
    s_uc.depthOk = false;
}

/* ---------------------------------------- what the dispatched frames held
 * The 10 s line under an upscaler (issue 203: a report's log says which of
 * the game's effects were in the pictures the upscaler took, so a flaw in
 * one kind of scene can be tied to a pass without a dump): each dispatched
 * frame's lists scanned for the post passes it draws (RDC_POST_STUB by
 * kind), the render-to-texture blocks it draws into (the water's
 * reflections, the queen's barrier: RDC_TARGET naming a block) and its
 * film noise; counted a frame, not a pass. Nothing changes a picture. */
enum {
    HELD_FOG = 0,
    HELD_GLOW,
    HELD_AURA,
    HELD_DOF,
    HELD_MBLUR,
    HELD_NOISE,
    HELD_BLOCK,
    HELD_COUNT
};

static struct {
    double blockAt;
    uint32_t frames, held[HELD_COUNT];
    uint32_t lastFrame; /* the frame number scanned last, and what it held */
    bool scanned;
    bool has[HELD_COUNT];
} s_held;

static void scanFrame(const RdFrame *f, bool *has)
{
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < f->lists[l].count; i++) {
            const RdCmd *c = &f->lists[l].cmds[i];
            if (c->type == RDC_POST_STUB) {
                switch (rd__perf_post_of_kind(c->b[0])) {
                case RD_PERF_POST_FOG:
                    has[HELD_FOG] = true;
                    break;
                case RD_PERF_POST_GLOW:
                    has[HELD_GLOW] = true;
                    break;
                case RD_PERF_POST_AURA:
                    has[HELD_AURA] = true;
                    break;
                case RD_PERF_POST_DOF:
                    has[HELD_DOF] = true;
                    break;
                case RD_PERF_POST_MOTION_BLUR:
                    has[HELD_MBLUR] = true;
                    break;
                default:
                    has[HELD_NOISE] |= c->b[0] == RD_POST_FILM_NOISE;
                    break;
                }
            } else if (c->type == RDC_TARGET && !has[HELD_BLOCK]) {
                const RdTargetRec *t = rd__target_rec(c->u[0]);
                has[HELD_BLOCK] = t && t->block;
            }
        }
    }
}

void rd__upscale_note_frame(const RdFrame *f)
{
    if (!f) {
        return;
    }
    /* a tick's presents replay the same frame number: its lists scanned
     * once (the interpolated presents draw the same passes) */
    bool *has = s_held.has;
    if (f->number != s_held.lastFrame || !s_held.scanned) {
        s_held.lastFrame = f->number;
        s_held.scanned = true;
        memset(s_held.has, 0, sizeof(s_held.has));
        scanFrame(f, has);
    }
    s_held.frames++;
    for (int k = 0; k < HELD_COUNT; k++) {
        s_held.held[k] += has[k] ? 1u : 0u;
    }
    const double now = rd__now_ms();
    if (s_held.blockAt == 0.0) {
        s_held.blockAt = now;
        return;
    }
    if (now - s_held.blockAt < 10000.0) {
        return;
    }
    rd__log("upscale point: 10 s: the %u pictures dispatched held: the fog in %u, the glow chain "
            "in %u, the aura in %u, the depth of field in %u, the motion blur in %u, film noise "
            "in %u, render-to-texture blocks (the water's reflections) in %u; FSR 3.1 fp16 %s "
            "(the motion vectors' refusals on the velocity line)",
            s_held.frames, s_held.held[HELD_FOG], s_held.held[HELD_GLOW], s_held.held[HELD_AURA],
            s_held.held[HELD_DOF], s_held.held[HELD_MBLUR], s_held.held[HELD_NOISE],
            s_held.held[HELD_BLOCK],
            rd__upscale_by_fsr3() ? rd__fsr3_fp16_word() : "(not FSR 3.1)");
    s_held.frames = 0;
    memset(s_held.held, 0, sizeof(s_held.held));
    s_held.blockAt = now;
}
