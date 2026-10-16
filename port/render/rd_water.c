/* rd_water.c: the render-to-texture surfaces of
 * puddle.c, pool.c and queen_barrier_disp.c.
 *
 * Recording only.  Three things the GS register decoder cannot know:
 *
 *   - which target a VRAM block that is also a named buffer stands for in a
 *     given list (rd_gs_named_block, rd_block_target, rd_alias_target): the
 *     three files draw into the block tex_AllocVramAuto hands out after
 *     tex_ResetVramPri, TBP 0x2800, which the decoder maps to AA0;
 *   - that the target needs its own depth buffer (puddle and pool point ZBUF
 *     at a second block, work1Vram);
 *   - the camera of a mid-frame gsb_SetVSMatrix (rd_push_camera).
 *
 * The records of the open frame live here, one per frame slot (RdContext
 * keeps RD_FRAME_RING frames), and are cleared by rd__frame_reset when a slot is
 * reused.  Nothing here is dumped: the commands name the block target, so a
 * dump replays as recorded.
 *
 * Also the screen-space STs of pool.c's grids (rd_grid_screen_st): the
 * formula and scales wait here for the next rd_draw_vu_grid, which writes
 * them into its command (so dumps carry them). */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "rd_internal.h"

#define RD_WATER_BLOCKS 8
#define RD_WATER_ALIASES 4
#define RD_CAMERA_SCOPES 8
#define RD_CAMERA_DEPTH 4

typedef struct WaterBlock {
    uint32_t tbp, w, h, withDepth, id;
} WaterBlock;

typedef struct WaterAlias {
    uint32_t from, to;
} WaterAlias;

typedef struct WaterFrame {
    uint32_t number; /* RdFrame.number of the frame these records belong to */
    WaterBlock blocks[RD_WATER_BLOCKS];
    uint32_t blockCount;
    WaterAlias alias[RD_LIST_COUNT][RD_WATER_ALIASES];
    RdCameraScope scopes[RD_CAMERA_SCOPES];
    uint32_t scopeCount;
    int32_t open[RD_CAMERA_DEPTH]; /* scope indices of the open pushes */
    uint32_t depth;
} WaterFrame;

static WaterFrame s_water[RD_FRAME_RING];

static WaterFrame *waterOf(const RdFrame *f)
{
    if (f == NULL || f < g_rd.frames || f >= g_rd.frames + RD_FRAME_RING) {
        return NULL;
    }
    WaterFrame *w = &s_water[f - g_rd.frames];
    if (w->number != f->number) {
        memset(w, 0, sizeof(*w));
        w->number = f->number;
    }
    return w;
}

void rd__water_frame_reset(const RdFrame *f)
{
    if (f >= g_rd.frames && f < g_rd.frames + RD_FRAME_RING) {
        memset(&s_water[f - g_rd.frames], 0, sizeof(s_water[0]));
    }
}

/* ----------------------------------------------------------- the blocks */

RdTarget rd_gs_named_block(uint32_t tbp, uint32_t gsW, uint32_t gsH)
{
    RdTargetId id;

    if (tbp & 31) {
        return (RdTarget){0};
    }
    /* GifPacket.c gsTargetOfFbp, by FBP; its TEX0 table (gsResolveTex0)
     * agrees for the sizes these files use (TH 8 or 9 at 0x2800).  The
     * classes are the frame's: a block up to a quarter of the frame's
     * width at 0x2C00 is AA1 (128 at 4:3), wider is WORK1 */
    switch (tbp >> 5) {
    case 0x000:
        id = RD_TARGET_DISPLAY;
        break;
    case 0x040:
        id = RD_TARGET_SCENE;
        break;
    case 0x140:
        id = gsH <= 128 ? RD_TARGET_WORK0 : RD_TARGET_AA0;
        break;
    case 0x142:
        id = RD_TARGET_SHADOW0;
        break;
    case 0x160:
        id = gsW <= (g_rd.gsW ? g_rd.gsW : 512u) / 4u ? RD_TARGET_AA1 : RD_TARGET_WORK1;
        break;
    case 0x180:
        id = RD_TARGET_WORK2;
        break;
    case 0x1F8:
        id = RD_TARGET_FEED128;
        break;
    default:
        return (RdTarget){0};
    }
    return rd_target(id);
}

