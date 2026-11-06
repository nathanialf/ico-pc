/* rd_vu_skin_check.h: the skinned draws' bone-block check (issue 163;
 * docs/port/VU_SKIN_CHECK.md).
 *
 * Issue 163's streak (v0.10.3, Turnip on an Adreno 740) is one skinned
 * draw whose vertices on one bone were placed as if that bone's matrix in
 * VuBoneCB had read as zeros, with the bytes the CPU wrote right.  Which
 * layer zeroed it (the driver's uniform read, the ring's bytes under the
 * GPU, robust access only) is what this check names from a reporter's next
 * log, with nothing for the reporter to switch on:
 *
 *   S2  the in-game check (rd_vu_skin_check.c): at most every 100 ms one
 *       skinned command of the replay, round robin, is drawn again after
 *       the replay's last pass through the vertex module its draw used in
 *       probe mode (ICO_VU_PROBE: GS X, Y, Z of each vertex into a 16-wide
 *       RGBA8_UINT target of its own), with the draw's VuCB and stream
 *       binding and the SAME VuBoneCB offset; beside it the variants (c1)
 *       the CPU's bytes at a fresh ring offset of another page phase and
 *       (c2) a pipeline with uniform-buffer robustness off
 *       (VK_EXT_pipeline_robustness, where robust access is on and the
 *       device offers it); the draw's 3,840 VuBoneCB bytes copied from the
 *       ring into the same readback buffer.  When the slot's fence has
 *       signalled (rd__wait_frame, never a stall) the GPU's copy and the
 *       ring as the CPU reads it are compared with the bytes the CPU wrote
 *       and every probe with vu1_ref's cluster program run on the CPU.
 *   S3  the box check: the checked command drawn again inside its own pass
 *       right after its draw, through its own pipeline key with colour
 *       writes and Z writes off, inside an occlusion query whose scissors
 *       cover the frame less the CPU's box of the draw (+ 2 GS pixels):
 *       any sample is a vertex the GPU placed where the CPU does not.
 *   S1  the start-up probe (rd_vu_skin_selftest.c): 60 bones, 120 vertices
 *       weighting every address 16..252 in both lanes, through codes 20,
 *       22 and 24, with the block at 64 ring offsets of every 64-byte page
 *       phase, against vu1_ref.
 *
 * Nothing the check draws is meant to reach a picture: the probes into
 * their own target after the last pass, the box check with no write at all.
 * On by itself only on an Adreno (Qualcomm's driver or Turnip: issue 163's
 * devices) with a window; off elsewhere (issue 179's Mali-G52 is GPU-bound:
 * no new GPU work there).  [dev] vu_skin_check = false
 * (ICO_RD_VU_SKIN_CHECK=0) turns it off, true (=1) on on any Vulkan device,
 * headless too (the tests; rd_replay_tool --skin-check). */
#ifndef PORT_RENDER_RD_VU_SKIN_CHECK_H
#define PORT_RENDER_RD_VU_SKIN_CHECK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rd_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------- the judgement
 * rd_vu_skin_judge.c: pure functions over vu1_ref (no device). */

/* the most vertices one check probes */
#define RD_SKIN_MAXV 1024u
/* the most checks one replay holds (mode 2), and the box check's
 * occlusion queries: check i's is query i (rhi_occlusion_prepare) */
#define RD_SKIN_CHECKS_ALL 80u
/* the twins (rd_vu_skin_check.c): at most this many skinned draws of one
 * replay twinned; twin t's occlusion queries are RD_SKIN_CHECKS_ALL + 3 t
 * (A, the draw's bone offset), + 1 (B, the same bytes at a fresh offset)
 * and + 2 (C, kept for the ring read's twin), so every query the check
 * uses is below RD_SKIN_QUERIES (rhi_occlusion_prepare) */
#define RD_SKIN_TWINS_MAX 128u
#define RD_SKIN_QUERIES (RD_SKIN_CHECKS_ALL + 3u * RD_SKIN_TWINS_MAX)
/* a vertex is wrong when its X or Y is more than this far (1/16 pixel:
 * one GS pixel) from the CPU's, or its Z more than RD_SKIN_Z_REL of the
 * CPU's (at least RD_SKIN_XY_TOL) */
