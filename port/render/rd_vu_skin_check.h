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
 * Nothing the check draws reaches a picture: the probes into their own
 * target after the last pass, the box check with no write at all.  On by
 * itself on a window's Vulkan device; [dev] vu_skin_check = false
 * (ICO_RD_VU_SKIN_CHECK=0) turns it off, ICO_RD_VU_SKIN_CHECK=1 turns it on
 * in a headless run (the tests; rd_replay_tool --skin-check). */
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

/* ------------------------------------------------------- the engine
 * rd_vu_skin_check.c, rd_vu_skin_selftest.c; the replay calls these. */

/* whether the in-game check runs (the device, the settings, the run) */
bool rd__vu_skin_check_on(void);
/* the tests' and the tool's switch over the settings: 0 off, 1 the game's
 * rule (one command every 100 ms), 2 every skinned command of every replay
 * (rd_replay_tool --skin-check; the 100 ms and the one-a-replay limits
 * off), -1 back to the settings */
void rd__vu_skin_check_set(int mode);

/* doVuDraw: a skinned command's draw is to be checked (sets the arm that
 * rd__vu_skin_note reads); f, c the command, m its mesh */
void rd__vu_skin_pick(const RdFrame *f, const RdCmd *c, const RdMeshRec *m);
/* set by rd__vu_skin_pick for the vuDraw that follows */
extern bool g_rdSkinArm;

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
} RdSkinSummary;

int rd__vu_skin_check_frame(const RdFrame *f, int keep, RdSkinSummary *out);

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

#ifdef __cplusplus
}
#endif

#endif
