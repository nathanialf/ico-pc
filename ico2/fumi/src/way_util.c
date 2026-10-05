#include "debug.h"
#include "act.h"
#include "way_llf.h"
#include "way_util.h"
#include "geometryManager.h"
#include <stdlib.h>
#include <libvu0.h>
#include "fuzio.h"
#include "memory.h"
#include "ios.h"
#include <string.h>
#include "debug_exception.h"
#include "fieldCollision.h"
#include <assert.h>

typedef struct WpSortEnt { /* field names derived */
    WayPoint *wp;
    float d;
} WpSortEnt; /* derived name */

/* the qsort comparator the two visible_waypoint searches sort by */
static inline int wpsort_compfnc(WpSortEnt *p, WpSortEnt *q);

/* the name every iosMallocDebug and assert in this file reports itself under */
static const char wayUtilFile[] = "src/way_util.c"; /* derived name */

/* set while the way tool loads or saves */
int load_save_flag = 0;

/* the red, green and blue the waypoint debug draw uses for the three axes;
   no retail code reads the table */
static const int axisColor[3][4] = {
    {128, 0, 0, 128},
    {0, 128, 0, 128},
    {0, 0, 128, 128},
}; /* derived name */

/* The body of visible_waypoint_of_all_except_gid and of its _ThreadVersion,
   one shared static inline helper taking the thread flag: the thread build
   keeps the _ACTWait arms. */
static inline WayPoint *visible_waypoint_of_all_except_gid_sub(float *pos, int gid,
                                                               int thread) /* derived name */
{
    float buf[4];
    ClipWork cb;
    WpSortEnt *tbl;
    WayPoint *wp;
    WayPoint *ret;
    int n;
    int i;

    tbl = iosMallocDebug(ios_partition_sugipon, 275 * sizeof(WpSortEnt), wayUtilFile, 313);

    n = 0;
    for (wp = WayPoint_begin(); wp != 0; wp = WayPoint_next(wp)) {
        if (wp->group != gid) {
            float d;

            sceVu0SubVector(buf, wp->pos, pos);
            d = fzMagnitudefv(buf);
            tbl[n].wp = wp;
            tbl[n].d = d;
            n++;
        }
    }

    qsort(tbl, n, sizeof(WpSortEnt), (int (*)(const void *, const void *))wpsort_compfnc);
    if (thread) {
        _ACTWait(1);
    }
    cb.radius = 0.0f;

    ret = 0;
    for (i = 0; i < n; i++) {
        wp = tbl[i].wp;
        sceVu0CopyVector(cb.pt[0], pos);
        sceVu0CopyVector(cb.pt[1], wp->pos);
        cb.pt[0][1] -= 75.0f;
        cb.pt[1][1] -= 75.0f;
        ClipWall(&cb);
        if (cb.wall.elem == 0) {
            if (thread) {
                _ACTWait(1);
            }
            ClipWallField(&cb);
            if (cb.wall.elem == 0) {
                ret = wp;
                break;
            }
        }
        if (thread) {
            _ACTWait(1);
        }
    }

    iosFree(tbl);
    return ret;
}

WayPoint *visible_waypoint_of_all_except_gid(float *pos, int gid)
{
    return visible_waypoint_of_all_except_gid_sub(pos, gid, 0);
}

WayPoint *visible_waypoint_of_all_except_gid_ThreadVersion(float *pos, int gid)
{
    return visible_waypoint_of_all_except_gid_sub(pos, gid, 1);
}

/* Same two-wrapper shape as the pair above, for
   visible_waypoint_of_all_except_temp and its _ThreadVersion. */
