/* rd_lamps.c: the lamps' glow frames for less (rd_lamps.h;
 * docs/port/RENDER_POST.md, "The lamps' glow for less"; docs/port/WORK_BUFFERS.md,
 * "WORK2 under the lamps' area").
 *
 * The frames.  At the title (stage 1's castle) and in issue 163's rooms the
 * glow's light plan (rd_glow_light.c) is LAMPS: WORK2's fill is drawn as
 * (0, 0, 0, 0), the mask is not drawn, and the lamps' meshes (list 7,
 * RDC_MESH into WORK2 under a GEQUAL test of SCENE's depth, writing it) are
 * the only light the chain carries.  On the PowerVR phone (its log's first
 * three 10 s blocks of the opening) the pass that draws them is WORK2 at
 * 2366 x 1080: its colour cleared (the fill, a pending clear taken as the
 * pass's load op: 9.7 MB cleared) and stored (9.7 MB), SCENE's depth loaded
 * (9.6 MB) for the lamps' Z test, for a dozen small draws.
 *
 * RD_LAMPS_AREA.  That pass gets a render area (RhiRenderPassDesc.area): the
 * union of the lamps' screen boxes and of the region of WORK2 the last
 * lamps frame left lit, aligned out to 32 x 32 tiles.  The Vulkan rule
 * (VkRenderingInfo / VkRenderPassBeginInfo renderArea: "the effects of
 * attachment load, store, and multisample resolve operations are
 * restricted to the pixels whose x and y coordinates fall within the render
 * area"; rhi.h: "a CLEAR load clears the area alone") leaves every texel
 * outside it as memory holds it, and the RHI cuts every scissor to it
 * (vk_cmd.c rhi_cmd_set_scissor).  The bytes are the whole pass's when:
 *   - every fragment of the pass lies inside the area: a lamp's box is
 *     rd_vis_hiz.c rd__vis_hiz_occludee's samples' box of its stream's
 *     bound through its matrix (VU memory 16..19), the bound the occlusion
 *     test skips draws by (every corner in front of the eye, the box inside
 *     the GS region: else no area), padded by two GS pixels (the frame
 *     head's half line, the snap, a texel's reach at Full pixel) through
 *     the target's texel scale and crop (rd__gs_rect_texels); the sun's fans
 *     a LAMPS frame draws (off the target: the plan's rule) by their
 *     vertices' box (rd__screen_area); anything else drawn into WORK2 in the
 *     frame and the pass is whole;
 *   - outside the area WORK2 already holds what the whole pass would leave
 *     there: (0, 0, 0, 0), the fill's colour, the lamps having drawn nothing
 *     there.  The tracker (s_w2) holds the region a confirmed lamps pass may
 *     have lit (the union of its lamps' boxes): every texel of WORK2 outside
 *     it holds (0, 0, 0, 0), stored by that pass (its colour STORE, the
 *     whole target cleared or the rest kept from the frame before under the
 *     same rule), and the area holds that region, so the texels the area
 *     leaves were zero before and are zero after;
 *   - SCENE's depth outside the area: loaded and stored unchanged by the
 *     whole pass (the lamps draw nothing there), left as memory holds it by
 *     the area pass; where the store plan drops the depth's store
 *     (DONT_CARE: nothing reads it) the whole pass leaves it undefined, the
 *     area pass as it was: nothing reads it either way.
 * The tracker forgets the region (the next lamps pass whole) at rd_init, at
 * WORK2's new texture (its id, size, or the new target's pending clear),
 * after a replay that writes WORK2 any other way (the scan: a command into
 * WORK2 that is not the LAMPS plan's fill, mask or lamps or the DARK plan's
 * undrawn chain; a WHOLE frame's mask), at any render pass on WORK2's
 * colour that is not the planned one, when the planned pass does not begin
 * with the fill's CLEAR to (0, 0, 0, 0) and a colour STORE, and under the
 * replay tool's pass window.  The DARK plan's frames keep it: their chain
 * draws nothing into WORK2 (a lazy clear of (0, 0, 0, 0), the mask and the
 * fans skipped).  So the first lamps frame after anything else is drawn
 * whole, and the area from the second on.
 *
 * Where.  A CLEAR over a render area smaller than the attachment is a form
 * no other pass takes (rd_pass_area.c gives no area to a pass that clears),
 * so it is on only where the start-up self-test passed (rd__lamps_selftest:
 * a 64 x 64 RGBA8 target and a D32F_S8 depth cleared whole and stored, then
 * a pass over a 32 x 32 area with the colour cleared to 0 and the depth
 * loaded: read back, the area 0 and the rest the first clear's, the depth
 * the first clear's everywhere), on the PowerVR and desktop classes under
 * Vulkan where the profile's pass area is on (rd_lamps.h); never on the
 * Arm, Qualcomm and Apple classes (issues 123, 179, 211: only less of a
 * kind of work already drawn there).
 *
 * RD_LAMPS_PAD.  WORK2_PAD's band (fillWork2's 256 x 64 sprite, rd.h "never
 * read") is left a lazy clear in a LAMPS frame as the DARK plan leaves it
 * (rd_glow_light.c planActs: drawn when something reads WORK2_PAD, at a
 * later replay's begin or a read outside the replay): one small pass fewer
 * a picture.  The DARK plan's lazy clears run on every class and backend,
 * so this does too.
 *
 * The lines: the state when it changes ("lamps: area on|off (...); band
 * lazy on|off"), the self-test's verdict, the first area pass and each
 * reason a pass was whole once a run; the 10 s "window: lamps: ..."
 * (rd_lamps_line) and rd_replay_tool --stats "stats: lamps ..."
 * (RdPerfRecord.lamps*). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_jitter.h" /* rd__frame_jitter_get: depth_down_ps's sample point */
#include "rd_lamps.h"
#include "rd_mesh.h"
#include "rd_replay.h"
#include "rd_shadow_tile.h"
#include "rd_vis.h"
#include "rhi_traffic.h"

/* the WORK2 writes a frame's scan keeps (past it: the pass whole) */
#define LAMPS_MAX 128u
/* GS pixels round a lamp's samples' box (rd_lamps.h rd__lamps_box_texels) */
#define LAMPS_PAD 2
/* the self-test's target and its area */
#define TEST_W 64u
#define TEST_H 64u
#define TEST_AX 32
#define TEST_AW 32u
#define TEST_AH 32u

static struct {
    int force; /* rd__lamps_force: -1 none */
    int env;   /* -1 unread, 0 unset, 1 set (envBits), 2 not understood */
    uint32_t envBits;
    uint32_t def; /* the class's (rd__lamps_init) */
    char said[200];
} s_sw = {.force = -1, .env = -1};

static struct {
    int state; /* 0 not run, 1 passed, 2 failed or not run */
    char words[200];
} s_test;

/* WORK2's lit region: every texel outside lit holds (0, 0, 0, 0) */
static struct {
    bool valid;
    uint32_t tex, tw, th;
    RhiRect lit;
} s_w2;

typedef struct W2Write {
    uint32_t ord;
    uint8_t type;
    bool ok;     /* box known */
    uint8_t why; /* not known: RD_LAMPS_BOX_* */
    uint8_t prog, code, clip;
    uint32_t verts, behind;
    char name[24]; /* the mesh's (RdMeshRec.name) */
    RhiRect box;   /* the texels it can touch (w 0: none) */
} W2Write;

static struct {
    bool on;
    uint32_t replay, w2, frame;
    bool overflow;
    uint32_t n;
    W2Write w[LAMPS_MAX];
    /* scan end: the planned pass */
    bool want, confirmed, area;
    RhiRect areaRect, box;
    uint32_t fillOrd, redOrd;
    int why;
} s_scan;

/* the 10 s sums (the window thread reads them) */
static struct {
    uint64_t areas, whole, pads, areaTexels, texels, clear, store, dload, dstore, owed, fold4;
    uint64_t f4why[RD_LAMPS_F4_COUNT];
    uint64_t why[RD_LAMPS_WHY_COUNT];
} s_sum;

static uint64_t s_totAreas, s_totWhole, s_totPads;
static bool s_saidWhy[RD_LAMPS_WHY_COUNT], s_saidArea;

/* why a lamp's box is not known (the BOX reason's parts: the 10 s line's
 * "boxes not known" clause and a line once each, naming the draw) */
static const char *const kBoxWhy[RD_LAMPS_BOX_COUNT] = {
    [RD_LAMPS_BOX_NONE] = "-",
    [RD_LAMPS_BOX_KIND] = "not a mesh or a screen prim (skinned, grid, particles, a sprite)",
    [RD_LAMPS_BOX_PAYLOAD] = "its payload short or outside the frame",
    [RD_LAMPS_BOX_CLIP] = "clip mode none (code 34) with a corner where X or Y may wrap",
    [RD_LAMPS_BOX_PROG] = "not a mesh program",
    [RD_LAMPS_BOX_MESH] = "no mesh record or stream",
    [RD_LAMPS_BOX_FINITE] = "a position or matrix not finite",
    [RD_LAMPS_BOX_BEHIND] = "a vertex at or behind the eye",
    [RD_LAMPS_BOX_BUDGET] = "past the frame's vertex budget, its corners' box refused",
    [RD_LAMPS_BOX_SCREEN] = "a screen prim under DATE, the wrap or a full-frame sprite",
};
static uint64_t s_boxSum[RD_LAMPS_BOX_COUNT];
static bool s_saidBox[RD_LAMPS_BOX_COUNT];
static uint32_t s_budgetUsed;

static void sumAdd(uint64_t *v, uint64_t n)
{
    if (n) {
        __atomic_add_fetch(v, n, __ATOMIC_RELAXED);
    }
}

static const char *const kWhy[RD_LAMPS_WHY_COUNT] = {
    [RD_LAMPS_WHY_NONE] = "-",
    [RD_LAMPS_WHY_OFF] = "switched off by ICO_RD_LAMPS",
    [RD_LAMPS_WHY_CLASS] = "not the PowerVR or a desktop class under Vulkan with the pass area",
    [RD_LAMPS_WHY_TEST] = "its self-test not passed",
    [RD_LAMPS_WHY_FIRST] = "the WORK2 an earlier frame left not known",
    [RD_LAMPS_WHY_BOX] = "a lamp whose screen box is not known",
    [RD_LAMPS_WHY_OTHER] = "another command writes WORK2 in the frame",
    [RD_LAMPS_WHY_LARGE] = "the lamps and the lit region cover WORK2",
    [RD_LAMPS_WHY_EMPTY] = "no lamp on WORK2 and nothing lit there",
    [RD_LAMPS_WHY_FORM] = "the pass not begun as planned",
    [RD_LAMPS_WHY_WINDOW] = "the replay tool's pass window",
};

