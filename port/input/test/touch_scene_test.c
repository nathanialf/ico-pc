/*
 * port/input/test/touch_scene_test.c
 *
 * ctest touch_scene: Touch controls "Always, Start only in scenes"
 * (touch.c ico_touch_scene_alpha), on the CPU:
 *   (a) outside a scene, and in the other modes in a scene, every alpha is
 *       as the layout had it (Auto, Always, Off; the fourth mode with scene 0);
 *   (b) in a scene in the fourth mode every control whose pad bits lack
 *       ICO_PAD_START (the D-pad, the faces, the shoulders, Select, the
 *       stick, the look pad) ends at alpha 0 and the Start control keeps
 *       its own; the first call starts at the target with no fade;
 *   (c) the fade takes ICO_TOUCH_FADE_NS each way and is linear;
 *   (d) a Start the player hid (alpha 0) stays 0, a Start with a lower alpha
 *       keeps that one, and a control with the Start bit among others is kept
 *       (selected by the bit, not by the control's id);
 *   (e) the mapper is untouched (the helper takes a copy; the original
 *       layout keeps its alpha) and the mode counts as Always for the
 *       mapping and the opacity;
 *   (f) the Frame rate counter's place follows the faded copy: with only
 *       Start left it is the place a layout with those controls dropped gets.
 */
#include <stdio.h>
#include <string.h>

#include "input.h"
#include "touch.h"
#include "touch_gap.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define MS 1000000ull
#define S 1000000000ull
#define SCENES ICO_TOUCH_MODE_ALWAYS_SCENES

static float absf(float v)
{
    return v < 0 ? -v : v;
}

static IcoTouchLayout fresh(void)
{
    IcoTouchInsets in = {0, 0, 0, 0};

    return ico_touch_layout(2400, 1080, in, ICO_TOUCH_MEDIUM);
}

static int has_start(const IcoTouchLayout *l, int i)
{
    return (l->button[i].pad & ICO_PAD_START) != 0;
}

static void test_unchanged(void)
{
    const int modes[] = {ICO_TOUCH_MODE_OFF, ICO_TOUCH_MODE_AUTO, ICO_TOUCH_MODE_ALWAYS};
    const IcoTouchLayout ref = fresh();
    IcoTouchSceneFade f;
    IcoTouchLayout l;
    int m, i;

    for (m = 0; m < 3; m++) {
        memset(&f, 0, sizeof(f));
        l = ref;
        CHECK(ico_touch_scene_alpha(&l, &f, modes[m], 1, S) == 0.0f);
        CHECK(ico_touch_scene_alpha(&l, &f, modes[m], 1, 2 * S) == 0.0f);
        CHECK(memcmp(&l, &ref, sizeof(l)) == 0);
    }
    /* the fourth mode outside a scene */
    memset(&f, 0, sizeof(f));
    l = ref;
    CHECK(ico_touch_scene_alpha(&l, &f, SCENES, 0, S) == 0.0f);
    CHECK(ico_touch_scene_alpha(&l, &f, SCENES, 0, 5 * S) == 0.0f);
    CHECK(memcmp(&l, &ref, sizeof(l)) == 0);
    for (i = 0; i < ICO_TOUCH_BUTTONS; i++) {
        CHECK(l.button[i].alpha == 1.0f);
    }
}

static void test_scene_hides_all_but_start(void)
{
    const IcoTouchLayout ref = fresh();
    IcoTouchSceneFade f;
    IcoTouchLayout l = ref;
    int i, starts = 0;

    memset(&f, 0, sizeof(f));
    /* the first call is at the target: no fade */
    CHECK(ico_touch_scene_alpha(&l, &f, SCENES, 1, S) == 1.0f);
    for (i = 0; i < ICO_TOUCH_BUTTONS; i++) {
        if (has_start(&l, i)) {
            starts++;
            CHECK(l.button[i].alpha == ref.button[i].alpha);
            CHECK(l.button[i].active == ref.button[i].active);
        } else {
            CHECK(l.button[i].alpha == 0.0f);
            CHECK(l.button[i].active == ref.button[i].active); /* still takes touches */
        }
    }
    CHECK(starts == 1 && has_start(&l, ICO_TOUCH_B_START));
    CHECK(l.stickAlpha == 0.0f && l.lookAlpha == 0.0f);
    CHECK(l.stickActive == ref.stickActive && l.lookActive == ref.lookActive);
    /* the geometry is untouched */
    CHECK(l.button[ICO_TOUCH_B_CROSS].rect.x == ref.button[ICO_TOUCH_B_CROSS].rect.x);
}