RdTarget rd_block_target(uint32_t tbp, uint32_t gsW, uint32_t gsH, int withDepth)
{
    WaterFrame *w = waterOf(rd__rec_frame());
    uint32_t i;

    if (w == NULL || gsW == 0 || gsH == 0) {
        return (RdTarget){0};
    }
    withDepth = withDepth ? 1 : 0;
    for (i = 0; i < w->blockCount; i++) {
        const WaterBlock *b = &w->blocks[i];
        if (b->tbp == tbp && b->w == gsW && b->h == gsH && b->withDepth >= (uint32_t)withDepth &&
            rd__target_rec(b->id) != NULL) {
            return (RdTarget){b->id};
        }
    }
    if (w->blockCount == RD_WATER_BLOCKS) {
        rd__log("rd_block_target: more than %d blocks in one frame", RD_WATER_BLOCKS);
        return (RdTarget){0};
    }
    /* marked as a block: under Water reflections: Sharp one that is not
     * scene-sized takes the scene's scale (rd_core.c rd__target_scale_of) */
    RdTarget t = rd__block_temp_target(gsW, gsH, withDepth);
    if (t.id != 0) {
        WaterBlock *b = &w->blocks[w->blockCount++];
        b->tbp = tbp;
        b->w = gsW;
        b->h = gsH;
        b->withDepth = (uint32_t)withDepth;
        b->id = t.id;
    }
    return t;
}

void rd_alias_target(RdTarget from, RdTarget to)
{
    WaterFrame *w = waterOf(rd__rec_frame());
    int l = rd_current_list();
    int i, freeSlot = -1;

    if (w == NULL || from.id == 0 || l < 0 || l >= RD_LIST_COUNT) {
        return;
    }
    for (i = 0; i < RD_WATER_ALIASES; i++) {
        WaterAlias *a = &w->alias[l][i];
        if (a->from == from.id) {
            a->to = to.id;
            if (to.id == 0) {
                a->from = 0;
            }
            return;
        }
        if (a->from == 0 && freeSlot < 0) {
            freeSlot = i;
        }
    }
    if (to.id == 0) {
        return;
    }
    if (freeSlot < 0) {
        rd__log("rd_alias_target: more than %d aliases in list %d", RD_WATER_ALIASES, l);
        return;
    }
    w->alias[l][freeSlot].from = from.id;
    w->alias[l][freeSlot].to = to.id;
}

uint32_t rd__alias_of(uint32_t id)
{
    RdFrame *f = rd__rec_frame();
    int l = rd_current_list();
    int i;

    if (f == NULL || id == 0 || l < 0 || l >= RD_LIST_COUNT) {
        return 0;
    }
    const WaterFrame *w = &s_water[f - g_rd.frames];
    if (w->number != f->number) {
        return 0;
    }
    for (i = 0; i < RD_WATER_ALIASES; i++) {
        if (w->alias[l][i].from == id) {
            return w->alias[l][i].to;
        }
    }
    return 0;
}

/* ----------------------------------------------------------- the camera */

void rd_push_camera(const RdCamera *cam)
{
    RdFrame *f = rd__rec_frame();
    WaterFrame *w = waterOf(f);
    int l = rd_current_list();

    if (w == NULL || cam == NULL || l < 0 || l >= RD_LIST_COUNT) {
        return;
    }
    if (w->depth == RD_CAMERA_DEPTH || w->scopeCount == RD_CAMERA_SCOPES) {
        rd__log("rd_push_camera: more than %d scopes (or %d deep) in one frame", RD_CAMERA_SCOPES,
                RD_CAMERA_DEPTH);
        w->depth++; /* the pop still balances */
        return;
    }
    RdCameraScope *s = &w->scopes[w->scopeCount];
    s->list = l;
    s->start = f->lists[l].count;
    s->end = UINT32_MAX;
    s->cam = *cam;
    w->open[w->depth++] = (int32_t)w->scopeCount++;
}

