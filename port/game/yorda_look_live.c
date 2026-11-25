/*
 * port/game/yorda_look_live.c
 *
 * Look at Yorda: the game's side (yorda_look.h).  Built with the game's
 * include path and record layout, into the program (and into
 * yorda_look_test with the game stubbed).
 *
 * Where it runs.  ico_yorda_look_tick is called by the Main thread at the
 * end of every Main pass (trace_host.c ico_host_main_tick, which main.c
 * calls after iosOmMain and the draw lists), never from the game's code:
 * no function of the game changes, so a save state of an earlier version
 * that is paused inside the boy's threads resumes in the same code
 * (state_xbuild.h: a frame's function must hash alike).
 *
 * Why there.  The boy's look in the game is subBoyCollision's (boyact.c,
 * the European body and the _ntsc, _jun26 and _aug6 twins), his collision
 * thread, which iosOmMain wakes in its second phase.  Its camera request
 * (Camctrl_SetTarget at priority 1) is taken by the camera's next step
 * (camera-root.c SetCameraMatrix*, Camctrl_Exec), an object function of the
 * next pass's first phase, which drops it again at the step's end
 * (Camctrl_ExitNormal).  A request made here, after the pass and before the
 * next one, is taken by that same camera step: no tick later, and the
 * camera sees it once.  The head turn (_ACTLookTarget_Set, priority 4) is
 * cleared at the start of the boy's next frame (ACTGame_BeforeFunc) as the
 * game's own is.
 *
 * When.  Only on a pass in which the boy's objects ran: his frame count
 * (Act.frame, which BeforeFunc counts under the same pause and active
 * tests as his threads) moved since the last call.  A paused game, photo
 * mode or a menu therefore asks nothing and sees no L3, as the thread did.
 *
 * What.  The tests the look block made, per disc body:
 *   live   IsAbleBoyControl(); his stick not locked by a script (the
 *          flags18 bit ACTCharctrl_Lock clears and subBoyControl tests
 *          before it reads the pad: 48, 43 on the North American and
 *          August 2001 bodies, 42 on the June 2001 one); Developer > Free
 *          movement off (its own L3); the Debug menu's camera off (it reads
 *          L3 too); on the European body also not his own cut to her
 *          capture (PrivInsCamChk, boyact.c's accessor of privInsCam)
 *   hand   ACTGame_FLAG_TETSUNAGI()
 * and, when the rule says to look (yorda_look.c), the block's own calls:
 *   her on the stage: the head turn; with the script's call camera allowed
 *          (flags20 bit 23 European, 12 North American, 9 August 2001; the
 *          June 2001 body has no flag) the camera on the boy and her, or
 *          on layout object 4 at the script's call-camera target when the
 *          script names one (bits 24-25, 13, 10)
 *   her not on the stage, R1 not held (R1 runs this itself): layout object
 *          4 moved toward her other stage and the camera on it, as the R1
 *          branch does, in that body's order of calls
 * No wish bit, no mail, no status: her brain sees nothing of it.
 */
#include <stddef.h>

#include "typedef.h"
#include "main.h"
#include "boyact.h"
#include "act-game.h"
#include "camera-root.h"
#include "commonact.h"
#include "debug.h"
#include "geometryManager.h"
#include "gobj.h"
#include "gv.h"
#include "script.h"
#include <libvu0.h>

#include "../input/pad_script.h" /* ICO_PAD_L3, the game's pad bit 0x200 on every disc */
#include "ico_rows.h"
#include "yorda_look.h"

/* the boy and his frame count at the last call */
static GObj *s_boy;
static int s_frame;

/* the disc's body: its flags20 bits and the stick lock's flags18 bit */
typedef struct {
    int cam;         /* the script's call camera allowed; -1: no such flag */
    int target;      /* the script's call-camera target named (cam bit + 1) */
    int wide;        /* the target test is two bits (the European body) */
    int stick;       /* the stick not locked */
    int pinch;       /* his own cut to her capture is this body's (PrivInsCamChk) */
    int orientFirst; /* her other stage's direction before the roots are read */
} Body;

static Body body(void)
{
    if (ICO_JUN26) {
        return (Body){-1, -1, 0, 42, 0, 1};
    }
    if (ICO_NTSC) {
        return (Body){12, 13, 0, 43, 0, 0};
    }
    if (ICO_AUG6) {
        return (Body){9, 10, 0, 43, 0, 1};
    }
    return (Body){23, 24, 1, 48, 1, 0};
}