static inline WayPoint *visible_waypoint_of_all_except_temp_sub(float *pos, int gid,
                                                                int thread) /* derived name */
{
    float buf[4];
    ClipWork cb;
    WpSortEnt *tbl;
    WayPoint *wp;
    WayPoint *ret;
    int n;
    int i;

    tbl = iosMallocDebug(ios_partition_sugipon, 275 * sizeof(WpSortEnt), wayUtilFile, 383);

    n = 0;
    for (wp = WayPoint_begin(); wp != 0; wp = WayPoint_next(wp)) {
        int g = wp->group;

        if (way_group[g].temp == 0 || g == gid) {
            float d;

            sceVu0SubVector(buf, wp->pos, pos);
            d = fzMagnitudefv(buf);
            tbl[n].wp = wp;
            tbl[n].d = d;
            n++;
        }
    }

    qsort(tbl, n, sizeof(WpSortEnt), (int (*)(const void *, const void *))wpsort_compfnc);
    if (thread) {
        _ACTWait(1);
    }
    cb.radius = 0.0f;

    ret = 0;
    for (i = 0; i < n; i++) {
        wp = tbl[i].wp;
        sceVu0CopyVector(cb.pt[0], pos);
        sceVu0CopyVector(cb.pt[1], wp->pos);
        cb.pt[0][1] -= 75.0f;
        cb.pt[1][1] -= 75.0f;
        ClipWall(&cb);
        if (cb.wall.elem == 0) {
            if (thread) {
                _ACTWait(1);
            }
            ClipWallField(&cb);
            if (cb.wall.elem == 0) {
                ret = wp;
                break;
            }
        }
        if (thread) {
            _ACTWait(1);
        }
    }

    iosFree(tbl);
    return ret;
}

WayPoint *visible_waypoint_of_all_except_temp(float *pos, int gid)
{
    return visible_waypoint_of_all_except_temp_sub(pos, gid, 0);
}

WayPoint *visible_waypoint_of_all_except_temp_ThreadVersion(float *pos, int gid)
{
    return visible_waypoint_of_all_except_temp_sub(pos, gid, 1);
}

void ez_line(void *a, void *b, unsigned int col)
{
    volatile int local[12];
}

void ez_circle(void *pos, void *base, unsigned int col, float r)
{
    volatile int local[12];
}

/* void * (void *, int, int) here, void * (void *, int, unsigned int) in string.h */
#ifndef ICO_HOST

extern void *memset(void *dst, int c, int n);

#endif

int short_direction_between_wp(WayPoint *from, WayPoint *to)
{
    float len[2];
    WayPoint *wp;
    WayPoint *nxt;
    int dir;

    memset(len, 0, 8);
    dir = -1;

    for (wp = WayPointList_begin(to->group); wp != 0; wp = WayPointList_next(wp)) {
        if (wp == from) {
            dir = 0;
            break;
        }
        if (wp == to) {
            dir = 1;
            break;
        }
        nxt = wp->next;
        if (nxt != 0) {
            len[0] += fzMagnitude2fv(wp->pos, nxt->pos);
        }
    }

    for (; wp != 0; wp = WayPointList_next(wp)) {
        if (dir == 1 && wp == from) {
            break;
        }
        if (dir == 0 && wp == to) {
            break;
        }
        nxt = wp->next;
        if (nxt != 0) {
            len[1] += fzMagnitude2fv(wp->pos, nxt->pos);
        }
    }

    if (wp == 0) {
        debug_StdPrintfDummy("not same group\n");
        debug_StdPrintfDummy("not same grp, %d\n", to->group);
        for (wp = WayPointList_begin(to->group); wp != 0; wp = WayPointList_next(wp)) {
            debug_StdPrintfDummy("wp:%p %d\n", wp, wp->index);
        }
        return -2;
    }

    if (way_group[wp->group].closed == 0) {
        return dir;
    }

    for (; wp != 0; wp = WayPointList_next(wp)) {
        nxt = wp->next;
        if (nxt != 0) {
            len[1] += fzMagnitude2fv(wp->pos, nxt->pos);
        }
    }

    if (len[0] < len[1]) {
        dir ^= 1;
    }
    return dir;
}

inline int direction_across_bridge(WayGroup *bridge, int gid)
{
    WayPoint *e1 = &way_point[bridge->end[0]];
    WayPoint *e2;
    if (e1->group == gid) {
        return 1;
    }
    e2 = &way_point[bridge->end[1]];
    if (e2->group != gid) {
        debug_StdPrintfDummy("abnormal bridge\n");
        debug_assert(wayUtilFile, 706);
        __assert(wayUtilFile, 706, "0");
    }
    return 0;
}

/* the bridge between two groups, which waybridge_between_group and
   wgid_next inline */
static inline WayGroup *bridgeBetweenGroups(int gidA, int gidB) /* derived name */
{
    WayGroup *p = WayBridge_begin();
    while (p != 0) {
        WayPoint *eA = &way_point[p->end[0]];
        WayPoint *eB = &way_point[p->end[1]];
        int a = eA->group;
        int b = eB->group;
        if (a == gidA && b == gidB) {
            return p;
        }
        if (b == gidA && a == gidB) {
            return p;
        }
        p = WayBridge_next(p);
    }
    return 0;
}

