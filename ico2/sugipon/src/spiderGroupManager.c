#include "debug.h"
#include "debug_exception.h"
#include "gobj.h"
#include "generator.h"
#include "act_a_p_1.h"
#include "frameDependSequence.h"
#include "geometryManager.h"
#include "matrixDrive.h"
#include "spiderGroupManager.h"
#include "spider.h"
#include "main.h"
#include <assert.h>

/* the manager's counters and the revive state */
static int reviveNext = 0; /* derived name */

static int spiderGroupIdCount = 0; /* derived name */

static int spiderGroupCount = 0; /* derived name */

static int reviveGroupIdCount = 0; /* derived name */

static int execFrame = 0; /* derived name */

static GObj *reviveMaster = 0; /* derived name */

static int reviveCounter = 0; /* derived name */

static int reviveDone = 0; /* derived name */

typedef struct { /* field names derived */
    GObj *group; /* 0x00 */
    int spider;  /* 0x04, the spider's index in the group */
} SpiderPair;    /* derived name */

typedef struct { /* field names derived */
    GObj *gobj;  /* 0x00 */
    ICO_WORD_PTR(GObj *)
    rev; /* 0x04, the enemy layout object locked for the revive, held as a word: typed GObj *, the store order of EntrySpiderGroupManager moves (measured) */
} SpiderGroupEnt; /* derived name */

/* the spiders found inside a revive range, the pairs picked out of them,
   the registered group ids, the group table and the group ids the revive
   walk works from */
static int spidersInRange[100]; /* derived name */

static SpiderPair spiderPairs[5]; /* derived name */

/* the registered groups' GObj handles, held as words: typed GObj *, the
   store order of EntrySpiderGroupManager moves (measured) */
static ICO_WORD_PTR(GObj *) spiderGroupIds[64]; /* derived name */

static SpiderGroupEnt spiderGroups[64]; /* derived name */

static GObj *reviveGroupIds[64]; /* derived name */

/* one RGBA tint per spider group, alpha 128 throughout */
static int spiderGroupColors[7][4] = {{127, 0, 0, 128}, {64, 127, 0, 128}, {0, 64, 127, 128},
                                      {0, 127, 0, 128}, {64, 0, 127, 128}, {127, 64, 0, 128},
                                      {64, 64, 64, 128}}; /* derived name */

inline void InitSpiderGroupManager(void)
{
    spiderGroupIdCount = 0;
    reviveGroupIdCount = 0;

    spiderGroupCount = 0;

    reviveMaster = 0;

    reviveNext = 0;

    execFrame = 0;

    reviveCounter = 0;
    reviveDone = 0;
}

inline GObj *getReviveEnemyGObj(int count)
{
    GObj *p = isysGObjSearchFromObjKindID_begin(4);
    int i;
    {
        for (i = 0; i < count; i++) {
            if (p == 0) {
                return 0;
            }
            p = isysGObjSearchFromObjKindID_next(p);
        }
    }
    return p;
}

inline void EntryRevivedSpiderGroupManager(GObj *group)
{
    int idx = spiderGroupIdCount;
    spiderGroupIdCount = idx + 1;
    spiderGroupIds[idx] = (ICO_WORD_PTR(GObj *))group;
}

void EntrySpiderGroupManager(GObj *gobj)
{
    GObj *p;

    spiderGroups[spiderGroupCount].gobj = gobj;
    p = getReviveEnemyGObj(spiderGroupIdCount);
    if (p != 0) {
        debug_StdPrintfDummy("LOCK %p for LABEL %d, ID:%d\n", p, p->labelId, spiderGroupCount);
        LockEnemyGenerate(p);
        p->active = 0;
    } else {
        debug_assertMessage(
            "src/spiderGroupManager.c", 85,
            "No valid enemy layout data for spider.\n(Lack of enemy layout for spider revive.)\n");
        __assert("src/spiderGroupManager.c", 85, "e");
    }
    spiderGroups[spiderGroupCount].rev = (ICO_WORD_PTR(GObj *))p;
    spiderGroupCount = spiderGroupCount + 1;
    EntryRevivedSpiderGroupManager(gobj);
}

