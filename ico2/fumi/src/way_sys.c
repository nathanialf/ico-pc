#include "debug.h"
#include "way_llf.h"
#include "way_sys.h"
#include <libvu0.h>
#include "typedef.h"
#include "Matrix.h"
#include "main.h"
#include "matrixDrive.h"
#include "geometryManager.h"
#include "tableSin.h"
#include "fuzio.h"
#include "way_util.h"
#include "fieldCollision.h"
#include "gamesys.h"

WayPoint *_FUNC_GetWay_begin(float *from, WVTObj *w, float *goal, int threaded)
{
    WayPoint *(*findTemp)(float *, int);
    WayPoint *(*findGid)(float *, int);
    int (*findPath)(int, int, WgAll *);
    WayPoint *ret;
    WgAll *work;
    WayPoint *wp0;
    WayPoint *wp;
    int g0;
    int g1;
    int r;
    int gid;
    WayPoint *wpn;

    ret = 0;
    work = WayUtilWorkAlloc();
    if (threaded) {
        findTemp = visible_waypoint_of_all_except_temp_ThreadVersion;

        findGid = visible_waypoint_of_all_except_gid_ThreadVersion;

        findPath = shortest_path_ThreadVersion;
    } else {
        findTemp = visible_waypoint_of_all_except_temp;
        findGid = visible_waypoint_of_all_except_gid;
        findPath = shortest_path;
    }

    wp0 = findTemp(from, -1);
    if (wp0 == 0) {
        goto out;
    }

    if ((unsigned int)w->stampFrame < (unsigned int)(lock_execIcoMisc - 1) || w->nearWp == 0) {
        if (w->guideFirst >= 0) {
            wp = findTemp(goal, way_point[w->guideFirst].group);
        } else {
            wp = findTemp(goal, -1);
        }

        w->nearWp = wp;
    } else {
        if (w->guideFirst >= 0) {
            wp = findTemp(w->nearWp->pos, way_point[w->guideFirst].group);
        } else {
            wp = findTemp(w->nearWp->pos, -1);
        }
    }

    if (wp == 0) {
        goto out;
    }

    if (way_group[wp->group].active == 0) {
        wp = findGid(goal, wp->group);
        if (wp == 0) {
            goto out;
        }
    }

    w->nearWp = wp;
    w->fromWp = wp0;
    w->stampFrame = lock_execIcoMisc;
    DeleteGuideWay(w);
    debug_StdPrintfDummy("GetWay_begin\n");
    debug_StdPrintfDummy("gid t:%d m:%d\n", wp0->group, wp->group);

    w->group = wp->group;
    sceVu0CopyVector(w->pos, from);

    g0 = wp0->group;
    g1 = wp->group;

    w->reached = 0;
    w->guideFirst = -1;
    w->flag6C = 0;
    w->pathKind = 0;
    if (g0 == g1) {
        debug_StdPrintfDummy("same_group\n");
        w->avoiding = 0;
        w->flag3C = 0;
        w->direction = short_direction_between_wp(wp0, wp);
        if (w->direction == -2) {
            goto out;
        }
        w->chk.cur = wp;
        w->chk.start = wp0;
        w->chk.cross = 0;

        ret = wp;
        goto out;
    }

    debug_StdPrintfDummy("other_group\n");
    r = findPath(g0, g1, work);
    if (r == -1) {
        w->pathKind = 1;

        if (way_group[g1].active == 0) {
            goto out;
        }
        r = GetWgAll(g0, g1, work);
        if (r == -1) {
            w->pathKind = 2;
        } else {
            gid = NearestWgFromTarget(g0, g1, work);
            /* a compiled-out print, see the note at the `r < 0` test below */
            if (0) {
                debug_StdPrintfDummy("gid:%d = tgid:%d, mgid:%d\n", gid, g1, g0);
            }
            wpn = nearest_waypoint_of_group(from, gid);

            g0 = gid;
            wp0 = wpn;
            sceVu0CopyVector(w->pos, wp0->pos);
            if (g0 == g1) {
                debug_StdPrintfDummy("same_group\n");
                w->avoiding = 0;
                w->flag3C = 0;
                w->direction = short_direction_between_wp(wp0, wp);
                if (w->direction == -2) {
                    goto out;
                }
                w->chk.cur = wp;
                w->chk.start = wp0;
                w->chk.cross = 0;

                w->flag6C = 1;

                ret = wp;
                goto out;
            }
        }
    }

    w->avoiding = 1;
    w->flag3C = 1;
    if (r == g0) {
        WayGroup *a = &way_group[wp->group];
        WayGroup *b = &way_group[wp0->group];

        switch (a->bridge) {
        case 1:
            if (wp == a->first) {
                if (way_point[a->end[0]].group == r) {
                    w->flag3C = 0;
                }
            } else if (wp == a->last) {
                if (way_point[a->end[1]].group == r) {
                    w->flag3C = 0;
                }
            }
            break;

        case 0:
            if (wp->index == b->end[0]) {
                w->flag3C = 0;
            } else if (wp->index == b->end[1]) {
                w->flag3C = 0;
            }
            break;
        }
    }

    if (r < 0) {
        /* prints compiled out of the retail build; their strings stay in
           .rodata */
        if (0) {
            debug_StdPrintfDummy("!!cant reach!!\n");
        }
        goto out;
    }
    set_check_wp(&w->chk, r, g1);
    w->chk.cur = wp;

    debug_StdPrintfDummy("wp:%p %p\n", w->chk.start, wp);
    debug_StdPrintfDummy("gid:%d %d\n", w->chk.start->group, wp->group);
    w->direction = short_direction_between_wp(w->chk.start, wp);
    debug_StdPrintfDummy("direction:%d\n", w->direction);
    ret = w->chk.cur;

out:
    WayUtilWorkFree(work);
    return ret;
}