#define RD_SKIN_XY_TOL 16
#define RD_SKIN_Z_REL (1.0 / 1024.0)

/* one batch of the probed vertices: [first, first + count) of the stream */
typedef struct RdSkinBatch {
    uint32_t first, count;
} RdSkinBatch;

/* A skinned draw as the CPU sees it: the cluster code (20, 22, 24), VU
 * memory 0..35 (VuCB.mem), VuBoneCB's 240 qwords as the CPU wrote them, the
 * stream's vertices (qpv qwords each, vertex 0 at stream[0]) and the
 * batches of the probed range (vertex indices relative to stream). */
typedef struct RdSkinCase {
    int code;
    const float (*mem)[4];
    const float (*bones)[4];
    const float (*stream)[4];
    uint32_t qpv;
    const RdSkinBatch *batches;
    uint32_t batchCount;
    uint32_t count; /* the vertices: the batches cover [0, count) */
} RdSkinCase;

/* the CPU's position of a vertex: GS X, Y (12.4) and Z, whether a batch
 * holds it, and whether it is compared (in front of the eye, nothing
 * saturated) */
typedef struct RdSkinPos {
    int32_t x, y, z;
    uint8_t inBatch, compared;
} RdSkinPos;

/* vu1_ref's cluster program over the case, the bone at VU address zeroAddr
 * (16..252, 0: none) read as zeros (its four qwords), into out[0 ..
 * count); false when a batch is too long for the reference */
bool rd__skin_reference(const RdSkinCase *k, uint32_t zeroAddr, RdSkinPos *out);
/* the same with the bone's four qwords filled with the 32-bit pattern
 * fillBits instead of zeros (0x7FC00000: a NaN matrix, the judgement's
 * non-finite candidate) */
bool rd__skin_reference_fill(const RdSkinCase *k, uint32_t zeroAddr, uint32_t fillBits,
                             RdSkinPos *out);
/* a probed position the shader dropped: X, Y or Z saturated by ftoi4 (a
 * non-finite or huge value: a NaN takes 0x80000000, vu_common.hlsli
 * vu_ftoi) */
bool rd__skin_got_dropped(const int32_t got[3]);

/* whether a probed position (GS X, Y, Z) is the CPU's within the bounds */
bool rd__skin_pos_near(const int32_t got[3], const RdSkinPos *want);

/* A probe's verdict against the reference ref (rd__skin_reference with no
 * zero bone): compared and wrong vertices, the first wrong one and its
 * positions, the VU address whose zero matrix gives the most wrong
 * vertices' positions (0: none of the wrong vertices' addresses does;
 * lanes then names the first wrong vertex's),
 * whether it gives the first wrong vertex's (explains), how many it gives,
 * and the largest X/Y difference on a right vertex (1/16 pixel). */
typedef struct RdSkinVerdict {
    uint32_t compared, wrong;
    uint32_t firstWrong;
    uint32_t lanes[2]; /* the first wrong vertex's weight addresses (lanes x and z) */
    int32_t got[3];
    RdSkinPos want;
    uint32_t addr;
    bool explains;
    uint32_t explained;
    int32_t maxRightXY;
    /* of the wrong vertices, those the probe dropped (rd__skin_got_dropped:
     * read as non-finite or huge), and the address whose non-finite (NaN)
     * matrix drops the most of them (0: none, or none dropped) */
    uint32_t dropped;
    uint32_t nanAddr, nanExplained;
} RdSkinVerdict;

void rd__skin_judge(const RdSkinCase *k, const RdSkinPos *ref, const int32_t (*got)[3],
                    RdSkinVerdict *v);

/* the first and last 16-byte qword at which a and b (bytes long) differ:
 * false when equal */
bool rd__skin_bytes_differ(const void *a, const void *b, uint32_t bytes, uint32_t *q0,
                           uint32_t *q1);
/* "equal" or "differs at qwords Q..R" */
void rd__skin_bytes_words(bool differ, uint32_t q0, uint32_t q1, char *out, size_t size);

