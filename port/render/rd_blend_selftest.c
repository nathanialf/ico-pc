/* rd_blend_selftest.c: the blend's self-test (rd.h rd_blend_selftest).
 *
 * The blend in force is checked on the device at start: the
 * fixed-function blend (the default everywhere, rd_dst_read.c) and, when
 * [dev] blend opted into one, the exact blend, which reads the pixel behind
 * each blended fragment.  A driver whose read returns the wrong colour
 * draws a wrong picture with no error anywhere: issue 89, Adreno 830 on
 * Qualcomm's driver 512.800.72, where the attachment read gave white
 * around the leaves' clear texels, washed the title out and drew the menus
 * white on white, while the same frames are right through the same path
 * on lavapipe.  A driver whose dual-source blend is wrong is caught the
 * same way, and takes the two-pass fallback.  So is one that keeps a
 * dual-source equation on a draw with blending off: issues 146 and 154,
 * RADV on Mesa 25 on Radeon RX 7000 cards, every other column of every
 * blend-off draw after a dual-source one lost (the after cell below; the
 * backends send the equation on every change now, vk_cmd.c
 * vkr_flush_dynamic).
 *
 * The frame, built here and never recorded (rd_fog_path.c's helpers), on
 * SCENE at its real size and scale, in a block of 256 x 96 GS pixels in its
 * middle, list 0, Z ALWAYS without write, COLCLAMP 1, FBA 0, PABE 0:
 *   row 0  eight 32 x 32 cells, each a destination pixel of PCSX2's capture
 *          of the forest (rd_pixel_test.c kTint), then the forest's tint
 *          sprite over the row, RGBA 223 219 205 4, ALPHA 0x44 (the LERP
 *          (Cs - Cd) * As >> 7 + Cd): each cell must be PCSX2's pixel;
 *   row 1  x 0..64: a cell, then a textured quad (DECAL, nearest) whose left
 *          texel is clear (alpha 0) and right texel opaque: the left half
 *          must keep the cell's colour (the leaf case), the right half be
 *          the texel's;
 *          x 64..160: a cell, then two LERP draws, A over 64..128 and B over
 *          96..160: where they overlap B must blend over A's result;
 *          x 160..256: a cell, then one glyph quad through the text sheet's
 *          shader (font_sheet_ps, the largest destination-read shader);
 *   row 2  x 0..64: the after cell, opaque (ABE 0), drawn after every
 *          blended draw: it must be its own colour on an even and an odd
 *          texel column (a driver that keeps the LERP's dual-source
 *          equation with blending off leaves the odd one undefined).
 * The samples are the centres of the regions (six points in the glyph, two
 * neighbouring texel columns in the after cell), copied out of SCENE and
 * read back.  The modelled samples are
 * compared with the GS arithmetic, the glyph's with the fixed-function
 * run's pixels (the shader's colour is the same in every mode; only the
 * blend differs, by at most the blender's rounding).
 *
 * The order: the fixed-function blend first (the reference; it passes
 * within 2 levels, the blender rounding to nearest where the GS floors,
 * twice where the two draws overlap; failing that the two-pass fallback,
 * rd_set_no_dual, is tried and kept if it passes), then, only when
 * ICO_RD_BLEND ([dev] blend) opted into an exact mode the device has
 * (rd__dst_asked), that mode within 1 level, kept whatever its result
 * (run and logged).  It runs on every device, whatever the blend: a
 * device without dual-source blending starts from the two-pass blend.
 * MSAA is held off while it runs (the MSAA epoch draws with the
 * fixed-function blend), and SCENE is left to start from its clear.
 *
 *   blend: fixed-function (self-test: fixed-function ok 1; exact modes not
 *     asked for: [dev] blend = attachment|storage tries one) on SCENE
 *     640x448 at 1920x1344 texels in T ms
 *   blend: exact (attachment read) (self-test: attachment ok 1,
 *     fixed-function ok 1; kept by [dev] blend) on SCENE ...
 *   blend: fixed-function (self-test: fixed-function fail N -> two-pass
 *     ok 1; ...) on SCENE ...
 *   blend: self-test: attachment ok 1 at glyph 0 (texel 1105,709):
 *     73,57,41,23 want 74,57,41,23
 * (lavapipe: the glyph one level off the fixed-function reference, the
 * blender's rounding; the GS samples exact), and read white
 * (ICO_RD_BLEND_SABOTAGE=attachment):
 *   blend: self-test: attachment fail 241 at tint cell 0 (texel 625,625):
 *     255,255,255,4 want 21,28,14,4
 * and on a driver that loses the odd columns (the two-pass fallback then
 * tried, as for any fixed-function failure):
 *   blend: self-test: fixed-function fail N at after cell, odd column
 *     (texel x,y): ...
 *
 * ICO_RD_BLEND_SABOTAGE=attachment,storage,fixed,twopass (tests): those
 * entries' read-back samples are made white, as a driver whose read (or
 * blend) returns white would draw them, so the fallback can be run where
 * every mode is right.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"

#define BT_W 256 /* the block, GS pixels */
#define BT_H 96  /* three rows: 32 + 32 + 32 */

