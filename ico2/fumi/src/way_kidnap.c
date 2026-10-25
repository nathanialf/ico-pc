#include "way_kidnap.h"
#include "debug.h"
#include "gobj.h"
#include "enemy_act.h"
#include "fuzio.h"
#include "way_llf.h"
#include "way_sys.h"
#include "way_util.h"
#include "geometryManager.h"
#include <libvu0.h>
#include <stdlib.h>
#include "Matrix.h"
#include "main.h"

/* the count of positions add_wp_pos has collected, the flag that stops it
   while WayRangeSearch measures a path, and the range search's limit mode */
static int wpPosCount = 0; /* derived name */

static int wpPosLock = 0; /* derived name */

static int wayRangeLimit = 0; /* derived name */

static inline void ClearWpPos(void) /* derived name */
{
    if (wpPosLock == 0) {
        wpPosCount = 0;
    }
}

typedef struct WpPosEntry { /* field names derived */
    WayPoint *wp;
    float len;
} WpPosEntry; /* derived name */

/* searchNodes is the node list WayPointWithRangeFromPos2 grows while it walks
   the way graph and edgeDone its per-edge visited flags, each sized by the
   count the search clears; wpPosVec and wpPosInfo are the positions and the
   (waypoint, length) pairs add_wp_pos appends, CopyWpPos reading at most
   128. */
static WayPoint *searchNodes[275]; /* derived name */

static char edgeDone[94]; /* derived name */

static float wpPosVec[128][4]; /* derived name */

/* 276 entries, one more than the loops' 275 */
static WpPosEntry wpPosInfo[276]; /* derived name */

static void add_wp_pos(WayPoint *wp, float *pos, float len)
{
    if (wpPosLock) {
        return;
    }
    fzShowV(pos);

    wpPosInfo[wpPosCount].wp = wp;
    wpPosInfo[wpPosCount].len = len;
    sceVu0CopyVector(wpPosVec[wpPosCount++], pos);
}

/* public inlines, deferred to the end of the object in the order
   way_kidnap.h declares them; the string CopyWpPos prints comes first in the
   TU's .rodata. */
inline int NumOfWpPos(void)
{
    return wpPosCount;
}

inline int CopyWpPos(float dst[][4], int from, int to)
{
    int j = 0;
    if (from < 0) {
        return 1;
    }

    for (; from <= to && from < 128; from++, j++) {
        debug_StdPrintfDummy("index %d\n", j);
        sceVu0CopyVector(dst[j], wpPosVec[from]);
    }

    return 0;
}

float WayLengthOfPos_Pos(float *pos0, float *pos1)
{
    float cur[4];
    float dst[4];
    WVTObj w;
    WayPoint *wp;
    WayPoint *wp0;
    WayPoint *wp1;
    float len;
    int i;

    len = 0.0f;

    w.guideFirst = -1;
    w.nearWp = 0;
    sceVu0CopyVector(cur, pos0);
    sceVu0CopyVector(dst, pos1);
    wp0 = visible_waypoint_of_all(cur);
    wp1 = visible_waypoint_of_all(dst);

    ClearWpPos();
    add_wp_pos(0, cur, len);

    wp = GetWay_begin(dst, &w, cur);
    if (wp == 0) {
        goto fail;
    }
    if (wp == wp1) {
        goto found;
    }
    if (w.pathKind == 2) {
        goto fail;
    }
    if (wayRangeLimit == 0 && w.pathKind == 1) {
        goto fail;
    }

    len += _GetLength(cur, wp->pos);
    add_wp_pos(wp0, wp0->pos, len);

    sceVu0CopyVector(cur, wp->pos);

    for (i = 0;;) {
        if (++i >= 101) {
            goto fail;
        }
        wp = GetWay_next(&w, cur);

        if (w.reached != 0) {
            if (wp == wp1) {
                goto found;
            }
            wp = GetWay_begin(dst, &w, cur);
            if (wp == 0) {
                goto fail;
            }
            if (wp == wp1) {
                goto found;
            }

            sceVu0CopyVector(cur, wp->pos);
        } else {
            len += _GetLength(cur, wp->pos);

            add_wp_pos(wp, wp->pos, len);
            sceVu0CopyVector(cur, wp->pos);
        }
    }

fail:
    ClearWpPos();

    return -1.0f;

found:
    len += _GetLength(pos1, wp1->pos);
    add_wp_pos(0, dst, len);

    return len;
}

/* public inlines, deferred to the end of the object like NumOfWpPos;
   NearestEnemyFromGirl inlines the pair. */
