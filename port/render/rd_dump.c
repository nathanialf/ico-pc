/* rd_dump.c: an RdFrame to a file and back.
 *
 * Dumps hold game assets (texture pixels) and are local only: never commit
 * one.  The format is the in-memory frame, little-endian, versioned by
 * RD_DUMP_VERSION; RdCmd, RdStateBlock and RdScreenVtx have fixed-width
 * fields and no implicit padding (static asserts in rd_internal.h), so the
 * 32-bit and 64-bit builds read each other's dumps.
 *
 *   char[8]  "ICORDMP\0"
 *   u32      version, sizeof(RdCmd), sizeof(RdStateBlock), sizeof(RdScreenVtx)
 *   u32      gsW, gsH, number, keep, hasCamera
 *   RdCamera camera (raw)
 *   RdStateBlock startState, endState (version 3: without the trailing aa1,
 *            80 bytes; rd__LoadFrame reads it with AA1 off)
 *   13 x     u32 count, RdCmd[count]
 *   u32      payload size, payload bytes
 *   u32      texture count; per texture: u32 id, kind, src, bakedTexa, w, h,
 *            target, view; then the texels for images.  An image has no
 *            view: its view word holds the texel format (RD_TEXEL_*,
 *            package R8; 0 RGBA8 in every older dump), and its texels are
 *            w*h*4 RGBA8 bytes or w*h R8 bytes.  Version 7 (v0.4.2,
 *            package F-A): a sheet image (RD_TEXEL_SHEET, w*h coverage
 *            bytes) has its style above the format byte: bit 8 rimOn, bit
 *            9 dither, bits 10..15 the rim's weight in 64ths (0 full: the
 *            first version 7 dumps have none), bits 16..23 rimLevel,
 *            24..31 fillLevel (sheetView); v0.4.2 F-G: a sheet's
 *            bakedTexa word holds its scale less one in bits 8..15 (0, a
 *            1x sheet, in every older dump: sheetTexa)
 *   u32      temp target count; per target: u32 id, w, h, withDepth, keep
 *   (version 5, package DEF: RDC_OVERLAY_TEXT commands, their RdTextItem and
 *   RdTextOp payloads, and RDC_SCREEN's b[3]; no new section.  A version 4
 *   dump has none, so it replays as before in every preset)
 *   (version 6: RD_TARGET_FEED_HELD appended, so the temporary targets'
 *   slots start one later; an older dump's temps start at low half 17)
 *   (version 8, v0.4.3: RD_TARGET_DISPLAY_HELD appended, the same again; a
 *   version 6 or 7 dump's temps start at low half 18)
 *   u32      VU mesh count (version 3, wave 3); per mesh: u32 id, vertexCount,
 *            qwPerVertex, indexCount, batchCount, char[24] name, then the
 *            stream (vertexCount * qwPerVertex * 16 bytes), the index list
 *            (u32 each) and the batch records (RdVuBatchRec, raw)
 *
 * Only the textures and temporary targets the frame references are written.
 * Target contents are not: a frame that reads a retained target (keep,
 * motion blur, FEED128) replays from whatever the loading context holds
 * (zero in a fresh rd_replay_tool).
 *
 * Loading creates the textures and temporary targets in the current context
 * and rewrites every id in the commands and state blocks to the new ones.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_fs.h"
#include "rd_internal.h"
#include "rd_mesh.h"
#include "shader_consts.h"

#define MAX_REFS 4096

typedef struct IdSet {
    uint32_t ids[MAX_REFS];
    uint32_t n;
} IdSet;

static void idAdd(IdSet *s, uint32_t id)
{
    if (!id) {
        return;
    }
    for (uint32_t i = 0; i < s->n; i++) {
        if (s->ids[i] == id) {
            return;
        }
    }
    if (s->n < MAX_REFS) {
        s->ids[s->n++] = id;
    }
}

/* a temporary target's handle has a slot past the fixed targets in its low
   half; count is the fixed targets' count the dump was written with */
static int isTempOf(uint32_t targetId, uint32_t count)
{
    return targetId && (targetId & 0xFFFF) > count;
}

static int isTemp(uint32_t targetId)
{
    return isTempOf(targetId, RD_TARGET_COUNT);
}

