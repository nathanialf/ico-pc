/* rd_replay_tool: loads an rd frame dump (rd_dump_frame) and renders it
 * headless to a PNG, for the renderer verification plan (backend against
 * backend, before against after).
 *
 *   rd_replay_tool <dump> <out.png> [--target NAME] [--present WxH]
 *                  [--backend vulkan|d3d12]
 *
 * NAME is a named target (SCENE, DISPLAY (default), SHADOW0..2, WORK0..3,
 * AA0, AA1, FEED128, AURA_WORK, AURA_TAP, WORK2_PAD, FEED_HELD,
 * DISPLAY_HELD).  --present renders the Original presenter into a W x H
 * output and writes that instead.  Commands the replayer does not model are
 * skipped with a message (the final line counts them).
 *
 * --backend picks the RHI backend (default: the build's default,
 * port/rhi/rhi.h rhi_create_backend), so the same dump can be rendered on
 * Vulkan and D3D12 and the PNGs compared.
 *
 * The display options: a dump does not carry them, so the replay takes them
 * here, the PS2 picture by default:
 *   --enhanced            the Enhanced flag: the deferred text and UI scale,
 *                         and the --present box as the default resolution;
 *                         the four options below apply without it, but a
 *                         replay at a scale above 1x still wants it for the
 *                         game's picture
 *   --aspect A            4:3 (default), 16:10, 16:9, 21:9 (64/27), 32:9, 48:9
 *                         (the widest the game allows is 20:3) or a number
 *                         (w / h)
 *   --resolution R        the scene's resolution: WxH or Nx (default: the
 *                         --present box with --enhanced, else the GS size)
 *   --full-height         the full-height scene
 *   --full-pixel          the reduction draws the whole frame: no black border
 *   --filter F            original, trilinear or anisotropic
 *   --mirror              the mirror mode: UI prims flipped at replay, the
 *                         present flipped (any preset)
 *   --crt MODE            (with --present) the CRT filter in MODE
 *                         (scanlines, consumer, trinitron, pvm, shadow) at
 *                         full strength, the modes' own parameters;
 *                         --crt-strength K (0..1, after it) sets the strength
 *   --overlay-test        (with --present) registers a presentation overlay
 *                         (rd.h rd_set_present_overlay) drawing a test
 *                         pattern after the box blit: a one-pixel white
 *                         outline on the box's edge and a 32 x 32 square,
 *                         opaque red, 16 pixels in from the box's top-left
 *                         corner, plus a half-transparent white one beside
 *                         it, and a popup: a dark panel with two rows of
 *                         text ("Continue", "Quit Game") through font.c's
 *                         overlay mode.  Without it the tool registers no
 *                         overlay
 *
 * Inspection:
 *   --list                prints every command of the replayed lists: the
 *                         list, index, type and key; for a draw its texture
 *                         (id and size), and for screen prims the prim,
 *                         space, vertex count, the bounding box in GS pixels
 *                         and the texel rectangle (UV, or STQ times the size)
 *   --no-device           no device: the dump is loaded into a record-only
 *                         renderer (rd__init_record_only) for
 *                         --list, --mesh and --dump-textures and nothing is
 *                         rendered (<out.png> is not written); --list falls
 *                         back to it by itself when there is no device.
 *                         --list also prints, per texture, its alpha (min,
 *                         max, the share below 0x80, the share of black
 *                         texels; "replacement (blank)" for a pack's
 *                         texture, dumped blank), the state's FBA, PABE,
 *                         TEXA, DATE and targets, and a post record's kind,
 *                         rectangle, UVs, scalar, RGBA and blend
 *   --nop L:A[-B]         turns commands A..B of list L into NOPs before the
 *                         replay (repeatable), to find the draw behind a pixel
 *   --mesh NAME           prints the VU meshes of that name vertex by vertex
 *                         (stream quadwords; '*' where the static index list
 *                         draws the triangle ending at the vertex, '^' where
 *                         that triangle overlaps an earlier one in its plane
 *                         and is drawn in front of it: issue 25)
 *   --dump-textures DIR   writes every image texture of the dump to
 *                         DIR/tex-<id>-<w>x<h>.png as decoded (RGBA8, the
 *                         alpha byte as stored: GS 0x80 = 1.0; an R8
 *                         texture as grey, its byte in each channel)
 *   --stats               prints the replay's counts from its performance
 *                         record (rd.h RdPerfRecord): draws, passes, bind
 *                         groups created (rd's uniform and texture groups
 *                         apart), bind group and pipeline binds, ring bytes,
 *                         pipeline barriers and copies, the screen-prim
 *                         draws one per command and merged; on a device
 *                         with GPU timestamps, after the picture is written,
 *                         the frame is replayed RHI_FRAMES_IN_FLIGHT + 2
 *                         more times and the last replay's GPU times are
 *                         printed: the uploads, each list, each picture
 *                         effect (rd.h RD_PERF_POST_*, inside the lists'
 *                         times) and the present
 *   --no-aa1              replays with PRIM.AA1 off: every RDC_AA1 a NOP and
 *                         the start state's bit clear, the frame as the
 *                         renderer drew it before AA1 was decoded (a
 *                         before/after pair from one dump)
 *
 * Interpolation:
 *   --interp T PREV       replays the frame the presenter builds between the
 *                         dump PREV (the tick before, e.g. the game's
 *                         rd-NNNNN-prev.rddump) and <dump> at alpha T (0..1)
 *                         instead of <dump> itself: rd__interp_frame as a
 *                         tick's first present; the two must be
 *                         consecutive frames.  The meshes' kept versions are
 *                         not in a dump: each frame's own mesh is its stream
 *
 * Exit: 0 written (or, with --no-device, listed), 1 error, 77 no device or no dump file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../include/ico_endian.h"
#include "rd_internal.h"
#include "rd_mesh.h"
#include "font.h" /* port/ui: the overlay test's text */

