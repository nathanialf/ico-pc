#include "debug.h"
#include <libvu0.h>
#include "typedef.h"
#include "way_llf.h"
#include "way_util.h"

/* the way groups and the way points, zero-initialised; the points start on
   the 16-byte boundary their position vector gives them */
WayGroup way_group[94] = {0};

WayPoint way_point[275] = {0};

/* way_llf.o's .sdata globals (declared in way_llf.h), tentative definitions */
int first_waytool;

int n_way_group;

int current_select_gid;

/* defined `inline` below, and called before their definitions */
inline int DeleteWayPoint(int pno);
inline void SetWayGroupActive(int gno, int active);

/* The TU's shared group allocator, expanded into CreateWayGroup's and
 * CreateTempWayGroup's callers: the first free slot of the way-group table. */
inline int CreateWayGroup(void)
{
    int i;

    for (i = 0; i < PAL_AUG6JUN26(94, 64); i++) {
        WayGroup *wg = &way_group[i];

        if (wg->used == 0) {
            wg->used = 1;
            wg->first = 0;
            wg->last = 0;
            wg->count = 0;
            wg->closed = 0;
            wg->bridge = 0;
            wg->boxBridge = 0;
            wg->end[0] = -1;
            wg->end[1] = -1;
            wg->temp = 0;
            n_way_group++;
            return i;
        }
    }
    return -1;
}

inline int CreateTempWayGroup(void)
{
    int no = CreateWayGroup();

    if (no != -1) {
        way_group[no].temp = 1;
    }
    return no;
}

inline int DeleteWayGroup(int gno)
{
    WayGroup *wg = &way_group[gno];

    if (wg->used == 1) {
        if (wg->first != 0) {
            WayPoint *wp = wg->first;

            do {
                DeleteWayPoint(wp->index);
                wp = wp->next;
            } while (wp != 0);
        }

        wg->used = 0;
        n_way_group--;
        return 0;
    }
    return 1;
}

inline void CloseWayGroup(int idx)
{
    WayGroup *wg = &way_group[idx];
    WayPoint *first = wg->first;
    WayPoint *last = wg->last;
    wg->closed = 1;
    first->prev = last;
    last->next = first;
}

inline int CreateWayPoint(float *pos)
{
    int i;

    for (i = 0; i < PAL_AUG6JUN26(275, 215); i++) {
        WayPoint *node = &way_point[i];

        if (node->used == 0) {
            node->used = 1;
            node->group = -1;
            node->prev = 0;
            node->next = 0;
            node->escape = 0;
            sceVu0CopyVector(node->pos, pos);
            return i;
        }
    }
    return -1;
}

inline int AddWayPoint(int gno, int pno)
{
    WayGroup *wg = &way_group[gno];
    WayPoint *wp = &way_point[pno];

    if (wg->first == 0) {
        wg->first = wp;
        wg->last = wp;
    } else {
        wg->last->next = wp;
        wp->prev = wg->last;
        wp->next = 0;

        wg->last = wp;
    }

    wp->group = gno;
    wg->count++;
    return 0;
}

inline int AddWayPointTop(int gno, int pno)
{
    WayGroup *wg = &way_group[gno];
    WayPoint *node = &way_point[pno];
    WayPoint *old;
    node->prev = 0;
    old = wg->first;
    wg->first = node;
    node->next = old->prev;
    old->prev = node;
    return 0;
}

inline int InsertWayPointAfter(int dummy, int idx1, int idx2)
{
    WayPoint *node_a = &way_point[idx1];
    WayPoint *node_b = &way_point[idx2];
    WayPoint *old = node_a->next;
    node_a->next = node_b;
    node_b->prev = node_a;
    node_b->next = old;
    old->prev = node_b;
    return 0;
}

inline int DeleteWayPoint(int pno)
{
    WayPoint *wp = &way_point[pno];
    WayPoint *prev = wp->prev;
    WayPoint *next = wp->next;
    WayGroup *wg = &way_group[wp->group];

    if (wg->count < 4)
        wg->closed = 0;

    if (wg->closed != 0) {
        if (wp == wg->first) {
            wg->first = next;

            wg->count--;
            wp->used = 0;
            return 0;
        } else if (wp == wg->last) {
            wg->last = prev;

            wg->count--;
            wp->used = 0;
            return 0;
        }
    }

    if (prev != 0) {
        prev->next = next;
    } else if (next != 0) {
        wg->first = next;
    }

    if (next != 0) {
        next->prev = prev;
    } else if (prev != 0) {
        wg->last = prev;
    }

    if (prev == 0 && next == 0) {
        wg->first = 0;
        wg->last = 0;
    }

    wp->used = 0;
    wg->count--;
    return 0;
}

/* CreateBridge sits between DeleteWayPoint and the begin iterators; it is the
   one function of the TU too large to inline. */
int CreateBridge(float *a, float *b)
{
    int gno;
    int p0;
    int p1;

    gno = CreateWayGroup();
    if (gno < 0) {
        debug_StdPrintfDummy("WayPointCreateNewBridge: way group not create\n");
        return -1;
    }
    p0 = CreateWayPoint(a);
    AddWayPoint(gno, p0);
    way_point[p0].bridgeEnd = 1;
    p1 = CreateWayPoint(b);
    AddWayPoint(gno, p1);
    way_point[p1].bridgeEnd = 1;
    set_bridge(gno);
    SetWayGroupActive(gno, 1);
    return gno;
}

/* the begin iterators start one record before the way-group table and step
   before the first test */

