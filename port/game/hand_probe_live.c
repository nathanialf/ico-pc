/*
 * port/game/hand_probe_live.c
 *
 * v0.4.3 AN-19d (issue 19): the hand probe's per-tick line, in the program
 * only (it reads the game's objects, so it is built with the game's include
 * path and record layout, as gamestate.c is). trace_host.c calls it once
 * per Main tick, after the objects ran, while [dev] hand_probe is on
 * (diag_host.h, ico_hand_probe_on); it writes one line while Ico holds
 * Yorda's hand (one of his hand records in mode 5):
 *
 *   probe: tick t=<tick> boy=<x,y,z> girl=<x,y,z> bdir=<x,z> gdir=<x,z>
 *          bmot=<n> gmot=<n> bact=<n> gact=<n> vu0r=<hex> rand=<hex>
 *          bframe=<f> gframe=<f>
 *
 * (one line), each float as value/bits (diag_host.h, ico_hand_probe_put):
 * the two root positions, the motion directions, the motions, their
 * frames, the action modes and the two random states, so a phone's log and
 * a computer's can be compared tick by tick (tools/hand_probe_diff.py).
 */
#include <stdio.h>

#include "typedef.h"

#include "diag_host.h"

extern GObj *boyGObj;                    /* common/src/main.c */
extern GObj *girlGObj;                   /* common/src/main.c */
extern int mpegPlay;                     /* common/src/StageManager.c */
extern int stageManagerFreeResourceFlag; /* common/src/StageManager.c */
unsigned int ico_vu0_random_get(void);   /* port/math/matrix.c */
unsigned int ico_rand_state(void);       /* port/math/newlib/rand.c */

void ico_hand_probe_tick(unsigned int tick);

static int probeReady(GObj *o)
{
    return o != 0 && GOBJ_SUB(o) != 0 && GOBJ_ACT(o) != 0;
}

void ico_hand_probe_tick(unsigned int tick)
{
    static char line[1000];
    char *end = line + sizeof line;
    char *p = line;
    Sub15C *b;
    Sub15C *g;
    float dir[2];
    int w;

    /* the objects are being freed or a movie plays: gamestate.c's test */
    if (stageManagerFreeResourceFlag != 0 || mpegPlay != 0 || !probeReady(boyGObj) ||
        !probeReady(girlGObj)) {
        return;
    }
    b = GOBJ_SUB(boyGObj);
    g = GOBJ_SUB(girlGObj);
    if (b->root.hand0.mode != 5 && b->root.hand1.mode != 5) {
        return;
    }
    w = snprintf(p, sizeof line, "probe: tick t=%u", tick);
    p += (w > 0 && w < end - p) ? w : 0;
    p = ico_hand_probe_put(p, end, "boy", b->root.pos, 3);
    p = ico_hand_probe_put(p, end, "girl", g->root.pos, 3);
    dir[0] = b->ctrl.dir[0];
    dir[1] = b->ctrl.dir[2];
    p = ico_hand_probe_put(p, end, "bdir", dir, 2);
    dir[0] = g->ctrl.dir[0];
    dir[1] = g->ctrl.dir[2];
    p = ico_hand_probe_put(p, end, "gdir", dir, 2);
    if (p < end) {
        w = snprintf(p, (size_t)(end - p), " bmot=%d gmot=%d bact=%d gact=%d vu0r=%08x rand=%08x",
                     b->ctrl.motion, g->ctrl.motion, GOBJ_ACT(boyGObj)->actMode,
                     GOBJ_ACT(girlGObj)->actMode, ico_vu0_random_get(), ico_rand_state());
        p += (w > 0 && w < end - p) ? w : end - p;
    }
    p = ico_hand_probe_put(p, end, "bframe", &b->ctrl.animFrame, 1);
    (void)ico_hand_probe_put(p, end, "gframe", &g->ctrl.animFrame, 1);
    ico_diag_log("%s", line);
}