inline WayPoint *GetWay_begin(float *from, WVTObj *w, float *goal)
{
    return _FUNC_GetWay_begin(from, w, goal, 0);
}

typedef float WayVec[4] __attribute__((aligned(16))); /* derived name */

/* a wall probe between two points, both lifted 75 units */
static inline struct FcWallEnt *way_probe(float *a, float *b) /* derived name */
{
    ClipWork cc;
    WayVec off;

    off[0] = 0.0f;
    off[1] = -75.0f;
    off[2] = 0.0f;
    off[3] = 0.0f;

    cc.radius = 0;
    sceVu0AddVector(cc.pt[0], a, off);
    sceVu0AddVector(cc.pt[1], b, off);
    ClipWall(&cc);
    return cc.wall.elem;
}

static int avoid_obstacle2(float *pos, float *wp, WVTObj *w)
{
    ClipWork cc;
    WayVec box[4];
    WayVec rp;
    WayVec off;
    int ids[3];
    int hold;
    GObj *obj;
    int g;
    int i;
    int k;
    int ofs = 0;
    float best;
    float d;

    hold = 0;
    sceVu0CopyVector(cc.pt[0], pos);
    sceVu0CopyVector(cc.pt[1], w->chk.cur->pos);
    cc.radius = 20.0f;
    ClipWall(&cc);
    if (cc.wall.elem == 0) {
        return 0;
    }
    obj = cc.wall.o.obj;
    if (objLayout[obj->labelId].kind != 17) {
        return 0;
    }

    GetRootPosition(rp, obj);

    if (absf(rp[0] - wp[0]) <= 100.0f) {
        if (absf(rp[2] - wp[2]) <= 100.0f) {
            if (rp[1] + 50.0f - wp[1] > 95.0f) {
                return 0;
            }
            debug_StdPrintfDummy("skip wp\n");

            if (w->chk.cur != w->chk.start || w->avoiding != 0) {
                return 1;
            }

            hold = 1;
        }
    }

    if (w->guideFirst >= 0) {
        debug_StdPrintfDummy("delete guide point at avoid\n");

        DeleteWayGroup(way_point[w->guideFirst].group);
        w->guideFirst = -1;
    }

    best = 10000.0f;
    GetRootPosition(rp, obj);
    k = 0;
    off[0] = 100.0f;
    off[1] = 50.0f;
    off[2] = 100.0f;
    off[3] = 0.0f;
    sceVu0AddVector(box[0], rp, off);
    off[0] = 100.0f;
    off[1] = 50.0f;
    off[2] = -100.0f;
    off[3] = 0.0f;
    sceVu0AddVector(box[1], rp, off);
    off[0] = -100.0f;
    off[1] = 50.0f;
    off[2] = -100.0f;
    off[3] = 0.0f;
    sceVu0AddVector(box[2], rp, off);
    off[0] = -100.0f;
    off[1] = 50.0f;
    off[2] = 100.0f;
    off[3] = 0.0f;
    sceVu0AddVector(box[3], rp, off);

    for (i = 0; i < 4; i++) {
        d = _GetLength(box[i], pos);
        if (d < best) {
            best = d;

            k = i;
        }
    }

    /* the right-hand chain's first probe reads its second corner as
       box[k += ofs], ofs a zero corner offset */
    if (way_probe(box[(k + 3) % 4], box[k]) == 0 && way_probe(box[k], box[(k + 1) % 4]) == 0 &&
        way_probe(box[(k + 1) % 4], box[(k + 2) % 4]) == 0) {
        int j;

        g = CreateTempWayGroup();
        for (j = 0; j < 3; j++) {
            ids[j] = CreateWayPoint(box[(k + j) % 4]);
            AddWayPoint(g, ids[j]);
        }
        debug_StdPrintfDummy("left way %d\n", g);

    } else if (way_probe(box[(k + 1) % 4], box[k += ofs]) == 0 &&
               way_probe(box[k], box[(k + 3) % 4]) == 0 &&
               way_probe(box[(k + 3) % 4], box[(k + 2) % 4]) == 0) {
        int j;

        g = CreateTempWayGroup();
        for (j = 0; j < 3; j++) {
            ids[j] = CreateWayPoint(box[(k + 4 - j) % 4]);
            AddWayPoint(g, ids[j]);
        }
        debug_StdPrintfDummy("right way %d\n", g);

    } else {
        off[0] = 0.0f;
        off[1] = -50.0f;
        off[2] = 0.0f;
        off[3] = 0.0f;
        sceVu0AddVector(off, off, rp);
        g = CreateTempWayGroup();
        ids[0] = CreateWayPoint(off);
        AddWayPoint(g, ids[0]);
        ids[2] = ids[0];
        debug_StdPrintfDummy("up way %d\n", g);
    }

    w->guideFirst = ids[0];
    w->chk.cross = w->chk.cur;
    w->chk.start = &way_point[ids[2]];
    w->chk.cur = &way_point[ids[0]];

    w->avoiding = 1;
    w->direction = 1;

    if (hold == 1) {
        w->chk.cross = 0;
        w->avoiding = 0;
    }

    return 0;
}