static const char *const kNames[] = {
    "SCENE", "DISPLAY",   "SHADOW0",  "SHADOW1",   "SHADOW2",   "WORK0",
    "WORK1", "WORK2",     "WORK3",    "AA0",       "AA1",       "FEED128",
    "",      "AURA_WORK", "AURA_TAP", "WORK2_PAD", "FEED_HELD", "DISPLAY_HELD"};

static const char *const kCmdNames[RDC_COUNT] = {
    "NOP",          "TEST",           "BLEND",     "ABE",         "ZWRITE",       "FBA",
    "PABE",         "COLCLAMP",       "TEXA",      "FILTER",      "WRAP",         "TEXTURE",
    "TEXTURE_OFF",  "UVOFFSET",       "COLORMASK", "TARGET",      "SCISSOR",      "ALPHA",
    "SHADE",        "CLEAR",          "SCREEN",    "EXACT_BLEND", "COPY",         "MESH",
    "SKINNED",      "GRID",           "PARTICLES", "WORLD_PRIMS", "SHADOW_STRIP", "POST_STUB",
    "SHADOW_RESET", "SHADOW_RESOLVE", "AA1",       "OVERLAY_TEXT"};

static const char *const kPrimNames[] = {"points",   "lines",  "linestrip", "tris",
                                         "tristrip", "trifan", "sprites"};

static const char *const kPostNames[RD_POST_COUNT] = {
    "REDUCTION",  "KEEP",          "FADE",         "LETTERBOX",    "BRIGHTNESS",
    "FILM_NOISE", "AA_DOWNSAMPLE", "AA_COMPOSITE", "FOG",          "SHADOW_RESOLVE",
    "BLUR",       "COMPOSITE_FIX", "COPY",         "PRESENT_BLIT", "MOTION_BLUR",
    "DOF",        "FLARE",         "BLOOM",        "AURA",         "EYE_BLUR"};

/* the name of a named target's id, else the id in hex */
static const char *targetName(uint32_t id, char buf[16])
{
    if (id == 0) {
        return "none";
    }
    for (int k = 0; k < RD_TARGET_COUNT; k++) {
        if (k == RD_TARGET_DATE_SNAPSHOT) {
            if (rd_target((RdTargetId)k).id == id) {
                return "DATE_SNAPSHOT";
            }
        } else if (k < (int)(sizeof(kNames) / sizeof(kNames[0])) &&
                   rd_target((RdTargetId)k).id == id) {
            return kNames[k];
        }
    }
    snprintf(buf, 16, "%#x", id);
    return buf;
}

/* --list: a texture's alpha as the GS sees it (0x80 = 1.0): min and max,
 * the share below 0x80 and the share of black texels (RGB 0); a dumped
 * replacement is a blank image (rd_dump.c), every byte 0 */
static void texSummary(uint32_t id)
{
    const RdTexRec *t = rd__tex_rec(id);
    if (!t) {
        printf(" (no record)");
        return;
    }
    if (t->kind == RD_TEXKIND_TARGET) {
        char buf[16];
        printf(" target %s view %u", targetName(t->target, buf), t->view);
        return;
    }
    if (!t->pixels || (t->format != RD_TEXEL_RGBA8 && !rd__texel_is_coverage(t->format))) {
        printf(" no texels");
        return;
    }
    const size_t n = (size_t)t->w * t->h;
    if (rd__texel_is_coverage(t->format)) {
        unsigned lo = 255, hi = 0;
        for (size_t k = 0; k < n; k++) {
            lo = t->pixels[k] < lo ? t->pixels[k] : lo;
            hi = t->pixels[k] > hi ? t->pixels[k] : hi;
        }
        printf(" %s %02x..%02x", t->format == RD_TEXEL_SHEET ? "sheet" : "r8", lo, hi);
        return;
    }
    unsigned lo = 255, hi = 0;
    size_t below = 0, black = 0, zero = 0;
    for (size_t k = 0; k < n; k++) {
        const uint8_t *px = t->pixels + k * 4;
        lo = px[3] < lo ? px[3] : lo;
        hi = px[3] > hi ? px[3] : hi;
        below += px[3] < 0x80;
        black += (px[0] | px[1] | px[2]) == 0;
        zero += (px[0] | px[1] | px[2] | px[3]) == 0;
    }
    if (zero == n) {
        printf(" replacement (blank)");
        return;
    }
    printf(" alpha %02x..%02x below80 %.1f%% black %.1f%%", lo, hi,
           n ? 100.0 * (double)below / (double)n : 0.0,
           n ? 100.0 * (double)black / (double)n : 0.0);
}

/* --list, RDC_SKINNED: the drawn vertices' place as cluster.vsm computes it
 * (vu_skin.hlsl: two bones, the world-to-screen matrix at VU memory 4..7,
 * the divide), on the CPU: the bounding box in GS pixels of the target and
 * the GS Z range.  A summary of where the draw lands, not a raster */
