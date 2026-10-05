#include "typedef.h"
#include "fieldCollision.h"
#include "gobj.h"
#include "debug.h"
#include "debug_exception.h"
#include "FileManager.h"
#include "lineManager.h"
#include "matrixDrive.h"
#include "tableSin.h"
#include "sugiCommon.h"
#include <stdio.h>
#include <string.h>
#include "GsBase.h"
#include <eeregs.h>
#include "main.h"
#include "Matrix.h"
#include <libvu0.h>
#include "GifPacket.h"
#include <assert.h>

/* The collision file's head (fieldCollision.h's FcColl).  The EE reads its
   relocated words through this typed view; the host keeps them as EE
   address words (eeword.h) and reads them through the FUZIO_ accessors. */
#ifdef ICO_HOST

typedef FcColl FuzioCtx; /* derived name */

#define FUZIO_WALLS(c) ICO_EEPTR(FcWallEnt *, (c)->wcl)
#define FUZIO_FLOORS(c) ICO_EEPTR(FcFloorEnt *, (c)->fcl)
#define FUZIO_WBLK(c, i) ICO_EEPTR(short *, ICO_EEPTR(int *, (c)->wblk)[i])
#define FUZIO_FBLK(c, i) ICO_EEPTR(short *, ICO_EEPTR(int *, (c)->fblk)[i])
#define FUZIO_OFS(c) ICO_EEPTR(float *, (c)->ofs)
#else

typedef struct { /* field names derived */
    char pad0[16];
    FcWallEnt *walls; /* 0x10 */
    FcFloorEnt *fcl;  /* 0x14, the floor list (charFileManager.c's names) */
    short **wblk;     /* 0x18, per block, the walls' indices, ended by a negative one */
    short **fblk;     /* 0x1C, per block, the floors' indices */
    float *ofs;       /* 0x20, the origin the blocks are counted from */
} FuzioCtx;           /* derived name */

#define FUZIO_WALLS(c) ((c)->walls)
#define FUZIO_FLOORS(c) ((c)->fcl)
#define FUZIO_WBLK(c, i) ((c)->wblk[i])
#define FUZIO_FBLK(c, i) ((c)->fblk[i])
#define FUZIO_OFS(c) ((c)->ofs)
#endif

/* One line colour of the collision display: red, green, blue, alpha. */
typedef struct { /* field names derived */
    int rgba[4];
} FcColor; /* derived name */

typedef int (*FcFunc)(void *work, int mode);

/* fieldCollision.o's .sbss and .bss.  .sbss: the number of objects in the
   collision list, the nine collision statistics DispCollisionPC prints (a
   pair per format, wall, wall R, floor, floor R, with the timer between the
   two halves; the retail build only resets them), the number of block-table
   entries, the fuzio context the current object carries, the filter the list
   builder asks, and the number of exit-attribute slots in use. */
static int colObjNum; /* derived name */

static int pcWall0; /* derived name */

static int pcWallR0; /* derived name */

static int pcFloor0; /* derived name */

static int pcFloorR0; /* derived name */

static int pcTime; /* derived name */

static int pcWall1; /* derived name */

static int pcWallR1; /* derived name */

static int pcFloor1; /* derived name */

static int pcFloorR1; /* derived name */

static int blockNum; /* derived name */

static FuzioCtx *curFuzio; /* derived name */

static int (*colFilter)(void *obj);

static int exitAttrNum; /* derived name */

/* .bss: DispCollisionPC's line buffer, the collision object list, the block
   table and the exit-attribute slots. */
static char pcLine[256]; /* derived name */

static void *colObjList[256]; /* derived name */

static short blockTable[64]; /* derived name */

static void *exitAttr[16]; /* derived name */

/* .data: InitialColInfo here, the debug label table in
   MakeCollisionDependGObjList's disabled block, then the clip mode table, the
   clip work matrix and the plane point after the clip functions and the wall
   line colours with the unused unit matrix before DrawGObjWallCollision.
   .sdata, in emission order: the four variables here, the assert text "e",
   the three labels, the two clip function pointers and the wall draw count,
   then GetEdgeOfFloor's three strings.  No code reads the word before
   collision_pick. */
WallCfg InitialColInfo = {{0, -1}, 0};

static int fcReserved = 0; /* derived name */

int collision_pick = 0;

ObjNode InitialObjPointer = {0, -1};

static int colObjListNum = 0; /* derived name */

void MakeCollisionDependGObjList(void)
{
    GObj *g;
    Sub15C *sub;

    colObjListNum = 0;
    for (g = isysGObjGetExist_begin(); g != 0; g = isysGObjGetExist_next(g)) {
        sub = GOBJ_SUB(g);
        if ((char *)sub != 0 && sub->colData != 0 && g->active != 0 && g->labelType == 1 &&
            g->labelId >= 0 && sub->disp != 0) {
            colObjList[colObjListNum] = g;
            colObjListNum = colObjListNum + 1;
        }
    }
    if (colObjListNum >= 0x100) {
        debug_assertMessage(__FILE__, 533, "TOO MANY COLLISION DEPEND GOBJS\n");
        __assert(__FILE__, 533, "e");
    }
    /* a debug dump of the list, compiled out of the retail build; its label
     * table and format stay in the data */
    if (0) {
        static char *label[8] = {"GOBJ: ", " MAT: ", " COL: "};
        char buf[48];
        int i;

        for (i = 0; i < colObjListNum; i++) {
            debug_StdPrintfDummy("%s%d(%d)\n", buf, i, colObjListNum);
        }
    }
}

void GetReflectionElement(ClipWork *work, float arg0, float arg1)
{
    float buf0[4];
    float L10[4];
    float L20[4];
    float z;

    CopyVector(L10, &work->normal);
    *(int *)&L10[3] = 0;
    sceVu0SubVector(buf0, work->pt[1], work->pt[0]);
    sceVu0ScaleVector(work->reflect.bounce, L10, -GetDistanceFromPlane(L10, buf0));
    sceVu0AddVector(work->reflect.slide, buf0, work->reflect.bounce);
    sceVu0ScaleVectorXYZ(work->reflect.bounce, work->reflect.bounce, arg1);
    sceVu0ScaleVectorXYZ(work->reflect.slide, work->reflect.slide, arg0);
    sceVu0AddVector(work->reflect.dir, work->reflect.slide, work->reflect.bounce);
    {
        float *p20 = L20;
        z = GetPointDistance(work->pt[2], work->pt[1]);
        sceVu0ScaleVector(p20, work->reflect.dir, z / GetPointDistance(work->pt[0], work->pt[1]));
        sceVu0AddVector(work->reflect.pos, work->pt[2], p20);
    }
}

inline void SetSimplePlane(float *self, float a, float b, float c, float d)
{
    self[0] = a;
    self[1] = b;
    self[2] = c;
    self[3] = d;
}

/* the absolute value clip_wall_1 inlines five times */
static __inline__ float FcAbsF(float v) /* derived name */
{
    if (v < 0.0f) {
        v = -v;
    }
    return v;
}