const char *rd__lamps_why_text(int why)
{
    return why >= 0 && why < RD_LAMPS_WHY_COUNT && kWhy[why] ? kWhy[why] : "unknown";
}

const char *rd__lamps_box_why_text(int why)
{
    return why >= 0 && why < RD_LAMPS_BOX_COUNT && kBoxWhy[why] ? kBoxWhy[why] : "unknown";
}

const char *rd__lamps_bits_text(uint32_t bits)
{
    static const char *const kWord[12] = {"area",       "pad",     "owed",     "wholepad",
                                          "fold4",      "lowres",  "fillhold", "lowresbox",
                                          "lowresbias", "lowres4", "lowresp",  "lowreshy"};
    static char s_text[8][112];
    static unsigned s_next;
    bits &= RD_LAMPS_ALL | RD_LAMPS_FILLHOLD | RD_LAMPS_LOWRES_ANY;
    if (!bits) {
        return "off";
    }
    char *t = s_text[s_next++ & 7u]; /* a ring: a few calls in one expression */
    t[0] = '\0';
    size_t at = 0;
    for (int i = 0; i < 12; i++) {
        if (bits & (1u << i)) {
            const int m = snprintf(t + at, sizeof(s_text[0]) - at, "%s%s", at ? "+" : "", kWord[i]);
            at += m > 0 && (size_t)m < sizeof(s_text[0]) - at ? (size_t)m : 0;
        }
    }
    return t;
}

/* ------------------------------------------------------- pure helpers */

int rd__lamps_parse(const char *e, uint32_t *bits)
{
    if (!e || !e[0]) {
        return 0;
    }
    if (strcmp(e, "0") == 0 || strcmp(e, "off") == 0) {
        *bits = 0;
        return 1;
    }
    if (strcmp(e, "1") == 0 || strcmp(e, "all") == 0 || strcmp(e, "on") == 0) {
        *bits = RD_LAMPS_ALL;
        return 1;
    }
    uint32_t b = 0;
    const char *s = e;
    while (*s) {
        const char *t = s;
        while (*t && *t != ',' && *t != '+') {
            t++;
        }
        const size_t n = (size_t)(t - s);
        if (n == 4 && strncmp(s, "area", 4) == 0) {
            b |= RD_LAMPS_AREA;
        } else if (n == 3 && strncmp(s, "pad", 3) == 0) {
            b |= RD_LAMPS_PAD;
        } else if (n == 4 && strncmp(s, "owed", 4) == 0) {
            b |= RD_LAMPS_OWED;
        } else if (n == 8 && strncmp(s, "wholepad", 8) == 0) {
            b |= RD_LAMPS_WHOLEPAD;
        } else if (n == 5 && strncmp(s, "fold4", 5) == 0) {
            b |= RD_LAMPS_FOLD4;
        } else if (n == 6 && strncmp(s, "lowres", 6) == 0) {
            b |= RD_LAMPS_LOWRES;
        } else if (n == 8 && strncmp(s, "fillhold", 8) == 0) {
            b |= RD_LAMPS_FILLHOLD;
        } else if (n == 9 && strncmp(s, "lowresbox", 9) == 0) {
            b |= RD_LAMPS_LOWRES | RD_LAMPS_LOWRESBOX;
        } else if (n == 10 && strncmp(s, "lowresbias", 10) == 0) {
            b |= RD_LAMPS_LOWRES | RD_LAMPS_LOWRESBIAS;
        } else if (n == 8 && strncmp(s, "lowreshy", 8) == 0) {
            b |= RD_LAMPS_LOWRES | RD_LAMPS_LOWRESHY;
        } else if (n == 7 && strncmp(s, "lowres4", 7) == 0) {
            b |= RD_LAMPS_LOWRES | RD_LAMPS_LOWRES4;
        } else if (n == 7 && strncmp(s, "lowresp", 7) == 0) {
            b |= RD_LAMPS_LOWRES | RD_LAMPS_LOWRESP;
        } else {
            return -1;
        }
        s = *t ? t + 1 : t;
    }
    *bits = b;
    return 1;
}

uint32_t rd__lamps_default_bits(int tileClass, bool tilePhone, bool vulkan, bool passArea,
                                bool blurFold)
{
    /* FILLHOLD by its word alone: on the PowerVR phone (rc24) every held
     * fill was flushed by a later draw on SCENE before the paste's pass (the
     * glow's paste deferred to list 11), drawn early in a pass of its own
     * with the snapshot copy back: a loss */
    uint32_t b = RD_LAMPS_PAD | RD_LAMPS_OWED | RD_LAMPS_WHOLEPAD |
                 (RD_LAMPS_LOWRES_DEFAULT ? RD_LAMPS_LOWRES : 0u);
    const bool cls = tileClass == 1 || (tileClass == 2 && !tilePhone);
    if (cls && vulkan && passArea) {
        b |= RD_LAMPS_AREA;
    }
    /* the fold of two blur pairs is off by default on every class in this
     * release: on the owner's PowerVR phone (rc20) the lamps pictures took
     * 28.3 to 30.9 ms of GPU work with it against 25.7 to 26.0 without
     * (glow WORK0: 9 passes in 8.8 ms against 12 in 3.2 ms): the folded
     * shader costs more than the passes it removes. ICO_RD_LAMPS=...,fold4
     * turns it on (tests) */
    (void)blurFold;
    return b;
}

RhiRect rd__lamps_union(RhiRect a, RhiRect b)
{
    if (!b.w || !b.h) {
        return a;
    }
    if (!a.w || !a.h) {
        return b;
    }
    const int64_t x0 = a.x < b.x ? a.x : b.x, y0 = a.y < b.y ? a.y : b.y;
    const int64_t ax1 = (int64_t)a.x + a.w, bx1 = (int64_t)b.x + b.w;
    const int64_t ay1 = (int64_t)a.y + a.h, by1 = (int64_t)b.y + b.h;
    const int64_t x1 = ax1 > bx1 ? ax1 : bx1, y1 = ay1 > by1 ? ay1 : by1;
    return (RhiRect){(int32_t)x0, (int32_t)y0, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0)};
}

RhiRect rd__lamps_tile_align(RhiRect r, uint32_t tile, uint32_t w, uint32_t h)
{
    const int64_t t = tile ? (int64_t)tile : 1;
    int64_t x0 = r.x > 0 ? r.x : 0, y0 = r.y > 0 ? r.y : 0;
    int64_t x1 = (int64_t)r.x + r.w, y1 = (int64_t)r.y + r.h;
    x0 = x0 / t * t;
    y0 = y0 / t * t;
    x1 = (x1 + t - 1) / t * t;
    y1 = (y1 + t - 1) / t * t;
    x1 = x1 < (int64_t)w ? x1 : (int64_t)w;
    y1 = y1 < (int64_t)h ? y1 : (int64_t)h;
    if (x1 <= x0 || y1 <= y0) {
        return (RhiRect){0, 0, 0, 0};
    }
    return (RhiRect){(int32_t)x0, (int32_t)y0, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0)};
}

bool rd__lamps_box_texels(const RdTargetRec *tc, uint32_t gsW, uint32_t gsH, double x0, double y0,
                          double x1, double y1, int32_t pad, RhiRect *out)
{
    if (!tc || !out || !(x0 <= x1) || !(y0 <= y1) || !isfinite(x0) || !isfinite(x1) ||
        !isfinite(y0) || !isfinite(y1)) {
        return false;
    }
    /* the target's GS pixels from its origin: pixel p holds the samples of
     * [p, p + 1); the box's first and last pixels padded */
    const double ox = 2048.0 - (double)(gsW >> 1), oy = 2048.0 - (double)(gsH >> 1);
    const double a = floor(x0 - ox) - pad, b = floor(y0 - oy) - pad;
    const double c = ceil(x1 - ox) + pad, d = ceil(y1 - oy) + pad;
    if (a > 1e6 || b > 1e6 || c < -1e6 || d < -1e6) {
        return false; /* nowhere near the target */
    }
    const double lo = -1e6, hi = 1e6;
    return rd__gs_rect_texels(tc, (int32_t)(a < lo ? lo : a), (int32_t)(b < lo ? lo : b),
                              (int32_t)(c > hi ? hi : c), (int32_t)(d > hi ? hi : d), 0, out);
}

int rd__lamps_area_pick(bool litKnown, RhiRect lit, RhiRect box, uint32_t tw, uint32_t th,
                        RhiRect *area)
{
    if (!litKnown) {
        return RD_LAMPS_WHY_FIRST;
    }
    const RhiRect u = rd__lamps_union(lit, box);
    if (!u.w || !u.h) {
        return RD_LAMPS_WHY_EMPTY;
    }
    const RhiRect a = rd__lamps_tile_align(u, RD_PASS_AREA_TILE, tw, th);
    if (!a.w || !a.h || (a.w + RD_PASS_AREA_TILE > tw && a.h + RD_PASS_AREA_TILE > th)) {
        return RD_LAMPS_WHY_LARGE; /* not a tile column or row narrower */
    }
    *area = a;
    return RD_LAMPS_WHY_NONE;
}

