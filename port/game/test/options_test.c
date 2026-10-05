/*
 * port/game/test/options_test.c
 *
 * The gameplay options on the CPU (docs/port/OPTIONS.md): the defaults are
 * the original game's, the config and run-time setters, and eBrainGetTarget
 * (ico2/omori/src/ebrain.c, compiled in below so its statics are in reach)
 * with yorda_safe off and on, on a synthetic slot set.
 */
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "options.h"
/* the enemy brain, as the game's own source; stubs below satisfy it */
#include "../../../ico2/omori/src/ebrain.c"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* --- what ebrain.c needs ------------------------------------------------- */
static GObj s_boy, s_girl, s_enemy;

GObj *boyGObj, *girlGObj;

static int s_sees_boy, s_sees_girl;

int ACTCheckViewCl(struct GObj *self, void *target, void *targetPos, int range, float f)
{
    return target == &s_boy ? s_sees_boy : target == &s_girl ? s_sees_girl : 0;
}

void GetRootPosition(void *out, struct GObj *g)
{
    memset(out, 0, 16);
}

/* the rest are reached only from eBrainProcess and the generator lookups */
const StgPre stageData[1];

GenGeo objLayout[1];

void debug_StdPrintfDummy(const char *fmt, ...) {}

int IsBoyStatus_EnemyMustWait(void)
{
    return 0;
}

void sceVu0SubVector(void *dst, void *a, void *b) {}

float sceVu0InnerProduct(void *a, void *b)
{
    return 0.0f;
}

int GetMotherGenerator(int label)
{
    return -1;
}

void *isysGObjSearchFromObjLayoutID(int layoutId)
{
    return 0;
}

void debug_assert(const char *f, int l) {}

void __assert(const char *f, int l, const char *e) {}

/* One slot in the lists of both targets; the girl is the nearer. */
static EBSlot *setup(int status, int message, int chase)
{
    EBSlot *p;

    eBrainInit();
    boyGObj = &s_boy;
    girlGObj = &s_girl;
    p = (EBSlot *)eBrainStatusSet(&s_enemy, 4);
    p->status = (unsigned short)status;
    p->message = message;
    p->chaseFrames = chase;
    p->dist[0] = 1000.0f;
    p->dist[1] = 500.0f;
    boyTargets[0] = girlTargets[0] = p;
    boyTargetNum = girlTargetNum = 1;
    return p;
}

static void test_defaults(void)
{
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_opt_reload();
    CHECK(ico_opt_stick_fix() == 0);
    CHECK(ico_opt_yorda_safe() == 0);
    CHECK(ico_opt_mirror() == 0);
    CHECK(ico_opt_developer_mode() == 0);
    CHECK(ico_opt_debug_option() == 0);
    ico_opt_set_developer_mode(3);
    CHECK(ico_opt_developer_mode() == 1);
    ico_opt_set_yorda_safe(5);
    CHECK(ico_opt_yorda_safe() == 1);
    ico_opt_set_yorda_safe(0);
    CHECK(ico_opt_yorda_safe() == 0);
    ico_opt_set_stick_fix(1);
    ico_opt_set_mirror(1);
    CHECK(ico_opt_stick_fix() == 1 && ico_opt_mirror() == 1);
    ico_opt_reload();
    CHECK(ico_opt_stick_fix() == 0 && ico_opt_mirror() == 0);
    CHECK(ico_opt_developer_mode() == 0);
}

static void test_config(const char *dir)
{
    char path[512];
    FILE *f;

    snprintf(path, sizeof(path), "%s/options_test.toml", dir);
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        failures++;
        return;
    }
    fputs("[gameplay]\nstick_fix = true\nyorda_safe = true\nmirror = true\n"
          "developer_mode = true\n[dev]\ndebug_option = 1\n",
          f);
    fclose(f);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_opt_reload();
    CHECK(ico_opt_stick_fix() == 1);
    CHECK(ico_opt_yorda_safe() == 1);
    CHECK(ico_opt_mirror() == 1);
    /* renderer wave 6 (R6a): developer mode and the debug option file switch */
    CHECK(ico_opt_developer_mode() == 1);
    CHECK(ico_opt_debug_option() == 1);
    ico_opt_set_yorda_safe(0); /* the run-time value wins until a reload */
    CHECK(ico_opt_yorda_safe() == 0);
    remove(path);
    /* debug_option is an int; a non-number or an absent key reads 0 */
    f = fopen(path, "wb");
    if (f != NULL) {
        fputs("[dev]\ndebug_option = 7\n", f);
        fclose(f);
        ico_config_reset(path, "/nonexistent/options_test.ini");
        ico_opt_reload();
        CHECK(ico_opt_debug_option() == 7);
        CHECK(ico_opt_developer_mode() == 0);
        remove(path);
    }
    f = fopen(path, "wb");
    if (f != NULL) {
        fputs("[dev]\ndebug_option = \"yes\"\n", f);
        fclose(f);
        ico_config_reset(path, "/nonexistent/options_test.ini");
        ico_opt_reload();
        CHECK(ico_opt_debug_option() == 0);
        remove(path);
    }
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_opt_reload();
}

