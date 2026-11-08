/* title_logo_test.c: the title's logo under the port's menus
 * (port/game/title_logo.c).  CPU only; the game's globals and
 * the two menus' answers are stubs here (settings_test checks
 * ui_settings_covers_title itself).
 *
 *   models    the nine logo models and nothing else
 *   title     on the title's menu nothing hides; a Settings page over the
 *             title hides the logo's models, never another stage object;
 *             back on layout 12 or 13 the logo shows on the first frame,
 *             and back on the Continue screen (19) from the Save states page;
 *             the New Game screen (from the vibration screen, 9) leaves it
 *   extras    the model list over the title hides it; Credits and a
 *             model's stage load (the title fading out on a layout of
 *             neither) keep it hidden until the stage changes, and the next
 *             visit to the title starts with it shown
 *   pause     the pause menu's Settings, on any other stage, hides nothing
 *   stretch   title_back is the title's full-frame model, by name alone, on any
 *             stage and on no other; no other model is, the logo's
 *             included
 *   icon      the application icon's capture (title_icon.c) with the
 *             renderer, the curtain and the icon files stubbed: the pass
 *             off leaves the old table; ICO_TITLE_ICON_WAIT ticks on layout
 *             12 or 13 (counted again after the menu left) start it when
 *             an icon is wanted, never when not; a picture not plain
 *             (ICO_APP_ICON_NOT_PLAIN) leaves it idle and asks again after
 *             another wait, not done for the run; on, every model but "I"
 *             is left out (title_back, st26a_p1, C, I_f, I_sd); the shot over
 *             black is armed after the settle, the clear turns white once
 *             it came, the shot over white after the settle again, then the
 *             pass off, the pair delivered, the curtain off at the first
 *             present past the pass's frames (not over a blend with one;
 *             after ICO_TITLE_ICON_HOLD_MAX ticks without one), done for
 *             the run; an empty answer or none within the
 *             wait is armed again, and after ICO_TITLE_ICON_RETRIES the
 *             run gives up; "the frames differ" takes the pair again once,
 *             then the picture over black alone; the menu left during the
 *             pass ends it and the next visit starts again
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_icon.h" /* ICO_APP_ICON_OK, _GHOST */
#include "title_icon.h"
#include "title_logo.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* the game's globals and the menus' answers */
int stage_no;
int current_layout_id;
int ico_mv_active;   /* options.h */
static int s_covers; /* ui_settings_covers_title */
static int s_list = -1;

int ui_settings_covers_title(void)
{
    return s_covers;
}

int ico_mv_title_list_layout(void)
{
    return s_list;
}

#define TITLE 1
#define PAGE 300          /* a Settings page (a port layout) */
#define LIST 310          /* the model list */
#define EMPTY 0           /* the game's empty layout, while a stage changes */
#define PAUSE 58          /* the Options screen */
#define CONTINUE_FILES 19 /* the Continue screen's file select */
#define VIBE 9            /* the vibration screen (the European disc's) */
#define NEWGAME 320       /* the New Game screen (a port layout) */

/* one frame of draws: the logo's I and a castle mesh of the stage */
static int s_logoShown, s_castleShown;

static void frame(int stage, int layout, int covers)
{
    stage_no = stage;
    current_layout_id = layout;
    s_covers = covers;
    ico_title_logo_update();
    s_castleShown = !ico_title_logo_skip("st26a_p1");
    s_logoShown = !ico_title_logo_skip("I");
}

static void testModels(void)
{
    static const char *const logo[] = {"I", "C", "O", "I_f", "C_f", "O_f", "I_sd", "C_sd", "O_sd"};
    static const char *const other[] = {"st26a_p1", "fogs", "boy", "i", "IC", "O_f2", "", "I_s"};
    for (unsigned i = 0; i < sizeof(logo) / sizeof(logo[0]); i++) {
        CHECK(ico_title_logo_model(logo[i]), "%s is the logo's", logo[i]);
    }
    for (unsigned i = 0; i < sizeof(other) / sizeof(other[0]); i++) {
        CHECK(!ico_title_logo_model(other[i]), "\"%s\" is not the logo's", other[i]);
    }
}

