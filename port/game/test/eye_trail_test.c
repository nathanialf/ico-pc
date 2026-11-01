/*
 * port/game/test/eye_trail_test.c
 *
 * The shadows' eye trails rebuilt from their world points
 * (port/game/eye_trail.c) against the game's own point blur
 * (ico2/sugipon/src/enemyParts.c, the file linked whole; what it names from
 * the rest of the game is stubbed below), on one disc:
 *
 *   eye_trail_test [pal|jpn|kor|cn|ntsc|aug6|jun26]   (default pal)
 *
 * The strip is read where DispEnemyEye hands it to gif_Draw2DStripG (the
 * stub keeps the vertices), with photo mode's camera "moved" or not (the
 * ico_photo_view_moved stub).  Checks, for the selected disc:
 *   - 12 ticks of a moving point (UpdateEnemyEye), then the rebuild with
 *     the same matrices: the game's strip byte for byte, and the game's
 *     strip drawn as it was with the camera not moved (not frame-projected)
 *   - the camera moved and zoomed (+0x80 and +0xC0 changed): every pair the
 *     direct recomputation of UpdatePointBlur's arithmetic from the tick's
 *     world point and the one before it under the new matrices, the oldest
 *     point's pair (joined to the point two ticks before the strip's
 *     oldest) exact; frame-projected in the window build; the game's strip
 *     and the current matrix untouched
 *   - the reset path (ResetEnemyEye, then the disc's reset rule forcing
 *     every tick dirty): the copies of the newest pair, joined to the
 *     point the game joins it to, rebuilt byte for byte, and again under
 *     the moved camera
 *   - the first update after the init (the point joined to nothing the
 *     trail saw): the game's strip drawn, counted as kept
 *   - the side table forgotten (a state load): the game's strip drawn
 *   - the session's end: the counts start again
 * CPU only.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/region.h"
#include "../../test/ico_check.h"
#include "typedef.h"
#include "DObj.h"
#include "memory.h"
#include "DisplayP2O.h"
#include "RegistPacket.h"
#include "quaternion.h"
#include "sugiCommon.h"
#include "GsBase.h"
#include "sceneManager.h"
#include "enemyParts.h"
#include "GifPacket.h"
#include "main.h"
#include "matrixDrive.h"
#include "Matrix.h"
#include "ios.h"
#ifdef ICO_RD
#include "GifHost.h"
#endif

#include "../eye_trail.h"

#define POINTS 10
#define TICKS 12

/* --- what enemyParts.c names from the rest of the game -------------------- */

static char s_matrices[0x800] __attribute__((aligned(16)));
char *matrixptr = s_matrices;
int GlobalTimer;
int NonLinearCameraMove;
int currentScreenWidth;
float YUnitVector[4] = {0.0f, 1.0f, 0.0f, 0.0f};
float ZUnitVector[4] = {0.0f, 0.0f, 1.0f, 0.0f};
struct IosMemPart *ios_partition_seki;
struct IosMemPart *ios_partition_sugipon;
SObjSimpleSetting InitialSObjSimpleSetting;

/* every block the stubs hand the game, freed at exit (the game never frees
   an eye's record; LeakSanitizer in the asan job counts what is left) */
static void *s_blocks[1024];
static int s_blockCount;

static void freeBlocks(void)
{
    for (int i = 0; i < s_blockCount; i++) {
        free(s_blocks[i]);
    }
    s_blockCount = 0;
}

static void *trackedCalloc(size_t n)
{
    void *p = calloc(1, n);
    if (s_blockCount == 0) {
        atexit(freeBlocks);
    }
    if (p != NULL && s_blockCount < (int)(sizeof(s_blocks) / sizeof(s_blocks[0]))) {
        s_blocks[s_blockCount++] = p;
    }
    return p;
}

void *iosMallocDebug(IosMemPart *part, int size, const char *file, int line)
{
    (void)part, (void)file, (void)line;
    return trackedCalloc((size_t)size);
}

void *iosFree(void *ptr)
{
    for (int i = 0; i < s_blockCount; i++) {
        if (s_blocks[i] == ptr) {
            s_blocks[i] = s_blocks[--s_blockCount];
            break;
        }
    }
    free(ptr);
    return NULL;
}

struct Sub15C *CSVSYSTEM_InitDObj(int id, SObjSimpleSetting *lay)
{
    Sub15C *d = trackedCalloc(sizeof(Sub15C));