#define TOL_EXACT 1 /* levels */
#define TOL_FIXED 2

/* the entries of the log line; FIXED the reference, NODUAL its fallback */
enum { E_ATTACH, E_STORAGE, E_FIXED, E_NODUAL, E_COUNT };

enum { R_NOT_TRIED, R_ERROR, R_FAIL, R_OK };

static const char *const kEntryName[E_COUNT] = {"attachment", "storage", "fixed-function",
                                                "two-pass"};

/* rd_pixel_test.c kTint: PCSX2 2.8.2's rt0 pixels under the forest's tint
 * sprite and its rt1 pixels after it */
static const struct {
    uint8_t dst[3], pcsx2[3];
} kTint[8] = {
    {{15, 22, 8}, {21, 28, 14}},  {{12, 17, 14}, {18, 23, 19}}, {{37, 40, 31}, {42, 45, 36}},
    {{66, 84, 38}, {70, 88, 43}}, {{23, 31, 16}, {29, 36, 21}}, {{9, 9, 9}, {15, 15, 15}},
    {{79, 89, 56}, {83, 93, 60}}, {{25, 31, 24}, {31, 36, 29}},
};

static const uint8_t kTintSprite[4] = {223, 219, 205, 4};

static const uint8_t kLeafCell[4] = {40, 120, 60, 0x80};
/* the quad's texture: a clear texel (its colour must not show) and an
 * opaque one */
static const uint8_t kLeafTex[8] = {250, 250, 250, 0, 200, 40, 30, 0x80};

static const uint8_t kOverCell[4] = {30, 60, 200, 0x80};
static const uint8_t kOverA[4] = {220, 180, 20, 0x40};
static const uint8_t kOverB[4] = {10, 240, 120, 0x60};

static const uint8_t kGlyphCell[4] = {90, 70, 50, 0x80};
static const uint8_t kGlyphInk[4] = {0x80, 0x80, 0x80, 0x80};

/* the opaque cell drawn after the blended draws (row 2) */
static const uint8_t kAfterCell[4] = {170, 30, 110, 0x80};

#define SHEET_W 40
#define SHEET_H 12

#define N_GLYPH 6
#define N_SAMPLES (8 + 2 + 3 + N_GLYPH + 2)

typedef struct Sample {
    int16_t x, y;     /* GS pixels in the block */
    bool model;       /* want holds the GS arithmetic; else the reference's */
    const char *what; /* for the log */
    int idx;          /* logged after what when numbered */
    bool numbered;
    int want[4];
    /* 0: the texel x the GS pixel maps to; 1: the even column at or
     * before it, 2: the odd one after that (texelOf; the after cell) */
    uint8_t column;
} Sample;

static Sample s_samples[N_SAMPLES];