/* an image's view word: its texel format, and a sheet's style above it
   (version 7, v0.4.2 package F-A; the header comment's layout) */
static uint32_t sheetView(const RdTexRec *t)
{
    if (t->format != RD_TEXEL_SHEET) {
        return t->format;
    }
    return (uint32_t)RD_TEXEL_SHEET | (uint32_t)(t->sheet[0] != 0) << 8 |
           (uint32_t)(t->sheet[0] & 63u) << 10 | (uint32_t)(t->sheet[3] & 1u) << 9 |
           (uint32_t)t->sheet[1] << 16 | (uint32_t)t->sheet[2] << 24;
}

/* an image's bakedTexa word: a sheet's scale less one above the byte (F-G) */
static uint32_t sheetTexa(const RdTexRec *t)
{
    const uint32_t up = t->format == RD_TEXEL_SHEET && t->sheetScale > 1 ? t->sheetScale - 1u : 0u;
    return (uint32_t)t->bakedTexa | up << 8;
}

static void targetRef(IdSet *temps, uint32_t id)
{
    if (isTemp(id)) {
        idAdd(temps, id);
    }
}

_Static_assert(sizeof(RdVuBatchRec) == 28, "RdVuBatchRec is dumped as raw bytes");

static void collectRefs(const RdFrame *f, IdSet *texs, IdSet *temps, IdSet *meshes)
{
    const RdStateBlock *st[2] = {&f->startState, &f->endState};
    for (int i = 0; i < 2; i++) {
        idAdd(texs, st[i]->tex);
        targetRef(temps, st[i]->color);
        targetRef(temps, st[i]->depth);
    }
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < f->lists[l].count; i++) {
            const RdCmd *c = &f->lists[l].cmds[i];
            switch (c->type) {
            case RDC_TEXTURE:
                idAdd(texs, c->u[0]);
                break;
            case RDC_TARGET:
            case RDC_EXACT_BLEND:
            case RDC_COPY:
                targetRef(temps, c->u[0]);
                targetRef(temps, c->u[1]);
                break;
            case RDC_CLEAR:
                targetRef(temps, c->u[0]);
                break;
            case RDC_MESH:
            case RDC_SKINNED:
                idAdd(meshes, c->u[0]);
                break;
            default:
                break;
            }
        }
    }
    for (uint32_t i = 0; i < texs->n; i++) {
        RdTexRec *t = rd__TexRec(texs->ids[i]);
        if (t && t->kind == RD_TEXKIND_TARGET) {
            targetRef(temps, t->target);
        }
    }
}

static bool w32(FILE *fp, uint32_t v)
{
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    return fwrite(b, 1, 4, fp) == 4;
}

static bool wraw(FILE *fp, const void *p, size_t n)
{
    return n == 0 || fwrite(p, 1, n, fp) == n;
}

