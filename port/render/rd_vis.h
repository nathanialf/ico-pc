/* rd_vis.h: the port's visibility test per scene draw (rd_vis.c;
 * docs/port/RENDER.md, "Culling").
 *
 * Once a picture, before the replay's scans (rd_replay.c replayFrame), every
 * mesh, skinned and grid command of lists 0 to 10 is tested on the CPU: a
 * command whose vertices all lie beyond one plane of the bound target's GS
 * rectangle (left, right, top, bottom, with a margin) or all behind the eye
 * draws no pixel, and the replay walks a shadow copy of the frame in which
 * it is an RDC_NOP (the replay tool's --nop): no pre-pass entry, no VU draw,
 * no ring bytes.  Conservative: a command is skipped only when the bound of
 * every vertex it can draw proves it; anything the test cannot bound is
 * drawn.  The picture is the same, byte for byte (rd_cull_ab).
 *
 * [dev] cull = off | frustum | full (ICO_RD_CULL: off or 0, frustum or 1,
 * full; default frustum).  full adds the occlusion test (rd_vis_hiz.c): a
 * draw of list 0 whose every sample lies behind what the opaque draws
 * before it wrote to its depth buffer is skipped too. */
#ifndef PORT_RENDER_RD_VIS_H
#define PORT_RENDER_RD_VIS_H

#include <stdbool.h>
#include <stdint.h>

#include "rd_internal.h"
#include "rd_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the mode ([dev] cull, ICO_RD_CULL) */
enum { RD_VIS_OFF = 0, RD_VIS_FRUSTUM = 1, RD_VIS_FULL = 2 };

/* a command's verdict: drawn, or skipped beyond a plane of the target's
 * rectangle or behind the eye */
enum {
    RD_VIS_KEEP = 0,
    RD_VIS_LEFT,
    RD_VIS_RIGHT,
    RD_VIS_TOP,
    RD_VIS_BOTTOM,
    RD_VIS_NEAR,
    RD_VIS_OCCLUDED, /* behind the depth list 0's occluders wrote (rd_vis_hiz.c) */
    RD_VIS_VERDICTS
};

/* The model-space bound of a stream's positions (qword 0 of each vertex):
 * x, y, z and the w lane (the scissor programs read pos.w,
 * vu_prelit.hlsl), lo and hi inclusive.  n: the vertices bounded (0: an
 * empty bound, which the test never skips). */
typedef struct RdVisBox {
    float lo[4], hi[4];
    uint32_t n;
} RdVisBox;

/* The bone slots vu_skin.hlsl reads: a weight's VU address a gives the
 * qwords i .. i + 3 of VuBoneCB, i = min(a - 16, 236) (unsigned). */
#define RD_VIS_SLOTS 237u

/* A skinned stream's bound (RD_VU_QW_SKIN layout: pos, normal, weights
 * (address, w0, address, w1), ...): per bone slot the box of the
 * positions of the vertices whose either weight names it.  ok: every
 * position finite and every weight in [0, 1] (else nothing is skipped);
 * qwEnd: one past the highest VuBoneCB qword a slot reads (slot + 4);
 * emax: the largest |w0 + w1 - 1|. */
typedef struct RdVisSkin {
    bool ok;
    uint32_t qwEnd;
    double emax;
    uint32_t used[(RD_VIS_SLOTS + 31u) / 32u];
    RdVisBox box[RD_VIS_SLOTS];
} RdVisSkin;

/* The bound target's GS rectangle a draw cannot write outside: x0 .. x1
 * across, y0 .. y1 down, in GS primitive coordinates (the programs'
 * positions, X / 16 of XYZ2): the target's pixel 0 is at XYOFFSET, 2048
 * less half the size the state gives (rd_replay.c frameRefresh) */
typedef struct RdVisRect {
    double x0, y0, x1, y1;
} RdVisRect;

/* the margins (GS pixels) of the two planes tested at each side, and the
 * relative tolerance of every plane value (vu_mat's float rounding in the
 * shader and the clip-space mapping's, in units of the sum of the
 * absolute products) */
#define RD_VIS_MARGIN 2.0
#define RD_VIS_MARGIN_IN 1.0
#define RD_VIS_TOL (1.0 / 262144.0) /* 2^-18 */

/* The rectangle of a target of gsW x gsH GS pixels at its origin
 * (2048 - gsW / 2, 2048 - gsH / 2) whose record is tw x th GS pixels
 * (RdTargetRec.w, h): the larger of the two each way, and one more row
 * down for the frame head's half pixel (RD_TARGET_HALF_Y) */
RdVisRect rd__vis_rect(uint32_t gsW, uint32_t gsH, uint32_t tw, uint32_t th);

/* The bound of vertexCount vertices of qpv qwords each from stream
 * (qword 0 of each); false (out untouched past n = 0) when a position
 * lane is not finite. */
