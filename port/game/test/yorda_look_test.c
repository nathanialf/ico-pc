/*
 * port/game/test/yorda_look_test.c
 *
 * Look at Yorda's press, toggle and end rules (port/game/yorda_look.h) on
 * the CPU: Off answers 0 over pseudo-random input; Hold looks while L3 is
 * held; Toggle starts with a press and ends with the next, a held press
 * does not repeat; a toggled look ends without control, with her hand
 * taken, on a new stage and with a Camera reset (camera_reset.h, the real
 * call), and an L3 held through any of those is no press afterwards; the
 * Main tick clock (a pause keeps the toggle, L3 down at the first tick back
 * is held); a state's load ends a toggled look; the game's side
 * (yorda_look_live.c, compiled in, the game stubbed below) on every disc
 * body: the head turn and the camera (or the script's call-camera target)
 * in the look block's order, her other stage as R1 does there, nothing
 * while R1 runs that itself, each live test (control, a script's stick
 * lock ending a toggle, Free movement, the Debug camera, her hand, the
 * European body's capture cut), nothing on a pass the boy's objects did
 * not run; the save-state block (one FRESH block whose bytes copied back
 * resume the toggle).
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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

#include "../../core/region.h"
#include "camera_reset.h"
#include "config.h"
#include "options.h"
#include "state_registry.h"
#include "yorda_look.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define L3 0x200
#define R3 0x400

static unsigned s_rng = 2266u;

static unsigned rnd(void)
{
    s_rng = s_rng * 1103515245u + 12345u;
    return (s_rng >> 16) & 0x7fffu;
}

/* one tick under control, no hand, stage 3 */
static int tick(int l3)
{
    return ico_yorda_look(l3 ? L3 : 0, 1, 0, 3);
}

static void fresh(int type)
{
    ico_yorda_look_clear();
    ico_opt_set_yorda_look(type);
}

static void test_off(void)
{
    int i, none = 0;
    const unsigned long n0 = ico_yorda_look_count();

    fresh(ICO_YORDA_LOOK_OFF);
    for (i = 0; i < 2000; i++) {
        const int l3 = (rnd() & 1) ? L3 : 0;
        const int live = (rnd() % 4) != 0;
        const int hand = (rnd() % 5) == 0;
        const int stage = (int)(rnd() % 3);

        none += ico_yorda_look(l3, live, hand, stage) == 0;
    }
    CHECK(none == 2000);
    CHECK(ico_yorda_look_count() == n0);
    /* a toggle left on when the row goes Off: gone, and not back on Toggle */
    fresh(ICO_YORDA_LOOK_TOGGLE);
    CHECK(tick(0) == 0 && tick(1) == 1);
    ico_opt_set_yorda_look(ICO_YORDA_LOOK_OFF);
    CHECK(tick(1) == 0);
    ico_opt_set_yorda_look(ICO_YORDA_LOOK_TOGGLE);
    CHECK(tick(1) == 0 && tick(0) == 0);
}

static void test_hold(void)
{
    int i;
    const unsigned long n0 = ico_yorda_look_count();

    fresh(ICO_YORDA_LOOK_HOLD);
    CHECK(tick(0) == 0);
    for (i = 0; i < 30; i++) {
        CHECK(tick(1) == 1);
    }
    CHECK(tick(0) == 0);
    CHECK(ico_yorda_look_count() == n0 + 1); /* one look started */
    CHECK(tick(1) == 1 && tick(0) == 0);
    CHECK(ico_yorda_look_count() == n0 + 2);
    /* not under control or holding her hand: no look while held */
    CHECK(ico_yorda_look(L3, 0, 0, 3) == 0);
    CHECK(ico_yorda_look(L3, 1, 1, 3) == 0);
    CHECK(tick(1) == 1); /* Hold follows the button: control back, still held */
}

static void test_toggle(void)
{
    int i;
    const unsigned long n0 = ico_yorda_look_count();

    fresh(ICO_YORDA_LOOK_TOGGLE);
    CHECK(tick(0) == 0);
    CHECK(tick(1) == 1); /* a press starts it */
    for (i = 0; i < 30; i++) {
        CHECK(tick(1) == 1); /* held: no repeat */
    }
    for (i = 0; i < 30; i++) {
        CHECK(tick(0) == 1); /* let go: it stays on */
    }
    CHECK(tick(1) == 0); /* the second press ends it */
    CHECK(tick(1) == 0 && tick(0) == 0);
    CHECK(ico_yorda_look_count() == n0 + 1);
    CHECK(tick(1) == 1 && ico_yorda_look_count() == n0 + 2);
}

