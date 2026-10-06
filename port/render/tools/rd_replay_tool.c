/* rd_replay_tool: loads an rd frame dump (rd_DumpFrame) and renders it
 * headless to a PNG, for the renderer verification plan (backend against
 * backend, before against after).
 *
 *   rd_replay_tool <dump> <out.png> [--target NAME] [--present WxH]
 *                  [--backend vulkan|d3d12]
 *
 * NAME is a named target (SCENE, DISPLAY (default), SHADOW0..2, WORK0..3,
 * AA0, AA1, FEED128, and since wave 5 AURA_WORK, AURA_TAP, WORK2_PAD).  --present renders the Original presenter into a
 * W x H output and writes that instead.  Commands of later waves (meshes,
 * fog, ...) are skipped with a message instead of stopping.
 *
 * --backend picks the RHI backend (renderer wave 6, R6c; default: the
 * build's default, port/rhi/rhi.h rhi_CreateBackend), so the same dump can
 * be rendered on Vulkan and D3D12 and the PNGs compared
 * (docs/port/TESTING.md).
 *
 * The display options (renderer wave 7, R7a; RENDER_API.md "Presets and display options"): a
 * dump does not carry them, so the replay takes them here, the Original
 * preset by default:
 *   --enhanced            the Enhanced preset (needed by the four below)
 *   --aspect A            4:3 (default), 16:10, 16:9 or a number (w / h)
 *   --resolution R        the scene's resolution: WxH or Nx (default: the
 *                         --present box, else the GS size)
 *   --full-height         the full-height scene
 *   --filter F            original, trilinear or anisotropic
 *   --mirror              the mirror mode (R7c, section 21): UI prims
 *                         flipped at replay, the present flipped (any preset)
 *   --quad-text           (package DEF) registers no deferred text renderer:
 *                         an Enhanced present draws the menu rows' glyph
 *                         quads into SCENE as the Original preset does,
 *                         instead of their RDC_OVERLAY_TEXT items on the
 *                         output (a before/after pair from one dump).  By
 *                         default the tool installs port/ui/font.c's
 *                         renderer (ui_InstallDeferredText)
 *   --crt MODE            (with --present; package CRT) the CRT filter in
 *                         MODE (scanlines, consumer, trinitron, pvm) at
 *                         full strength, the modes' own parameters
 *                         (DISPLAY.md "CRT filter"); --crt-strength K (0..1,
 *                         after it) sets the strength
 *   --overlay-test        (with --present) registers a presentation overlay
 *                         (package OV, rd.h rd_SetPresentOverlay) drawing a
 *                         test pattern after the box blit: a one-pixel white
 *                         outline on the box's edge and a 32 x 32 square,
 *                         opaque red, 16 pixels in from the box's top-left
 *                         corner, plus a half-transparent white one beside
 *                         it.  Without it the tool registers no overlay
 *
 * Inspection (P2):
 *   --list                prints every command of the replayed lists: the
 *                         list, index, type and key; for a draw its texture
 *                         (id and size), and for screen prims the prim,
 *                         space, vertex count, the bounding box in GS pixels
 *                         and the texel rectangle (UV, or STQ times the size)
 *   --nop L:A[-B]         turns commands A..B of list L into NOPs before the
 *                         replay (repeatable), to find the draw behind a pixel
 *   --mesh NAME           prints the VU meshes of that name vertex by vertex
 *                         (stream quadwords; '*' where the static index list
 *                         draws the triangle ending at the vertex)
 *   --dump-textures DIR   writes every image texture of the dump to
 *                         DIR/tex-<id>-<w>x<h>.png as decoded (RGBA8, the
 *                         alpha byte as stored: GS 0x80 = 1.0; an R8
 *                         texture as grey, its byte in each channel)
 *   --stats               (package PA) prints the replay's counts from its
 *                         performance record (rd.h RdPerfRecord): draws,
 *                         passes, bind groups created (rd's uniform and
 *                         texture groups apart), bind group and pipeline
 *                         binds, ring bytes, (package PB) pipeline
 *                         barriers and copies
 *   --no-aa1              (package AA1) replays with PRIM.AA1 off: every
 *                         RDC_AA1 a NOP and the start state's bit clear, the
 *                         frame as the renderer drew it before AA1 was
 *                         decoded (a before/after pair from one dump)
 *
 * Interpolation (package I1; RENDER_API.md "Frame rate and interpolation"):
 *   --interp T PREV       replays the frame the presenter builds between the
 *                         dump PREV (the tick before, e.g. the game's
 *                         rd-NNNNN-prev.rddump) and <dump> at alpha T (0..1)
 *                         instead of <dump> itself: rd__InterpFrame as a
 *                         tick's first present (dt 1); the two must be
 *                         consecutive frames.  The meshes' kept versions are
 *                         not in a dump: each frame's own mesh is its stream
 *
 * Exit: 0 written, 1 error, 77 no device or no dump file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_mesh.h"
#include "font.h" /* port/ui: the deferred text renderer (package DEF) */

