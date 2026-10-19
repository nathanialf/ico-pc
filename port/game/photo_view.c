/*
 * port/game/photo_view.c
 *
 * Photo mode's camera in the game's matrices (photo_view.h).  Built with
 * the game's include path and record layout (it reads matrixptr, the pause
 * and the stage number), in the window and the headless build.
 */
#include <stdio.h>
#include <string.h>

#include "typedef.h"
#include "main.h"
#include "GsBase.h"
#ifdef ICO_RD
#include "BgAnimation.h"
#include "DisplayList.h"
#include "Matrix.h"
#include "RegistPacket.h"
#include "StageAnimation.h"
#include "gobj.h"
#include "quaternion.h"
#endif

#include "photo_mode.h"
#include "photo_view.h"
#include "../core/region.h" /* ico_region_cur */
#include "rd.h"
#include "video_options.h"

static struct {
    int saved;
    int stage;      /* stage_no at enter */
    float d;        /* the game camera's distance (gsb_ViewFocus) */
    RdCamera game0; /* the game camera at enter */
    /* the 500 unit pair (matrixptr +0x640, +0x680) of the tick's full
       screen projection, for ico_photo_view_veil; set while pair is */
    float screen500[2][16] __attribute__((aligned(16)));
    int pair;
} v;

int ico_photo_view_saved(void)
{
    return v.saved;
}

#ifdef ICO_RD
static void reelStop(void);
static void reelTick(void);
#endif

static void enter(void)
{
    gsb_PushView();
    memset(&v.game0, 0, sizeof(v.game0));
    memcpy(v.game0.view, matrixptr + 0x80, sizeof(v.game0.view));
    memcpy(v.game0.proj, matrixptr + 0xC0, sizeof(v.game0.proj));
    v.game0.aspect = gsFrame.aspect; /* the frame's display aspect, as gsbHostCommon */
    v.d = gsb_ViewFocus();
    v.stage = stage_no;
    v.saved = 1;
    if (!ico_photo_set_game(&v.game0)) {
        fprintf(stderr, "photo: the game camera's view does not invert\n");
    }
}

static void leave(void)
{
    const int same = stage_no == v.stage && systemStatus[5] != 0;
    if (same) {
        /* the game's camera back, its common block and the renderer's
           camera rebuilt from it, then the save once more: after a
           reflection pass (puddle.c) +0x240 is at the reflection's size
           while +0x280 is still the full screen's, and the rebuild would
           replace that +0x280 (the cull's matrix) with the reflection's */
        gsb_PopView();
        gsb_MakeCommonMatrix();
        gsb_PopView();
    }
    v.saved = 0;
    v.pair = 0;
#ifdef ICO_RD
    reelStop();
#endif
    ico_photo_set_game(NULL);
    ico_photo_set_left(1);
    /* the picture jumps back to the game camera: no blend into it */
    ico_video_camera_cut();
    if (!same) {
        fprintf(stderr, "photo: the stage changed or the game runs: its camera kept\n");
    }
}

#ifdef ICO_RD
/* 1 when the photo camera ov at zoom k is the game camera at enter: the
   same view matrix, no zoom */
static int photoIsGameCamera(const RdCamera *ov, float k)
{
    return k == 1.0f && memcmp(ov->view, v.game0.view, sizeof(ov->view)) == 0;
}
#endif

