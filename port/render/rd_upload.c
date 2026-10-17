/* rd_upload.c: what a replay uploads before its first pass, and the upload
 * ring it takes it from.
 *
 * The ring: one upload buffer a frame slot (g_rd.ring), sized for the frame
 * before its list is begun (rd__estimate_ring, rd__texture_ring_bytes) and
 * grown, never shrunk, by rd__ensure_ring; rd_replay.c rd__ring_alloc hands
 * out its bytes.  The uploads: dirty game textures and their levels, texture
 * pack replacements (rd__upload_dirty_textures), the static meshes into the
 * device arena (rd__upload_meshes), and the first clear of a target with a
 * new texture (rd__clear_new_targets).  Everything is recorded into the
 * replay's command list (rd__replay_cl) on the ring of its frame slot
 * (rd__ring_slot).
 */
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_mesh.h"
#include "rd_tex.h"
#include "shader_consts.h"
#include "texpack.h"

/* The levels a game texture gets: the full chain with power-of-two sides
 * under the Enhanced filter or in the Original preset (where only the draws
 * whose TEX1 mipmaps reach past level 0: rd_replay.c drawGsLevels), else 1. */
static uint8_t texLevels(const RdTexRec *t)
{
    if (t->replacement) {
        return t->mipLevels; /* texture packs: the image's own levels, whatever the filter */
    }
    if (t->gsLevels > 1 && t->gsChain) {
        return t->gsLevels; /* the TIM2's own levels, whatever the filter */
    }
    if ((!g_rd.filterUpgrade && g_rd.settings.preset != RD_PRESET_ORIGINAL) ||
        rd__texel_is_coverage(t->format) || t->w < 2 || t->h < 2 || (t->w & (t->w - 1)) ||
        (t->h & (t->h - 1))) {
        return 1;
    }
    uint8_t n = 1;
    for (uint32_t m = t->w > t->h ? t->w : t->h; m > 1; m >>= 1) {
        n++;
    }
    return n;
}

/* Texture packs: a replacement's levels as rd__upload_dirty_textures lays them out,
 * each row of texels (or of 4x4 blocks) padded to copyRowPitchAlign, each
 * level at an offset aligned for the backend and the block.  Replacements
 * of more than RD_REPLACEMENT_RING_MAX bytes bypass the ring (a 4096 x
 * 4096 RGBA8 image with its chain is 85 MB; the ring of every frame slot
 * would keep that size) through a buffer of their own. */
#define RD_REPLACEMENT_RING_MAX (4ull << 20)

static uint64_t replacementOffsetAlign(const RdTexRec *t)
{
    const uint64_t oa = rhi_limits()->copyOffsetAlign ? rhi_limits()->copyOffsetAlign : 1u;
    const uint64_t bb = rd__texel_block_bytes(t->format);
    return oa > bb ? oa : bb; /* both powers of two: the larger is a multiple of the other */
}

static uint64_t replacementLevelPitch(const RdTexRec *t, uint32_t w)
{
    const uint64_t pa = rhi_limits()->copyRowPitchAlign ? rhi_limits()->copyRowPitchAlign : 1u;
    const uint32_t bw = rd__texel_block_w(t->format);
    const uint64_t row = (uint64_t)((w + bw - 1) / bw) * rd__texel_block_bytes(t->format);
    return (row + pa - 1) / pa * pa;
}

static uint64_t replacementBytes(const RdTexRec *t)
{
    const TexpackImage *p = t->pending;
    const uint64_t oa = replacementOffsetAlign(t);
    const uint32_t bw = rd__texel_block_w(t->format);
    uint64_t total = 0;
    for (uint32_t l = 0; p && l < p->levels; l++) {
        total += replacementLevelPitch(t, p->lv[l].w) * ((p->lv[l].h + bw - 1) / bw) + oa;
    }
    return total;
}