bool rd__DumpFrame(const RdFrame *f, const char *path)
{
    if (!f || !path) {
        return false;
    }
    static IdSet texs, temps, meshes;
    texs.n = temps.n = meshes.n = 0;
    collectRefs(f, &texs, &temps, &meshes);
    FILE *fp = ico_fopen(path, "wb");
    if (!fp) {
        rd__Log("dump: cannot open %s", path);
        return false;
    }
    bool ok = wraw(fp, RD_DUMP_MAGIC, 8) && w32(fp, RD_DUMP_VERSION) &&
              w32(fp, (uint32_t)sizeof(RdCmd)) && w32(fp, (uint32_t)sizeof(RdStateBlock)) &&
              w32(fp, (uint32_t)sizeof(RdScreenVtx)) && w32(fp, f->gsW) && w32(fp, f->gsH) &&
              w32(fp, f->number) && w32(fp, f->keep) && w32(fp, f->hasCamera) &&
              wraw(fp, &f->camera, sizeof(f->camera)) &&
              wraw(fp, &f->startState, sizeof(RdStateBlock)) &&
              wraw(fp, &f->endState, sizeof(RdStateBlock));
    for (int l = 0; ok && l < RD_LIST_COUNT; l++) {
        ok = w32(fp, f->lists[l].count) &&
             wraw(fp, f->lists[l].cmds, (size_t)f->lists[l].count * sizeof(RdCmd));
    }
    ok = ok && w32(fp, f->payloadSize) && wraw(fp, f->payload, f->payloadSize);
    uint32_t nt = 0;
    for (uint32_t i = 0; i < texs.n; i++) {
        nt += rd__TexRec(texs.ids[i]) != NULL;
    }
    ok = ok && w32(fp, nt);
    for (uint32_t i = 0; ok && i < texs.n; i++) {
        RdTexRec *t = rd__TexRec(texs.ids[i]);
        if (!t) {
            continue;
        }
        const int image = t->kind == RD_TEXKIND_IMAGE;
        if (image && t->replacement) {
            /* texture packs: a replacement keeps no CPU texels (and BC ones
               are no format the dump knows); it is written as a blank RGBA8
               image of the size its draws' UVs address, so the replay
               draws the same geometry without the pack's picture */
            const uint32_t w = t->uvW && t->uvH ? t->uvW : t->w;
            const uint32_t h = t->uvW && t->uvH ? t->uvH : t->h;
            static const uint8_t zero[4096];
            ok = w32(fp, texs.ids[i]) && w32(fp, t->kind) && w32(fp, t->src) &&
                 w32(fp, t->bakedTexa) && w32(fp, w) && w32(fp, h) && w32(fp, t->target) &&
                 w32(fp, RD_TEXEL_RGBA8);
            for (size_t left = (size_t)w * h * 4; ok && left;) {
                const size_t n = left < sizeof(zero) ? left : sizeof(zero);
                ok = wraw(fp, zero, n);
                left -= n;
            }
            continue;
        }
        ok = w32(fp, texs.ids[i]) && w32(fp, t->kind) && w32(fp, t->src) &&
             w32(fp, image ? sheetTexa(t) : t->bakedTexa) && w32(fp, t->w) && w32(fp, t->h) &&
             w32(fp, t->target) && w32(fp, image ? sheetView(t) : t->view);
        if (ok && image) {
            ok = wraw(fp, t->pixels, (size_t)t->w * t->h * rd__TexelBytes(t->format));
        }
    }
    uint32_t nr = 0;
    for (uint32_t i = 0; i < temps.n; i++) {
        nr += rd__TargetRec(temps.ids[i]) != NULL;
    }
    ok = ok && w32(fp, nr);
    for (uint32_t i = 0; ok && i < temps.n; i++) {
        RdTargetRec *t = rd__TargetRec(temps.ids[i]);
        if (t) {
            ok = w32(fp, temps.ids[i]) && w32(fp, t->w) && w32(fp, t->h) && w32(fp, t->withDepth) &&
                 w32(fp, t->keepAcross);
        }
    }
    uint32_t nm = 0;
    for (uint32_t i = 0; i < meshes.n; i++) {
        const RdMeshRec *m = rd__MeshRec(meshes.ids[i]);
        nm += m && m->vu;
    }
    ok = ok && w32(fp, nm);
    for (uint32_t i = 0; ok && i < meshes.n; i++) {
        const RdMeshRec *m = rd__MeshRec(meshes.ids[i]);
        if (!m || !m->vu) {
            continue;
        }
        ok = w32(fp, meshes.ids[i]) && w32(fp, m->vertexCount) && w32(fp, m->qwPerVertex) &&
             w32(fp, m->indexCount) && w32(fp, m->batchCount) && wraw(fp, m->name, 24) &&
             wraw(fp, m->stream, (size_t)m->vertexCount * m->qwPerVertex * 16) &&
             wraw(fp, m->index, (size_t)m->indexCount * 4) &&
             wraw(fp, m->batches, (size_t)m->batchCount * sizeof(RdVuBatchRec));
    }
    ok = fclose(fp) == 0 && ok;
    if (!ok) {
        rd__Log("dump: write to %s failed", path);
    }
    return ok;
}

/* ------------------------------------------------------------------ load */

static bool r32(FILE *fp, uint32_t *v)
{
    uint8_t b[4];
    if (fread(b, 1, 4, fp) != 4) {
        return false;
    }
    *v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return true;
}

static bool rraw(FILE *fp, void *p, size_t n)
{
    return n == 0 || fread(p, 1, n, fp) == n;
}