inline void EntryToSpiderGroupManagerForReviveMaster(GObj *group, GObj *master)
{
    reviveGroupIds[reviveGroupIdCount++] = group;
    reviveMaster = master;
}

static int tryToRevive(void)
{
    float pos[4];
    int k = 0;
    int i;
    int j;
    int n;
    GObj *p;

    if (reviveMaster != 0) {
        GetRootPosition(pos, reviveMaster);
        pos[1] -= GOBJ_SUB(reviveMaster)->root.height;
        for (i = 0; i < reviveGroupIdCount; i++) {
            n = CheckSpidersInsideOfReviveRange(spidersInRange, reviveGroupIds[i], pos);
            if (n != 0) {
                for (j = 0; j < n; j++) {
                    spiderPairs[k].group = reviveGroupIds[i];
                    spiderPairs[k].spider = spidersInRange[j];
                    k++;
                    if (k == 5) {
                        if (reviveNext < spiderGroupCount) {
                            int m;

                            p = (GObj *)spiderGroups[reviveNext].rev;
                            UnlockEnemyGenerate(p);
                            debug_StdPrintfDummy("UNLOCK %p: (id:%d)\n", p, reviveNext);
                            p->active = 1;
                            if (DirectCallEnemy(p, 0, pos, ZUnitVector, 0) == 0) {
                                return 0;
                            }
                            ExecuteSEPackage(p, 107);
                            for (m = 0; m < 5; m++) {
                                SetAP1DeadStatus(DeleteSpiderFromLayoutGroup(
                                    spiderPairs[m].group, spiderPairs[m].spider));
                            }
                            SetSpiderGroupReviveStatus(spiderGroups[reviveNext].gobj);
                            reviveNext++;
                            return 1;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

void ExecSpiderGroupManager(void)
{
    int i;
    int total;
    int groups;

    if ((execFrame & 0xF) == 0) {
        tryToRevive();
    }

    if (spiderGroupIdCount != 0 && reviveDone == 0) {
        groups = 0;
        total = 0;
        for (i = 0; i < spiderGroupIdCount; i++) {
            int n = GetAliveSpiders(spiderGroupIds[i]);
            if (n >= 0) {
                total += n;
                groups++;
            }
        }

        if (groups != 0 && total > 0 && total < 5) {
            reviveCounter = reviveCounter + 1;
            if (debug_brain_bar_flag != 0) {
                debug_PrintfDummy(400, 120, 0xFFFFFFFF, "COUNTER %d/%d", reviveCounter,
                                  (60 - systemStatus[0] * 10) / systemStatus[1] * 45);
            }
        }

        if ((60 - systemStatus[0] * 10) / systemStatus[1] * 45 < reviveCounter) {
            for (i = 0; i < spiderGroupIdCount; i++) {
                if (GetAliveSpiders(spiderGroupIds[i]) >= 0) {
                    DeadAllSpiders(spiderGroupIds[i]);
                }
            }
            reviveDone = 1;
        }

        if (debug_brain_bar_flag != 0) {
            if (groups != 0) {
                debug_PrintfDummy(400, 110, 0xFFFFFFFF, "REMAIN %d", total);
            } else {
                debug_PrintfDummy(400, 110, 0xFFFFFFFF, "NO GROUP WAKEUPED");
            }
        }
    }

    execFrame = execFrame + 1;
}

inline void DispAllSpiderGroups(void)
{
    int v = pad[0].flags;
    sgInfoLine = 0;
    if (v & 0x1000) {
        sgSelLine = sgSelLine - 1;
    }
    if (v & 0x4000) {
        sgSelLine = sgSelLine + 1;
    }
    {
        int i;
        for (i = 0; i < spiderGroupIdCount; i++) {
            DispAllMemberOfSpider(spiderGroupIds[i], spiderGroupColors[i]);
        }
    }
}