static void test_fade(void)
{
    const IcoTouchLayout ref = fresh();
    IcoTouchSceneFade f;
    IcoTouchLayout l;
    int i;

    memset(&f, 0, sizeof(f));
    l = ref;
    ico_touch_scene_alpha(&l, &f, SCENES, 0, S); /* start shown */
    /* the scene begins: a linear 300 ms fade out */
    l = ref;
    CHECK(absf(ico_touch_scene_alpha(&l, &f, SCENES, 1, S + 150 * MS) - 0.5f) < 1e-5f);
    CHECK(absf(l.button[ICO_TOUCH_B_CROSS].alpha - 0.5f) < 1e-5f);
    CHECK(absf(l.stickAlpha - 0.5f) < 1e-5f && absf(l.lookAlpha - 0.5f) < 1e-5f);
    CHECK(l.button[ICO_TOUCH_B_START].alpha == 1.0f);
    l = ref;
    CHECK(ico_touch_scene_alpha(&l, &f, SCENES, 1, S + 300 * MS) == 1.0f);
    CHECK(l.button[ICO_TOUCH_B_CROSS].alpha == 0.0f);
    l = ref;
    CHECK(ico_touch_scene_alpha(&l, &f, SCENES, 1, S + 900 * MS) == 1.0f);
    CHECK(l.button[ICO_TOUCH_B_CROSS].alpha == 0.0f);
    /* it ends: the same fade back in, partway */
    l = ref;
    CHECK(absf(ico_touch_scene_alpha(&l, &f, SCENES, 0, S + 1050 * MS) - 0.5f) < 1e-5f);
    CHECK(absf(l.button[ICO_TOUCH_B_DOWN].alpha - 0.5f) < 1e-5f);
    /* the scene returns mid-fade: it turns around from where it was */
    l = ref;
    CHECK(absf(ico_touch_scene_alpha(&l, &f, SCENES, 1, S + 1110 * MS) - 0.7f) < 1e-4f);
    l = ref;
    ico_touch_scene_alpha(&l, &f, SCENES, 0, S + 2000 * MS);
    l = ref;
    CHECK(ico_touch_scene_alpha(&l, &f, SCENES, 0, S + 2000 * MS + 300 * MS) == 0.0f);
    for (i = 0; i < ICO_TOUCH_BUTTONS; i++) {
        CHECK(l.button[i].alpha == 1.0f);
    }
    /* leaving the mode mid-scene fades back in too */
    l = ref;
    ico_touch_scene_alpha(&l, &f, SCENES, 1, S + 3000 * MS);
    l = ref;
    ico_touch_scene_alpha(&l, &f, SCENES, 1, S + 3300 * MS);
    l = ref;
    CHECK(ico_touch_scene_alpha(&l, &f, ICO_TOUCH_MODE_ALWAYS, 1, S + 3450 * MS) < 1.0f);
    CHECK(l.button[ICO_TOUCH_B_CROSS].alpha > 0.0f);
}