void rd_pop_camera(void)
{
    RdFrame *f = rd__rec_frame();
    WaterFrame *w = waterOf(f);

    if (w == NULL || w->depth == 0) {
        return;
    }
    w->depth--;
    if (w->depth >= RD_CAMERA_DEPTH) {
        return; /* a push past the limit */
    }
    RdCameraScope *s = &w->scopes[w->open[w->depth]];
    s->end = f->lists[s->list].count;
}

int rd_camera_depth(void)
{
    const WaterFrame *w = waterOf(rd__rec_frame());
    return w != NULL ? (int)w->depth : 0;
}

/* rd_frame_projected's switch: the recording thread's, like the rest of the
 * recorder's state */
static int s_frameProjected;

void rd_frame_projected(int on)
{
    s_frameProjected = on != 0;
}

bool rd__frame_projected(void)
{
    return s_frameProjected && rd_camera_depth() == 0;
}

const RdCamera *rd__camera_at(const RdFrame *f, int list, uint32_t index)
{
    const RdCamera *cam = f != NULL && f->hasCamera ? &f->camera : NULL;
    uint32_t i;

    if (f == NULL || f < g_rd.frames || f >= g_rd.frames + RD_FRAME_RING) {
        return cam;
    }
    const WaterFrame *w = &s_water[f - g_rd.frames];
    if (w->number != f->number) {
        return cam;
    }
    /* scopes are recorded outermost first, so the last match is the innermost */
    for (i = 0; i < w->scopeCount; i++) {
        const RdCameraScope *s = &w->scopes[i];
        if (s->list == list && index >= s->start && index < s->end) {
            cam = &s->cam;
        }
    }
    return cam;
}

uint32_t rd__camera_scopes(const RdFrame *f, const RdCameraScope **scopes)
{
    if (f == NULL || f < g_rd.frames || f >= g_rd.frames + RD_FRAME_RING) {
        return 0;
    }
    const WaterFrame *w = &s_water[f - g_rd.frames];
    if (w->number != f->number) {
        return 0;
    }
    if (scopes) {
        *scopes = w->scopes;
    }
    return w->scopeCount;
}

/* --------------------------------------------- the grids' screen STs */

/* rd_grid_screen_st's kind and scales, waiting for the next grid draw */
static struct {
    uint8_t kind;
    float sx, sy;
} s_gridSt;

void rd_grid_screen_st(RdGridSt kind, float sx, float sy)
{
    const bool ok = kind > RD_GRID_ST_NONE && kind < RD_GRID_ST_COUNT && sx > 0.0f && sy > 0.0f;
    s_gridSt.kind = ok ? (uint8_t)kind : (uint8_t)RD_GRID_ST_NONE;
    s_gridSt.sx = ok ? sx : 0.0f;
    s_gridSt.sy = ok ? sy : 0.0f;
}

void rd__grid_screen_st_take(uint8_t *kind, float *sx, float *sy)
{
    *kind = s_gridSt.kind;
    *sx = s_gridSt.sx;
    *sy = s_gridSt.sy;
    memset(&s_gridSt, 0, sizeof(s_gridSt));
}

