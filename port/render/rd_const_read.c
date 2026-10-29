/* rd_const_read.c: a staticBlur sprite whose source target holds nothing
 * but the colour it was last cleared to is drawn with that colour instead
 * of reading the target (docs/port/WORK_BUFFERS.md, "A cleared source read
 * as its colour"; [dev] constant_reads, ICO_RD_CONSTANT_READS).
 *
 * The aura (staticBlur.c auraInspireBefore) clears AURA_WORK, the frame's
 * size at the scene's scale, and the frame's aura objects are drawn into
 * it in list 8; auraInspireAfter's first sprite then reads the whole of it
 * into WORK0 (reduceCopyAlphaChannelOfWork1ToWork0).  With no aura object
 * on screen nothing draws into AURA_WORK between the two, and the sprite
 * reads a 2.5 Mpix clear at the phone's 2366 x 1080 to compute one colour.
 *
 * Why the bytes are the same (fx_sprite.hlsl fx_sprite_ps, common.hlsli):
 * every texel of a target a pass cleared (the load op over its whole
 * render area, or the pass flushClear records) holds the clear's bytes C,
 * so every tap the sprite's kernel loads is C (fx_load: floor(C / 255 *
 * 255 + 0.5) = C; the UINT view: C), whatever the wrap and the clamp.  TEXA
 * leaves an RGBA32 texel as it is (gs_texa_expand; other formats are not
 * taken).  Each kernel returns C for taps that are all C:
 *   nearest       the one tap
 *   GS bilinear   gs_bilerp: C + (((C - C) f) >> 4) = C
 *   above 1x      per axis the weights sum to the divisor D (the box's
 *                 overlaps of [lo, lo + S) tile it: S = D; the B-spline's
 *                 four sum to 24576; the GS axis 16 - f + f = 16 or 1), so
 *                 R = C Dx, R' = (16 C Dx + Dx / 2) / Dx = 16 C (Dx / 2 <
 *                 Dx), and t = (16 C Dy + 8 Dy) / (16 Dy) = C
 * fx_tfx(C, RGBAQ) is then computed here (rd__const_tfx, the shader's
 * integer arithmetic), and the sprite drawn untextured with that RGBAQ:
 * fx_sprite_ps's untextured colour is RGBAQ itself, and everything after
 * the texture function (the alpha test, DATE, the blend, FBA) reads only
 * that colour.  The forms an untextured sprite can take after this (the
 * clear of blurSpriteClears, the fx plan's constant fold) are exact for an
 * untextured sprite of that colour.
 *
 * What makes the colour known: rd_replay.c pendClear notes the clear of an
 * RGBA8_UNORM target (rd__const_note_clear), with the count of actions
 * walked so far in this replay (rd__const_cmd, from the list loop's
 * rd__replay_fold_at: every command but the state deltas, the NOPs and the
 * deferred text).  The note holds while the next action is the reading
 * sprite itself: any action between (a draw of any kind, a copy, an exact
 * blend, a shadow resolve, a blur pair a fold drew) may write the target
 * and ends it, whatever target it names.  SCENE is never taken (the MSAA
 * epoch moves its clear to SCENE_MS and resolves it back), nor a target
 * whose colour texture changed since (blurSwapTarget), nor a sprite
 * sampling its own target.  A sprite drawn into the noted target ends the
 * note.
 *
 * Counters (RdPerfRecord.constReads, constKept): the sprites drawn this
 * way, and those reading another target that were not (the source drawn
 * since its clear, or not cleared in this replay).  [dev] constant_reads =
 * false reads every source as before (the A/B). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "rd_replay.h"

/* the actions walked in replay `replay` (rd__const_cmd) */
static uint32_t s_actions, s_actionsReplay;

/* the last clear noted: target id, its colour texture, the bytes, and
 * s_actions when it was noted (its own command counted) */
static struct {
    bool valid;
    uint32_t replay, actions;
    uint32_t id, tex;
    uint8_t rgba[4];
} s_held;

bool rd__const_reads_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("ICO_RD_CONSTANT_READS");
        on = !(e && e[0] == '0');
    }
    return on != 0;
}