static int bit(unsigned long long w, int b)
{
    return (int)((w >> b) & 1);
}

/* the camera on layout object 4 at the script's call-camera target */
static void toScriptTarget(GObj *boy)
{
    void *lo = isysGObjSearchFromObjLayoutID(4);

    if (lo != 0) {
        float t[4] __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};

        ScpCallCameraGetTarget(t);
        SetRootPosition(lo, t);
        Camctrl_SetTarget(boy, lo, 1);
    }
}

static int scriptTarget(const Body *b, unsigned long long f20)
{
    return b->wide ? ((int)(f20 >> b->target) & 3) != 0 : bit(f20, b->target);
}

/* her on the stage: the look block's head turn and camera */
static void towardHer(GObj *boy, const Body *b)
{
    const unsigned long long f20 = GOBJ_ACT(boy)->flags20.ll;

    _ACTLookTarget_Set(boy, girlGObj, 0, 4, 2);
    if (b->cam < 0) {
        Camctrl_SetTarget(boy, girlGObj, 1);
    } else if (bit(f20, b->cam)) {
        if (scriptTarget(b, f20)) {
            toScriptTarget(boy);
        } else {
            Camctrl_SetTarget(boy, girlGObj, 1);
        }
    }
}

/* her not on the stage: the R1 branch's helper toward her other stage */
static void towardHerStage(GObj *boy, const Body *b)
{
    void *lo = isysGObjSearchFromObjLayoutID(4);
    /* the w lanes the game's own arrays left as the stack had them: 0 */
    float root[4] __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
    float cam[4] __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
    float ofs[4] __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
    float work[4] __attribute__((aligned(16))) = {0.0f, 0.0f, 0.0f, 0.0f};
    unsigned long long f20;

    if (lo == 0) {
        return;
    }
    if (b->orientFirst) {
        GetOtherStageGirlOrient(ofs, (float *)GetCurrentCameraSet2());
    }
    root[0] = test_CURRENTROOT(boyGObj)[0];
    root[1] = test_CURRENTROOT(boyGObj)[1];
    root[2] = test_CURRENTROOT(boyGObj)[2];
    cam[0] = ((float *)GetCurrentCameraSet2())[0];
    cam[1] = ((float *)GetCurrentCameraSet2())[1];
    cam[2] = ((float *)GetCurrentCameraSet2())[2];
    if (!b->orientFirst) {
        GetOtherStageGirlOrient(ofs, cam);
    }
    sceVu0ScaleVector(ofs, ofs, _DistGV(cam, root));
    sceVu0AddVector(work, cam, ofs);
    SetRootPosition(lo, work);
    f20 = GOBJ_ACT(boy)->flags20.ll;
    if (b->cam < 0) {
        Camctrl_SetTarget(boy, lo, 1);
    } else if (bit(f20, b->cam)) {
        if (scriptTarget(b, f20)) {
            toScriptTarget(boy);
        } else {
            Camctrl_SetTarget(boy, lo, 1);
        }
    }
}

void ico_yorda_look_tick(void)
{
    GObj *boy = boyGObj;
    const Body b = body();
    Act *s;
    int live;

    if (boy == 0 || boy->act == 0) {
        s_boy = 0;
        return;
    }
    s = GOBJ_ACT(boy);
    if (boy == s_boy && s->frame == s_frame) {
        return; /* his objects did not run in this pass */
    }
    s_boy = boy;
    s_frame = s->frame;
    live = IsAbleBoyControl() != 0 && bit(s->flags18.ll, b.stick) && ico_free_movement() == 0 &&
           debug_camera_flag == 0 && !(b.pinch && PrivInsCamChk() != 0);
    if (!ico_yorda_look(pad[0].now & ICO_PAD_L3, live, ACTGame_FLAG_TETSUNAGI() != 0, stage_no)) {
        return;
    }
    if (girlGObj != 0) {
        towardHer(boy, &b);
    } else if ((s->pad.now & 0x8) == 0) {
        towardHerStage(boy, &b);
    }
}

void ico_yorda_look_tick_clear(void)
{
    s_boy = 0;
    s_frame = 0;
}