typedef struct IdMap {
    uint32_t from[MAX_REFS], to[MAX_REFS];
    uint32_t n;
    uint32_t fixedCount; /* the targets map: RD_TARGET_COUNT when the dump was written */
} IdMap;

static uint32_t mapId(const IdMap *m, uint32_t id)
{
    for (uint32_t i = 0; i < m->n; i++) {
        if (m->from[i] == id) {
            return m->to[i];
        }
    }
    return id;
}

static uint32_t mapTarget(const IdMap *m, uint32_t id)
{
    return isTempOf(id, m->fixedCount) ? mapId(m, id) : id;
}

static void remapState(RdStateBlock *s, const IdMap *tex, const IdMap *tgt)
{
    s->tex = s->tex ? mapId(tex, s->tex) : 0;
    s->color = mapTarget(tgt, s->color);
    s->depth = mapTarget(tgt, s->depth);
}

typedef struct TexHeader {
    uint32_t id, kind, src, bakedTexa, w, h, target, view;
} TexHeader;

/* size bytes at off lie inside a payload of psz bytes */
static bool payloadRange(uint32_t psz, uint32_t off, uint64_t size)
{
    return off <= psz && size <= (uint64_t)(psz - off);
}

/* Every payload range a loaded command names lies inside the payload, and
 * the parts of a VU payload inside its command's range, as rd_replay.c reads
 * them: a corrupt dump is refused instead of read past at replay. */
static bool cmdValid(const RdFrame *f, const RdCmd *c)
{
    const uint32_t psz = f->payloadSize;
    switch (c->type) {
    case RDC_SCREEN:
        return payloadRange(psz, c->u[0], (uint64_t)c->u[1] * sizeof(RdScreenVtx));
    case RDC_COPY:
        return payloadRange(psz, c->u[2], sizeof(RdCopyRec));
    case RDC_MESH:
    case RDC_SKINNED:
    case RDC_GRID:
    case RDC_PARTICLES: {
        const uint64_t head = sizeof(RdVuPayload) + sizeof(RdVuBlock);
        if (c->u[2] < head || !payloadRange(psz, c->u[1], c->u[2])) {
            return false;
        }
        RdVuPayload p;
        memcpy(&p, f->payload + c->u[1], sizeof(p));
        if (head + ((uint64_t)p.boneQw + p.streamQw) * 16 > c->u[2]) {
            return false;
        }
        if (c->type == RDC_GRID) {
            /* rd_DrawVuGrid: per strip the vertices and 4 header/trailer qwords */
            return p.vertsPerBatch >= 3 && p.qwPerVertex != 0 &&
                   (uint64_t)p.batchCount * ((uint64_t)p.vertsPerBatch * p.qwPerVertex + 4) <=
                       p.streamQw;
        }
        if (c->type == RDC_PARTICLES) {
            return 6 + 2 * (uint64_t)p.vertsPerBatch <= p.streamQw;
        }
        return true;
    }
    case RDC_SHADOW_STRIP:
        return c->b[0] == RD_SHADOW_TRIS
                   ? payloadRange(psz, c->u[1], ((uint64_t)c->u[0] + c->u[3]) * sizeof(RdScreenVtx))
                   : payloadRange(psz, c->u[1], (uint64_t)c->u[0] * 16);
    case RDC_POST_STUB: {
        if (!payloadRange(psz, c->u[1], sizeof(RdPostRec))) {
            return false;
        }
        RdPostRec r;
        memcpy(&r, f->payload + c->u[1], sizeof(r));
        /* the fog's LUT, or (R-POST) the reduction's four screen vertices */
        const uint64_t n =
            c->b[0] == RD_POST_REDUCTION ? 4 * (uint64_t)sizeof(RdScreenVtx) : 256 * 4;
        return r.lutOffset == ~0u || payloadRange(psz, r.lutOffset, n);
    }
    case RDC_WORLD_PRIMS:
        return payloadRange(psz, c->u[1], c->u[2]);
    case RDC_OVERLAY_TEXT: /* package DEF: an item or an op, as recorded */
        return payloadRange(psz, c->u[1], c->u[2]) &&
               ((c->b[0] == RD_OTEXT_ITEM && c->u[2] == sizeof(RdTextItem)) ||
                (c->b[0] == RD_OTEXT_OP && c->u[2] == sizeof(RdTextOp)));
    default:
        return c->type < RDC_COUNT;
    }
}

