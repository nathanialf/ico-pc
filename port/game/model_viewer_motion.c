/*
 * port/game/model_viewer_motion.c
 *
 * Which motions of a model's block the viewer can play on the stage it
 * loaded (issue 136).  A motion is held when it is the stage's own (area 0)
 * or its data is loaded (motionTable).  A node-blend motion (blendKind not
 * 320) is a list of blendMotionKind rows, one partner motion each, ended by
 * motion 1147; the game's getNodeBlendedFloatingMotion hands every partner's
 * data to GetFloatingMotion, which reads through it, so one partner that is
 * not loaded (a dynamic pool motion of another area) is a read through NULL.
 * The game only requests these motions where it loaded the partners; the
 * viewer offers a block, so it checks them.
 */
#include "typedef.h"
#include "motionFileManager.h"
#include "motionOrientManager.h"

#include "model_viewer.h"
#include "../data/tables.h"

extern MotionDef motionKind[];

/* the end marker of a blendMotionKind list */
#define MV_BLEND_END 1147

static int motionHeld(int id)
{
    return id >= 0 && id < MV_MOTION_KINDS && (motionKind[id].area == 0 || motionTable[id] != 0);
}

int ico_mv_motion_playable(int id, int *missing)
{
    if (missing != NULL) {
        *missing = -1;
    }
    if (id < 0 || id >= MV_MOTION_KINDS) {
        return 0;
    }
    if (motionKind[id].blendKind == 320) {
        if (!motionHeld(id)) {
            return 0;
        }
        return 1;
    }
    /* the rows of the table the disc loaded; none loaded, nothing is known */
    int rows = (int)ico_tables_count(ICO_TID_blendMotionKind);
    int first = motionKind[id].blendKind;
    int n = 0;
    for (int j = first; j >= 0 && j < rows; j++) {
        int m = blendMotionKind[j].motion;
        if (m == MV_BLEND_END) {
            return n > 0; /* an empty list is not a motion */
        }
        n++;
        /* the game's checkMotionKind: a partner is a plain motion */
        if (m < 0 || m >= MV_MOTION_KINDS || motionKind[m].blendKind != 320 || !motionHeld(m)) {
            if (missing != NULL) {
                *missing = m;
            }
            return 0;
        }
    }
    return 0; /* no end marker inside the table */
}