int wgid_next(int me, int target)
{
    WayGroup *p;

    switch (way_group[target].bridge) {
    case 0:
        for (p = WayBridge_begin(); p != 0; p = WayBridge_next(p)) {
            WayGroup *br;

            if (p->index == me) {
                int g = way_point[p->end[0]].group;
                debug_StdPrintfDummy("gid:%d\n", g);
                if (g == target) {
                    return me;
                }
                g = way_point[p->end[1]].group;
                debug_StdPrintfDummy("gid:%d\n", g);
                if (g == target) {
                    return me;
                }
            }

            br = bridgeBetweenGroups(me, target);
            if (br != 0) {
                debug_StdPrintfDummy("target is over bridge\n");
                return br->index;
            }
        }
        break;

    case 1: {
        WayGroup *wg = &way_group[target];

        int g = way_point[wg->end[0]].group;
        if (g == me) {
            return me;
        }
        return way_point[wg->end[1]].group;
    }
    }

    return -1;
}

WgAll *WayUtilWorkAlloc(void)
{
    WgAll *p = iosMallocDebug(ios_partition_sugipon, sizeof(WgAll), wayUtilFile, 857);
    int **q;
    int i;
    p->visited = iosMallocDebug(ios_partition_sugipon, 95, wayUtilFile, 859);
    p->costBuf = iosMallocDebug(ios_partition_sugipon, 94 * 94 * sizeof(int), wayUtilFile, 860);
    p->prev = iosMallocDebug(ios_partition_sugipon, 95 * sizeof(int), wayUtilFile, 861);
    p->prev2 = iosMallocDebug(ios_partition_sugipon, 95 * sizeof(int), wayUtilFile, 862);
    p->dist = iosMallocDebug(ios_partition_sugipon, 95 * sizeof(int), wayUtilFile, 863);
    p->dist2 = iosMallocDebug(ios_partition_sugipon, 95 * sizeof(int), wayUtilFile, 864);
    q = iosMallocDebug(ios_partition_sugipon, 94 * sizeof(int *), wayUtilFile, 866);
    p->cost = q;
    for (i = 0; i < 94; i++) {
        q[i] = p->costBuf + i * 94;
    }
    return p;
}

void WayUtilWorkFree(WgAll *self)
{
    iosFree(self->visited);
    iosFree(self->cost);
    iosFree(self->costBuf);
    iosFree(self->prev);
    iosFree(self->prev2);
    iosFree(self->dist);
    iosFree(self->dist2);
    iosFree(self);
}

/* The body of shortest_path and shortest_path_ThreadVersion, one shared
   static inline helper taking the thread flag: the thread build keeps the
   _ACTWait arms. */
static inline int shortest_path_sub(int from, int to, WgAll *w, int thread) /* derived name */
{
    char *visited = w->visited;
    int *prev = w->prev;
    int *dist = w->dist;
    int **cost = w->cost;
    WayGroup *p;
    int i, j;
    int next, best;

    for (i = 0; i < 94; i++) {
        for (j = 93; j >= 0; j--) {
            cost[i][j] = 0x7FFFFFFF;
        }
    }

    if (thread) {
        _ACTWait(1);
    }

    for (p = WayBridge_begin(); p != 0; p = WayBridge_next(p)) {
        int g1 = way_point[p->end[0]].group;
        int g2 = way_point[p->end[1]].group;
        int b = p->index;

        cost[g1][b] = 1;
        cost[g2][b] = 1;
        cost[b][g1] = 1;
        cost[b][g2] = 1;
    }

    if (thread) {
        _ACTWait(1);
    }

    for (i = 0; i < 94; i++) {
        visited[i] = 0;
        dist[i] = 0x7FFFFFFF;
    }

    if (thread) {
        _ACTWait(1);
    }

    dist[to] = 0;
    next = to;
    do {
        i = next;
        best = 0x7FFFFFFF;
        visited[i] = 1;
        for (j = 0; j < 94; j++) {
            if (visited[j]) {
                continue;
            }
            if (cost[i][j] < 0x7FFFFFFF && dist[i] + cost[i][j] < dist[j]) {
                dist[j] = dist[i] + cost[i][j];
                prev[j] = i;
            }
            if (dist[j] < best) {
                best = dist[j];
                next = j;
            }
        }
    } while (best < 0x7FFFFFFF);

    if (dist[from] >= 0x7FFFFFFF) {
        i = -1;
    } else {
        i = from;
        while (prev[i] != to) {
            i = prev[i];
        }
    }
    return i;
}