static void test_start_variants(void)
{
    const IcoTouchLayout ref = fresh();
    IcoTouchSceneFade f;
    IcoTouchLayout l;

    /* a Start the player hid stays hidden */
    memset(&f, 0, sizeof(f));
    l = ref;
    l.button[ICO_TOUCH_B_START].alpha = 0.0f;
    ico_touch_scene_alpha(&l, &f, SCENES, 1, S);
    CHECK(l.button[ICO_TOUCH_B_START].alpha == 0.0f);
    CHECK(l.button[ICO_TOUCH_B_START].active == 1);
    CHECK(l.button[ICO_TOUCH_B_CROSS].alpha == 0.0f);
    /* a dimmer Start keeps its own alpha */
    memset(&f, 0, sizeof(f));
    l = ref;
    l.button[ICO_TOUCH_B_START].alpha = 0.4f;
    ico_touch_scene_alpha(&l, &f, SCENES, 1, S);
    CHECK(l.button[ICO_TOUCH_B_START].alpha == 0.4f);
    /* chosen by the pad bit: the Cross zone carrying the Start bit stays, and
       the zone that is called Start without the bit goes */
    memset(&f, 0, sizeof(f));
    l = ref;
    l.button[ICO_TOUCH_B_CROSS].pad |= ICO_PAD_START;
    l.button[ICO_TOUCH_B_START].pad = ICO_PAD_SELECT;
    ico_touch_scene_alpha(&l, &f, SCENES, 1, S);
    CHECK(l.button[ICO_TOUCH_B_CROSS].alpha == 1.0f);
    CHECK(l.button[ICO_TOUCH_B_START].alpha == 0.0f);
}

static void test_mapper_and_modes(void)
{
    const IcoTouchLayout ref = fresh();
    IcoTouchSceneFade f;
    IcoTouchLayout l = ref;
    IcoTouchState t;

    memset(&f, 0, sizeof(f));
    ico_touch_scene_alpha(&l, &f, SCENES, 1, S);
    /* the helper changed its copy only; the original keeps every alpha */
    CHECK(ref.button[ICO_TOUCH_B_CROSS].alpha == 1.0f && ref.stickAlpha == 1.0f);
    /* the mode maps like Always (with a gamepad too) and draws at full */
    CHECK(ico_touch_accepts(SCENES, 0) == 1 && ico_touch_accepts(SCENES, 2) == 1);
    memset(&t, 0, sizeof(t));
    CHECK(ico_touch_mode_opacity(&t, SCENES, 1, S) == 1.0f);
    CHECK(ico_touch_effective_mode(SCENES, 0, 1) == SCENES);
    CHECK(strcmp(ico_touch_mode_names[SCENES], "always_scenes") == 0);
}

static void test_fps_place(void)
{
    const IcoTouchLayout ref = fresh();
    IcoTouchSceneFade f;
    IcoTouchLayout faded = ref, manual = ref;
    float ax, ax1, ay, bx, bx1, by;
    int i, ra, rb;

    memset(&f, 0, sizeof(f));
    ico_touch_scene_alpha(&faded, &f, SCENES, 1, S);
    for (i = 0; i < ICO_TOUCH_BUTTONS; i++) {
        if (!has_start(&manual, i)) {
            manual.button[i].alpha = 0.0f;
        }
    }
    manual.stickAlpha = manual.lookAlpha = 0.0f;
    ra = ico_touch_fps_place(&faded, 20.0f, 20.0f, 30.0f, 200.0f, 100.0f, &ax, &ax1, &ay);
    rb = ico_touch_fps_place(&manual, 20.0f, 20.0f, 30.0f, 200.0f, 100.0f, &bx, &bx1, &by);
    CHECK(ra == rb && ax == bx && ax1 == bx1 && ay == by);
    /* Start hidden too: nothing is counted, the corner */
    faded = ref;
    faded.button[ICO_TOUCH_B_START].alpha = 0.0f;
    memset(&f, 0, sizeof(f));
    ico_touch_scene_alpha(&faded, &f, SCENES, 1, S);
    ra = ico_touch_fps_place(&faded, 20.0f, 20.0f, 30.0f, 200.0f, 100.0f, &ax, &ax1, &ay);
    CHECK(ra == 1);
}

int main(void)
{
    test_unchanged();
    test_scene_hides_all_but_start();
    test_fade();
    test_start_variants();
    test_mapper_and_modes();
    test_fps_place();
    if (failures != 0) {
        fprintf(stderr, "touch_scene: %d failure(s)\n", failures);
        return 1;
    }
    printf("touch_scene: ok\n");
    return 0;
}
