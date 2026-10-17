/* rd_replay_tool: loads an rd frame dump (rd_dump_frame) and renders it
 * headless to a PNG, for the renderer verification plan (backend against
 * backend, before against after).
 *
 *   rd_replay_tool <dump> <out.png> [--target NAME] [--present WxH]
 *                  [--pass-dump DIR] [--backend vulkan|d3d12]
 *
 * <dump> is an .rddump, or the .zip F12 writes: its first .rddump entry is
 * taken (extracted to a temporary file in the temp folder, deleted at exit).
 *
 * NAME is a named target (SCENE, DISPLAY (default), SHADOW0..2, WORK0..3,
 * AA0, AA1, FEED128, AURA_WORK, AURA_TAP, WORK2_PAD, FEED_HELD,
 * DISPLAY_HELD); an unknown name is an error (exit 1, the names listed).
 * --pass-dump DIR renders the frame once and writes every one of those
 * targets to DIR/<NAME>.png (DIR must exist; <out.png> is unused, give -),
 * and with --present WxH the presented picture to DIR/PRESENT.png (a second
 * replay in a fresh context, so it equals a run of its own); a target that
 * cannot be read back is named on stderr and the exit is 1, the others are
 * still written.  --present renders the Original presenter into a W x H
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
 *   --aspect A            the dump's own aspect (default; 4:3 before dump
 *                         version 10), 16:10, 16:9, 21:9 (64/27), 32:9, 48:9
 *                         (the widest the game allows is 20:3) or a number
 *                         (w / h).  A dump holds the picture as drawn at
 *                         its aspect: a narrower A crops it (the --present
 *                         picture is the recorded one cut to the box of A,
 *                         the centre columns; a target is the whole
 *                         recorded frame), a wider A is an error ("a dump
 *                         drawn at 4:3 cannot show 16:9: capture an aspect
 *                         sweep"); a dump before version 10 is a 4:3 dump
 *   --resolution R        the scene's resolution: WxH or Nx (default: the
 *                         --present box with --enhanced, else the GS size)
 *   --full-height         the full-height scene
 *   --full-pixel          the reduction draws the whole frame: no black border
 *   --border-mask         the game's Full pixel off: the same picture, the
 *                         border masked over it at present (rd__border_mask)
 *   --water-sharp         Water reflections: Sharp: the dump's render-to-texture
 *                         blocks that are not scene-sized (the reflections in
 *                         water, the queen barrier's copy) at the scene's scale
 *                         (rd_core.c rd__target_scale_of); without it they are
 *                         drawn at their GS size, the PS2's.  A dump from before
 *                         the option marks no block and replays as without it
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
 *                         and is drawn in front of it; then 'j'
 *                         where its strip is joined to another strip of
 *                         the mesh, ICO_VU_MARK_JOINED: not a card)
 *   --dump-textures DIR   writes every image texture of the dump to
 *                         DIR/tex-<id>-<w>x<h>.png as decoded (RGBA8, the
 *                         alpha byte as stored: GS 0x80 = 1.0; an R8
 *                         texture as grey, its byte in each channel), and
 *                         a texture's GS levels (RdTexRec.gsChain) as
 *                         tex-<id>-<w>x<h>-gslevel<N>.png
 *   --stats               prints the replay's counts from its performance
 *                         record (rd.h RdPerfRecord): draws, passes, bind
 *                         groups created (rd's uniform and texture groups
 *                         apart), bind group and pipeline binds, ring bytes,
 *                         pipeline barriers and copies, the screen-prim
 *                         draws one per command and merged; a second line:
 *                         the exact blend's destination-read draws, storage
 *                         passes and pass switches, the glow's passes, the
 *                         target snapshots copied and swapped, the draws
 *                         through the above-1x kernels (rd.h
 *                         RdPerfRecord); a third: the VU vertex pre-pass,
 *                         on or off, its passes, vertices and draws and the
 *                         mesh and grid draws drawn the old way; on a device
 *                         with GPU timestamps, after the picture is written,
 *                         the frame is replayed RHI_FRAMES_IN_FLIGHT + 2
 *                         more times and the last replay's GPU times are
 *                         printed: the uploads, each list, each picture
 *                         effect (rd.h RD_PERF_POST_*, inside the lists'
 *                         times), the present and the VU pre-pass
 *   --pass-times          (implies --stats) every replay timed pass by pass
 *                         ([dev] perf_passes, rd_perf.c): after the gpu line
 *                         the warm replay's passes, one line each ("pass N:
 *                         <label> <target> WxH, list L, D draws, B
 *                         pipeline binds, GPU <us> us, CPU <us> us"), then
 *                         the window's top-passes text over that replay and
 *                         its passes by kind
 *   --prev DUMP           (repeatable, oldest first) replays DUMP in the same
 *                         context before <dump>, so the targets a frame
 *                         reads back from the frame before (the motion
 *                         blur's previous picture, FEED128) hold the run's
 *                         pictures instead of zero; give the dumps of the
 *                         ticks just before <dump>
 *   --no-half             every TARGET command without the field's half
 *                         pixel (RD_TARGET_HALF_Y): the frame as the flip
 *                         of the other field draws it
 *   --no-aa1              replays with PRIM.AA1 off: every RDC_AA1 a NOP and
 *                         the start state's bit clear, the frame without
 *                         the GS's edge antialiasing (an on/off pair from
 *                         one dump)
 *
 * The loss bisect (docs/port/RENDER.md, "The loss bisect"):
 *   --loss-bisect         replays the frame as the game's next start after a
 *                         device loss does (rd.h rd_loss_bisect: one pass
 *                         longer each time, waiting for the GPU after each)
 *                         and prints what it found; <out.png> is unused (give
 *                         -).  Exit 0 when it ran (lost or not), 1 when it
 *                         could not
 *   --passes A[-B]        replays passes A..B only (rd_internal.h
 *                         rd__replay_set_window: the draws and copies of the
 *                         other passes left out, the walk stopped where pass
 *                         B + 1 begins); A alone is pass A alone
 *   --draws A[-B]         with --passes: in the window's last pass only its
 *                         draws A..B (numbered as the pass asks for them;
 *                         rd__replay_set_window_draws, the second level of
 *                         the loss bisect); A alone is draw A alone
 *   --mask-marks          every VU draw with its triangles' marks left out
 *                         (the second level's MASK_MARKS)
 *   With --loss-bisect, --passes P and --draws or --mask-marks make one step
 *   of the second level instead (rd.h rd_loss_bisect_step: passes 1..P - 1
 *   and pass P's draws through the window, then a wait for the GPU) and
 *   print its verdict and the note of the last draw it recorded
 *
 * Interpolation:
 *   --interp T PREV       replays the frame the presenter builds between the
 *                         dump PREV (the tick before, e.g. the game's
 *                         rd-NNNNN-prev.rddump) and <dump> at alpha T (0..1)
 *                         instead of <dump> itself: rd__interp_frame as a
 *                         tick's first present; the two must be
 *                         consecutive frames.  The meshes' kept versions are
 *                         not in a dump: each frame's own mesh is its stream
 *   --interp-any          (with --interp) PREV is taken for the tick right
 *                         before <dump> whatever their frame numbers (dumps
 *                         saved some ticks apart, as the F12 sets are): no
 *                         gap snap; the other snaps (a camera past the cut
 *                         thresholds) still apply
 *   --no-reproject        (with --interp) the draws the frame camera
 *                         projected on the CPU (lightning, lines, shadow
 *                         volumes) blend in screen space instead of being
 *                         seen through the blended camera
 *                         (rd__interp_set_reproject): the before of a
 *                         before/after pair from one build
 *
 * Exit: 0 written (or, with --no-device, listed), 1 error, 77 no device or no dump file. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../include/ico_endian.h"
#include "dump_zip.h" /* port/platform: the F12 zip */
#include "rd_internal.h"
#include "rd_mesh.h"
#include "../vu1_ref/vu1_ref.h"          /* --vu-ref: the CPU mirror of the VU programs */
#include "../../shaders/shader_consts.h" /* ICO_VU_MARK_*: the marks --mesh shows */
#include "font.h"                        /* port/ui: the overlay test's text */

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
    if (t->gsLevels > 1) {
        printf(" gs levels %u", t->gsLevels);
    }
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
        if (c->type == RDC_FILTER && c->u[0]) {
            /* TEX1's mipmap fields (rd_tex1_lod) */
            printf(" mmin %u mxl %u lcm %u l %u k %d", RD_TEX1_MMIN(c->u[0]), RD_TEX1_MXL(c->u[0]),
                   RD_TEX1_LCM(c->u[0]), RD_TEX1_L(c->u[0]), (int)RD_TEX1_K(c->u[0]));
        }
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
        if (c->type == RDC_GRID && c->b[5]) {
            /* pool.c's ST formula (rd.h rd_grid_screen_st) and its scales */
            printf(" screen st %u (%g, %g)", c->b[5], (double)c->f[0], (double)c->f[1]);
        }
        if (c->type == RDC_MESH && c->b[5]) {
            /* a clip plane in the model's space (rd_chain.h, Natural chains) */
            printf(" plane %u %g", c->b[5], (double)c->f[0]);
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
                bool drawn = false, later = false, joined = false;
                for (uint32_t k = 0; k < br->indexCount; k++) {
                    const uint32_t ix = m->index[br->firstIndex + k];
                    const uint32_t mk = rd__mesh_kick_mark(m, ix / 4);
                    drawn |= ix / 4 == v;
                    later |= ix / 4 == v && (mk & ICO_VU_MARK_LATER) != 0;
                    joined |= ix / 4 == v && (mk & ICO_VU_MARK_JOINED) != 0;
                }
                printf("  %4u %c%c", v, later ? '^' : drawn ? '*' : ' ', joined ? 'j' : ' ');
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
        } else if (rd_write_png(path, t->pixels, t->w, t->h, t->w * 4, 1)) {
            printf("%s\n", path);
        }
        /* the GS levels (gsChain: levels 1.. as RGBA8, each half the one before) */
        const uint8_t *lv = t->gsLevels > 1 ? t->gsChain : NULL;
        uint32_t w = t->w, h = t->h;
        for (uint32_t l = 1; lv && l < t->gsLevels; l++) {
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
            snprintf(path, sizeof(path), "%s/tex-%u-%ux%u-gslevel%u.png", dir, id, t->w, t->h, l);
            if (rd_write_png(path, lv, w, h, w * 4, 1)) {
                printf("%s\n", path);
            }
            lv += (size_t)w * h * 4;
        }
    }
}