static int lerp(int cs, int cd, int a)
{
    const int v = (((cs - cd) * a) >> 7) + cd;
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

static void lerp4(const uint8_t cs[4], const int cd[4], int out[4])
{
    for (int ch = 0; ch < 3; ch++) {
        out[ch] = lerp(cs[ch], cd[ch], cs[3]);
    }
    out[3] = cs[3]; /* As is stored */
}

static void buildSamples(void)
{
    int n = 0;
    for (int i = 0; i < 8; i++) {
        Sample *s = &s_samples[n++];
        *s = (Sample){(int16_t)(i * 32 + 16), 16, true, "tint cell", i, true, {0}, 0};
        for (int ch = 0; ch < 3; ch++) {
            s->want[ch] = kTint[i].pcsx2[ch];
        }
        s->want[3] = kTintSprite[3];
    }
    s_samples[n++] = (Sample){16,
                              48,
                              true,
                              "leaf, clear texel",
                              0,
                              false,
                              {kLeafCell[0], kLeafCell[1], kLeafCell[2], kLeafTex[3]},
                              0};
    s_samples[n++] = (Sample){48,
                              48,
                              true,
                              "leaf, opaque texel",
                              0,
                              false,
                              {kLeafTex[4], kLeafTex[5], kLeafTex[6], kLeafTex[7]},
                              0};
    const int cell[4] = {kOverCell[0], kOverCell[1], kOverCell[2], kOverCell[3]};
    int a[4], ab[4], b[4];
    lerp4(kOverA, cell, a);
    lerp4(kOverB, a, ab);
    lerp4(kOverB, cell, b);
    s_samples[n++] =
        (Sample){80, 48, true, "overlap, first draw", 0, false, {a[0], a[1], a[2], a[3]}, 0};
    s_samples[n++] = (Sample){
        112, 48, true, "overlap, second over first", 0, false, {ab[0], ab[1], ab[2], ab[3]}, 0};
    s_samples[n++] =
        (Sample){144, 48, true, "overlap, second draw", 0, false, {b[0], b[1], b[2], b[3]}, 0};
    for (int g = 0; g < N_GLYPH; g++) {
        s_samples[n++] = (Sample){(int16_t)(176 + (g % 3) * 24),
                                  (int16_t)(44 + (g / 3) * 8),
                                  false,
                                  "glyph",
                                  g,
                                  true,
                                  {0},
                                  0};
    }
    for (int k = 0; k < 2; k++) {
        s_samples[n++] = (Sample){32,
                                  80,
                                  true,
                                  k ? "after cell, odd column" : "after cell, even column",
                                  0,
                                  false,
                                  {kAfterCell[0], kAfterCell[1], kAfterCell[2], kAfterCell[3]},
                                  (uint8_t)(k + 1)};
    }
}

/* ------------------------------------------------------------ the frame */

static bool pushBlend(RdFrame *f, int abe)
{
    RdCmd *c = rd__st_push(f, 0, RDC_BLEND);
    if (c) {
        c->b[0] = RD_BLEND_LERP_AS;
        c->b[1] = 0x80;
        c->b[2] = (uint8_t)abe;
    }
    return c != NULL;
}

static bool pushTexture(RdFrame *f, RdTex t, RdTexFn fn)
{
    RdCmd *c = rd__st_push(f, 0, RDC_TEXTURE);
    if (c) {
        c->u[0] = t.id;
        c->b[0] = (uint8_t)fn;
        c->b[1] = RD_TCC_RGBA;
    }
    c = c ? rd__st_push(f, 0, RDC_FILTER) : NULL;
    if (c) {
        c->b[0] = RD_FILTER_NEAREST;
        c->b[1] = RD_FILTER_NEAREST;
    }
    return c != NULL;
}

/* sprites (x0, y0)-(x1, y1) in the block's GS pixels, (u0, v0)-(u1, v1)
 * texels; one command */
typedef struct Rect {
    int16_t x0, y0, x1, y1;
    const uint8_t *rgba;
    int16_t u0, v0, u1, v1;
} Rect;

static bool pushSprites(RdFrame *f, int32_t bx, int32_t by, const Rect *r, int n)
{
    RdScreenVtx v[16];
    if (n > 8) {
        return false;
    }
    memset(v, 0, sizeof(v));
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < 2; k++) {
            RdScreenVtx *p = &v[i * 2 + k];
            p->x = (bx + (k ? r[i].x1 : r[i].x0)) * 16;
            p->y = (by + (k ? r[i].y1 : r[i].y0)) * 16;
            p->s = (float)((k ? r[i].u1 : r[i].u0) * 16);
            p->t = (float)((k ? r[i].v1 : r[i].v0) * 16);
            p->q = 1.0f;
            memcpy(p->rgba, r[i].rgba, 4);
        }
    }
    RdCmd *c = rd__st_push(f, 0, RDC_SCREEN);
    if (c) {
        c->b[0] = RD_PRIM_SPRITES;
        c->b[1] = RD_SPACE_FULLFRAME;
        c->b[2] = 1; /* UV in 12.4 texels */
        c->u[0] = rd__frame_payload(f, v, (uint32_t)(n * 2) * (uint32_t)sizeof(RdScreenVtx));
        c->u[1] = (uint32_t)(n * 2);
    }
    return c != NULL;
}