/* ------------------------------------------------------ the pipelines
 * The screen-prim states of these files that the screen families
 * (rd__enumerate_reachable_screen) leave out, through rd__plan_screen_draw as
 * the replayer plans them, on SCENE's D32F_S8:
 *   puddle.c leveldown, copy, drawRipples (WORLD, list 4): TEST 0x3F001
 *   and 0x3F000 (DATE DATM 1, Z ALWAYS; AFAIL RGB_ONLY), Z write off,
 *   ALPHA modes 0 (ADD FIX), 2 (LERP FIX), 4 (LERP As, stage 34);
 *   waterDot.c (list 11 raw writes, recorded in WORLD space):
 *   TEST 0x50000, Z write off, mode 5 (ADD As). */
uint32_t rd__enumerate_reachable_water(RdPipeKeyInt *out, uint32_t max, uint32_t n)
{
    static const struct {
        uint64_t test;
        uint8_t space;
        int8_t blend;
    } kStates[] = {
        {RD_TEST_NEVER_RGBONLY_DATE1, RD_SPACE_WORLD, 0},
        {RD_TEST_NEVER_RGBONLY_DATE1, RD_SPACE_WORLD, 2},
        {RD_TEST_NEVER_RGBONLY_DATE1, RD_SPACE_WORLD, 4},
        {RD_TEST_RGBONLY_DATE1, RD_SPACE_WORLD, 0},
        {RD_TEST_RGBONLY_DATE1, RD_SPACE_WORLD, 4},
        {RD_TEST_Z_GEQUAL, RD_SPACE_WORLD, 5},
    };

    for (size_t i = 0; i < sizeof(kStates) / sizeof(kStates[0]); i++) {
        RdStateBlock s;
        rd__reset_state_block(&s);
        s.ds.test = rd_test_from_gs(kStates[i].test);
        s.ds.zwrite = RD_ZWRITE_OFF;
        s.ds.abe = 1;
        s.ds.blend = (uint8_t)kStates[i].blend;
        RdDrawPass dp[2];
        const int np = rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, kStates[i].space,
                                            RHI_FMT_RGBA8_UNORM, RHI_FMT_D32F_S8, dp);
        for (int k = 0; k < np; k++) {
            n = rd__add_pipe_key(out, max, n, &dp[k].key);
        }
    }
    return n;
}

/* The other-size rule's count (rd_replay.c resolveTexture, every draw that
 * samples a target): a render-to-texture block drawn at a scale other than
 * SCENE's (rd_core.c rd__target_scale_of), for the 10 s line (issue 106) */
void rd__block_read(const RdTargetRec *src)
{
    if (src->block) {
        const RdTargetRec *sc = rd__target_rec(RD_TARGET_SCENE + 1);
        if (sc && (src->sx != sc->sx || src->sy != sc->sy)) {
            g_rdPerf.blockOtherScaleReads++;
        }
    }
}

/* ------------------------------------ a block read wholly past its period
 * rd_internal.h rd__repeat_kept: the open border's clamp of a target read
 * (rd_replay.c drawWrap) leaves the ripples' read on REPEAT (issue 106) */
bool rd__repeat_kept(float lo, float hi, float size)
{
    if (!(size > 0.0f) || !(hi >= lo)) {
        return false;
    }
    const float k = floorf(lo / size);
    return k != 0.0f && hi <= (k + 1.0f) * size;
}

/* the texel range of the vertices on each axis, as convVtx converts them
 * (without the scroll offset: a texel's fraction at most) */
uint32_t rd__screen_repeat_kept(const RdScreenVtx *v, uint32_t n, int uvFixed, float tw, float th)
{
    if (n == 0) {
        return 0;
    }
    float lo[2] = {INFINITY, INFINITY}, hi[2] = {-INFINITY, -INFINITY};
    for (uint32_t i = 0; i < n; i++) {
        float c[2];
        if (uvFixed) {
            c[0] = v[i].s * (1.0f / 16.0f);
            c[1] = v[i].t * (1.0f / 16.0f);
        } else {
            const float q = v[i].q != 0.0f ? v[i].q : 1.0f;
            c[0] = v[i].s / q * tw;
            c[1] = v[i].t / q * th;
        }
        for (int a = 0; a < 2; a++) {
            lo[a] = c[a] < lo[a] ? c[a] : lo[a];
            hi[a] = c[a] > hi[a] ? c[a] : hi[a];
        }
    }
    return (rd__repeat_kept(lo[0], hi[0], tw) ? 1u : 0u) |
           (rd__repeat_kept(lo[1], hi[1], th) ? 2u : 0u);
}