static int clip_wall_1(ClipWork *ray, FcWallEnt *wall, int flip, int useh)
{
    FcWallEnt *e;
    float pa[4];
    float pb[4];
    float pc[4];
    float d[4];
    float out[4];
    float h;
    float lo;
    float hi;
    float nx;
    float nz;
    float mx;
    float ex;
    float ez;
    float ds;
    float hh;
    float t1;
    float t2;
    float sz;
    float *n;
    int far;

    h = 0.0f;
    if (useh) {
        h = ray->radius;
    }
    lo = -h;
    hi = wall->height + h;
    n = FC_WALL_NORMAL(wall);
    nx = n[0];
    nz = n[1];
    mx = -nx;

    sceVu0CopyVector((int *)out, (int *)ray->pt[2]);

    d[0] = out[0] - wall->pt[0][0];
    d[1] = out[1];
    d[2] = out[2] - wall->pt[0][2];
    pb[2] = d[0] * nx + d[2] * nz;
    if (flip) {
        pb[2] = -pb[2];
    }
    if (ray->radius < pb[2]) {
        return 0;
    }
    pb[0] = d[0] * nz - d[2] * nx;
    pb[1] = d[1];
    /* the start point reads the wall through a pointer of its own */
    e = wall;
    d[0] = ray->pt[0][0] - e->pt[0][0];
    d[1] = ray->pt[0][1];
    d[2] = ray->pt[0][2] - e->pt[0][2];
    /* the wall origin is taken after the subtraction that reads it */
    ex = e->pt[0][0];
    ez = e->pt[0][2];
    pa[2] = d[0] * nx + d[2] * nz;
    if (flip) {
        pa[2] = -pa[2];
    }
    ds = pa[2];
    if (ds <= 0.0f) {
        return 0;
    }
    pa[0] = d[0] * nz - d[2] * nx;
    pa[1] = d[1];
    if (pa[0] < lo && pb[0] < lo) {
        return 0;
    }
    /* a second read of pa[2], into its own variable */
    sz = pa[2];
    if (hi < pa[0] && hi < pb[0]) {
        return 0;
    }
    if (FcAbsF(pb[2] - sz) < 1.0f) {
        if (ray->slideCount > 0) {
            pc[0] = pb[0];
            pc[1] = pb[1];
            pb[2] = ray->radius + 1.0f;
        } else if (pa[0] < 0.0f) {
            pc[0] = lo;
            if (FcAbsF(pb[0] - pa[0]) < 5.0f) {
                pc[1] = pa[1];
            } else {
                pc[1] = (pb[1] - pa[1]) * (lo - pa[0]) / (pb[0] - pa[0]) + pa[1];
            }
            pb[2] = sz;
        } else if (e->height < pa[0]) {
            pc[0] = hi;
            if (FcAbsF(pb[0] - pa[0]) < 5.0f) {
                pc[1] = pa[1];
            } else {
                pc[1] = (pb[1] - pa[1]) * (hi - pa[0]) / (pb[0] - pa[0]) + pa[1];
            }
            pb[2] = sz;
        } else {
            pc[0] = pa[0];
            pc[1] = pa[1];
            pb[2] = ray->radius + 1.0f;
        }
        pb[0] = pc[0];
        pb[1] = pc[1];
        ray->slideCount++;
    } else {
        far = 25.0f < distance_squared_xz(pa, pb);
        if (ds < ray->radius) {
            hh = ds;
            ray->slideCount++;
        } else {
            hh = ray->radius;
        }
        if (pa[0] != pb[0] && far != 0) {
            pc[0] = (pb[0] - pa[0]) * (ds - hh) / FcAbsF(pb[2] - sz) + pa[0];
        } else {
            pc[0] = pa[0];
        }
        if (pc[0] < lo || hi < pc[0]) {
            return 0;
        }
        if (pa[1] != pb[1] && far != 0) {
            pc[1] = (pb[1] - pa[1]) * (ds - hh) / FcAbsF(pb[2] - sz) + pa[1];
        } else {
            pc[1] = pa[1];
        }
        pb[0] = pc[0];
        pb[1] = pc[1];
        pb[2] = ray->radius + 1.0f;
    }
    if (pb[1] < e->pt[0][1] && pb[1] < e->pt[1][1]) {
        return 0;
    }
    if (e->pt[2][1] < pb[1] && e->pt[3][1] < pb[1]) {
        return 0;
    }
    if (pb[1] < (e->pt[1][1] - e->pt[0][1]) * pb[0] / e->height + e->pt[0][1]) {
        return 0;
    }
    if ((e->pt[3][1] - e->pt[2][1]) * pb[0] / e->height + e->pt[2][1] < pb[1]) {
        return 0;
    }
    if (flip) {
        pb[2] = -pb[2];
    }
    t2 = pb[0] * nz - pb[2] * mx;
    out[1] = pb[1];
    t1 = pb[0] * mx + pb[2] * nz;
    out[0] = t2 + ex;
    out[2] = t1 + ez;
    sceVu0CopyVector((int *)ray->pt[2], (int *)out);
    return 1;
}

static __inline__ int FloorPointInside(FcFloorEnt *e, float *pt) /* derived name */
{
    FcVec4 *v;
    FcVec4 *p2;
    float cp[4];
    float vx;
    int cross;
    int i;
    int n;

    cross = 0;
    n = e->nex + 2;
    /* indexed, not walked: every expansion rebuilds &e->v[i] each pass */
    p2 = &e->v[n];
    for (i = 0; i <= n; i++) {
        v = &e->v[i];
        vx = v->x;
        if ((vx < pt[0] && pt[0] <= p2->x) || (p2->x < pt[0] && pt[0] <= vx)) {
            cp[0] = pt[0];
            cp[2] = (v->z - p2->z) * (pt[0] - p2->x) / (vx - p2->x) + p2->z;
            if (pt[2] < cp[2]) {
                cross++;
            } else if (cp[0] == pt[0] && cp[2] == pt[2]) {
                return 1;
            }
        }
        p2 = v;
    }

    return cross & 1;
}

static int clip_floor_1(ClipWork *ray, FcFloorEnt *e, int backFace)
{
    float hit[4];
    float nx = e->nx;
    float ex = ray->pt[2][0];
    float ny = e->ny;
    float ey = ray->pt[2][1];
    float nz = e->nz;
    float ez = ray->pt[2][2];
    float pd = e->d;
    float sx;
    float sy;
    float sz;
    float de;
    float ds;
    float t;

    de = nx * ex + ny * ey + nz * ez + pd;
    if (backFace != 0) {
        if (de < 0.0f) {
            return 0;
        }
    } else {
        if (de >= 0.0f) {
            return 0;
        }
    }
    sx = ray->pt[0][0];
    sy = ray->pt[0][1];
    sz = ray->pt[0][2];
    ds = nx * sx + ny * sy + nz * sz + pd;
    if (backFace != 0) {
        if (ds >= 0.0f) {
            return 0;
        }
    } else {
        if (ds < 0.0f) {
            return 0;
        }
    }
    t = 1.0f / (ds - de);
    hit[0] = (ex * ds - sx * de) * t;
    hit[1] = (ey * ds - sy * de) * t;
    hit[2] = (ez * ds - sz * de) * t;
    if (FloorPointInside(e, hit) == 0) {
        return 0;
    }
    sceVu0CopyVector((int *)ray->pt[2], (int *)hit);
    return 1;
}

inline void ResetCollisionPC(void)
{
    int tmp;
    pcWall0 = 0;
#ifdef ICO_HOST
    tmp = 0; /* no EE timer 0 on the host */
#else
    tmp = *T0_COUNT;
#endif
    pcWallR0 = 0;
    pcTime = tmp;

    pcFloor0 = 0;
    pcFloorR0 = 0;
    pcWall1 = 0;
    pcWallR1 = 0;
    pcFloor1 = 0;
    pcFloorR1 = 0;
}

void DispCollisionPC(void)
{
    if (game_pause == 0) {
        return;
    }
#ifdef ICO_HOST
    pcTime = 0 - pcTime; /* no EE timer 0 on the host */
#else
    pcTime = *T0_COUNT - pcTime;
#endif
    sprintf(pcLine, "W :%4d %2d", pcWall0, pcWall1);
    if (debug_font_flag & 1) {
        debug_Printf(ScreenWidth / 2, ScreenHeight / 2, 0xFFFFFF00, pcLine);
    }
    sprintf(pcLine, "WR:%4d %2d", pcWallR0, pcWallR1);
    if (debug_font_flag & 1) {
        debug_Printf(ScreenWidth / 2, ScreenHeight / 2 + 8, 0xFFFFFF00, pcLine);
    }
    sprintf(pcLine, "F :%4d %2d", pcFloor0, pcFloor1);
    if (debug_font_flag & 1) {
        debug_Printf(ScreenWidth / 2, ScreenHeight / 2 + 0x10, 0xFFFFFF00, pcLine);
    }
    sprintf(pcLine, "FR:%4d %2d", pcFloorR0, pcFloorR1);
    if (debug_font_flag & 1) {
        debug_Printf(ScreenWidth / 2, ScreenHeight / 2 + 0x18, 0xFFFFFF00, pcLine);
    }
}