/* a variant's state for rd__skin_decision */
enum { RD_SKIN_VAR_NOT_RUN = -1, RD_SKIN_VAR_WRONG = 0, RD_SKIN_VAR_RIGHT = 1 };

/* The decision table's words (docs/port/VU_SKIN_CHECK.md) for one check:
 * the GPU's copy of the block differs from the CPU's bytes, the ring as
 * the CPU reads it after the fence differs, the probe has a wrong vertex,
 * and variants c1 (a fresh ring offset) and c2 (uniform robustness off)
 * (RD_SKIN_VAR_*).  A static string. */
const char *rd__skin_decision(bool copyDiffers, bool rereadDiffers, bool probeWrong, int c1,
                              int c2);

/* ICO_RD_VU_SKIN_CHECK's value: -1 unset (NULL or ""), 2 for "2" (every
 * skinned command of every replay: rd_vu_skin_check_ab), 1 for 1, true, on
 * or yes, 0 for anything else (rd_profile.c envBool's rule: "false" is off) */
int rd__skin_check_env(const char *value);
/* The check's mode: env (rd__skin_check_env) when set, else on (1) only on
 * an Adreno with a window, off (0) elsewhere */
int rd__skin_check_default(bool windowed, bool adreno, int env);
/* rd_profile.c: an Adreno, Qualcomm's driver or Turnip (the profile's
 * PC_ADRENO_QUALCOMM and PC_ADRENO_TURNIP): from the ids (vendor 0x5143, not
 * through MoltenVK) and of the device in use */
bool rd__profile_vendor_adreno(uint32_t vendorId, bool moltenvk);
bool rd__profile_adreno(void);

/* the box check's pipeline slots (distinct keys, kept for the run), and
 * every pipeline the check holds outside the renderer's cache: the probe's
 * three, c2's three and the box checks' (rd_pipeline.c counts them in the
 * pool's headroom, rhi_pipeline_pool.h) */
#define RD_VU_SKIN_BOX_PIPES 16
#define RD_VU_SKIN_PIPES (3u + 3u + RD_VU_SKIN_BOX_PIPES)

/* ------------------------------------------------------- the engine
 * rd_vu_skin_check.c, rd_vu_skin_selftest.c; the replay calls these. */

/* whether the in-game check runs (the device, the settings, the run) */
bool rd__vu_skin_check_on(void);
/* why it is on or off, for the start and off lines: "on by default on an
 * Adreno with a window (Turnip)", "[dev] vu_skin_check = true
 * (ICO_RD_VU_SKIN_CHECK=1)", "not an Adreno (...turns it on)", ... */
const char *rd__vu_skin_check_why(void);
/* the tests' and the tool's switch over the settings: 0 off, 1 the game's
 * rule (one command every 100 ms), 2 every skinned command of every replay
 * (rd_replay_tool --skin-check; the 100 ms and the one-a-replay limits
 * off), -1 back to the settings */
void rd__vu_skin_check_set(int mode);

/* doVuDraw: a skinned command's draw is to be checked (sets the arm that
 * rd__vu_skin_note reads); f, c the command, m its mesh */
void rd__vu_skin_pick(const RdFrame *f, const RdCmd *c, const RdMeshRec *m);
/* set by rd__vu_skin_pick for the vuDraw that follows: g_rdSkinArm the
 * sampled probe's draw, g_rdSkinTwin a draw to twin (every skinned draw
 * where the twins are on: rd__vu_skin_twins_on) */
extern bool g_rdSkinArm;
extern bool g_rdSkinTwin;
/* whether the twins run: on an Adreno with a window (Qualcomm's driver and
 * Turnip, the check's default rule) wherever the check runs, under
 * ICO_RD_VU_SKIN_CHECK=2 (and the tool's and the tests' mode 2) anywhere;
 * rd__vu_skin_twins_set (the tests) over it.  The probe's interval stays
 * 0 (one a present) on Turnip alone, 100 ms elsewhere */