bool rd__vis_box(const float (*stream)[4], uint32_t vertexCount, uint32_t qpv, RdVisBox *out);

/* A grid's payload stream (rd_mesh.h RdVuGridDraw: per strip the VIF
 * qword, the GIF tag and the colour, stripLen vertices of qpv qwords,
 * MSCNT): the bound of every strip's vertices; false when the strips do
 * not fit streamQw, stripLen < 3, qpv < 2 or a position is not finite. */
bool rd__vis_grid_box(const float (*stream)[4], uint32_t streamQw, uint32_t strips,
                      uint32_t stripLen, uint32_t qpv, RdVisBox *out);

/* A skinned stream's bound (above); false when qpv < 3 (out->ok false). */
bool rd__vis_skin(const float (*stream)[4], uint32_t vertexCount, uint32_t qpv, RdVisSkin *out);

/* The verdict of a mesh or grid draw whose clip position is
 * h = m[0] x + m[1] y + m[2] z + m[3] w (VU memory 16..19, columns), w = 1,
 * or for the scissor mode (code 36) w over the box's w range and 1; the
 * scissor mode is skipped only beyond a side with every corner in front of
 * the eye (its triangles without a clip flag are drawn at their GS
 * positions whatever w is), never behind the eye alone. */
int rd__vis_test(const float (*m)[4], const RdVisBox *b, bool scissor, const RdVisRect *r);

/* The verdict of a skinned draw: VU memory mem (0..7: the region test's
 * lower bound mem[0], world to GS screen mem[4..7]), the bones (VuBoneCB
 * from qword 0, boneQw qwords), the stream's bound s. */
int rd__vis_test_skin(const float (*mem)[4], const float (*bones)[4], uint32_t boneQw,
                      const RdVisSkin *s, const RdVisRect *r);

/* The renderer's side (rd_vis.c): the mode at rd_init (the start line);
 * the mode and its name; a test's mode (rd__vis_set_mode, -1: the
 * environment's again) */
void rd__vis_init(void);
int rd__vis_mode(void);
const char *rd__vis_mode_name(int mode);
void rd__vis_set_mode(int mode);

/* The frame the replay walks (rd_replay.c replayFrame): f itself when the
 * mode is off, a pass window is set (the bisect and the tool's --passes
 * read the passes as recorded), f was never recorded (number 0: the
 * self-tests' frames) or nothing is skipped; else the shadow frame, f with
 * the skipped commands' type RDC_NOP and every other field kept, valid
 * until the next call.  Sets RdPerfRecord.cull*. */
const RdFrame *rd__vis_frame(const RdFrame *f, int keep);

/* The frame f was made from when f is the shadow frame, else f: what
 * decides across draws or replays reads the frame as recorded (rd_join.c's
 * strip joining, rd_fades_fold.c's plan) */
const RdFrame *rd__vis_source(const RdFrame *f);

/* The command the shadow frame f turned into an RDC_NOP at list l, index
 * i, or NULL (no shadow, or a command not skipped) */
const RdCmd *rd__vis_culled(const RdFrame *f, int l, uint32_t i);

/* The last rd__vis_frame's counts: the VU draw commands walked, those
 * skipped (by the frustum, by occlusion), those the test does not take
 * (exempt: particles, clip mode none, no target; unbounded: no bound or a
 * layout it does not read), and its time */
typedef struct RdVisCounts {
    uint32_t draws, skipped, frustum, occluded, exempt, unbounded;
    uint32_t byVerdict[RD_VIS_VERDICTS];
    double ms;
    /* the occlusion test (full): the occluders' triangles rasterised and
     * those left out past the budget, the cells they covered, the draws
     * tested against the record, its resets, and its share of ms */
    uint32_t hizTris, hizOverBudget, hizTested, hizResets;
    uint64_t hizCells;
    double hizMs;
} RdVisCounts;

void rd__vis_counts(RdVisCounts *out);

/* ------------------------------------------- the occlusion test (Hi-Z)
 * rd_vis_hiz.c (its header comment holds the argument).  All positions are
 * GS primitive coordinates (X / 16 of XYZ2), all depths GS Z (larger is
 * nearer). */

/* a cell's side in GS pixels; a tile is 8 x 8 cells */
#define RD_VIS_HIZ_CELL 2.0
/* the occluders' triangles a picture rasterises by default, the largest
 * screen boxes first (ICO_RD_CULL_HIZ_TRIS) */
#define RD_VIS_HIZ_TRIS 12000u

/* An occluder's corner as the GPU draws it: within r GS pixels of (x, y)
 * on each axis, its GS Z at least zlo. */
typedef struct RdVisHizVtx {
    double x, y, r, zlo;
} RdVisHizVtx;

/* An occluder's triangle: its corners' centres, their largest r, the lower
 * bound of its depth (floor of the least zlo, less 1), its screen box's
 * area, the list-0 index of its command and whether the budget took it. */