static void makeCollisionBlockTable(float *ray)
{
    int x0;
    int x1;
    int z0;
    int z1;
    int bx;
    int bz;
    int bx1;
    int bz1;
    int dx;
    int dz;
    int cx;
    int cz;
    int sx;
    int sz;
    int d;
    int err;
    int swap;
    int i;
    int t;
    int px;
    int pz;

    blockNum = 0;
    x0 = (int)(ray[0] - FUZIO_OFS(curFuzio)[0]);
    x1 = (int)(ray[8] - FUZIO_OFS(curFuzio)[0]);
    z0 = (int)(ray[2] - FUZIO_OFS(curFuzio)[2]);
    z1 = (int)(ray[10] - FUZIO_OFS(curFuzio)[2]);
    bx = x0 >> 9;
    bz = z0 >> 9;
    dx = x1 - x0;
    dz = z1 - z0;
    if (dx != 0 || dz != 0) {
        bx1 = x1 >> 9;
        bz1 = z1 >> 9;
        cx = abs(bx1 - bx);
        cz = abs(bz1 - bz);
        sx = dx > 0 ? 1 : -1;
        sz = dz > 0 ? 1 : (dz < 0 ? -1 : 0);
        px = x0 - 256;
        pz = z0 - 256;
        d = (sx * sz * (dz * ((bx << 9) - px) - dx * ((bz << 9) - pz))) >> 8;
        dx = dx < 0 ? -dx : dx;
        dz = dz < 0 ? -dz : dz;
        swap = 0;
        if (dx < dz) {
            t = dx;
            dx = dz;
            dz = t;
            cx = cz;
            d = -d;
            swap = 1;
        }
        err = dz - dx + d;
        if (dx > 100000) {
            return;
        }
        for (i = 0; i <= cx; i++) {
            if (bx >= 0 && bx < 32 && bz >= 0 && bz < 32) {
                blockTable[blockNum] = (bz << 5) + bx;
                blockNum = blockNum + 1;
            }
            while (err >= 0) {
                if (swap == 1) {
                    bx += sx;
                } else {
                    bz += sz;
                }
                if (bx >= 0 && bx < 32 && bz >= 0 && bz < 32) {
                    blockTable[blockNum] = (bz << 5) + bx;
                    blockNum = blockNum + 1;
                }
                err -= dx * 2;
            }
            if (swap == 1) {
                bz += sz;
            } else {
                bx += sx;
            }
            err += dz * 2;
        }
    } else {
        if (bx >= 0 && bx < 32 && bz >= 0 && bz < 32) {
            blockTable[blockNum] = (bz << 5) + bx;
            blockNum = blockNum + 1;
        }
    }
}

static inline int _clipWDebug(ClipWork *work, GObj *obj, int node)
{
    int ret = 0;
    int i;

    for (i = 0; i < blockNum; i++) {
        short *p = FUZIO_WBLK(curFuzio, blockTable[i]);
        if (p != 0) {
            while (*p >= 0) {
                FcWallEnt *e = &FUZIO_WALLS(curFuzio)[*p];
                if (clip_wall_1(work, e, 0, 1) != 0) {
                    work->wall.elem = e;
                    ret = 1;
                    work->wall.o.obj = obj;
                    work->wall.o.node = node;
                }
                p++;
            }
        }
    }
    return ret;
}

static inline int _clipW(ClipWork *work, GObj *obj, int node)
{
    int ret = 0;
    int i;

    for (i = 0; i < blockNum; i++) {
        short *p = FUZIO_WBLK(curFuzio, blockTable[i]);
        if (p != 0) {
            while (*p >= 0) {
                FcWallEnt *e = &FUZIO_WALLS(curFuzio)[*p];
                int val = e->attr;
                if ((val & 0xF0000000) == 0) {
                    if ((val & 0xF0000) != 0x10000) {
                        if (clip_wall_1(work, e, 0, 1) != 0) {
                            work->wall.elem = e;
                            ret = 1;
                            work->wall.o.obj = obj;
                            work->wall.o.node = node;
                        }
                    }
                }
                p++;
            }
        }
    }
    return ret;
}

static inline int _clipWE(ClipWork *work, GObj *obj, int node)
{
    int ret = 0;
    int i;

    for (i = 0; i < blockNum; i++) {
        short *p = FUZIO_WBLK(curFuzio, blockTable[i]);
        if (p != 0) {
            while (*p >= 0) {
                FcWallEnt *e = &FUZIO_WALLS(curFuzio)[*p];
                int val = e->attr;
                if ((val & 0xF0000000) == 0) {
                    if ((val & 0xF0000) != 0x10000) {
                        if (obj != work->filter.o.obj || node != work->filter.o.node ||
                            e != work->filter.elem) {
                            if (clip_wall_1(work, e, 0, 0) != 0) {
                                work->wall.elem = e;
                                ret = 1;
                                work->wall.o.obj = obj;
                                work->wall.o.node = node;
                            }
                        }
                    }
                }
                p++;
            }
        }
    }
    return ret;
}

static inline int _clipWEField(ClipWork *work, GObj *obj, int node)
{
    int found = 0;
    int i;

    for (i = 0; i < blockNum; i++) {
        short *p = FUZIO_WBLK(curFuzio, blockTable[i]);
        if (p != 0) {
            while (*p >= 0) {
                FcWallEnt *e = &FUZIO_WALLS(curFuzio)[*p];
                if ((e->attr & 0xF0000000) == 0) {
                    if (obj != work->filter.o.obj || node != work->filter.o.node ||
                        e != work->filter.elem) {
                        if (clip_wall_1(work, e, 0, 0) != 0) {
                            work->wall.elem = e;
                            found = 1;
                            work->wall.o.obj = obj;
                            work->wall.o.node = node;
                        }
                    }
                }
                p++;
            }
        }
    }
    return found;
}

static inline int _clipWR(ClipWork *work, GObj *obj, int node)
{
    int ret = 0;
    int i;

    for (i = 0; i < blockNum; i++) {
        short *p = FUZIO_WBLK(curFuzio, blockTable[i]);
        if (p != 0) {
            while (*p >= 0) {
                FcWallEnt *e = &FUZIO_WALLS(curFuzio)[*p];
                int val = e->attr;
                if ((val & 0xF0000000) == 0) {
                    if ((val & 0xF0000) != 0x10000) {
                        if (clip_wall_1(work, e, 1, 1) != 0) {
                            work->wall.elem = e;
                            ret = 1;
                            work->wall.o.obj = obj;
                            work->wall.o.node = node;
                        }
                    }
                }
                p++;
            }
        }
    }
    return ret;
}

static inline int _clipWField(ClipWork *work, GObj *obj, int node)
{
    int ret = 0;
    int i;

    for (i = 0; i < blockNum; i++) {
        short *p = FUZIO_WBLK(curFuzio, blockTable[i]);
        if (p != 0) {
            while (*p >= 0) {
                FcWallEnt *e = &FUZIO_WALLS(curFuzio)[*p];
                if ((e->attr & 0xF0000000) == 0) {
                    if (clip_wall_1(work, e, 0, 1) != 0) {
                        work->wall.elem = e;
                        ret = 1;
                        work->wall.o.obj = obj;
                        work->wall.o.node = node;
                    }
                }
                p++;
            }
        }
    }
    return ret;
}