/* the ends of a toggled look, and an L3 held through each is no press */
static void test_ends(void)
{
    /* no control (a cutscene, the cut to her capture, Free movement) */
    fresh(ICO_YORDA_LOOK_TOGGLE);
    CHECK(tick(0) == 0 && tick(1) == 1 && tick(0) == 1);
    CHECK(ico_yorda_look(0, 0, 0, 3) == 0);
    CHECK(tick(0) == 0); /* not back with control */
    CHECK(tick(1) == 1);
    CHECK(ico_yorda_look(L3, 0, 0, 3) == 0);
    CHECK(tick(1) == 0); /* held through it: no press */
    CHECK(tick(0) == 0 && tick(1) == 1);
    /* her hand taken; a press while holding it starts nothing */
    CHECK(ico_yorda_look(0, 1, 1, 3) == 0);
    CHECK(tick(0) == 0);
    CHECK(ico_yorda_look(L3, 1, 1, 3) == 0);
    CHECK(ico_yorda_look(0, 1, 1, 3) == 0 && tick(0) == 0);
    /* a new stage */
    CHECK(tick(1) == 1 && tick(0) == 1);
    CHECK(ico_yorda_look(0, 1, 0, 4) == 0);
    CHECK(ico_yorda_look(0, 1, 0, 4) == 0);
    CHECK(ico_yorda_look(L3, 1, 0, 4) == 1);
    /* a stage change on the tick of a press: the press starts a new look */
    CHECK(ico_yorda_look(0, 1, 0, 4) == 1);
    CHECK(ico_yorda_look(L3, 1, 0, 5) == 1);
    CHECK(ico_yorda_look(0, 1, 0, 5) == 1);
}

static void test_camera_reset(void)
{
    float sx = 0.0f, sz = 0.0f;

    /* Camera reset on: R3 ends a toggled look (camera_reset.c) */
    fresh(ICO_YORDA_LOOK_TOGGLE);
    ico_cam_reset_clear();
    ico_opt_set_camera_reset(1);
    CHECK(tick(0) == 0 && tick(1) == 1 && tick(0) == 1);
    CHECK(ico_cam_reset(0, 1, &sx, &sz) == 0);
    CHECK(ico_cam_reset(R3, 1, &sx, &sz) == 1);
    CHECK(tick(0) == 0);
    /* Hold: L3 held keeps looking (the player's own input) */
    ico_opt_set_yorda_look(ICO_YORDA_LOOK_HOLD);
    CHECK(tick(1) == 1);
    CHECK(ico_cam_reset(0, 1, &sx, &sz) == 0 && ico_cam_reset(R3, 1, &sx, &sz) == 1);
    CHECK(tick(1) == 1);
    /* Camera reset off: R3 changes nothing */
    ico_opt_set_camera_reset(0);
    fresh(ICO_YORDA_LOOK_TOGGLE);
    CHECK(tick(0) == 0 && tick(1) == 1);
    CHECK(ico_cam_reset(0, 1, &sx, &sz) == 0 && ico_cam_reset(R3, 1, &sx, &sz) == 0);
    CHECK(tick(0) == 1);
    ico_cam_reset_clear();
}

/* the Main tick clock (ico_yorda_look_set_clock) */
static unsigned s_tick;

static unsigned fake_clock(void)
{
    return s_tick;
}

static void test_clock(void)
{
    fresh(ICO_YORDA_LOOK_TOGGLE);
    ico_yorda_look_set_clock(fake_clock);
    s_tick = 200;
    CHECK(tick(0) == 0);
    s_tick = 201;
    CHECK(tick(1) == 1);
    s_tick = 202;
    CHECK(tick(0) == 1);
    /* a pause or photo mode (the boy's thread does not run for 50 ticks):
       the look is still on after it, and L3 down at the first tick back is
       held, not a press */
    s_tick = 253;
    CHECK(tick(1) == 1);
    s_tick = 254;
    CHECK(tick(0) == 1);
    s_tick = 255;
    CHECK(tick(1) == 0); /* a real press ends it */
    /* the same with the look off: L3 down at the first tick back starts
       nothing */
    s_tick = 300;
    CHECK(tick(1) == 0);
    s_tick = 301;
    CHECK(tick(0) == 0);
    s_tick = 302;
    CHECK(tick(1) == 1);
    /* a second call in the same tick is not a new stretch */
    CHECK(tick(1) == 1);
    /* a load rewinds the clock */
    s_tick = 40;
    CHECK(tick(0) == 1);
    s_tick = 41;
    CHECK(tick(1) == 0);
    ico_yorda_look_set_clock(NULL);
}