    (void)id, (void)lay;
    d->nodes = trackedCalloc(sizeof(struct DObjNode));
    d->nodeMtx = (ICO_WORD)trackedCalloc(64);
    return d;
}

void p2o_DispVU1DObj(void *req)
{
    (void)req;
}

void reg_DispMultiPri(Sub15C *o, int pri)
{
    (void)o, (void)pri;
}

void SetQuaternionByAxisRotateVWithNoRegularize(float *self, short ang, float *src)
{
    (void)self, (void)ang, (void)src;
}

void GetMatrixFromQuaternionPos(void *mtx, void *q, void *pos)
{
    (void)mtx, (void)q, (void)pos;
}

/* photo mode's camera, as photo_view.c reports it */
static int s_moved;

int ico_photo_view_moved(void)
{
    return s_moved;
}

/* the strip DispEnemyEye draws */
static int s_drawn[POINTS * 2][4];
static int s_drawnN;
static int s_drawnProj;
static int s_proj;

void gif_StartPacketPri(int pri)
{
    (void)pri;
}

void gif_SetAlpha(long long alpha, long long mode, long long fix)
{
    (void)alpha, (void)mode, (void)fix;
}

void gif_EndPacket(void) {}

void gif_Draw2DStripG(int *v, GifColor *col, int n, int prim)
{
    (void)col, (void)prim;
    s_drawnN = n;
    s_drawnProj = s_proj;
    if (n > 0 && n <= POINTS * 2) {
        memcpy(s_drawn, v, (size_t)n * 16);
    }
}

#ifdef ICO_RD
void gif_HostFrameProjected(int on)
{
    s_proj = on != 0;
}
#endif

/* --- the matrices and the point ------------------------------------------- */

static void setCamera(int moved)
{
    /* the view (+0x80): the game camera 500 units back; moved, turned
       about Y (cos 0.8, sin 0.6) and shifted */
    static const float view0[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 500, 1};
    static const float view1[16] = {0.8f, 0, -0.6f, 0, 0,  1,   0,   0,
                                    0.6f, 0, 0.8f,  0, 30, -10, 520, 1};
    /* the screen matrix (+0xC0): w = the view's z, x and y about 2048;
       moved, zoomed by 1.6 */
    static const float screen0[16] = {500, 0, 0, 0, 0, 500, 0, 0, 2048, 2048, 1, 1, 0, 0, 10, 0};
    static const float screen1[16] = {800, 0, 0, 0, 0, 800, 0, 0, 2048, 2048, 1, 1, 0, 0, 10, 0};

    memcpy(matrixptr + 0x80, moved ? view1 : view0, 64);
    memcpy(matrixptr + 0xC0, moved ? screen1 : screen0, 64);
}

/* the eye's point at tick k (UpdateEnemyEye's m translation row) */
static void pointAt(int k, float out[4])
{
    out[0] = 100.0f + 7.0f * (float)k;
    out[1] = 20.0f - 3.0f * (float)k + 0.5f * (float)(k * k);
    out[2] = 40.0f + 2.0f * (float)k;
    out[3] = 1.0f;
}

static void tickAt(EnemyEye *eye, int k)
{
    float m[4][4] __attribute__((aligned(16)));

    _UnitMatrix(m);
    pointAt(k, m[3]);
    UpdateEnemyEye(eye, m, 1.0f + 0.1f * (float)k); /* the width 3 x rate */
}

/* UpdatePointBlur's pair for the point at tick k joined to the point at
   tick j, written out from its source with the matrices in +0x80/+0xC0 */