static inline int _clipWDitchHangWalkStop(ClipWork *work, GObj *obj, int node)
{
    int ret = 0;
    int i;

    for (i = 0; i < blockNum; i++) {
        short *p = FUZIO_WBLK(curFuzio, blockTable[i]);
        if (p != 0) {
            while (*p >= 0) {
                FcWallEnt *e = &FUZIO_WALLS(curFuzio)[*p];
                if ((e->attr & 0x30000000) != 0) {
                    if (clip_wall_1(work, e, 0, 1) != 0) {
                        work->wall.elem = e;
                        ret = 1;
                        work->wall.o.obj = obj;
                        work->wall.o.node = node;
                    }
                }
                p++;
            }
        }
    }
    return ret;
}

static inline int _clipWWaveForce(ClipWork *work, GObj *obj, int node)
{
    int ret = 0;
    int i;

    for (i = 0; i < blockNum; i++) {
        short *p = FUZIO_WBLK(curFuzio, blockTable[i]);
        if (p != 0) {
            while (*p >= 0) {
                FcWallEnt *e = &FUZIO_WALLS(curFuzio)[*p];
                if ((e->attr & 0xC0000000) == 0x40000000) {
                    if (clip_wall_1(work, e, 0, 1) != 0) {
                        work->wall.elem = e;
                        ret = 1;
                        work->wall.o.obj = obj;
                        work->wall.o.node = node;
                    }
                }
                p++;
            }
        }
    }
    return ret;
}

static inline int _clipWBoxStop(ClipWork *work, GObj *obj, int node)
{
    int ret = 0;
    int i;

    for (i = 0; i < blockNum; i++) {
        short *p = FUZIO_WBLK(curFuzio, blockTable[i]);
        if (p != 0) {
            while (*p >= 0) {
                FcWallEnt *e = &FUZIO_WALLS(curFuzio)[*p];
                int val = e->attr;
                if ((val & 0x70000000) == 0) {
                    if ((val & 0xF0000) != 0x10000 || (val & 0xC0000000) == 0x80000000) {
                        if (clip_wall_1(work, e, 0, 1) != 0) {
                            work->wall.elem = e;
                            ret = 1;
                            work->wall.o.obj = obj;
                            work->wall.o.node = node;
                        }
                    }
                }
                p++;
            }
        }
    }
    return ret;
}

static inline int _clipWAdjustPos(ClipWork *work, GObj *obj, int node)
{
    int ret = 0;
    int i;

    for (i = 0; i < blockNum; i++) {
        short *p = FUZIO_WBLK(curFuzio, blockTable[i]);
        if (p != 0) {
            while (*p >= 0) {
                FcWallEnt *e = &FUZIO_WALLS(curFuzio)[*p];
                if ((e->attr & 0xC0000000) == 0xC0000000) {
                    if (clip_wall_1(work, e, 0, 1) != 0) {
                        work->wall.elem = e;
                        ret = 1;
                        work->wall.o.obj = obj;
                        work->wall.o.node = node;
                    }
                }
                p++;
            }
        }
    }
    return ret;
}

static inline int _clipF(ClipWork *work, GObj *obj, int node)
{
    int ret = 0;
    int i;

    for (i = 0; i < blockNum; i++) {
        short *p = FUZIO_FBLK(curFuzio, blockTable[i]);
        if (p != 0) {
            while (*p >= 0) {
                FcFloorEnt *e = &FUZIO_FLOORS(curFuzio)[*p];
                if (clip_floor_1(work, e, 0) != 0) {
                    work->floor.elem = e;
                    ret = 1;
                    work->floor.o.obj = obj;
                    work->floor.o.node = node;
                    work->wall.elem = 0;
                }
                p++;
            }
        }
    }
    return ret;
}

static inline int _clipFE(ClipWork *work, GObj *obj, int node)
{
    int ret = 0;
    int i;

    for (i = 0; i < blockNum; i++) {
        short *p = FUZIO_FBLK(curFuzio, blockTable[i]);
        if (p != 0) {
            while (*p >= 0) {
                FcFloorEnt *e = &FUZIO_FLOORS(curFuzio)[*p];
                if (obj != work->filter.o.obj || node != work->filter.o.node ||
                    e != work->filter.elem) {
                    if (clip_floor_1(work, e, 0) != 0) {
                        work->floor.elem = e;
                        ret = 1;
                        work->floor.o.obj = obj;
                        work->floor.o.node = node;
                        work->wall.elem = 0;
                    }
                }
                p++;
            }
        }
    }
    return ret;
}

static inline int _clipFIH(ClipWork *work, GObj *obj, int node)
{
    int ret = 0;
    int i;

    for (i = 0; i < blockNum; i++) {
        short *p = FUZIO_FBLK(curFuzio, blockTable[i]);
        if (p != 0) {
            while (*p >= 0) {
                FcFloorEnt *e = &FUZIO_FLOORS(curFuzio)[*p];
                if ((e->attr & 0xF0000) != 0x20000) {
                    if (clip_floor_1(work, e, 0) != 0) {
                        work->floor.elem = e;
                        ret = 1;
                        work->floor.o.obj = obj;
                        work->floor.o.node = node;
                        work->wall.elem = 0;
                    }
                }
                p++;
            }
        }
    }
    return ret;
}

static inline int _clipFR(ClipWork *work, GObj *obj, int node)
{
    int ret = 0;
    int i;

    for (i = 0; i < blockNum; i++) {
        short *p = FUZIO_FBLK(curFuzio, blockTable[i]);
        if (p != 0) {
            while (*p >= 0) {
                FcFloorEnt *e = &FUZIO_FLOORS(curFuzio)[*p];
                if (clip_floor_1(work, e, 1) != 0) {
                    work->floor.elem = e;
                    ret = 1;
                    work->floor.o.obj = obj;
                    work->floor.o.node = node;
                    work->wall.elem = 0;
                }
                p++;
            }
        }
    }
    return ret;
}

/* The clip-mode table the ClipWall/ClipFloor wrappers index by mode: modes
 * 0..11 are the wall entries, 12 on the floor ones (ClipFloor passes 0xC).
 * 16-byte records, func at +0xC (ClipFloorByGObj reads entry 12's
 * directly). */
typedef struct {      /* field names derived */
    int wall;         /* the wall-hit arm runs after the walk (modes 0..11) */
    int skipFilter;   /* skip the work's filter object and node */
    int useColFilter; /* only objects colFilter passes */
    int (*func)(ClipWork *work, GObj *obj, int node);
} FcClipMode; /* derived name */

/* The clip modes, then the work matrix whose translation row _Clip sets for
   an unrotated object, and the plane point of the wall hit arm. */
static FcClipMode clipMode[17] = {
    /* derived name */
    {1, 0, 0, _clipWDebug},     {1, 0, 0, _clipW},
    {1, 0, 0, _clipWR},         {1, 0, 0, _clipWField},
    {1, 1, 0, _clipWE},         {1, 1, 0, _clipWEField},
    {1, 0, 0, _clipWWaveForce}, {1, 0, 0, _clipWDitchHangWalkStop},
    {1, 0, 1, _clipW},          {1, 0, 1, _clipWField},
    {1, 0, 0, _clipWBoxStop},   {1, 0, 0, _clipWAdjustPos},
    {0, 0, 0, _clipF},          {0, 1, 0, _clipFE},
    {0, 0, 0, _clipFR},         {0, 0, 0, _clipFIH},
    {0, 0, 1, _clipF},
}; /* derived name */

static float clipMatrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0}; /* derived name */

static float clipPlanePos[4] = {0}; /* derived name */

typedef union { /* field names derived */
    float f[4];
    int i[4];
    long long ll[2];
} FcPlane; /* derived name */