bool rd__vu_skin_twins_on(void);
/* the tests' switch: 1 the twins on, 0 off, -1 back to the rule */
void rd__vu_skin_twins_set(int on);
/* the occlusion queries the check uses (RD_SKIN_QUERIES with the twins,
 * RD_SKIN_CHECKS_ALL without): the start-up probe prepares them */
uint32_t rd__vu_skin_queries(void);
/* B2's hook (the ring-read module, not built yet): the probe pipeline of
 * column 3 (the draw's bones through the ring) for the cluster entry vs,
 * {0} while there is none (column 3 not drawn) */
RhiPipeline rd__vu_skin_ring_module(uint8_t vs);

/* vuDraw, after a picked command's draw (inside its pass): what the probe
 * needs of the draw, and the box check drawn here (S3).  The types are
 * rd_replay.h's and rhi.h's, passed as pointers. */
struct Replay;
struct DrawSetup;

typedef struct RdSkinDraw {
    const RdPipeKeyInt *key;       /* its first pass's */
    RhiPipeline pipe;              /* that pass's pipeline */
    RhiBindGroup g1, g2;           /* group 1 (VU) and 2 */
    const void *vcb;               /* its IcoVuCB as bound (vu_draw.x set) */
    uint64_t vOff, dOff, bonesOff; /* the ring offsets bound at b2, b1, b3 */
    RhiBuffer indexBuf;            /* id 0: not indexed */
    uint64_t indexOff;
    uint32_t first, count; /* its indices */
    uint8_t vs;            /* the vertex entry (RD_VS_*) of the program */
} RdSkinDraw;

void rd__vu_skin_note(struct Replay *r, const struct DrawSetup *ds, const RdSkinDraw *d);

/* rd_replay.c replayFrame, after the last pass: the picked command's
 * probes and copies (outside every pass) */
void rd__vu_skin_end(void);
/* rd__wait_frame: a check whose slot the fence has now let go read and
 * judged (its lines logged) */
void rd__vu_skin_harvest(void);
/* the ring bytes a replay's check can add (rd__estimate_ring) */
uint64_t rd__vu_skin_ring_bytes(void);
/* the 10 s summary's line (rd_interp.c presentLog) */
void rd__vu_skin_block(void);
/* rd_shutdown */
void rd__vu_skin_shutdown(void);

/* rd_replay_tool --skin-check: the skinned commands of f checked in
 * replays of f (every command once, mode 2), each harvested at once;
 * counts and the last wrong line's text into the summary.  Returns the
 * wrong checks, -1 when f could not be replayed. */
typedef struct RdSkinSummary {
    uint32_t commands, checks, wrong, blocksDiffer, rereadDiffer, boxChecks, boxOutside;
    uint32_t c1Right, c2Right, c2Run;
    uint32_t lastAddr;
    char decision[160];
    /* the twins read, those whose two counts differed, those drawn through
     * the draw's own pipeline (its attachment read), the checks targeted
     * after a twin event, of them wrong, the wrong vertices the probe
     * dropped (non-finite) */
    uint32_t twins, twinsDiffer, twinsOwn, targeted, targetedWrong, dropped;
} RdSkinSummary;

int rd__vu_skin_check_frame(const RdFrame *f, int keep, RdSkinSummary *out);
/* The tests: f replayed `replays` times under the check's mode as it is
 * (the schedule, the twins, the targeted probe), each replay's check and
 * twins harvested at once, the counts into out (zeroed first).  Returns
 * the wrong checks, -1 when f could not be replayed. */
int rd__vu_skin_check_replays(const RdFrame *f, int keep, uint32_t replays, RdSkinSummary *out);

/* ---------------------------------------------------- the capture
 * On the run's first failure (twins that differ, or a wrong check), once
 * a run: the sidecar <dir>/vu-skin-fail-<frame>-<list>-<index>.bin
 * (rd_vu_skin_capture.c writes it) and a frame dump asked of the window
 * (rd.h rd_vu_skin_capture_due). */
#define RD_SKIN_CAPTURE_MAGIC "ICOSKINF"
#define RD_SKIN_CAPTURE_VERSION 1u

/* the sidecar's header (little-endian, as written): every section's
 * offset from the file's start and its bytes (0: not in this capture) */