void ico_photo_view_tick(void)
{
    ico_photo_set_left(0);
    const int on = ico_photo_active() && systemStatus[5] != 0;
    if (v.saved && (!on || stage_no != v.stage)) {
        leave();
        return;
    }
    if (!on) {
        return;
    }
    if (!v.saved) {
        enter();
    }
    RdCamera ov;
    if (!ico_photo_camera(&ov, &v.game0)) {
        ov = v.game0;
    }
    IcoPhotoState st;
    ico_photo_get(&st);
    const float k = st.zoom > 0.0f ? st.zoom : 1.0f;
    memcpy(matrixptr + 0x80, ov.view, sizeof(ov.view));
    gsb_SetVSMatrix(ScreenWidth, ScreenHeight, v.d * k);
    /* the full screen's 500 unit pair, which a reflection pass drawn later
       in the frame replaces at its own size (puddle.c, pool.c) */
    memcpy(v.screen500, matrixptr + 0x640, sizeof(v.screen500));
    v.pair = 1;
    gsb_MakeCommonMatrix();
#ifdef ICO_RD
    /* the renderer keeps the triangles with a vertex behind this camera
       (RdCamera.freeCamera): the game's cameras rely on them being dropped,
       a free camera does not; the headless build has no renderer.  The
       game's own rule stays while the aspect sweep runs (its frames are
       the game's frame at each width, so a draw the game's rule drops
       beside the picture shows in the sweep as it does in play) and while
       the photo camera is still the game's camera (not moved, no zoom) */
    if (!ico_video_frame_forced() && !photoIsGameCamera(&ov, k)) {
        rd_free_camera();
    }
    /* Deja vu's film reel: one step of its noise animation a tick, where
       the game steps its stage animations (main.c, before the draw) */
    reelTick();
#endif
}

#ifdef ICO_RD
/* Photo mode's Deja vu (photo_mode.h ICO_PHOTO_FILM_DEJA): the deja
 * vision's grain veil over the photo, drawn as stage 40 draws it.
 *
 * The intent, from the game's data: stage 40 (deja) plays the stage
 * animation deja_lightning.bga (stageTable row 79, whose objTableScene range
 * holds the model 1614, sandstorm.p2o, of object kind 35), whose two
 * "sandstorm" entries (type 10, nodes 13 and 14 under CAMERA_PARENT) hold
 * the model in the camera's space (BgAnimation.c sets screenPos, node flag
 * 2): position (0, 0, 0.72907) and (0, 0, 0.74207), scale 0.0039999 (their
 * keys; the roll the keys also carry is no part of a camera-held node's
 * matrix, RegistPacket.c reg_setMMatrixPacket), and their type 0 envelopes
 * the node's fade (bga_calcEnvelope), resting at 0.9 and -0.95.
 * reg_dispMObj draws a node with a fade through reg_setDissolve in list 5:
 * 0.9 the sheet added at FIX 9 (ALPHA 0x68), -0.95 subtracted at FIX 4
 * (ALPHA 0x62), with the model's material (the "sandstorm" texture under
 * REPEAT, ATE GEQUAL 96, Z test GEQUAL, Z write).  The dump of the stage
 * (frame v2227) shows exactly that: list 5, two layers of the model's four
 * quadrant packets, GS X 2048 -+ 702.3 and 690.0 (w 3.6454 and 3.7104 =
 * 5 x the keys' depth, the 500 unit screen's scaling of a node nearer
 * than 5).
 *
 * The mechanism: every stage pack carries sandstorm.p2o and sandstorm.tm2
 * (the Film effect's own animations, stageTable rows 67 to 70, place the
 * same model in every stage), so the stage's own sandstorm object is
 * there: its DObj is borrowed for one reg_DispObj with two nodes of the
 * values above in place of its own, then given its own back.  The draws
 * are the game's packets through the game's path (the stage's texture, its
 * material and dissolve packets, the meshes rd_content_extend.c continues
 * to the frame's edges), so rd records them as it records stage 40's.  The
 * 500 unit pair is the full screen's of this tick (ico_photo_view_tick) for
 * the draw, and the display list priority is put back after it.  In stage
 * 40 the stage draws the veil itself: nothing here.  Nothing is allocated
 * or loaded. */
/* the running disc's (port/core/region.h): deja's stage (40), sandstorm.p2o's
   model id (1614; 1598 on the Japanese disc), the stage animation objects'
   kind (35, objTableScene) */
#define VEIL_STAGE (ico_region_cur()->stage_veil)
#define VEIL_MODEL (ico_region_cur()->veil_model)
#define VEIL_KIND (ico_region_cur()->veil_kind)