int rd__lamps_vertex_box(const float (*m)[4], const float (*stream)[4], uint32_t n, uint32_t qpv,
                         bool scissor, double box[4], uint32_t *behind)
{
    *behind = 0;
    if (!m || !stream || n == 0 || qpv == 0) {
        return RD_LAMPS_BOX_MESH;
    }
    for (int i = 0; i < 4; i++) {
        for (int c = 0; c < 4; c++) {
            if (!isfinite(m[i][c])) {
                return RD_LAMPS_BOX_FINITE;
            }
        }
    }
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
    bool any = false;
    const int nw = scissor ? 2 : 1;
    for (uint32_t i = 0; i < n; i++) {
        const float *p = stream[(size_t)i * qpv];
        if (!isfinite(p[0]) || !isfinite(p[1]) || !isfinite(p[2]) || !isfinite(p[3])) {
            return RD_LAMPS_BOX_FINITE;
        }
        for (int j = 0; j < nw; j++) {
            /* w = 1 (code 32), or under code 36 the vertex's own pos.w and 1
             * (rd_vis.c: the scissor programs read pos.w) */
            const double v[4] = {p[0], p[1], p[2], j == 0 && scissor ? (double)p[3] : 1.0};
            double h[4], tol[4];
            for (int c = 0; c < 4; c++) {
                double sum = 0.0, mag = 0.0;
                for (int k = 0; k < 4; k++) {
                    sum += (double)m[k][c] * v[k];
                    mag += fabs((double)m[k][c] * v[k]);
                }
                h[c] = sum;
                tol[c] = RD_VIS_TOL * mag; /* the shader's float h lies within this */
            }
            const double wl = h[3] - tol[3], wh = h[3] + tol[3];
            if (!(wl > 0.0)) {
                (*behind)++;
                continue;
            }
            /* x / w over x in [h - tol, h + tol], w in [wl, wh], w > 0 */
            const double xl = h[0] - tol[0], xh = h[0] + tol[0];
            const double yl = h[1] - tol[1], yh = h[1] + tol[1];
            const double ax = fmin(xl / wl, xl / wh), bx = fmax(xh / wl, xh / wh);
            const double ay = fmin(yl / wl, yl / wh), by = fmax(yh / wl, yh / wh);
            x0 = any ? fmin(x0, ax) : ax;
            x1 = any ? fmax(x1, bx) : bx;
            y0 = any ? fmin(y0, ay) : ay;
            y1 = any ? fmax(y1, by) : by;
            any = true;
        }
    }
    if (*behind) {
        return RD_LAMPS_BOX_BEHIND;
    }
    if (!any || !isfinite(x0) || !isfinite(x1) || !isfinite(y0) || !isfinite(y1)) {
        return RD_LAMPS_BOX_FINITE;
    }
    /* the divide's rounding, a GS position up to 1/16 left of (above) its
     * point (ftoi4), the GPU's snap and the GS-to-clip mapping's rounding:
     * rd_vis_hiz.c rd__vis_hiz_occludee's margins */
    const double ulp = 1.0 / 1048576.0, snap = 1.0 / 8.0, ndc = 1.0 / 1024.0;
    box[0] = x0 - fabs(x0) * ulp - 1.0 / 16.0 - snap - ndc;
    box[1] = y0 - fabs(y0) * ulp - 1.0 / 16.0 - snap - ndc;
    box[2] = x1 + fabs(x1) * ulp + snap + ndc;
    box[3] = y1 + fabs(y1) * ulp + snap + ndc;
    return RD_LAMPS_BOX_NONE;
}

/* ------------------------------------------------------------- the switch */

static void envRead(void)
{
    if (s_sw.env >= 0) {
        return;
    }
    const char *e = getenv("ICO_RD_LAMPS"); /* the tests only: no config key */
    uint32_t b = 0;
    const int k = rd__lamps_parse(e, &b);
    s_sw.env = k == 1 ? 1 : k == 0 ? 0 : 2;
    s_sw.envBits = b;
    if (k < 0) {
        rd__log("lamps: ICO_RD_LAMPS=%.24s is not 0, 1 or words of area, pad, owed, wholepad, "
                "fold4, lowres, lowresbox and fillhold: the class's default",
                e);
    }
}

/* the bits asked for, before the self-test and the backend */
static uint32_t wanted(void);

uint32_t rd__lamps_wanted(void)
{
    return wanted();
}

static uint32_t wanted(void)
{
    if (s_sw.force >= 0) {
        return (uint32_t)s_sw.force & RD_LAMPS_ALL;
    }
    envRead();
    return s_sw.env == 1 ? s_sw.envBits : s_sw.def;
}

uint32_t rd__lamps_bits(void)
{
    uint32_t b = wanted();
    if ((b & RD_LAMPS_AREA) &&
        (s_test.state != 1 || !g_rd.hasDevice || rhi_backend() != RHI_BACKEND_VULKAN)) {
        b &= ~RD_LAMPS_AREA;
    }
    if ((b & RD_LAMPS_FOLD4) && (rd__blur_fold4_test_state(NULL) != 1 || !g_rd.hasDevice ||
                                 rhi_backend() != RHI_BACKEND_VULKAN)) {
        b &= ~RD_LAMPS_FOLD4;
    }
    return b;
}

/* why AREA is not in force (NONE: it is) */
static int areaWhy(void)
{
    const uint32_t w = wanted();
    if (!(w & RD_LAMPS_AREA)) {
        const bool asked = s_sw.force >= 0 || s_sw.env == 1;
        return asked ? RD_LAMPS_WHY_OFF : RD_LAMPS_WHY_CLASS;
    }
    if (!g_rd.hasDevice || rhi_backend() != RHI_BACKEND_VULKAN) {
        return RD_LAMPS_WHY_CLASS;
    }
    return s_test.state == 1 ? RD_LAMPS_WHY_NONE : RD_LAMPS_WHY_TEST;
}

static void note(void)
{
    if (!g_rd.hasDevice) {
        return;
    }
    char line[sizeof(s_sw.said)];
    const uint32_t b = rd__lamps_bits();
    const int why = areaWhy();
    const char *src = s_sw.force >= 0 ? "the tests' switch" : s_sw.env == 1 ? "ICO_RD_LAMPS" : "";
    snprintf(line, sizeof(line), "lamps: area %s (%s%s%s); band lazy %s%s%s%s",
             (b & RD_LAMPS_AREA) ? "on" : "off",
             why == RD_LAMPS_WHY_NONE                        ? "self-test passed"
             : why == RD_LAMPS_WHY_TEST && s_test.state == 0 ? "its self-test not run yet"
                                                             : rd__lamps_why_text(why),
             src[0] ? ", " : "", src, (b & RD_LAMPS_PAD) ? "on" : "off", src[0] ? " (" : "", src,
             src[0] ? ")" : "");
    if (strcmp(line, s_sw.said) != 0) {
        snprintf(s_sw.said, sizeof(s_sw.said), "%s", line);
        rd__log("%s: the lamps' pass on WORK2 loads, clears and stores only their tiles and "
                "those an earlier lamps frame lit; WORK2_PAD's band a lazy clear (the same "
                "bytes)",
                line);
    }
}

static void lowresNote(void);

void rd__lamps_init(void)
{
    const RhiLimits *lim = g_rd.hasDevice ? rhi_limits() : NULL;
    s_sw.def = rd__lamps_default_bits(rd__profile_tile_class(), lim && lim->tilePhone,
                                      g_rd.hasDevice && rhi_backend() == RHI_BACKEND_VULKAN,
                                      rd__pass_area_on(), g_rd.profile.blurFold);
    memset(&s_w2, 0, sizeof(s_w2));
    memset(&s_scan, 0, sizeof(s_scan));
    /* the self-test again for this renderer (rd__lamps_selftest: its first
     * rd_fog_selftest, or rd_replay_tool after rd_init); off until then */
    memset(&s_test, 0, sizeof(s_test));
    note();
    lowresNote();
}

void rd__lamps_force(int bits)
{
    s_sw.force = bits < 0 ? -1 : (int)((uint32_t)bits & RD_LAMPS_ALL);
    memset(&s_w2, 0, sizeof(s_w2));
    note();
}

int rd__lamps_test_state(const char **words)
{
    if (words) {
        *words = s_test.words[0] ? s_test.words : "not run";
    }
    return s_test.state;
}

bool rd__lamps_poison(void)
{
    static int s_env = -1;
    if (s_env < 0) {
        const char *e = getenv("ICO_RD_LAMPS_POISON"); /* the tests only: no config key */
        s_env = e && strcmp(e, "1") == 0;
    }
    return s_env == 1;
}

bool rd__lamps_pad_on(void)
{
    return (rd__lamps_bits() & RD_LAMPS_PAD) != 0;
}

void rd__lamps_pad_noted(void)
{
    g_rdPerf.lampsPads++;
    s_totPads++;
    sumAdd(&s_sum.pads, 1);
}

bool rd__lamps_owed_on(void)
{
    return (rd__lamps_bits() & RD_LAMPS_OWED) != 0;
}

void rd__lamps_owed_noted(void)
{
    g_rdPerf.lampsOwed++;
    sumAdd(&s_sum.owed, 1);
}

static const char *const kF4Why[RD_LAMPS_F4_COUNT] = {
    [RD_LAMPS_F4_NONE] = "-",
    [RD_LAMPS_F4_PAIR] = "the next pair does not fold",
    [RD_LAMPS_F4_STRIP] = "the glow's edge strip follows the next pair",
    [RD_LAMPS_F4_LAST] = "the chain's last pair next",
    [RD_LAMPS_F4_FORM] = "the pairs differ past the shrink",
    [RD_LAMPS_F4_WINDOW] = "a window wider than the shader holds",
};

const char *rd__lamps_f4_why_text(int why)
{
    return why >= 0 && why < RD_LAMPS_F4_COUNT && kF4Why[why] ? kF4Why[why] : "unknown";
}

void rd__lamps_fold4_noted(void)
{
    g_rdPerf.lampsFold4++;
    sumAdd(&s_sum.fold4, 1);
}

void rd__lamps_fold4_refused(int why)
{
    if (why <= RD_LAMPS_F4_NONE || why >= RD_LAMPS_F4_COUNT) {
        return;
    }
    g_rdPerf.lampsFold4Why = (uint32_t)why;
    sumAdd(&s_sum.f4why[why], 1);
    static bool s_said[RD_LAMPS_F4_COUNT];
    if (!s_said[why]) {
        s_said[why] = true;
        rd__log("blur fold: a pair drawn alone, not with the next one: %s (reported once for this "
                "reason)",
                kF4Why[why]);
    }
}

static int32_t f4Floor16(int32_t v)
{
    return v >= 0 ? v / 16 : -((-v + 15) / 16);
}

static int32_t f4Clamp(int32_t c, int32_t lsz, int32_t asz)
{
    c = c < 0 ? 0 : (c > lsz - 1 ? lsz - 1 : c);
    return c < 0 ? 0 : (c > asz - 1 ? asz - 1 : c);
}