static void create_box_bridge(char *g)
{
    ClipWork cc;
    WayVec pos;
    WayVec start;
    WayVec end;
    WayVec off;
    WayVec wp[3];
    int i;
    int j;
    int id;

    GetRootPosition(pos, g);
    for (i = 0; i < 4; i++) {
        sceVu0CopyVector(start, pos);
        off[0] = GetTableCos((short)(i * 0x4000)) * 75.0f;
        off[2] = -GetTableSin((short)(i * 0x4000)) * 75.0f;
        off[1] = 0.0f;
        sceVu0AddVector(end, pos, off);

        sceVu0CopyVector(cc.pt[0], start);
        sceVu0CopyVector(cc.pt[1], end);
        cc.radius = 0;
        ClipWall(&cc);
        if (cc.wall.elem == 0) {
            continue;
        }

        sceVu0CopyVector(cc.pt[0], cc.pt[1]);
        cc.pt[1][1] = cc.pt[1][1] - 175.0f;
        ClipFloorR(&cc);
        sceVu0CopyVector(wp[0], cc.pt[2]);
        if (cc.floor.elem == 0) {
            continue;
        }

        sceVu0CopyVector(cc.pt[0], start);
        sceVu0SubVector(cc.pt[1], pos, off);
        ClipWall(&cc);
        sceVu0CopyVector(wp[2], cc.pt[2]);
        wp[2][1] = pos[1] + 50.0f;
        if (cc.wall.elem != 0) {
            continue;
        }

        sceVu0CopyVector(wp[1], pos);
        wp[1][1] = wp[1][1] - 50.0f;

        id = CreateWayGroup();
        for (j = 0; j < 3; j++) {
            AddWayPoint(id, CreateWayPoint(wp[j]));
        }
        set_bridge(id);
        way_group[id].boxBridge = 1;
        if (way_group[id].bridge == 0) {
            DeleteWayGroup(id);
        } else {
            WayPoint *a = &way_point[way_group[id].end[0]];
            WayPoint *b = &way_point[way_group[id].end[1]];

            if (a->group == b->group) {
                DeleteWayGroup(id);
            }
        }
    }
}

