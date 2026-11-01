/*
 * port/game/test/camera_wall_stage_test.c
 *
 * The live wall check (port/game/camera_wall_stage.c) over a synthetic stage
 * built in a stand-in arena with the game's records (typedef.h GObj and
 * Sub15C, fieldCollision.h FcColl, FcWallEnt, FcFloorEnt, EE address words),
 * the object table's walk (gobj.c isysGObjGetExist_begin / _next), photo
 * mode, the Frame rate rule and the log helper stubbed below:
 *   wall      an object placed by its node's translation (no colRotate)
 *             with one wall in one block cell: the eye's path through it is
 *             a wall, either way; beside it none
 *   floor     an object whose node turns 90 degrees about y (colRotate): the
 *             path down through its floor in the node's frame is a floor,
 *             the same world path read without the turn would miss it
 *   nodes     colPerNode with two nodes: a wall of node 1 (the matrix one
 *             64-byte record on) is found
 *   filter    MakeCollisionDependGObjList's: an inactive object, one not a
 *             stage layout object (labelType), one with labelId -1, one not
 *             displayed, one without collision data: none; walls with the
 *             attributes ClipWall's mode skips (top nibble, 0xF0000 nibble
 *             1): none, another nibble: a wall
 *   arena     a record outside the arena (the collision data, a block list)
 *             is not read: none, no crash
 *   readonly  every byte of the arena and of the object table is the same
 *             after all the checks as before them (the check writes nothing
 *             of the game's)
 *   live      ico_camera_wall_check: the first flip none, the crossing a
 *             wall with the log line "camera: between-tick blend skipped at
 *             stage 21: the eye moved 40 cm through a wall"; no line at
 *             the Original frame rate (still counted); a pending cut and
 *             photo mode none; ico_camera_wall_forget makes a first flip
 * CPU only.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "typedef.h"
#include "eeword.h"
#include "fieldCollision.h"
#include "gobj.h"

#include "../../test/ico_check.h"
#include "camera_wall.h"
#include "diag_host.h"

/* ------------------------------------------------------------- the arena */
static unsigned char s_arena[1 << 20] __attribute__((aligned(16)));
static size_t s_arenaTop = 64;
unsigned char *ico_arena_cached_base = s_arena;

unsigned char *ico_arena_base(void)
{
    return s_arena;
}

int ico_arena_contains(const void *p, __SIZE_TYPE__ n)
{
    const unsigned char *c = (const unsigned char *)p;
    return c >= s_arena && c + n <= s_arena + sizeof(s_arena);
}

static void *arenaAlloc(size_t n)
{
    void *p = &s_arena[s_arenaTop];
    s_arenaTop = (s_arenaTop + n + 15) & ~(size_t)15;
    memset(p, 0, n);
    return p;
}

static unsigned eeWord(const void *p)
{
    return p ? (unsigned)((const unsigned char *)p - s_arena) : 0u;
}

/* ------------------------------------------------------ the object table */
#define OBJS 4
static GObj s_obj[OBJS];

void *isysGObjGetExist_begin(void)
{
    for (int i = 0; i < OBJS; i++) {
        if (s_obj[i].self != 0) {
            return &s_obj[i];
        }
    }
    return 0;
}

void *isysGObjGetExist_next(GObj *start)
{
    for (int i = (int)(start - s_obj) + 1; i < OBJS; i++) {
        if (s_obj[i].self != 0) {
            return &s_obj[i];
        }
    }
    return 0;
}

/* ------------------------------------------------------ the port's side */
static int s_photo, s_interp = 1;
static char s_line[256];
static int s_lines;

int ico_photo_active(void)
{
    return s_photo;
}

int ico_video_interpolate(void)
{
    return s_interp;
}

void ico_log_limit_write(IcoLogLimit *l, const char *fmt, ...)
{
    va_list ap;

    (void)l;
    va_start(ap, fmt);
    vsnprintf(s_line, sizeof(s_line), fmt, ap);
    va_end(ap);
    s_lines++;
}

/* --------------------------------------------------------------- a stage */
/* the block grids' origin: the node frame's 0 is cell 16 */
static const float kOrigin = -8192.0f;

static int cellOf(float x, float z)
{
    return ((int)floorf((z - kOrigin) / 512.0f) << 5) + (int)floorf((x - kOrigin) / 512.0f);
}