int shortest_path(int from, int to, WgAll *w)
{
    return shortest_path_sub(from, to, w, 0);
}

int shortest_path_ThreadVersion(int from, int to, WgAll *w)
{
    return shortest_path_sub(from, to, w, 1);
}

int GetWgAll(int from, int to, WgAll *w)
{
    char *visited = w->visited;
    int *prev = w->prev2;
    int *dist = w->dist2;
    int **cost = w->cost;
    WayGroup *p;
    int i, j;
    int next, best;

    for (i = 0; i < 94; i++) {
        for (j = 93; j >= 0; j--) {
            cost[i][j] = 0x7FFFFFFF;
        }
    }

    for (p = WayBridgeAll_begin(); p != 0; p = WayBridgeAll_next(p)) {
        int g1 = way_point[p->end[0]].group;
        int g2 = way_point[p->end[1]].group;
        int b = p->index;

        cost[g1][b] = 1;
        cost[g2][b] = 1;
        cost[b][g1] = 1;
        cost[b][g2] = 1;
    }

    for (i = 0; i < 94; i++) {
        visited[i] = 0;
        dist[i] = 0x7FFFFFFF;
    }

    dist[to] = 0;
    next = to;
    do {
        i = next;
        best = 0x7FFFFFFF;
        visited[i] = 1;
        for (j = 0; j < 94; j++) {
            if (visited[j]) {
                continue;
            }
            if (cost[i][j] < 0x7FFFFFFF && dist[i] + cost[i][j] < dist[j]) {
                dist[j] = dist[i] + cost[i][j];
                prev[j] = i;
            }
            if (dist[j] < best) {
                best = dist[j];
                next = j;
            }
        }
    } while (best < 0x7FFFFFFF);

    if (dist[from] >= 0x7FFFFFFF) {
        i = -1;
    } else {
        i = from;
        while (prev[i] != to) {
            i = prev[i];
        }
    }
    return i;
}

/* the end of a group on a given side: waypoint_connect_group_side_me and
   set_check_wp inline the first; set_check_wp inlines the second, a
   file-static copy of waypoint_connect_group_side_bridge */
static inline WayPoint *groupSideMe(WayGroup *bridge, int gid) /* derived name */
{
    WayPoint *e = &way_point[bridge->end[0]];
    if (e->group == gid)
        return e;
    e = &way_point[bridge->end[1]];
    return e->group == gid ? e : 0;
}

static inline WayPoint *groupSideBridge(WayGroup *bridge, int gid) /* derived name */
{
    WayPoint *e = &way_point[bridge->end[0]];
    if (e->group == gid)
        return bridge->first;
    e = &way_point[bridge->end[1]];
    if (e->group == gid)
        return bridge->last;
    return 0;
}

void set_check_wp(CheckWp *out, int wp, int gid)
{
    switch (way_group[gid].bridge) {
    case 0: {
        WayGroup *f = &way_group[wp];

        out->start = groupSideMe(f, gid);
        out->cross = groupSideBridge(f, gid);
        debug_StdPrintfDummy("set_check_wp:%p %p\n", out->start, out->cross);
        break;
    }
    case 1: {
        WayGroup *g = &way_group[gid];

        out->start = groupSideBridge(g, wp);
        out->cross = groupSideMe(g, wp);
        break;
    }
    }
}

typedef struct WayDist { /* field names derived */
    float d0;
    float d1;
} WayDist; /* derived name */