/* --- the game's side (yorda_look_live.c), the game stubbed ---------------- */
GObj *boyGObj;
GObj *girlGObj;
PadState pad[16];
int stage_no = 3;
int debug_camera_flag;

static GObj s_boyObj, s_girlObj, s_lo;
static Act s_boyAct;
static unsigned char s_able = 1;
static int s_pinch, s_hand, s_haveLo = 1;

unsigned char IsAbleBoyControl(void)
{
    return s_able;
}

int PrivInsCamChk(void)
{
    return s_pinch;
}

unsigned char ACTGame_FLAG_TETSUNAGI(void)
{
    return (unsigned char)s_hand;
}

/* the calls the side makes, in order */
enum { C_HEAD = 1, C_CAM, C_TARGET, C_ROOT, C_ORIENT, C_ROOTREAD };

typedef struct {
    int kind;
    const void *a, *b;
    int n;
    float v[3];
} Call;

static Call s_calls[16];
static int s_ncalls;

static void note(int kind, const void *a, const void *b, int n, const float *v)
{
    if (s_ncalls < 16) {
        Call *c = &s_calls[s_ncalls];

        c->kind = kind;
        c->a = a;
        c->b = b;
        c->n = n;
        c->v[0] = v != NULL ? v[0] : 0.0f;
        c->v[1] = v != NULL ? v[1] : 0.0f;
        c->v[2] = v != NULL ? v[2] : 0.0f;
    }
    s_ncalls++;
}

int _ACTLookTarget_Set(GObj *self, GObj *target, float *pos, int pri, int mode)
{
    note(C_HEAD, self, target, pri * 10 + mode, pos);
    return 1;
}

void Camctrl_SetTarget(GObj *gobj, GObj *subGObj, int pri)
{
    note(C_CAM, gobj, subGObj, pri, NULL);
}

void *isysGObjSearchFromObjLayoutID(int layoutId)
{
    return layoutId == 4 && s_haveLo ? &s_lo : NULL;
}

void ScpCallCameraGetTarget(float *dst)
{
    dst[0] = 7.0f;
    dst[1] = 8.0f;
    dst[2] = 9.0f;
    note(C_TARGET, NULL, NULL, 0, NULL);
}

void SetRootPosition(GObj *obj, void *pos)
{
    note(C_ROOT, obj, NULL, 0, (const float *)pos);
}

static float s_camSet[4] = {100.0f, 0.0f, 0.0f, 0.0f};
static float s_root[4] = {100.0f, 0.0f, 40.0f, 0.0f};

int *GetCurrentCameraSet2(void)
{
    return (int *)s_camSet;
}

void GetOtherStageGirlOrient(float *orient, float *root)
{
    (void)root;
    orient[0] = 0.0f;
    orient[1] = 0.0f;
    orient[2] = 1.0f;
    orient[3] = 0.0f;
    note(C_ORIENT, NULL, NULL, 0, NULL);
}

float *test_CURRENTROOT(GObj *self)
{
    note(C_ROOTREAD, self, NULL, 0, NULL);
    return s_root;
}

/* the VU0 vector library's two, as port/math/libvu0.c computes them */
void sceVu0ScaleVector(void *dst, void *src, float scale)
{
    float *d = dst;
    const float *v = src;

    for (int i = 0; i < 4; i++) {
        d[i] = v[i] * scale;
    }
}

void sceVu0AddVector(void *dst, void *a, void *b)
{
    float *d = dst;
    const float *p = a, *q = b;

    for (int i = 0; i < 4; i++) {
        d[i] = p[i] + q[i];
    }
}

float _DistGV(void *a, void *b)
{
    const float *p = a, *q = b;
    const float dx = p[0] - q[0], dy = p[1] - q[1], dz = p[2] - q[2];

    return dx * dx + dy * dy + dz * dz == 1600.0f ? 40.0f : -1.0f;
}

/* the disc body's numbers (boyact.c's four subBoyCollision copies and
   subBoyControl's stick test) */
typedef struct {
    const char *slug;
    int cam, target, stick, pinch, orientFirst;
} Disc;

static const Disc kDiscs[] = {
    {"pal", 23, 24, 48, 1, 0},   {"jpn", 23, 24, 48, 1, 0},  {"kor", 23, 24, 48, 1, 0},
    {"cn", 23, 24, 48, 1, 0},    {"ntsc", 12, 13, 43, 0, 0}, {"aug6", 9, 10, 43, 0, 1},
    {"jun26", -1, -1, 42, 0, 1},
};