static void skinnedPlace(const RdFrame *f, const RdCmd *c, const RdStateBlock *st)
{
    RdVuPayload p;
    const RdMeshRec *m = rd__mesh_rec(c->u[0]);
    if (!m || !m->stream || m->qwPerVertex < 5 ||
        (uint64_t)c->u[1] + sizeof(p) + sizeof(RdVuBlock) > f->payloadSize) {
        return;
    }
    memcpy(&p, f->payload + c->u[1], sizeof(p));
    const float (*mem)[4] = (const float (*)[4])(const void *)(f->payload + c->u[1] + sizeof(p));
    const float (*bone)[4] = mem + 36;
    if ((uint64_t)c->u[1] + sizeof(p) + sizeof(RdVuBlock) + (uint64_t)p.boneQw * 16 >
        f->payloadSize) {
        return;
    }
    uint32_t last = p.firstBatch + p.batchCount;
    if (last > m->batchCount || p.batchCount == 0) {
        last = m->batchCount;
    }
    double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30, z0 = 1e30, z1 = -1e30;
    const double ox = 2048.0 - (double)(st->gsW >> 1), oy = 2048.0 - (double)(st->gsH >> 1);
    for (uint32_t b = p.firstBatch; b < last; b++) {
        const RdVuBatchRec *br = &m->batches[b];
        for (uint32_t v = br->firstVertex; v < br->firstVertex + br->vertexCount; v++) {
            const float (*q)[4] = m->stream + (size_t)v * m->qwPerVertex;
            uint32_t a[2];
            memcpy(&a[0], &q[2][0], 4);
            memcpy(&a[1], &q[2][2], 4);
            const float w[2] = {q[2][1], q[2][3]};
            float pb[3] = {0, 0, 0};
            for (int k = 0; k < 2; k++) {
                const uint32_t i = (a[k] < 16 ? 16 : a[k]) - 16;
                if (i + 3 >= p.boneQw) {
                    continue;
                }
                for (int e = 0; e < 3; e++) {
                    const float t = bone[i][e] * q[0][0] + bone[i + 1][e] * q[0][1] +
                                    bone[i + 2][e] * q[0][2] + bone[i + 3][e];
                    pb[e] += t * w[k];
                }
            }
            float h[4];
            for (int e = 0; e < 4; e++) {
                h[e] = mem[4][e] * pb[0] + mem[5][e] * pb[1] + mem[6][e] * pb[2] + mem[7][e];
            }
            if (h[3] <= 0.0f) {
                continue;
            }
            const double x = h[0] / h[3] - ox, y = h[1] / h[3] - oy, z = 16.0 * h[2] / h[3];
            x0 = x < x0 ? x : x0;
            x1 = x > x1 ? x : x1;
            y0 = y < y0 ? y : y0;
            y1 = y > y1 ? y : y1;
            z0 = z < z0 ? z : z0;
            z1 = z > z1 ? z : z1;
        }
    }
    if (x1 >= x0) {
        printf(" place (%.1f,%.1f)-(%.1f,%.1f) z %.0f..%.0f", x0, y0, x1, y1, z0, z1);
    }
}

static uint32_t s_listed;