static void test_brain(void)
{
    EBSlot *p;
    int safe;

    for (safe = 0; safe < 2; safe++) {
        ico_opt_set_yorda_safe(safe);

        /* both in view, the girl nearer: the original goes for her */
        s_sees_boy = s_sees_girl = 1;
        p = setup(0, 0, 0);
        CHECK(eBrainGetTarget(&s_enemy) == p);
        CHECK(p->status == (safe ? 1 : 2));
        CHECK(p->target == (safe ? &s_boy : &s_girl));
        CHECK(eBrainGirlChaseCount == (safe ? 0 : 1));

        /* only the boy in view: boy, with the option either way */
        s_sees_boy = 1;
        s_sees_girl = 0;
        p = setup(0, 0, 0);
        eBrainGetTarget(&s_enemy);
        CHECK(p->status == 1 && p->target == &s_boy);

        /* only the girl in view */
        s_sees_boy = 0;
        s_sees_girl = 1;
        p = setup(0, 0, 0);
        eBrainGetTarget(&s_enemy);
        CHECK(p->status == (safe ? 0 : 2));

        /* the 181-frame switch from the boy to the girl */
        s_sees_boy = s_sees_girl = 1;
        p = setup(1, 0, 200);
        eBrainGetTarget(&s_enemy);
        CHECK(p->status == (safe ? 1 : 2));
        p = setup(1, 0, 100);
        eBrainGetTarget(&s_enemy);
        CHECK(p->status == 1);

        /* messages: 2 (chase the girl), 6 (go to the girl), 1 (the boy) */
        p = setup(0, 2, 0);
        eBrainGetTarget(&s_enemy);
        CHECK(p->status == (safe ? 1 : 2));
        p = setup(0, 6, 0);
        s_sees_boy = s_sees_girl = 0;
        eBrainGetTarget(&s_enemy);
        CHECK(p->status == (safe ? 0 : 3));
        s_sees_boy = s_sees_girl = 1;
        p = setup(0, 1, 0);
        eBrainGetTarget(&s_enemy);
        CHECK(p->status == 1);

        /* the carry statuses are not the option's: a scripted or story grab
           sends 9 and 7 (status 4), which both settings keep */
        p = setup(0, 0, 0);
        eBrainSendMes(&s_enemy, 9);
        eBrainSendMes(&s_enemy, 7);
        eBrainGetTarget(&s_enemy);
        CHECK(p->status == 4);
    }
    ico_opt_set_yorda_safe(0);
}

/* --- mirror mode per save slot (renderer wave 7, R7c) -------------------- */
static int s_heard = -1, s_heard_count;

static void listener(int on)
{
    s_heard = on;
    s_heard_count++;
}

static void test_mirror_slots(const char *dir)
{
    char path[512];
    FILE *f;

    snprintf(path, sizeof(path), "%s/options_mirror_test.toml", dir);
    remove(path);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_opt_reload();
    /* the listener hears the current value at once, then every change */
    ico_opt_set_mirror_listener(listener);
    CHECK(s_heard == 0 && s_heard_count == 1);
    /* New Game with Mirror On, saved to slot 3 (game.003) with sum 0x1234 */
    ico_opt_set_mirror(1);
    CHECK(s_heard == 1 && s_heard_count == 2);
    CHECK(ico_mirror_slot_saved(3, 0x1234u) == 0);
    /* a New Game with Off saved to slot 5 */
    ico_opt_set_mirror(0);
    CHECK(ico_mirror_slot_saved(5, 0xFFFFFFF0u) == 0);
    /* the next run: the file is read again */
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_opt_reload();
    CHECK(ico_opt_mirror() == 0);
    CHECK(ico_mirror_slot_get(3, 0x1234u) == 1);
    CHECK(ico_mirror_slot_get(5, 0xFFFFFFF0u) == 0);
    CHECK(ico_mirror_slot_get(4, 0x1234u) == -1);  /* no entry */
    CHECK(ico_mirror_slot_get(3, 0x1235u) == -1);  /* another save in slot 3 */
    CHECK(ico_mirror_slot_get(-1, 0x1234u) == -1); /* not a slot */
    /* loading slot 3 sets the run's value On (the listener hears it) */
    CHECK(ico_mirror_slot_loaded(3, 0x1234u) == 1);
    CHECK(ico_opt_mirror() == 1 && s_heard == 1);
    /* loading slot 5: Off */
    CHECK(ico_mirror_slot_loaded(5, 0xFFFFFFF0u) == 0);
    CHECK(ico_opt_mirror() == 0 && s_heard == 0);
    /* a slot without an entry (a PS2 save) and a slot whose save was
       replaced elsewhere (another sum) load Off */
    ico_opt_set_mirror(1);
    CHECK(ico_mirror_slot_loaded(7, 42u) == 0 && ico_opt_mirror() == 0);
    ico_opt_set_mirror(1);
    CHECK(ico_mirror_slot_loaded(3, 0x9999u) == 0 && ico_opt_mirror() == 0);
    /* saving over slot 3 with another flag: the new flag wins */
    ico_opt_set_mirror(0);
    CHECK(ico_mirror_slot_saved(3, 0x5678u) == 0);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_opt_reload();
    CHECK(ico_mirror_slot_get(3, 0x5678u) == 0);
    CHECK(ico_mirror_slot_get(3, 0x1234u) == -1);
    /* the file holds the [mirror] table */
    f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f != NULL) {
        char buf[2048];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = '\0';
        fclose(f);
        CHECK(strstr(buf, "[mirror]") != NULL);
        CHECK(strstr(buf, "slot_3 = false") != NULL);
        CHECK(strstr(buf, "slot_3_sum = 22136") != NULL);
        CHECK(strstr(buf, "slot_5_sum = 4294967280") != NULL);
    }
    /* the title: back to [gameplay] mirror (absent: Off) */
    ico_opt_set_mirror(1);
    ico_opt_mirror_reset();
    CHECK(ico_opt_mirror() == 0 && s_heard == 0);
    ico_opt_set_mirror_listener(NULL);
    remove(path);
    ico_opt_reload();
}

int main(int argc, char **argv)
{
    test_defaults();
    test_config(argc > 1 ? argv[1] : ".");
    test_mirror_slots(argc > 1 ? argv[1] : ".");
    test_brain();
    if (failures != 0) {
        fprintf(stderr, "options_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("options_test: ok\n");
    return 0;
}