static const char *const kNames[] = {
    "SCENE", "DISPLAY", "SHADOW0", "SHADOW1", "SHADOW2", "WORK0",     "WORK1",    "WORK2",
    "WORK3", "AA0",     "AA1",     "FEED128", "",        "AURA_WORK", "AURA_TAP", "WORK2_PAD"};

static const char *const kCmdNames[RDC_COUNT] = {
    "NOP",          "TEST",           "BLEND",     "ABE",         "ZWRITE",       "FBA",
    "PABE",         "COLCLAMP",       "TEXA",      "FILTER",      "WRAP",         "TEXTURE",
    "TEXTURE_OFF",  "UVOFFSET",       "COLORMASK", "TARGET",      "SCISSOR",      "ALPHA",
    "SHADE",        "CLEAR",          "SCREEN",    "EXACT_BLEND", "COPY",         "MESH",
    "SKINNED",      "GRID",           "PARTICLES", "WORLD_PRIMS", "SHADOW_STRIP", "POST_STUB",
    "SHADOW_RESET", "SHADOW_RESOLVE", "AA1",       "OVERLAY_TEXT"};

static const char *const kPrimNames[] = {"points",   "lines",  "linestrip", "tris",
                                         "tristrip", "trifan", "sprites"};

static void listCmd(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *st)
{
    const RdFrame *f = user;
    printf("%2d:%-5u %-14s key %08x%08x", list, index,
           c->type < RDC_COUNT ? kCmdNames[c->type] : "?", c->keyHi, c->keyLo);
    if (c->type == RDC_TEXTURE) {
        const RdTexRec *t = rd__TexRec(c->u[0]);
        printf(" tex %u %ux%u fn %u tcc %u", c->u[0], t ? t->w : 0, t ? t->h : 0, c->b[0], c->b[1]);
    } else if (c->type == RDC_FILTER || c->type == RDC_WRAP) {
        printf(" %u %u", c->b[0], c->b[1]);
    } else if (c->type == RDC_SCREEN &&
               c->u[0] + (uint64_t)c->u[1] * sizeof(RdScreenVtx) <= f->payloadSize) {
        const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + c->u[0]);
        const RdTexRec *t = st->ds.texEnabled ? rd__TexRec(st->tex) : NULL;
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
        printf(" post %u rgba %u,%u,%u,%u fix %u", c->b[0], r.rgba[0], r.rgba[1], r.rgba[2],
               r.rgba[3], r.fix);
    } else if (c->type >= RDC_MESH && c->type <= RDC_PARTICLES) {
        const RdMeshRec *m = rd__MeshRec(c->u[0]);
        if (m) {
            printf(" mesh %s (%u vertices, %u batches)", m->name, m->vertexCount, m->batchCount);
        }
        if (st->ds.texEnabled) {
            printf(" tex %u", st->tex);
        }
    }
    if (c->type >= RDC_CLEAR) {
        const RdTestState *t = &st->ds.test;
        printf(" | ate %u atst %u aref %u afail %u zte %u ztst %u zwrite %u abe %u blend %u fix %u",
               t->ate, t->atst, t->aref, t->afail, t->zte, t->ztst, st->ds.zwrite, st->ds.abe,
               st->ds.blend, st->ds.blendFix);
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
                bool drawn = false;
                for (uint32_t k = 0; k < br->indexCount; k++) {
                    drawn |= m->index[br->firstIndex + k] / 4 == v;
                }
                printf("  %4u %c", v, drawn ? '*' : ' ');
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
        if (t->format == RD_TEXEL_R8) {
            /* package R8: coverage as grey */
            uint8_t *grey = malloc((size_t)t->w * t->h * 4);
            for (size_t k = 0; grey && k < (size_t)t->w * t->h; k++) {
                grey[k * 4] = grey[k * 4 + 1] = grey[k * 4 + 2] = t->pixels[k];
                grey[k * 4 + 3] = 0xFF;
            }
            if (grey && rd_WritePng(path, grey, t->w, t->h, t->w * 4, 0)) {
                printf("%s\n", path);
            }
            free(grey);
        } else if (rd_WritePng(path, t->pixels, t->w, t->h, t->w * 4, 0)) {
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
        *w = (uint32_t)b[24] | ((uint32_t)b[25] << 8) | ((uint32_t)b[26] << 16) |
             ((uint32_t)b[27] << 24);
        *h = (uint32_t)b[28] | ((uint32_t)b[29] << 8) | ((uint32_t)b[30] << 16) |
             ((uint32_t)b[31] << 24);
    }
    return ok;
}