static void listCmd(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *st)
{
    const RdFrame *f = user;
    s_listed++;
    char nb[2][16];
    printf("%2d:%-5u %-14s key %08x%08x", list, index,
           c->type < RDC_COUNT ? kCmdNames[c->type] : "?", c->keyHi, c->keyLo);
    if (c->type == RDC_TEXTURE) {
        const RdTexRec *t = rd__tex_rec(c->u[0]);
        printf(" tex %u %ux%u fn %u tcc %u", c->u[0], t ? t->w : 0, t ? t->h : 0, c->b[0], c->b[1]);
        texSummary(c->u[0]);
    } else if (c->type == RDC_FILTER || c->type == RDC_WRAP) {
        printf(" %u %u", c->b[0], c->b[1]);
    } else if (c->type == RDC_TARGET) {
        printf(" colour %s depth %s gs %ux%u offset %u", targetName(c->u[0], nb[0]),
               targetName(c->u[1], nb[1]), c->u[2] & 0xFFFF, c->u[2] >> 16, c->b[0]);
    } else if (c->type == RDC_ALPHA) {
        printf(" blend %u fix %u", c->b[0], c->b[1]);
    } else if (c->type == RDC_COLORMASK) {
        printf(" fbmsk %08x", c->u[0]);
    } else if (c->type == RDC_SCISSOR) {
        printf(" (%d,%d)-(%d,%d)", (int32_t)c->u[0], (int32_t)c->u[1], (int32_t)c->u[2],
               (int32_t)c->u[3]);
    } else if (c->type <= RDC_STATE_LAST || c->type == RDC_AA1) {
        if (c->type != RDC_TEXTURE_OFF && c->type != RDC_NOP && c->type != RDC_UVOFFSET) {
            printf(" %u", c->b[0]);
        }
    } else if (c->type == RDC_CLEAR) {
        printf(" %s rgba %u,%u,%u,%u depth %u z %u", targetName(c->u[0], nb[0]), c->b[0], c->b[1],
               c->b[2], c->b[3], c->b[4], c->u[1]);
    } else if (c->type == RDC_SCREEN &&
               c->u[0] + (uint64_t)c->u[1] * sizeof(RdScreenVtx) <= f->payloadSize) {
        const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + c->u[0]);
        const RdTexRec *t = st->ds.texEnabled ? rd__tex_rec(st->tex) : NULL;
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        float u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
        for (uint32_t i = 0; i < c->u[1]; i++) {
            float x = v[i].x / 16.0f, y = v[i].y / 16.0f, s = v[i].s, tt = v[i].t;
            if (c->b[2]) {
                s /= 16.0f;
                tt /= 16.0f;
            } else if (t && v[i].q != 0.0f) {
                s = s / v[i].q * (float)t->w;
                tt = tt / v[i].q * (float)t->h;
            }
            x0 = x < x0 ? x : x0;
            x1 = x > x1 ? x : x1;
            y0 = y < y0 ? y : y0;
            y1 = y > y1 ? y : y1;
            u0 = s < u0 ? s : u0;
            u1 = s > u1 ? s : u1;
            v0 = tt < v0 ? tt : v0;
            v1 = tt > v1 ? tt : v1;
        }
        printf(" %s space %u n %u xy (%.2f,%.2f)-(%.2f,%.2f)%s",
               c->b[0] < 7 ? kPrimNames[c->b[0]] : "?", c->b[1], c->u[1], x0, y0, x1, y1,
               c->b[3] == RD_SCREEN_TEXT_QUADS ? " text-quads" : "");
        if (st->aa1) {
            printf(" aa1");
        }
        if (st->ds.texEnabled) {
            printf(" tex %u %ux%u %s (%.3f,%.3f)-(%.3f,%.3f) filter %u/%u", st->tex, t ? t->w : 0,
                   t ? t->h : 0, c->b[2] ? "uv" : "stq", u0, v0, u1, v1, st->ds.magFilter,
                   st->ds.minFilter);
        }
    } else if (c->type == RDC_OVERLAY_TEXT && c->u[1] <= f->payloadSize &&
               c->u[2] <= f->payloadSize - c->u[1]) {
        if (c->b[0] == RD_OTEXT_ITEM && c->u[2] == sizeof(RdTextItem)) {
            RdTextItem it;
            memcpy(&it, f->payload + c->u[1], sizeof(it));
            it.utf8[RD_TEXT_BYTES - 1] = '\0';
            printf(" item \"%s\" at (%.2f,%.2f) size %.1f flags %x rgba %u,%u,%u,%u%s%s", it.utf8,
                   it.x, it.y, it.size, it.flags, it.rgba[0], it.rgba[1], it.rgba[2], it.rgba[3],
                   it.additive ? " additive" : "", it.hasXf ? " xf" : "");
        } else if (c->b[0] == RD_OTEXT_OP && c->u[2] == sizeof(RdTextOp)) {
            RdTextOp op;
            memcpy(&op, f->payload + c->u[1], sizeof(op));
            printf(" op post %u rgba %u,%u,%u,%u fix %u lines %u", c->b[1], op.rgba[0], op.rgba[1],
                   op.rgba[2], op.rgba[3], op.fix, op.lines);
        }
    } else if (c->type == RDC_POST_STUB && c->u[1] <= f->payloadSize &&
               sizeof(RdPostRec) <= f->payloadSize - c->u[1]) {
        RdPostRec r;
        memcpy(&r, f->payload + c->u[1], sizeof(r));
        printf(" post %s rgba %u,%u,%u,%u fix %u blend %u abe %u exact %u z %u lines %u"
               " rect (%g,%g)-(%g,%g) uv (%g,%g)-(%g,%g) scalar (%g,%g,%g,%g)"
               " src %s dst %s view %u target %s depth %s",
               c->b[0] < RD_POST_COUNT ? kPostNames[c->b[0]] : "?", r.rgba[0], r.rgba[1], r.rgba[2],
               r.rgba[3], r.fix, r.blend, r.abe, r.exactInt, r.z, r.lines, (double)r.rect[0],
               (double)r.rect[1], (double)r.rect[2], (double)r.rect[3], (double)r.uv[0],
               (double)r.uv[1], (double)r.uv[2], (double)r.uv[3], (double)r.scalar[0],
               (double)r.scalar[1], (double)r.scalar[2], (double)r.scalar[3],
               targetName(r.src, nb[0]), targetName(r.dst, nb[1]), r.srcView,
               targetName(st->color, nb[0]), targetName(st->depth, nb[1]));
        if (st->ds.texEnabled) {
            printf(" tex %u", st->tex);
            texSummary(st->tex);
        }
    } else if (c->type >= RDC_MESH && c->type <= RDC_PARTICLES) {
        const RdMeshRec *m = rd__mesh_rec(c->u[0]);
        if (m) {
            printf(" mesh %s (%u vertices, %u batches)", m->name, m->vertexCount, m->batchCount);
        }
        printf(" prog %u", c->b[0]);
        if (c->u[1] <= f->payloadSize && sizeof(RdVuPayload) <= f->payloadSize - c->u[1]) {
            RdVuPayload p;
            memcpy(&p, f->payload + c->u[1], sizeof(p));
            printf(" code %u clip %u batches %u+%u", p.code, p.clip, p.firstBatch, p.batchCount);
        }
        if (c->b[3]) {
            printf(" stretch"); /* drawn across a wide target (rd_mesh.c pushVu) */
        }
        if (c->type == RDC_SKINNED) {
            skinnedPlace(f, c, st);
        }
        if (st->ds.texEnabled) {
            printf(" tex %u", st->tex);
            texSummary(st->tex);
        }
    }
    if (c->type >= RDC_CLEAR) {
        const RdTestState *t = &st->ds.test;
        printf(" | ate %u atst %u aref %u afail %u zte %u ztst %u zwrite %u abe %u blend %u fix %u"
               " fba %u pabe %u texa %u date %u colclamp %u mask %x",
               t->ate, t->atst, t->aref, t->afail, t->zte, t->ztst, st->ds.zwrite, st->ds.abe,
               st->ds.blend, st->ds.blendFix, st->ds.fba, st->ds.pabe, st->ds.texa, t->date,
               st->ds.colclamp, st->ds.colorMask);
        if (c->type != RDC_CLEAR) {
            printf(" target %s depth %s", targetName(st->color, nb[0]),
                   targetName(st->depth, nb[1]));
        }
    }
    printf("\n");
}