static void testTitle(void)
{
    for (int menu = 12; menu <= 13; menu++) {
        frame(TITLE, 55, 0);
        CHECK(s_logoShown && s_castleShown, "the title's demo layout: shown");
        frame(TITLE, menu, 0);
        CHECK(s_logoShown, "layout %d: shown", menu);
        frame(TITLE, PAGE, 1);
        CHECK(!s_logoShown && s_castleShown,
              "a Settings page over layout %d: the logo hidden, "
              "the stage drawn",
              menu);
        frame(TITLE, PAGE + 1, 1);
        CHECK(!s_logoShown, "another page: hidden");
        frame(TITLE, menu, 0);
        CHECK(s_logoShown, "back on layout %d: shown on the first frame", menu);
    }
    /* Continue's file select (19), the Save states page over it, Back */
    frame(TITLE, 12, 0);
    frame(TITLE, CONTINUE_FILES, 0);
    CHECK(s_logoShown, "the Continue screen: shown");
    frame(TITLE, PAGE, 1);
    CHECK(!s_logoShown, "the Save states page over it: hidden");
    frame(TITLE, CONTINUE_FILES, 0);
    CHECK(s_logoShown, "back on the Continue screen: shown on the first frame");
    /* the New Game screen, reached from the vibration screen: not a page,
       the logo is left as the vibration screen had it (the game's own title
       animation has taken the lettering away by then); a page over the title
       before it still keeps it hidden until a stage change */
    frame(TITLE, 13, 0);
    frame(TITLE, VIBE, 0);
    CHECK(s_logoShown, "the vibration screen: shown");
    frame(TITLE, NEWGAME, 0);
    CHECK(s_logoShown && s_castleShown,
          "the New Game screen: the logo left shown, the stage drawn");
    frame(TITLE, VIBE, 0);
    CHECK(s_logoShown, "back on the vibration screen: shown");
    frame(TITLE, PAGE, 1);
    CHECK(!s_logoShown, "a page over the title: hidden");
    frame(TITLE, NEWGAME, 0);
    CHECK(!s_logoShown, "the New Game screen after a page: stays hidden");
    frame(46, EMPTY, 0);
    CHECK(s_logoShown, "a stage change: shown");
}

static void testExtras(void)
{
    /* the model list, then back */
    frame(TITLE, 12, 0);
    s_list = LIST;
    frame(TITLE, LIST, 0);
    CHECK(!s_logoShown, "the model list over the title: hidden");
    frame(TITLE, PAGE, 1);
    frame(TITLE, 12, 0);
    CHECK(s_logoShown, "back on the title: shown");
    /* a model chosen: the title fades out on another layout, then the
       model's stage */
    frame(TITLE, LIST, 0);
    s_list = -1; /* the viewer is on */
    frame(TITLE, EMPTY, 0);
    CHECK(!s_logoShown, "the title fading out to a model's stage: still hidden");
    frame(46, EMPTY, 0);
    frame(46, LIST + 1, 0);
    CHECK(s_castleShown && s_logoShown, "the model's stage: nothing hidden");
    /* the title again: the logo is there before its menu */
    frame(TITLE, 55, 0);
    CHECK(s_logoShown, "the title again: shown");
    /* Extras > Credits: the empty layout while the title fades out */
    frame(TITLE, 12, 0);
    frame(TITLE, PAGE, 1);
    frame(TITLE, EMPTY, 0);
    CHECK(!s_logoShown, "Credits, the title fading out: hidden");
    frame(88, EMPTY, 0);
    frame(TITLE, EMPTY, 0);
    CHECK(s_logoShown, "the title after the credits: shown");
}