/* t0 + (X - a0) (t1 - t0) / (a1 - a0), truncated toward zero */
static int32_t f4U(int32_t X, int32_t a0, int32_t a1, int32_t t0, int32_t t1)
{
    return t0 + (int32_t)(((int64_t)(X - a0) * (t1 - t0)) / (a1 - a0));
}

bool rd__lamps_fold4_axis(const RdFold4Axis *a, int32_t *w2, int32_t *w1)
{
    if (a->b2x1 == a->b2x0 || a->b1x1 == a->b1x0 || a->a1x1 == a->a1x0 || a->lsz < 1 || a->w0 < 1 ||
        a->w1 < 1 || a->c1 < a->c0) {
        return false;
    }
    for (int32_t p = a->c0; p <= a->c1; p++) {
        const int32_t i = f4Floor16(f4U(a->o + 16 * p, a->b2x0, a->b2x1, a->b2u0, a->b2u1) - 8);
        int32_t jA = f4Clamp(i, a->lsz, a->w1), jB = f4Clamp(i + 1, a->lsz, a->w1);
        jA = jA > a->loB ? jA : a->loB;
        jB = jB > a->loB ? jB : a->loB;
        const int32_t k0 = f4Floor16(f4U(a->o + 16 * jA, a->b1x0, a->b1x1, a->b1u0, a->b1u1) - 8);
        const int32_t k1 = f4Floor16(f4U(a->o + 16 * jB, a->b1x0, a->b1x1, a->b1u0, a->b1u1) - 8);
        const int32_t lo2 = f4Clamp(k0 < k1 ? k0 : k1, a->lsz, a->w0);
        const int32_t hi2 = f4Clamp((k0 > k1 ? k0 : k1) + 1, a->lsz, a->w0);
        const int32_t g2 = hi2 - lo2 + 1;
        *w2 = g2 > *w2 ? g2 : *w2;
        if (g2 > 4) {
            return false;
        }
        int32_t lo1 = INT32_MAX, hi1 = INT32_MIN;
        for (int32_t t = lo2; t <= hi2; t++) {
            /* pass 2A: pass 2B's rect, its own UVs */
            const int32_t m = f4Floor16(f4U(a->o + 16 * t, a->b2x0, a->b2x1, a->a2u0, a->a2u1) - 8);
            int32_t m0 = f4Clamp(m, a->lsz, a->w1), m1 = f4Clamp(m + 1, a->lsz, a->w1);
            m0 = m0 > a->loA ? m0 : a->loA;
            m1 = m1 > a->loA ? m1 : a->loA;
            lo1 = m0 < lo1 ? m0 : lo1;
            hi1 = m1 > hi1 ? m1 : hi1;
        }
        const int32_t g1 = hi1 - lo1 + 1;
        *w1 = g1 > *w1 ? g1 : *w1;
        if (g1 > 6) {
            return false;
        }
    }
    return true;
}

bool rd__lamps_wholepad_on(void)
{
    return (rd__lamps_bits() & RD_LAMPS_WHOLEPAD) != 0;
}

void rd__lamps_counts(uint64_t *areas, uint64_t *whole, uint64_t *pads)
{
    if (areas) {
        *areas = s_totAreas;
    }
    if (whole) {
        *whole = s_totWhole;
    }
    if (pads) {
        *pads = s_totPads;
    }
}

/* ---------------------------------------------------------- the self-test */

static const uint8_t kTestColour[4] = {51, 102, 153, 204}; /* 0.2, 0.4, 0.6, 0.8 */

static bool testPass(RhiCommandList cl, RhiTexture col, RhiTexture dep, bool readable, bool second)
{
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = col;
    p.color[0].load = RHI_LOAD_CLEAR;
    p.color[0].readable = readable;
    if (!second) {
        for (int i = 0; i < 4; i++) {
            p.color[0].clear[i] = (float)kTestColour[i] / 255.0f;
        }
    }
    p.colorCount = 1;
    p.depth.texture = dep;
    p.depth.depthLoad = second ? RHI_LOAD_LOAD : RHI_LOAD_CLEAR;
    p.depth.stencilLoad = second ? RHI_LOAD_LOAD : RHI_LOAD_CLEAR;
    p.depth.clearDepth = 0.25f;
    p.depth.clearStencil = 0x33;
    p.width = TEST_W;
    p.height = TEST_H;
    if (second) {
        p.area = (RhiRect){TEST_AX, 0, TEST_AW, TEST_AH};
    }
    rhi_cmd_begin_render_pass(cl, &p);
    rhi_cmd_end_render_pass(cl);
    return true;
}

static bool areaSelftest(void);

bool rd__lamps_selftest(void)
{
    const bool area = areaSelftest();
    (void)rd__blur_fold4_selftest(); /* the fold of two pairs (rd_replay_tiler.c), logged there */
    note();
    return area;
}

static bool areaSelftest(void)
{
    if (!g_rd.inited || !g_rd.hasDevice || !(wanted() & RD_LAMPS_AREA) ||
        rhi_backend() != RHI_BACKEND_VULKAN) {
        return true; /* not asked here: nothing to check */
    }
    s_test.state = 2;
    const RhiFormat fmt = RHI_FMT_RGBA8_UNORM;
    uint8_t dstUsage = 0;
    const uint32_t usage = RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC |
                           rd__dst_target_usage(fmt, &dstUsage);
    /* readable as WORK2's passes are (rd_uber.c rd__pass_wants_readable:
     * DATE's local read on an RGBA8 target made for it) */
    const bool readable = g_rd.profile.dateStencil && (dstUsage & RD_DST_USAGE_LOCAL_DATE) != 0;
    RhiTexture col = rhi_create_texture(
        &(RhiTextureDesc){TEST_W, TEST_H, 1, fmt, usage, "rd lamps area test", 0});
    RhiTexture dep = rhi_create_texture(&(RhiTextureDesc){TEST_W, TEST_H, 1, RHI_FMT_D32F_S8,
                                                          RHI_TEX_DEPTH_STENCIL | RHI_TEX_COPY_SRC,
                                                          "rd lamps area test depth", 0});
    const char *why = NULL;
    int bad = -1;
    uint8_t got[4] = {0}, want[4] = {0};
    float gotD = 0.0f;
    if (!col.id || !dep.id) {
        why = "no memory for its textures";
    } else {
        RhiCommandList cl =
            rd__begin_own_frame(64u * 1024u) ? rhi_begin_commands() : (RhiCommandList){0};
        if (!cl.id) {
            why = "no command list";
        } else {
            RhiState cs = RHI_STATE_UNDEFINED, ds = RHI_STATE_UNDEFINED;
            rd__transition(cl, col, &cs,
                           readable ? RHI_STATE_RENDER_TARGET_READ : RHI_STATE_RENDER_TARGET);
            rd__transition(cl, dep, &ds, RHI_STATE_DEPTH_WRITE);
            (void)testPass(cl, col, dep, readable, false);
            (void)testPass(cl, col, dep, readable, true);
            rd__transition(cl, col, &cs, RHI_STATE_COPY_SRC);
            rd__transition(cl, dep, &ds, RHI_STATE_COPY_SRC);
            rhi_end_commands(cl);
            rhi_submit(cl);
            rhi_wait_idle();
        }
        static uint8_t back[TEST_W * TEST_H * 4u + 4096u];
        uint32_t rp = 0;
        if (!why && (!rhi_readback_texture(col, RHI_ASPECT_COLOR, back, sizeof(back), &rp) ||
                     rp < TEST_W * 4u || (size_t)rp * TEST_H > sizeof(back))) {
            why = "its readback failed";
        }
        for (uint32_t i = 0; !why && i < TEST_W * TEST_H && bad < 0; i++) {
            const uint32_t x = i % TEST_W, y = i / TEST_W;
            const bool in =
                x >= (uint32_t)TEST_AX && x < (uint32_t)TEST_AX + TEST_AW && y < TEST_AH;
            for (uint32_t ch = 0; ch < 4u; ch++) {
                got[ch] = back[(size_t)y * rp + x * 4u + ch];
                want[ch] = in ? 0u : kTestColour[ch];
            }
            if (memcmp(got, want, 4) != 0) {
                bad = (int)i;
            }
        }
        /* the depth loaded and stored inside the area, kept outside it:
         * 0.25 everywhere (where the device reads depth back) */
        const RhiLimits *lim = rhi_limits();
        if (!why && bad < 0 && lim && lim->depthReadback) {
            static float backD[TEST_W * TEST_H + 1024u];
            uint32_t dp = 0;
            if (rhi_readback_texture(dep, RHI_ASPECT_DEPTH, backD, sizeof(backD), &dp) &&
                dp >= TEST_W * 4u && (size_t)dp * TEST_H <= sizeof(backD)) {
                for (uint32_t i = 0; i < TEST_W * TEST_H && bad < 0; i++) {
                    const uint32_t x = i % TEST_W, y = i / TEST_W;
                    float v;
                    memcpy(&v, (const uint8_t *)backD + (size_t)y * dp + x * 4u, sizeof(v));
                    if (v != 0.25f) {
                        bad = (int)(TEST_W * TEST_H + i);
                        gotD = v;
                    }
                }
            }
        }
    }
    rhi_wait_idle();
    if (col.id) {
        rhi_destroy_texture(col);
    }
    if (dep.id) {
        rhi_destroy_texture(dep);
    }
    if (!why && bad < 0) {
        s_test.state = 1;
        snprintf(s_test.words, sizeof(s_test.words), "passed");
        rd__log("lamps: area self-test passed: a %ux%u colour target cleared over a %ux%u render "
                "area kept the rest as stored, its depth loaded there kept everywhere%s",
                TEST_W, TEST_H, TEST_AW, TEST_AH, readable ? " (readable passes)" : "");
    } else if (why) {
        snprintf(s_test.words, sizeof(s_test.words), "not run: %s", why);
        rd__log("lamps: area self-test not run (%s): the lamps' pass drawn whole", why);
    } else if (bad >= (int)(TEST_W * TEST_H)) {
        snprintf(s_test.words, sizeof(s_test.words), "failed at depth texel %d", bad);
        rd__log("lamps: area self-test failed: depth texel %d read %g for 0.25: the lamps' pass "
                "drawn whole",
                bad - (int)(TEST_W * TEST_H), (double)gotD);
    } else {
        snprintf(s_test.words, sizeof(s_test.words), "failed at texel %d", bad);
        rd__log("lamps: area self-test failed at texel %d: %02X%02X%02X%02X for %02X%02X%02X%02X: "
                "the lamps' pass drawn whole",
                bad, got[0], got[1], got[2], got[3], want[0], want[1], want[2], want[3]);
    }
    note();
    return s_test.state == 1;
}