static const struct {
    float z;    /* the node's depth in the camera's space */
    float fade; /* the dissolve: > 0 added, < 0 subtracted */
} kVeilLayer[2] = {{0.72907043f, 0.9f}, {0.74207032f, -0.95f}};

#define VEIL_SCALE 0.0039999341f

/* the stage's sandstorm object, NULL without one */
static Sub15C *veilObject(void)
{
    GObj *g;

    for (g = isysGObjSearchFromObjKindID_begin(VEIL_KIND); g != 0;
         g = isysGObjSearchFromObjKindID_next(g)) {
        Sub15C *d = g->dobj;
        if (d != 0 && d->modelId == VEIL_MODEL && d->model != 0 && d->lightMtx != 0 &&
            d->dispType == 2) {
            return d;
        }
    }
    return 0;
}

/* Photo mode's Deja vu, its film reel: the scratches and specks of the deja
 * vision over the photo, drawn and animated as stage 40 draws and animates
 * them.
 *
 * The intent, from the game's data: deja_lightning.bga (stageTable row 79,
 * looping) also holds nine "noise" entries (type 10 under CAMERA_PARENT,
 * nodes 5 to 9, 11, 12, 15 and 16) of the models noiseA to noiseE (1609 to
 * 1613, 32 x 32 speck textures, quads of 0.066 across, noiseE's 0.906
 * tall), each with its own keyed motion (position about the picture's
 * middle in the camera's space, depth 0.83 to 1.17, scale, roll; 90 to 135
 * frames) and its own type 0 envelope, the node's fade (91 to 132 frames,
 * between -1 and 1: at -1 and 1 reg_dispMObj skips the node, between them
 * reg_setDissolve adds or subtracts it at FIX (1 - |fade|) x 96).  The
 * stage steps each node's motion and envelope one frame a tick and wraps
 * each at its own length (BgAnimation.c bga_stepMotion, bga_stepEnvelope,
 * the row's loop flag), so the flicker has no short period.  The dump of
 * the stage (frame v2227) shows it: list 5, before the veil, noiseB
 * subtracted at FIX 44, noiseD added at FIX 95, noiseE added at FIX 22 and
 * 44, at GS X 2263, 2020, 1802 and 1655: the tracks evaluated from the
 * stage's start (at 60 Hz) give exactly those four draws and places 81
 * ticks in, every other node at -1 or 1.  The placement is the vision's:
 * centred on the picture, each speck drawn once at its own size (not
 * repeated across a wide frame); over the tracks the specks reach GS X
 * 1648 to 2387, the 4:3 picture and up to 144 GS pixels left and 84 right
 * of it, which a wide frame shows as it does in the vision.
 *
 * The mechanism: the Film effect's fourth animation, film_noise_l4.bga
 * (stageTable row 70, the same objTableScene range of noiseA..E,
 * sandstorm and the rest), is in every stage pack and holds the same nine
 * noise entries key for key and envelope for envelope (its nodes 0 to 4,
 * 6, 7, 9 and 10), so every stage has the noise objects and the tracks.
 * Each tick (ico_photo_view_tick, where main.c steps the stage animations)
 * the row's header is evaluated by the game's own code: its frames (the
 * header's, every node's motion and envelopes) and the node records of the
 * objects it drives are saved, the reel's own frames put in, the row run
 * one step (bga_CalcAnimation, as stage_CalcAnimationNoParent runs a
 * playing row; the first tick bga_SetFrame from the start, as
 * stage_SetAnimation starts a looping row), the reel's frames read back and
 * the noise objects' evaluated nodes kept; then everything is put back, so
 * the game's own state of the row never changes.  At the draw
 * (ico_photo_view_veil) each noise object is borrowed with the kept nodes
 * for one reg_DispObj, before the veil as list 5 holds them in stage 40.
 * In stage 40 the stage draws the reel itself: nothing here. */