static void testPause(void)
{
    /* settings.c answers 0 from the pause menu; here the state is driven
       as if it did not, to show the stage alone keeps the logo's models */
    frame(26, PAUSE, 0);
    frame(26, PAGE, 0);
    CHECK(s_logoShown && s_castleShown, "the pause menu's Settings: nothing hidden");
    frame(26, PAGE, 1);
    CHECK(s_logoShown, "on another stage nothing is hidden whatever the menus say");
    int hidden = 1;
    CHECK(ico_title_logo_step(&hidden, 26, PAGE, 1) == 0 && hidden == 0,
          "a step on another stage clears the state");
}

static void testStretch(void)
{
    static const char *const other[] = {"I",         "O_sd",       "st26a_p1", "title_back2",
                                        "title_bac", "Title_back", "title",    ""};
    stage_no = TITLE;
    CHECK(ico_title_fullframe_model("title_back"), "title_back on the title: stretched");
    for (unsigned i = 0; i < sizeof(other) / sizeof(other[0]); i++) {
        CHECK(!ico_title_fullframe_model(other[i]), "\"%s\" on the title: not stretched", other[i]);
    }
    /* the name alone decides: the stage number has already moved on while the
       title's last frames are drawn at the start of a load */
    static const int stages[] = {0, 2, 6, 26, 46};
    for (unsigned i = 0; i < sizeof(stages) / sizeof(stages[0]); i++) {
        stage_no = stages[i];
        CHECK(ico_title_fullframe_model("title_back"), "title_back on stage %d: still stretched",
              stages[i]);
    }
    /* the logo's hidden state does not enter into it */
    frame(TITLE, PAGE, 1);
    CHECK(!s_logoShown && ico_title_fullframe_model("title_back"),
          "title_back under a Settings page: still stretched");
    frame(TITLE, 12, 0);
}

/* ------------------------------------------------------------ the icon */

/* the stubs' state */
enum { ANS_NONE = 0, ANS_NULL, ANS_PICTURE };

static struct {
    int wanted;
    int arms, cancels, delivers, singles;
    int pass;            /* on */
    uint8_t passRgba[4]; /* its colour */
    int curtain;
    int answer; /* the next take's: none, NULL, a picture of answerByte */
    uint8_t answerByte;
    uint32_t answerW;
    int deliverR[2];       /* deliver's answers in turn */
    int seenA, seenB;      /* the first byte of the pair deliver got */
    int seenSingle;        /* the first byte deliver_single got */
    uint32_t passLast;     /* pass_last's answer */
    int presentValid;      /* presented's: a present was made */
    uint32_t presentFrame; /* and its frame and alpha */
    float presentAlpha;
} st;

static int stWanted(void)
{
    return st.wanted;
}

static int stArm(void)
{
    st.arms++;
    return 1;
}

static int stTake(uint8_t **rgba, uint32_t *w, uint32_t *h)
{
    const int a = st.answer;
    st.answer = ANS_NONE;
    if (a == ANS_NONE) {
        return 0;
    }
    *rgba = NULL;
    *w = *h = 0;
    if (a == ANS_PICTURE) {
        *w = st.answerW;
        *h = 2;
        *rgba = malloc((size_t)*w * *h * 4);
        memset(*rgba, st.answerByte, (size_t)*w * *h * 4);
    }
    return 1;
}

static void stCancel(void)
{
    st.cancels++;
}

static void stPass(const uint8_t *rgba)
{
    st.pass = rgba != NULL;
    if (rgba) {
        memcpy(st.passRgba, rgba, 4);
    }
}

static void stCurtain(int on)
{
    st.curtain = on;
}

static int stDeliver(const uint8_t *a, const uint8_t *b, int w, int h)
{
    CHECK(w == 4 && h == 2, "icon: the pair's size %dx%d", w, h);
    st.seenA = a[0];
    st.seenB = b[w * h * 4 - 1];
    return st.deliverR[st.delivers++ & 1];
}