typedef struct RdVisHizTri {
    double x[3], y[3], r, area;
    uint32_t lb, cmd;
    uint8_t sel;
} RdVisHizTri;

typedef struct RdVisHizList {
    RdVisHizTri *t;
    uint32_t n, cap;
} RdVisHizList;

/* An occludee's samples' box (GS pixels) and the GS Z it can write at
 * most. */
typedef struct RdVisHizBox {
    double x0, y0, x1, y1, zmax;
} RdVisHizBox;

/* The record: cells of RD_VIS_HIZ_CELL GS pixels from (gx0, gy0), tile by
 * tile (64 cells a tile, row by row), each the least GS Z the depth buffer
 * holds at its samples (0: nothing known); a tile's least cell (tmin,
 * stale while HIZ_T_DIRTY), the tiles written since the last reset; the
 * box (x0..x1, y0..y1) a draw's samples can lie in. */
typedef struct RdVisHiz {
    double gx0, gy0, x0, y0, x1, y1;
    uint32_t ncx, ncy, ntx, nty;
    uint32_t *cell, *tmin, *written;
    uint8_t *tflag;
    uint32_t nWritten, cap;
} RdVisHiz;

/* Vertex pos (qword 0 of a prelit or lit vertex) through m (VU memory
 * 16..19), w = 1, or pos.w with code 36's clip matrix clip (VU memory
 * 20..23; NULL under code 32): true when the program draws it at its GS
 * position in every state (the region test or no clip flag, in front of the
 * eye, GS Z inside ftoi4's range, each by the float rounding's margin), out
 * then where the GPU puts it and its GS Z's lower bound. */
bool rd__vis_hiz_vertex(const float (*m)[4], const float (*clip)[4], const float *pos,
                        RdVisHizVtx *out);
/* The triangle a, b, c (t->sel 1, t->cmd 0); false when its depth bound is
 * below 1 (t->lb 0). */
bool rd__vis_hiz_tri(const RdVisHizVtx *a, const RdVisHizVtx *b, const RdVisHizVtx *c,
                     RdVisHizTri *t);
/* A mesh or grid draw's samples' box and Z bound from its stream's bound b
 * through m (code 36: w over b's pos.w range and 1, as rd__vis_test):
 * false unless every corner is in front of the eye, the box inside
 * (0, 4094) and the bound below gs_z_to_depth's top band. */
bool rd__vis_hiz_occludee(const float (*m)[4], const RdVisBox *b, bool scissor, RdVisHizBox *out);
/* The margin above an occludee's Z bound zmax its cells must clear (later:
 * a mesh with the later mark, whose triangles' depth rises by up to
 * vu_later_bias's cap) */
double rd__vis_hiz_margin(double zmax, bool later);
/* The record for the target rectangle r (rd__vis_rect), every cell 0;
 * false (an empty record: nothing hidden) without memory or past 8192 x
 * 4096 GS pixels. */
bool rd__vis_hiz_setup(RdVisHiz *z, const RdVisRect *r);
/* every cell to 0 (the tiles written only) */
void rd__vis_hiz_reset(RdVisHiz *z);
void rd__vis_hiz_free(RdVisHiz *z);
/* Triangle t into the record: every cell whose square, grown by half a GS
 * pixel up and down, lies inside clip (x0, y0, x1, y1: the samples the
 * draw's scissor lets through) and inside t for every placement of its
 * corners within t->r, by 0.25 GS pixel more, rises to t->lb; the cells
 * covered. */
uint32_t rd__vis_hiz_raster(RdVisHiz *z, const RdVisHizTri *t, const double clip[4]);
/* Whether every cell box b touches inside the record's box holds more
 * than thr (zmax + rd__vis_hiz_margin); false for a box outside it. */
bool rd__vis_hiz_hidden(RdVisHiz *z, const RdVisHizBox *b, double thr);
bool rd__vis_hiz_list_push(RdVisHizList *l, const RdVisHizTri *t);
/* The triangles of an RDC_MESH draw of m (payload header p, VU memory
 * mem) that count as occluders, in the index ranges rd_replay_vu.c
 * doVuDraw draws: those drawn exactly (rd__vis_hiz_vertex), not hidden,
 * not later-marked, not a sheet's tile, whose screen box covers minArea,
 * appended to out with cmd.  *lbAll: the least depth bound over every
 * triangle drawn (hidden ones aside).  1 when every triangle drawn is
 * exact, 0 when one is not (a draw that may lower the depth then marks
 * nothing), -1 without memory. */
int rd__vis_hiz_mesh(const RdMeshRec *m, const RdVuPayload *p, const float (*mem)[4], uint32_t cmd,
                     double minArea, RdVisHizList *out, uint32_t *lbAll);

#ifdef __cplusplus
}
#endif

#endif