/* --overlay-test: rd_OverlayPrims sprites in 12.4 output pixels */
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
    rd_OverlayPrims(RD_PRIM_SPRITES, v, 2, (RdTex){0}, RD_BLEND_LERP_AS);
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
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(
            stderr,
            "usage: %s <dump> <out.png> [--target NAME] [--present WxH] [--enhanced] "
            "[--aspect A] [--resolution WxH|Nx] [--full-height] [--filter F] "
            "[--mirror] [--overlay-test] [--backend vulkan|d3d12] [--list] [--nop L:A[-B]] "
            "[--mesh NAME] [--dump-textures DIR] [--no-aa1] [--stats] [--interp T PREV] [--quad-text]\n"
            "       [--crt scanlines|consumer|trinitron|pvm [--crt-strength K]]\n",
            argv[0]);
        return 1;
    }
    const char *dump = argv[1], *png = argv[2];
    int target = RD_TARGET_DISPLAY;
    uint32_t pw = 0, ph = 0;
    /* R7a: the display options */
    RdSettings s;
    bool list = false, overlay = false, noAa1 = false, stats = false, quadText = false;
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
            if (!rhi_CreateBackend(argv[++i])) {
                fprintf(stderr, "backend %s is not in this build\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "--enhanced") == 0) {
            s.preset = RD_PRESET_ENHANCED;
        } else if (strcmp(argv[i], "--full-height") == 0) {
            s.fullHeightScene = 1;
        } else if (strcmp(argv[i], "--mirror") == 0) {
            s.mirror = 1; /* R7c */
        } else if (strcmp(argv[i], "--aspect") == 0 && i + 1 < argc) {
            unsigned a = 0, b = 0;
            const char *v = argv[++i];
            if (sscanf(v, "%u:%u", &a, &b) == 2 && a && b) {
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
        } else if (strcmp(argv[i], "--quad-text") == 0) {
            quadText = true;
        } else if (strcmp(argv[i], "--crt") == 0 && i + 1 < argc) {
            /* package CRT: the CRT filter's mode, at full strength */
            static const char *const modes[] = {"scanlines", "consumer", "trinitron", "pvm"};
            const char *v = argv[++i];
            int m = -1;
            for (int k = 0; k < 4; k++) {
                if (strcmp(v, modes[k]) == 0) {
                    m = k;
                }
            }
            if (m < 0) {
                fprintf(stderr, "bad --crt (scanlines, consumer, trinitron or pvm)\n");
                return 1;
            }
            rd_CrtSettings(&s, (RdCrtMode)(m + 1), s.crtMode ? s.crtStrength : 1.0f);
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
    if (!rd_Init(gw, gh, &s, NULL)) {
        fprintf(stderr, "no usable %s device\n",
                rhi_Backend() == RHI_BACKEND_D3D12 ? "D3D12" : "Vulkan");
        return 77;
    }
    rd__SetNotImplementedFatal(false);
    if (overlay) {
        rd_SetPresentOverlay(overlayTest, NULL);
    }
    /* package DEF: the menu rows' items at the output's resolution in an
       Enhanced present */
    ui_InstallDeferredText(!quadText);
    RdFrame pf, f;
    memset(&pf, 0, sizeof(pf));
    if (interpPrev && !rd__LoadFrame(interpPrev, &pf)) {
        rd_Shutdown();
        return 1;
    }
    if (!rd__LoadFrame(dump, &f)) {
        rd__FrameFree(&pf);
        rd_Shutdown();
        return 1;
    }
    for (int k = 0; k < nopCount; k++) {
        const RdCmdList *cl = &f.lists[nops[k].l];
        for (uint32_t c = nops[k].a; c <= nops[k].b && c < cl->count; c++) {
            if (!rd__CmdIsState(cl->cmds[c].type)) {
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
        /* I1: the presenter's frame between the two */
        RdInterpStats ist;
        rf = rd__InterpFrame(&pf, &f, interpT, 1.0f, 1, &ist);
        if (!rf) {
            fprintf(stderr, "rd__InterpFrame failed\n");
            rd__FrameFree(&pf);
            rd__FrameFree(&f);
            rd_Shutdown();
            return 1;
        }
        printf("interp %u -> %u at %g: snap %u, %u keyed draws: %u blended, %u unmatched, %u "
               "mismatched, %u jumped; %u mesh streams blended; %u blended as rotations\n",
               pf.number, f.number, (double)interpT, ist.snap, ist.keyed, ist.lerped, ist.missing,
               ist.mismatch, ist.jump, ist.morph, ist.rotated);
    }
    if (list) {
        RdStateBlock st = rf->startState;
        rd__Walk(rf, (int)rf->keep, &st, listCmd, (void *)rf);
    }
    if (texDir) {
        dumpTextures(texDir);
    }
    if (meshName) {
        listMesh(meshName);
    }
    int rc = 1;
    const bool replayed = rd__ReplayFrame(rf, (int)rf->keep, pw != 0);
    if (replayed && stats) {
        /* the record rd__PerfEnd just closed (rd_PerfPop hands it out only
         * once its timestamps are in, RHI_FRAMES_IN_FLIGHT replays later) */
        const RdPerfRecord *pr = &g_rdPerf;
        printf("%s: stats: %u draws, %u passes, %u bind groups (%u uniform, %u texture), %u bind "
               "group binds, %u pipeline binds, %llu ring bytes, %u barriers, %u copies\n",
               dump, pr->draws, pr->renderPasses, pr->bindGroups, pr->uniformGroups,
               pr->textureGroups, pr->bindGroupBinds, pr->pipelineBinds,
               (unsigned long long)pr->uploadBytes, pr->barriers, pr->copies);
    }
    if (replayed) {
        uint32_t w = 0, h = 0;
        size_t cap = pw ? (size_t)pw * ph * 4 : (size_t)4096 * 4096 * 4;
        uint8_t *px = malloc(cap);
        bool ok = px && (pw ? rd_ReadPresented(px, &w, &h)
                            : rd__ReadTarget(rd_Target((RdTargetId)target), px, cap, &w, &h));
        if (ok && rd_WritePng(png, px, w, h, w * 4, 1)) {
            printf("%s: frame %u, %ux%u -> %s (%u commands skipped)\n", dump, f.number, w, h, png,
                   rd__NotImplementedCount());
            rc = 0;
        } else {
            fprintf(stderr, "readback or PNG write failed\n");
        }
        free(px);
    }
    rd__FrameFree(&f);
    rd__FrameFree(&pf);
    rd_Shutdown();
    return rc;
}