typedef struct RdSkinCaptureHeader {
    char magic[8];    /* RD_SKIN_CAPTURE_MAGIC */
    uint32_t version; /* RD_SKIN_CAPTURE_VERSION */
    uint32_t headerBytes;
    uint32_t frame, list, index, meshId, meshVertices;
    int32_t code;
    /* bit 0 a check's data (the probe), 1 the twins' counts, 2 the ring
     * re-read after the fence, 3 the check was wrong, 4 the twins differed,
     * 5 the check targeted after a twin event */
    uint32_t flags;
    uint32_t probeFirst, probeVertices, probeCols, wrong, dropped, addr, nanAddr;
    uint64_t bonesOff, freshOff;
    uint64_t twinA, twinB, twinC; /* the samples (UINT64_MAX: not drawn) */
    char mesh[24];
    char fs[32];     /* the twins' fragment entry */
    char reads[160]; /* the decision's words */
    /* sections: the CPU's bone bytes (3,840), the GPU's copy (3,840), the
     * ring as the CPU read it after the fence (3,840), the draw's VuCB, VU
     * memory 0..35, the stream's slice (5 qwords a vertex), the probe's
     * rows (int32 X, Y, Z a vertex a column) */
    uint32_t cpuOff, cpuBytes, gpuOff, gpuBytes, ringOff, ringBytes, vcbOff, vcbBytes;
    uint32_t memOff, memBytes, streamOff, streamBytes, rowsOff, rowsBytes;
} RdSkinCaptureHeader;

typedef struct RdSkinCapture {
    RdSkinCaptureHeader h;
    const void *cpu, *gpu, *ring, *vcb, *mem, *stream, *rows;
} RdSkinCapture;

/* writes c to <dir>/vu-skin-fail-<frame>-<list>-<index>.bin (the
 * sections whose pointer is NULL left out); the path into path; false
 * when the file could not be written (why in path) */
bool rd__vu_skin_capture_write(const char *dir, RdSkinCapture *c, char *path, size_t size);
/* the window's frame dump asked (rd_vu_skin_capture_due), path the
 * sidecar's ("" none), and the folder rd_set_vu_skin_capture_dir set */
void rd__vu_skin_capture_ask(const char *path);
const char *rd__vu_skin_capture_dir(void);

/* The start-up probe (S1; rd_vu_skin_selftest.c): run by
 * rd_vuprepass_selftest before anything else, on a Vulkan device, unless
 * the check is off.  Logs "VU skin probe: ok (...)" or "... wrong at offset
 * ..." and the check's start line.  Returns whether it was right (true
 * when not run). */
bool rd__vu_skin_selftest(void);

/* shared by the check and the start-up probe: the probe pipeline of the
 * cluster entry vs (RD_VS_VU_SKIN .. RD_VS_VU_SKIN_DEBUG; robustOff: c2's,
 * {0} until its background compile is done or where it is not offered),
 * made at first use, and the probe's index list (fields 0..2 of 8192
 * vertices, 9 indices a vertex) */
RhiPipeline rd__vu_skin_probe_pipeline(uint8_t vs, bool robustOff);
RhiBuffer rd__vu_skin_index_buffer(void);

/* rd_pipeline.c: the pipeline description of a key, as rd__get_pipeline
 * makes it (layouts: 4 entries the description points to) */
bool rd__pipeline_desc(const RdPipeKeyInt *k, RhiPipelineDesc *d, RhiBindGroupLayout layouts[4]);
/* rd_pipeline.c: the key rd__get_pipeline hashes for k in the pass now
 * (drawKey: the pass's sample count, the dynamic state and the attachment
 * read's mask normalised), so a pipeline made beside the cache is told
 * apart by the pass it can draw in */
void rd__pipeline_key_norm(const RdPipeKeyInt *k, RdPipeKeyInt *out);
/* rd_pipeline.c: note to the activity function (rd_set_activity_fn) as
 * it is; rd__pipeline_note_end puts back the note rd__get_pipeline would
 * leave */
void rd__activity_note(const char *note);

#ifdef __cplusplus
}
#endif

#endif