/* --mesh NAME: every vertex of the VU meshes of that name, per batch: the
 * quadwords as the stream holds them and whether the static index list
 * draws the triangle that ends at the vertex */
static void listMesh(const char *name)
{
    for (uint32_t i = 0; g_rd.meshes && i < RD_MAX_MESHES; i++) {
        const RdMeshRec *m = &g_rd.meshes[i];
        if (!m->live || !m->vu || strcmp(m->name, name) != 0) {
            continue;
        }
        printf("mesh %u %s: %u vertices, %u qw each, %u batches\n", (m->gen << 16) | (i + 1),
               m->name, m->vertexCount, m->qwPerVertex, m->batchCount);
        for (uint32_t b = 0; b < m->batchCount; b++) {
            const RdVuBatchRec *br = &m->batches[b];
            printf(" batch %u: prim %03x material %u, vertices %u..%u, %u indices\n", b, br->prim,
                   br->material, br->firstVertex, br->firstVertex + br->vertexCount - 1,
                   br->indexCount);
            for (uint32_t v = br->firstVertex; v < br->firstVertex + br->vertexCount; v++) {
                bool drawn = false, later = false;
                for (uint32_t k = 0; k < br->indexCount; k++) {
                    const uint32_t ix = m->index[br->firstIndex + k];
                    drawn |= ix / 4 == v;
                    later |= ix / 4 == v && rd__mesh_draw_index(m)[br->firstIndex + k] != ix;
                }
                printf("  %4u %c", v, later ? '^' : drawn ? '*' : ' ');
                for (uint32_t q = 0; q < m->qwPerVertex; q++) {
                    const float *f = m->stream[(size_t)v * m->qwPerVertex + q];
                    printf(" (%g %g %g %g)", f[0], f[1], f[2], f[3]);
                }
                printf("\n");
            }
        }
    }
}

static void dumpTextures(const char *dir)
{
    char path[1024];
    for (uint32_t i = 0; g_rd.textures && i < RD_MAX_TEXTURES; i++) {
        const RdTexRec *t = &g_rd.textures[i];
        if (!t->live || t->kind != RD_TEXKIND_IMAGE || !t->pixels) {
            continue;
        }
        const uint32_t id = (t->gen << 16) | (i + 1);
        snprintf(path, sizeof(path), "%s/tex-%u-%ux%u.png", dir, id, t->w, t->h);
        if (rd__texel_is_coverage(t->format)) {
            /* coverage as grey (a sheet's too) */
            uint8_t *grey = malloc((size_t)t->w * t->h * 4);
            for (size_t k = 0; grey && k < (size_t)t->w * t->h; k++) {
                grey[k * 4] = grey[k * 4 + 1] = grey[k * 4 + 2] = t->pixels[k];
                grey[k * 4 + 3] = 0xFF;
            }
            if (grey && rd_write_png(path, grey, t->w, t->h, t->w * 4, 0)) {
                printf("%s\n", path);
            }
            free(grey);
        } else if (rd_write_png(path, t->pixels, t->w, t->h, t->w * 4, 0)) {
            printf("%s\n", path);
        }
    }
}

static bool peekSize(const char *path, uint32_t *w, uint32_t *h)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return false;
    }
    uint8_t b[32];
    bool ok = fread(b, 1, sizeof(b), fp) == sizeof(b) && memcmp(b, RD_DUMP_MAGIC, 8) == 0;
    fclose(fp);
    if (ok) {
        *w = ico_le32(b + 24);
        *h = ico_le32(b + 28);
    }
    return ok;
}

/* --overlay-test: rd_overlay_prims sprites in 12.4 output pixels */
static void overlaySprite(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t c[4])
{
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    v[0].x = x0 * 16;
    v[0].y = y0 * 16;
    v[1].x = x1 * 16;
    v[1].y = y1 * 16;
    v[0].q = v[1].q = 1.0f;
    memcpy(v[0].rgba, c, 4);
    memcpy(v[1].rgba, c, 4);
    rd_overlay_prims(RD_PRIM_SPRITES, v, 2, (RdTex){0}, RD_BLEND_LERP_AS);
}