/* --------------------------------------------------------------- the scan */

static void whyOnce(int why);

static uint32_t idOf(int named)
{
    return rd_target((RdTargetId)named).id;
}

/* command c under st writes target id (rd_glow_light.c writesTarget's
 * rule: a command it does not know writes every target) */
static bool writesW2(const RdCmd *c, const RdStateBlock *st, uint32_t id)
{
    switch (c->type) {
    case RDC_NOP:
    case RDC_OVERLAY_TEXT:
        return false;
    case RDC_CLEAR:
        return c->u[0] == id;
    case RDC_UPSCALE_POINT:
        return c->u[1] == id;
    case RDC_COPY:
    case RDC_EXACT_BLEND:
        return c->u[1] == id || c->u[0] == id;
    case RDC_SCREEN:
    case RDC_MESH:
    case RDC_SKINNED:
    case RDC_GRID:
    case RDC_PARTICLES:
    case RDC_POST_STUB:
    case RDC_SHADOW_RESET:
    case RDC_SHADOW_STRIP:
    case RDC_SHADOW_RESOLVE:
    case RDC_WORLD_PRIMS:
        return st->color == id;
    default:
        return true;
    }
}

/* the vertices a frame's lamps may transform one by one (a lamp is a few
 * dozen; past this the corners' box of the stream's bound is taken) */
#define LAMPS_VERTEX_BUDGET 16384u

/* a mesh draw's texels on tc (RD_LAMPS_BOX_NONE), or why not: every vertex
 * of its stream (the draws' batches and a sheet's tiles lie in it,
 * rd_vis.c) through VU memory 16..19 one by one, each in front of the eye
 * (rd__lamps_vertex_box: the points the GPU draws of a triangle whose
 * corners are in front lie in the hull of the corners' x / w, y / w); past
 * the frame's budget the occlusion test's box of the stream's bound
 * (rd__vis_hiz_occludee, its 8 corners, which a mesh across the screen near
 * the eye fails far more often) */
static int meshBox(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, const RdTargetRec *tc,
                   W2Write *w)
{
    const uint64_t after = sizeof(RdVuPayload) + sizeof(RdVuBlock);
    if (!f->payload || c->u[2] < after || (uint64_t)c->u[1] + c->u[2] > f->payloadSize) {
        return RD_LAMPS_BOX_PAYLOAD;
    }
    const uint8_t *pay = f->payload + c->u[1];
    RdVuPayload p;
    memcpy(&p, pay, sizeof(p));
    w->prog = p.prog;
    w->code = p.code;
    w->clip = p.clip;
    const bool none = p.clip == RD_VU_CLIP_NONE;
    if (p.clip != RD_VU_CLIP_REGION && p.clip != RD_VU_CLIP_SCISSOR && !none) {
        return RD_LAMPS_BOX_CLIP;
    }
    if (p.prog > RD_PROG_REFLECT) {
        return RD_LAMPS_BOX_PROG;
    }
    const RdMeshRec *m = rd__mesh_rec(c->u[0]);
    if (m) {
        snprintf(w->name, sizeof(w->name), "%s", m->name);
        w->verts = m->vertexCount;
    }
    if (!m || !m->vu || !m->stream || !m->vertexCount || !m->qwPerVertex ||
        p.firstBatch >= m->batchCount) {
        return RD_LAMPS_BOX_MESH;
    }
    float mem[36][4];
    memcpy(mem, pay + sizeof(RdVuPayload), sizeof(mem));
    const bool scissor = p.clip == RD_VU_CLIP_SCISSOR;
    double gb[4];
    if (s_budgetUsed + m->vertexCount <= LAMPS_VERTEX_BUDGET) {
        s_budgetUsed += m->vertexCount;
        const int why =
            rd__lamps_vertex_box((const float (*)[4]) & mem[16], (const float (*)[4])m->stream,
                                 m->vertexCount, m->qwPerVertex, scissor, gb, &w->behind);
        if (why != RD_LAMPS_BOX_NONE) {
            return why;
        }
        /* code 34 (clip mode none) draws every corner at its GS position,
         * X and Y wrapped to 16 bits (4096 GS pixels): exact, so the same
         * box, where every corner lies inside (1, 4094) and nothing wraps */
        if (none && !(gb[0] > 1.0 && gb[1] > 1.0 && gb[2] < 4094.0 && gb[3] < 4094.0)) {
            return RD_LAMPS_BOX_CLIP;
        }
    } else if (none) {
        return RD_LAMPS_BOX_CLIP;
    } else {
        RdVisBox b;
        RdVisHizBox hb;
        if (!rd__vis_box((const float (*)[4])m->stream, m->vertexCount, m->qwPerVertex, &b) ||
            !b.n || !rd__vis_hiz_occludee((const float (*)[4]) & mem[16], &b, scissor, &hb)) {
            return RD_LAMPS_BOX_BUDGET;
        }
        gb[0] = hb.x0;
        gb[1] = hb.y0;
        gb[2] = hb.x1;
        gb[3] = hb.y1;
    }
    if (!rd__lamps_box_texels(tc, st->gsW, st->gsH, gb[0], gb[1], gb[2], gb[3], LAMPS_PAD,
                              &w->box)) {
        w->box = (RhiRect){0, 0, 0, 0}; /* off the target: nothing drawn on it */
    }
    return RD_LAMPS_BOX_NONE;
}

/* a screen-prim command's texels on tc (rd_pass_area.c screenKind's
 * reading: its vertices' box padded, through the UI flip either way) */
static bool screenBox(const RdFrame *f, const RdCmd *c, const RdStateBlock *st,
                      const RdTargetRec *tc, RhiRect *box)
{
    *box = (RhiRect){0, 0, 0, 0};
    const uint32_t n = c->u[1];
    if (rd__wrap_applies(st) || st->ds.test.date != RD_DATE_OFF ||
        (c->b[1] == RD_SPACE_FULLFRAME && c->b[0] == RD_PRIM_SPRITES)) {
        return false;
    }
    if (n == 0) {
        return true;
    }
    if (!f->payload || (uint64_t)c->u[0] + (uint64_t)n * sizeof(RdScreenVtx) > f->payloadSize) {
        return false;
    }
    const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + c->u[0]);
    const int aa1 = st->aa1 != 0;
    RhiRect a = {0, 0, 0, 0}, b = {0, 0, 0, 0};
    if (!rd__screen_area(tc, st, v, n, 0, aa1, &a)) {
        a = (RhiRect){0, 0, 0, 0};
    }
    if (c->b[1] == RD_SPACE_UI && rd__screen_area(tc, st, v, n, 1, aa1, &b)) {
        a = rd__lamps_union(a, b);
    }
    *box = a;
    return true;
}

void rd__lamps_scan_begin(const RdFrame *f)
{
    s_scan.on = f != NULL && g_rd.hasDevice;
    s_scan.frame = f ? f->number : 0u;
    s_scan.replay = g_rd.replayCounter;
    s_scan.w2 = idOf(RD_TARGET_WORK2);
    s_scan.overflow = false;
    s_scan.n = 0;
    s_scan.want = s_scan.confirmed = s_scan.area = false;
    s_scan.areaRect = s_scan.box = (RhiRect){0, 0, 0, 0};
    s_scan.fillOrd = s_scan.redOrd = UINT32_MAX;
    s_scan.why = RD_LAMPS_WHY_NONE;
    s_budgetUsed = 0;
}

void rd__lamps_scan_cmd(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, bool state,
                        uint32_t ord)
{
    if (!s_scan.on || state || !writesW2(c, st, s_scan.w2)) {
        return;
    }
    if (s_scan.n >= LAMPS_MAX) {
        s_scan.overflow = true;
        return;
    }
    W2Write *w = &s_scan.w[s_scan.n++];
    memset(w, 0, sizeof(*w));
    w->ord = ord;
    w->type = (uint8_t)c->type;
    const RdTargetRec *tc = rd__target_rec(st->color);
    int why = RD_LAMPS_BOX_KIND;
    if (tc && st->color == s_scan.w2 && c->type == RDC_MESH) {
        why = meshBox(f, c, st, tc, w);
    } else if (tc && st->color == s_scan.w2 && c->type == RDC_SCREEN) {
        why = screenBox(f, c, st, tc, &w->box) ? RD_LAMPS_BOX_NONE : RD_LAMPS_BOX_SCREEN;
    }
    w->ok = why == RD_LAMPS_BOX_NONE;
    w->why = (uint8_t)why;
}

/* a lamp's box not known: counted by its part, and a line once a part
 * naming the draw (its command's type and ordinal, the mesh, its program,
 * code and clip mode, the vertices behind the eye) */
static void boxNot(const W2Write *w)
{
    const int k = w->why < RD_LAMPS_BOX_COUNT ? w->why : RD_LAMPS_BOX_KIND;
    sumAdd(&s_boxSum[k], 1);
    g_rdPerf.lampsBoxWhy = (uint32_t)k;
    if (s_saidBox[k]) {
        return;
    }
    s_saidBox[k] = true;
    rd__log("lamps: a lamp's screen box not known: %s (command type %u at ordinal %u, mesh "
            "\"%s\" of %u vertices, program %u code %u clip mode %u, %u vertices behind the "
            "eye; frame %u): the lamps' pass on WORK2 drawn whole (reported once for this "
            "reason)",
            kBoxWhy[k], (unsigned)w->type, w->ord, w->name[0] ? w->name : "-", w->verts,
            (unsigned)w->prog, (unsigned)w->code, (unsigned)w->clip, w->behind,
            (unsigned)s_scan.frame);
}

/* WORK2's texture still the tracker's */
static bool sameTexture(const RdTargetRec *t)
{
    return s_w2.valid && t && t->color.id && t->color.id == s_w2.tex && t->tw == s_w2.tw &&
           t->th == s_w2.th && !t->clearPending;
}