/* the block's top-left in the frame's XYOFFSET space */
static void blockOrigin(uint32_t w, uint32_t h, int32_t *bx, int32_t *by)
{
    *bx = 2048 - (int32_t)(w / 2) + (int32_t)(w - BT_W) / 2;
    *by = 2048 - (int32_t)(h / 2) + (int32_t)(h - BT_H) / 2;
}

static bool buildFrame(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h, RdTex leaf, RdTex sheet)
{
    static const uint8_t kClear[4] = {0, 0, 0, 0x80};
    memset(f, 0, sizeof(*f));
    rd__reset_state_block(&f->startState);
    f->gsW = w;
    f->gsH = h;
    int32_t bx, by;
    blockOrigin(w, h, &bx, &by);
    bool ok = rd__st_target(f, 0, scene, w, h);
    RdCmd *c = rd__st_push(f, 0, RDC_CLEAR);
    if (c) {
        c->u[0] = scene;
        memcpy(c->b, kClear, 4);
        c->b[4] = 1;
        c->u[1] = 0;
    }
    ok = ok && c && rd__st_push(f, 0, RDC_TEXTURE_OFF) && pushBlend(f, 0) &&
         rd__st_test(f, 0, RD_TEST_Z_ALWAYS) && rd__st_push1(f, 0, RDC_ZWRITE, RD_ZWRITE_OFF) &&
         rd__st_push1(f, 0, RDC_FBA, 0) && rd__st_push1(f, 0, RDC_PABE, 0) &&
         rd__st_push1(f, 0, RDC_COLCLAMP, 1) && rd__st_push1(f, 0, RDC_SHADE, 0);
    /* the cells, opaque */
    uint8_t tint[8][4];
    Rect cells[8];
    for (int i = 0; i < 8; i++) {
        memcpy(tint[i], kTint[i].dst, 3);
        tint[i][3] = 0x80;
        cells[i] = (Rect){(int16_t)(i * 32), 0, (int16_t)(i * 32 + 32), 32, tint[i], 0, 0, 0, 0};
    }
    const Rect row1[3] = {{0, 32, 64, 64, kLeafCell, 0, 0, 0, 0},
                          {64, 32, 160, 64, kOverCell, 0, 0, 0, 0},
                          {160, 32, 256, 64, kGlyphCell, 0, 0, 0, 0}};
    ok = ok && pushSprites(f, bx, by, cells, 8) && pushSprites(f, bx, by, row1, 3);
    /* the blended draws */
    const Rect tintR = {0, 0, 256, 32, kTintSprite, 0, 0, 0, 0};
    const Rect leafR = {0, 32, 64, 64, kOverA /* colour unused: DECAL */, 0, 0, 2, 1};
    const Rect overA = {64, 32, 128, 64, kOverA, 0, 0, 0, 0};
    const Rect overB = {96, 32, 160, 64, kOverB, 0, 0, 0, 0};
    const Rect glyph = {168, 36, 248, 60, kGlyphInk, 0, 0, SHEET_W, SHEET_H};
    ok = ok && pushBlend(f, 1) && pushSprites(f, bx, by, &tintR, 1) &&
         pushTexture(f, leaf, RD_TEXFN_DECAL) && pushSprites(f, bx, by, &leafR, 1) &&
         rd__st_push(f, 0, RDC_TEXTURE_OFF) && pushSprites(f, bx, by, &overA, 1) &&
         pushSprites(f, bx, by, &overB, 1) && pushTexture(f, sheet, RD_TEXFN_MODULATE) &&
         pushSprites(f, bx, by, &glyph, 1);
    /* the after cell: blending off after the dual-source LERPs */
    const Rect after = {0, 64, 64, 96, kAfterCell, 0, 0, 0, 0};
    ok = ok && rd__st_push(f, 0, RDC_TEXTURE_OFF) && pushBlend(f, 0) &&
         pushSprites(f, bx, by, &after, 1);
    return ok;
}