/* ------------------------------------------- the F12 line (issue 106)
 * What a reporter's dump line alone has to tell about the water: which
 * scale each block was drawn at (Water reflections Original: its GS size;
 * Sharp: the scene's), and the state of the draws that wrote and read it
 * (puddle.c's leveldown, drawRipples and copy test DATE with AFAIL
 * RGB_ONLY; the puddle surface sets the alpha bit they test).  A dump
 * carries a texture-pack replacement as a blank image (rd_dump.c), so the
 * draws that sampled one are counted: a replay cannot show what they drew. */
#define WATER_LINE_BLOCKS 8
#define WATER_LINE_DATE 6

typedef struct WaterUse {
    uint32_t id;
    uint32_t into, reads;
    uint32_t date[3]; /* reads by RdDate */
    uint32_t afail;   /* reads' RdAFail bits (when the alpha test is on) */
    uint32_t blend;   /* reads' RdBlend bits; bit RD_BLEND_COUNT: blending off */
    uint32_t repeat;  /* reads on REPEAT on an axis */
    uint32_t kept;    /* of them, screen prims wholly past the first period on
                         it (rd__screen_repeat_kept: REPEAT kept under Full pixel) */
} WaterUse;

typedef struct WaterLineAcc {
    const RdFrame *f;
    WaterUse blocks[WATER_LINE_BLOCKS];
    uint32_t nBlocks, moreBlocks;
    WaterUse date[WATER_LINE_DATE]; /* DATE draws by colour target */
    uint32_t nDate;
    uint32_t packDraws;
    uint32_t datePackDraws; /* of them, DATE draws (the puddle's mask readers) */
} WaterLineAcc;

static WaterUse *waterUse(WaterUse *u, uint32_t *n, uint32_t max, uint32_t id, uint32_t *more)
{
    for (uint32_t i = 0; i < *n; i++) {
        if (u[i].id == id) {
            return &u[i];
        }
    }
    if (*n == max) {
        if (more) {
            (*more)++;
        }
        return NULL;
    }
    memset(&u[*n], 0, sizeof(u[*n]));
    u[*n].id = id;
    return &u[(*n)++];
}

static void waterNoteState(WaterUse *u, const RdStateBlock *s)
{
    u->date[s->ds.test.date < 3 ? s->ds.test.date : 0]++;
    if (s->ds.test.ate) {
        u->afail |= 1u << (s->ds.test.afail & 3u);
    }
    u->blend |= 1u << (s->ds.abe ? (s->ds.blend < RD_BLEND_COUNT ? s->ds.blend : RD_BLEND_COUNT)
                                 : RD_BLEND_COUNT);
}