void rd__const_cmd(const RdCmd *c)
{
    if (s_actionsReplay != g_rd.replayCounter) {
        s_actionsReplay = g_rd.replayCounter;
        s_actions = 0;
    }
    if (!rd__cmd_is_state(c->type) && c->type != RDC_OVERLAY_TEXT) {
        s_actions++;
    }
}

void rd__const_note_clear(uint32_t id, const RdTargetRec *t, const float col[4])
{
    s_held.valid = false;
    if (!t || t->format != RHI_FMT_RGBA8_UNORM || !t->color.id ||
        id == rd_target(RD_TARGET_SCENE).id || s_actionsReplay != g_rd.replayCounter) {
        return;
    }
    for (int k = 0; k < 4; k++) {
        const float v = floorf(col[k] * 255.0f + 0.5f);
        s_held.rgba[k] = (uint8_t)(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
    }
    s_held.valid = true;
    s_held.replay = g_rd.replayCounter;
    s_held.actions = s_actions;
    s_held.id = id;
    s_held.tex = t->color.id;
}

/* fx_sprite.hlsl fx_tfx of texel t and RGBAQ col under the sprite's flags
 * (TFX in bits 1..2, TCC), into out */
static void constTfx(const uint8_t t[4], const uint8_t col[4], uint32_t flags, uint8_t out[4])
{
    const uint32_t tfx = (flags >> RD_FXF_TFX_SHIFT) & 3u;
    const bool tcc = (flags & RD_FXF_TCC) != 0u;
    uint32_t o[4];
    if (tfx == 1u) {
        for (int k = 0; k < 3; k++) {
            o[k] = t[k];
        }
        o[3] = tcc ? t[3] : col[3];
    } else {
        for (int k = 0; k < 3; k++) {
            uint32_t v = ((uint32_t)t[k] * col[k]) >> 7;
            v = v > 255u ? 255u : v;
            if (tfx >= 2u) {
                v += col[3];
                v = v > 255u ? 255u : v;
            }
            o[k] = v;
        }
        if (!tcc) {
            o[3] = col[3];
        } else if (tfx == 0u) {
            const uint32_t v = ((uint32_t)t[3] * col[3]) >> 7;
            o[3] = v > 255u ? 255u : v;
        } else if (tfx == 2u) {
            const uint32_t v = (uint32_t)t[3] + col[3];
            o[3] = v > 255u ? 255u : v;
        } else {
            o[3] = t[3];
        }
    }
    for (int k = 0; k < 4; k++) {
        out[k] = (uint8_t)o[k];
    }
}

bool rd__const_read(const RdTargetRec *tc, uint32_t tcId, const RdTargetRec *src, uint32_t srcId,
                    const RdDrawState *d, RdPostRec *p, uint32_t tfmt)
{
    const bool held = s_held.valid && s_held.replay == g_rd.replayCounter &&
                      s_actionsReplay == g_rd.replayCounter && s_held.actions + 1u == s_actions &&
                      s_held.id == srcId && src && s_held.tex == src->color.id;
    const bool take = rd__const_reads_on() && held && src != tc && tfmt == RD_TEXSRC_RGBA32;
    if (s_held.valid && s_held.id == tcId) {
        s_held.valid = false; /* the sprite draws into the noted target */
    }
    if (!take) {
        if (src && src != tc) {
            g_rdPerf.constKept++;
        }
        return false;
    }
    const uint32_t flags = rd__replay_blur_flags(d, p->lines, 1, 0);
    uint8_t col[4];
    constTfx(s_held.rgba, p->rgba, flags, col);
    static bool logged;
    if (!logged) {
        logged = true;
        rd__log("constant reads: a sprite into %s read %s, which held only its clear "
                "(%u,%u,%u,%u): drawn with its colour (%u,%u,%u,%u), the source not read "
                "(reported once; [dev] constant_reads = false reads it)",
                rd__target_name(tcId), rd__target_name(srcId), s_held.rgba[0], s_held.rgba[1],
                s_held.rgba[2], s_held.rgba[3], col[0], col[1], col[2], col[3]);
    }
    memcpy(p->rgba, col, sizeof(col));
    g_rdPerf.constReads++;
    return true;
}