void rd__lamps_scan_end(void)
{
    if (!s_scan.on || s_scan.replay != g_rd.replayCounter) {
        s_w2.valid = false;
        return;
    }
    const int plan = rd__glow_light_plan();
    const RdTargetRec *t2 = rd__target_rec(s_scan.w2);
    bool other = s_scan.overflow, boxOk = true;
    RhiRect box = {0, 0, 0, 0};
    for (uint32_t i = 0; i < s_scan.n; i++) {
        const W2Write *w = &s_scan.w[i];
        const int role = rd__glow_light_role(w->ord);
        if (plan == RD_GLOW_LIGHT_LAMPS &&
            (role == RD_GLOW_ROLE_FILL || role == RD_GLOW_ROLE_MASK_SKIPPED)) {
            continue; /* the fill as (0, 0, 0, 0), the mask not drawn */
        }
        if (plan == RD_GLOW_LIGHT_LAMPS &&
            (role == RD_GLOW_ROLE_LAMP || role == RD_GLOW_ROLE_FAN)) {
            if (!w->ok && boxOk) {
                boxNot(w); /* the first lamp refused names the reason */
            }
            boxOk = boxOk && w->ok;
            box = rd__lamps_union(box, w->box);
            continue;
        }
        if (plan == RD_GLOW_LIGHT_DARK &&
            (role == RD_GLOW_ROLE_FILL || role == RD_GLOW_ROLE_MASK_SKIPPED ||
             role == RD_GLOW_ROLE_FAN_SKIPPED)) {
            continue; /* (0, 0, 0, 0) or nothing drawn */
        }
        other = true;
    }
    if (plan != RD_GLOW_LIGHT_LAMPS) {
        if (other || g_rdWin.first || !sameTexture(t2)) {
            s_w2.valid = false;
        }
        return;
    }
    s_scan.fillOrd = rd__glow_light_role_ord(RD_GLOW_ROLE_FILL);
    s_scan.redOrd = rd__glow_light_role_ord(RD_GLOW_ROLE_REDUCTION);
    const int aw = areaWhy();
    int why = RD_LAMPS_WHY_NONE;
    if (aw != RD_LAMPS_WHY_NONE) {
        why = aw;
    } else if (g_rdWin.first) {
        why = RD_LAMPS_WHY_WINDOW;
    } else if (other) {
        why = RD_LAMPS_WHY_OTHER;
    } else if (!boxOk) {
        why = RD_LAMPS_WHY_BOX;
    } else if (!t2 || !t2->color.id || s_scan.fillOrd == UINT32_MAX ||
               s_scan.redOrd == UINT32_MAX || s_scan.redOrd < s_scan.fillOrd) {
        why = RD_LAMPS_WHY_FORM;
    }
    const bool known = sameTexture(t2);
    s_w2.valid = false; /* until the planned pass confirms the new region */
    s_scan.why = why;
    if (why != RD_LAMPS_WHY_NONE) {
        /* the pass as before, and nothing learned: the next lamps frame
         * whole too; counted here, once for the replay (any pass on WORK2
         * in it then only forgets) */
        g_rdPerf.lampsWhole++;
        g_rdPerf.lampsWhy = (uint32_t)why;
        s_totWhole++;
        sumAdd(&s_sum.whole, 1);
        sumAdd(&s_sum.why[why], 1);
        if (aw == RD_LAMPS_WHY_NONE) {
            whyOnce(why); /* the state line names the switch's, the class's and the test's */
        }
        return;
    }
    s_scan.want = true;
    s_scan.box = box;
    RhiRect a = {0, 0, 0, 0};
    const int pick = rd__lamps_area_pick(known, s_w2.lit, box, t2->tw, t2->th, &a);
    s_scan.area = pick == RD_LAMPS_WHY_NONE;
    s_scan.areaRect = a;
    s_scan.why = pick;
    /* the region as of now, for the area: kept in areaRect; the tracker
     * learns the new one when the pass begins as planned */
}

/* ------------------------------------------------------------ the pass */

static void forget(int why)
{
    s_w2.valid = false;
    if (s_scan.want && s_scan.replay == g_rd.replayCounter && !s_scan.confirmed) {
        s_scan.want = false;
        g_rdPerf.lampsWhole++;
        g_rdPerf.lampsWhy = (uint32_t)why;
        s_totWhole++;
        sumAdd(&s_sum.whole, 1);
        sumAdd(&s_sum.why[why], 1);
    }
}

static void whyOnce(int why)
{
    if (why > RD_LAMPS_WHY_NONE && why < RD_LAMPS_WHY_COUNT && !s_saidWhy[why]) {
        s_saidWhy[why] = true;
        rd__log("lamps: the lamps' pass on WORK2 drawn whole: %s (reported once for this "
                "reason)",
                kWhy[why]);
    }
}

/* the poison's magenta over the whole of WORK2's colour, before a planned
 * pass that clears it whole (the tests: a pass that should not rely on
 * WORK2's earlier contents but did would show it) */
static void poisonWhole(const RhiRenderPassDesc *p)
{
    RhiRenderPassDesc q;
    memset(&q, 0, sizeof(q));
    q.color[0].texture = p->color[0].texture;
    q.color[0].load = RHI_LOAD_CLEAR;
    q.color[0].readable = p->color[0].readable;
    q.color[0].clear[0] = 1.0f;
    q.color[0].clear[2] = 1.0f;
    q.color[0].clear[3] = 1.0f;
    q.colorCount = 1;
    q.width = p->width;
    q.height = p->height;
    const RhiCommandList cl = rd__replay_cl();
    rhi_cmd_begin_render_pass(cl, &q);
    rhi_cmd_end_render_pass(cl);
}

/* the traffic the area saves: the whole pass's figures less the area's */
static void savedTraffic(const RhiRenderPassDesc *p)
{
    RhiTrafficFmt cf[RHI_MAX_COLOR_TARGETS];
    memset(cf, 0, sizeof(cf));
    (void)rhi_traffic_format_of(p->color[0].texture, &cf[0]);
    RhiTrafficFmt df;
    memset(&df, 0, sizeof(df));
    if (p->depth.texture.id) {
        (void)rhi_traffic_format_of(p->depth.texture, &df);
    }
    RhiRenderPassDesc whole = *p;
    whole.area = (RhiRect){0, 0, 0, 0};
    RhiTrafficPass a, b;
    rhi_traffic_pass_sum(&whole, cf, df, &a);
    rhi_traffic_pass_sum(p, cf, df, &b);
    const uint32_t clear = (uint32_t)(a.clear - b.clear),
                   store = (uint32_t)(a.colorStore - b.colorStore);
    const uint32_t dload = (uint32_t)(a.depthLoad - b.depthLoad);
    const uint32_t dstore = (uint32_t)(a.depthStore - b.depthStore);
    g_rdPerf.lampsSavedClear += clear;
    g_rdPerf.lampsSavedColorStore += store;
    g_rdPerf.lampsSavedDepthLoad += dload;
    g_rdPerf.lampsSavedDepthStore += dstore;
    g_rdPerf.lampsAreaTexels += (uint32_t)b.pixels;
    g_rdPerf.lampsTexels += (uint32_t)a.pixels;
    sumAdd(&s_sum.clear, clear);
    sumAdd(&s_sum.store, store);
    sumAdd(&s_sum.dload, dload);
    sumAdd(&s_sum.dstore, dstore);
    sumAdd(&s_sum.areaTexels, b.pixels);
    sumAdd(&s_sum.texels, a.pixels);
}

bool rd__lamps_pass(RhiRenderPassDesc *p, uint32_t cid, bool ms, bool storage)
{
    const uint32_t w2 = idOf(RD_TARGET_WORK2);
    const RdTargetRec *t2 = rd__target_rec(w2);
    if (!p || p->colorCount == 0 || !t2 || !t2->color.id ||
        p->color[0].texture.id != t2->color.id) {
        return false; /* not on WORK2's colour */
    }
    const bool planned = s_scan.want && s_scan.replay == g_rd.replayCounter;
    const uint32_t ord = g_rdStoreOrd;
    const bool inRange = planned && cid == w2 && cid == s_scan.w2 && ord >= s_scan.fillOrd &&
                         ord <= s_scan.redOrd && !ms && !storage;
    const float *k = p->color[0].clear;
    const bool clear0 = p->color[0].load == RHI_LOAD_CLEAR && k[0] == 0.0f && k[1] == 0.0f &&
                        k[2] == 0.0f && k[3] == 0.0f && p->color[0].store == RHI_STORE_STORE &&
                        p->colorCount == 1;
    if (inRange && clear0 && !s_scan.confirmed) {
        /* the planned pass: WORK2 cleared to (0, 0, 0, 0) where it is not
         * already, the lamps drawn inside their box, stored */
        s_scan.confirmed = true;
        const RhiRect area = s_scan.areaRect;
        const bool known = s_scan.area;
        s_w2.valid = true;
        s_w2.tex = t2->color.id;
        s_w2.tw = t2->tw;
        s_w2.th = t2->th;
        s_w2.lit = s_scan.box;
        const RdTargetRec *sc = rd__target_rec(idOf(RD_TARGET_SCENE));
        /* SCENE's depth, or under LOWRES above 1x WORK2's own (rd__lamps_lowres_depth) */
        const bool depthOk =
            !p->depth.texture.id ||
            (((sc && p->depth.texture.id == sc->depth.id) ||
              (rd__lamps_lowres_on() && t2->depth.id && p->depth.texture.id == t2->depth.id)) &&
             p->depth.depthLoad != RHI_LOAD_CLEAR && p->depth.stencilLoad != RHI_LOAD_CLEAR);
        if (known && depthOk) {
            p->area = area;
            savedTraffic(p);
            g_rdPerf.lampsAreas++;
            g_rdPerf.lampsAreaX = area.x;
            g_rdPerf.lampsAreaY = area.y;
            g_rdPerf.lampsAreaW = area.w;
            g_rdPerf.lampsAreaH = area.h;
            s_totAreas++;
            sumAdd(&s_sum.areas, 1);
            if (!s_saidArea) {
                s_saidArea = true;
                rd__log("lamps: WORK2's lamps pass loads, clears and stores %ux%u at %d,%d of "
                        "%ux%u (the lamps' tiles and those the frame before lit; the rest of "
                        "WORK2 holds the fill's (0, 0, 0, 0) already; reported once)",
                        area.w, area.h, area.x, area.y, t2->tw, t2->th);
            }
            return true;
        }
        const int why = !depthOk ? RD_LAMPS_WHY_FORM : s_scan.why;
        g_rdPerf.lampsWhole++;
        g_rdPerf.lampsWhy = (uint32_t)why;
        s_totWhole++;
        sumAdd(&s_sum.whole, 1);
        sumAdd(&s_sum.why[why], 1);
        whyOnce(why);
        if (rd__lamps_poison()) {
            poisonWhole(p);
        }
        return false;
    }
    if (inRange && s_scan.confirmed && p->color[0].load == RHI_LOAD_LOAD &&
        p->color[0].store == RHI_STORE_STORE) {
        return false; /* the lamps after their clear's own pass: inside the box */
    }
    forget(RD_LAMPS_WHY_FORM);
    if (planned) {
        whyOnce(RD_LAMPS_WHY_FORM);
    }
    return false;
}