uint64_t rd__estimate_ring(const RdFrame *f, int keep)
{
    const uint64_t align = rhi_limits()->uniformAlign;
    const uint64_t pitchA = rhi_limits()->copyRowPitchAlign;
    uint64_t total = 64 * 1024; /* the vertex pre-pass's DrawCB among it */
    /* with PRIM.AA1 anywhere in the frame, room for every
     * screen command's AA1 geometry too (at most 7 vertices per expanded
     * vertex, 21 per vertex of a triangle strip) */
    int hasAa1 = f->startState.aa1 != 0;
    for (int l = rd__first_list(keep); l < RD_LIST_COUNT && !hasAa1; l++) {
        for (uint32_t i = 0; i < f->lists[l].count && !hasAa1; i++) {
            hasAa1 = f->lists[l].cmds[i].type == RDC_AA1;
        }
    }
    for (int l = rd__first_list(keep); l < RD_LIST_COUNT; l++) {
        const RdCmdList *cl = &f->lists[l];
        for (uint32_t i = 0; i < cl->count; i++) {
            const RdCmd *c = &cl->cmds[i];
            total += 4 * align;
            if (c->type == RDC_SCREEN) {
                total += (uint64_t)c->u[1] * 6 * sizeof(IcoSpriteVertex) + 16;
                /* a triangle command's planes: at most 3 vertices a vertex */
                total += (uint64_t)c->u[1] * 3 * sizeof(IcoSpritePlaneVertex) + 16;
                if (hasAa1) {
                    total += (uint64_t)c->u[1] * 21 * sizeof(IcoSpriteAa1Vertex) + 16;
                }
            } else if (c->type == RDC_SHADOW_STRIP) {
                /* the triangles, or a strip's 3 (n - 2) */
                total += ((uint64_t)c->u[0] * 3 + c->u[3]) * sizeof(IcoSpriteVertex) + 64;
            } else if (c->type == RDC_SHADOW_RESOLVE) {
                total += (RD_SHADOW_RESOLVE_PASSES + 1) * (sizeof(IcoDrawCB) + align);
            } else if (c->type == RDC_POST_STUB && c->b[0] == RD_POST_FOG) {
                /* the fog: the sprite, the LUT upload, two DrawCBs */
                total += 6 * sizeof(IcoSpriteVertex) + 16 + 256 * 4 + pitchA +
                         rhi_limits()->copyOffsetAlign + 2 * (sizeof(IcoDrawCB) + align);
            } else if (c->type >= RDC_MESH && c->type <= RDC_PARTICLES) {
                /* VU draws: per pass DrawCB + VuCB, the bones, the stream and
                 * the indices (a mesh drawn twice is counted twice) */
                RdVuPayload p;
                memcpy(&p, f->payload + c->u[1], sizeof(p));
                const uint64_t passes = 4 * (2 * (uint64_t)(p.batchCount ? p.batchCount : 1) + 1);
                total += passes * (sizeof(IcoDrawCB) + sizeof(IcoVuCB) + 2 * align);
                total += sizeof(IcoVuBoneCB) + align + (uint64_t)p.streamQw * 16 + align;
                /* the vertex pre-pass's VuCB (rd_replay_vu.c rd__vu_prepass) */
                total += sizeof(IcoVuCB) + align;
                total += (uint64_t)p.batchCount * (p.vertsPerBatch + 1) * 12 + 16;
                const RdMeshRec *m = c->type <= RDC_SKINNED ? rd__mesh_rec(c->u[0]) : NULL;
                if (m) {
                    total += (uint64_t)m->vertexCount * m->qwPerVertex * 16 +
                             (uint64_t)rd__mesh_mark_qw(m) * 16 + (uint64_t)m->indexCount * 4 +
                             2 * align;
                }
            }
        }
    }
    return total + rd__texture_ring_bytes();
}

uint64_t rd__texture_ring_bytes(void)
{
    const uint64_t pitchA = rhi_limits()->copyRowPitchAlign;
    uint64_t total = 0;
    const bool walk = g_rd.texDirtyCount != 0 || g_rd.texLevelsFilter != (int)g_rd.filterUpgrade;
    for (uint32_t i = 0; walk && i < RD_MAX_TEXTURES; i++) {
        const RdTexRec *t = &g_rd.textures[i];
        if (t->live && t->kind == RD_TEXKIND_IMAGE && t->replacement) {
            /* texture packs: only the small ones come through the ring */
            const uint64_t b = t->dirty && t->pending ? replacementBytes(t) : 0;
            total += b <= RD_REPLACEMENT_RING_MAX ? b : 0;
            continue;
        }
        if (t->live && t->kind == RD_TEXKIND_IMAGE && (t->dirty || t->mipLevels != texLevels(t))) {
            uint64_t pitch =
                ((uint64_t)t->w * rd__texel_bytes(t->format) + pitchA - 1) / pitchA * pitchA;
            uint64_t one = pitch * t->h + rhi_limits()->copyOffsetAlign;
            /* a mip chain adds at most the base again (with the
             * padding of every level) */
            total +=
                texLevels(t) > 1 ? 2 * one + 16 * (pitchA + rhi_limits()->copyOffsetAlign) : one;
        }
    }
    return total;
}