inline void BridgeBox(void) {}

/* a wall probe between `pos` and a way point, both lifted 75 units, with a
   30-unit radius */
static __inline__ struct FcWallEnt *way_wall_between(float *pos, WayPoint *wp) /* derived name */
{
    ClipWork cc;
    WayVec off;
    float *p = (float *)(wp->pos);

    off[0] = 0.0f;
    off[1] = -75.0f;
    off[2] = 0.0f;
    off[3] = 0.0f;
    cc.radius = 30.0f;
    sceVu0AddVector(cc.pt[0], pos, off);
    sceVu0AddVector(cc.pt[1], p, off);
    ClipWall(&cc);
    if (cc.wall.elem == 0) {
        ClipWallField(&cc);
    }
    return cc.wall.elem;
}

inline void DeleteGuideWay(WVTObj *o)
{
    if (o->guideFirst >= 0) {
        debug_StdPrintfDummy("delete guide point group:%d\n", o->guideFirst);
        {
            WayPoint *e = &way_point[o->guideFirst];
            DeleteWayGroup(e->group);
        }
        o->guideFirst = -1;
    }
}

/* the object whose wall collision GetWay_next draws, for debugging */
static void *wayDebugWallGObj = 0; /* derived name */

WayPoint *GetWay_next(WVTObj *w, float *pos)
{
    WayVec dv;
    WayPoint *cur;
    WayPoint *nxt;
    float *p;
    int blocked;
    float lim;

    w->stampFrame = lock_execIcoMisc;
    if (w->chk.cur == 0 || w->chk.cur->used == 0) {
        debug_StdPrintfDummy("illigal way ");
        return 0;
    }
    ez_circle(w->nearWp->pos, pos, 0x80800000, 30.0f);

    if (wayDebugWallGObj != 0) {
        DrawGObjWallCollision(wayDebugWallGObj, 0x800000);
    }

    if (w->chk.cur != 0) {
        ez_line(pos, w->chk.cur->pos, 0);
        ez_circle(w->chk.cur->pos, pos, 0x80000080, w->chk.cur->radius);
    }

    if (w->chk.start != 0) {
        ez_line(pos, w->chk.start->pos, 1);
        ez_circle(w->chk.start->pos, pos, 0x80008000, w->chk.start->radius);
    }

    if (w->chk.cross != 0) {
        ez_line(pos, w->chk.cross->pos, 2);
        ez_circle(w->chk.cross->pos, pos, 0x80800000, w->chk.cross->radius);
    }

    blocked = avoid_obstacle2(pos, w->chk.cur->pos, w);

    switch (w->avoiding) {
    case 1:
        /* compiled-out prints, see the note before the second switch below */
        if (0) {
            debug_StdPrintfDummy("WGROUP STAT OTHER\n");
        }
        if (w->chk.cur != w->chk.cross && way_group[w->chk.cur->group].bridge == 0) {
            if (way_wall_between(pos, w->chk.cross) == 0) {
                w->chk.cur = w->chk.cross;
                blocked = 0;
                debug_StdPrintfDummy("short cut 2:%p\n", w->chk.cur);
                if (w->guideFirst >= 0) {
                    debug_StdPrintfDummy("delete guide point\n");
                    DeleteWayGroup(way_point[w->guideFirst].group);
                    w->guideFirst = -1;
                }
                break;
            }
            nxt = w->chk.start;
            while (nxt != w->chk.cur && w->guideFirst < 0) {
                if (way_wall_between(pos, nxt) == 0) {
                    w->chk.cur = nxt;
                    blocked = 0;
                    debug_StdPrintfDummy("short cut 1:%p\n", nxt);
                    break;
                }
                nxt = waypoint_bidirectional_list(nxt, w->direction ^ 1);
            }
        }
        break;

    case 0:
        if (0) {
            debug_StdPrintfDummy("WGROUP STAT SAME\n");
        }
        nxt = w->chk.start;
        while (nxt != w->chk.cur) {
            if (way_wall_between(pos, nxt) == 0) {
                w->chk.cur = nxt;
                blocked = 0;
                debug_StdPrintfDummy("short cut 1:%p\n", nxt);
                break;
            }
            nxt = waypoint_bidirectional_list(nxt, w->direction ^ 1);
            if (nxt == 0) {
                nxt = w->chk.cur;
            }
        }
        break;
    }

    cur = w->chk.cur;
    p = cur->pos;
    ez_line(p, pos, 0xFF000080);
    ez_line(p, w->chk.start->pos, 0xFF80);

    sceVu0SubVector(dv, p, pos);
    sceVu0Normalize(w->nrm, dv);

    if (cur->radius == 0.0f) {
        lim = 50.0f;
    } else {
        lim = cur->radius * 1.5f;
    }

    if (blocked == 0 && lim < fzMagnitudefv(dv)) {
        return cur;
    }

    /* prints compiled out of the retail build; their strings stay in .rodata.
       The two state prints open the first switch's arms (avoiding 1 is the
       other-group state, 0 the same-group one). */
    if (0) {
        debug_StdPrintfDummy("wp %p myway %p pos %p\n", cur, w->chk.start, pos);
        debug_StdPrintfDummy("wgroup stat:%d\n", w->avoiding);
    }
    switch (w->avoiding) {
    case 0:
        if (cur == w->chk.start) {
            w->reached = 1;
            DeleteGuideWay(w);
            return cur;
        }
        break;

    case 1:
        if (cur == w->chk.cross) {
            w->reached = 1;
            DeleteGuideWay(w);
            w->nearWp = w->chk.cross;
            return cur;
        }
        if (cur == w->chk.start) {
            debug_StdPrintfDummy("goal wp1\n");
            w->chk.cur = w->chk.cross;
            if (w->guideFirst >= 0) {
                debug_StdPrintfDummy("delete guide point\n");
                DeleteWayGroup(way_point[w->guideFirst].group);
                w->guideFirst = -1;
            }
            w->nearWp = w->chk.start;
            return w->chk.cur;
        }
        break;
    }

    w->chk.cur = waypoint_bidirectional_list(cur, w->direction);
    /* prints compiled out of the retail build; their strings stay in
       .sdata */
    if (0) {
        debug_StdPrintfDummy("reset\n");
        debug_StdPrintfDummy("hit\n");
        debug_StdPrintfDummy("free\n");
        debug_StdPrintfDummy("fail\n");
        debug_StdPrintfDummy("ev:%f\n", lim);
        debug_StdPrintfDummy("dst %p\n", w->chk.cur);
        debug_StdPrintfDummy("->%p\n", cur);
    }
    debug_StdPrintfDummy("bilist:%p\n", w->chk.cur);
    w->nearWp = w->chk.cur;
    return w->nearWp;
}