/* one static inline expanded in both tail arms */
static __inline__ void setClipPlane(ClipWork *self, void *m, void *v) /* derived name */
{
    Vec16 *n = &self->normal;

    sceVu0ApplyMatrix(n, m, v);
    n->f[3] = -sceVu0InnerProduct(n, self->pt[2]);
}

/* a gobj's display-object slot read through a union: the ROM orders the
 * wall arm's read of it behind the float stores to clipPlanePos, as an
 * access through a union is ordered; a plain GObj.dobj read is scheduled
 * ahead of them (measured) */
typedef union { /* field names derived */
    Sub15C *dobj;
} FcSubSlot; /* derived name */

static void _Clip(ClipWork *self, int mode)
{
    float sv0[4];
    float sv1[4];
    float m0[16];
    float keep[4];
    float m1[16];
    FcPlane keep2;
    int (*func)(ClipWork *, GObj *, int);
    GObj *obj;
    Sub15C *sub;
    char *m;
    int cnt;
    int i;

    func = clipMode[mode].func;
    {
        int x = clipMode[mode].skipFilter;
        int y = clipMode[mode].useColFilter;

        sceVu0CopyVector(sv0, self->pt[0]);
        sceVu0CopyVector(sv1, self->pt[1]);
        sceVu0CopyVector(self->pt[2], self->pt[1]);
        colObjNum = 0;
        obj = colObjList[0];
        if (colObjListNum > 0) {
            do {
                m = (char *)clipMatrix;
                sub = obj->dobj;
                if (sub->disp != 0) {
                    if (x != 0) {
                        if (obj == self->filter.o.obj) {
                            if (self->filter.o.node < 0) {
                                goto next_gobj;
                            }
                        }
                    }
                    if (y != 0) {
                        if (colFilter(obj) == 0) {
                            goto next_gobj;
                        }
                    }
                    sub = obj->dobj;
                    cnt = 1;
                    if (sub->colPerNode != 0) {
                        cnt = sub->nodeNum;
                    }
                    curFuzio = (FuzioCtx *)sub->colData;
                    for (i = 0; i < cnt; i++) {
                        if (x != 0) {
                            if (obj == self->filter.o.obj && i == self->filter.o.node &&
                                self->filter.elem == 0) {
                                continue;
                            }
                        }
                        CopyVector(keep, self->pt[2]);
                        CopyVector(self->pt[0], sv0);
                        if (obj->dobj->colRotate == 0) {
                            CopyVector(&clipMatrix[12],
                                       (char *)obj->dobj->nodeMtx + (i << 6) + 0x30);
                        } else {
                            m = (char *)obj->dobj->nodeMtx + (i << 6);
                        }
                        MatrixDrive_SetTransposeMatrix(m0, m);
                        self->pt[0][3] = self->pt[2][3] = 1.0f;
                        _ApplyMatrix(self->pt[0], m0, self->pt[0]);
                        _ApplyMatrix(self->pt[2], m0, self->pt[2]);
                        makeCollisionBlockTable(self->pt[0]);
                        if (func(self, obj, i)) {
                            self->pt[2][3] = 1.0f;
                            _ApplyMatrix(self->pt[2], m, self->pt[2]);
                        } else {
                            CopyVector(self->pt[2], keep);
                        }
                    }
                }
            next_gobj:
                colObjNum = colObjNum + 1;
                obj = colObjList[colObjNum];
            } while (colObjNum < colObjListNum);
        }
        if (clipMode[mode].wall != 0) {
            if (self->wall.elem != 0) {
                clipPlanePos[0] = FC_WALL_NORMAL((FcWallEnt *)self->wall.elem)[0];
                clipPlanePos[2] = FC_WALL_NORMAL((FcWallEnt *)self->wall.elem)[1];
                CopyMatrix(m1, (char *)((FcSubSlot *)&self->wall.o.obj->dobj)->dobj->nodeMtx +
                                   (self->wall.o.node << 6));
                if (self->wall.o.obj->dobj->colRotate == 0) {
                    UnitRotation(m1);
                }
                setClipPlane(self, m1, clipPlanePos);
                self->attr = ((FcWallEnt *)self->wall.elem)->attr;
            } else {
                sceVu0CopyVector(self->pt[2], sv1);
            }
        } else {
            if (self->floor.elem != 0) {
                CopyVector(&keep2, &((FcFloorEnt *)self->floor.elem)->nx);
                keep2.i[3] = 0;
                CopyMatrix(m1,
                           (char *)self->floor.o.obj->dobj->nodeMtx + (self->floor.o.node << 6));
                if (self->floor.o.obj->dobj->colRotate == 0) {
                    UnitRotation(m1);
                }
                setClipPlane(self, m1, &keep2);
                self->attr = ((FcFloorEnt *)self->floor.elem)->attr;
            }
        }
        sceVu0CopyVector(self->pt[0], sv0);
        sceVu0CopyVector(self->pt[1], sv1);
    }
}

static void __ClipWall(ClipWork *work, int mode)
{
    work->slideCount = 0;
    work->floor.elem = 0;
    work->wall.elem = 0;
    work->wall.o = InitialObjPointer;
    _Clip(work, mode);
}

static inline void __ClipWallWithDrawRay(ClipWork *w, int mode)
{
    __ClipWall(w, mode);
    gif_StartPacketPri(11);
    MatrixDrive_PushMatrix();
    {
        sceVu0IVECTOR c0 = {255, 64, 64, 128};
        sceVu0IVECTOR c1 = {32, 0, 0, 128};

        gif_SetAlpha(1, 5, 0x80);
        gif_SetZTest(1);
        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        DrawLineG(w->pt[0], c0, w->pt[1], c0, 0);
        DrawLineG(w->pt[0], c1, w->pt[1], c1, -1);
    }
    MatrixDrive_PopMatrix();
    gif_EndPacket();
}

static void __ClipFloor(ClipWork *work, int mode)
{
    work->floor.elem = 0;
    work->floor.o = InitialObjPointer;
    _Clip(work, mode);
}

static inline void __ClipFloorWithDrawRay(ClipWork *w, int mode)
{
    __ClipFloor(w, mode);
    gif_StartPacketPri(11);
    MatrixDrive_PushMatrix();
    {
        sceVu0IVECTOR c0 = {64, 64, 255, 128};
        sceVu0IVECTOR c1 = {0, 0, 32, 128};

        gif_SetAlpha(1, 5, 0x80);
        gif_SetZTest(1);
        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        DrawLineG(w->pt[0], c0, w->pt[1], c0, 0);
        DrawLineG(w->pt[0], c1, w->pt[1], c1, -1);
    }
    MatrixDrive_PopMatrix();
    gif_EndPacket();
}

inline void ClipWallRD(void)
{
    collision_pick = 1;
    /* ClipWall called with no argument, through a cast of its (int)
     * prototype */
    ((void (*)(void))ClipWall)();
    collision_pick = 0;
}

static void (*clipWallFunc)(ClipWork *work, int mode) = __ClipWall; /* derived name */

static void (*clipFloorFunc)(ClipWork *work, int mode) = __ClipFloor; /* derived name */

inline int ChangeFieldCollisionDebugMode(int drawRay)
{
    clipWallFunc = __ClipWall;
    clipFloorFunc = __ClipFloor;
    if (drawRay != 0) {
        clipWallFunc = __ClipWallWithDrawRay;
        clipFloorFunc = __ClipFloorWithDrawRay;
    }
    return 0;
}

inline void ClipWallDebug(ClipWork *work)
{
    clipWallFunc(work, 0);
}

inline void ClipWall(ClipWork *work)
{
    clipWallFunc(work, 0x1);
}

inline void ClipWallR(ClipWork *work)
{
    clipWallFunc(work, 0x2);
}