static void expectPair(int k, int j, float rate, int *out)
{
    float a[4] __attribute__((aligned(16)));
    float b[4] __attribute__((aligned(16)));
    float c[4] __attribute__((aligned(16)));
    float s0[4] __attribute__((aligned(16)));
    float s1[4] __attribute__((aligned(16)));
    float w[4] __attribute__((aligned(16)));
    float cur[16] __attribute__((aligned(16)));
    float scale;

    _GetCurrentMatrix(cur);
    _SetCurrentMatrix(matrixptr + 0xC0);
    pointAt(j, w);
    _ApplyMatrix(a, matrixptr + 0x80, w);
    _RotTransPersCurrentMatrix(s1, a);
    pointAt(k, w);
    _ApplyMatrix(a, matrixptr + 0x80, w);
    _RotTransPersCurrentMatrix(s0, a);
    a[0] = a[0] + rate * 3.0f;
    _RotTransPersCurrentMatrix(b, a);
    scale = b[0] - s0[0];
    _SubVector(c, s1, s0);
    c[2] = 0.0f;
    _OuterProduct(c, c, ZUnitVector);
    _NormalizeVector(c, c);
    _ScaleVector(c, c, scale);
    c[2] = 0.0f;
    _AddVectorXYZ(b, s0, c);
    _FTOI4Vector(out, b);
    _SubVectorXYZ(b, s0, c);
    _FTOI4Vector(out + 4, b);
    _SetCurrentMatrix(cur);
}

static float rateAt(int k)
{
    return 1.0f + 0.1f * (float)k;
}

/* what DispEnemyEye draws with photo mode's camera moved or not */
static void draw(EnemyEye *eye, int moved)
{
    s_moved = moved;
    s_drawnN = -1;
    s_drawnProj = -1;
    DispEnemyEye(eye);
    s_moved = 0;
}

/* the disc's reset rule (UpdatePointBlur): every tick dirty or none */
static void setResetRule(int every)
{
    GlobalTimer = 0;
    currentScreenWidth = 0;
    NonLinearCameraMove = 0;
    if (every) {
        if (ICO_AUG6JUN26) {
            NonLinearCameraMove = 1;
        } else {
            GlobalTimer = 1;
        }
    }
}