uint64_t g_rdRingFailOver; /* rd_internal.h: a test's refusal of larger rings */

static bool newRing(uint64_t cap)
{
    if (g_rdRingFailOver && cap > g_rdRingFailOver) {
        return false;
    }
    const RhiBuffer b = rhi_create_buffer(&(RhiBufferDesc){
        cap,
        RHI_BUF_VERTEX | RHI_BUF_INDEX | RHI_BUF_UNIFORM | RHI_BUF_STORAGE_READ | RHI_BUF_COPY_SRC,
        RHI_MEM_UPLOAD, "rd ring"});
    uint8_t *map = b.id ? rhi_map_buffer(b) : NULL;
    if (!map) {
        if (b.id) {
            rhi_destroy_buffer(b);
        }
        return false;
    }
    if (g_rd.ring[rd__ring_slot()].id) {
        rhi_destroy_buffer(
            g_rd.ring[rd__ring_slot()]); /* deferred: this slot's last use may be in flight */
    }
    g_rd.ring[rd__ring_slot()] = b;
    g_rd.ringMap[rd__ring_slot()] = map;
    g_rd.ringCap[rd__ring_slot()] = cap;
    return true;
}

/* The slot's ring of at least `need` bytes: a new one is made before the
   old one goes, so when it cannot be had (issue 87: a full PCIe BAR
   window) the old ring stays and the next frame that fits draws */
bool rd__ensure_ring(uint64_t need)
{
    if (g_rd.ringCap[rd__ring_slot()] >= need) {
        return true;
    }
    uint64_t cap = g_rd.ringCap[rd__ring_slot()] ? g_rd.ringCap[rd__ring_slot()] : 4u << 20;
    while (cap < need) {
        cap *= 2;
    }
    /* a frame that uploads many pack replacements at once (a
       stage change) can need a ring of hundreds of MB; the ring never
       shrinks, so its large sizes are named in the log */
    const uint64_t exact = (need + (1u << 20) - 1) & ~(uint64_t)((1u << 20) - 1);
    if (!newRing(cap) && !(exact < cap && newRing(exact))) {
        rd__log("the upload ring of frame slot %d could not grow to %llu MB (this frame needs "
                "%llu MB; it keeps %llu MB)",
                (int)rd__ring_slot(), (unsigned long long)(cap >> 20),
                (unsigned long long)(need >> 20),
                (unsigned long long)(g_rd.ringCap[rd__ring_slot()] >> 20));
        return false;
    }
    if (g_rd.ringCap[rd__ring_slot()] >= (64ull << 20)) {
        rd__log("the upload ring of frame slot %d grows to %llu MB (this frame needs %llu MB)",
                (int)rd__ring_slot(), (unsigned long long)(g_rd.ringCap[rd__ring_slot()] >> 20),
                (unsigned long long)(need >> 20));
    }
    return true;
}

/* Levels 1.. of a game texture: its GS levels when it has them
 * (RdTexRec.gsChain, the TIM2's own pictures), else 2x2 box filtered from
 * the base (rdtex_build_mip_chain; colour weighted by alpha when the alpha
 * byte is the draws' alpha), alpha coverage kept (rdtex_keep_alpha_coverage)
 * for the textures rd__mip_boost names, at their draws' reference, each level
 * copied into the ring and onto its subresource. */
