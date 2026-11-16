/*
 * port/render/rd_readback.c
 *
 * The readbacks (moved verbatim from rd_replay.c's "readback" section and
 * rd__read_present, v0.11.8, for the file cap, tools/check_no_rom.sh): a
 * texture, a named target's colour or depth, the presented output read
 * back to the CPU for the dumps, screenshots and tests (rd_internal.h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_present_scene.h"

/* bpp: the format's bytes a texel (1 for the R8 coverage textures) */
static bool readTextureBpp(RhiTexture t, RhiState *state, uint32_t w, uint32_t h, uint32_t bpp,
                           void *dst, size_t dstSize)
{
    if (!t.id || dstSize < (size_t)w * h * bpp) {
        return false;
    }
    if (*state != RHI_STATE_COPY_SRC) {
        RhiCommandList cl = rhi_begin_commands();
        if (!cl.id) {
            return false;
        }
        rd__transition(cl, t, state, RHI_STATE_COPY_SRC);
        rhi_end_commands(cl);
        rhi_submit(cl);
    }
    uint32_t pitch = 0;
    const double t0 = rd__now_ms();
    const bool ok = rhi_readback_texture(t, RHI_ASPECT_COLOR, dst, dstSize, &pitch);
    rd__perf_readback_ms(rd__now_ms() - t0); /* dumps and screenshots only */
    if (!ok) {
        return false;
    }
    if (pitch != w * bpp) {
        uint8_t *p = dst;
        for (uint32_t y = 1; y < h; y++) {
            memmove(p + (size_t)y * w * bpp, p + (size_t)y * pitch, (size_t)w * bpp);
        }
    }
    return true;
}

static bool readTexture(RhiTexture t, RhiState *state, uint32_t w, uint32_t h, void *dst,
                        size_t dstSize)
{
    return readTextureBpp(t, state, w, h, 4, dst, dstSize);
}

bool rd__read_rhi_texture(RhiTexture t, RhiState *state, uint32_t w, uint32_t h, void *dst,
                          size_t dstSize)
{
    return readTexture(t, state, w, h, dst, dstSize);
}

bool rd__read_texture(RdTex tex, void *dst, size_t dstSize, uint32_t *w, uint32_t *h)
{
    RdTexRec *t = rd__tex_rec(tex.id);
    if (!g_rd.hasDevice || !t || t->kind != RD_TEXKIND_IMAGE || !t->rhi.id ||
        t->state == RHI_STATE_UNDEFINED || rd__texel_is_block(t->format) || t->sheetBaked) {
        return false; /* no texels to read back: a BC replacement, a baked sheet */
    }
    if (w) {
        *w = t->w;
    }
    if (h) {
        *h = t->h;
    }
    const bool ok =
        readTextureBpp(t->rhi, &t->state, t->w, t->h, rd__texel_bytes(t->format), dst, dstSize);
    /* draws sample image textures only in SHADER_READ (resolveTexture) */
    if (t->state != RHI_STATE_SHADER_READ) {
        RhiCommandList cl = rhi_begin_commands();
        if (cl.id) {
            rd__transition(cl, t->rhi, &t->state, RHI_STATE_SHADER_READ);
            rhi_end_commands(cl);
            rhi_submit(cl);
        }
    }
    return ok;
}

bool rd__read_target(RdTarget target, void *dst, size_t dstSize, uint32_t *w, uint32_t *h)
{
    RdTargetRec *t = rd__target_rec(target.id);
    if (!g_rd.hasDevice || !t || t->format == RHI_FMT_R8_UNORM) {
        return false;
    }
    if (w) {
        *w = t->tw;
    }
    if (h) {
        *h = t->th;
    }
    rd__clear_skip_readback(target.id); /* a clear not drawn, drawn first (rd_pass_cut.c) */
    rd__glow_fold_readback(target.id);  /* WORK2 a fold left undrawn (rd_glow_fold.c) */
    rd__present_scene_readback(target.id);
    return readTexture(t->color, &t->colorState, t->tw, t->th, dst, dstSize);
}

/* A target's depth buffer as floats (tests). */
bool rd__read_target_depth(RdTarget target, float *dst, size_t dstSize, uint32_t *w, uint32_t *h)
{
    RdTargetRec *t = rd__target_rec(target.id);
    if (!g_rd.hasDevice || !t || !t->withDepth || !t->depth.id ||
        dstSize < (size_t)t->tw * t->th * sizeof(float)) {
        return false;
    }
    rd__store_plan_readback(target.id); /* read after its replays from now on */
    if (w) {
        *w = t->tw;
    }
    if (h) {
        *h = t->th;
    }
    if (t->depthState != RHI_STATE_COPY_SRC) {
        RhiCommandList cl = rhi_begin_commands();
        if (!cl.id) {
            return false;
        }
        rd__transition(cl, t->depth, &t->depthState, RHI_STATE_COPY_SRC);
        rhi_end_commands(cl);
        rhi_submit(cl);
    }
    uint32_t pitch = 0;
    if (!rhi_readback_texture(t->depth, RHI_ASPECT_DEPTH, dst, dstSize, &pitch)) {
        return false;
    }
    return pitch == t->tw * sizeof(float);
}

bool rd__read_present(void *dst, size_t dstSize, uint32_t *w, uint32_t *h)
{
    if (!g_rd.hasDevice || !g_rd.presentOut.id) {
        return false;
    }
    if (w) {
        *w = g_rd.presentOutW;
    }
    if (h) {
        *h = g_rd.presentOutH;
    }
    return readTexture(g_rd.presentOut, &g_rd.presentOutState, g_rd.presentOutW, g_rd.presentOutH,
                       dst, dstSize);
}