int set_bridge(int gid)
{
    WayPoint *wpA[2];
    WayPoint *wpB[2];
    WayDist dA;
    WayDist dB;
    float buf[4];
    WayPoint *wp;
    float d;

    WayGroup *g = &way_group[gid];

    memset(wpA, 0, 8);
    memset(wpB, 0, 8);
    dA = (WayDist){100000.0f, 100000.0f};
    dB = (WayDist){100000.0f, 100000.0f};

    if (g->closed == 1) {
        g->bridge = 0;
        return 0;
    }

    for (wp = WayPoint_begin(); wp != 0; wp = WayPoint_next(wp)) {
        if (wp->group == gid) {
            continue;
        }
        if (way_group[wp->group].bridge == 1) {
            continue;
        }
        sceVu0SubVector(buf, wp->pos, g->first->pos);
        d = fzMagnitudefv(buf);
        if (d < dA.d0) {
            dA.d1 = dA.d0;
            wpA[1] = wpA[0];
            dA.d0 = d;

            wpA[0] = wp;
        } else if (d < dA.d1) {
            dA.d1 = d;
            wpA[1] = wp;
        }
    }

    if (wpA[0] == 0) {
        return 0;
    }

    for (wp = WayPoint_begin(); wp != 0; wp = WayPoint_next(wp)) {
        if (wp->group == gid) {
            continue;
        }
        if (way_group[wp->group].bridge == 1) {
            continue;
        }
        sceVu0SubVector(buf, wp->pos, g->last->pos);
        d = fzMagnitudefv(buf);
        if (d < dB.d0) {
            dB.d1 = dB.d0;
            wpB[1] = wpB[0];
            dB.d0 = d;

            wpB[0] = wp;
        } else if (d < dB.d1) {
            dB.d1 = d;
            wpB[1] = wp;
        }
    }

    if (wpA[0] == wpB[0]) {
        if (dA.d0 < dB.d0) {
            if (wpB[1] == 0) {
                return 0;
            }
            wpB[0] = wpB[1];
        } else {
            if (wpA[1] == 0) {
                return 0;
            }
            wpA[0] = wpA[1];
        }
    }

    g->end[0] = wpA[0]->index;
    g->end[1] = wpB[0]->index;

    g->bridge = 1;

    way_point[g->end[0]].escape = 1;
    way_point[g->end[1]].escape = 1;

    return 1;
}

inline WayPoint *nearest_waypoint_of_group(float *arg0, int handle)
{
    float buf[4];
    WayPoint *t = WayPointList_begin(handle);
    float bestDist = 100000.0f;
    WayPoint *best, *cur;
    best = t;
    cur = best;
    if (best != 0) {
        do {
            float d;
            sceVu0SubVector(buf, cur->pos, arg0);
            d = fzMagnitudefv(buf);
            if (d < bestDist) {
                bestDist = d;
                best = cur;
            }
            cur = WayPointList_next(cur);
        } while (cur != 0);
    }
    return best;
}

inline WayPoint *nearest_waypoint(float *pos)
{
    return nearest_waypoint_of_group(pos, current_select_gid);
}

inline WayPoint *nearest_waypoint_from_gobj(void *dobj)
{
    float mtx[4];
    GetRootPosition(mtx, dobj);
    return nearest_waypoint_of_group(mtx, current_select_gid);
}

inline WayPoint *nearest_waypoint_by_lineseg_of_group(void *arg0, int gid)
{
    WayGroup *g = &way_group[gid];
    WayPoint *cur = g->first;
    float bestDist = 100000.0f;
    WayPoint *best = 0;
    WayPoint *next, *n;
    next = cur->next;
    if (next == 0)
        goto out;
    if (next == cur)
        goto out;
    do {
        float d = fzMagnitudeByLineSeg(cur->pos, next->pos, arg0);
        if (d < bestDist) {
            bestDist = d;
            best = cur;
        }
        cur = cur->next;
        n = cur->next;
        next = n;
        if (n == 0)
            goto out;
    } while (n != cur);
out:
    return best;
}

inline WayPoint *nearest_waypoint_by_lineseg(void *arg0)
{
    WayGroup *g = &way_group[current_select_gid];
    WayPoint *cur = g->first;
    float bestDist = 100000.0f;
    WayPoint *best = 0;
    WayPoint *next, *n;
    next = cur->next;
    if (next == 0)
        goto out;
    if (next == cur)
        goto out;
    do {
        float d = fzMagnitudeByLineSeg(cur->pos, next->pos, arg0);
        if (d < bestDist) {
            bestDist = d;
            best = cur;
        }
        cur = cur->next;
        n = cur->next;
        next = n;
        if (n == 0)
            goto out;
    } while (n != cur);
out:
    return best;
}