static void uploadMips(RdTexRec *t, uint8_t levels)
{
    const uint32_t pitchA = rhi_limits()->copyRowPitchAlign;
    const uint32_t offA = rhi_limits()->copyOffsetAlign;
    uint8_t *chain = NULL;
    const uint8_t *src;
    uint32_t n;
    if (t->gsLevels > 1 && t->gsChain) {
        /* the TIM2's own levels (rdtex_store_levels), as the GS has them:
           no box filter, no alpha coverage kept */
        src = t->gsChain;
        n = t->gsLevels - 1u;
        t->mipBuiltBoost = 0;
        t->mipBuiltRef = 0;
    } else {
        chain = malloc(rdtex_mip_chain_bytes(t->w, t->h) + 4);
        if (!chain) {
            return;
        }
        n = rdtex_build_mip_chain(t->pixels, t->w, t->h, chain, t->src == RD_TEXSRC_RGBA32);
        uint8_t ref;
        t->mipBuiltBoost = (uint8_t)rd__mip_boost(t, &ref);
        t->mipBuiltRef = ref;
        if (t->mipBuiltBoost) {
            rdtex_keep_alpha_coverage(t->pixels, t->w, t->h, chain, n, ref);
        }
        src = chain;
    }
    uint32_t w = t->w, h = t->h;
    for (uint32_t l = 1; l < levels && l <= n; l++) {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        const uint32_t pitch = (w * 4 + pitchA - 1) / pitchA * pitchA;
        const uint64_t off = rd__ring_alloc((uint64_t)pitch * h, offA);
        if (off == ~0ull) {
            break;
        }
        for (uint32_t y = 0; y < h; y++) {
            memcpy(g_rd.ringMap[rd__ring_slot()] + off + (uint64_t)y * pitch,
                   src + (size_t)y * w * 4, (size_t)w * 4);
        }
        rhi_cmd_copy_buffer_to_texture(rd__replay_cl(), g_rd.ring[rd__ring_slot()], off, pitch,
                                       t->rhi, l, (RhiRect){0, 0, w, h});
        src += (size_t)w * h * 4;
    }
    free(chain);
}

/* Texture packs: a replacement's RHI texture and every level of its
 * pending image, through the ring when small, else through a buffer of
 * its own (destroyed right after recording: rhi_destroy_buffer waits for
 * the frames in flight).  The pending image is freed once the copies are
 * recorded.  Returns 0 when the upload must wait for the next replay (no
 * room), 1 when the texture is done with (uploaded, or dropped because the
 * device refused it: marked refused, it draws as an untextured placeholder
 * for the rest of this frame, and the next bind's rdtex_find misses and
 * decodes the game's own texture). */
static int uploadReplacement(RdTexRec *t)
{
    const TexpackImage *p = t->pending;
    if (!p || p->levels == 0) {
        return 1;
    }
    if (!t->rhi.id) {
        t->rhi = rhi_create_texture(
            &(RhiTextureDesc){t->w, t->h, p->levels, rd__texel_rhi_format(t->format),
                              RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, t->name, 0});
        t->state = RHI_STATE_UNDEFINED;
        t->mipLevels = (uint8_t)p->levels;
        if (!t->rhi.id) {
            rd__log("textures: the graphics card refused \"%s\" (%ux%u); the game's own "
                    "texture is shown instead",
                    t->name, t->w, t->h);
            t->refused = 1;
            rd__free_pending(t);
            return 1;
        }
    }
    const uint64_t bytes = replacementBytes(t);
    const uint64_t oa = replacementOffsetAlign(t);
    const uint32_t bw = rd__texel_block_w(t->format);
    RhiBuffer own = {0};
    uint8_t *map = g_rd.ringMap[rd__ring_slot()];
    RhiBuffer src = g_rd.ring[rd__ring_slot()];
    uint64_t next = 0;
    if (bytes > RD_REPLACEMENT_RING_MAX) {
        own = rhi_create_buffer(
            &(RhiBufferDesc){bytes, RHI_BUF_COPY_SRC, RHI_MEM_UPLOAD, "rd replacement upload"});
        map = own.id ? rhi_map_buffer(own) : NULL;
        if (!map) {
            if (own.id) {
                rhi_destroy_buffer(own);
            }
            /* the replacement waits for a later replay; named once */
            static int s_ownFailLogged;
            if (!s_ownFailLogged) {
                s_ownFailLogged = 1;
                rd__log("textures: no room for the %llu MB upload of \"%s\" (%ux%u); it waits "
                        "for a later frame (reported once)",
                        (unsigned long long)(bytes >> 20), t->name, t->w, t->h);
            }
            return 0;
        }
        src = own;
    }
    rd__transition(rd__replay_cl(), t->rhi, &t->state, RHI_STATE_COPY_DST);
    for (uint32_t l = 0; l < p->levels; l++) {
        const TexpackImageLevel *lv = &p->lv[l];
        const uint64_t pitch = replacementLevelPitch(t, lv->w);
        const uint32_t rows = (lv->h + bw - 1) / bw;
        const size_t rowBytes = (size_t)((lv->w + bw - 1) / bw) * rd__texel_block_bytes(t->format);
        uint64_t off;
        if (own.id) {
            off = (next + oa - 1) / oa * oa;
            next = off + pitch * rows;
        } else {
            off = rd__ring_alloc(pitch * rows, oa);
            if (off == ~0ull) {
                /* rd__estimate_ring counted it: only a level the image does not
                   describe can get here; the texture stays as far as it got */
                break;
            }
        }
        for (uint32_t y = 0; y < rows; y++) {
            memcpy(map + off + (uint64_t)y * pitch, lv->data + (size_t)y * lv->pitch, rowBytes);
        }
        rhi_cmd_copy_buffer_to_texture(rd__replay_cl(), src, off, (uint32_t)pitch, t->rhi, l,
                                       (RhiRect){0, 0, lv->w, lv->h});
    }
    rd__transition(rd__replay_cl(), t->rhi, &t->state, RHI_STATE_SHADER_READ);
    if (own.id) {
        rhi_destroy_buffer(own);
    }
    rd__free_pending(t);
    return 1;
}