/* ------------------------------------------------------------- the runs */

static struct {
    bool armed;   /* rd_blend_selftest has run: a new SCENE size runs it again */
    bool running; /* rd__blend_scene_remade during a run does nothing */
    uint32_t w, h, tw, th;
    RhiTexture read;
    RhiState readState;
    uint8_t sabotage; /* ICO_RD_BLEND_SABOTAGE: a bit per entry */
    bool fellBack;    /* the last run took the two-pass fallback: the next starts from
                         the dual-source blend again, so each run's line is whole */
} s_bt;

/* the sample's texel in SCENE (rd_fog_path.c stSamples' mapping) */
static void texelOf(const RdTargetRec *t, const Sample *s, uint32_t *x, uint32_t *y)
{
    const double gx = (double)((t->w - BT_W) / 2) + s->x + 0.5;
    const double gy = (double)((t->h - BT_H) / 2) + s->y + 0.5;
    const double tx = t->ox > 0.0f ? gx * t->sx - t->ox : gx * t->tw / t->w;
    *x = tx < 0.0 ? 0u : (tx >= (double)t->tw ? t->tw - 1u : (uint32_t)tx);
    if (s->column) {
        /* the even column at or before x, or the odd one after it */
        *x = (*x & ~1u) + (s->column == 2 ? 1u : 0u);
        *x = *x >= t->tw ? t->tw - 1u : *x;
    }
    const double ty = gy * t->th / t->h;
    *y = ty >= (double)t->th ? t->th - 1u : (uint32_t)ty;
}

/* one replay of f under the mode in force, the samples into px */
static bool runOnce(const RdFrame *f, RdTargetRec *t, uint8_t px[N_SAMPLES * 4])
{
    if (!rd__replay_frame(f, 0, false)) {
        return false;
    }
    if (!s_bt.read.id) {
        s_bt.read = rhi_create_texture(&(RhiTextureDesc){N_SAMPLES, 1, 1, t->format,
                                                         RHI_TEX_COPY_DST | RHI_TEX_COPY_SRC,
                                                         "rd blend self-test", 0});
        s_bt.readState = RHI_STATE_UNDEFINED;
        if (!s_bt.read.id) {
            return false;
        }
    }
    RhiCommandList cl = rhi_begin_commands();
    if (!cl.id) {
        return false;
    }
    rd__transition(cl, t->color, &t->colorState, RHI_STATE_COPY_SRC);
    rd__transition(cl, s_bt.read, &s_bt.readState, RHI_STATE_COPY_DST);
    for (int k = 0; k < N_SAMPLES; k++) {
        uint32_t x, y;
        texelOf(t, &s_samples[k], &x, &y);
        rhi_cmd_copy_texture(cl, t->color, (RhiRect){(int32_t)x, (int32_t)y, 1, 1}, s_bt.read, k,
                             0);
    }
    rhi_end_commands(cl);
    rhi_submit(cl);
    return rd__read_rhi_texture(s_bt.read, &s_bt.readState, N_SAMPLES, 1, px, N_SAMPLES * 4);
}

typedef struct Verdict {
    int result; /* R_* */
    int err;    /* the largest channel difference */
    int worst;  /* its sample */
    uint8_t px[N_SAMPLES * 4];
} Verdict;