inline void ClipWallWaveForce(ClipWork *work)
{
    clipWallFunc(work, 0x6);
}

inline void ClipWallFuchiHangWalkStop(ClipWork *work)
{
    clipWallFunc(work, 0x7);
}

inline void ClipWallField(ClipWork *work)
{
    clipWallFunc(work, 0x3);
}

inline void ClipWallEField(ClipWork *work)
{
    clipWallFunc(work, 0x5);
}

inline void ClipWallBoxStop(ClipWork *work)
{
    clipWallFunc(work, 0xA);
}

inline void ClipWallAdjustPos(ClipWork *work)
{
    clipWallFunc(work, 0xB);
}

inline void ClipWallE(ClipWork *work)
{
    clipWallFunc(work, 0x4);
}

inline void ClipWallCheckCB(ClipWork *work, int filter)
{
    colFilter = (int (*)(void *))filter;
    clipWallFunc(work, 8);
}

inline void ClipWallFieldCheckCB(ClipWork *work, int filter)
{
    colFilter = (int (*)(void *))filter;
    clipWallFunc(work, 9);
}

inline void ClipFloor(ClipWork *work)
{
    clipFloorFunc(work, 0xC);
}

inline void ClipFloorE(ClipWork *work)
{
    clipFloorFunc(work, 0xD);
}

inline void ClipFloorR(ClipWork *work)
{
    clipFloorFunc(work, 0xE);
}

inline void ClipFloorIH(ClipWork *work)
{
    clipFloorFunc(work, 0xF);
}

inline void ClipFloorCheckCB(ClipWork *work, int filter)
{
    colFilter = (int (*)(void *))filter;
    clipFloorFunc(work, 0x10);
}

inline void *ClipWallVector(float *start, float *end)
{
    ClipWork w;
    w.radius = 50.0f;
    sceVu0CopyVector(w.pt[0], start);
    sceVu0CopyVector(w.pt[1], end);
    ClipWall(&w);
    return w.wall.elem;
}

inline float GetYProjectionOfPlane(float *plane, float *pos)
{
    return -(plane[0] * pos[0] + plane[2] * pos[2] + plane[3]) / plane[1];
}

inline float GetDistanceFromPlane(void *plane, void *pos)
{
    return sceVu0InnerProduct(plane, pos) + ((float *)plane)[3];
}

inline float GetYDistanceFromPlane(float *plane, float *pos)
{
    return pos[1] - GetYProjectionOfPlane(plane, pos);
}

typedef union { /* field names derived */
    float f[4];
    long long ll[2];
} FcVec; /* derived name */

inline void GetWallGlobalInfo(void *pts, void *nrm, char *w, void *m)
{
    FcVec vec = {
        {GetTableSin(*(short *)(w + 0x44)), 0.0f, GetTableCos(*(short *)(w + 0x44)), 0.0f}};
    int i;

    if (pts != 0) {
        char *src = w;
        char *dst = pts;
        for (i = 3; i >= 0; i--) {
            sceVu0ApplyMatrix(dst, m, src);
            src += 0x10;
            dst += 0x10;
        }
    }
    sceVu0ApplyMatrix(nrm, m, &vec);
}

inline void GetGlobalWallPlane(float *plane, WallCfg *wall)
{
    FcVec pts[4];

    GetWallGlobalInfo(pts, plane, wall->elem,
                      (void *)((wall->o.node << 6) + GOBJ_SUB(wall->o.obj)->nodeMtx));
    plane[3] = -sceVu0InnerProduct(plane, pts);
}

inline int ClipPlane(int work)
{
    float *p = (float *)work;
    char *q = (char *)(work + 0xA0);
    float t0, t1, d;

    sceVu0CopyVector((int *)(work + 0x20), (int *)(work + 0x10));
    t0 = GetDistanceFromPlane(q, (void *)(work + 0x10));
    if (t0 >= 0.0f) {
        return 0;
    }
    t1 = GetDistanceFromPlane(q, (void *)work);
    if (t1 < 0.0f) {
        if (t0 < 0.0f) {
            return 0;
        }
    }
    d = t1 - t0;
    p[8] = (p[4] * t1 - p[0] * t0) / d;
    p[9] = (p[5] * t1 - p[1] * t0) / d;
    p[10] = (p[6] * t1 - p[2] * t0) / d;
    return 1;
}

inline void ClipCollision(ClipWork *self)
{
    int buf[4];
    sceVu0CopyVector(buf, self->pt[1]);
    ClipWall(self);
    sceVu0CopyVector(self->pt[1], self->pt[2]);
    ClipFloor(self);
    sceVu0CopyVector(self->pt[1], buf);
}

inline void MapCollisionData(void *data)
{
    int *p = (int *)data;
    p[4] = (int)data + p[4];
    p[5] = (int)data + p[5];
}

inline void LoadCollision(void **self, char *fname)
{
    int *p;
    file_LoadFile(self, fname, 0);
    p = *self;
    p[4] = (int)p + p[4];
    p[5] = (int)p + p[5];
}

static int wallDrawCnt = 0; /* derived name */

/* The wall edge and rim colours for a plain wall, an attributed one and the
   two exit kinds, then a unit matrix nothing reads. */
static FcColor wallEdgeColor = {0, 56, 255, 128}; /* derived name */

static FcColor wallEdgeColorAttr = {0, 0, 255, 128}; /* derived name */

static FcColor wallEdgeColorExit = {0, 255, 192, 128}; /* derived name */

static FcColor wallEdgeColorExit1 = {255, 0, 192, 128}; /* derived name */

static FcColor wallRimColor = {0, 5, 25, 32}; /* derived name */

static FcColor wallRimColorAttr = {0, 0, 25, 32}; /* derived name */

static FcColor wallRimColorExit = {0, 25, 19, 32}; /* derived name */

static FcColor wallRimColorExit1 = {25, 0, 19, 32}; /* derived name */

static float unitMatrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0}; /* derived name */

typedef struct { /* field names derived */
    char pad0[8];
    int nobj;  /* 0x8  */
    char *mtx; /* 0xC  */
    char pad10[96];
    char *coll; /* 0x70 */
    char pad74[4];
    int norot; /* 0x78 */
    char pad7C[4];
    int multi; /* 0x80 */
} FcWallSub;   /* derived name */

typedef struct { /* field names derived */
    char pad0[348];
    FcWallSub *sub; /* 0x15C */
} FcWallObj;        /* derived name */

/* the collision file's head again (fieldCollision.h's FcColl) */
#ifdef ICO_HOST

typedef FcColl FcWallSet; /* derived name */

#define FCWS_NWALL(c) ((c)->count)
#define FCWS_WALLS(c) ICO_EEPTR(char *, (c)->wcl)
#else

typedef struct { /* field names derived */
    char pad0[8];
    int nwall; /* 0x8  */
    char padC[4];
    char *walls; /* 0x10 */
} FcWallSet;     /* derived name */

#define FCWS_NWALL(c) ((c)->nwall)
#define FCWS_WALLS(c) ((c)->walls)
#endif