static int stSingle(const uint8_t *a, int w, int h)
{
    CHECK(w == 4 && h == 2, "icon: the single picture's size %dx%d", w, h);
    st.seenSingle = a[0];
    st.singles++;
    return ICO_APP_ICON_OK;
}

static uint32_t stPassLast(void)
{
    return st.passLast;
}

static int stPresented(uint32_t *frame, float *alpha)
{
    *frame = st.presentFrame;
    *alpha = st.presentAlpha;
    return st.presentValid;
}

static const IcoTitleIconOps kStub = {stWanted,  stArm,     stTake,   stCancel,   stPass,
                                      stCurtain, stDeliver, stSingle, stPassLast, stPresented};

static void iconStart(int wanted)
{
    ico_title_icon_reset();
    memset(&st, 0, sizeof(st));
    st.wanted = wanted;
    st.answerW = 4;
    st.deliverR[0] = st.deliverR[1] = ICO_APP_ICON_OK;
    /* the presents already past the pass's frames: the curtain comes off
       at the first tick after the pass (testIconCurtain holds them back) */
    st.passLast = 10;
    st.presentValid = 1;
    st.presentFrame = 12;
    st.presentAlpha = 1.0f;
    ico_title_icon_set_ops(&kStub);
}

/* n ticks of the title's menu */
static void menuTicks(int n)
{
    for (int i = 0; i < n; i++) {
        frame(TITLE, 12, 0);
    }
}

/* ticks of the menu until the shot is armed (at most limit); the ticks */
static int untilArmed(int limit)
{
    const int arms = st.arms;
    int n = 0;
    while (st.arms == arms && n < limit) {
        frame(TITLE, 13, 0);
        n++;
    }
    return n;
}

static int isBlack(void)
{
    return st.pass && st.passRgba[0] == 0 && st.passRgba[1] == 0 && st.passRgba[2] == 0;
}

static int isWhite(void)
{
    return st.pass && st.passRgba[0] == 0xFF && st.passRgba[1] == 0xFF && st.passRgba[2] == 0xFF;
}

/* the answer to the armed shot, taken on the next tick */
static void answer(int kind, uint8_t byte)
{
    st.answer = kind;
    st.answerByte = byte;
    frame(TITLE, 12, 0);
}

static void testIconSkip(void)
{
    static const char *const others[] = {"title_back", "st26a_p1", "C",    "O",
                                         "I_f",        "I_sd",     "C_sd", "boy"};
    iconStart(1);
    menuTicks(ICO_TITLE_ICON_WAIT);
    CHECK(ico_title_icon_pass_on() && isBlack() && st.curtain, "icon: the pass on over black");
    CHECK(!ico_title_logo_skip("I"), "icon: I drawn in the pass");
    for (unsigned i = 0; i < sizeof(others) / sizeof(others[0]); i++) {
        CHECK(ico_title_logo_skip(others[i]), "icon: %s left out in the pass", others[i]);
    }
    ico_title_icon_reset();
    CHECK(!ico_title_icon_pass_on() && !st.pass && !st.curtain,
          "icon: reset turns the pass and the curtain off");
    /* off: the old table (the logo shown on the menu, nothing left out) */
    frame(TITLE, 12, 0);
    for (unsigned i = 0; i < sizeof(others) / sizeof(others[0]); i++) {
        CHECK(!ico_title_logo_skip(others[i]), "icon off: %s drawn", others[i]);
    }
    frame(TITLE, PAGE, 1);
    CHECK(ico_title_logo_skip("I") && ico_title_logo_skip("C") &&
              !ico_title_logo_skip("st26a_p1") && !ico_title_logo_skip("title_back"),
          "icon off: a Settings page hides the logo alone");
    frame(TITLE, 12, 0);
    ico_title_icon_set_ops(NULL);
}