/* ------------------------------------------------------------ LOWRES */

/* RD_LAMPS_LOWRES (rd_lamps.h): WORK2 at the GS frame's size, its own depth
 * SCENE's taken down to it before the chain's first draw on it */
static struct {
    int on;          /* -1 unread, 0 off, 1 on (read once: WORK2's size) */
    bool box;        /* the reduction the box (lowresbox), else the GS bilinear */
    int depthForm;   /* depth_down_ps's: 0 bilinear, 1 bias, 2 four taps, 3 a point, 4 hybrid */
    const char *src; /* who decided: ICO_RD_LAMPS or the build's default */
    uint32_t replay; /* the replay whose SCENE depth WORK2's holds */
    bool fresh;      /* and no draw has written SCENE's depth since */
    RdDepthCopy copy;
    bool saidOn, saidDrop;
    uint64_t draws, fills, dropped;                 /* since rd_init */
    uint64_t sDraws, sFills, sDropped, sPic, sHold; /* the 10 s sums */
    uint32_t picReplay, holdReplay;
    uint32_t w, h, sw, sh; /* the last sizes (the line) */
} s_lr = {.on = -1};

bool rd__lamps_lowres_on(void)
{
    if (s_lr.on < 0) {
        /* ICO_RD_LAMPS's words where it has words it understands (env 1:
         * lowres, lowresbox, or neither), else (unset: 0, or not understood:
         * 2) the build's RD_LAMPS_LOWRES_DEFAULT; read once: WORK2's size */
        envRead();
        if (s_sw.env == 1) {
            s_lr.on = (s_sw.envBits & RD_LAMPS_LOWRES_ANY) != 0u ? 1 : 0;
            s_lr.box = (s_sw.envBits & RD_LAMPS_LOWRESBOX) != 0u;
            const uint32_t e = s_sw.envBits;
            s_lr.depthForm = (e & RD_LAMPS_LOWRESP)      ? 3
                             : (e & RD_LAMPS_LOWRES4)    ? 2
                             : (e & RD_LAMPS_LOWRESHY)   ? 4
                             : (e & RD_LAMPS_LOWRESBIAS) ? 1
                                                         : 0;
        } else {
            s_lr.on = RD_LAMPS_LOWRES_DEFAULT ? 1 : 0;
            s_lr.box = false;
            s_lr.depthForm = 0;
        }
        s_lr.src = s_sw.env == 1 ? "ICO_RD_LAMPS" : "the build's default";
    }
    return s_lr.on == 1;
}

bool rd__lamps_lowres_gs(const RdTargetRec *t)
{
    if (s_lr.on != 1 || !t || t != rd__target_rec(idOf(RD_TARGET_WORK2))) {
        return false;
    }
    const RdTargetRec *sc = rd__target_rec(idOf(RD_TARGET_SCENE));
    return sc && (sc->sx != 1.0f || sc->sy != 1.0f) && (t->tw != sc->tw || t->th != sc->th);
}

bool rd__lamps_lowres_box(const RdTargetRec *src)
{
    return s_lr.box && rd__lamps_lowres_gs(src);
}

bool rd__lamps_own_depth(uint32_t did)
{
    return s_lr.on == 1 && did != 0u && did == idOf(RD_TARGET_WORK2);
}

bool rd__lamps_fill_hold_ok(uint32_t color, uint32_t depth)
{
    if (s_lr.on != 1 || !(wanted() & RD_LAMPS_FILLHOLD) || color != idOf(RD_TARGET_WORK2)) {
        return false;
    }
    const RdTargetRec *w2 = rd__target_rec(color), *td = rd__target_rec(depth);
    if (!w2 || !td || td == w2 || !w2->color.id || !td->depth.id || !w2->withDepth ||
        (td->tw == w2->tw && td->th == w2->th)) {
        return false; /* 1x (or a target not made yet): SCENE's depth written, as without LOWRES */
    }
    if (s_lr.holdReplay != g_rd.replayCounter) {
        s_lr.holdReplay = g_rd.replayCounter;
        sumAdd(&s_lr.sHold, 1);
    }
    return true;
}

/* the start line: which form is on and who decided */
static void lowresNote(void)
{
    if (!g_rd.hasDevice) {
        return;
    }
    const bool on = rd__lamps_lowres_on();
    static const char *const kDepth[5] = {
        "the texel at a whole-number sample point, else bilinear",
        "the texel at a whole-number sample point, else the slope bias (lowresbias)",
        "the farthest of four texels (lowres4)", "a point compare (lowresp)",
        "the texel at a whole-number sample point, else bilinear on a plane (lowreshy)"};
    rd__log("lamps: low resolution %s (%s; RD_LAMPS_LOWRES_DEFAULT %d)%s%s%s%s", on ? "on" : "off",
            s_lr.src, RD_LAMPS_LOWRES_DEFAULT,
            on ? ": above 1x WORK2 at the GS frame's size with its own depth (" : "",
            on ? kDepth[s_lr.depthForm] : "", on ? "), its reduction " : "",
            on ? (s_lr.box ? "the box (lowresbox; DIVERGENCES.md VD-32)"
                           : "the GS bilinear (DIVERGENCES.md VD-32)")
               : "");
}

void rd__lamps_lowres_counts(uint64_t *draws, uint64_t *fills, uint64_t *dropped)
{
    if (draws) {
        *draws = s_lr.draws;
    }
    if (fills) {
        *fills = s_lr.fills;
    }
    if (dropped) {
        *dropped = s_lr.dropped;
    }
}

void rd__lamps_shutdown(void)
{
    if (s_lr.copy.tex.id && g_rd.hasDevice) {
        rd__depth_copy_free(&s_lr.copy);
    }
    memset(&s_lr.copy, 0, sizeof(s_lr.copy));
    s_lr.fresh = false;
}

/* WORK2's own depth (w2) from SCENE's (sc): a depth-only pass of WORK2's
 * size, depth_down_ps at every texel (the load a don't-care: every texel is
 * written; under ICO_RD_LAMPS_POISON a clear to a depth no draw gives, so a
 * texel it left would show) */
static bool lowresFill(Replay *r, RdTargetRec *w2, RdTargetRec *sc)
{
    RdPipeKeyInt k = rd__post_key(RD_VS_BLIT, RD_FS_DEPTH_DOWN, RHI_FMT_UNKNOWN);
    k.depthFmt = RHI_FMT_D32F_S8;
    k.gs.ztst = RD_ZTST_ALWAYS;
    k.gs.zwrite = RD_ZWRITE_ON;
    const RhiPipeline pipe = g_rd.fs[RD_FS_DEPTH_DOWN].id ? rd__get_pipeline(&k) : (RhiPipeline){0};
    if (!pipe.id) {
        return rd__pipeline_gathering(); /* a gathering replay names the key only */
    }
    rd__replay_end_pass(r);
    RhiCommandList cl = rd__replay_cl();
    /* SCENE's depth read in place where it can be sampled: outside any
     * pass, read-only, after the barrier (the fog's in-place path reads it
     * so); the transition closes an MSAA epoch first (rd__transition: the
     * depth resolved).  A copy only where the texture cannot be sampled: the
     * fog path's copy (g_rd.depthCopy on the local read) is for reads beside
     * SCENE's own pass, and would cost the phone 12.8 MB each way */
    RhiTexture src = {0};
    if (sc->depthSampled) {
        rd__transition(cl, sc->depth, &sc->depthState, RHI_STATE_DEPTH_READ);
        src = sc->depth;
    } else {
        src = rd__depth_copy_take(cl, sc, &s_lr.copy, "rd lamps SCENE depth copy");
    }
    if (!src.id) {
        return false;
    }
    RhiBinding b[3];
    memset(b, 0, sizeof(b));
    b[0].slot = 1;
    b[0].type = RHI_BIND_SAMPLED_TEXTURE;
    b[0].texture = src;
    b[0].aspect = RHI_ASPECT_DEPTH;
    b[1].slot = 1;
    b[1].type = RHI_BIND_SAMPLER;
    b[1].sampler = rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    b[2].slot = 2;
    b[2].type = RHI_BIND_SAMPLED_TEXTURE;
    b[2].texture = g_rd.dummy;
    const RhiBindGroup g = rd__replay_bind_group(g_rd.layoutTex, b, 3);
    if (!g.id) {
        return false;
    }
    const uint32_t w = w2->tw, h = w2->th, id = idOf(RD_TARGET_WORK2);
    const bool poison = rd__lamps_poison();
    rd__transition(cl, w2->depth, &w2->depthState, RHI_STATE_DEPTH_WRITE);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.colorCount = 0;
    p.depth.texture = w2->depth;
    p.depth.depthLoad = poison ? RHI_LOAD_CLEAR : RHI_LOAD_DONT_CARE;
    p.depth.clearDepth = 0.375f; /* poison: far from any GS Z the lamps test */
    p.depth.stencilLoad = RHI_LOAD_CLEAR;
    p.depth.clearStencil = 0;
    p.depth.store = RHI_STORE_STORE;
    p.depth.stencilStore = RHI_STORE_STORE;
    p.width = w;
    p.height = h;
    rd__perf_pass(cl, RD_PERF_PASS_RENDER, id, w, h, r->perfPost);
    rhi_cmd_checkpoint(cl, rd__replay_pass_push(RD_PASSK_PASS, id, w, h, id, w, h, 0, r->perfPost));
    rhi_cmd_begin_render_pass(cl, &p);
    rd__replay_viewport(w, h);
    const RhiRect all = {0, 0, w, h};
    rhi_cmd_set_scissor(cl, &all);
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.tex[0] = (float)sc->tw;
    cb.tex[1] = (float)sc->th;
    cb.tex[2] = 1.0f / (float)sc->tw;
    cb.tex[3] = 1.0f / (float)sc->th;
    cb.param[0] = sc->sx; /* SCENE's texels per GS pixel */
    cb.param[1] = sc->sy;
    cb.param[2] = sc->ox; /* its Full pixel crop */
    /* the form (depth_down_ps): x 0 the slope bias, 1 the four taps; y the
     * mismatch past half a texel (GS pixels); z the bias's factor; w its
     * constant in depth (GS Z units times SCENE's Z scale) */
    cb.uvRect[0] = (float)s_lr.depthForm;
    cb.uvRect[1] = RD_LAMPS_BIAS_REACH;
    cb.uvRect[2] = 1.0f;
    cb.param[3] = 1.0f; /* the texel itself at a whole-number sample point */
    /* a jittered frame's SCENE content moved by its jitter in texels
     * (rd_jitter.c; WORK2 at the GS size is not jittered: rd__frame_jitter_of) */
    float jit[2] = {0.0f, 0.0f};
    if (rd__frame_jitter_get(jit)) {
        cb.scale[0] = jit[0];
        cb.scale[1] = jit[1];
    }
    cb.uvRect[3] = RD_LAMPS_BIAS_UNITS * rd__target_z_scale(idOf(RD_TARGET_SCENE));
    rd__set_pipeline(cl, pipe, &k);
    rd__bind_uniform(cl, 0, rd__frame_group(w, h, 0.0f, 0.0f));
    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(cl, 2, g);
    rd__replay_draw(cl, 3, 0, 1);
    rd__stats_here()->draws++;
    rhi_cmd_end_render_pass(cl);
    return true;
}

