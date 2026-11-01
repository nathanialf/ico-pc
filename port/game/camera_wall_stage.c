/*
 * port/game/camera_wall_stage.c
 *
 * The live check of camera_wall.h: the rule (camera_wall.c) at every flip,
 * over the stage's collision.  Built with the game's include path and record
 * layout, in the window and the headless build.
 *
 * The objects are the ones fieldCollision.c MakeCollisionDependGObjList puts
 * in the actors' collision list (active, a stage layout object, displayed,
 * with collision data), taken from the live object table at the flip
 * (gobj.c isysGObjGetExist_begin / _next) rather than from that list, which
 * Main built at the start of the tick and which can name an object the tick
 * has freed since (ExecDelayFree runs after the draw).  Each object's walls
 * and floors are read in its node's frame as _Clip reads them (the node
 * matrix, or its translation alone without colRotate; one set per node with
 * colPerNode), through the 32 x 32 block grids around the eye's path; walls
 * with the attributes ClipWall's mode (_clipW) skips are skipped.  The walk
 * only reads: no global of fieldCollision.c (colObjNum, curFuzio, the block
 * table, clipMatrix, clipPlanePos) and nothing else of the game's is
 * written, and every record is checked to lie in the arena before it is
 * read.
 */
#include <math.h>
#include <stddef.h>

#include "typedef.h"
#include "eeword.h"
#include "fieldCollision.h"
#include "gobj.h"

#include "camera_wall.h"
#include "diag_host.h"
#include "photo_mode.h"
#include "video_options.h"

/* the block grid: 32 x 32 cells of 512 units from the set's origin
   (fieldCollision.c makeCollisionBlockTable) */
#define CW_GRID 32
#define CW_CELL 512.0f
/* a block's index list is ended by a negative index; no list is longer than
   its set's elements */
#define CW_LIST_MAX 4096

static IcoCameraWall s_rule;
static unsigned s_count;

/* p in the node's frame: the node matrix's inverse (its 3 x 3 is a
   rotation; fieldCollision.c FC_SET_TRANSPOSE), or the translation alone */
static void toNode(float out[3], const float *m, int rotate, const float p[3])
{
    const double d[3] = {(double)p[0] - m[12], (double)p[1] - m[13], (double)p[2] - m[14]};
    int r;

    for (r = 0; r < 3; r++) {
        out[r] = rotate ? (float)(m[r * 4] * d[0] + m[r * 4 + 1] * d[1] + m[r * 4 + 2] * d[2])
                        : (float)d[r];
    }
}

/* the block cells [lo, hi] along one axis that a span from s0 to s1,
   widened by r, touches; 0 when none is in the grid */
static int cellSpan(float s0, float s1, float r, float origin, int *lo, int *hi)
{
    const float a = (s0 < s1 ? s0 : s1) - r - origin;
    const float b = (s0 < s1 ? s1 : s0) + r - origin;
    int l, h;

    if (!isfinite(a) || !isfinite(b) || b < 0.0f || a >= CW_GRID * CW_CELL) {
        return 0;
    }
    l = a < 0.0f ? 0 : (int)floorf(a / CW_CELL);
    h = (int)floorf(b / CW_CELL);
    *lo = l;
    *hi = h >= CW_GRID ? CW_GRID - 1 : h;
    return *lo <= *hi;
}

/* a block's index list, NULL for an empty cell or one outside the arena */
static const short *blockList(const int *grid, int cell)
{
    const short *p = ICO_EEPTR(const short *, grid[cell]);

    return p && ico_arena_contains(p, sizeof(short)) ? p : NULL;
}