#define REEL_ROW (ico_region_cur()->reel_row) /* film_noise_l4.bga: 70, 66 on the Japanese disc */
#define REEL_LOOP 1   /* deja_lightning.bga's row (79, region.h veil_anim_row) loops */
#define REEL_NODES 24 /* the row's nodes kept (it has 13) */
#define REEL_ENVS 4   /* envelopes a node */
#define REEL_OBJS 8   /* the objects the row drives (it has 7) */
#define REEL_SLOTS 4  /* nodes an object (noiseE has 3) */
#define REEL_NOISE 5  /* noiseA .. noiseE */

/* noiseA.p2o .. noiseE.p2o (objTableScene rows 1253 to 1257) */
static const int kReelModel[REEL_NOISE] = {1609, 1610, 1611, 1612, 1613};

typedef struct {
    float head;                       /* the header's frame */
    float frame[REEL_NODES];          /* each node's own motion */
    float env[REEL_NODES][REEL_ENVS]; /* its envelopes' */
} ReelClock;

static struct {
    BgaHeader *head; /* the row the nodes below are of */
    int n;
    BgaDObjEnt *node[REEL_NODES];
    int envs[REEL_NODES];
    int nobj;
    Sub15C *obj[REEL_OBJS]; /* every object the row's nodes drive */
    int running;            /* the clock started (bga_SetFrame) */
    int stage;              /* stage_no of the clock and the draws */
    ReelClock clock;
    int draws; /* the noise objects kept for the draw */
    Sub15C *drawObj[REEL_NOISE];
    struct DObjNode nodes[REEL_NOISE][REEL_SLOTS];
    float mtx[REEL_NOISE][REEL_SLOTS][16] __attribute__((aligned(16)));
} reel;

/* the game's node records of the row's objects, while the reel runs */
static struct DObjNode s_reelOwnNodes[REEL_OBJS][REEL_SLOTS];
static float s_reelOwnMtx[REEL_OBJS][REEL_SLOTS][16] __attribute__((aligned(16)));
static float s_reelOwnQuat[REEL_OBJS][REEL_SLOTS][4] __attribute__((aligned(16)));

static void reelStop(void)
{
    reel.running = 0;
    reel.draws = 0;
    reel.head = 0;
}

static BgaPtMotion *reelMotion(BgaDObjEnt *d)
{
    return (BgaPtMotion *)&d->motion;
}

static BgaEnvEnt *reelEnv(BgaDObjEnt *d, int i)
{
    return &ICO_EEPTR(BgaEnvEnt *, d->env)[i];
}

/* the frame of envelope i of node d (its record's frame, BgaExtMotion) */
static float *reelEnvFrame(BgaDObjEnt *d, int i)
{
    return &ICO_EEPTR(BgaExtMotion *, reelEnv(d, i)->data)->frame;
}

/* node d and those after it in the tree, into reel; 0 on what the reel does
   not put back: a node bga_CalcObject gives a light, a particle, a
   lightning or a Kyomi object, an envelope of a light, the gizmo, the pivot
   or an object pointer, more nodes, envelopes or objects than kept */
static int reelWalk(BgaDObjEnt *d)
{
    for (; d != 0; d = ICO_EEPTR(BgaDObjEnt *, d->sibling)) {
        int k;

        if (reel.n >= REEL_NODES) {
            return 0;
        }
        switch (d->type) {
        case 0:
        case 4:
        case 10:
            if (d->u.obj != 0) {
                Sub15C *o = ICO_EEPTR(Sub15C *, d->u.obj);

                for (k = 0; k < reel.nobj && reel.obj[k] != o; k++) {}
                if (k == reel.nobj) {
                    if (k >= REEL_OBJS || o->nodeNum > REEL_SLOTS) {
                        return 0;
                    }
                    reel.obj[reel.nobj++] = o;
                }
            }
            break;
        case 1:
        case 2:
            break;
        default:
            return 0;
        }
        reel.node[reel.n] = d;
        reel.envs[reel.n] = 0;
        if (d->env != 0) {
            for (k = 0; reelEnv(d, k)->data != 0; k++) {
                if (k >= REEL_ENVS || reelEnv(d, k)->type > 1) {
                    return 0;
                }
            }
            reel.envs[reel.n] = k;
        }
        reel.n++;
        if (d->child != 0 && !reelWalk(ICO_EEPTR(BgaDObjEnt *, d->child))) {
            return 0;
        }
    }
    return 1;
}