/* one Main pass with the boy's objects run (his frame counted) and L3 as
   given; the calls it made */
static int pass(int l3)
{
    s_boyAct.frame++;
    pad[0].now = l3 ? L3 : 0;
    s_ncalls = 0;
    ico_yorda_look_tick();
    return s_ncalls;
}

static void sideReset(const Disc *d)
{
    memset(&s_boyAct, 0, sizeof s_boyAct);
    s_boyObj.act = &s_boyAct;
    boyGObj = &s_boyObj;
    girlGObj = &s_girlObj;
    s_boyAct.flags18.ll = 1ULL << d->stick;                 /* the stick not locked */
    s_boyAct.flags20.ll = d->cam >= 0 ? 1ULL << d->cam : 0; /* the call camera allowed */
    s_able = 1;
    s_pinch = s_hand = 0;
    s_haveLo = 1;
    debug_camera_flag = 0;
    ico_yorda_look_tick_clear();
    fresh(ICO_YORDA_LOOK_HOLD);
}

static int isCall(int i, int kind, const void *a, const void *b, int n)
{
    return i < s_ncalls && i < 16 && s_calls[i].kind == kind && s_calls[i].a == a &&
           s_calls[i].b == b && s_calls[i].n == n;
}

static void sideDisc(const Disc *d)
{
    int n;

    CHECK(ico_region_select(d->slug) == 0);
    sideReset(d);
    /* off: nothing asked, whatever L3 does */
    ico_opt_set_yorda_look(ICO_YORDA_LOOK_OFF);
    CHECK(pass(1) == 0 && pass(0) == 0);
    ico_opt_set_yorda_look(ICO_YORDA_LOOK_HOLD);
    CHECK(pass(0) == 0);
    /* her on the stage, the call camera allowed: the head turn at priority
       4, then the camera on the boy and her at priority 1 */
    CHECK(pass(1) == 2);
    CHECK(isCall(0, C_HEAD, &s_boyObj, &s_girlObj, 42));
    CHECK(isCall(1, C_CAM, &s_boyObj, &s_girlObj, 1));
    /* the same pass again (his objects did not run: a pause, photo mode):
       nothing, and the rule not asked */
    s_ncalls = 0;
    ico_yorda_look_tick();
    CHECK(s_ncalls == 0);
    /* the script's call camera off: the head only (the June 2001 body has
       no such flag: the camera too) */
    s_boyAct.flags20.ll = 0;
    n = pass(1);
    CHECK(isCall(0, C_HEAD, &s_boyObj, &s_girlObj, 42));
    CHECK(d->cam >= 0 ? n == 1 : n == 2 && isCall(1, C_CAM, &s_boyObj, &s_girlObj, 1));
    /* the script names its call-camera target: layout object 4 put there,
       the camera on it */
    if (d->cam >= 0) {
        s_boyAct.flags20.ll = (1ULL << d->cam) | (1ULL << d->target);
        CHECK(pass(1) == 4);
        CHECK(isCall(0, C_HEAD, &s_boyObj, &s_girlObj, 42));
        CHECK(isCall(1, C_TARGET, NULL, NULL, 0));
        CHECK(isCall(2, C_ROOT, &s_lo, NULL, 0) && s_calls[2].v[0] == 7.0f &&
              s_calls[2].v[2] == 9.0f);
        CHECK(isCall(3, C_CAM, &s_boyObj, &s_lo, 1));
        s_boyAct.flags20.ll = 1ULL << d->cam;
    }
    /* not under control, the stick locked by a script, Free movement's or
       the Debug camera's L3, her hand: nothing */
    s_able = 0;
    CHECK(pass(1) == 0);
    s_able = 1;
    s_boyAct.flags18.ll = 0;
    CHECK(pass(1) == 0);
    s_boyAct.flags18.ll = 1ULL << d->stick;
    debug_camera_flag = 1;
    CHECK(pass(1) == 0);
    debug_camera_flag = 0;
    s_hand = 1;
    CHECK(pass(1) == 0);
    s_hand = 0;
    /* his own cut to her capture: the European body's alone */
    s_pinch = 1;
    CHECK(pass(1) == (d->pinch ? 0 : 2));
    s_pinch = 0;
    CHECK(pass(1) == 2);
    /* a Toggle ends when a script locks his stick, as without control */
    ico_opt_set_yorda_look(ICO_YORDA_LOOK_TOGGLE);
    CHECK(pass(0) == 0 && pass(1) == 2 && pass(0) == 2);
    s_boyAct.flags18.ll = 0;
    CHECK(pass(0) == 0);
    s_boyAct.flags18.ll = 1ULL << d->stick;
    CHECK(pass(0) == 0);
    ico_opt_set_yorda_look(ICO_YORDA_LOOK_HOLD);
    /* her not on the stage: layout object 4 toward her other stage, from
       the camera by the boy's distance, then the camera on it; the body's
       own order of reads */
    girlGObj = NULL;
    n = pass(1);
    CHECK(n == 6);
    CHECK(isCall(d->orientFirst ? 0 : 3, C_ORIENT, NULL, NULL, 0));
    for (int i = 0; i < 3; i++) {
        CHECK(isCall(d->orientFirst + i, C_ROOTREAD, &s_boyObj, NULL, 0));
    }
    CHECK(isCall(4, C_ROOT, &s_lo, NULL, 0) && s_calls[4].v[0] == 100.0f &&
          s_calls[4].v[2] == 40.0f);
    CHECK(isCall(5, C_CAM, &s_boyObj, &s_lo, 1));
    /* R1 held there: the game's own branch runs it, the look adds nothing */
    s_boyAct.pad.now = 0x8;
    CHECK(pass(1) == 0);
    s_boyAct.pad.now = 0;
    /* no layout object 4: nothing */
    s_haveLo = 0;
    CHECK(pass(1) == 0);
    s_haveLo = 1;
    /* no boy: nothing */
    boyGObj = NULL;
    CHECK(pass(1) == 0);
    boyGObj = &s_boyObj;
    girlGObj = &s_girlObj;
}