inline float WayLengthOfGObj_Pos(GObj *obj, float *pos)
{
    float buf[4];
    if (obj == 0) {
        return -1.0f;
    }
    GetRootPosition(buf, obj);
    return WayLengthOfPos_Pos(buf, pos);
}

inline float WayLengthOfGObj_GObj(GObj *obj0, GObj *obj1)
{
    float pos[4];
    if (obj1 == 0) {
        return -1.0f;
    }
    GetRootPosition(pos, obj1);
    return WayLengthOfGObj_Pos(obj0, pos);
}

/* a file-static wpsort_compfnc, distinct from the way_util global of the same
   name; only qsort takes its address, so its body is emitted last, after the
   header's public inlines. */
static inline int wpsort_compfnc(const void *a, const void *b)
{
    const WpPosEntry *ea = a;
    const WpPosEntry *eb = b;

    if (ea->len < eb->len)
        return -1;
    if (eb->len < ea->len)
        return 1;
    return 0;
}

static inline void WayRangeSearch(float *pos, float range, WpPosEntry *e, int limit,
                                  int chk) /* derived name */
{
    wayRangeLimit = limit;
    ClearWpPos();

    for (e->wp = WayPoint_begin(); e->wp != 0; e->wp = WayPoint_next(e->wp)) {
        if (range <= _GetLength(pos, e->wp->pos)) {
            continue;
        }
        wpPosLock = 1;
        e->len = WayLengthOfPos_Pos(pos, e->wp->pos);
        wpPosLock = 0;
        if (e->len < 0.0f) {
            continue;
        }
        if (chk && e->wp->bridgeEnd != 0) {
            continue;
        }
        add_wp_pos(e->wp, e->wp->pos, e->len);
    }
    wayRangeLimit = 0;
}

int WayPointWithRangeFromPos(float *pos, float range, int mode)
{
    WpPosEntry e;
    int n;
    int i;

    switch (mode) {
    case 0:
    default:
        WayRangeSearch(pos, range, &e, 0, 0);
        break;

    case 1:
        WayRangeSearch(pos, range, &e, 0, 1);

        n = NumOfWpPos();
        qsort(wpPosInfo, n, sizeof(WpPosEntry), wpsort_compfnc);
        for (i = 0; i < n; i++) {
            sceVu0CopyVector(wpPosVec[i], wpPosInfo[i].wp->pos);
        }
        break;

    case 2:
        WayRangeSearch(pos, range, &e, 1, 0);
        break;

    case 3:
        WayRangeSearch(pos, range, &e, 1, 1);

        n = NumOfWpPos();
        qsort(wpPosInfo, n, sizeof(WpPosEntry), wpsort_compfnc);
        for (i = 0; i < n; i++) {
            sceVu0CopyVector(wpPosVec[i], wpPosInfo[i].wp->pos);
        }
        break;
    }

    return NumOfWpPos();
}

static inline WayPoint *SearchOpenNode(WayPoint *start) /* derived name */
{
    WayPoint *p;
    int wrapped;
    int hit;

    wrapped = 0;
    hit = 0;

    p = start;
    while (p != 0) {
        if (p->bridgeEnd == 0) {
            hit = 1;
            break;
        }
        p = p->next;
        if (p == start) {
            wrapped = 1;
            break;
        }
    }
    if (hit == 0 && wrapped == 0) {
        p = start;
        while (p != 0) {
            if (p->bridgeEnd == 0) {
                hit = 1;
                break;
            }
            p = p->prev;
            if (p == start) {
                break;
            }
        }
    }
    if (hit == 0) {
        p = 0;
    }
    return p;
}

/* the DEBUG build's trace of the edge the search opens, by its table index */
static __inline__ void wayKidnapDebugEdge(int k) /* derived name */
{
#ifdef DEBUG
    scePrintf("open edge %d\n", k);
#endif
}