static void overlayTest(const RdOverlayCtx *ctx, void *user)
{
    (void)user;
    static const uint8_t white[4] = {0xFF, 0xFF, 0xFF, 0x80}, red[4] = {0xFF, 0, 0, 0x80},
                         half[4] = {0xFF, 0xFF, 0xFF, 0x40};
    const int32_t x0 = ctx->box.x, y0 = ctx->box.y;
    const int32_t x1 = x0 + (int32_t)ctx->box.w, y1 = y0 + (int32_t)ctx->box.h;
    overlaySprite(x0, y0, x1, y0 + 1, white);
    overlaySprite(x0, y1 - 1, x1, y1, white);
    overlaySprite(x0, y0, x0 + 1, y1, white);
    overlaySprite(x1 - 1, y0, x1, y1, white);
    overlaySprite(x0 + 16, y0 + 16, x0 + 48, y0 + 48, red);
    overlaySprite(x0 + 64, y0 + 16, x0 + 96, y0 + 48, half);
    /* a popup as port/ui draws one (a dark panel, two rows of
     * text through font.c's overlay mode), to see the UI under the filter */
    static const uint8_t panel[4] = {0x10, 0x10, 0x18, 0x60}, row[4] = {0xFF, 0xFF, 0xFF, 0x80},
                         dim[4] = {0xA0, 0xA0, 0xA0, 0x80};
    ui_begin_overlay(ctx);
    ui_draw_rect(200.0f, 170.0f, 440.0f, 282.0f, panel);
    ui_draw_text(320.0f, 214.0f, 27.0f, row, "Continue", UI_ALIGN_CENTER | UI_VALIGN_BASELINE);
    ui_draw_text(320.0f, 254.0f, 27.0f, dim, "Quit Game", UI_ALIGN_CENTER | UI_VALIGN_BASELINE);
    ui_end_overlay();
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s <dump> <out.png> [--target NAME] [--present WxH] [--enhanced] "
                "[--aspect A] [--resolution WxH|Nx] [--full-height] [--full-pixel] [--filter F] "
                "[--mirror] [--overlay-test] [--backend vulkan|d3d12] [--list] [--nop L:A[-B]] "
                "[--mesh NAME] [--dump-textures DIR] [--no-aa1] [--stats] [--interp T PREV]\n"
                "       [--no-device (with --list, --mesh or --dump-textures; <out.png> unused)]\n"
                "       [--crt scanlines|consumer|trinitron|pvm|shadow [--crt-strength K]]\n",
                argv[0]);
        return 1;
    }
    const char *dump = argv[1], *png = argv[2];
    int target = RD_TARGET_DISPLAY;
    uint32_t pw = 0, ph = 0;
    /* the display options */
    RdSettings s;
    bool list = false, overlay = false, noAa1 = false, stats = false;
    bool noDevice = false;
    const char *texDir = NULL, *meshName = NULL, *interpPrev = NULL;
    float interpT = 1.0f;

    struct {
        unsigned l, a, b;
    } nops[64];

    int nopCount = 0;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ORIGINAL;
    s.aspect = 4.0f / 3.0f;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            const char *n = argv[++i];
            target = -1;
            for (int k = 0; k < (int)(sizeof(kNames) / sizeof(kNames[0])); k++) {
                if (strcmp(n, kNames[k]) == 0) {
                    target = k;
                }
            }
            if (target < 0) {
                fprintf(stderr, "unknown target %s\n", n);
                return 1;
            }
        } else if (strcmp(argv[i], "--backend") == 0 && i + 1 < argc) {
            if (!rhi_create_backend(argv[++i])) {
                fprintf(stderr, "backend %s is not in this build\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "--enhanced") == 0) {
            s.preset = RD_PRESET_ENHANCED;
        } else if (strcmp(argv[i], "--full-height") == 0) {
            s.fullHeightScene = 1;
        } else if (strcmp(argv[i], "--full-pixel") == 0) {
            s.fullPixel = 1;
        } else if (strcmp(argv[i], "--mirror") == 0) {
            s.mirror = 1;
        } else if (strcmp(argv[i], "--aspect") == 0 && i + 1 < argc) {
            unsigned a = 0, b = 0;
            const char *v = argv[++i];
            if (sscanf(v, "%u:%u", &a, &b) == 2 && a == 21 && b == 9) {
                /* "21:9" is the game's 64:27 (2560x1080), as the Aspect
                   option means it (video_options.c), not 2.333 */
                s.aspect = 64.0f / 27.0f;
            } else if (sscanf(v, "%u:%u", &a, &b) == 2 && a && b) {
                s.aspect = (float)a / (float)b;
            } else if ((s.aspect = (float)atof(v)) <= 0.0f) {
                fprintf(stderr, "bad --aspect\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--resolution") == 0 && i + 1 < argc) {
            unsigned a = 0, b = 0;
            const char *v = argv[++i];
            if (sscanf(v, "%ux%u", &a, &b) == 2 && a && b) {
                s.sceneWidth = a;
                s.sceneHeight = b;
            } else if (sscanf(v, "%ux", &a) == 1 && a) {
                s.sceneScale = (float)a;
            } else {
                fprintf(stderr, "bad --resolution\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            const char *v = argv[++i];
            s.filterUpgrade = strcmp(v, "anisotropic") == 0 ? RD_FILTER_UPGRADE_ANISOTROPIC
                              : strcmp(v, "trilinear") == 0 ? RD_FILTER_UPGRADE_TRILINEAR
                                                            : RD_FILTER_UPGRADE_OFF;
        } else if (strcmp(argv[i], "--overlay-test") == 0) {
            overlay = true;
        } else if (strcmp(argv[i], "--stats") == 0) {
            stats = true;
        } else if (strcmp(argv[i], "--no-aa1") == 0) {
            noAa1 = true;
        } else if (strcmp(argv[i], "--interp") == 0 && i + 2 < argc) {
            char *end = NULL;
            interpT = strtof(argv[++i], &end);
            if (!end || *end != '\0' || !(interpT >= 0.0f && interpT <= 1.0f)) {
                fprintf(stderr, "bad --interp alpha\n");
                return 1;
            }
            interpPrev = argv[++i];
        } else if (strcmp(argv[i], "--crt") == 0 && i + 1 < argc) {
            /* the CRT filter's mode, at full strength */
            static const char *const modes[] = {"scanlines", "consumer", "trinitron", "pvm",
                                                "shadow"};
            const char *v = argv[++i];
            int m = -1;
            for (int k = 0; k < 5; k++) {
                if (strcmp(v, modes[k]) == 0) {
                    m = k;
                }
            }
            if (m < 0) {
                fprintf(stderr, "bad --crt (scanlines, consumer, trinitron, pvm or shadow)\n");
                return 1;
            }
            rd_crt_settings(&s, (RdCrtMode)(m + 1), s.crtMode ? s.crtStrength : 1.0f);
        } else if (strcmp(argv[i], "--crt-strength") == 0 && i + 1 < argc) {
            char *end = NULL;
            const float k = strtof(argv[++i], &end);
            if (!end || *end != '\0' || !(k >= 0.0f && k <= 1.0f) || !s.crtMode) {
                fprintf(stderr, "bad --crt-strength (0..1, after --crt)\n");
                return 1;
            }
            s.crtStrength = k;
        } else if (strcmp(argv[i], "--list") == 0) {
            list = true;
        } else if (strcmp(argv[i], "--no-device") == 0) {
            noDevice = true;
        } else if (strcmp(argv[i], "--mesh") == 0 && i + 1 < argc) {
            meshName = argv[++i];
        } else if (strcmp(argv[i], "--dump-textures") == 0 && i + 1 < argc) {
            texDir = argv[++i];
        } else if (strcmp(argv[i], "--nop") == 0 && i + 1 < argc && nopCount < 64) {
            unsigned l = 0, a = 0, b = 0;
            int n = sscanf(argv[++i], "%u:%u-%u", &l, &a, &b);
            if (n < 2 || l >= RD_LIST_COUNT) {
                fprintf(stderr, "bad --nop\n");
                return 1;
            }
            nops[nopCount].l = l;
            nops[nopCount].a = a;
            nops[nopCount++].b = n == 3 ? b : a;
        } else if (strcmp(argv[i], "--present") == 0 && i + 1 < argc) {
            if (sscanf(argv[++i], "%ux%u", &pw, &ph) != 2 || !pw || !ph) {
                fprintf(stderr, "bad --present size\n");
                return 1;
            }
        } else {
            fprintf(stderr, "unknown argument %s\n", argv[i]);
            return 1;
        }
    }
    uint32_t gw = 0, gh = 0;
    FILE *probe = fopen(dump, "rb");
    if (!probe) {
        /* rd_pixel writes the dump; without a Vulkan device it skipped */
        fprintf(stderr, "%s: no such dump (skipped)\n", dump);
        return 77;
    }
    fclose(probe);
    if (!peekSize(dump, &gw, &gh) || !gw || !gh || gw > 4096 || gh > 4096) {
        fprintf(stderr, "%s: not an rd dump\n", dump);
        return 1;
    }
    s.outputWidth = pw;
    s.outputHeight = ph;
    /* --no-device (or --list without a device) loads the dump into a
       record-only renderer, lists it and renders nothing */
    if (noDevice && !list && !texDir && !meshName) {
        fprintf(stderr, "--no-device renders nothing: give --list, --mesh or --dump-textures\n");
        return 1;
    }
    if (noDevice || !rd_init(gw, gh, &s, NULL)) {
        if (!noDevice && !list) {
            fprintf(stderr, "no usable %s device\n",
                    rhi_backend() == RHI_BACKEND_D3D12 ? "D3D12" : "Vulkan");
            return 77;
        }
        noDevice = true;
        if (!rd__init_record_only(gw, gh)) {
            fprintf(stderr, "rd__init_record_only failed\n");
            return 1;
        }
    }
    rd__set_not_implemented_fatal(false);
    if (overlay) {
        rd_set_present_overlay(overlayTest, NULL);
    }
    RdFrame pf, f;
    memset(&pf, 0, sizeof(pf));
    if (interpPrev && !rd__load_frame(interpPrev, &pf)) {
        rd_shutdown();
        return 1;
    }
    if (!rd__load_frame(dump, &f)) {
        rd__frame_free(&pf);
        rd_shutdown();
        return 1;
    }
    for (int k = 0; k < nopCount; k++) {
        const RdCmdList *cl = &f.lists[nops[k].l];
        for (uint32_t c = nops[k].a; c <= nops[k].b && c < cl->count; c++) {
            if (!rd__cmd_is_state(cl->cmds[c].type)) {
                cl->cmds[c].type = RDC_NOP;
            }
        }
    }
    if (noAa1) {
        f.startState.aa1 = 0;
        for (int l = 0; l < RD_LIST_COUNT; l++) {
            for (uint32_t c = 0; c < f.lists[l].count; c++) {
                if (f.lists[l].cmds[c].type == RDC_AA1) {
                    f.lists[l].cmds[c].type = RDC_NOP;
                }
            }
        }
    }
    const RdFrame *rf = &f;
    if (interpPrev) {
        /* the presenter's frame between the two */
        RdInterpStats ist;
        rf = rd__interp_frame(&pf, &f, interpT, 1, &ist);
        if (!rf) {
            fprintf(stderr, "rd__interp_frame failed\n");
            rd__frame_free(&pf);
            rd__frame_free(&f);
            rd_shutdown();
            return 1;
        }
        printf("interp %u -> %u at %g: snap %u, %u keyed draws: %u blended, %u unmatched, %u "
               "mismatched, %u jumped; %u mesh streams blended; %u blended as rotations; %u lights "
               "re-paired; %u paired by place; %u grid STs blended\n",
               pf.number, f.number, (double)interpT, ist.snap, ist.keyed, ist.lerped, ist.missing,
               ist.mismatch, ist.jump, ist.morph, ist.rotated, ist.lightPaired, ist.placed,
               ist.gridSt);
        printf("interp: unmatched %u not drawn before, %u fewer before, %u left by the place "
               "pairing, %u payload; %u paired with another place; %u of the tick before kept, "
               "%u held\n",
               ist.unmatchedWhy[RD_UNMATCHED_ABSENT], ist.unmatchedWhy[RD_UNMATCHED_FEWER],
               ist.unmatchedWhy[RD_UNMATCHED_UNPLACED], ist.unmatchedWhy[RD_UNMATCHED_PAYLOAD],
               ist.apart, ist.prevKept, ist.prevHeld);
    }
    if (list) {
        RdStateBlock st = rf->startState;
        s_listed = 0;
        rd__walk(rf, (int)rf->keep, &st, listCmd, (void *)rf);
        printf("listed %u commands of frame %u (%ux%u, fba/pabe/texa/date per action)\n", s_listed,
               rf->number, rf->gsW, rf->gsH);
    }
    if (texDir) {
        dumpTextures(texDir);
    }
    if (meshName) {
        listMesh(meshName);
    }
    int rc = 1;
    if (noDevice) {
        rd__frame_free(&f);
        rd__frame_free(&pf);
        rd_shutdown();
        return 0;
    }
    const bool replayed = rd__replay_frame(rf, (int)rf->keep, pw != 0);
    if (replayed && stats) {
        /* the record rd__perf_end just closed (rd_perf_pop hands it out only
         * once its timestamps are in, RHI_FRAMES_IN_FLIGHT replays later) */
        const RdPerfRecord *pr = &g_rdPerf;
        printf("%s: stats: %u draws, %u passes, %u bind groups (%u uniform, %u texture), %u bind "
               "group binds, %u pipeline binds, %llu ring bytes, %u barriers, %u copies; screen "
               "prims %u draws merged into %u\n",
               dump, pr->draws, pr->renderPasses, pr->bindGroups, pr->uniformGroups,
               pr->textureGroups, pr->bindGroupBinds, pr->pipelineBinds,
               (unsigned long long)pr->uploadBytes, pr->barriers, pr->copies, pr->screenCmds,
               pr->screenDraws);
    }
    if (replayed) {
        uint32_t w = 0, h = 0;
        size_t cap = pw ? (size_t)pw * ph * 4 : (size_t)4096 * 4096 * 4;
        uint8_t *px = malloc(cap);
        bool ok = px && (pw ? rd_read_presented(px, &w, &h)
                            : rd__read_target(rd_target((RdTargetId)target), px, cap, &w, &h));
        if (ok && rd_write_png(png, px, w, h, w * 4, 1)) {
            printf("%s: frame %u, %ux%u -> %s (%u commands skipped)\n", dump, f.number, w, h, png,
                   rd__not_implemented_count());
            rc = 0;
        } else {
            fprintf(stderr, "readback or PNG write failed\n");
        }
        free(px);
    }
    if (replayed && stats && rhi_timestamps_supported()) {
        /* a record's GPU times come in RHI_FRAMES_IN_FLIGHT replays later:
           replays of the same frame after the picture was read, the last
           complete record printed (a warm replay) */
        RdPerfRecord pr, last;
        bool have = false;
        memset(&last, 0, sizeof(last));
        for (int k = 0; k < RHI_FRAMES_IN_FLIGHT + 2; k++) {
            rd__replay_frame(rf, (int)rf->keep, pw != 0);
            while (rd_perf_pop(&pr)) {
                if (pr.gpuValid) {
                    last = pr;
                    have = true;
                }
            }
        }
        if (have) {
            printf("%s: gpu: %.3f ms: uploads %.3f; lists", dump, last.gpuMs, last.gpuUploadMs);
            for (int l = 0; l < RD_LIST_COUNT; l++) {
                printf(" %.3f", last.gpuListMs[l]);
            }
            printf("; effects:");
            for (int p = 0; p < RD_PERF_POST_COUNT; p++) {
                printf("%s %s %.3f", p ? "," : "", rd_perf_post_name(p), last.gpuPostMs[p]);
            }
            printf("%s; present %.3f\n", last.gpuPostPartial ? " (not all timed apart)" : "",
                   last.gpuPresentMs);
        } else {
            printf("%s: gpu: no timestamps came back\n", dump);
        }
    }
    rd__frame_free(&f);
    rd__frame_free(&pf);
    rd_shutdown();
    return rc;
}