/* one collision set: `walls` walls and `floors` floors, each listed in the
   block cell of its first corner */
static FcColl *makeSet(const FcWallEnt *walls, int nw, const FcFloorEnt *floors, int nf)
{
    FcColl *c = arenaAlloc(sizeof(FcColl));
    FcWallEnt *w = nw ? arenaAlloc(sizeof(FcWallEnt) * (size_t)nw) : NULL;
    FcFloorEnt *f = nf ? arenaAlloc(sizeof(FcFloorEnt) * (size_t)nf) : NULL;
    int *wblk = arenaAlloc(1024 * sizeof(int));
    int *fblk = arenaAlloc(1024 * sizeof(int));
    float *ofs = arenaAlloc(4 * sizeof(float));

    ofs[0] = kOrigin;
    ofs[2] = kOrigin;
    for (int i = 0; i < nw; i++) {
        float *n = arenaAlloc(4 * sizeof(float));
        short *list = arenaAlloc(2 * sizeof(short));
        w[i] = walls[i];
        n[0] = walls[i].pt[3][3]; /* the normal rides in the template's pad word */
        n[1] = walls[i].pt[2][3];
        w[i].pt[3][3] = w[i].pt[2][3] = 0.0f;
        w[i].normal = eeWord(n);
        list[0] = (short)i;
        list[1] = -1;
        wblk[cellOf(walls[i].pt[0][0], walls[i].pt[0][2])] = (int)eeWord(list);
    }
    for (int i = 0; i < nf; i++) {
        short *list = arenaAlloc(2 * sizeof(short));
        f[i] = floors[i];
        list[0] = (short)i;
        list[1] = -1;
        fblk[cellOf(floors[i].v[0].x, floors[i].v[0].z)] = (int)eeWord(list);
    }
    c->count = nw;
    c->nfloor = nf;
    c->wcl = eeWord(w);
    c->fcl = eeWord(f);
    c->wblk = eeWord(wblk);
    c->fblk = eeWord(fblk);
    c->ofs = eeWord(ofs);
    return c;
}

/* camera_wall_test's wall: z = 0 facing +z, x 0 .. 100, y 0 .. 50 */
static FcWallEnt wallZ0(int attr)
{
    FcWallEnt w;

    memset(&w, 0, sizeof(w));
    w.pt[1][0] = 100.0f;
    w.pt[2][1] = 50.0f;
    w.pt[3][0] = 100.0f;
    w.pt[3][1] = 50.0f;
    w.height = 100.0f;
    w.attr = attr;
    w.pt[3][3] = 0.0f; /* nx */
    w.pt[2][3] = 1.0f; /* nz */
    return w;
}

/* a floor at y = 10 facing up over x, z 0 .. 100 */
static FcFloorEnt floorY10(void)
{
    FcFloorEnt f;

    memset(&f, 0, sizeof(f));
    f.v[1].x = 100.0f;
    f.v[2].x = 100.0f;
    f.v[2].z = 100.0f;
    f.v[3].z = 100.0f;
    f.ny = 1.0f;
    f.d = -10.0f;
    f.nex = 1;
    return f;
}

/* object i: a stage layout object with `nodes` node matrices (the first
   translated by t and turned about y by yaw degrees) and the set c */
static Sub15C *makeObject(int i, FcColl *c, const float t[3], float yaw, int rotate, int nodes)
{
    Sub15C *sub = arenaAlloc(sizeof(Sub15C));
    float *m = arenaAlloc(64 * (size_t)nodes);
    const float r = yaw * 3.14159265f / 180.0f;

    for (int k = 0; k < nodes; k++) {
        float *n = m + 16 * k;
        n[0] = cosf(r);
        n[2] = -sinf(r);
        n[5] = 1.0f;
        n[8] = sinf(r);
        n[10] = cosf(r);
        n[12] = t[0];
        n[13] = t[1];
        n[14] = t[2];
        n[15] = 1.0f;
    }
    sub->nodeNum = nodes;
    sub->nodeMtx = (ICO_WORD)m;
    sub->colData = (ICO_WORD)c;
    sub->disp = 1;
    sub->colRotate = rotate;
    sub->colPerNode = nodes > 1;
    memset(&s_obj[i], 0, sizeof(s_obj[i]));
    s_obj[i].self = &s_obj[i];
    s_obj[i].labelType = 1;
    s_obj[i].labelId = i;
    s_obj[i].active = 1;
    s_obj[i].dobj = sub;
    return sub;
}