static void reelRead(ReelClock *c, BgaHeader *p)
{
    int i;
    int k;

    c->head = p->frame;
    for (i = 0; i < reel.n; i++) {
        c->frame[i] = reelMotion(reel.node[i])->frame;
        for (k = 0; k < reel.envs[i]; k++) {
            c->env[i][k] = *reelEnvFrame(reel.node[i], k);
        }
    }
}

static void reelWrite(const ReelClock *c, BgaHeader *p)
{
    int i;
    int k;

    p->frame = c->head;
    for (i = 0; i < reel.n; i++) {
        reelMotion(reel.node[i])->frame = c->frame[i];
        for (k = 0; k < reel.envs[i]; k++) {
            *reelEnvFrame(reel.node[i], k) = c->env[i][k];
        }
    }
}

/* the node records of object k, out of it (save) or into it */
static void reelObjCopy(int k, int save)
{
    Sub15C *o = reel.obj[k];
    const size_t n = (size_t)o->nodeNum;

    if (save) {
        memcpy(s_reelOwnNodes[k], o->nodes, n * sizeof(struct DObjNode));
        memcpy(s_reelOwnMtx[k], (const void *)o->nodeMtx, n * 64);
        memcpy(s_reelOwnQuat[k], (const void *)o->nodeQuat, n * 16);
    } else {
        memcpy(o->nodes, s_reelOwnNodes[k], n * sizeof(struct DObjNode));
        memcpy((void *)o->nodeMtx, s_reelOwnMtx[k], n * 64);
        memcpy((void *)o->nodeQuat, s_reelOwnQuat[k], n * 16);
    }
}

static void reelTick(void)
{
    static int s_noReelStage = -1; /* the stage last logged without one */
    BgaHeader *p;
    ReelClock own;
    signed char mode;
    int i;
    int k;

    if (!ico_photo_deja() || stage_no == VEIL_STAGE) {
        reelStop();
        return;
    }
    p = stage_HostBgaHeader(REEL_ROW);
    reel.n = 0;
    reel.nobj = 0;
    for (i = 0; p != 0 && p->cut == 0 && BGA_ROOT(p, i) != 0; i++) {
        if (!reelWalk(BGA_ROOT(p, i))) {
            p = 0;
        }
    }
    if (p == 0 || p->cut != 0) {
        reelStop();
        if (stage_no != s_noReelStage) {
            s_noReelStage = stage_no;
            fprintf(stderr,
                    "photo: Deja vu: this place has no film noise animation; no scratches "
                    "drawn (stage %d)\n",
                    stage_no);
        }
        return;
    }
    if (p != reel.head || stage_no != reel.stage) {
        reel.running = 0;
    }
    /* the game's frames and node records aside */
    reelRead(&own, p);
    mode = p->mode;
    for (k = 0; k < reel.nobj; k++) {
        reelObjCopy(k, 1);
    }
    _PushCurrentMatrix();
    PushQuaternion();
    _InitCurrentMatrix();
    if (!reel.running) {
        bga_SetFrame(p, 0, 1, REEL_LOOP);
        reel.running = 1;
        reel.head = p;
        reel.stage = stage_no;
    } else {
        reelWrite(&reel.clock, p);
        p->mode = 1;
        bga_CalcAnimation(p, REEL_LOOP, 0);
    }
    PopQuaternion();
    _PopCurrentMatrix();
    reelRead(&reel.clock, p);
    /* the noise objects as evaluated, for the draw */
    reel.draws = 0;
    for (i = 0; i < REEL_NOISE; i++) {
        for (k = 0; k < reel.nobj; k++) {
            Sub15C *o = reel.obj[k];

            if (o->modelId == kReelModel[i] && o->model != 0 && o->lightMtx != 0 &&
                o->dispType == 2) {
                const int d = reel.draws++;

                reel.drawObj[d] = o;
                memcpy(reel.nodes[d], o->nodes, (size_t)o->nodeNum * sizeof(struct DObjNode));
                memcpy(reel.mtx[d], (const void *)o->nodeMtx, (size_t)o->nodeNum * 64);
                break;
            }
        }
    }
    /* the game's back */
    reelWrite(&own, p);
    p->mode = mode;
    for (k = 0; k < reel.nobj; k++) {
        reelObjCopy(k, 0);
    }
}

