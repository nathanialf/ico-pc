/* rd_replay_tool: loads an rd frame dump (rd_dump_frame) and renders it
 * headless to a PNG, for the renderer verification plan (backend against
 * backend, before against after).
 *
 *   rd_replay_tool <dump> <out.png> [--target NAME] [--present WxH]
 *                  [--pass-dump DIR] [--backend vulkan|d3d12|opengl|d3d11]
 *
 * <dump> is an .rddump, or the .zip F12 writes: its first .rddump entry is
 * taken (and for --interp's PREV given as a zip, its second, the tick
 * before F12 and the loss dump zip beside the frame: `rd_replay_tool
 * frame.zip out.png --interp 0.5 frame.zip`) (extracted to a temporary file in the temp folder, deleted at exit).
 * The replay runs as the disc the dump was drawn on (its region, dump
 * version 12 and later; the European disc for an older dump): the reduction's crop
 * and the border mask are that disc's.
 *
 * NAME is a named target (SCENE, DISPLAY (default), SHADOW0..2, WORK0..3,
 * AA0, AA1, FEED128, AURA_WORK, AURA_TAP, WORK2_PAD, FEED_HELD,
 * DISPLAY_HELD, WORK3_HELD); an unknown name is an error (exit 1, the names listed).
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
 * port/rhi/rhi.h rhi_create_backend; "vk" and "gl" are taken too), so the
 * same dump can be rendered on Vulkan, D3D12, OpenGL and D3D11 and the PNGs
 * compared; a name the build does not link is an error (exit 1).
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
 *   --downsample          Downsampling: To original size (RdSettings.downsample):
 *                         with a --resolution above 1x, DISPLAY box-averaged
 *                         to its GS size before the present, which then
 *                         draws it as at 1x; --stats names the reduce
 *   --full-pixel          the reduction draws the whole frame: no black border
 *   --border-mask         the game's Full pixel off: the same picture, the
 *                         border masked over it at present (rd__border_mask)
 *   --water-sharp         Water reflections: Sharp: the dump's render-to-texture
 *                         blocks that are not scene-sized (the reflections in
 *                         water, the queen barrier's copy) at the scene's scale
 *                         (rd_core.c rd__target_scale_of); without it they are
 *                         drawn at their GS size, the PS2's.  A dump from before
 *                         the option marks no block and replays as without it
 *   --motion-blur M       original (the default) or smooth: Options > Effects
 *                         > Motion blur's way (RdSettings.motionBlurSmooth,
 *                         rd_blur_smooth.c).  Smooth's trail is an
 *                         accumulator of the replays before: one replay
 *                         alone holds none (the trail starts with it); the
 *                         --prev dumps and --presents' PREV feed it, as
 *                         the presents of a run do
 *   --no-crack-fill       the crack filler off (rd_crack_fill.c; [dev]
 *                         crack_fill = false): above 1x under --enhanced the
 *                         pixels no draw covered between drawn ones keep the
 *                         clear and take the fog's colour, as before it (an
 *                         A/B of the filler)
 *   --work-clear RRGGBB   every target but SCENE, DISPLAY and the feedback
 *                         kept across frames (DISPLAY_HELD, FEED128,
 *                         FEED_HELD, WORK3_HELD) cleared to RRGGBB at the head of the
 *                         replay (rd_upload.c, ICO_RD_WORK_CLEAR for the
 *                         game): a texel the frame reads before drawing it
 *                         shows the colour downstream (issue 129's stale
 *                         column; --target and --pass-dump show where)
 *   --no-glow-edge        the glow's blur drawn as recorded: on a frame wider
 *                         than 512 its first pass leaves WORK1's column 0
 *                         (rd_blur_border.c glowEdge, an A/B of the fix)
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
 *                         draws one per command and merged (on Direct3D
 *                         11 a line of the bytes its submits wrote to the
 *                         GPU, RdPerfRecord.backendWriteBytes); a second line:
 *                         the exact blend's destination-read draws, storage
 *                         passes and pass switches, the glow's passes, the
 *                         target snapshots copied and swapped, the draws
 *                         through the above-1x kernels (rd.h
 *                         RdPerfRecord); a third: the VU vertex pre-pass,
 *                         on or off, its passes, vertices and draws and the
 *                         mesh and grid draws drawn the old way; the last:
 *                         the crack filler's line (its passes, the pixels
 *                         it filled, every filled replay counted, the scale
 *                         and the reach, the fills drawn inside the
 *                         scene pass with the local fog, those drawn after
 *                         the MSAA resolve, or why it did not draw); then
 *                         the region-mode mesh vertices in front of the eye
 *                         whose GS Z saturates (on the CPU, vu1_ref), which a
 *                         GPU-clipped triangle draws at their GS position in
 *                         a state other than out of the box; on a device
 *                         with GPU timestamps, after the picture is written,
 *                         the frame is replayed RHI_FRAMES_IN_FLIGHT + 2
 *                         more times and the last replay's GPU times are
 *                         printed: the uploads, each list, each picture
 *                         effect (rd.h RD_PERF_POST_*, inside the lists'
 *                         times), the present and the VU pre-pass
 *   --keys                the pipelines the replay asks for against the start-up
 *                         set (rd__enumerate_reachable): the replay is walked
 *                         with nothing drawn (rd__pipeline_gather) and the keys
 *                         it names are made, again while a replay names new
 *                         ones (at most 6 rounds); then one line a key,
 *                         "<dump>: key: <in set|MISSING> <family>: <words>",
 *                         and "<dump>: keys: N asked, M in the set of K, X
 *                         missing"; exit 1 on a miss.  <out.png> is unused
 *                         (give -).  The profile the set was made for is
 *                         printed first ("<dump>: keys: profile ...")
 *   --pass-times          (implies --stats) every replay timed pass by pass
 *                         ([dev] perf_passes, rd_perf.c): after the gpu line
 *                         the warm replay's passes, one line each ("pass N:
 *                         <label> <target> WxH, list L, D draws, B
 *                         pipeline binds, GPU <us> us, CPU <us> us"), then
 *                         the window's top-passes text over that replay and
 *                         its passes by kind
 *   --time N              after the picture is written, the frame replayed
 *                         N more times (with --interp the presenter's frame
 *                         built again before each, rd__interp_frame: the
 *                         first as the tick's first present, the others as
 *                         its later presents) and one line of the N
 *                         replays' records (rd.h RdPerfRecord), each CPU
 *                         phase's mean and least: "<dump>: time: N
 *                         replays, CPU mean (min) ms: total, interp, wait,
 *                         acquire, upload, walk, bind, submit, present;
 *                         per replay: ring, uniform and interp KB, draws,
 *                         pipeline and bind group binds" (on Direct3D 11
 *                         a line of the KB its submits wrote to the GPU),
 *                         then the first replay's interp alone; with
 *                         --interp DISPLAY after the first and the last of
 *                         them compared byte for byte ("pictures
 *                         identical", else the bytes that differ and exit
 *                         1) and the replays that pasted the first's glow
 *                         ("glow held in N of M replays", rd_glow_hold.c)
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
 *   --presents N          (with --interp T PREV) PREV replayed once (the tick
 *                         before: the motion blur's old frame), then N
 *                         presents of <dump> at alpha T (k + 1) / N, k = 0
 *                         .. N - 1, as the presenter draws a tick of N
 *                         presents (the first the tick's first present),
 *                         each written to <out>-p<k>.png (<out> without its
 *                         .png); the motion blur takes k = N presents a
 *                         tick.  Each line names the glow: "glow drawn"
 *                         at the first, "glow held" at a later present
 *                         that pasted the first's (rd_glow_hold.c;
 *                         ICO_RD_GLOW_REDRAW=1 draws it at every one,
 *                         "glow redrawn ([dev] glow_redraw)"), "no glow".  Under --motion-blur original the N
 *                         pictures' motion blur is one (DISPLAY_HELD), under
 *                         smooth its trail moves on at each.  Each line
 *                         ends "rendered" or "shown again": a present whose
 *                         frame hashes as the last rendered one's is shown
 *                         again (rd_reshow.c; ICO_RD_STILL_REPRESENT=0
 *                         renders every one, the pictures the same)
 *   --interp-any          (with --interp) PREV is taken for the tick right
 *                         before <dump> whatever their frame numbers (dumps
 *                         saved some ticks apart, as the F12 sets are): no
 *                         gap snap; the other snaps (a camera past the cut
 *                         thresholds) still apply
 *                         The third interp line counts the still draws
 *                         (rd_interp.c stillDraw), those the blend would
 *                         have re-rounded, the
 *                         parts of the world drawn as the tick's and the
 *                         draws marked for the ticks' cameras
 *   --no-reproject        (with --interp) the draws the frame camera
 *                         projected on the CPU (lightning, lines, shadow
 *                         volumes) blend in screen space instead of being
 *                         seen through the blended camera
 *                         (rd__interp_set_reproject): the before of a
 *                         before/after pair from one build
 *   --interp-nop-prev L:A[-B]
 *                         (with --interp, repeatable) turns commands A..B
 *                         of list L of PREV into NOPs before the blend, as
 *                         --nop does to <dump>: their draws in <dump> find
 *                         no match and are drawn as the tick's alone
 *                         (unmatched), to make chosen parts of the world
 *                         the tick's in a blended picture (the mixed
 *                         picture of rd_interp.c, RdInterpStats.mixed)
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
#include "../core/region.h"              /* the dump's disc (version 12) */
#include "../vu1_ref/vu1_ref.h"          /* --vu-ref: the CPU mirror of the VU programs */
#include "../../shaders/shader_consts.h" /* ICO_VU_MARK_*: the marks --mesh shows */
#include "font.h"                        /* port/ui: the overlay test's text */