static char s_zipTemp[1100];

static void removeZipTemp(void)
{
    if (s_zipTemp[0] != '\0') {
        remove(s_zipTemp);
    }
}

/* a .zip given as the dump: its .rddump goes to a temporary file (the
   loader takes a path) and *dump names that file; false with a message when
   it cannot */
static bool unzipDump(const char **dump)
{
    const char *path = *dump;
    const size_t n = strlen(path);

    if (n < 4 || (strcmp(path + n - 4, ".zip") != 0 && strcmp(path + n - 4, ".ZIP") != 0)) {
        return true;
    }
    size_t size = 0;
    void *data = ico_dump_zip_extract(path, &size, NULL, 0);

    if (data == NULL) {
        fprintf(stderr, "%s: no .rddump in this zip\n", path);
        return false;
    }
    const char *dir = getenv("TMPDIR");

    if (dir == NULL) {
        dir = getenv("TEMP");
    }
    if (dir == NULL) {
        dir = getenv("TMP");
    }
    if (dir == NULL) {
        dir = "/tmp";
    }
    snprintf(s_zipTemp, sizeof(s_zipTemp), "%s/rd_replay_%u.rddump", dir,
             (unsigned)(uintptr_t)data & 0xFFFFFFu);
    FILE *fp = fopen(s_zipTemp, "wb");
    const bool ok = fp != NULL && fwrite(data, 1, size, fp) == size;

    if (fp != NULL && fclose(fp) != 0) {
        s_zipTemp[0] = '\0';
        free(data);
        return false;
    }
    free(data);
    if (!ok) {
        fprintf(stderr, "%s: cannot write the temporary dump\n", s_zipTemp);
        removeZipTemp();
        s_zipTemp[0] = '\0';
        return false;
    }
    atexit(removeZipTemp);
    *dump = s_zipTemp;
    return true;
}