inline WayPoint *nearest_waypoint_by_lineseg_of_group_from_gobj(void *dobj, int gid)
{
    float mtx[4];
    float *pos;
    GetRootPosition(mtx, dobj);
    pos = mtx;
    {
        float bestDist = 100000.0f;
        WayPoint *best = 0;
        WayGroup *g = &way_group[gid];
        WayPoint *cur = g->first;
        WayPoint *next, *n;
        next = cur->next;
        if (next == 0)
            goto out;
        if (next == cur)
            goto out;
        do {
            float d = fzMagnitudeByLineSeg(cur->pos, next->pos, pos);
            if (d < bestDist) {
                bestDist = d;
                best = cur;
            }
            cur = cur->next;
            n = cur->next;
            next = n;
            if (n == 0)
                goto out;
        } while (n != cur);
    out:
        return best;
    }
}

inline WayPoint *nearest_waypoint_by_lineseg_from_gobj(void *dobj)
{
    float mtx[4];
    int gid = current_select_gid;
    float *pos;
    GetRootPosition(mtx, dobj);
    pos = mtx;
    {
        float bestDist = 100000.0f;
        WayPoint *best = 0;
        WayGroup *g = &way_group[gid];
        WayPoint *cur = g->first;
        WayPoint *next, *n;
        next = cur->next;
        if (next == 0)
            goto out;
        if (next == cur)
            goto out;
        do {
            float d = fzMagnitudeByLineSeg(cur->pos, next->pos, pos);
            if (d < bestDist) {
                bestDist = d;
                best = cur;
            }
            cur = cur->next;
            n = cur->next;
            next = n;
            if (n == 0)
                goto out;
        } while (n != cur);
    out:
        return best;
    }
}

inline WayPoint *waypoint_with_range(float *arg0, float thresh)
{
    float buf[4];
    WayPoint *node = WayPointList_begin(current_select_gid);
    if (node == 0)
        goto ret0;
    do {
        sceVu0SubVector(buf, node->pos, arg0);
        if (fzMagnitudefv(buf) < thresh) {
            return node;
        }
        node = WayPointList_next(node);
    } while (node != 0);
ret0:
    return 0;
}

inline WayPoint *nearest_waypoint_of_all_except_group(float *pos, int gid)
{
    float buf[4];
    WayPoint *t = WayPoint_begin();
    float bestDist = 100000.0f;
    WayPoint *best, *cur;
    best = t;
    cur = best;
    if (best != 0) {
        do {
            float d;
            if (cur->group != gid) {
                sceVu0SubVector(buf, cur->pos, pos);
                d = fzMagnitudefv(buf);
                if (d < bestDist) {
                    bestDist = d;
                    best = cur;
                }
            }
            cur = WayPoint_next(cur);
        } while (cur != 0);
    }
    return best;
}

inline WayPoint *nearest_waypoint_of_all_not_bridge_except_group(float *arg0, int gid)
{
    float buf[4];
    WayPoint *t = WayPoint_begin();
    float bestDist = 100000.0f;
    WayPoint *best, *cur;
    best = t;
    cur = best;
    if (best != 0) {
        do {
            int g = cur->group;
            if (g != gid && way_group[g].bridge != 1) {
                float d;
                sceVu0SubVector(buf, cur->pos, arg0);
                d = fzMagnitudefv(buf);
                if (d < bestDist) {
                    bestDist = d;
                    best = cur;
                }
            }
            cur = WayPoint_next(cur);
        } while (cur != 0);
    }
    return best;
}

inline WayPoint *nearest_waypoint_of_all(float *pos)
{
    float buf[4];
    int neg1 = -1;
    WayPoint *t = WayPoint_begin();
    float bestDist = 100000.0f;
    WayPoint *best, *cur;
    best = t;
    cur = best;
    if (best != 0) {
        do {
            float d;
            if (cur->group != neg1) {
                sceVu0SubVector(buf, cur->pos, pos);
                d = fzMagnitudefv(buf);
                if (d < bestDist) {
                    bestDist = d;
                    best = cur;
                }
            }
            cur = WayPoint_next(cur);
        } while (cur != 0);
    }
    return best;
}

inline WayPoint *visible_waypoint_of_all(void *pos)
{
    return visible_waypoint_of_all_except_gid(pos, -1);
}

inline void visible_waypoint_of_all_from_gobj(void *obj)
{
    float buf[4];
    GetRootPosition(buf, obj);
    visible_waypoint_of_all_except_gid(buf, -1);
}