inline WayGroup *WayGroup_begin(void)
{
    WayGroup *p = way_group - 1;
    WayGroup *end = p + PAL_AUG6JUN26(94, 64);
    if (p != 0 && p != end) {
        do {
            p++;
            if (p->used != 0)
                return p;
        } while (p != end);
    }
    return 0;
}

inline WayGroup *WayGroup_next(WayGroup *p)
{
    WayGroup *end = &way_group[PAL_AUG6JUN26(93, 63)];
    if (p != 0 && p != end) {
        WayGroup *q = p;
        do {
            q++;
            if (q->used != 0)
                return q;
        } while (q != end);
    }
    return 0;
}

inline WayGroup *WayBridge_begin(void)
{
    WayGroup *p = way_group - 1;
    WayGroup *end = p + PAL_AUG6JUN26(94, 64);
    if (p != 0 && p != end) {
        do {
            p++;
            if (p->used != 0 && p->bridge != 0 && p->active != 0)
                return p;
        } while (p != end);
    }
    return 0;
}

inline WayGroup *WayBridge_next(WayGroup *p)
{
    WayGroup *end = &way_group[PAL_AUG6JUN26(93, 63)];
    if (p != 0 && p != end) {
        WayGroup *q = p;
        do {
            q++;
            if (q->used != 0 && q->bridge != 0 && q->active != 0)
                return q;
        } while (q != end);
    }
    return 0;
}

inline WayGroup *WayBridgeAll_begin(void)
{
    WayGroup *p = way_group - 1;
    WayGroup *end = p + PAL_AUG6JUN26(94, 64);
    if (p != 0 && p != end) {
        do {
            p++;
            if (p->used != 0 && p->bridge != 0)
                return p;
        } while (p != end);
    }
    return 0;
}

inline WayGroup *WayBridgeAll_next(WayGroup *p)
{
    WayGroup *end = &way_group[PAL_AUG6JUN26(93, 63)];
    if (p != 0 && p != end) {
        WayGroup *q = p;
        do {
            q++;
            if (q->used != 0 && q->bridge != 0)
                return q;
        } while (q != end);
    }
    return 0;
}

inline WayGroup *WayBridgeVar_begin(void)
{
    WayGroup *p = way_group - 1;
    WayGroup *end = way_group - 1 + PAL_AUG6JUN26(94, 64);
    if (p == 0)
        goto ret0;
    if (p == end)
        goto ret0;
    for (p++;; p++) {
        if (p->used != 0 && p->bridge != 0 && p->active != 0)
            return p;
        if (p == end)
            break;
    }
ret0:
    return 0;
}

inline WayGroup *WayBridgeVar_next(WayGroup *g)
{
    WayGroup *p, *end = &way_group[PAL_AUG6JUN26(93, 63)];
    if (g != 0 && g != end) {
        for (p = g + 1;; p++) {
            if (p->used != 0 && p->bridge != 0 && p->active != 0)
                return p;
            if (p == end)
                break;
        }
    }
    return 0;
}

inline WayPoint *WayPoint_begin(void)
{
    WayPoint *p = way_point - 1;
    WayPoint *end = way_point - 1 + PAL_AUG6JUN26(275, 215);
    if (p == 0)
        goto ret0;
    if (p == end)
        goto ret0;
    for (p++;; p++) {
        if (p->used != 0)
            return p;
        if (p == end)
            break;
    }
ret0:
    return 0;
}

inline WayPoint *WayPoint_next(WayPoint *p)
{
    WayPoint *end = &way_point[PAL_AUG6JUN26(274, 214)];
    if (p == 0)
        goto ret0;
    if (p == end)
        goto ret0;
    for (p++;; p++) {
        if (p->used != 0)
            return p;
        if (p == end)
            break;
    }
ret0:
    return 0;
}

/* WayPointList_next's body, a helper defined ahead of WayPointList_begin: the
 * next point of wp's group, or 0 at the end of the ring.  The group record is
 * read before the null test. */
static inline WayPoint *wayPointListNext(WayPoint *wp) /* derived name */
{
    WayGroup *grp = &way_group[wp->group];

    if (wp == 0)
        return 0;
    if (wp->next == grp->first)
        return 0;
    return wp->next;
}

inline WayPoint *WayPointList_begin(int gno)
{
    return way_group[gno].first;
}

inline WayPoint *WayPointList_next(WayPoint *p)
{
    return wayPointListNext(p);
}

inline WayPoint *waypoint_bidirectional_list(WayPoint *self, int which)
{
    if (self == 0) {
        return 0;
    }
    debug_StdPrintfDummy("bidir wp:%p\n", self);
    if (which == 0) {
        return self->prev;
    }
    return self->next;
}

void InitWayPointSystem(void)
{
    int i;

    for (i = 0; i < PAL_AUG6JUN26(275, 215); i++) {
        WayPoint *node = &way_point[i];

        node->used = 0;
        node->index = i;
        node->prev = 0;
        node->next = 0;
        node->group = -1;
    }

    for (i = 0; i < PAL_AUG6JUN26(94, 64); i++) {
        WayGroup *wg = &way_group[i];

        wg->used = 0;
        wg->index = i;
        wg->first = 0;
        wg->last = 0;
        wg->count = 0;
        wg->closed = 0;
        wg->bridge = 0;
        wg->boxBridge = 0;
        wg->active = 0;
        wg->end[0] = -1;
        wg->end[1] = -1;
    }

    first_waytool = 0;
    n_way_group = 0;
}

inline void SetWayGroupActive(int gno, int active)
{
    way_group[gno].active = active;
}

inline int CheckWayGroupActive(int idx)
{
    return way_group[idx].active != 0;
}