#define kAspect43 (4.0f / 3.0f)

/* an aspect as the options name it (16:9) or its number */
static const char *aspectName(float a, char buf[16])
{
    static const struct {
        float v;
        const char *n;
    } known[] = {{4.0f / 3.0f, "4:3"},    {16.0f / 10.0f, "16:10"}, {16.0f / 9.0f, "16:9"},
                 {64.0f / 27.0f, "21:9"}, {32.0f / 9.0f, "32:9"},   {48.0f / 9.0f, "48:9"},
                 {60.0f / 9.0f, "60:9"}};

    for (size_t k = 0; k < sizeof(known) / sizeof(known[0]); k++) {
        if (fabsf(a - known[k].v) < 1e-3f) {
            snprintf(buf, 16, "%s", known[k].n);
            return buf;
        }
    }
    snprintf(buf, 16, "%.3f", (double)a);
    return buf;
}

/* a named target's picture to path (--target, --pass-dump) */
static bool writeTarget(int k, const char *path)
{
    const size_t cap = (size_t)4096 * 4096 * 4;
    static uint8_t *px;
    uint32_t w = 0, h = 0;

    if (!px) {
        px = malloc(cap);
    }
    return px && rd__read_target(rd_target((RdTargetId)k), px, cap, &w, &h) &&
           rd_write_png(path, px, w, h, w * 4, 1);
}

/* The presented picture (an ow x oh output) to path as a pw x ph picture.
 * When the output is the whole picture (ow x oh = pw x ph) that is the
 * readback; when --aspect asked for less than the dump holds (a crop) the
 * output is the recorded aspect's box at the requested box's height, and the
 * picture is the centre columns of it that the requested box (want, in the
 * pw x ph picture) covers, on black. */
