/* title_logo_test.c: the title's logo under the port's menus
 * (port/game/title_logo.c).  CPU only; the game's globals and
 * the two menus' answers are stubs here (settings_test checks
 * ui_settings_covers_title itself).
 *
 *   models    the nine logo models and nothing else
 *   title     on the title's menu nothing hides; a Settings page over the
 *             title hides the logo's models, never another stage object;
 *             back on layout 12 or 13 the logo shows on the first frame
 *   extras    the model list over the title hides it; Credits and a
 *             model's stage load (the title fading out on a layout of
 *             neither) keep it hidden until the stage changes, and the next
 *             visit to the title starts with it shown
 *   pause     the pause menu's Settings, on any other stage, hides nothing
 *   stretch   title_back is the title's full-screen model on the title's
 *             stage and on no other; no other model is, the logo's
 *             included
 */
#include <stdio.h>
#include <string.h>

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
#define PAGE 300 /* a Settings page (a port layout) */
#define LIST 310 /* the model list */
#define EMPTY 0  /* the game's empty layout, while a stage changes */
#define PAUSE 58 /* the Options screen */

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
    CHECK(ico_title_stretch_model("title_back"), "title_back on the title: stretched");
    for (unsigned i = 0; i < sizeof(other) / sizeof(other[0]); i++) {
        CHECK(!ico_title_stretch_model(other[i]), "\"%s\" on the title: not stretched", other[i]);
    }
    static const int stages[] = {0, 2, 6, 26, 46};
    for (unsigned i = 0; i < sizeof(stages) / sizeof(stages[0]); i++) {
        stage_no = stages[i];
        CHECK(!ico_title_stretch_model("title_back"), "title_back on stage %d: not stretched",
              stages[i]);
    }
    /* the logo's hidden state does not enter into it */
    frame(TITLE, PAGE, 1);
    CHECK(!s_logoShown && ico_title_stretch_model("title_back"),
          "title_back under a Settings page: still stretched");
    frame(TITLE, 12, 0);
}

int main(void)
{
    testModels();
    testTitle();
    testExtras();
    testPause();
    testStretch();
    if (failures) {
        printf("title_logo_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("title_logo_test: ok\n");
    return 0;
}