inline WayPoint *visible_waypoint(float *arg0, int handle)
{
    float buf[4];
    ClipWork cb;
    float bestDist;
    WayPoint *best = 0;
    WayPoint *cur;
    cb.radius = 50.0f;
    cur = WayPointList_begin(handle);
    bestDist = 100000.0f;
    if (cur != 0) {
        do {
            float d;
            sceVu0SubVector(buf, cur->pos, arg0);
            d = fzMagnitudefv(buf);
            if (d < bestDist) {
                sceVu0CopyVector(cb.pt[0], arg0);
                sceVu0CopyVector(cb.pt[1], cur->pos);
                cb.pt[0][1] -= 75.0f;
                cb.pt[1][1] -= 75.0f;
                ClipWall(&cb);
                if (cb.wall.elem == 0) {
                    bestDist = d;
                    best = cur;
                }
            }
            cur = WayPointList_next(cur);
        } while (cur != 0);
    }
    return best;
}

inline WayPoint *visible_waypoint_from_gobj(void *dobj, int handle)
{
    float mtx[4];
    GetRootPosition(mtx, dobj);
    return visible_waypoint(mtx, handle);
}

inline WayPoint *get_wp_nearest_bridge_side_me(int arg0, int arg1)
{
    int i;
    for (i = 0; i < 94; i++) {
        WayGroup *g = &way_group[i];
        WayPoint *a;
        WayPoint *b;
        if (g->used == 0)
            continue;
        if (g->bridge == 0)
            continue;
        a = &way_point[g->end[0]];
        b = &way_point[g->end[1]];
        if (a->group == arg0 && b->group == arg1)
            return b;
        if (b->group == arg0 && a->group == arg1)
            return a;
    }
    return 0;
}

inline WayPoint *get_wp_nearest_bridge_side_bridge(int arg0, int arg1)
{
    int i;
    for (i = 0; i < 94; i++) {
        WayGroup *g = &way_group[i];
        WayPoint *a;
        WayPoint *b;
        if (g->used == 0)
            continue;
        if (g->bridge == 0)
            continue;
        a = &way_point[g->end[0]];
        b = &way_point[g->end[1]];
        if (a->group == arg0 && b->group == arg1)
            return g->last;
        if (b->group == arg0 && a->group == arg1)
            return g->first;
    }
    return 0;
}

inline WayGroup *waybridge_between_group(int gidA, int gidB)
{
    return bridgeBetweenGroups(gidA, gidB);
}

inline WayPoint *bridge_waypoint_side_me(int me, int target)
{
    WayGroup *p = WayBridge_begin();
    while (p != 0) {
        WayPoint *eA = &way_point[p->end[0]];
        WayPoint *eB = &way_point[p->end[1]];
        int a = eA->group;
        if (a == me && eB->group == target)
            return eB;
        if (eB->group == me && a == target)
            return eA;
        p = WayBridge_next(p);
    }
    return 0;
}

inline WayPoint *waypoint_connect_group_side_me(WayGroup *bridge, int gid)
{
    return groupSideMe(bridge, gid);
}

inline WayPoint *bridge_waypoint_side_bridge(int gidA, int gidB)
{
    WayGroup *p = WayBridge_begin();
    while (p != 0) {
        WayPoint *eA = &way_point[p->end[0]];
        WayPoint *eB = &way_point[p->end[1]];
        int a = eA->group;
        if (a == gidA && eB->group == gidB) {
            return p->last;
        }
        if (eB->group == gidA && a == gidB) {
            return p->first;
        }
        p = WayBridge_next(p);
    }
    return 0;
}

inline WayPoint *waypoint_connect_group_side_bridge(WayGroup *bridge, int gid)
{
    WayPoint *e = &way_point[bridge->end[0]];
    if (e->group == gid)
        return bridge->first;
    e = &way_point[bridge->end[1]];
    if (e->group == gid)
        return bridge->last;
    return 0;
}

inline int NearestWgFromTarget(int cur, int end, WgAll *w)
{
    int *dist = w->dist;
    int *prev = w->prev2;
    while (1) {
        if (dist[cur] != 0x7FFFFFFF) {
            if (way_group[cur].bridge == 0)
                break;
        }
        if (cur == end)
            break;
        cur = prev[cur];
    }
    return cur;
}

static inline int wpsort_compfnc(WpSortEnt *p, WpSortEnt *q)
{
    float x = p->d;
    float y = q->d;
    if (x < y) {
        return -1;
    }
    if (x > y) {
        return 1;
    }
    return 0;
}