static void testIconSteps(void)
{
    /* not wanted: done, nothing on */
    iconStart(0);
    menuTicks(ICO_TITLE_ICON_WAIT + 5);
    CHECK(ico_title_icon_state() == ICO_TITLE_ICON_DONE && !st.pass && !st.curtain && st.arms == 0,
          "icon: not wanted, nothing captured");

    /* wanted but the picture not plain (the CRT filter, a colour
       adjustment or Upscaling on): idle, not done for the run; asked again
       after another wait, and the capture starts once the picture is plain */
    iconStart(ICO_APP_ICON_NOT_PLAIN);
    menuTicks(ICO_TITLE_ICON_WAIT);
    CHECK(ico_title_icon_state() == ICO_TITLE_ICON_IDLE && !st.pass && !st.curtain && st.arms == 0,
          "icon: not plain: idle, nothing captured");
    menuTicks(ICO_TITLE_ICON_WAIT - 1);
    CHECK(ico_title_icon_state() == ICO_TITLE_ICON_IDLE && !st.pass,
          "icon: not plain: the wait counted again from 0");
    st.wanted = 1; /* the settings changed */
    menuTicks(1);
    CHECK(ico_title_icon_pass_on() && isBlack() && st.curtain,
          "icon: plain again: the capture starts at the next wait's end");

    /* the wait: counted on layout 12/13 only, again after the menu left */
    iconStart(1);
    frame(TITLE, 55, 0); /* the intro's layout */
    menuTicks(ICO_TITLE_ICON_WAIT - 1);
    CHECK(!st.pass, "icon: %d ticks of the menu: not yet", ICO_TITLE_ICON_WAIT - 1);
    frame(TITLE, PAGE, 1); /* a Settings page: the logo hidden */
    frame(TITLE, 12, 0);
    menuTicks(ICO_TITLE_ICON_WAIT - 2);
    CHECK(!st.pass, "icon: the count starts again after the menu left");
    ico_mv_active = 1;
    menuTicks(5);
    CHECK(!st.pass, "icon: never inside the model viewer");
    ico_mv_active = 0;
    menuTicks(ICO_TITLE_ICON_WAIT - 1);
    CHECK(!st.pass, "icon: the viewer restarted the count");
    frame(TITLE, 12, 0);
    CHECK(isBlack() && st.curtain && ico_title_icon_state() == ICO_TITLE_ICON_SETTLE_A,
          "icon: %d ticks of the menu: the pass on over black, the curtain on",
          ICO_TITLE_ICON_WAIT);

    /* shot A after the settle; the clear white once it came */
    CHECK(untilArmed(20) == ICO_TITLE_ICON_SETTLE && st.arms == 1,
          "icon: shot A armed after %d ticks", ICO_TITLE_ICON_SETTLE);
    frame(TITLE, 12, 0);
    frame(TITLE, 12, 0);
    CHECK(ico_title_icon_state() == ICO_TITLE_ICON_SHOT_A && isBlack(),
          "icon: waiting for shot A, still black");
    answer(ANS_PICTURE, 0x11);
    CHECK(isWhite() && st.curtain && ico_title_icon_state() == ICO_TITLE_ICON_SETTLE_B,
          "icon: shot A taken, the clear white");
    CHECK(untilArmed(20) == ICO_TITLE_ICON_SETTLE && st.arms == 2,
          "icon: shot B armed after the settle");
    answer(ANS_PICTURE, 0xEE);
    CHECK(st.delivers == 1 && st.seenA == 0x11 && st.seenB == 0xEE,
          "icon: the pair delivered (over black, over white)");
    CHECK(!st.pass && !ico_title_icon_pass_on() && st.curtain,
          "icon: the pass off at once, the curtain held");
    menuTicks(1);
    CHECK(!st.curtain && ico_title_icon_state() == ICO_TITLE_ICON_DONE,
          "icon: the curtain off at the first present past the pass, done");
    menuTicks(ICO_TITLE_ICON_WAIT * 2);
    CHECK(st.arms == 2 && !st.pass, "icon: done for the run");
    ico_title_icon_set_ops(NULL);
}