void rd__lamps_lowres_depth(Replay *r, DrawSetup *ds)
{
    if (s_lr.on != 1 || !ds || !ds->tdId) {
        return;
    }
    const uint32_t id2 = idOf(RD_TARGET_WORK2);
    RdTargetRec *w2 = rd__target_rec(id2);
    RdTargetRec *td = rd__target_rec(ds->tdId);
    if (!w2 || !td) {
        return;
    }
    if (ds->tc != w2) {
        if (td != w2 && r->st.ds.zwrite == RD_ZWRITE_ON) {
            s_lr.fresh = false; /* SCENE's depth written: taken down again before the next */
        }
        return;
    }
    if (td == w2 || (td->tw == w2->tw && td->th == w2->th)) {
        return; /* 1x: the draw tests SCENE's own depth, as without LOWRES */
    }
    const bool fresh = s_lr.fresh && s_lr.replay == g_rd.replayCounter;
    bool ok = w2->withDepth && w2->depth.id && !ds->dateStencil && !r->dateStNow;
    if (ok && !fresh) {
        ok = lowresFill(r, w2, td);
        if (ok) {
            s_lr.fresh = true;
            s_lr.replay = g_rd.replayCounter;
            s_lr.fills++;
            sumAdd(&s_lr.sFills, 1);
        }
    }
    if (!ok) {
        /* no depth of WORK2's size to test: drawn without one (the pass
         * cannot pair WORK2 with SCENE's larger depth) */
        ds->tdId = 0;
        ds->depthFmt = RHI_FMT_UNKNOWN;
        s_lr.dropped++;
        sumAdd(&s_lr.sDropped, 1);
        if (!s_lr.saidDrop) {
            s_lr.saidDrop = true;
            rd__log("lamps: low resolution: a draw into WORK2 %ux%u drawn without a depth (%s; "
                    "frame %u, reported once)",
                    w2->tw, w2->th,
                    !w2->withDepth || !w2->depth.id ? "WORK2 has no depth of its own"
                    : ds->dateStencil || r->dateStNow
                        ? "its DATE takes SCENE's stencil"
                        : "SCENE's depth could not be taken down (no pipeline or no copy)",
                    (unsigned)s_scan.frame);
        }
        return;
    }
    w2->zFormat = td->zFormat; /* SCENE's Z format: the draw's FrameCB scale */
    ds->tdId = id2;
    s_lr.draws++;
    sumAdd(&s_lr.sDraws, 1);
    if (s_lr.picReplay != g_rd.replayCounter) {
        s_lr.picReplay = g_rd.replayCounter;
        sumAdd(&s_lr.sPic, 1);
    }
    s_lr.w = w2->tw;
    s_lr.h = w2->th;
    s_lr.sw = td->tw;
    s_lr.sh = td->th;
    if (!s_lr.saidOn) {
        s_lr.saidOn = true;
        rd__log("lamps: low resolution: WORK2 %ux%u (the GS frame's size) under SCENE %ux%u: "
                "the glow's fill, fans, mask and lamps drawn at the PS2's resolution, the "
                "lamps' and fans' depth WORK2's own (SCENE's taken down, depth_down_ps; "
                "DIVERGENCES.md VD-32; reported once)",
                w2->tw, w2->th, td->tw, td->th);
    }
}

/* ------------------------------------------------------------ the line */

/* LOWRES's 10 s line (on its own: the lamps' line keeps its words) */
static void lowresLine(void)
{
    const uint64_t pic = __atomic_exchange_n(&s_lr.sPic, 0, __ATOMIC_RELAXED);
    const uint64_t d = __atomic_exchange_n(&s_lr.sDraws, 0, __ATOMIC_RELAXED);
    const uint64_t f = __atomic_exchange_n(&s_lr.sFills, 0, __ATOMIC_RELAXED);
    const uint64_t x = __atomic_exchange_n(&s_lr.sDropped, 0, __ATOMIC_RELAXED);
    const uint64_t hold = __atomic_exchange_n(&s_lr.sHold, 0, __ATOMIC_RELAXED);
    if (pic + d + f + x + hold == 0) {
        return;
    }
    fprintf(stderr,
            "window: lamps low resolution: %llu pictures with WORK2 %ux%u under SCENE %ux%u, %llu "
            "draws into WORK2 on its own depth, %llu depth fills (depth_down_ps), %llu draws "
            "without a depth; %llu pictures whose lamps let the crack fill's hold pass "
            "(fillhold)\n",
            (unsigned long long)pic, s_lr.w, s_lr.h, s_lr.sw, s_lr.sh, (unsigned long long)d,
            (unsigned long long)f, (unsigned long long)x, (unsigned long long)hold);
}

void rd_lamps_line(void)
{
    lowresLine();
    uint64_t v[9];
    uint64_t *const src[9] = {&s_sum.areas,      &s_sum.whole,  &s_sum.pads,
                              &s_sum.areaTexels, &s_sum.texels, &s_sum.clear,
                              &s_sum.store,      &s_sum.dload,  &s_sum.dstore};
    for (int i = 0; i < 9; i++) {
        v[i] = __atomic_exchange_n(src[i], 0, __ATOMIC_RELAXED);
    }
    uint64_t w[RD_LAMPS_WHY_COUNT];
    for (int i = 0; i < RD_LAMPS_WHY_COUNT; i++) {
        w[i] = __atomic_exchange_n(&s_sum.why[i], 0, __ATOMIC_RELAXED);
    }
    const uint64_t owed = __atomic_exchange_n(&s_sum.owed, 0, __ATOMIC_RELAXED);
    const uint64_t fold4 = __atomic_exchange_n(&s_sum.fold4, 0, __ATOMIC_RELAXED);
    char f4[300] = "";
    size_t fat = 0;
    uint64_t alone = 0;
    for (int i = 1; i < RD_LAMPS_F4_COUNT; i++) {
        const uint64_t n = __atomic_exchange_n(&s_sum.f4why[i], 0, __ATOMIC_RELAXED);
        if (!n) {
            continue;
        }
        alone += n;
        const int m = snprintf(f4 + fat, sizeof(f4) - fat, "%s%llu %s", fat ? ", " : " (",
                               (unsigned long long)n, kF4Why[i]);
        if (m < 0 || (size_t)m >= sizeof(f4) - fat) {
            break;
        }
        fat += (size_t)m;
    }
    if (v[0] + v[1] + v[2] + owed + fold4 + alone == 0) {
        return;
    }
    char why[512] = "";
    size_t at = 0;
    for (int i = 1; i < RD_LAMPS_WHY_COUNT; i++) {
        if (!w[i]) {
            continue;
        }
        const int m = snprintf(why + at, sizeof(why) - at, "%s%llu %s", at ? ", " : " (",
                               (unsigned long long)w[i], kWhy[i]);
        if (m < 0 || (size_t)m >= sizeof(why) - at) {
            break;
        }
        at += (size_t)m;
    }
    char box[400] = "";
    size_t bat = 0;
    for (int i = 1; i < RD_LAMPS_BOX_COUNT; i++) {
        const uint64_t n = __atomic_exchange_n(&s_boxSum[i], 0, __ATOMIC_RELAXED);
        if (!n) {
            continue;
        }
        const int m =
            snprintf(box + bat, sizeof(box) - bat, "%s%llu %s",
                     bat ? ", " : "; boxes not known: ", (unsigned long long)n, kBoxWhy[i]);
        if (m < 0 || (size_t)m >= sizeof(box) - bat) {
            break;
        }
        bat += (size_t)m;
    }
    const double mb = 1.0 / 1048576.0;
    fprintf(stderr,
            "window: lamps: %llu lamps passes on WORK2 with an area (%.1f %% of their texels "
            "loaded and stored; %.1f MB not cleared, %.1f MB of colour and %.1f MB of depth "
            "not stored, %.1f MB of depth not loaded), %llu whole%s%s; %llu bands left lazy "
            "(WORK2_PAD: a pass fewer each); %llu WORK0 alpha sprites owed (dark pictures: a "
            "pass fewer each); %llu blur passes of two pairs (a pass fewer each), %llu pairs "
            "drawn alone%s%s%s\n",
            (unsigned long long)v[0], v[4] ? 100.0 * (double)v[3] / (double)v[4] : 0.0,
            (double)v[5] * mb, (double)v[6] * mb, (double)v[8] * mb, (double)v[7] * mb,
            (unsigned long long)v[1], why, at ? ")" : "", (unsigned long long)v[2],
            (unsigned long long)owed, (unsigned long long)fold4, (unsigned long long)alone, f4,
            fat ? ")" : "", box);
}