/* the noise objects, each borrowed with the reel's nodes for one
   reg_DispObj */
static void reelDraw(void)
{
    int i;

    if (!reel.running || reel.stage != stage_no) {
        return;
    }
    for (i = 0; i < reel.draws; i++) {
        Sub15C *o = reel.drawObj[i];
        const ICO_WORD nodeMtx = o->nodeMtx;
        struct DObjNode *own = o->nodes;

        o->nodeMtx = (ICO_WORD)reel.mtx[i];
        o->nodes = reel.nodes[i];
        reg_DispObj(o);
        o->nodeMtx = nodeMtx;
        o->nodes = own;
    }
}

void ico_photo_view_veil(void)
{
    static struct DObjNode nodes[2];
    static float mtx[2][16] __attribute__((aligned(16)));
    float saved500[2][16] __attribute__((aligned(16)));
    Sub15C *o;
    int nodeNum;
    ICO_WORD nodeMtx;
    struct DObjNode *own;
    int pri;
    int i;

    static int s_noVeilStage = -1; /* the stage last logged without one */

    if (!v.pair || stage_no == VEIL_STAGE) {
        return;
    }
    o = veilObject();
    if (o == 0 && stage_no != s_noVeilStage) {
        s_noVeilStage = stage_no;
        fprintf(stderr,
                "photo: Deja vu: this place has no grain sheet; no veil drawn "
                "(stage %d)\n",
                stage_no);
    }
    if (o == 0 && reel.draws == 0) {
        return;
    }
    memcpy(saved500, matrixptr + 0x640, sizeof(saved500));
    memcpy(matrixptr + 0x640, v.screen500, sizeof(saved500));
    pri = dl_GetPri();
    /* the film reel's specks first, then the veil, as list 5 holds them in
       stage 40 (the row's objects in objTableScene order) */
    reelDraw();
    if (o != 0) {
        memset(nodes, 0, sizeof(nodes));
        memset(mtx, 0, sizeof(mtx));
        for (i = 0; i < 2; i++) {
            nodes[i].scale[0] = nodes[i].scale[1] = nodes[i].scale[2] = 1.0f;
            nodes[i].fade = kVeilLayer[i].fade;
            nodes[i].alpha = 1.0f;
            nodes[i].flags.ll = 1 | 2; /* the fade, held in the camera's space */
            nodes[i].pos[2] = kVeilLayer[i].z;
            nodes[i].pos[3] = 1.0f;
            mtx[i][0] = mtx[i][5] = mtx[i][10] = VEIL_SCALE;
            mtx[i][15] = 1.0f;
        }
        nodeNum = o->nodeNum;
        nodeMtx = o->nodeMtx;
        own = o->nodes;
        o->nodeNum = 2;
        o->nodeMtx = (ICO_WORD)mtx;
        o->nodes = nodes;
        reg_DispObj(o);
        o->nodeNum = nodeNum;
        o->nodeMtx = nodeMtx;
        o->nodes = own;
    }
    dl_SetDLPriority(pri);
    memcpy(matrixptr + 0x640, saved500, sizeof(saved500));
}
#endif
