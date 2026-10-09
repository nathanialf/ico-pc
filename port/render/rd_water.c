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
 * dump replays as recorded. */
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
     * agrees for the sizes these files use (TH 8 or 9 at 0x2800) */
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
        id = gsW <= 128 ? RD_TARGET_AA1 : RD_TARGET_WORK1;
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
    RdTarget t = rd_temp_target(gsW, gsH, withDepth, 0);
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