static void waterLineWalk(void *user, int list, uint32_t index, const RdCmd *c,
                          const RdStateBlock *s)
{
    WaterLineAcc *a = (WaterLineAcc *)user;
    uint32_t readId = 0;
    (void)list, (void)index;
    if (c->type == RDC_COPY) {
        const RdTargetRec *src = rd__target_rec(c->u[0]);
        const RdTargetRec *dst = rd__target_rec(c->u[1]);
        WaterUse *u;
        if (dst && dst->block &&
            (u = waterUse(a->blocks, &a->nBlocks, WATER_LINE_BLOCKS, c->u[1], &a->moreBlocks))) {
            u->into++;
        }
        if (src && src->block &&
            (u = waterUse(a->blocks, &a->nBlocks, WATER_LINE_BLOCKS, c->u[0], &a->moreBlocks))) {
            u->reads++;
        }
        return;
    }
    if (c->type != RDC_SCREEN && c->type != RDC_MESH && c->type != RDC_SKINNED &&
        c->type != RDC_GRID && c->type != RDC_PARTICLES && c->type != RDC_WORLD_PRIMS) {
        return;
    }
    const RdTargetRec *tc = rd__target_rec(s->color);
    if (tc && tc->block) {
        WaterUse *u = waterUse(a->blocks, &a->nBlocks, WATER_LINE_BLOCKS, s->color, &a->moreBlocks);
        if (u) {
            u->into++;
        }
    }
    if (s->ds.texEnabled) {
        const RdTexRec *t = rd__tex_rec(s->tex);
        if (t && t->kind == RD_TEXKIND_TARGET) {
            const RdTargetRec *src = rd__target_rec(t->target);
            readId = src && src->block ? t->target : 0;
        } else if (t && t->replacement) {
            a->packDraws++;
            a->datePackDraws += s->ds.test.date != RD_DATE_OFF;
        }
    }
    if (readId) {
        WaterUse *u = waterUse(a->blocks, &a->nBlocks, WATER_LINE_BLOCKS, readId, &a->moreBlocks);
        if (u) {
            u->reads++;
            waterNoteState(u, s);
            const uint32_t rep = (s->ds.wrap.s == RD_WRAP_REPEAT ? 1u : 0u) |
                                 (s->ds.wrap.t == RD_WRAP_REPEAT ? 2u : 0u);
            u->repeat += rep != 0;
            const RdTargetRec *src = rd__target_rec(readId);
            if (rep && c->type == RDC_SCREEN && src) {
                const RdScreenVtx *v = (const RdScreenVtx *)(a->f->payload + c->u[0]);
                u->kept +=
                    (rd__screen_repeat_kept(v, c->u[1], c->b[2], (float)src->w, (float)src->h) &
                     rep) != 0;
            }
        }
    }
    if (s->ds.test.date != RD_DATE_OFF) {
        WaterUse *u = waterUse(a->date, &a->nDate, WATER_LINE_DATE, s->color, NULL);
        if (u) {
            u->reads++;
            waterNoteState(u, s);
        }
    }
}

/* appends to line (at most cap - 1 characters in all); the new length */
static size_t waterPut(char *line, size_t cap, size_t at, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int k = at < cap ? vsnprintf(line + at, cap - at, fmt, ap) : -1;
    va_end(ap);
    if (k > 0) {
        at = at + (size_t)k < cap ? at + (size_t)k : cap - 1;
    }
    return at;
}

/* the AFAIL modes and the blends of u's draws */
static size_t waterPutStates(char *line, size_t cap, size_t at, const WaterUse *u)
{
    static const char *const kAfail[4] = {"keep", "fb-only", "zb-only", "rgb-only"};
    for (int k = 0; k < 4; k++) {
        if (u->afail & (1u << k)) {
            at = waterPut(line, cap, at, ", AFAIL %s", kAfail[k]);
        }
    }
    at = waterPut(line, cap, at, ", blends");
    for (int k = 0; k <= RD_BLEND_COUNT; k++) {
        if (u->blend & (1u << k)) {
            at = k == RD_BLEND_COUNT ? waterPut(line, cap, at, " off")
                                     : waterPut(line, cap, at, " %d", k);
        }
    }
    return at;
}