void DrawGObjWallCollision(GObj *gobj, int col)
{
    FcWallObj *g = (FcWallObj *)gobj;
    FcWallSet *cd;
    char *e;
    const FcColor *c0;
    const FcColor *c1;
    int n;
    int i;
    int j;
    int attr;

    wallDrawCnt = wallDrawCnt + 1;
    n = 1;
    if (g->sub->multi != 0) {
        n = g->sub->nobj;
    }
    cd = (FcWallSet *)g->sub->coll;
    gif_StartPacketPri(11);
    MatrixDrive_PushMatrix();
    gif_SetAlpha(1, 5, 0);
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    for (i = 0; i < n; i++) {
        CopyMatrix(MatrixDrive_GetMatrix(), g->sub->mtx + (i << 6));
        if (g->sub->norot == 0) {
            UnitRotation(MatrixDrive_GetMatrix());
        }
        for (j = 0; j < FCWS_NWALL(cd); j++) {
            e = FCWS_WALLS(cd) + j * 0x50;
            c0 = &wallEdgeColor;
            c1 = &wallRimColor;
            attr = *(int *)(e + 0x48);
            if ((attr & 0xF0000000) != 0) {
                c0 = &wallEdgeColorExit;
                c1 = &wallRimColorExit;
                if ((attr & 0x10000000) != 0) {
                    c0 = &wallEdgeColorExit1;
                    c1 = &wallRimColorExit1;
                }
            } else if (attr != 0) {
                c0 = &wallEdgeColorAttr;
                c1 = &wallRimColorAttr;
            }
            DrawLineG(e, (void *)c0, e + 0x10, (void *)c0, col);
            DrawLineG(e + 0x10, (void *)c0, e + 0x30, (void *)c0, col);
            DrawLineG(e + 0x30, (void *)c0, e + 0x20, (void *)c0, col);
            DrawLineG(e + 0x20, (void *)c0, e, (void *)c0, col);
            DrawLineG(e, (void *)c1, e + 0x10, (void *)c1, -1);
            DrawLineG(e + 0x10, (void *)c1, e + 0x30, (void *)c1, -1);
            DrawLineG(e + 0x30, (void *)c1, e + 0x20, (void *)c1, -1);
            DrawLineG(e + 0x20, (void *)c1, e, (void *)c1, -1);
        }
    }
    MatrixDrive_PopMatrix();
    gif_EndPacket();
}

void DrawGObjFloorCollision(GObj *gobj, int col)
{
    int n;
    char *cd;
    int i;
    int j;

    n = 1;
    if (GOBJ_SUB(gobj)->colPerNode != 0) {
        n = GOBJ_SUB(gobj)->nodeNum;
    }
    cd = (char *)GOBJ_SUB(gobj)->colData;
    gif_StartPacketPri(11);
    MatrixDrive_PushMatrix();
    gif_SetAlpha(1, 5, 0);
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    for (i = 0; i < n; i++) {
        CopyMatrix(MatrixDrive_GetMatrix(), (char *)GOBJ_SUB(gobj)->nodeMtx + (i << 6));
        if (GOBJ_SUB(gobj)->colRotate == 0) {
            UnitRotation(MatrixDrive_GetMatrix());
        }
        for (j = 0; j < *(int *)(cd + 0xC); j++) {
            char *e = ICO_EEPTR(char *, *(int *)(cd + 0x14)) + j * 0x70;
            sceVu0IVECTOR c = {56, 0, 8, 128};

            DrawLineG(e, c, e + 0x10, c, col);
            if (*(int *)(e + 0x54) == 0) {
                DrawLineG(e + 0x10, c, e + 0x20, c, col);
                DrawLineG(e + 0x20, c, e, c, col);
            } else {
                DrawLineG(e + 0x10, c, e + 0x20, c, col);
                DrawLineG(e + 0x20, c, e + 0x30, c, col);
                DrawLineG(e + 0x30, c, e, c, col);
            }
        }
    }
    MatrixDrive_PopMatrix();
    gif_EndPacket();
}

inline void DrawCollision(int mode)
{
    int n = mode;
    void *obj;

    if (n > 0) {
        n = -1;
    }
    gif_StartPacketPri(11);
    gif_SetZTest(1);
    gif_EndPacket();
    colObjNum = 0;
    obj = colObjList[0];
    if (colObjListNum > 0) {
        do {
            DrawGObjWallCollision(obj, n);
            colObjNum = colObjNum + 1;
            obj = colObjList[colObjNum];
        } while (colObjNum < colObjListNum);
    }
    colObjNum = 0;
    obj = colObjList[0];
    if (colObjListNum > 0) {
        do {
            DrawGObjFloorCollision(obj, n);
            colObjNum = colObjNum + 1;
            obj = colObjList[colObjNum];
        } while (colObjNum < colObjListNum);
    }
}

void DBG_VECTOR(float *vec)
{
    return debug_StdPrintfDummy("%8f %8f %8f", vec[0], vec[1], vec[2]);
}

int GetEdgeOfFloor(float *out, FcFloorEnt *e, float *p1, float *p2)
{
    float n[4];
    FcVec4 *va;
    FcVec4 *vb;
    float d1;
    float d2;
    int i;
    int j;

    if (FloorPointInside(e, p1) != 1) {
        debug_StdPrintfDummy("cl:src is not inside\n");
    }
    if (FloorPointInside(e, p2) != 0) {
        debug_StdPrintfDummy("cl:dst is not outside\n");
    }
    for (i = 0; i < 4; i++) {
        j = (i + 3) % 4;
        va = &e->v[i];
        vb = &e->v[j];
        if (va->x != vb->x) {
            d1 = (va->z - vb->z) * (p1[0] - vb->x) / (va->x - vb->x) - (p1[2] - vb->z);
            d2 = (va->z - vb->z) * (p2[0] - vb->x) / (va->x - vb->x) - (p2[2] - vb->z);
        } else {
            d1 = (va->x - vb->x) * (p1[2] - vb->z) / (va->z - vb->z) - (p1[0] - vb->x);
            d2 = (va->x - vb->x) * (p2[2] - vb->z) / (va->z - vb->z) - (p2[0] - vb->x);
        }
        if (d1 < 0.0f && d2 < 0.0f) {
            continue;
        }
        if (d1 > 0.0f && d2 > 0.0f) {
            continue;
        }
        /* the normal reads both vertices through pointers of its own; the fourth
         * store lands one float past n */
        {
            FcVec4 *ca = &e->v[i];
            FcVec4 *cb = &e->v[j];

            n[0] = ca->z - cb->z;
            n[1] = 0.0f;
            n[2] = -(ca->x - cb->x);
            n[4] = 0.0f;
            sceVu0Normalize(out, n);
            out[4] = 0.0f;
            break;
        }
    }
    if (i == 4) {
        /* The dump's loop has locals of its own.  The fptodp calls below are
         * the float-to-double promotions of the variadic call's
         * arguments. */
        FcVec4 *da;
        FcVec4 *db;
        float g1;
        float g2;

        debug_StdPrintfDummy("cl:no hit??\n");
        debug_StdPrintfDummy("src:%8f %8f %8f\n", p1[0], p1[1], p1[2]);
        debug_StdPrintfDummy("dst:%8f %8f %8f\n", p2[0], p2[1], p2[2]);
        for (i = 0; i < 4; i++) {
            debug_StdPrintfDummy("%2d ", i);
            DBG_VECTOR((float *)&e->v[i]);
            debug_StdPrintfDummy("\n");
        }
        for (i = 0; i < 4; i++) {
            j = (i + 3) % 4;
            da = &e->v[i];
            db = &e->v[j];
            if (da->x != db->x) {
                g1 = (da->z - db->z) * (p1[0] - db->x) / (da->x - db->x) - (p1[2] - db->z);
                g2 = (da->z - db->z) * (p2[0] - db->x) / (da->x - db->x) - (p2[2] - db->z);
            } else {
                g1 = (da->x - db->x) * (p1[2] - db->z) / (da->z - db->z) - (p1[0] - db->x);
                g2 = (da->x - db->x) * (p2[2] - db->z) / (da->z - db->z) - (p2[0] - db->x);
            }
            debug_StdPrintfDummy("%02d: src:%8f dst:%8f\n", i, g1, g2);
        }
        debug_assert(__FILE__, 2006);
        __assert(__FILE__, 2006, "0");
    }
    return i;
}