/* px against the model (and, ref non-NULL, the glyph against ref's) */
static void judge(Verdict *v, int tol, const uint8_t *ref)
{
    v->err = 0;
    v->worst = 0;
    for (int k = 0; k < N_SAMPLES; k++) {
        const Sample *s = &s_samples[k];
        if (!s->model && !ref) {
            continue;
        }
        int e = 0;
        for (int ch = 0; ch < 4; ch++) {
            const int want = s->model ? s->want[ch] : ref[k * 4 + ch];
            const int d = abs((int)v->px[k * 4 + ch] - want);
            e = d > e ? d : e;
        }
        if (e > v->err) {
            v->err = e;
            v->worst = k;
        }
    }
    v->result = v->err <= tol ? R_OK : R_FAIL;
}

/* one entry run and judged; the mode and the two-pass switch are set by
 * the caller */
static void runEntry(int e, const RdFrame *f, RdTargetRec *t, const uint8_t *ref, Verdict *v)
{
    if (!runOnce(f, t, v->px)) {
        v->result = R_ERROR;
        return;
    }
    if (s_bt.sabotage & (1u << e)) {
        for (int k = 0; k < N_SAMPLES; k++) {
            memset(&v->px[k * 4], 255, 3);
        }
    }
    judge(v, e == E_FIXED || e == E_NODUAL ? TOL_FIXED : TOL_EXACT, ref);
}

static void readSabotage(void)
{
    s_bt.sabotage = 0;
    const char *sb = getenv("ICO_RD_BLEND_SABOTAGE");
    while (sb && *sb) {
        const char *end = strchr(sb, ',');
        const size_t n = end ? (size_t)(end - sb) : strlen(sb);
        if (n == 10 && strncmp(sb, "attachment", n) == 0) {
            s_bt.sabotage |= 1u << E_ATTACH;
        } else if ((n == 7 && strncmp(sb, "storage", n) == 0) ||
                   (n == 9 && strncmp(sb, "interlock", n) == 0) ||
                   (n == 3 && strncmp(sb, "rov", n) == 0)) {
            s_bt.sabotage |= 1u << E_STORAGE;
        } else if (n == 5 && strncmp(sb, "fixed", n) == 0) {
            s_bt.sabotage |= 1u << E_FIXED;
        } else if (n == 7 && strncmp(sb, "twopass", n) == 0) {
            s_bt.sabotage |= 1u << E_NODUAL;
        }
        sb = end ? end + 1 : NULL;
    }
    if (s_bt.sabotage) {
        rd__log("blend: ICO_RD_BLEND_SABOTAGE: the self-test's pixels of%s%s%s%s read white",
                (s_bt.sabotage & (1u << E_ATTACH)) ? " attachment" : "",
                (s_bt.sabotage & (1u << E_STORAGE)) ? " storage" : "",
                (s_bt.sabotage & (1u << E_FIXED)) ? " fixed-function" : "",
                (s_bt.sabotage & (1u << E_NODUAL)) ? " two-pass" : "");
    }
}

static void freeObjects(void)
{
    if (s_bt.read.id) {
        rhi_destroy_texture(s_bt.read);
    }
    s_bt.read = (RhiTexture){0};
}

/* "<entry> ok N", "fail N", "error" into out at *n */
static void entryText(char *out, size_t size, size_t *n, int e, const Verdict *v)
{
    int k;
    switch (v->result) {
    case R_OK:
        k = snprintf(out + *n, size - *n, "%s ok %d", kEntryName[e], v->err);
        break;
    case R_FAIL:
        k = snprintf(out + *n, size - *n, "%s fail %d", kEntryName[e], v->err);
        break;
    case R_ERROR:
        k = snprintf(out + *n, size - *n, "%s error", kEntryName[e]);
        break;
    default:
        k = snprintf(out + *n, size - *n, "%s not tried", kEntryName[e]);
        break;
    }
    if (k > 0) {
        *n += (size_t)k;
        if (*n >= size) {
            *n = size - 1;
        }
    }
}