static const char *const kNames[] = {
    "SCENE",    "DISPLAY",   "SHADOW0",   "SHADOW1",      "SHADOW2",   "WORK0", "WORK1",
    "WORK2",    "WORK3",     "AA0",       "AA1",          "FEED128",   "",      "AURA_WORK",
    "AURA_TAP", "WORK2_PAD", "FEED_HELD", "DISPLAY_HELD", "WORK3_HELD"};

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

/* --stats: the vertices of the frame's region-mode mesh draws (static and
 * skinned; not the grids) in front of the eye whose ftoi4 Z saturates and X
 * and Y do not (vu1_ref.h vu1ref_region_z_saturated, the shaders' own
 * transform): the corners vu_common.hlsli vu_corner_cut_position keeps at
 * their GS position when a GPU-clipped triangle has them, in a state other
 * than out of the box.  *drawsOut: the draws with one or more */
static uint64_t satZVertices(const RdFrame *f, uint32_t *drawsOut)
{
    static float bones[240][4];
    uint64_t n = 0;
    uint32_t draws = 0;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < f->lists[l].count; i++) {
            const RdCmd *c = &f->lists[l].cmds[i];
            if (c->type != RDC_MESH && c->type != RDC_SKINNED) {
                continue;
            }
            const RdMeshRec *m = rd__mesh_rec(c->u[0]);
            RdVuPayload p;
            if (!m || !m->stream ||
                (uint64_t)c->u[1] + sizeof(p) + sizeof(RdVuBlock) > f->payloadSize) {
                continue;
            }
            memcpy(&p, f->payload + c->u[1], sizeof(p));
            int kind;
            switch (p.prog) {
            case RD_PROG_PRELIT:
            case RD_PROG_LIT:
            case RD_PROG_LIT_SPEC:
            case RD_PROG_REFLECT:
                kind = VU1REF_CARD_NORMAL;
                break;
            case RD_PROG_SKIN:
            case RD_PROG_SKIN_SPEC:
                kind = VU1REF_CARD_CLUSTER;
                break;
            default:
                kind = -1;
                break;
            }
            if (kind < 0 || p.clip != RD_VU_CLIP_REGION ||
                (kind == VU1REF_CARD_CLUSTER) != (c->type == RDC_SKINNED)) {
                continue;
            }
            const float (*mem)[4] =
                (const float (*)[4])(const void *)(f->payload + c->u[1] + sizeof(p));
            if (kind == VU1REF_CARD_CLUSTER) {
                const uint32_t nb = p.boneQw < 240u ? p.boneQw : 240u;
                if ((uint64_t)c->u[1] + sizeof(p) + sizeof(RdVuBlock) + (uint64_t)nb * 16 >
                    f->payloadSize) {
                    continue;
                }
                memset(bones, 0, sizeof(bones));
                memcpy(bones, mem + 36, (size_t)nb * 16);
            }
            uint32_t last = p.firstBatch + p.batchCount;
            if (last > m->batchCount || p.batchCount == 0) {
                last = m->batchCount;
            }
            uint64_t k = 0;
            for (uint32_t b = p.firstBatch; b < last; b++) {
                const uint32_t v0 = m->batches[b].firstVertex;
                for (uint32_t v = v0; v < v0 + m->batches[b].vertexCount; v++) {
                    k += (uint64_t)vu1ref_region_z_saturated(
                        kind, mem, (const float (*)[4])bones,
                        (const float (*)[4])(m->stream + (size_t)v * m->qwPerVertex));
                }
            }
            n += k;
            draws += k != 0;
        }
    }
    *drawsOut = draws;
    return n;
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
        if (c->b[5] == RD_SCREEN_ST_SHIFT) {
            /* rd.h rd_frame_projected_st: S / Q and T / Q per 12.4 unit */
            printf(" st-shift (%g,%g)", (double)c->f[0], (double)c->f[1]);
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

static char s_zipTemps[2][1100];

static void removeZipTemp(void)
{
    for (int k = 0; k < 2; k++) {
        if (s_zipTemps[k][0] != '\0') {
            remove(s_zipTemps[k]);
        }
    }
}

/* a .zip given as a dump: its .rddump goes to a temporary file (the loader
   takes a path) and *dump names that file; false with a message when it
   cannot.  nth 0: the zip's first .rddump (the frame); 1: --interp's PREV,
   the zip's second (F12's and the loss dump's tick before), its first when
   it holds one only */
static bool unzipDump(const char **dump, int nth)
{
    char *tmp = s_zipTemps[nth];
    const char *path = *dump;
    const size_t n = strlen(path);

    if (n < 4 || (strcmp(path + n - 4, ".zip") != 0 && strcmp(path + n - 4, ".ZIP") != 0)) {
        return true;
    }
    size_t size = 0;
    void *data = nth ? ico_dump_zip_extract_nth(path, 1, &size, NULL, 0) : NULL;
    if (data == NULL) {
        data = ico_dump_zip_extract(path, &size, NULL, 0);
    }

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
    snprintf(tmp, sizeof(s_zipTemps[0]), "%s/rd_replay_%u_%d.rddump", dir,
             (unsigned)(uintptr_t)data & 0xFFFFFFu, nth);
    FILE *fp = fopen(tmp, "wb");
    const bool ok = fp != NULL && fwrite(data, 1, size, fp) == size;

    if (fp != NULL && fclose(fp) != 0) {
        tmp[0] = '\0';
        free(data);
        return false;
    }
    free(data);
    if (!ok) {
        fprintf(stderr, "%s: cannot write the temporary dump\n", tmp);
        removeZipTemp();
        tmp[0] = '\0';
        return false;
    }
    static bool registered;
    if (!registered) {
        registered = true;
        atexit(removeZipTemp);
    }
    *dump = tmp;
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
    bool prev; /* --interp-nop-prev: applied to the --interp PREV */
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
/* --motion-blur smooth (every replay armed as a present, rd__blur_smooth_arm)
 * and --presents N */
static bool motionSmooth;
static int presentsN;

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
        if (motionSmooth) {
            rd__blur_smooth_arm(1, 1); /* Smooth's trail fed by the frames before */
        }
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
    uint32_t prevNops = 0;
    for (int k = 0; k < nopCount; k++) {
        RdFrame *nf = nops[k].prev ? pf : f; /* main: --interp-nop-prev needs --interp */
        const RdCmdList *cl = &nf->lists[nops[k].l];
        for (uint32_t c = nops[k].a; c <= nops[k].b && c < cl->count; c++) {
            if (!rd__cmd_is_state(cl->cmds[c].type)) {
                prevNops += nops[k].prev && cl->cmds[c].type != RDC_NOP;
                cl->cmds[c].type = RDC_NOP;
            }
        }
    }
    if (prevNops) {
        /* those draws' matches in <dump> are the tick's alone */
        printf("interp-nop-prev: %u draws of PREV turned into NOPs\n", prevNops);
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
               "%u held; %u CPU-projected draws re-projected (%u re-projected draws with their "
               "texture shifted: the ripples); %u draws turned the long way, "
               "%u kept at the tick's pose for their turn; %u VU draws blended across a change "
               "of payload size, %u mismatched by size\n",
               ist.unmatchedWhy[RD_UNMATCHED_ABSENT], ist.unmatchedWhy[RD_UNMATCHED_FEWER],
               ist.unmatchedWhy[RD_UNMATCHED_UNPLACED], ist.unmatchedWhy[RD_UNMATCHED_PAYLOAD],
               ist.apart, ist.prevKept, ist.prevHeld, ist.reprojected, ist.stShifted, ist.longWay,
               ist.turnSnap, ist.sizeTailBlended, ist.why[RD_MISMATCH_SIZE]);
        static const char *const kCamOff[RD_CAM_OFF_COUNT] = {"on (or the frame not blended)",
                                                              "off: alpha out of range",
                                                              "off at alpha 1",
                                                              "off: no camera",
                                                              "off: legacy",
                                                              "off: the blend failed"};
        printf("interp: %u still draws, each drawn from the tick's block through the blended "
               "camera; %u of them the blend would have re-rounded; %u parts of the world drawn "
               "as the tick's beside blended ones; %u VU draws with the ticks' cameras; a mixed "
               "picture (no blended camera): %u parts of the world from the tick before, %u from "
               "the tick, %u in the minority; the blended camera: %s\n",
               ist.stillDraws, ist.stillReRounded, ist.curWorld, ist.tickCams, ist.worldPrevRaw,
               ist.worldCurRaw, ist.mixed,
               kCamOff[ist.camOffWhy < RD_CAM_OFF_COUNT ? ist.camOffWhy : 0]);
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

/* DISPLAY's texels into *px (grown to fit); false when it cannot be read */
static bool readDisplay(uint8_t **px, size_t *cap, size_t *size)
{
    const RdTargetRec *tr = rd__target_rec(rd_target(RD_TARGET_DISPLAY).id);
    const size_t want = tr ? (size_t)tr->tw * tr->th * 4 : 0;
    uint32_t w = 0, h = 0;
    if (!want) {
        return false;
    }
    if (want > *cap) {
        uint8_t *p = realloc(*px, want);
        if (!p) {
            return false;
        }
        *px = p;
        *cap = want;
    }
    if (!rd__read_target(rd_target(RD_TARGET_DISPLAY), *px, *cap, &w, &h)) {
        return false;
    }
    *size = (size_t)w * h * 4;
    return true;
}

/* --time N (the usage comment): rf replayed n times, with --interp built
 * again from pf and f before each; false on a failure or, with --interp,
 * when DISPLAY after the first replay and after the last differ */
static bool timeReplays(const char *dump, const RdFrame *pf, const RdFrame *f, const RdFrame *rf,
                        int n, bool present)
{
    enum {
        P_TOTAL,
        P_INTERP,
        P_WAIT,
        P_ACQUIRE,
        P_UPLOAD,
        P_WALK,
        P_BIND,
        P_SUBMIT,
        P_PRESENT,
        P_COUNT
    };

    double sum[P_COUNT] = {0}, low[P_COUNT] = {0};
    double ring = 0.0, uniform = 0.0, interp = 0.0, draws = 0.0, pipes = 0.0, groups = 0.0;
    double d3d11 = 0.0;    /* RdPerfRecord.backendWriteBytes (Direct3D 11 only) */
    uint32_t glowHeld = 0; /* RdPerfRecord.glowHeld: replays that pasted the held glow */
    RdPerfRecord first;
    uint8_t *pic[2] = {NULL, NULL};
    size_t cap[2] = {0, 0}, size[2] = {0, 0};
    bool ok = true;
    memset(&first, 0, sizeof(first));
    for (int k = 0; k < n && ok; k++) {
        if (interpPrev) {
            RdInterpStats ist;
            const double t0 = rd__now_ms();
            rf = rd__interp_frame(pf, f, interpT, k == 0, &ist);
            const double ms = rd__now_ms() - t0;
            rd__perf_interp_ms(ms); /* charged to the replay below, as rd_present does */
            if (!rf) {
                fprintf(stderr, "%s: time: rd__interp_frame failed\n", dump);
                ok = false;
                break;
            }
        }
        if (!rd__replay_frame(rf, (int)rf->keep, present)) {
            fprintf(stderr, "%s: time: replay %d failed\n", dump, k + 1);
            ok = false;
            break;
        }
        const RdPerfRecord r = g_rdPerf; /* the record the replay just closed */
        while (rd_perf_pop(NULL)) {}
        const double v[P_COUNT] = {r.totalMs, r.interpMs, r.waitMs,   r.acquireMs, r.uploadMs,
                                   r.walkMs,  r.bindMs,   r.submitMs, r.presentMs};
        for (int p = 0; p < P_COUNT; p++) {
            sum[p] += v[p];
            low[p] = k == 0 || v[p] < low[p] ? v[p] : low[p];
        }
        ring += (double)r.uploadBytes;
        d3d11 += (double)r.backendWriteBytes;
        uniform += (double)r.uploadUniformBytes;
        interp += (double)r.uploadInterpBytes;
        draws += r.draws;
        pipes += r.pipelineBinds;
        groups += r.bindGroupBinds;
        glowHeld += r.glowHeld;
        if (k == 0) {
            first = r;
        }
        if (interpPrev && (k == 0 || k == n - 1)) {
            const int at = k == 0 ? 0 : 1;
            if (!readDisplay(&pic[at], &cap[at], &size[at])) {
                fprintf(stderr, "%s: time: DISPLAY cannot be read after replay %d\n", dump, k + 1);
                ok = false;
            }
        }
    }
    if (ok) {
        const double m = (double)n;
        printf("%s: time: %d replays, CPU mean (min) ms: total %.3f (%.3f), interp %.3f (%.3f), "
               "wait %.3f (%.3f), acquire %.3f (%.3f), upload %.3f "
               "(%.3f), walk %.3f (%.3f), bind %.3f (%.3f), submit %.3f (%.3f), present %.3f "
               "(%.3f); per replay: ring %.1f KB, uniform %.1f KB, interp %.1f KB, %.1f draws, "
               "%.1f pipeline binds, %.1f bind group binds\n",
               dump, n, sum[P_TOTAL] / m, low[P_TOTAL], sum[P_INTERP] / m, low[P_INTERP],
               sum[P_WAIT] / m, low[P_WAIT], sum[P_ACQUIRE] / m, low[P_ACQUIRE], sum[P_UPLOAD] / m,
               low[P_UPLOAD], sum[P_WALK] / m, low[P_WALK], sum[P_BIND] / m, low[P_BIND],
               sum[P_SUBMIT] / m, low[P_SUBMIT], sum[P_PRESENT] / m, low[P_PRESENT],
               ring / m / 1024.0, uniform / m / 1024.0, interp / m / 1024.0, draws / m, pipes / m,
               groups / m);
        if (d3d11 > 0.0) {
            /* the ring's bytes Direct3D 11's submits copied to the GPU */
            printf("%s: time: Direct3D 11 submits wrote %.1f KB to the GPU a replay\n", dump,
                   d3d11 / m / 1024.0);
        }
        printf("%s: time: the first replay: interp %.3f ms, total %.3f\n", dump, first.interpMs,
               first.totalMs);
        if (interpPrev) {
            /* rd_glow_hold.c: the later replays of the tick paste the first's glow */
            printf("%s: time: glow held in %u of %d replays\n", dump, glowHeld, n);
        }
    }
    if (ok && interpPrev) {
        /* two sizes: every byte of the larger counts */
        size_t diff = size[0] != size[1] ? (size[0] > size[1] ? size[0] : size[1]) : 0;
        for (size_t i = 0; size[0] == size[1] && i < size[0]; i++) {
            diff += pic[0][i] != pic[1][i];
        }
        if (diff == 0) {
            printf("%s: time: DISPLAY after replay 1 and replay %d: pictures identical\n", dump, n);
        } else {
            printf("%s: time: DISPLAY after replay 1 and replay %d: %zu bytes differ\n", dump, n,
                   diff);
            ok = false;
        }
    }
    free(pic[0]);
    free(pic[1]);
    return ok;
}

/* --keys: the replay's pipelines against the start-up set.  The rounds are
 * rd_dump.c bisectPipelines': a replay that only names the keys it finds no
 * pipeline for, then those keys made (a key a draw asks for only once
 * another exists, the fallback pair's second pass, shows in a later round).
 * The set is enumerated after the rounds, from the same state the keys were
 * asked in.  1 on a miss. */
#define KEYS_ROUNDS 6

static int checkKeys(const char *dump, const RdFrame *rf)
{
    static RdPipeKeyInt asked[RD_PIPELINE_CACHE_MAX];
    static RdPipeKeyInt round[RD_PIPELINE_CACHE_MAX];
    static RdPipeKeyInt set[RD_PIPELINE_CACHE_MAX]; /* as rd_precreate_pipelines enumerates */
    uint32_t na = 0, rounds = 0;
    for (int r = 0; r < KEYS_ROUNDS; r++) {
        bool stopped = false;
        rd__pipeline_gather(true);
        (void)rd__replay_frame(rf, (int)rf->keep, false);
        rd__pipeline_gather(false);
        rhi_wait_idle();
        const uint32_t got = rd__pipeline_gathered(round, RD_PIPELINE_CACHE_MAX);
        for (uint32_t i = 0; i < got; i++) {
            na = rd__add_pipe_key(asked, RD_PIPELINE_CACHE_MAX, na, &round[i]);
        }
        if (got == 0) {
            break;
        }
        rounds++;
        (void)rd__pipeline_create_gathered(&stopped);
        if (stopped || rhi_device_lost()) {
            fprintf(stderr, "%s: keys: stopped in round %d\n", dump, r + 1);
            return 1;
        }
    }
    const uint32_t ns = rd__enumerate_reachable(set, RD_PIPELINE_CACHE_MAX);
    const uint32_t nsm = ns < RD_PIPELINE_CACHE_MAX ? ns : RD_PIPELINE_CACHE_MAX;
    const uint32_t nam = na < RD_PIPELINE_CACHE_MAX ? na : RD_PIPELINE_CACHE_MAX;
    const RdProfile *pr = &g_rd.profile;
    printf("%s: keys: profile tiler %d, local read %d, fog local %d, DATE stencil %d, blur fold "
           "%d, dynamic depth %d, dynamic blend %d, VU pre-pass %d\n",
           dump, pr->tiler, pr->localRead, pr->fogLocal, pr->dateStencil, pr->blurFold,
           g_rd.dynDepth, g_rd.dynBlend, g_rd.vuPrepass);
    uint32_t miss = 0;
    for (uint32_t i = 0; i < nam; i++) {
        bool in = false;
        for (uint32_t j = 0; j < nsm && !in; j++) {
            in = rd__pipe_key_equal(&asked[i], &set[j]);
        }
        char w[200];
        rd__pipe_key_text(&asked[i], w, sizeof(w));
        printf("%s: key: %s %s: %s\n", dump, in ? "in set" : "MISSING",
               rd__pipe_family_name(rd__pipe_key_family(&asked[i])), w);
        miss += !in;
    }
    printf("%s: keys: %u asked in %u rounds, %u in the set of %u, %u missing\n", dump, nam, rounds,
           nam - miss, ns, miss);
    return miss ? 1 : 0;
}

/* --presents N: PREV replayed once, then N presents of the tick at alpha
 * T (k + 1) / N, each picture to <png>-p<k>.png (a target's readback, or
 * the presented picture with --present); 0 written, 1 a failure */
static int presentsRun(const char *dump, const char *png, RdFrame *pf, RdFrame *f, int target,
                       uint32_t ow, uint32_t oh, RhiRect want, uint32_t pw, uint32_t ph)
{
    char stem[1024];
    snprintf(stem, sizeof(stem), "%s", png);
    const size_t n = strlen(stem);
    if (n > 4 && strcmp(stem + n - 4, ".png") == 0) {
        stem[n - 4] = '\0';
    }
    rd__blur_smooth_arm(1, (uint32_t)presentsN);
    if (!rd__replay_frame(pf, (int)pf->keep, false)) {
        fprintf(stderr, "%s: --presents: PREV's replay failed\n", dump);
        return 1;
    }
    rhi_wait_frame();
    printf("%s: presents: PREV (frame %u) replayed as the tick before\n", dump, pf->number);
    const size_t cap = (size_t)4096 * 4096 * 4;
    uint8_t *px = pw ? NULL : malloc(cap);
    int rc = 0;
    for (int k = 0; k < presentsN && rc == 0; k++) {
        const float a = interpT * (float)(k + 1) / (float)presentsN;
        RdInterpStats ist;
        const RdFrame *rf = rd__interp_frame(pf, f, a, k == 0, &ist);
        const int glow = rd__glow_hold_last(); /* rd_glow_hold.c: held or drawn */
        if (!rf) {
            fprintf(stderr, "%s: --presents: rd__interp_frame failed at present %d\n", dump, k);
            rc = 1;
            break;
        }
        rd__blur_smooth_arm(k == 0, (uint32_t)presentsN);
        char name[1100];
        snprintf(name, sizeof(name), "%s-p%d.png", stem, k);
        uint32_t w = 0, h = 0;
        const uint64_t shown0 = rd__reshow_count(RD_RESHOW_SHOWN);
        bool ok = rd__present_blended(rf, k == 0, pw != 0);
        const bool reshown = rd__reshow_count(RD_RESHOW_SHOWN) != shown0;
        if (ok && pw) {
            ok = writePresent(name, ow, oh, want, pw, ph);
            w = pw;
            h = ph;
        } else if (ok) {
            ok = px && rd__read_target(rd_target((RdTargetId)target), px, cap, &w, &h) &&
                 rd_write_png(name, px, w, h, w * 4, 1);
        }
        rhi_wait_frame();
        uint64_t drawn = 0, restarts = 0;
        rd__blur_smooth_counts(&drawn, &restarts, NULL);
        printf("%s: present %d of %d at alpha %.4f (snap %u): %ux%u -> %s; motion blur %s (%llu "
               "drawn through the accumulator, %llu restarts); %s; %s\n",
               dump, k, presentsN, (double)a, ist.snap, w, h, ok ? name : "(failed)",
               motionSmooth ? "smooth" : "original", (unsigned long long)drawn,
               (unsigned long long)restarts, rd__glow_hold_text(glow),
               reshown ? "shown again" : "rendered");
        rc = ok ? 0 : 1;
    }
    free(px);
    return rc;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s <dump> <out.png> [--target NAME] [--present WxH] [--enhanced] "
                "[--aspect A] [--resolution WxH|Nx] [--full-height] [--full-pixel] "
                "[--border-mask] [--water-sharp] [--motion-blur original|smooth] [--no-crack-fill] "
                "[--work-clear RRGGBB] [--no-glow-edge] "
                "[--filter F] "
                "[--mirror] [--overlay-test] [--backend vulkan|d3d12|opengl|d3d11] [--list] "
                "[--nop L:A[-B]] "
                "[--mesh NAME] [--dump-textures DIR] [--no-aa1] [--stats] [--pass-times] "
                "[--time N] [--keys] "
                "[--pass-dump DIR] "
                "[--prev DUMP]... [--no-half] [--interp T PREV [--interp-any] [--no-reproject] "
                "[--interp-nop-prev L:A[-B]] [--presents N]]\n"
                "       [--loss-bisect] [--passes A[-B]] [--draws A[-B]] [--mask-marks]\n"
                "       [--no-device (with --list, --mesh or --dump-textures; <out.png> unused)]\n"
                "       [--crt scanlines|consumer|trinitron|pvm|shadow [--crt-strength K]]\n"
                "       [--aa off|ssaa2|ssaa4|msaa2|msaa4|msaa8 (with --enhanced)]\n"
                "       [--downsample (with --resolution above 1x)]\n",
                argv[0]);
        return 1;
    }
    const char *dump = argv[1], *png = argv[2];
    int target = RD_TARGET_DISPLAY;
    uint32_t pw = 0, ph = 0;
    /* the display options */
    RdSettings s;
    bool list = false, overlay = false, stats = false, passTimes = false;
    int timeN = 0; /* --time N */
    bool noDevice = false, aspectGiven = false;
    bool keysCheck = false;
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
        } else if (strcmp(argv[i], "--downsample") == 0) {
            s.downsample = 1;
        } else if (strcmp(argv[i], "--full-pixel") == 0) {
            s.fullPixel = 1;
        } else if (strcmp(argv[i], "--border-mask") == 0) {
            s.borderMask = 1;
        } else if (strcmp(argv[i], "--motion-blur") == 0 && i + 1 < argc) {
            const char *m = argv[++i];
            if (strcmp(m, "smooth") != 0 && strcmp(m, "original") != 0) {
                fprintf(stderr, "bad --motion-blur (original or smooth)\n");
                return 1;
            }
            motionSmooth = strcmp(m, "smooth") == 0;
            s.motionBlurSmooth = motionSmooth ? 1 : 0;
        } else if (strcmp(argv[i], "--presents") == 0 && i + 1 < argc) {
            char *end = NULL;
            const long n = strtol(argv[++i], &end, 10);
            if (!end || *end != '\0' || n < 1 || n > (long)RD_BLUR_SMOOTH_K_MAX) {
                fprintf(stderr, "bad --presents (1..%u)\n", RD_BLUR_SMOOTH_K_MAX);
                return 1;
            }
            presentsN = (int)n;
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
            rd__crack_fill_count_all(true); /* the replay's own count */
        } else if (strcmp(argv[i], "--no-crack-fill") == 0) {
            rd__crack_fill_set_enabled(false);
        } else if (strcmp(argv[i], "--work-clear") == 0 && i + 1 < argc) {
            char *end = NULL;
            const char *v = argv[++i];
            const unsigned long rgb = strtoul(v, &end, 16);
            if (!end || *end != '\0' || end - v != 6) {
                fprintf(stderr, "bad --work-clear (six hex digits, RRGGBB)\n");
                return 1;
            }
            rd__set_work_clear(true, (uint32_t)rgb);
        } else if (strcmp(argv[i], "--no-glow-edge") == 0) {
            rd__set_glow_edge_fill(false);
        } else if (strcmp(argv[i], "--keys") == 0) {
            keysCheck = true;
        } else if (strcmp(argv[i], "--pass-times") == 0) {
            stats = passTimes = true;
        } else if (strcmp(argv[i], "--time") == 0 && i + 1 < argc) {
            char *end = NULL;
            const long n = strtol(argv[++i], &end, 10);
            if (!end || *end != '\0' || n < 1 || n > 100000) {
                fprintf(stderr, "bad --time (a count of replays, 1..100000)\n");
                return 1;
            }
            timeN = (int)n;
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
        } else if ((strcmp(argv[i], "--nop") == 0 || strcmp(argv[i], "--interp-nop-prev") == 0) &&
                   i + 1 < argc && nopCount < 64) {
            unsigned l = 0, a = 0, b = 0;
            int n = sscanf(argv[++i], "%u:%u-%u", &l, &a, &b);
            if (n < 2 || l >= RD_LIST_COUNT) {
                fprintf(stderr, "bad %s\n", argv[i - 1]);
                return 1;
            }
            nops[nopCount].l = l;
            nops[nopCount].a = a;
            nops[nopCount].prev = strcmp(argv[i - 1], "--interp-nop-prev") == 0;
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
    for (int k = 0; k < nopCount; k++) {
        if (nops[k].prev && !interpPrev) {
            fprintf(stderr, "--interp-nop-prev needs --interp\n");
            return 1;
        }
    }
    if (presentsN && (!interpPrev || passDir || timeN)) {
        fprintf(stderr, "--presents needs --interp T PREV (and not --pass-dump or --time)\n");
        return 1;
    }
    uint32_t gw = 0, gh = 0;
    if (!unzipDump(&dump, 0) || (interpPrev && !unzipDump(&interpPrev, 1))) {
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
    /* the disc the frame was drawn on (dump version 12; "pal" before): the
     * reduction's crop and the border mask are its (rd_post.c
     * rd__reduction_crop), so the replay draws what the player saw */
    if (ico_region_select(info.region) != 0) {
        fprintf(stderr, "%s: the dump's region '%s' is not a row of config/regions.txt\n", dump,
                info.region);
        return 1;
    }
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
    if (keysCheck && noDevice) {
        fprintf(stderr, "--keys needs a device\n");
        return 77;
    }
    if (noDevice && !list && !texDir && !meshName && !vuRefCount) {
        fprintf(stderr, "--no-device renders nothing: give --list, --mesh or --dump-textures\n");
        return 1;
    }
    if (noDevice || !rd_init(gw, gh, &s, NULL)) {
        if (!noDevice && !list) {
            fprintf(stderr, "no usable %s device\n", rhi_backend_kind_name(rhi_backend()));
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
        printf("dump: version %u, GS %ux%u, aspect %.4f (%s), visible width %g, cull width %u, "
               "region %s\n",
               info.version, info.gsW, info.gsH, (double)info.aspect,
               aspectName(info.aspect, (char[16]){0}), (double)info.wExact, info.cullW,
               info.region);
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
    if (keysCheck) {
        const int krc = checkKeys(dump, rf);
        rd__frame_free(&f);
        rd__frame_free(&pf);
        rd_shutdown();
        return krc;
    }
    if (passA) {
        rd__replay_set_window_draws(passA, passB, drawA, drawA ? drawB : 0u, maskMarks ? 1 : 0, 0);
    } else if (maskMarks) {
        rd__replay_set_window_draws(1, UINT32_MAX, 0, 0, 1, 0);
    }
    if (presentsN && !list) {
        rc = presentsRun(dump, png, &pf, &f, target, ow, oh, want, pw, ph);
        rd__frame_free(&f);
        rd__frame_free(&pf);
        rd_shutdown();
        return rc;
    }
    if (motionSmooth) {
        /* a replay of its own is a tick's first present; its trail is the
           --prev frames' (none without them: the trail starts here) */
        rd__blur_smooth_arm(1, 1);
        if (!prevCount) {
            printf("note: --motion-blur smooth on one replay holds no trail (give --prev DUMP or "
                   "--interp T PREV --presents N)\n");
        }
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
        if (pr->backendWriteBytes) {
            printf("%s: stats: Direct3D 11 submits wrote %llu bytes to the GPU\n", dump,
                   (unsigned long long)pr->backendWriteBytes);
        }
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
         * static; of them the blend equations for draws without blending
         * and those that replaced a dual-source one (vk_cmd.c
         * vkr_flush_dynamic, issues 146 and 154) */
        printf("%s: stats: %u dynamic state sets (dynamic depth %s, blend %s) (%u for draws "
               "without blending, %u of them replacing a dual-source one)\n",
               dump, pr->dynamicSets, g_rd.dynDepth ? "on" : "off", g_rd.dynBlend ? "on" : "off",
               pr->blendEqOffSets, pr->dualLeftovers);
        /* the present's box reduces (rd_present.c): the downsample to the
         * GS size (--downsample) and the reduce to the window's box */
        uint32_t rdn[4], rwn[4];
        if (pw && rd__last_present_reduce(rdn, rwn)) {
            char dn[64] = "off", wn[64] = "off";
            if (rdn[2]) {
                snprintf(dn, sizeof(dn), "%ux%u -> %ux%u", rdn[0], rdn[1], rdn[2], rdn[3]);
            }
            if (rwn[2]) {
                snprintf(wn, sizeof(wn), "%ux%u -> %ux%u", rwn[0], rwn[1], rwn[2], rwn[3]);
            }
            printf("%s: stats: present: downsample %s, window reduce %s\n", dump, dn, wn);
        }
        /* the tiler passes (rd_replay.c: DATE through the stencil bit, the
         * fog's local read, the blur pairs folded) */
        printf("%s: stats: DATE snapshots %u, DATE stencil draws %u (%u setups); fog local draws "
               "%u (fog path %s); blur pairs folded %u\n",
               dump, pr->dateSnapshots, pr->dateStencilDraws, pr->dateStencilTakes,
               pr->fogLocalDraws, rd__fog_path_name(g_rd.fogPath), pr->blurFolds);
        /* the shadow count's resolves (rd_replay.c doShadowResolve) */
        printf("%s: stats: shadow count: %u in one draw, %u in seven draws (profile %s)\n", dump,
               pr->shadowOne, pr->shadowSeven, g_rd.profile.shadowOne ? "one draw" : "seven draws");
        /* the pixel diet (rd_alpha_plan.c, rd_fx_plan.c, rd_pixel_plan.c) */
        printf("%s: stats: pixel diet: alpha plan %u one-pass, %u opaque; fx plan %u constant, %u "
               "additive, %u local; vu no-planes %u\n",
               dump, pr->alphaOnePass, pr->alphaOpaque, pr->fxConstant, pr->fxAdditive, pr->fxLocal,
               pr->vuNoPlanes);
        /* the VU vertex and triangle pre-passes (rd_replay_vu.c rd__vu_prepass):
         * each mesh and grid vertex and triangle and each sprite once, before
         * the lists */
        printf("%s: stats: VU pre-pass %s: %u passes, %u vertices in %u draws; %u mesh and grid "
               "draws the old way; %u sprites, %u triangle slots; %u VU draws with the ticks' "
               "cameras\n",
               dump, g_rd.vuPrepass ? "on" : "off", pr->vuPrePasses, pr->vuPreVertices,
               pr->vuPreDraws, pr->vuPreFallbacks, pr->vuPreSprites, pr->vuTriSlots,
               pr->vuTickDraws);
        /* the draw-merge count (rd_replay_vu.c vuMergeNote): how many VU
         * draws could join the draw before them (same pipeline, textures,
         * DrawCB bytes, scissor and FrameCB, one pass, no DATE stencil,
         * storage pass or skin), counting only; and the pre-pass's batches */
        {
            uint32_t md = 0, mr = 0;
            char per[13 * 28];
            size_t pn = 0;
            per[0] = '\0';
            for (int l = 0; l < RD_LIST_COUNT; l++) {
                md += pr->vuMergeDraws[l] + pr->vuMergeRuns[l];
                mr += pr->vuMergeRuns[l];
                pn +=
                    (size_t)snprintf(per + pn, sizeof(per) - pn, "%s%d:%u/%u", l ? " " : "", l,
                                     pr->vuMergeDraws[l] + pr->vuMergeRuns[l], pr->vuMergeRuns[l]);
            }
            printf("%s: stats: VU merge: %u draws in %u runs (mean %.1f a run); blocked by "
                   "pipeline %u, texture group %u, DrawCB %u, split/DATE/dst %u, scissor/FrameCB "
                   "%u; per list: %s\n",
                   dump, md, mr, mr ? (double)md / (double)mr : 0.0, pr->vuMergeBlocked[0],
                   pr->vuMergeBlocked[1], pr->vuMergeBlocked[2], pr->vuMergeBlocked[3],
                   pr->vuMergeBlocked[4], per);
            printf("%s: stats: VU pre-pass batches: %u distinct (stage, frame, stream, bones) of "
                   "%u draws\n",
                   dump, pr->prepassBatches, pr->vuPreDraws);
            /* the draw merge as drawn (rd_replay_vu.c rd__replay_vu_flush;
             * g_rd.vuMerge): the merge builds' draws and the GPU draws they
             * took */
            printf("%s: stats: VU merge: %u draws in %u GPU draws (%u merged into %u, %u alone; "
                   "merge %s)\n",
                   dump, pr->vuMergeIn + pr->vuMergeAlone, pr->vuMergeGpu + pr->vuMergeAlone,
                   pr->vuMergeIn, pr->vuMergeGpu, pr->vuMergeAlone, g_rd.vuMerge ? "on" : "off");
            /* code 36's cut and kick pairs (rd_replay_vu.c vuFuseVerdict;
             * g_rd.vuFuse): fused, kept apart by reason, under one pipeline
             * already */
            uint32_t kept = 0;
            for (int k = 0; k < RD_VU_FUSE_WHY_COUNT; k++) {
                kept += pr->vuFuseKept[k];
            }
            printf("%s: stats: VU fuse: %u pairs fused, %u kept apart (blend form %u, plans "
                   "differ %u, passes %u, dst/DATE %u, no planes %u, no merge build %u, fuse off "
                   "%u), %u under one pipeline already; fuse %s\n",
                   dump, pr->vuFusePairs, kept, pr->vuFuseKept[RD_VU_FUSE_BLEND],
                   pr->vuFuseKept[RD_VU_FUSE_PLANS], pr->vuFuseKept[RD_VU_FUSE_PASSES],
                   pr->vuFuseKept[RD_VU_FUSE_DST], pr->vuFuseKept[RD_VU_FUSE_PLANES],
                   pr->vuFuseKept[RD_VU_FUSE_BUILD], pr->vuFuseKept[RD_VU_FUSE_OFF], pr->vuFuseSame,
                   g_rd.vuFuse ? "on" : "off");
        }
        /* the crack filler above 1x (rd_crack_fill.c): its count read back */
        rd__crack_fill_wait();
        RdCrackCounts cc;
        rd__crack_fill_counts(&cc);
        printf("%s: stats: crack fill %s: %u passes, %llu pixels filled (%llu replays counted; "
               "scale %.4gx%.4g, reach %ux%u texels; %llu fogged replays above 1x not filled; "
               "%llu fills inside the scene pass with the local fog; %llu fills after the MSAA "
               "resolve)\n",
               dump, cc.enabled ? "on" : "off", pr->crackFillPasses, (unsigned long long)cc.pixels,
               (unsigned long long)cc.counted, (double)cc.sx, (double)cc.sy, cc.rx, cc.ry,
               (unsigned long long)cc.skipped, (unsigned long long)cc.folded,
               (unsigned long long)cc.afterMsaa);
        uint32_t satDraws = 0;
        const uint64_t sat = satZVertices(rf, &satDraws);
        printf("%s: stats: VU corners with a saturated GS Z: %llu vertices of region-mode meshes "
               "in %u draws (in a GPU-clipped triangle at their GS position: %s)\n",
               dump, (unsigned long long)sat, satDraws,
               rd__vu_extended() ? "yes, not out of the box" : "no, out of the box");
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
    if (rc == 0 && timeN > 0 && !timeReplays(dump, &pf, &f, rf, timeN, pw != 0)) {
        rc = 1;
    }
    rd__frame_free(&f);
    rd__frame_free(&pf);
    rd_shutdown();
    return rc;
}