void rd__upload_dirty_textures(void)
{
    const uint32_t pitchA = rhi_limits()->copyRowPitchAlign;
    const uint32_t offA = rhi_limits()->copyOffsetAlign;
    /* a full ring gives ~0, whose write would land past the map: the
     * next replay uploads it */
    const uint64_t dummyOff =
        g_rd.dummyState != RHI_STATE_SHADER_READ ? rd__ring_alloc(4, offA) : ~0ull;
    if (dummyOff != ~0ull) {
        static const uint8_t white[4] = {255, 255, 255, 255};
        memcpy(g_rd.ringMap[rd__ring_slot()] + dummyOff, white, 4);
        rd__transition(rd__replay_cl(), g_rd.dummy, &g_rd.dummyState, RHI_STATE_COPY_DST);
        rhi_cmd_copy_buffer_to_texture(rd__replay_cl(), g_rd.ring[rd__ring_slot()], dummyOff,
                                       pitchA > 4 ? pitchA : 4, g_rd.dummy, 0,
                                       (RhiRect){0, 0, 1, 1});
        rd__transition(rd__replay_cl(), g_rd.dummy, &g_rd.dummyState, RHI_STATE_SHADER_READ);
    }
    /* the table is walked only while a texture is dirty or the filter
     * option changed since the last walk */
    if (g_rd.texDirtyCount == 0 && g_rd.texLevelsFilter == (int)g_rd.filterUpgrade) {
        return;
    }
    g_rd.texLevelsFilter = (int)g_rd.filterUpgrade;
    uint32_t stillDirty = 0;
    for (uint32_t i = 0; i < RD_MAX_TEXTURES; i++) {
        RdTexRec *t = &g_rd.textures[i];
        if (!t->live || t->kind != RD_TEXKIND_IMAGE) {
            continue;
        }
        if (t->replacement) {
            /* texture packs: uploaded once, from the pending image */
            stillDirty += t->dirty;
            if (t->dirty && uploadReplacement(t)) {
                t->dirty = 0;
                stillDirty--;
                g_rd.stats.textureUploads++;
                g_rdPerf.textureUploads++;
            }
            continue;
        }
        const uint8_t levels = texLevels(t);
        if (t->rhi.id && t->mipLevels != levels) {
            /* the filter option changed: a new texture with the new
             * level count (the old one is destroyed after the frames in
             * flight, rhi_destroy_texture) */
            rhi_destroy_texture(t->rhi);
            t->rhi = (RhiTexture){0};
            t->dirty = 1;
        }
        stillDirty += t->dirty; /* less the ones uploaded below */
        if (!t->dirty) {
            continue;
        }
        /* the format's bytes per texel; the changed rectangle only, unless
         * the RHI texture is new or has a mip chain to rebuild */
        const uint32_t bpp = rd__texel_bytes(t->format);
        int whole = levels > 1;
        if (!t->rhi.id) {
            t->rhi = rhi_create_texture(
                &(RhiTextureDesc){t->w, t->h, levels, rd__texel_rhi_format(t->format),
                                  RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "rd texture", 0});
            t->state = RHI_STATE_UNDEFINED;
            t->mipLevels = levels;
            if (!t->rhi.id) {
                rdtex_text_upload_failed(t->name, t->w, t->h, t->format,
                                         "the graphics card refused the texture");
                continue;
            }
            whole = 1;
        }
        uint32_t rx = 0, ry = 0, rw = t->w, rh = t->h;
        if (!whole && t->dirtyX1 > t->dirtyX0 && t->dirtyY1 > t->dirtyY0 && t->dirtyX1 <= t->w &&
            t->dirtyY1 <= t->h) {
            rx = t->dirtyX0;
            ry = t->dirtyY0;
            rw = t->dirtyX1 - t->dirtyX0;
            rh = t->dirtyY1 - t->dirtyY0;
        }
        const uint32_t pitch = (rw * bpp + pitchA - 1) / pitchA * pitchA;
        uint64_t off = rd__ring_alloc((uint64_t)pitch * rh, offA);
        if (off == ~0ull) {
            rdtex_text_upload_failed(t->name, t->w, t->h, t->format, "no room in the upload ring");
            continue;
        }
        for (uint32_t y = 0; y < rh; y++) {
            memcpy(g_rd.ringMap[rd__ring_slot()] + off + (uint64_t)y * pitch,
                   t->pixels + ((size_t)(ry + y) * t->w + rx) * bpp, (size_t)rw * bpp);
        }
        rd__transition(rd__replay_cl(), t->rhi, &t->state, RHI_STATE_COPY_DST);
        rhi_cmd_copy_buffer_to_texture(rd__replay_cl(), g_rd.ring[rd__ring_slot()], off, pitch,
                                       t->rhi, 0, (RhiRect){(int32_t)rx, (int32_t)ry, rw, rh});
        if (levels > 1) {
            uploadMips(t, levels);
        }
        rd__transition(rd__replay_cl(), t->rhi, &t->state, RHI_STATE_SHADER_READ);
        t->dirty = 0;
        t->dirtyX0 = t->dirtyY0 = t->dirtyX1 = t->dirtyY1 = 0;
        stillDirty--;
        g_rd.stats.textureUploads++;
        g_rdPerf.textureUploads++;
        /* a texture uploaded on 60 replays in a row is named once */
        t->uploadStreak = t->lastUpload + 2 >= g_rd.replayCounter ? t->uploadStreak + 1 : 0;
        t->lastUpload = g_rd.replayCounter;
        if (t->uploadStreak == 60 && !t->streakLogged) {
            t->streakLogged = 1;
            rd__log("texture \"%s\" (%ux%u) uploaded on 60 replays in a row: its content "
                    "changes every frame",
                    t->name, t->w, t->h);
        }
    }
    g_rd.texDirtyCount = stillDirty;
}