/* A dumped VU mesh as rd_CreateVuMesh builds them: every index a kick
 * (ICO_VU_INDEX: kick * 4 + corner 0..2) whose triangle k-2, k-1, k lies in
 * the stream, every batch inside the index list and the stream. */
static bool meshValid(uint32_t nv, uint32_t qpv, const uint32_t *ix, uint32_t ni,
                      const RdVuBatchRec *br, uint32_t nb)
{
    if (nv && qpv == 0) {
        return false;
    }
    for (uint32_t i = 0; i < ni; i++) {
        const uint32_t kick = ix[i] / 4, corner = ix[i] % 4;
        if (kick < 2 || kick >= nv || corner > 2) {
            return false;
        }
    }
    for (uint32_t b = 0; b < nb; b++) {
        if ((uint64_t)br[b].firstIndex + br[b].indexCount > ni ||
            (uint64_t)br[b].firstVertex + br[b].vertexCount > nv) {
            return false;
        }
    }
    return true;
}

bool rd__LoadFrame(const char *path, RdFrame *out)
{
    if (!g_rd.inited || !path || !out) {
        return false;
    }
    FILE *fp = ico_fopen(path, "rb");
    if (!fp) {
        rd__Log("load: cannot open %s", path);
        return false;
    }
    static IdMap texMap, tgtMap, meshMap;
    texMap.n = tgtMap.n = meshMap.n = 0;
    memset(out, 0, sizeof(*out));
    char magic[8];
    uint32_t ver = 0, szCmd = 0, szState = 0, szVtx = 0;
    /* package AA1: version 3 (before RDC_AA1 and RdStateBlock.aa1) loads with
     * AA1 off; its state blocks are the first RD_STATE_BLOCK_V3_SIZE bytes */
    bool ok = rraw(fp, magic, 8) && memcmp(magic, RD_DUMP_MAGIC, 8) == 0 && r32(fp, &ver) &&
              ver >= 3u && ver <= RD_DUMP_VERSION && r32(fp, &szCmd) && szCmd == sizeof(RdCmd) &&
              r32(fp, &szState) &&
              szState == (ver == 3u ? RD_STATE_BLOCK_V3_SIZE : sizeof(RdStateBlock)) &&
              r32(fp, &szVtx) && szVtx == sizeof(RdScreenVtx);
    if (!ok) {
        rd__Log("load: %s is not an rd dump of version 3 to %u", path, RD_DUMP_VERSION);
        fclose(fp);
        return false;
    }
    /* version 6 appended RD_TARGET_FEED_HELD: an older dump's handles have
       16 fixed targets, so its first temporary slot's low half is 17;
       version 8 RD_TARGET_DISPLAY_HELD: 17 before it, low half 18 */
    tgtMap.fixedCount = ver <= 5u ? 16u : (ver <= 7u ? 17u : RD_TARGET_COUNT);
    ok = r32(fp, &out->gsW) && r32(fp, &out->gsH) && r32(fp, &out->number) && r32(fp, &out->keep) &&
         r32(fp, &out->hasCamera) && rraw(fp, &out->camera, sizeof(out->camera)) &&
         rraw(fp, &out->startState, szState) && rraw(fp, &out->endState, szState);
    for (int l = 0; ok && l < RD_LIST_COUNT; l++) {
        uint32_t n = 0;
        ok = r32(fp, &n) && n < (1u << 24);
        if (ok && n) {
            out->lists[l].cmds = malloc((size_t)n * sizeof(RdCmd));
            out->lists[l].cap = n;
            out->lists[l].count = n;
            ok = out->lists[l].cmds && rraw(fp, out->lists[l].cmds, (size_t)n * sizeof(RdCmd));
        }
    }
    uint32_t psz = 0;
    ok = ok && r32(fp, &psz) && psz < (1u << 30);
    if (ok && psz) {
        out->payload = malloc(psz);
        out->payloadCap = out->payloadSize = psz;
        ok = out->payload && rraw(fp, out->payload, psz);
    }
    for (int l = 0; ok && l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; ok && i < out->lists[l].count; i++) {
            const RdCmd *c = &out->lists[l].cmds[i];
            ok = cmdValid(out, c);
            /* package DEF: the count is not dumped; textCollect needs it */
            if (c->type == RDC_OVERLAY_TEXT && c->b[0] == RD_OTEXT_ITEM) {
                out->textItems++;
            }
        }
    }
    /* textures: images now, target views after the temp targets exist */
    uint32_t nt = 0;
    ok = ok && r32(fp, &nt) && nt <= MAX_REFS;
    static TexHeader views[MAX_REFS];
    uint32_t nViews = 0;
    for (uint32_t i = 0; ok && i < nt; i++) {
        TexHeader h;
        ok = r32(fp, &h.id) && r32(fp, &h.kind) && r32(fp, &h.src) && r32(fp, &h.bakedTexa) &&
             r32(fp, &h.w) && r32(fp, &h.h) && r32(fp, &h.target) && r32(fp, &h.view);
        if (!ok) {
            break;
        }
        if (h.kind == RD_TEXKIND_IMAGE) {
            /* R8: an image's view word is its texel format (the uncompressed
               ones: a pack's BC replacement is never dumped); version 7 a
               sheet's, with its style above the format byte */
            const uint8_t fmt = (uint8_t)(h.view & 0xFFu);
            const int sheet = ver >= 7u && fmt == RD_TEXEL_SHEET;
            /* a sheet's bits 10..15 are its rim weight (16 for the faint
               class), so any value is valid there */
            ok = h.w && h.h && h.w <= 8192 && h.h <= 8192 && (sheet || h.view <= RD_TEXEL_R8);
            const size_t bytes = (size_t)h.w * h.h * rd__TexelBytes(fmt);
            uint8_t *px = ok ? malloc(bytes) : NULL;
            ok = px && rraw(fp, px, bytes);
            if (ok) {
                RdTex t = rd__CreateTextureFmt(h.w, h.h, px, fmt, (RdTexSrc)h.src, "dump");
                RdTexRec *tr = rd__TexRec(t.id);
                if (tr) {
                    tr->bakedTexa = (uint8_t)h.bakedTexa;
                    if (sheet) {
                        const uint32_t rw = (h.view >> 10) & 63u;
                        tr->sheet[0] = (h.view >> 8) & 1u ? (uint8_t)(rw ? rw : 64u) : 0u;
                        tr->sheet[1] = (uint8_t)(h.view >> 16);
                        tr->sheet[2] = (uint8_t)(h.view >> 24);
                        tr->sheet[3] = (uint8_t)((h.view >> 9) & 1u);
                        const uint32_t up = (h.bakedTexa >> 8) & 0xFFu;
                        tr->sheetScale =
                            (uint8_t)(up + 1u > ICO_SHEET_SCALE_MAX ? ICO_SHEET_SCALE_MAX
                                                                    : up + 1u);
                    }
                }
                texMap.from[texMap.n] = h.id;
                texMap.to[texMap.n++] = t.id;
            }
            free(px);
        } else {
            views[nViews++] = h;
        }
    }
    uint32_t nr = 0;
    ok = ok && r32(fp, &nr) && nr <= RD_MAX_TEMP_PER_FRAME * 4;
    for (uint32_t i = 0; ok && i < nr; i++) {
        uint32_t id, w, h, d, k;
        ok = r32(fp, &id) && r32(fp, &w) && r32(fp, &h) && r32(fp, &d) && r32(fp, &k) && w && h &&
             w <= 8192 && h <= 8192;
        if (ok) {
            uint32_t nid = rd__TempTargetAlloc(w, h, (int)d, (int)k);
            tgtMap.from[tgtMap.n] = id;
            tgtMap.to[tgtMap.n++] = nid;
            if (nid && out->tempCount < RD_MAX_TEMP_PER_FRAME) {
                out->tempTargets[out->tempCount++] = nid;
            }
        }
    }
    uint32_t nm = 0;
    ok = ok && r32(fp, &nm) && nm <= MAX_REFS;
    for (uint32_t i = 0; ok && i < nm; i++) {
        uint32_t id, nv, qpv, ni, nb;
        char name[25];
        ok = r32(fp, &id) && r32(fp, &nv) && r32(fp, &qpv) && r32(fp, &ni) && r32(fp, &nb) &&
             nv < (1u << 22) && qpv <= 8 && ni < (1u << 24) && nb < (1u << 20) &&
             rraw(fp, name, 24);
        if (!ok) {
            break;
        }
        name[24] = 0;
        float (*st)[4] = malloc((size_t)(nv ? nv : 1) * qpv * 16 + 16);
        uint32_t *ix = malloc((size_t)ni * 4 + 4);
        RdVuBatchRec *br = malloc((size_t)nb * sizeof(RdVuBatchRec) + 4);
        ok = st && ix && br && rraw(fp, st, (size_t)nv * qpv * 16) &&
             rraw(fp, ix, (size_t)ni * 4) && rraw(fp, br, (size_t)nb * sizeof(RdVuBatchRec)) &&
             meshValid(nv, qpv, ix, ni, br, nb);
        if (ok) {
            meshMap.from[meshMap.n] = id;
            meshMap.to[meshMap.n++] =
                rd__VuMeshCreateRaw((const float (*)[4])st, nv, qpv, ix, ni, br, nb, name);
        }
        free(st);
        free(ix);
        free(br);
    }
    fclose(fp);
    for (uint32_t i = 0; ok && i < nViews; i++) {
        RdTex t = rd_TargetTexture((RdTarget){mapTarget(&tgtMap, views[i].target)},
                                   (RdTexView)views[i].view);
        texMap.from[texMap.n] = views[i].id;
        texMap.to[texMap.n++] = t.id;
    }
    if (!ok) {
        rd__Log("load: %s is truncated or corrupt", path);
        /* what the load created in the context goes with it (the images and
         * meshes here, the temporary targets with the frame) */
        for (uint32_t i = 0; i < texMap.n; i++) {
            rd_DestroyTexture((RdTex){texMap.to[i]});
        }
        for (uint32_t i = 0; i < meshMap.n; i++) {
            rd_DestroyVuMesh((RdMesh){meshMap.to[i]});
        }
        rd__FrameFree(out);
        return false;
    }
    remapState(&out->startState, &texMap, &tgtMap);
    remapState(&out->endState, &texMap, &tgtMap);
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < out->lists[l].count; i++) {
            RdCmd *c = &out->lists[l].cmds[i];
            switch (c->type) {
            case RDC_TEXTURE:
                c->u[0] = mapId(&texMap, c->u[0]);
                break;
            case RDC_TARGET:
            case RDC_EXACT_BLEND:
            case RDC_COPY:
                c->u[0] = mapTarget(&tgtMap, c->u[0]);
                c->u[1] = mapTarget(&tgtMap, c->u[1]);
                break;
            case RDC_CLEAR:
                c->u[0] = mapTarget(&tgtMap, c->u[0]);
                break;
            case RDC_MESH:
            case RDC_SKINNED:
                c->u[0] = mapId(&meshMap, c->u[0]);
                break;
            default:
                break;
            }
        }
    }
    out->closed = 1;
    return true;
}

/* ------------------------------------------------------ dump on demand */

/* Package Q1: the window build's F12 (port/platform/window_host.c): the
 * last closed frame as a dump (rd_DumpFrame) and the DISPLAY target as it
 * was last presented as a PNG (rd_ReadDisplay: a synchronous readback, so
 * for a key press, not for every frame).  Either path may be NULL.  True
 * when everything asked for was written. */
bool rd_DumpOnDemand(const char *dumpPath, const char *pngPath);

bool rd_DumpOnDemand(const char *dumpPath, const char *pngPath)
{
    bool ok = true;
    if (dumpPath) {
        ok = rd_DumpFrame(dumpPath);
    }
    if (pngPath) {
        const RdTargetRec *t = rd__TargetRec(RD_TARGET_DISPLAY + 1);
        uint32_t w = 0, h = 0;
        uint8_t *px = t ? malloc((size_t)t->tw * t->th * 4) : NULL;
        const bool shot =
            px && rd_ReadDisplay(px, &w, &h) && rd_WritePng(pngPath, px, w, h, w * 4, 0);
        if (!shot) {
            rd__Log("dump: no screenshot %s", pngPath);
        }
        free(px);
        ok = ok && shot;
    }
    return ok;
}