int WayPointWithRangeFromPos2(float *pos, WVTObj *w, float *dst, int chk)
{
    float v[4];
    WayPoint *found;
    WayPoint *cur;
    /* the initialiser is dead: edge is set at the top of every pass of the loop
       before any read */
    WayGroup *edge = 0;
    WayPoint *nearest;
    float best;
    float d;
    int n = 0;
    int i;
    int j;
    int k;

    for (i = 0; i < PAL_AUG6(275, 215); i++) {
        searchNodes[i] = 0;
    }
    for (k = 0; k < PAL_AUG6(94, 64); k++) {
        edgeDone[k] = 0;
    }
    GetWay_begin(pos, w, pos);
    found = 0;
    cur = w->nearWp;
    if (cur == 0) {
        /* my own WAY was not found */
        debug_StdPrintfDummy("自分のWAYが見付からなかった");
        goto ret;
    }
    edgeDone[cur->group] = 1;
    searchNodes[n++] = cur;

    while (1) {
        for (i = 0; i < n; i++) {
            if (searchNodes[i] != 0) {
                goto found;
            }
        }
        break;
    found:
        cur = searchNodes[i];
        edge = &way_group[cur->group];
        debug_StdPrintfDummy("srh wp %p group id %d %d\n", cur, cur->group, i);
        searchNodes[i] = 0;
        debug_StdPrintfDummy("active %d\n", edge->active);
        if (edge->active != 0) {
            /* k is the index of this edge in the table, as everywhere below, and
               the DEBUG build's trace reads it; in retail the value is dead
               (the next read of k follows its reassignment in both loops) */
            k = edge - way_group;
            wayKidnapDebugEdge(k);
            found = SearchOpenNode(cur);
        }
        if (found != 0) {
            break;
        }
        if (edge->bridge == 0) {
            for (k = 0; k < PAL_AUG6(94, 64); k++) {
                if (way_group[k].used == 0) {
                    continue;
                }
                if (way_group[k].bridge == 0) {
                    continue;
                }
                for (j = 0; j < 2; j++) {
                    if (cur->group != way_point[way_group[k].end[j]].group) {
                        continue;
                    }
                    if (edgeDone[k] != 0) {
                        continue;
                    }
                    edgeDone[k] = 1;
                    if (chk && way_group[k].active == 0) {
                        continue;
                    }
                    if (j == 0) {
                        searchNodes[n++] = way_group[k].first;
                    } else {
                        searchNodes[n++] = way_group[k].last;
                    }
                    debug_StdPrintfDummy("add no bridge wp %p %d %d\n", searchNodes[n - 1], j, k);
                }
            }
        } else {
            for (j = 0; j < 2; j++) {
                k = way_point[edge->end[j]].group;
                if (edgeDone[k] != 0) {
                    continue;
                }
                edgeDone[k] = 1;
                if (chk && way_group[k].active == 0) {
                    continue;
                }
                searchNodes[n++] = &way_point[edge->end[j]];
                debug_StdPrintfDummy("add bridge wp %p %d %d\n", searchNodes[n - 1], j, k);
            }
        }
    }

ret:
    if (found == 0 && chk) {
        return 0;
    }
    if (found == 0) {
        nearest = 0;
        best = 3.40282347e+38f /* FLT_MAX */;
        /* not found, so search every WAYPOOINT for the nearest point of the
           active group that allows a nest */
        debug_StdPrintfDummy(
            "見付からないので全WAYPOOINTから アクティブグループで巣許可の一番近いポイントを検索");
        for (i = 0; i < PAL_AUG6(275, 215); i++) {
            cur = &way_point[i];
            if (cur->used == 0 || cur->bridgeEnd != 0 || way_group[cur->group].active == 0) {
                continue;
            }
            _SubVector(v, pos, cur->pos);
            d = _InnerProduct(v, v);
            if (d < best) {
                nearest = cur;
                best = d;
            }
        }
        if (nearest != 0) {
            sceVu0CopyVector(dst, cur->pos);
            return 1;
        }
        /* no point of the active group allows a nest */
        debug_StdPrintfDummy("アクティブグループの巣許可のポイントがみつかりません");
        return 0;
    }
    sceVu0CopyVector(dst, found->pos);
    return 1;
}

/* a public inline, deferred like the others */
inline int WayPointWithRangeFromGObj(GObj *obj, float f)
{
    float pos[4];
    if (obj == 0) {
        return -1;
    }
    GetRootPosition(pos, obj);
    return WayPointWithRangeFromPos(pos, f, 0);
}

GObj *NearestEnemyFromGirl(float *len)
{
    float min;
    GObj *nearest = 0;
    GObj *obj;
    float d;

    obj = isysGObjSearchFromObjKindID_begin(4);
    min = 1.0e10f;

    while (obj != 0 && !isEnemyKidnapEnable(obj))
        obj = isysGObjSearchFromObjKindID_next(obj);

    while (obj != 0) {
        d = WayLengthOfGObj_GObj(girlGObj, obj);

        if (d >= 0.0f && d < min) {
            min = d;
            nearest = obj;
        }

        do {
            obj = isysGObjSearchFromObjKindID_next(obj);
        } while (obj != 0 && !isEnemyKidnapEnable(obj));
    }
    *len = min;
    return nearest;
}