static void testIconRetries(void)
{
    /* empty answers: armed again, then given up */
    iconStart(1);
    menuTicks(ICO_TITLE_ICON_WAIT);
    untilArmed(20);
    for (int i = 0; i < ICO_TITLE_ICON_RETRIES; i++) {
        answer(ANS_NULL, 0);
        CHECK(ico_title_icon_pass_on() && isBlack(), "icon: drop %d: the pass stays", i + 1);
        CHECK(untilArmed(20) == 1 && st.arms == i + 2, "icon: drop %d: armed again next tick",
              i + 1);
    }
    answer(ANS_NULL, 0);
    CHECK(!st.pass && st.curtain && st.delivers == 0 && st.singles == 0,
          "icon: drop %d: given up, the pass off", ICO_TITLE_ICON_RETRIES + 1);
    menuTicks(1);
    CHECK(!st.curtain && ico_title_icon_state() == ICO_TITLE_ICON_DONE,
          "icon: given up for the run");

    /* no answer within the wait: cancelled and armed again */
    iconStart(1);
    menuTicks(ICO_TITLE_ICON_WAIT);
    untilArmed(20);
    menuTicks(ICO_TITLE_ICON_TIMEOUT);
    CHECK(st.cancels == 1, "icon: no answer within %d ticks: cancelled", ICO_TITLE_ICON_TIMEOUT);
    CHECK(untilArmed(20) == 1 && st.arms == 2, "icon: and armed again");
    answer(ANS_PICTURE, 0x22);
    CHECK(isWhite(), "icon: the late shot A kept");

    /* the pair differs: taken again once, then the picture over black alone */
    iconStart(1);
    st.deliverR[0] = st.deliverR[1] = ICO_APP_ICON_GHOST;
    menuTicks(ICO_TITLE_ICON_WAIT);
    untilArmed(20);
    answer(ANS_PICTURE, 0x31);
    untilArmed(20);
    answer(ANS_PICTURE, 0xC1);
    CHECK(st.delivers == 1 && isBlack() && st.curtain &&
              ico_title_icon_state() == ICO_TITLE_ICON_SETTLE_A,
          "icon: the pair differs: over black again, the curtain on");
    untilArmed(20);
    answer(ANS_PICTURE, 0x32);
    untilArmed(20);
    answer(ANS_PICTURE, 0xC2);
    CHECK(st.delivers == 2 && st.singles == 1 && st.seenSingle == 0x32 && !st.pass,
          "icon: it differs again: the second picture over black alone");
    menuTicks(1);
    CHECK(ico_title_icon_state() == ICO_TITLE_ICON_DONE && !st.curtain, "icon: then done");

    /* a picture of another size over white: as the pair differing */
    iconStart(1);
    st.deliverR[0] = ICO_APP_ICON_OK;
    menuTicks(ICO_TITLE_ICON_WAIT);
    untilArmed(20);
    answer(ANS_PICTURE, 0x41);
    untilArmed(20);
    st.answerW = 6;
    answer(ANS_PICTURE, 0xD1);
    CHECK(st.delivers == 0 && isBlack(), "icon: a resized window: the pair again");
    st.answerW = 4;
    untilArmed(20);
    answer(ANS_PICTURE, 0x42);
    untilArmed(20);
    answer(ANS_PICTURE, 0xD2);
    CHECK(st.delivers == 1 && st.seenA == 0x42 && st.seenB == 0xD2 && !st.pass,
          "icon: then delivered");

    /* the menu left during the pass: ended, started again next visit */
    iconStart(1);
    menuTicks(ICO_TITLE_ICON_WAIT);
    untilArmed(20);
    answer(ANS_PICTURE, 0x51);
    frame(TITLE, EMPTY, 0); /* New Game: the title fading out */
    CHECK(!st.pass && st.curtain, "icon: the menu left: the pass off");
    frame(TITLE, EMPTY, 0);
    frame(TITLE, EMPTY, 0);
    CHECK(!st.curtain && ico_title_icon_state() == ICO_TITLE_ICON_IDLE,
          "icon: the curtain off after the hold, idle");
    menuTicks(ICO_TITLE_ICON_WAIT);
    CHECK(isBlack() && st.arms == 1, "icon: the next visit starts again");
    untilArmed(20);
    CHECK(st.arms == 2, "icon: and arms");
    frame(26, EMPTY, 0); /* the stage changed while armed */
    CHECK(!st.pass && st.cancels >= 1, "icon: a stage change while armed cancels the shot");
    ico_title_icon_reset();
    ico_title_icon_set_ops(NULL);
}