static bool writePresent(const char *path, uint32_t ow, uint32_t oh, RhiRect want, uint32_t pw,
                         uint32_t ph)
{
    uint32_t w = 0, h = 0;
    uint8_t *px = malloc((size_t)ow * oh * 4);
    bool ok = px && rd_read_presented(px, &w, &h) && w == ow && h == oh;

    if (ok && (ow != pw || oh != ph)) {
        uint8_t *canvas = calloc((size_t)pw * ph, 4);
        const uint32_t cx = (ow - (uint32_t)want.w) / 2;

        ok = canvas != NULL && (uint32_t)want.w <= ow && (uint32_t)want.h <= oh;
        for (uint32_t y = 0; ok && y < (uint32_t)want.h; y++) {
            memcpy(canvas + (((size_t)want.y + y) * pw + (size_t)want.x) * 4,
                   px + ((size_t)y * ow + cx) * 4, (size_t)want.w * 4);
        }
        for (size_t k = 3; ok && k < (size_t)pw * ph * 4; k += 4) {
            canvas[k] = 0xFF;
        }
        ok = ok && rd_write_png(path, canvas, pw, ph, pw * 4, 1);
        free(canvas);
    } else if (ok) {
        ok = rd_write_png(path, px, w, h, w * 4, 1);
    }
    free(px);
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

/* the replay's frame edits and the interpolation (prepareFrame) */
static struct {
    unsigned l, a, b;
} nops[64];

static int nopCount;
static bool noAa1, interpAny;
static const char *interpPrev;
static float interpT = 1.0f;
/* --prev DUMP (repeatable, oldest first): frames replayed in this context
 * before <dump>, so the retained targets (the motion blur's previous
 * picture, FEED128, keep) hold what the game's run left there */
static const char *prevDumps[16];
static int prevCount;
static bool noHalf;

/* --no-half: every TARGET command without the field's half pixel
   (RD_TARGET_HALF_Y), as a flip in the other field sends the frame */
static void dropHalf(RdFrame *f)
{
    for (int l = 0; noHalf && l < RD_LIST_COUNT; l++) {
        for (uint32_t c = 0; c < f->lists[l].count; c++) {
            if (f->lists[l].cmds[c].type == RDC_TARGET) {
                f->lists[l].cmds[c].b[0] &= (uint8_t)~RD_TARGET_HALF_Y;
            }
        }
    }
}

/* replays the --prev dumps in order; false on a load failure */
static bool replayPrev(void)
{
    for (int k = 0; k < prevCount; k++) {
        RdFrame q;
        memset(&q, 0, sizeof(q));
        if (!rd__load_frame(prevDumps[k], &q)) {
            fprintf(stderr, "%s: --prev load failed\n", prevDumps[k]);
            return false;
        }
        dropHalf(&q);
        const bool ok = rd__replay_frame(&q, (int)q.keep, false);
        rhi_wait_frame();
        printf("prev %s: frame %u replayed%s\n", prevDumps[k], q.number, ok ? "" : " (failed)");
        rd__frame_free(&q);
    }
    return true;
}

/* Loads <dump> (and the --interp PREV) into the current context, applies
 * --nop and --no-aa1, and returns the frame to replay: <dump> itself, or the
 * interpolated frame.  NULL after shutting the renderer down on failure.
 * Called again after a re-init (--pass-dump's present pass). */
static const RdFrame *prepareFrame(const char *dump, RdFrame *pf, RdFrame *f)
{
    memset(pf, 0, sizeof(*pf));
    if (interpPrev && !rd__load_frame(interpPrev, pf)) {
        rd_shutdown();
        return NULL;
    }
    if (!rd__load_frame(dump, f)) {
        rd__frame_free(pf);
        rd_shutdown();
        return NULL;
    }
    for (int k = 0; k < nopCount; k++) {
        const RdCmdList *cl = &f->lists[nops[k].l];
        for (uint32_t c = nops[k].a; c <= nops[k].b && c < cl->count; c++) {
            if (!rd__cmd_is_state(cl->cmds[c].type)) {
                cl->cmds[c].type = RDC_NOP;
            }
        }
    }
    dropHalf(f);
    if (noAa1) {
        f->startState.aa1 = 0;
        for (int l = 0; l < RD_LIST_COUNT; l++) {
            for (uint32_t c = 0; c < f->lists[l].count; c++) {
                if (f->lists[l].cmds[c].type == RDC_AA1) {
                    f->lists[l].cmds[c].type = RDC_NOP;
                }
            }
        }
    }
    const RdFrame *rf = f;
    if (interpPrev) {
        /* the presenter's frame between the two */
        RdInterpStats ist;
        if (interpAny) {
            pf->number = f->number - 1; /* the tick before, whatever was saved between */
        }
        rf = rd__interp_frame(pf, f, interpT, 1, &ist);
        if (!rf) {
            fprintf(stderr, "rd__interp_frame failed\n");
            rd__frame_free(pf);
            rd__frame_free(f);
            rd_shutdown();
            return NULL;
        }
        printf("interp %u -> %u at %g: snap %u, %u keyed draws: %u blended, %u unmatched, %u "
               "mismatched, %u jumped; %u mesh streams blended; %u blended as rotations; %u lights "
               "re-paired; %u paired by place; %u grid STs blended, %u through the blended "
               "camera\n",
               pf->number, f->number, (double)interpT, ist.snap, ist.keyed, ist.lerped, ist.missing,
               ist.mismatch, ist.jump, ist.morph, ist.rotated, ist.lightPaired, ist.placed,
               ist.gridSt, ist.gridStCamera);
        printf("interp: unmatched %u not drawn before, %u fewer before, %u left by the place "
               "pairing, %u payload; %u paired with another place; %u of the tick before kept, "
               "%u held; %u CPU-projected draws re-projected; %u draws turned the long way, "
               "%u kept at the tick's pose for their turn\n",
               ist.unmatchedWhy[RD_UNMATCHED_ABSENT], ist.unmatchedWhy[RD_UNMATCHED_FEWER],
               ist.unmatchedWhy[RD_UNMATCHED_UNPLACED], ist.unmatchedWhy[RD_UNMATCHED_PAYLOAD],
               ist.apart, ist.prevKept, ist.prevHeld, ist.reprojected, ist.longWay, ist.turnSnap);
    }
    return rf;
}

/* --vu-ref L:I (a developer's probe): the VU mirror's vertices of the
 * prelit mesh draw I of list L at this frame's matrices: GS X, Y (12.4 /
 * 16), Z, the clip w, the scissor loop's clip flags, the region test's
 * inside bit and ADC; then the fans SCISSOR_COMMON cut. */
static void vuRefDraw(const RdFrame *f, int l, uint32_t i)
{
    if (l < 0 || l >= RD_LIST_COUNT || i >= f->lists[l].count) {
        printf("vu-ref: no command %d:%u\n", l, i);
        return;
    }
    const RdCmd *c = &f->lists[l].cmds[i];
    const RdMeshRec *m = c->type == RDC_MESH ? rd__mesh_rec(c->u[0]) : NULL;
    if (!m || !m->stream || m->qwPerVertex != 3 || c->b[0] != 0 || c->u[1] > f->payloadSize ||
        sizeof(RdVuPayload) > f->payloadSize - c->u[1]) {
        printf("vu-ref: %d:%u is not a prelit mesh draw\n", l, i);
        return;
    }
    RdVuPayload p;
    memcpy(&p, f->payload + c->u[1], sizeof(p));
    const float *mem = (const float *)(const void *)(f->payload + c->u[1] + sizeof(p));
    static Vu1Ref r;
    vu1ref_init(&r);
    vu1ref_load_common(&r, (const float (*)[4])mem);
    vu1ref_normal_set_matrix(&r, (const float (*)[4])(mem + 16 * 4));
    static float in[VU_BATCH_MAX + 1][4];
    static VuBatchOut out;
    printf("vu-ref %d:%u mesh %s code %u clip %u batches %u+%u\n", l, i, m->name, p.code, p.clip,
           p.firstBatch, p.batchCount);
    for (uint32_t b = p.firstBatch; b < p.firstBatch + p.batchCount && b < m->batchCount; b++) {
        const uint32_t first = m->batches[b].firstVertex, n = m->batches[b].vertexCount;
        if (n > VU_BATCH_MAX || first + n > m->vertexCount) {
            continue;
        }
        memset(in, 0, sizeof(in));
        uint32_t tag[4] = {n | 0x8000u, 0x10000000u, 0x412u,
                           0}; /* NLOOP n, EOP; the regs as pac packs them */
        memcpy(in[0], tag, 16);
        memcpy(in[1], m->stream[(size_t)first * 3], (size_t)n * 3 * 16);
        vu1ref_normal_c(&r, (int)p.code, (const float (*)[4])in, &out);
        printf(" batch %u: %u vertices in, %d out, %d fans\n", b, n, out.count, out.fanCount);
        for (int v = 0; v < out.count; v++) {
            const VuGsVertex *g = &out.v[v];
            printf(
                "  v%-3d x %9.3f y %9.3f z %10d w %10.3f flags 0x%02x inside %d adc %d stq %g %g %g\n",
                v, g->xyz[0] / 16.0, g->xyz[1] / 16.0, g->xyz[2], (double)g->clipW, g->clipFlags,
                g->inside, vu_gs_adc(g), (double)g->stq[0], (double)g->stq[1], (double)g->stq[2]);
        }
        /* the kicked triangles that contain the GS point ICO_VU_REF_AT=x,y */
        const char *at = getenv("ICO_VU_REF_AT");
        double ax = 0, ay = 0;
        if (at && sscanf(at, "%lf,%lf", &ax, &ay) == 2) {
            static int kicks[VU_BATCH_MAX + VU_FANS_MAX];
            int nk = vu1ref_kicks(&out, kicks);
            for (int q = 0; q < out.fanCount && nk < VU_BATCH_MAX + VU_FANS_MAX; q++) {
                kicks[nk++] = out.fans[q].beforeVertex; /* the cut triangles, by their kick */
            }
            for (int t = 0; t < nk; t++) {
                const int k = kicks[t];
                if (k < 2 || k >= out.count) {
                    continue;
                }
                double x[3], y[3];
                for (int j = 0; j < 3; j++) {
                    x[j] = out.v[k - 2 + j].xyz[0] / 16.0;
                    y[j] = out.v[k - 2 + j].xyz[1] / 16.0;
                }
                double s0 = (x[1] - x[0]) * (ay - y[0]) - (y[1] - y[0]) * (ax - x[0]);
                double s1 = (x[2] - x[1]) * (ay - y[1]) - (y[2] - y[1]) * (ax - x[1]);
                double s2 = (x[0] - x[2]) * (ay - y[2]) - (y[0] - y[2]) * (ax - x[2]);
                if ((s0 >= 0 && s1 >= 0 && s2 >= 0) || (s0 <= 0 && s1 <= 0 && s2 <= 0)) {
                    printf(
                        "  kick v%d (v%d v%d v%d) contains (%g, %g): flags 0x%02x 0x%02x 0x%02x\n",
                        k, k - 2, k - 1, k, ax, ay, out.v[k - 2].clipFlags, out.v[k - 1].clipFlags,
                        out.v[k].clipFlags);
                    for (int j = 0; j < 3; j++) {
                        const VuGsVertex *g = &out.v[k - 2 + j];
                        printf(
                            "     v%-3d x %9.3f y %9.3f z %10d flags 0x%02x stq %g %g %g rgba %d %d %d %d\n",
                            k - 2 + j, g->xyz[0] / 16.0, g->xyz[1] / 16.0, g->xyz[2], g->clipFlags,
                            (double)g->stq[0], (double)g->stq[1], (double)g->stq[2],
                            g->rgba[0] & 255, g->rgba[1] & 255, g->rgba[2] & 255, g->rgba[3] & 255);
                    }
                }
            }
        }
        for (int k = 0; k < out.fanCount; k++) {
            const VuGsFan *fan = &out.fans[k];
            printf("  fan %d (before v%d): %d corners\n", k, fan->beforeVertex, fan->n);
            for (int v = 0; v < fan->n; v++) {
                printf("   f%-2d x %9.3f y %9.3f z %10d stq %g %g %g\n", v, fan->v[v].xyz[0] / 16.0,
                       fan->v[v].xyz[1] / 16.0, fan->v[v].xyz[2], (double)fan->v[v].stq[0],
                       (double)fan->v[v].stq[1], (double)fan->v[v].stq[2]);
            }
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s <dump> <out.png> [--target NAME] [--present WxH] [--enhanced] "
                "[--aspect A] [--resolution WxH|Nx] [--full-height] [--full-pixel] "
                "[--border-mask] [--water-sharp] [--filter F] "
                "[--mirror] [--overlay-test] [--backend vulkan|d3d12] [--list] [--nop L:A[-B]] "
                "[--mesh NAME] [--dump-textures DIR] [--no-aa1] [--stats] [--pass-times] "
                "[--pass-dump DIR] "
                "[--prev DUMP]... [--no-half] [--interp T PREV [--interp-any] [--no-reproject]]\n"
                "       [--loss-bisect] [--passes A[-B]] [--draws A[-B]] [--mask-marks]\n"
                "       [--no-device (with --list, --mesh or --dump-textures; <out.png> unused)]\n"
                "       [--crt scanlines|consumer|trinitron|pvm|shadow [--crt-strength K]]\n"
                "       [--aa off|ssaa2|ssaa4|msaa2|msaa4|msaa8 (with --enhanced)]\n",
                argv[0]);
        return 1;
    }
    const char *dump = argv[1], *png = argv[2];
    int target = RD_TARGET_DISPLAY;
    uint32_t pw = 0, ph = 0;
    /* the display options */
    RdSettings s;
    bool list = false, overlay = false, stats = false, passTimes = false;
    bool noDevice = false, aspectGiven = false;
    int vuRefL[8], vuRefCount = 0;
    uint32_t vuRefI[8];
    const char *passDir = NULL;
    const char *tempsDir = NULL; /* --dump-temps: every per-frame temp target after the replay */
    const char *texDir = NULL, *meshName = NULL;
    bool lossBisect = false, maskMarks = false;
    uint32_t drawA = 0, drawB = 0;
    unsigned passA = 0, passB = 0;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ORIGINAL;
    s.aspect = 4.0f / 3.0f;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            const char *n = argv[++i];
            target = -1;
            if (strncmp(n, "0x", 2) == 0) {
                /* a target by its id as --list prints it (a per-frame block,
                   "target 0x10014"): read back after the replay */
                target = (int)strtoul(n, NULL, 16);
            }
            for (int k = 0; k < (int)(sizeof(kNames) / sizeof(kNames[0])); k++) {
                if (kNames[k][0] != '\0' && strcmp(n, kNames[k]) == 0) {
                    target = k;
                }
            }
            if (target < 0) {
                fprintf(stderr, "unknown target \"%s\": the targets are", n);
                for (int k = 0; k < (int)(sizeof(kNames) / sizeof(kNames[0])); k++) {
                    fprintf(stderr, "%s%s", kNames[k][0] ? " " : "", kNames[k]);
                }
                fprintf(stderr, "\n");
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
        } else if (strcmp(argv[i], "--border-mask") == 0) {
            s.borderMask = 1;
        } else if (strcmp(argv[i], "--water-sharp") == 0) {
            s.waterSharp = 1;
        } else if (strcmp(argv[i], "--mirror") == 0) {
            s.mirror = 1;
        } else if (strcmp(argv[i], "--pass-dump") == 0 && i + 1 < argc) {
            passDir = argv[++i];
        } else if (strcmp(argv[i], "--dump-temps") == 0 && i + 1 < argc) {
            tempsDir = argv[++i];
        } else if (strcmp(argv[i], "--aspect") == 0 && i + 1 < argc) {
            unsigned a = 0, b = 0;
            aspectGiven = true;
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
        } else if (strcmp(argv[i], "--pass-times") == 0) {
            stats = passTimes = true;
        } else if (strcmp(argv[i], "--no-aa1") == 0) {
            noAa1 = true;
        } else if (strcmp(argv[i], "--interp-any") == 0) {
            interpAny = true;
        } else if (strcmp(argv[i], "--no-reproject") == 0) {
            rd__interp_set_reproject(false);
        } else if (strcmp(argv[i], "--no-half") == 0) {
            noHalf = true;
        } else if (strcmp(argv[i], "--prev") == 0 && i + 1 < argc && prevCount < 16) {
            prevDumps[prevCount++] = argv[++i];
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
        } else if (strcmp(argv[i], "--aa") == 0 && i + 1 < argc) {
            /* the anti-aliasing (RdSettings.antiAlias; off under the Original
               preset, an MSAA count the device lacks drawn as off) */
            static const char *const aas[RD_AA_COUNT] = {"off",   "ssaa2", "ssaa4",
                                                         "msaa2", "msaa4", "msaa8"};
            const char *v = argv[++i];
            int a = -1;
            for (int k = 0; k < RD_AA_COUNT; k++) {
                if (strcmp(v, aas[k]) == 0) {
                    a = k;
                }
            }
            if (a < 0) {
                fprintf(stderr, "bad --aa (off, ssaa2, ssaa4, msaa2, msaa4 or msaa8)\n");
                return 1;
            }
            s.antiAlias = (uint8_t)a;
        } else if (strcmp(argv[i], "--list") == 0) {
            list = true;
        } else if (strcmp(argv[i], "--no-device") == 0) {
            noDevice = true;
        } else if (strcmp(argv[i], "--loss-bisect") == 0) {
            lossBisect = true;
        } else if (strcmp(argv[i], "--draws") == 0 && i + 1 < argc) {
            const int n = sscanf(argv[++i], "%u-%u", &drawA, &drawB);
            if (n < 1 || drawA == 0 || (n == 2 && drawB < drawA)) {
                fprintf(stderr, "bad --draws\n");
                return 1;
            }
            if (n == 1) {
                drawB = drawA;
            }
        } else if (strcmp(argv[i], "--mask-marks") == 0) {
            maskMarks = true;
        } else if (strcmp(argv[i], "--passes") == 0 && i + 1 < argc) {
            const int n = sscanf(argv[++i], "%u-%u", &passA, &passB);
            if (n < 1 || passA == 0 || (n == 2 && passB < passA)) {
                fprintf(stderr, "bad --passes\n");
                return 1;
            }
            if (n == 1) {
                passB = passA;
            }
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
        } else if (strcmp(argv[i], "--vu-ref") == 0 && i + 1 < argc && vuRefCount < 8) {
            if (sscanf(argv[++i], "%d:%u", &vuRefL[vuRefCount], &vuRefI[vuRefCount]) != 2) {
                fprintf(stderr, "bad --vu-ref\n");
                return 1;
            }
            vuRefCount++;
        } else {
            fprintf(stderr, "unknown argument %s\n", argv[i]);
            return 1;
        }
    }
    uint32_t gw = 0, gh = 0;
    if (!unzipDump(&dump)) {
        return 1;
    }
    FILE *probe = fopen(dump, "rb");
    if (!probe) {
        /* rd_pixel writes the dump; without a Vulkan device it skipped */
        fprintf(stderr, "%s: no such dump (skipped)\n", dump);
        return 77;
    }
    fclose(probe);
    RdDumpInfo info;
    if (!rd__dump_peek(dump, &info) || !info.gsW || !info.gsH || info.gsW > 4096 ||
        info.gsH > 4096) {
        fprintf(stderr, "%s: not an rd dump\n", dump);
        return 1;
    }
    gw = info.gsW;
    gh = info.gsH;
    /* The aspect: a dump holds the picture as drawn at its own width (4:3
     * before version 10).  Replaying narrower crops it in the present;
     * wider has nothing to show.  The renderer is built at the dump's
     * aspect; --aspect (default: the dump's) only chooses the crop. */
    const float recAspect = info.aspect;
    float reqAspect = aspectGiven ? s.aspect : recAspect;
    if (reqAspect < kAspect43) {
        reqAspect = kAspect43; /* the box is 4:3 at least (rd__present_box) */
    }
    if (reqAspect > recAspect + 1e-3f) {
        char a[16], b[16];
        fprintf(stderr,
                "%s: a dump drawn at %s cannot show %s: capture an aspect sweep (Shift+F12 in "
                "photo mode)\n",
                dump, aspectName(recAspect, a), aspectName(reqAspect, b));
        return 1;
    }
    const bool crop = reqAspect < recAspect - 1e-3f;
    s.aspect = recAspect;
    if (crop && !pw && !list) {
        printf("note: --aspect crops the --present picture only; a target is the recorded frame "
               "(%s)\n",
               aspectName(recAspect, (char[16]){0}));
    }
    /* the output the present renders into: the box of the requested aspect
     * in --present WxH is filled by the recorded picture cropped to it, so
     * the present draws the whole recorded picture at the same scale into
     * an output as high as that box (the recorded aspect's box), and the
     * centre columns the box takes are cut out after (cropPresent) */
    RhiRect want = {0, 0, (int32_t)pw, (int32_t)ph};
    uint32_t ow = pw, oh = ph;
    if (pw && crop) {
        rd__present_box(pw, ph, reqAspect, &want);
        oh = (uint32_t)want.h;
        ow = (uint32_t)((float)oh * recAspect + 0.5f);
    }
    /* --pass-dump renders the targets with no output, as --target did, and
     * the present in a second context */
    s.outputWidth = passDir ? 0 : ow;
    s.outputHeight = passDir ? 0 : oh;
    /* --no-device (or --list without a device) loads the dump into a
       record-only renderer, lists it and renders nothing */
    if (noDevice && !list && !texDir && !meshName && !vuRefCount) {
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
    if (!noDevice && prevCount && !replayPrev()) {
        rd_shutdown();
        return 1;
    }
    if (lossBisect) {
        if (noDevice) {
            fprintf(stderr, "--loss-bisect needs a device\n");
            return 77;
        }
        if (passA && (drawA || maskMarks)) {
            /* one step of the second level on pass B's draws */
            RdLossStep o;
            char note[400];
            rd_loss_bisect_step(dump, passB, drawA ? drawA : 1u, drawA ? drawB : UINT32_MAX,
                                maskMarks ? 1 : 0, 0, &o);
            static const char *const kStep[] = {"not run", "lost", "clean"};
            rd_draw_note_text(&o.note, note, sizeof(note));
            printf("%s: loss bisect step: %s; pass %u of %u, draws %u..%u of %u%s, %u drawn, in "
                   "%.1f s%s%s\n",
                   dump, kStep[o.result], passB, o.passes, drawA ? drawA : 1u,
                   drawA ? drawB : o.draws, o.draws, maskMarks ? ", marks masked" : "", o.drawn,
                   o.ms / 1000.0, o.line[0] ? ": " : "", o.line);
            printf("%s: loss bisect step: the last draw: %s\n", dump, note);
            rd_shutdown();
            return o.result == RD_BISECT_NOT_RUN ? 1 : 0;
        }
        RdLossBisect b;
        rd_loss_bisect(dump, &b);
        static const char *const kResult[] = {"not run", "lost", "clean"};
        printf("%s: loss bisect: %s; frame %u, %u passes, %u replays in %.1f s%s%s\n", dump,
               kResult[b.result], b.frame, b.passes, b.replays, b.ms / 1000.0,
               b.line[0] ? ": " : "", b.line);
        if (b.next[0]) {
            printf("%s: loss bisect: the replay stopped as %s began\n", dump, b.next);
        }
        rd_shutdown();
        return b.result == RD_BISECT_NOT_RUN ? 1 : 0;
    }
    RdFrame pf, f;
    const RdFrame *rf = prepareFrame(dump, &pf, &f);
    if (!rf) {
        return 1;
    }
    if (list) {
        RdStateBlock st = rf->startState;
        s_listed = 0;
        rd__walk(rf, (int)rf->keep, &st, listCmd, (void *)rf);
        printf("dump: version %u, GS %ux%u, aspect %.4f (%s), visible width %g, cull width %u\n",
               info.version, info.gsW, info.gsH, (double)info.aspect,
               aspectName(info.aspect, (char[16]){0}), (double)info.wExact, info.cullW);
        printf("listed %u commands of frame %u (%ux%u, fba/pabe/texa/date per action)\n", s_listed,
               rf->number, rf->gsW, rf->gsH);
        /* the game's F12 line about the water (rd_water.c rd__water_dump_line),
           at this replay's options */
        char water[1536];
        rd__water_dump_line(rf, water, sizeof(water));
        printf("water: %s\n", water);
    }
    for (int k = 0; k < vuRefCount; k++) {
        vuRefDraw(rf, vuRefL[k], vuRefI[k]);
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
    if (passA) {
        rd__replay_set_window_draws(passA, passB, drawA, drawA ? drawB : 0u, maskMarks ? 1 : 0, 0);
    } else if (maskMarks) {
        rd__replay_set_window_draws(1, UINT32_MAX, 0, 0, 1, 0);
    }
    const bool replayed = rd__replay_frame(rf, (int)rf->keep, pw != 0 && !passDir);
    if (replayed && tempsDir) {
        /* the frame's temp targets (the water blocks among them), read back
           before anything frees them, as DIR/temp-<n>-<w>x<h>.png */
        for (uint32_t k = 0; k < rf->tempCount; k++) {
            const RdTargetRec *tr = rd__target_rec(rf->tempTargets[k]);
            if (!tr || tr->format == RHI_FMT_R8_UNORM) {
                continue;
            }
            size_t cap = (size_t)tr->tw * tr->th * 4;
            uint8_t *tp = malloc(cap);
            uint32_t tw = 0, th = 0;
            char tn[1024];
            snprintf(tn, sizeof(tn), "%s/temp-%u-%ux%u.png", tempsDir, k, tr->w, tr->h);
            if (tp && rd__read_target((RdTarget){rf->tempTargets[k]}, tp, cap, &tw, &th) &&
                rd_write_png(tn, tp, tw, th, tw * 4, 1)) {
                printf("temp target %u: %ux%u GS, %ux%u texels%s -> %s\n", k, tr->w, tr->h, tw, th,
                       tr->withDepth ? " with depth" : "", tn);
            } else {
                fprintf(stderr, "temp target %u: readback or PNG write failed\n", k);
            }
            free(tp);
        }
    }
    if (passA) {
        RdReplayWindowStats ws;
        rd__replay_window_stats(&ws);
        printf("%s: passes %u-%u of the %u begun%s; %u draws in pass %u\n", dump, passA, passB,
               ws.passes, ws.stopped ? " (the walk stopped after the window)" : "", ws.lastDraws,
               passB);
        if (drawA) {
            char note[400];
            RdDrawNote n;
            rd__replay_window_note(&n);
            rd_draw_note_text(&n, note, sizeof(note));
            printf("%s: draws %u-%u of pass %u: %u drawn; the last: %s\n", dump, drawA, drawB,
                   passB, ws.drawn, note);
        }
        rd__replay_set_window(0, 0);
    } else if (maskMarks) {
        rd__replay_set_window(0, 0);
    }
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
        printf("%s: stats: %u destination-read draws, %u storage passes, %u pass switches for the "
               "exact blend; glow %u passes; snapshots %u copied, %u swapped; %u draws resampled "
               "above 1x\n",
               dump, pr->dstDraws, pr->storagePasses, pr->dstSwitches, pr->glowPasses,
               pr->snapshots, pr->snapSwaps, pr->a1xDraws);
        /* the GS-level textures' two filters and the UINT view's blur sprites
         * (RdPerfRecord.texaDraws, gridDraws, uintDraws) */
        printf("%s: stats: texa draws %u (TEXA per texel), %u GS-level draws on the GS's grid; "
               "%u staticBlur sprites through the UINT views\n",
               dump, pr->texaDraws, pr->gridDraws, pr->uintDraws);
        /* the dynamic state (rd_pipeline.c rd__set_pipeline): the depth and
         * blend commands the binds above needed, none where every state is
         * static */
        printf("%s: stats: %u dynamic state sets (dynamic depth %s, blend %s)\n", dump,
               pr->dynamicSets, g_rd.dynDepth ? "on" : "off", g_rd.dynBlend ? "on" : "off");
        /* the VU vertex pre-pass (rd_replay_vu.c rd__vu_prepass): each mesh and grid
         * vertex once, before the lists */
        printf("%s: stats: VU pre-pass %s: %u passes, %u vertices in %u draws; %u mesh and grid "
               "draws the old way\n",
               dump, g_rd.vuPrepass ? "on" : "off", pr->vuPrePasses, pr->vuPreVertices,
               pr->vuPreDraws, pr->vuPreFallbacks);
    }
    if (replayed && passDir) {
        /* One frame, every target: the renderer keeps each target's contents
         * after the replay (the readback of --target does the same), so
         * the one replay serves them all.  The present picture is another
         * replay, in a fresh context (output size, a retained target that a
         * frame with keep reads back as the first replay left it), so each
         * picture equals the one a process of its own would write. */
        rc = 0;
        int written = 0;
        for (int k = 0; k < (int)(sizeof(kNames) / sizeof(kNames[0])); k++) {
            if (kNames[k][0] == '\0') {
                continue;
            }
            char name[1024];
            snprintf(name, sizeof(name), "%s/%s.png", passDir, kNames[k]);
            /* each readback may open a command list for its barrier and the
             * Vulkan backend allows 8 a frame (VKR_MAX_CMD_LISTS): a new
             * frame slot per target (the targets keep their contents) */
            rhi_wait_frame();
            if (writeTarget(k, name)) {
                written++;
            } else {
                fprintf(stderr, "%s: readback or PNG write failed\n", kNames[k]);
                rc = 1;
            }
        }
        printf("%s: frame %u, %d targets -> %s (%u commands skipped)\n", dump, f.number, written,
               passDir, rd__not_implemented_count());
        if (pw) {
            rd__frame_free(&f);
            rd__frame_free(&pf);
            rd_shutdown();
            s.outputWidth = ow;
            s.outputHeight = oh;
            if (!rd_init(gw, gh, &s, NULL)) {
                fprintf(stderr, "no usable device for the present\n");
                return 1;
            }
            rd__set_not_implemented_fatal(false);
            if (overlay) {
                rd_set_present_overlay(overlayTest, NULL);
            }
            rf = prepareFrame(dump, &pf, &f);
            if (!rf) {
                return 1;
            }
            char name[1024];
            snprintf(name, sizeof(name), "%s/PRESENT.png", passDir);
            if (!rd__replay_frame(rf, (int)rf->keep, true) ||
                !writePresent(name, ow, oh, want, pw, ph)) {
                fprintf(stderr, "PRESENT: readback or PNG write failed\n");
                rc = 1;
            }
        }
    } else if (replayed) {
        uint32_t w = 0, h = 0;
        size_t cap = pw ? (size_t)pw * ph * 4 : (size_t)4096 * 4096 * 4;
        uint8_t *px = malloc(cap);
        bool ok = false;
        if (pw) {
            ok = writePresent(png, ow, oh, want, pw, ph);
            w = pw;
            h = ph;
        } else if (px) {
            ok = rd__read_target(rd_target((RdTargetId)target), px, cap, &w, &h) &&
                 rd_write_png(png, px, w, h, w * 4, 1);
        }
        if (ok) {
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
        static RdPerfPass passes[RHI_MAX_TIMESTAMPS];
        uint32_t passCount = 0;
        memset(&last, 0, sizeof(last));
        rd__set_perf_passes(passTimes);
        for (int k = 0; k < RHI_FRAMES_IN_FLIGHT + 2; k++) {
            rd__replay_frame(rf, (int)rf->keep, pw != 0);
            while (rd_perf_pop(&pr)) {
                if (pr.gpuValid) {
                    last = pr;
                    have = true;
                    const RdPerfPass *pp;
                    const uint32_t pn = rd_perf_passes_of(pr.replay, &pp);
                    if (pn) {
                        memcpy(passes, pp, (size_t)pn * sizeof(passes[0]));
                    }
                    passCount = pn;
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
            printf("%s; present %.3f; VU pre-pass %.3f\n",
                   last.gpuPostPartial ? " (not all timed apart)" : "", last.gpuPresentMs,
                   last.gpuPrepassMs);
            if (passTimes) {
                /* the warm replay pass by pass, then the window's texts */
                static RdPerfPassAgg agg;
                char label[32], text[1024];
                double kinds[RD_PERF_PASS_KIND_COUNT], effects[RD_PERF_POST_COUNT];
                for (uint32_t k = 0; k < passCount; k++) {
                    const RdPerfPass *q = &passes[k];
                    rd_perf_pass_label(q, label, sizeof(label));
                    printf("%s: pass %u: %s %s %ux%u, list %d, %u draws, %u pipeline binds, GPU "
                           "%.1f us, CPU %.1f us\n",
                           dump, k, label, rd_perf_target_name(q->target), (unsigned)q->w,
                           (unsigned)q->h, (int)q->list, q->draws, q->pipelineBinds,
                           (double)q->gpuUs, (double)q->cpuUs);
                }
                rd_perf_pass_agg_reset(&agg);
                rd_perf_pass_agg_add(&agg, passes, passCount);
                rd_perf_pass_agg_text(&agg, 5, text, sizeof(text));
                printf("%s: gpu top passes: %s%s\n", dump, text,
                       last.passPartial ? " (more passes than stamps: the last timed together)"
                                        : "");
                for (int k = 0; k < RD_PERF_PASS_KIND_COUNT; k++) {
                    kinds[k] = last.passKinds[k];
                }
                for (int k = 0; k < RD_PERF_POST_COUNT; k++) {
                    effects[k] = last.passEffects[k];
                }
                rd_perf_pass_kinds_text(kinds, effects, 1.0, text, sizeof(text));
                printf("%s: passes %s\n", dump, text);
            }
        } else {
            printf("%s: gpu: no timestamps came back\n", dump);
        }
    }
    rd__frame_free(&f);
    rd__frame_free(&pf);
    rd_shutdown();
    return rc;
}