/* The static meshes a frame draws live in the device arena
 * (rd_mesh.c): uploaded the first time a replay draws them and again only
 * after rd_update_vu_mesh rewrote them (gpuDirty, the morphs), through the
 * ring and one buffer copy each, all before the first pass.  The
 * interpolation's scratch meshes (transient) and a mesh the arena has no
 * room for stay on the ring path in rd_replay_vu.c doVuDraw. */
void rd__upload_meshes(const RdFrame *f, int keep)
{
    const uint64_t ua = rhi_limits()->uniformAlign;
    for (int l = rd__first_list(keep); l < RD_LIST_COUNT; l++) {
        const RdCmdList *cl = &f->lists[l];
        for (uint32_t i = 0; i < cl->count; i++) {
            const RdCmd *c = &cl->cmds[i];
            if (c->type != RDC_MESH && c->type != RDC_SKINNED) {
                continue;
            }
            RdMeshRec *m = rd__mesh_rec(c->u[0]);
            if (!m || !m->vu || m->transient || (m->gpuChunk && !m->gpuDirty) ||
                m->replaySeen == g_rd.replayCounter) {
                continue;
            }
            m->replaySeen = g_rd.replayCounter;
            /* the stream, then its mark bytes (vu_common.hlsli
             * vu_kick_marks), then the indices */
            const uint64_t vb = (uint64_t)m->vertexCount * m->qwPerVertex * 16;
            const uint64_t sb = vb + (uint64_t)rd__mesh_mark_qw(m) * 16;
            const uint64_t ib = (uint64_t)m->indexCount * 4;
            if (!rd__mesh_gpu_reserve(m, sb, ib, ua)) {
                m->replaySeen = 0; /* doVuDraw's ring path */
                continue;
            }
            /* stream and indices in the ring as they lie in the arena */
            const uint64_t span = m->gpuIndexOff - m->gpuOff + (ib ? ib : 4);
            const uint64_t off = rd__ring_alloc(span, 16);
            if (off == ~0ull) {
                /* the range is reserved but not written: doVuDraw must not draw
                 * from it, and the next replay uploads it */
                m->replaySeen = 0;
                m->gpuDirty = 1;
                continue;
            }
            if (vb) {
                memcpy(g_rd.ringMap[rd__ring_slot()] + off, m->stream, vb);
            }
            rd__mesh_write_marks(m, g_rd.ringMap[rd__ring_slot()] + off + vb);
            if (ib) {
                memcpy(g_rd.ringMap[rd__ring_slot()] + off + (m->gpuIndexOff - m->gpuOff), m->index,
                       ib);
            }
            rhi_cmd_copy_buffer(rd__replay_cl(), g_rd.ring[rd__ring_slot()], off,
                                rd__mesh_gpu_buffer(m->gpuChunk), m->gpuOff, span);
            m->gpuDirty = 0;
            g_rdPerf.meshUploads++;
            g_rdPerf.meshUploadBytes += sb + ib;
        }
    }
}