/* One candidate escape point: the way point id and the path length to it. */
typedef struct NigeEnt { /* field names derived */
    int id;
    float d;
} NigeEnt; /* derived name */

/* The TU's whole .bss: one entry per way point (way_llf's 275), which
   GetNearNigePointN fills and sorts by path length. */
static NigeEnt nigePointTbl[275]; /* derived name */

static __inline__ void nige_swap(NigeEnt *tbl, int a, int b) /* derived name */
{
    NigeEnt t = tbl[a];

    tbl[a] = tbl[b];
    tbl[b] = t;
}

static __inline__ int nige_add(NigeEnt *tbl, int n, WayPoint *e, float d) /* derived name */
{
    if (e->escape != 0) {
        tbl[n].id = e->index;
        tbl[n].d = d;
        n++;
    }
    return n;
}

int GetNearNigePointN(void *out, int num, WVTObj *w, float *pos)
{
    WayPoint *n;
    WayGroup *gb;
    WayPoint *m;
    WayPoint *a;
    float *bp;
    float d;
    float da;
    int i;
    int j;

    int cnt = 0;

    WayPoint *base = visible_waypoint_of_all_except_temp(pos, -1);
    WayGroup *ga = &way_group[base->group];

    w->escapeFound = 0;

    gb = &way_group[base->group];
    if (gb->bridge != 0) {
        n = base;
        d = _GetLength(pos, base->pos);
        while (n != 0) {
            if (n->prev != 0) {
                d += _GetLength(n->pos, n->prev->pos);
            }
            n = n->prev;
        }

        m = &way_point[gb->end[0]];
        d += _GetLength(gb->first->pos, m->pos);
        cnt = nige_add(nigePointTbl, cnt, m, d);

        n = base;
        bp = base->pos;
        d = _GetLength(pos, bp);
        if (n != 0) {
            d += _GetLength(pos, base->pos);
        }
        while (n != 0) {
            if (n->next != 0) {
                d += _GetLength(n->pos, n->next->pos);
            }
            n = n->next;
        }

        m = &way_point[gb->end[1]];
        d += _GetLength(gb->last->pos, m->pos);
        cnt = nige_add(nigePointTbl, cnt, m, d);
    } else {
        switch (ga->closed) {
        case 0:
            n = base;
            d = _GetLength(pos, base->pos);
            while (n != 0) {
                cnt = nige_add(nigePointTbl, cnt, n, d);
                if (n->prev != 0) {
                    d += _GetLength(n->pos, n->prev->pos);
                }
                n = n->prev;
            }

            bp = base->pos;
            d = _GetLength(pos, bp);
            if (base != 0) {
                if (base->next != 0) {
                    d += _GetLength(base->pos, base->next->pos);
                }
            }
            n = base->next;
            while (n != 0) {
                cnt = nige_add(nigePointTbl, cnt, n, d);
                if (n->next != 0) {
                    d += _GetLength(n->pos, n->next->pos);
                }
                n = n->next;
            }
            break;

        case 1:
            a = base;
            n = base;
            da = _GetLength(pos, base->pos);
            d = da;
            do {
                if (da <= d) {
                    da += _GetLength(a->pos, a->prev->pos);
                    a = a->prev;
                    cnt = nige_add(nigePointTbl, cnt, a, da);
                } else {
                    d += _GetLength(n->pos, n->next->pos);
                    n = n->next;
                    cnt = nige_add(nigePointTbl, cnt, n, d);
                }
            } while (a != n);
            break;
        }

        for (gb = WayBridge_begin(); gb != 0; gb = WayBridge_next(gb)) {
            if (gb->end[0] == base->index || gb->end[1] == base->index) {
                d = _GetLength(base->pos, gb->first->pos);
                m = gb->first;
                while (m->next != 0) {
                    d += _GetLength(m->pos, m->next->pos);
                    m = m->next;
                }
                d += _GetLength(gb->last->pos, way_point[gb->end[1]].pos);
                if (gb->end[0] == base->index) {
                    cnt = nige_add(nigePointTbl, cnt, &way_point[gb->end[1]], d);
                } else {
                    cnt = nige_add(nigePointTbl, cnt, &way_point[gb->end[0]], d);
                }
                w->escapeFound = 1;
            }
        }

        cnt = nige_add(nigePointTbl, cnt, base, 0.0f);
    }

    for (i = 0; i < num; i++) {
        for (j = cnt - 1; j > i; j--) {
            if (nigePointTbl[j].d < nigePointTbl[j - 1].d) {
                nige_swap(nigePointTbl, j, j - 1);
            }
        }
        CopyVector((char *)out + i * 16, way_point[nigePointTbl[i].id].pos);
    }
    return cnt;
}