int main(int argc, char **argv)
{
    const char *slug = argc > 1 ? argv[1] : "pal";
    static int game[POINTS * 2][4];
    static int want[POINTS * 2][4];
    float cur0[16] __attribute__((aligned(16)));
    float cur1[16] __attribute__((aligned(16)));
    unsigned rebuilt, kept;
    EnemyEye *eye;
    int bad;

    if (ico_region_select(slug) != 0) {
        fprintf(stderr, "FAIL: no disc \"%s\"\n", slug);
        return 1;
    }
    ico_eye_trail_reset();
    setResetRule(0);
    setCamera(0);

    /* 12 ticks of a moving point; the game's strip, then the rebuild with
       the same matrices */
    eye = InitEnemyEye(POINTS, 0, 10);
    for (int k = 1; k <= TICKS; k++) {
        tickAt(eye, k);
    }
    draw(eye, 0);
    CHECK(s_drawnN == POINTS * 2, "%s: the game's strip has %d vertices", slug, s_drawnN);
    CHECK(s_drawnProj <= 0, "%s: the game's strip is frame-projected in play", slug);
    memcpy(game, s_drawn, sizeof(game));
    draw(eye, 1);
    CHECK(s_drawnN == POINTS * 2, "%s: the rebuilt strip has %d vertices", slug, s_drawnN);
    CHECK(memcmp(s_drawn, game, sizeof(game)) == 0,
          "%s: the rebuild with the game's matrices is not the game's strip", slug);
    ico_eye_trail_counts(&rebuilt, &kept);
    CHECK(rebuilt == 1 && kept == 0, "%s: %u rebuilt, %u kept (want 1, 0)", slug, rebuilt, kept);

    /* the camera moved: each pair the point of its tick joined to the one
       before it, under the new matrices */
    setCamera(1);
    _GetCurrentMatrix(cur0);
    draw(eye, 1);
    _GetCurrentMatrix(cur1);
    for (int i = 0; i < POINTS; i++) {
        const int k = TICKS - i;
        expectPair(k, k - 1, rateAt(k), &want[i * 2][0]);
    }
    bad = -1;
    for (int i = POINTS - 1; i >= 0; i--) {
        if (memcmp(s_drawn[i * 2], want[i * 2], 32) != 0) {
            bad = i;
        }
    }
    CHECK(bad < 0, "%s: moved camera, pair %d is not the recomputation", slug, bad);
    CHECK(memcmp(s_drawn[(POINTS - 1) * 2], want[(POINTS - 1) * 2], 32) == 0,
          "%s: moved camera, the oldest point (tick %d joined to tick %d) is not exact", slug,
          TICKS - POINTS + 1, TICKS - POINTS);
    CHECK(memcmp(s_drawn, game, sizeof(game)) != 0, "%s: moved camera, the strip did not move",
          slug);
#ifdef ICO_RD
    CHECK(s_drawnProj == 1, "%s: moved camera, the rebuilt strip is not frame-projected", slug);
#endif
    CHECK(memcmp(cur0, cur1, sizeof(cur0)) == 0, "%s: the rebuild left the current matrix changed",
          slug);
    draw(eye, 0);
    CHECK(memcmp(s_drawn, game, sizeof(game)) == 0, "%s: the rebuild changed the game's strip",
          slug);

    /* the reset path: ResetEnemyEye, one tick (the copies of the newest
       pair, joined to the point two ticks back, where the game's
       screenPos[1] was left), then every tick dirty by the disc's rule */
    setCamera(0);
    ResetEnemyEye(eye);
    tickAt(eye, TICKS + 1);
    draw(eye, 0);
    memcpy(game, s_drawn, sizeof(game));
    for (int i = 1; i < POINTS; i++) {
        CHECK(memcmp(game[i * 2], game[0], 32) == 0, "%s: reset, pair %d not the newest", slug, i);
    }
    draw(eye, 1);
    CHECK(memcmp(s_drawn, game, sizeof(game)) == 0,
          "%s: reset, the rebuild with the game's matrices is not the game's strip", slug);
    setCamera(1);
    draw(eye, 1);
    expectPair(TICKS + 1, TICKS - 1, rateAt(TICKS + 1), &want[0][0]);
    bad = -1;
    for (int i = 0; i < POINTS; i++) {
        if (memcmp(s_drawn[i * 2], want[0], 32) != 0) {
            bad = i;
        }
    }
    CHECK(bad < 0, "%s: reset, moved camera, pair %d is not the recomputation", slug, bad);
    setCamera(0);
    setResetRule(1);
    for (int k = TICKS + 2; k <= TICKS + 4; k++) {
        tickAt(eye, k);
    }
    draw(eye, 0);
    memcpy(game, s_drawn, sizeof(game));
    draw(eye, 1);
    CHECK(memcmp(s_drawn, game, sizeof(game)) == 0,
          "%s: every tick reset, the rebuild is not the game's strip", slug);
    setCamera(1);
    draw(eye, 1);
    expectPair(TICKS + 4, TICKS + 3, rateAt(TICKS + 4), &want[0][0]);
    CHECK(memcmp(s_drawn[0], want[0], 32) == 0 &&
              memcmp(s_drawn[(POINTS - 1) * 2], want[0], 32) == 0,
          "%s: every tick reset, moved camera, not the recomputation", slug);
    setCamera(0);
    setResetRule(0);

    /* the first update after the init: the game's strip */
    {
        EnemyEye *fresh = InitEnemyEye(POINTS, 0, 10);

        tickAt(fresh, 1);
        draw(fresh, 0);
        memcpy(game, s_drawn, sizeof(game));
        unsigned kept0, kept1;

        setCamera(1);
        ico_eye_trail_counts(NULL, &kept0);
        draw(fresh, 1);
        CHECK(memcmp(s_drawn, game, sizeof(game)) == 0,
              "%s: the first update, not the game's strip", slug);
        CHECK(s_drawnProj <= 0, "%s: the first update, the game's strip frame-projected", slug);
        ico_eye_trail_counts(NULL, &kept1);
        CHECK(kept1 == kept0 + 1, "%s: the first update not counted as kept", slug);
        setCamera(0);
    }

    /* the side table forgotten (a state load): the game's strip */
    tickAt(eye, TICKS + 5);
    draw(eye, 0);
    memcpy(game, s_drawn, sizeof(game));
    ico_eye_trail_reset();
    setCamera(1);
    draw(eye, 1);
    CHECK(memcmp(s_drawn, game, sizeof(game)) == 0, "%s: no trail, not the game's strip", slug);
    ico_eye_trail_counts(&rebuilt, &kept);
    CHECK(rebuilt == 0 && kept == 1, "%s: no trail, %u rebuilt, %u kept (want 0, 1)", slug, rebuilt,
          kept);

    /* the session's end */
    ico_eye_trail_photo_end();
    ico_eye_trail_counts(&rebuilt, &kept);
    CHECK(rebuilt == 0 && kept == 0, "%s: the counts after the session's end", slug);

    {
        char name[64];
        snprintf(name, sizeof(name), "eye_trail %s", slug);
        return ico_check_summary(name);
    }
}