/* A target with a new texture starts cleared: zero colour and stencil, and
 * depth 1.0, above every GS Z (gs_z_to_depth: the depth grows with Z).  That
 * is every named target after rd_core.c createNamedTargets (a start, or a
 * display change), and a temporary target that took a pooled texture
 * (rd__temp_target_alloc) or a new one.  Every pending target is cleared,
 * whichever frame it belongs to: none of them was drawn since it was taken.
 * Without the named ones a start's first frames, which only keep and fade
 * the frame buffer, show what the driver's memory held before (the last
 * run's picture). */
void rd__clear_new_targets(void)
{
    for (int i = 0; i < RD_MAX_TARGETS; i++) {
        RdTargetRec *t = &g_rd.targets[i];
        if (!t->live || !t->clearPending || !t->color.id) {
            continue;
        }
        t->clearPending = 0;
        /* in the state the target's passes take (rd_replay.c beginPass): no transition
         * back and forth between this pass and the next */
        const bool readable = rd__pass_wants_readable(t);
        rd__transition(rd__replay_cl(), t->color, &t->colorState,
                       readable ? RHI_STATE_RENDER_TARGET_READ : RHI_STATE_RENDER_TARGET);
        RhiRenderPassDesc p;
        memset(&p, 0, sizeof(p));
        p.color[0].texture = t->color;
        p.color[0].load = RHI_LOAD_CLEAR;
        p.color[0].readable = readable;
        p.colorCount = 1;
        if (t->withDepth && t->depth.id) {
            rd__transition(rd__replay_cl(), t->depth, &t->depthState, RHI_STATE_DEPTH_WRITE);
            p.depth.texture = t->depth;
            p.depth.depthLoad = RHI_LOAD_CLEAR;
            p.depth.clearDepth = 1.0f; /* above every GS Z (gs_z_to_depth) */
            /* the stencil cleared, and stored only inside a shadow window
             * (none is open before the walk) */
            rd__stencil_ops_packed(rd__replay_stencil(), t->depth.id, true, RHI_LOAD_CLEAR,
                                   &p.depth.stencilLoad, &p.depth.stencilStore);
        }
        p.width = t->tw;
        p.height = t->th;
        rhi_cmd_begin_render_pass(rd__replay_cl(), &p);
        rhi_cmd_end_render_pass(rd__replay_cl());
        if (i >= RD_TARGET_COUNT) {
            g_rdPerf.tempClears++;
        }
    }
}