inline void GetOrientOfWall(void *out, void *wallEnt, ObjNode *src)
{
    float buf[4];
    /* NULL on the no-wall path, where the code stores 0 through it: a
       deliberate fault after the message */
    int *trap;
    void *obj = src->obj;

    if (wallEnt == 0) {
        buf[1] = 0.0f;
        buf[2] = 1.0f;
        trap = 0;
        buf[0] = 0.0f;
        /* "GetOrientOfWall was called with no wall" */
        debug_StdPrintfDummy("壁が無いのにGetOrientOfWallが呼ばれました\n");
    } else {
        trap = (int *)1;
        buf[0] = -GetTableSin((short)-*(unsigned short *)((char *)wallEnt + 0x44));
        buf[1] = 0.0f;
        buf[2] = GetTableCos((short)-*(unsigned short *)((char *)wallEnt + 0x44));
        buf[3] = 1.0f;
    }
    if (trap == 0) {
        CopyVector((void *)out, (void *)buf);
        *trap = 0;
        return;
    }
    *(int *)&buf[3] = 0;
    {
        int *sub = (int *)(int)GOBJ_SUB(obj);
        if (sub != 0 && *(int *)((char *)sub + 0xC) != 0) {
            if (*(int *)((char *)sub + 0x78) != 0) {
                int *p5 = (int *)src->obj;
                int idx = src->node;
                int *o3 = (int *)(int)GOBJ_SUB(p5);
                sceVu0ApplyMatrix(out, (void *)(*(int *)((char *)o3 + 0xC) + (idx << 6)), buf);
                return;
            }
            CopyVector((void *)out, (void *)buf);
            return;
        }
        /* "GetOrientOfWall was called for an object with no DOBJ" */
        debug_StdPrintfDummy("DOBJ無しのオブジェクトに対してGetOrientOfWallが呼ばれました\n");
    }
}

void DrawCollisionRay(ClipWork *ray)
{
    sceVu0IVECTOR c0 = {64, 64, 64, 128};
    sceVu0IVECTOR c1 = {8, 16, 32, 128};
    float d[4];
    float p1[4];
    float p0[4];
    float v[4];
    float len;

    gif_StartPacketPri(11);
    MatrixDrive_PushMatrix();
    memset(v, 0, 16);
    v[3] = 1.0f;
    gif_SetAlpha(1, 5, 128);
    gif_SetZTest(1);
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    DrawLineG(ray->pt[0], c0, ray->pt[1], c0, 0);
    DrawLineG(ray->pt[0], c1, ray->pt[1], c1, -1);
    sceVu0SubVector(d, ray->pt[1], ray->pt[0]);
    MatrixDrive_TransMatrixV(ray->pt[1]);
    MatrixDrive_TurnYObjectMatrixXZ(d[0], d[1], d[2]);
    len = FSqrt(sceVu0InnerProduct(d, d));
    v[0] = len * 0.05f;
    v[1] = len * 0.3f;
    sceVu0ApplyMatrix(p0, MatrixDrive_GetMatrix(), v);
    v[0] = -len * 0.05f;
    sceVu0ApplyMatrix(p1, MatrixDrive_GetMatrix(), v);
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    DrawLineG(p0, c0, ray->pt[1], c0, 0);
    DrawLineG(p1, c0, ray->pt[1], c0, 0);
    DrawLineG(p0, c1, ray->pt[1], c1, -1);
    DrawLineG(p1, c1, ray->pt[1], c1, -1);
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    MatrixDrive_TransMatrixV(ray->pt[2]);
    MatrixDrive_TurnYObjectMatrixXZ(d[0], d[1], d[2]);
    MatrixDrive_PopMatrix();
    gif_EndPacket();
}

inline int CompareAttribute(unsigned int a, unsigned int b)
{
    int i;
    if ((a & b) == 0)
        return 0;
    for (i = 0; i < 8; i++) {
        unsigned int da = (a >> (i * 4)) & 0xF;
        unsigned int db = (b >> (i * 4)) & 0xF;
        if (da != 0 && db != 0 && da == db)
            return 1;
    }
    return 0;
}

inline int GetWallAttribute(ClipWork *w)
{
    if (w->wall.elem == 0)
        return 0;
    return w->attr;
}

inline int GetFloorAttribute(ClipWork *w)
{
    if (w->floor.elem == 0)
        return 0;
    return w->attr;
}

void MakeExitAttributeIndex(void)
{
    int i;
    char *entry;
    int j;
    int *p70;
    void *obj;
    int slot;

    debug_StdPrintfDummy("MakeExitAttributeIndex() %d\n", frame_count);
    exitAttrNum = 0;
    i = 0xF;
    do {
        exitAttr[i] = 0;
        i--;
    } while (i >= 0);
    colObjNum = 0;
    obj = colObjList[0];
    if (colObjListNum > 0) {
        do {
            p70 = (int *)GOBJ_SUB(obj)->colData;
            for (j = 0; j < p70[0xC / 4]; j++) {
                entry = ICO_EEPTR(char *, p70[0x14 / 4]) + j * 0x70;
                slot = *(int *)(entry + 0x60) & 0xF;
                if (slot != 0) {
                    if (exitAttr[slot] == 0) {
                        debug_StdPrintfDummy("attr EXIT%2d\n", slot);
                        exitAttrNum = exitAttrNum + 1;
                        exitAttr[slot] = entry;
                    }
                }
            }
            colObjNum = colObjNum + 1;
            obj = colObjList[colObjNum];
        } while (colObjNum < colObjListNum);
    }
}

inline int PositionOfExit(float *pos, int attr)
{
    void *v = exitAttr[attr & 0xF];
    if (v != 0) {
        CopyVector(pos, v);
        return 0;
    }
    return 1;
}

void ClipFloorByGObj(ClipWork *p, GObj *gobj)
{
    float buf0[4];
    float buf1[4];
    float mtx[16];
    FcPlane keep;
    int (*clip)(ClipWork *, GObj *, int);
    char *m;
    float *ep;
    float *pos;

    clip = clipMode[12].func;
    sceVu0CopyVector(buf0, p->pt[0]);
    sceVu0CopyVector(buf1, p->pt[1]);
    pos = p->pt[2];
    sceVu0CopyVector(pos, p->pt[1]);
    curFuzio = (FuzioCtx *)GOBJ_SUB(gobj)->colData;
    ep = pos;
    CopyVector(&keep, ep);
    CopyVector(p->pt[0], buf0);
    /* pos now names the start point: the DEBUG build traces the segment
     * (pos to ep) once it is in the object's space */
    pos = p->pt[0];
    m = (char *)GOBJ_SUB(gobj)->nodeMtx;
    MatrixDrive_SetTransposeMatrix(mtx, m);
    p->pt[0][3] = p->pt[2][3] = 1.0f;
    _ApplyMatrix(p->pt[0], mtx, p->pt[0]);
    _ApplyMatrix(ep, mtx, ep);
#ifdef DEBUG
    DBG_VECTOR(pos);
    DBG_VECTOR(ep);
#endif
    makeCollisionBlockTable(p->pt[0]);
    if (clip(p, gobj, 0)) {
        p->pt[2][3] = 1.0f;
        _ApplyMatrix(ep, m, ep);
    } else {
        CopyVector(ep, &keep);
    }
    if (p->floor.elem != 0) {
        Vec16 *n;

        CopyVector(&keep, &((FcFloorEnt *)p->floor.elem)->nx);
        keep.i[3] = 0;
        CopyMatrix(mtx, (char *)p->floor.o.obj->dobj->nodeMtx + (p->floor.o.node << 6));
        if (p->floor.o.obj->dobj->colRotate == 0) {
            UnitRotation(mtx);
        }
        n = &p->normal;
        sceVu0ApplyMatrix(n, mtx, &keep);
        n->f[3] = -sceVu0InnerProduct(n, ep);
        p->attr = ((FcFloorEnt *)p->floor.elem)->attr;
    }
    sceVu0CopyVector(p->pt[0], buf0);
    sceVu0CopyVector(p->pt[1], buf1);
}