static bool runSelftest(void)
{
    const int asked = rd__dst_asked(); /* > 0: an exact mode opted into, on the device */
    RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (!t || !t->color.id || t->format != RHI_FMT_RGBA8_UNORM || t->w < BT_W || t->h < BT_H) {
        snprintf(g_rdSumBlend, sizeof g_rdSumBlend, "%s (self-test: no SCENE target)",
                 rd__blend_exact_name(g_rd.dstMode));
        rd__log("blend: self-test: no SCENE target to run it on");
        return false;
    }
    const double t0 = rd__now_ms();
    s_bt.w = t->w;
    s_bt.h = t->h;
    s_bt.tw = t->tw;
    s_bt.th = t->th;
    buildSamples();
    uint8_t cov[SHEET_W * SHEET_H];
    memset(cov, 0, sizeof(cov));
    for (int y = 3; y < 9; y++) {
        for (int x = 10; x < 30; x++) {
            cov[y * SHEET_W + x] = 255;
        }
        cov[y * SHEET_W + 30] = 128;
    }
    const RdTex leaf = rd_create_texture(2, 1, kLeafTex, RD_TEXA_80_80, "rd blend self-test leaf");
    const RdTex sheet =
        rd_create_texture_sheet(SHEET_W, SHEET_H, cov, NULL, "rd blend self-test glyph");
    static RdFrame f;
    if (!leaf.id || !sheet.id ||
        !buildFrame(&f, rd_target(RD_TARGET_SCENE).id, t->w, t->h, leaf, sheet)) {
        rd__frame_free(&f);
        rd_destroy_texture(leaf);
        rd_destroy_texture(sheet);
        snprintf(g_rdSumBlend, sizeof g_rdSumBlend, "%s (self-test: could not build its frame)",
                 rd__blend_exact_name(g_rd.dstMode));
        rd__log("blend: self-test: could not build its frame");
        return false;
    }
    if (s_bt.fellBack) {
        rd_set_no_dual(false);
        s_bt.fellBack = false;
    }
    const bool noDualBefore = g_rd.noDual;
    const bool wasBlocked = g_rd.msaaBlocked;
    g_rd.msaaBlocked = true; /* the MSAA epoch draws with the fixed-function blend */
    s_bt.running = true;

    static Verdict v[E_COUNT];
    for (int e = 0; e < E_COUNT; e++) {
        v[e].result = R_NOT_TRIED;
        v[e].err = v[e].worst = 0;
    }
    /* the reference: the fixed-function blend, the two-pass fallback when
       the dual-source blend fails (or from the start on a device without
       it) */
    g_rd.dstMode = RD_BLEND_EXACT_OFF;
    const int fixedEntry = g_rd.noDual ? E_NODUAL : E_FIXED;
    runEntry(fixedEntry, &f, t, NULL, &v[fixedEntry]);
    int refEntry = v[fixedEntry].result == R_OK ? fixedEntry : -1;
    if (refEntry < 0 && fixedEntry == E_FIXED && rd_set_no_dual(true)) {
        runEntry(E_NODUAL, &f, t, NULL, &v[E_NODUAL]);
        if (v[E_NODUAL].result == R_OK) {
            refEntry = E_NODUAL;
        } else {
            rd_set_no_dual(false);
        }
    }
    const uint8_t *ref = refEntry >= 0 ? v[refEntry].px : NULL;
    /* the exact mode [dev] blend opted into, kept whatever it finds */
    const int askedEntry = asked == RD_BLEND_EXACT_ATTACHMENT ? E_ATTACH
                           : asked == RD_BLEND_EXACT_STORAGE  ? E_STORAGE
                                                              : -1;
    if (askedEntry >= 0) {
        g_rd.dstMode = (uint8_t)asked;
        runEntry(askedEntry, &f, t, ref, &v[askedEntry]);
    }
    s_bt.running = false;
    g_rd.msaaBlocked = wasBlocked;
    rd__frame_free(&f);
    rd_destroy_texture(leaf);
    rd_destroy_texture(sheet);
    freeObjects();
    /* the next replay starts SCENE from its clear, not from the block */
    t->clearPending = 1;

    const int chosen = askedEntry >= 0 ? asked : RD_BLEND_EXACT_OFF;
    g_rd.dstMode = (uint8_t)chosen;
    s_bt.fellBack = g_rd.noDual && !noDualBefore;
    if (s_bt.fellBack) {
        rd__log("blend: the fixed-function blend through the two-pass fallback (the dual-source "
                "blend failed the self-test)");
    }

    /* the results: the asked exact mode, then the reference (and its
       fallback after "->") */
    char res[200];
    size_t n = 0;
    res[0] = '\0';
    if (askedEntry >= 0) {
        entryText(res, sizeof res, &n, askedEntry, &v[askedEntry]);
        n += (size_t)snprintf(res + n, sizeof(res) - n, ", ");
        n = n >= sizeof res ? sizeof res - 1 : n;
    }
    entryText(res, sizeof res, &n, fixedEntry, &v[fixedEntry]);
    if (fixedEntry == E_FIXED && v[E_NODUAL].result != R_NOT_TRIED) {
        n += (size_t)snprintf(res + n, sizeof(res) - n, " -> ");
        n = n >= sizeof res ? sizeof res - 1 : n;
        entryText(res, sizeof res, &n, E_NODUAL, &v[E_NODUAL]);
    }
    const char *why = askedEntry >= 0 ? "; kept by [dev] blend"
                                      : "; exact modes not asked for: [dev] blend = "
                                        "attachment|storage tries one";
    snprintf(g_rdSumBlend, sizeof g_rdSumBlend, "%s (self-test: %s%s)",
             rd__blend_exact_name(chosen), res, askedEntry >= 0 ? why : "");
    rd__log("blend: %s (self-test: %s%s) on SCENE %ux%u at %ux%u texels in %.1f ms",
            rd__blend_exact_name(chosen), res, why, t->w, t->h, t->tw, t->th, rd__now_ms() - t0);
    for (int e = 0; e < E_COUNT; e++) {
        /* a failing entry's worst sample, and a passing one's when it is not
           exact (the blender's rounding, or a driver near the limit) */
        if (v[e].result != R_FAIL && !(v[e].result == R_OK && v[e].err > 0)) {
            continue;
        }
        const Sample *s = &s_samples[v[e].worst];
        const uint8_t *p = &v[e].px[v[e].worst * 4];
        int want[4];
        for (int ch = 0; ch < 4; ch++) {
            want[ch] = s->model ? s->want[ch] : (ref ? ref[v[e].worst * 4 + ch] : 0);
        }
        uint32_t x, y;
        texelOf(t, s, &x, &y);
        char what[48];
        if (s->numbered) {
            snprintf(what, sizeof what, "%s %d", s->what, s->idx);
        } else {
            snprintf(what, sizeof what, "%s", s->what);
        }
        rd__log("blend: self-test: %s %s %d at %s (texel %u,%u): %u,%u,%u,%u want %d,%d,%d,%d",
                kEntryName[e], v[e].result == R_FAIL ? "fail" : "ok", v[e].err, what, x, y, p[0],
                p[1], p[2], p[3], want[0], want[1], want[2], want[3]);
    }
    /* a failed entry on Direct3D 11: the state of the test's last draw
       (issue 126: every entry read back the cleared target), from the
       backend (RhiStats.lastDrawText, NULL on the other backends) */
    bool anyFail = false;
    for (int e = 0; e < E_COUNT; e++) {
        anyFail = anyFail || v[e].result == R_FAIL;
    }
    if (anyFail) {
        RhiStats st;
        memset(&st, 0, sizeof(st));
        rhi_get_stats(&st);
        const char *draw = st.lastDrawText ? st.lastDrawText() : NULL;
        if (draw) {
            rd__log("blend: self-test failed on Direct3D 11; its last draw: %s", draw);
        }
    }
    if (askedEntry >= 0) {
        return v[askedEntry].result == R_OK;
    }
    return refEntry >= 0;
}

bool rd_blend_selftest(void)
{
    if (!g_rd.inited || !g_rd.hasDevice) {
        return false;
    }
    readSabotage();
    s_bt.armed = true;
    return runSelftest();
}

void rd__blend_scene_remade(void)
{
    if (!g_rd.hasDevice || !s_bt.armed || s_bt.running) {
        return;
    }
    const RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (!t || (t->w == s_bt.w && t->h == s_bt.h && t->tw == s_bt.tw && t->th == s_bt.th)) {
        return;
    }
    (void)runSelftest();
}

void rd__blend_selftest_shutdown(void)
{
    freeObjects();
    s_bt.armed = false;
    s_bt.fellBack = false;
}