static void test_side(void)
{
    for (size_t i = 0; i < sizeof kDiscs / sizeof kDiscs[0]; i++) {
        sideDisc(&kDiscs[i]);
    }
    CHECK(ico_region_select("pal") == 0);
    fresh(ICO_YORDA_LOOK_OFF);
}

/* a state's load ends a toggled look (any state: one of an earlier version
   has no block and keeps this run's bytes) */
static void test_loaded(void)
{
    fresh(ICO_YORDA_LOOK_TOGGLE);
    CHECK(tick(0) == 0 && tick(1) == 1 && tick(0) == 1);
    ico_yorda_look_state_loaded();
    CHECK(tick(0) == 0 && tick(1) == 1);
    /* Hold: L3 still held after the load looks at once */
    ico_opt_set_yorda_look(ICO_YORDA_LOOK_HOLD);
    ico_yorda_look_state_loaded();
    CHECK(tick(1) == 1);
}

static void test_block(void)
{
    const IcoStateEntry *e = NULL;
    unsigned char saved[32];
    int i;

    ico_state_registry_reset();
    CHECK(ico_yorda_look_state_register() == 0);
    CHECK(ico_state_registry_count() == 1);
    for (i = 0; i < ico_state_registry_count(); i++) {
        if (strcmp(ico_state_registry_entry(i)->name, "yorda_look.state") == 0) {
            e = ico_state_registry_entry(i);
        }
    }
    CHECK(e != NULL);
    if (e == NULL) {
        return;
    }
    CHECK(e->flags == ICO_STATE_FRESH && e->n == 4 * sizeof(int) && e->n <= sizeof saved);
    CHECK(ico_yorda_look_state_register() != 0);
    /* toggled on, saved; ended; copied back: on, and L3 held is no press */
    fresh(ICO_YORDA_LOOK_TOGGLE);
    CHECK(tick(0) == 0 && tick(1) == 1);
    memcpy(saved, e->p, e->n);
    CHECK(tick(0) == 1 && tick(1) == 0 && tick(0) == 0);
    memcpy(e->p, saved, e->n);
    CHECK(tick(1) == 1 && tick(0) == 1);
    ico_state_registry_reset();
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ico_config_reset("/nonexistent/yorda_look_test.toml", "/nonexistent/yorda_look_test.ini");
    ico_opt_reload();
    CHECK(ico_opt_yorda_look() == ICO_YORDA_LOOK_OFF); /* the default */
    test_off();
    test_hold();
    test_toggle();
    test_ends();
    test_camera_reset();
    test_clock();
    test_side();
    test_loaded();
    test_block();
    if (failures != 0) {
        fprintf(stderr, "yorda_look_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("yorda_look_test: ok\n");
    return 0;
}