/* the check's query, through the rule: the first flip at a, the second at b */
static int crossed(const float a[3], const float b[3])
{
    ico_camera_wall_forget();
    ico_camera_wall_check(a, 0, 21);
    return ico_camera_wall_check(b, 0, 21);
}

static unsigned char s_arenaBefore[sizeof(s_arena)];
static GObj s_objBefore[OBJS];

int main(void)
{
    const FcWallEnt plain = wallZ0(0);
    const FcFloorEnt flat = floorY10();
    const float tA[3] = {1000.0f, 0.0f, 2000.0f};
    const float tB[3] = {0.0f, 100.0f, 0.0f};
    const float tC[3] = {5000.0f, 0.0f, 5000.0f};
    const float front[3] = {1050.0f, 25.0f, 2020.0f};
    const float back[3] = {1050.0f, 25.0f, 1980.0f};
    Sub15C *a;
    Sub15C *c;

    /* the wall object, alone */
    a = makeObject(0, makeSet(&plain, 1, NULL, 0), tA, 0.0f, 0, 1);
    /* the live check first: the log helper writes the first 8 lines of its key */
    memcpy(s_arenaBefore, s_arena, sizeof(s_arena));
    memcpy(s_objBefore, s_obj, sizeof(s_obj));
    {
        const unsigned count = ico_camera_wall_count();
        ico_camera_wall_forget();
        s_lines = 0;
        CHECK(ico_camera_wall_check(front, 0, 21) == ICO_CAMERA_WALL_NONE, "live: the first flip");
        CHECK(ico_camera_wall_check(back, 0, 21) == ICO_CAMERA_WALL_WALL, "live: the crossing");
        CHECK(s_lines == 1, "live: one line (%d)", s_lines);
        CHECK_STR(s_line, "camera: between-tick blend skipped at stage 21: the eye moved 40 cm "
                          "through a wall");
        s_interp = 0;
        CHECK(ico_camera_wall_check(front, 0, 21) == ICO_CAMERA_WALL_WALL &&
                  ico_camera_wall_count() == count + 2 && s_lines == 1,
              "live: at the Original frame rate counted, no line");
        s_interp = 1;
        CHECK(ico_camera_wall_check(back, 1, 21) == ICO_CAMERA_WALL_NONE, "live: a pending cut");
        s_photo = 1;
        CHECK(ico_camera_wall_check(front, 0, 21) == ICO_CAMERA_WALL_NONE, "live: photo mode");
        s_photo = 0;
        CHECK(ico_camera_wall_check(back, 0, 21) == ICO_CAMERA_WALL_WALL,
              "live: after photo mode the next step is tested");
        ico_camera_wall_forget();
        CHECK(ico_camera_wall_check(front, 0, 21) == ICO_CAMERA_WALL_NONE,
              "live: forgotten, a first flip");
    }
    CHECK(memcmp(s_arenaBefore, s_arena, sizeof(s_arena)) == 0 &&
              memcmp(s_objBefore, s_obj, sizeof(s_obj)) == 0,
          "readonly: the arena and the object table unchanged by the live checks");
    memcpy(s_arenaBefore, s_arena, sizeof(s_arena));
    memcpy(s_objBefore, s_obj, sizeof(s_obj));
    CHECK(crossed(front, back) == ICO_CAMERA_WALL_WALL, "wall: front to back");
    CHECK(crossed(back, front) == ICO_CAMERA_WALL_WALL, "wall: back to front");
    {
        const float b0[3] = {1200.0f, 25.0f, 2020.0f}, b1[3] = {1200.0f, 25.0f, 1980.0f};
        CHECK(crossed(b0, b1) == ICO_CAMERA_WALL_NONE, "wall: beside it");
    }
    CHECK(memcmp(s_arenaBefore, s_arena, sizeof(s_arena)) == 0 &&
              memcmp(s_objBefore, s_obj, sizeof(s_obj)) == 0,
          "readonly: the arena and the object table unchanged by the wall checks");

    /* the filter */
    s_obj[0].active = 0;
    CHECK(crossed(front, back) == ICO_CAMERA_WALL_NONE, "filter: inactive");
    s_obj[0].active = 1;
    s_obj[0].labelType = 2;
    CHECK(crossed(front, back) == ICO_CAMERA_WALL_NONE, "filter: not a stage layout object");
    s_obj[0].labelType = 1;
    s_obj[0].labelId = -1;
    CHECK(crossed(front, back) == ICO_CAMERA_WALL_NONE, "filter: labelId -1");
    s_obj[0].labelId = 0;
    a->disp = 0;
    CHECK(crossed(front, back) == ICO_CAMERA_WALL_NONE, "filter: not displayed");
    a->disp = 1;
    {
        const ICO_WORD keep = a->colData;
        static FcColl outside;
        a->colData = 0;
        CHECK(crossed(front, back) == ICO_CAMERA_WALL_NONE, "filter: no collision data");
        outside = *(const FcColl *)keep;
        a->colData = (ICO_WORD)&outside;
        CHECK(crossed(front, back) == ICO_CAMERA_WALL_NONE, "arena: collision data outside it");
        a->colData = keep;
    }
    CHECK(crossed(front, back) == ICO_CAMERA_WALL_WALL, "filter: back on");
    {
        FcColl *set = (FcColl *)a->colData;
        FcWallEnt *w = ICO_EEPTR(FcWallEnt *, set->wcl);
        int *grid = ICO_EEPTR(int *, set->wblk);
        const int cell = cellOf(0.0f, 0.0f);
        const int list = grid[cell];
        w->attr = 0x10000000;
        CHECK(crossed(front, back) == ICO_CAMERA_WALL_NONE, "filter: a top-nibble attribute");
        w->attr = 0x10000;
        CHECK(crossed(front, back) == ICO_CAMERA_WALL_NONE, "filter: the 0xF0000 nibble 1");
        w->attr = 0x20000;
        CHECK(crossed(front, back) == ICO_CAMERA_WALL_WALL, "filter: the 0xF0000 nibble 2");
        w->attr = 0;
        grid[cell] = (int)(sizeof(s_arena) + 64u);
        CHECK(crossed(front, back) == ICO_CAMERA_WALL_NONE, "arena: a block list outside it");
        grid[cell] = list;
    }

    /* the turned floor object, the wall object off */
    s_obj[0].active = 0;
    makeObject(1, makeSet(NULL, 0, &flat, 1), tB, 90.0f, 1, 1);
    {
        /* node frame (50, 30, 20) -> (50, -10, 20); a 90 degree turn about
           y has columns (0, 0, -1), (0, 1, 0), (1, 0, 0): the frame's x is
           minus the world's z, its z the world's x */
        const float up[3] = {20.0f, 130.0f, -50.0f}, down[3] = {20.0f, 90.0f, -50.0f};
        const float noTurn[3] = {50.0f, 130.0f, 20.0f}, noTurnDown[3] = {50.0f, 90.0f, 20.0f};
        CHECK(crossed(up, down) == ICO_CAMERA_WALL_FLOOR, "floor: down through it");
        CHECK(crossed(down, up) == ICO_CAMERA_WALL_FLOOR, "floor: up through it");
        CHECK(crossed(noTurn, noTurnDown) == ICO_CAMERA_WALL_NONE,
              "floor: the frame's own coordinates as world ones miss it (the turn is applied)");
    }
    s_obj[1].active = 0;

    /* two nodes: the second one's wall */
    c = makeObject(2, makeSet(&plain, 1, NULL, 0), tA, 0.0f, 0, 2);
    {
        float *m1 = (float *)c->nodeMtx + 16;
        const float f1[3] = {5050.0f, 25.0f, 5020.0f}, b1[3] = {5050.0f, 25.0f, 4980.0f};
        m1[12] = tC[0];
        m1[13] = tC[1];
        m1[14] = tC[2];
        CHECK(crossed(f1, b1) == ICO_CAMERA_WALL_WALL, "nodes: node 1's wall");
        CHECK(crossed(front, back) == ICO_CAMERA_WALL_WALL, "nodes: node 0's wall");
    }
    s_obj[2].active = 0;
    s_obj[0].active = 1;

    return ico_check_summary("camera_wall_stage_test");
}