size_t rd__water_dump_line(const RdFrame *f, char *line, size_t cap)
{
    static WaterLineAcc a; /* the F12 path only: one caller at a time */
    size_t at = 0;
    if (cap == 0) {
        return 0;
    }
    line[0] = 0;
    if (!f) {
        return waterPut(line, cap, at, "no frame");
    }
    memset(&a, 0, sizeof(a));
    a.f = f;
    RdStateBlock s = f->startState;
    rd__walk(f, f->keep, &s, waterLineWalk, &a);
    const RdTargetRec *sc = rd__target_rec(RD_TARGET_SCENE + 1);
    at = waterPut(line, cap, at, "frame %u: water reflections %s; scene %gx%g", f->number,
                  g_rd.settings.waterSharp ? "sharp" : "original", sc ? (double)sc->sx : 0.0,
                  sc ? (double)sc->sy : 0.0);
    if (a.nBlocks == 0) {
        at = waterPut(line, cap, at, "; no render-to-texture block");
    }
    for (uint32_t i = 0; i < a.nBlocks; i++) {
        const WaterUse *u = &a.blocks[i];
        const RdTargetRec *b = rd__target_rec(u->id);
        if (b) {
            at = waterPut(line, cap, at, "; block %ux%u GS%s at %ux%u texels (%gx%g, %s)", b->w,
                          b->h, b->withDepth ? " with depth" : "", b->tw, b->th, (double)b->sx,
                          (double)b->sy,
                          sc && (b->sx != sc->sx || b->sy != sc->sy) ? "not the scene's scale"
                                                                     : "the scene's scale");
        } else {
            at = waterPut(line, cap, at, "; block (gone)");
        }
        at = waterPut(line, cap, at, ": %u draws into it, %u read it", u->into, u->reads);
        if (u->reads) {
            at = waterPut(line, cap, at, " (DATE off %u, alpha 0 %u, alpha 1 %u", u->date[0],
                          u->date[1], u->date[2]);
            at = waterPutStates(line, cap, at, u);
            at = waterPut(line, cap, at, ", REPEAT %u, %u of them wholly past the first period",
                          u->repeat, u->kept);
            at = waterPut(line, cap, at, ")");
        }
    }
    if (a.moreBlocks) {
        at = waterPut(line, cap, at, "; %u draws on more blocks not listed", a.moreBlocks);
    }
    for (uint32_t i = 0; i < a.nDate; i++) {
        const WaterUse *u = &a.date[i];
        at = waterPut(line, cap, at, "; DATE draws on %s: alpha 0 %u, alpha 1 %u",
                      rd__target_name(u->id), u->date[1], u->date[2]);
        at = waterPutStates(line, cap, at, u);
    }
    /* Full pixel clamps a draw reading a render target at its edges,
     * except on the axes the REPEAT reads above keep (issue 106: clamped,
     * the ripples took the block's corner texel); the blend path the DATE
     * draws took */
    at = waterPut(line, cap, at, "; Full pixel %s, border mask %s; blend %s, storage pass merge %s",
                  g_rd.settings.fullPixel ? "on" : "off", g_rd.settings.borderMask ? "on" : "off",
                  rd__blend_exact_name(g_rd.dstMode), g_rd.passMerge ? "on" : "off");
    if (a.packDraws) {
        at = waterPut(line, cap, at,
                      "; %u draws sampled a texture-pack replacement (blank in the dump), %u of "
                      "them DATE draws",
                      a.packDraws, a.datePackDraws);
    }
    /* the DATE snapshot against the MSAA epoch, as the last replay took it
     * (issue 106: with MSAA a DATE draw on SCENE reads the alpha bit of the
     * resolved picture, the colour the mean of the samples) */
    RdDateMsaa dm;
    rd__date_msaa_last(&dm);
    if (dm.samples) {
        at = waterPut(line, cap, at,
                      "; MSAA %ux (resolve: colour the mean of the samples, depth the smallest "
                      "sample): the last replay's DATE snapshots of SCENE %u, %u of them read the "
                      "resolved picture (%u closed the epoch), of other targets %u",
                      dm.samples, dm.sceneSnaps, dm.afterResolve, dm.closing, dm.otherSnaps);
    } else {
        at = waterPut(line, cap, at,
                      "; no MSAA: the last replay's DATE snapshots of SCENE %u, of other "
                      "targets %u",
                      dm.sceneSnaps, dm.otherSnaps);
    }
    return at;
}