/* the curtain comes off at the first present past the pass's frames, not
   after a count of ticks; a blend with a pass frame is not past them; the
   safety net lifts it after ICO_TITLE_ICON_HOLD_MAX ticks */
static void testIconCurtain(void)
{
    /* the rule alone */
    CHECK(!ico_title_icon_present_clean(10, 1.0f, 10), "icon: the pass's last frame is not clean");
    CHECK(!ico_title_icon_present_clean(3, 1.0f, 10), "icon: an older frame is not clean");
    CHECK(ico_title_icon_present_clean(11, 1.0f, 10), "icon: the next frame at alpha 1 is clean");
    CHECK(!ico_title_icon_present_clean(11, 0.5f, 10) &&
              !ico_title_icon_present_clean(11, 0.0f, 10),
          "icon: the next frame blended with the pass's last is not clean");
    CHECK(ico_title_icon_present_clean(12, 0.25f, 10),
          "icon: a blend of two later frames is clean");

    iconStart(1);
    menuTicks(ICO_TITLE_ICON_WAIT);
    untilArmed(20);
    answer(ANS_PICTURE, 0x61);
    untilArmed(20);
    st.passLast = 40; /* the pass ends with frame 40 open */
    st.presentFrame = 38;
    answer(ANS_PICTURE, 0xE1); /* the pair: the pass off, the curtain held */
    CHECK(!st.pass && st.curtain && st.delivers == 1, "icon: curtain: the pass off, held");
    menuTicks(5); /* presents of frames walked before the pass went off */
    st.presentFrame = 40;
    menuTicks(5);
    CHECK(st.curtain && ico_title_icon_state() == ICO_TITLE_ICON_CURTAIN,
          "icon: curtain: held while the presents show the pass's frames (10 ticks)");
    st.presentFrame = 41;
    st.presentAlpha = 0.5f; /* frame 41 blended with 40 */
    menuTicks(1);
    CHECK(st.curtain, "icon: curtain: held over a blend with the pass's last frame");
    st.presentAlpha = 1.0f;
    menuTicks(1);
    CHECK(!st.curtain && ico_title_icon_state() == ICO_TITLE_ICON_DONE,
          "icon: curtain: off at the first present past the pass, done");

    /* no present past the pass at all (none made, or the frames stopped):
       the safety net */
    iconStart(1);
    menuTicks(ICO_TITLE_ICON_WAIT);
    untilArmed(20);
    answer(ANS_PICTURE, 0x62);
    untilArmed(20);
    st.presentValid = 0;
    answer(ANS_PICTURE, 0xE2);
    menuTicks(ICO_TITLE_ICON_HOLD_MAX - 1);
    CHECK(st.curtain, "icon: curtain: no present yet after %d ticks: held",
          ICO_TITLE_ICON_HOLD_MAX - 1);
    menuTicks(1);
    CHECK(!st.curtain && ico_title_icon_state() == ICO_TITLE_ICON_DONE,
          "icon: curtain: off after %d ticks without a present past the pass",
          ICO_TITLE_ICON_HOLD_MAX);
    ico_title_icon_set_ops(NULL);
}

int main(void)
{
    testModels();
    testTitle();
    testExtras();
    testPause();
    testStretch();
    testIconSkip();
    testIconSteps();
    testIconRetries();
    testIconCurtain();
    if (failures) {
        printf("title_logo_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("title_logo_test: ok\n");
    return 0;
}