static int wallsCrossed(const FcColl *c, const float a[3], const float b[3], float r, int x0,
                        int x1, int z0, int z1)
{
    const FcWallEnt *walls = ICO_EEPTR(const FcWallEnt *, c->wcl);
    const int *grid = ICO_EEPTR(const int *, c->wblk);
    int x, z, k;

    if (c->count <= 0 || !walls || !grid ||
        !ico_arena_contains(walls, (size_t)c->count * sizeof(FcWallEnt)) ||
        !ico_arena_contains(grid, CW_GRID * CW_GRID * sizeof(int))) {
        return 0;
    }
    for (z = z0; z <= z1; z++) {
        for (x = x0; x <= x1; x++) {
            const short *p = blockList(grid, (z << 5) + x);
            for (k = 0; p && k < CW_LIST_MAX && ico_arena_contains(p + k, sizeof(short)); k++) {
                const int i = p[k];
                const FcWallEnt *e;
                const float *n;
                IcoCameraWallQuad q;

                if (i < 0 || i >= c->count) {
                    break;
                }
                e = &walls[i];
                /* ClipWall's mode: _clipW tests a wall whose attribute has no
                   top nibble and whose 0xF0000 nibble is not 1 */
                if ((e->attr & 0xF0000000) != 0 || (e->attr & 0xF0000) == 0x10000) {
                    continue;
                }
                n = FC_WALL_NORMAL(e);
                if (!n || !ico_arena_contains(n, 2 * sizeof(float))) {
                    continue;
                }
                q.x = e->pt[0][0];
                q.z = e->pt[0][2];
                q.nx = n[0];
                q.nz = n[1];
                q.length = e->height;
                q.bottom[0] = e->pt[0][1];
                q.bottom[1] = e->pt[1][1];
                q.top[0] = e->pt[2][1];
                q.top[1] = e->pt[3][1];
                if (ico_camera_wall_cross_wall(a, b, &q, r)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int floorsCrossed(const FcColl *c, const float a[3], const float b[3], float r, int x0,
                         int x1, int z0, int z1)
{
    const FcFloorEnt *floors = ICO_EEPTR(const FcFloorEnt *, c->fcl);
    const int *grid = ICO_EEPTR(const int *, c->fblk);
    int x, z, k, v;

    if (c->nfloor <= 0 || !floors || !grid ||
        !ico_arena_contains(floors, (size_t)c->nfloor * sizeof(FcFloorEnt)) ||
        !ico_arena_contains(grid, CW_GRID * CW_GRID * sizeof(int))) {
        return 0;
    }
    for (z = z0; z <= z1; z++) {
        for (x = x0; x <= x1; x++) {
            const short *p = blockList(grid, (z << 5) + x);
            for (k = 0; p && k < CW_LIST_MAX && ico_arena_contains(p + k, sizeof(short)); k++) {
                const int i = p[k];
                const FcFloorEnt *e;
                IcoCameraWallFloor f;

                if (i < 0 || i >= c->nfloor) {
                    break;
                }
                e = &floors[i];
                f.n[0] = e->nx;
                f.n[1] = e->ny;
                f.n[2] = e->nz;
                f.d = e->d;
                /* FloorPointInside walks v[0 .. nex + 2]; the record holds 4 */
                f.count = e->nex < 0 ? 3 : (e->nex > 1 ? 4 : e->nex + 3);
                for (v = 0; v < f.count; v++) {
                    f.x[v] = e->v[v].x;
                    f.z[v] = e->v[v].z;
                }
                if (ico_camera_wall_cross_floor(a, b, &f, r)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* the rule's query: the world segment a -> b against every collision
   object's walls, then its floors */
static int stageQuery(const float a[3], const float b[3], float r, void *user)
{
    GObj *g;

    (void)user;
    for (g = isysGObjGetExist_begin(); g != 0; g = isysGObjGetExist_next(g)) {
        const Sub15C *sub = GOBJ_SUB(g);
        const FcColl *c;
        int cnt, i;

        /* MakeCollisionDependGObjList's filter */
        if (sub == 0 || !ico_arena_contains(sub, sizeof(*sub)) || sub->colData == 0 ||
            g->active == 0 || g->labelType != 1 || g->labelId < 0 || sub->disp == 0) {
            continue;
        }
        c = (const FcColl *)sub->colData;
        if (!ico_arena_contains(c, sizeof(*c))) {
            continue;
        }
        cnt = sub->colPerNode != 0 ? sub->nodeNum : 1;
        for (i = 0; i < cnt; i++) {
            const float *m = (const float *)((const char *)sub->nodeMtx + (i << 6));
            const float *ofs = ICO_EEPTR(const float *, c->ofs);
            float la[3], lb[3];
            int x0, x1, z0, z1;

            if (!ico_arena_contains(m, 16 * sizeof(float)) || !ofs ||
                !ico_arena_contains(ofs, 3 * sizeof(float))) {
                break;
            }
            toNode(la, m, sub->colRotate != 0, a);
            toNode(lb, m, sub->colRotate != 0, b);
            if (!cellSpan(la[0], lb[0], r, ofs[0], &x0, &x1) ||
                !cellSpan(la[2], lb[2], r, ofs[2], &z0, &z1)) {
                continue;
            }
            if (wallsCrossed(c, la, lb, r, x0, x1, z0, z1)) {
                return ICO_CAMERA_WALL_WALL;
            }
            if (floorsCrossed(c, la, lb, r, x0, x1, z0, z1)) {
                return ICO_CAMERA_WALL_FLOOR;
            }
        }
    }
    return ICO_CAMERA_WALL_NONE;
}

int ico_camera_wall_check(const float *eyeNow, int cutPending, int stage)
{
    float moved = 0.0f;
    const int kind = ico_camera_wall_step(&s_rule, eyeNow, cutPending, ico_photo_active(),
                                          stageQuery, NULL, &moved);

    if (kind == ICO_CAMERA_WALL_NONE) {
        return kind;
    }
    s_count++;
    if (ico_video_interpolate()) {
        ico_log_limited("camera: blend skipped",
                        "camera: between-tick blend skipped at stage %d: the eye moved %.0f cm "
                        "through a %s",
                        stage, (double)moved, kind == ICO_CAMERA_WALL_WALL ? "wall" : "floor");
    }
    return kind;
}

void ico_camera_wall_forget(void)
{
    s_rule.valid = 0;
}

unsigned ico_camera_wall_count(void)
{
    return s_count;
}
