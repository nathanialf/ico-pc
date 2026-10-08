/*
 * port/game/test/options_test.c
 *
 * The gameplay options on the CPU: the defaults are
 * the original game's, the config and run-time setters, and eBrainGetTarget
 * (ico2/omori/src/ebrain.c, compiled in below so its statics are in reach)
 * with yorda_safe off and on, on a synthetic slot set.
 */
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "options.h"
#include "video_options.h"
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

/* issue 11: the five effect switches: on by default, read from [video],
   written back every time, and no part of the preset */
static char *read_text(const char *path)
{
    static char buf[16384];
    FILE *f = fopen(path, "rb");
    size_t n;

    if (f == NULL) {
        return NULL;
    }
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    return buf;
}

static int effects_are(const IcoVideoOptions *o, int g, int d, int s, int m, int f)
{
    return o->effectGlow == g && o->effectDepthOfField == d && o->effectSoftening == s &&
           o->effectMotionBlur == m && o->effectFog == f && ico_video_effect_glow() == g &&
           ico_video_effect_depth_of_field() == d && ico_video_effect_softening() == s &&
           ico_video_effect_motion_blur() == m && ico_video_effect_fog() == f;
}

static void test_video_effects(const char *dir)
{
    static const char *const keys[5] = {
        "effect_glow = ", "effect_depth_of_field = ", "effect_softening = ",
        "effect_motion_blur = ", "effect_fog = "};
    char path[512];
    IcoVideoOptions o, d;
    const char *text;
    FILE *f;

    /* the defaults, and no file: every effect on */
    ico_video_defaults(&d);
    CHECK(d.effectGlow == 1 && d.effectDepthOfField == 1 && d.effectSoftening == 1 &&
          d.effectMotionBlur == 1 && d.effectFog == 1);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(effects_are(&o, 1, 1, 1, 1, 1));
    /* a file with fog off (and a stray value elsewhere) */
    snprintf(path, sizeof(path), "%s/options_video_test.toml", dir);
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        failures++;
        return;
    }
    fputs("[video]\neffect_fog = false\n", f);
    fclose(f);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(effects_are(&o, 1, 1, 1, 1, 0));
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    /* run-time values are sanitized to 0 or 1 */
    o.effectGlow = 0;
    o.effectDepthOfField = 7;
    o.effectSoftening = 0;
    o.effectMotionBlur = -1;
    o.effectFog = 1;
    ico_video_set(&o);
    ico_video_get(&o);
    CHECK(effects_are(&o, 0, 1, 0, 1, 1));
    /* saved: all five keys written, and read back the same */
    CHECK(ico_video_save() == 0);
    text = read_text(path);
    CHECK(text != NULL);
    for (int i = 0; text != NULL && i < 5; i++) {
        CHECK(strstr(text, keys[i]) != NULL);
    }
    CHECK(text != NULL && strstr(text, "effect_glow = false") != NULL &&
          strstr(text, "effect_softening = false") != NULL &&
          strstr(text, "effect_fog = true") != NULL);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(effects_are(&o, 0, 1, 0, 1, 1));
    /* none of the five moves the preset off Original, and Enhanced's
       shortcut leaves them alone */
    for (int i = 0; i < 5; i++) {
        ico_video_defaults(&o);
        int *fx[5] = {&o.effectGlow, &o.effectDepthOfField, &o.effectSoftening, &o.effectMotionBlur,
                      &o.effectFog};
        *fx[i] = 0;
        CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
        ico_video_set_preset(&o, ICO_VIDEO_ENHANCED);
        CHECK(*fx[i] == 0 && ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
    }
    remove(path);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
}

/* v0.4.1 (R1): [video] effects_depth, on by default, read, sanitized,
   saved when off or already in the file, outside the preset, through
   ico_video_effects_depth */
static void test_video_effects_depth(const char *dir)
{
    char path[512];
    IcoVideoOptions o;
    const char *text;
    FILE *f;

    ico_video_defaults(&o);
    CHECK(o.effectsDepth == 1);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
    CHECK(ico_video_effects_depth() == 1);
    snprintf(path, sizeof(path), "%s/options_depth_test.toml", dir);
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        failures++;
        return;
    }
    fputs("[video]\neffects_depth = false\n", f);
    fclose(f);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.effectsDepth == 0 && ico_video_effects_depth() == 0);
    CHECK(effects_are(&o, 1, 1, 1, 1, 1));
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    /* sanitized to 0 or 1 */
    o.effectsDepth = 5;
    ico_video_set(&o);
    ico_video_get(&o);
    CHECK(o.effectsDepth == 1 && ico_video_effects_depth() == 1);
    o.effectsDepth = 0;
    ico_video_set(&o);
    CHECK(ico_video_save() == 0);
    text = read_text(path);
    CHECK(text != NULL && strstr(text, "effects_depth = false") != NULL);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    CHECK(ico_video_effects_depth() == 0);
    /* the preset's shortcut leaves it alone */
    ico_video_defaults(&o);
    o.effectsDepth = 0;
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    ico_video_set_preset(&o, ICO_VIDEO_ENHANCED);
    CHECK(o.effectsDepth == 0);
    remove(path);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
}

/* v0.4.1: [video] model_pack (default on) and dump_models (default off):
   the defaults, the file read, the round trip, and the preset untouched */
static void test_video_models(const char *dir)
{
    char path[512];
    IcoVideoOptions o, d;
    const char *text;
    FILE *f;

    ico_video_defaults(&d);
    CHECK(d.modelPack == 1 && d.dumpModels == 0);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.modelPack == 1 && o.dumpModels == 0);
    CHECK(ico_video_model_pack() == 1 && ico_video_dump_models() == 0);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    snprintf(path, sizeof(path), "%s/options_models_test.toml", dir);
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        failures++;
        return;
    }
    fputs("[video]\nmodel_pack = false\ndump_models = true\n", f);
    fclose(f);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.modelPack == 0 && o.dumpModels == 1);
    CHECK(ico_video_model_pack() == 0 && ico_video_dump_models() == 1);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    /* run-time values are sanitized to 0 or 1 */
    o.modelPack = 5;
    o.dumpModels = -1;
    ico_video_set(&o);
    ico_video_get(&o);
    CHECK(o.modelPack == 1 && o.dumpModels == 1);
    o.modelPack = 0;
    o.dumpModels = 0;
    ico_video_set(&o);
    CHECK(ico_video_save() == 0);
    text = read_text(path);
    CHECK(text != NULL && strstr(text, "model_pack = false") != NULL &&
          strstr(text, "dump_models = false") != NULL);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.modelPack == 0 && o.dumpModels == 0);
    /* neither moves the preset off Original, and Enhanced's shortcut
       leaves them alone */
    ico_video_defaults(&o);
    o.modelPack = 0;
    o.dumpModels = 1;
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    ico_video_set_preset(&o, ICO_VIDEO_ENHANCED);
    CHECK(o.modelPack == 0 && o.dumpModels == 1 && ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
    remove(path);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
}

/* v0.4.2 (N2): resolution "auto" (parsed, named, read, saved and read
   back, kept by sanitize) on any build; the Android rules (checked here on
   any computer through ico_video_set_android / ico_video_defaults_for): the
   frame rate default 60, Enhanced's resolution "auto" both ways (the
   shortcut writes it, the classifier calls it Enhanced, "window" is then
   Custom), Original 1x as everywhere, and a file with no framerate or
   resolution key read as 60 and "auto" */
static void test_video_auto(const char *dir)
{
    char path[512], buf[32];
    IcoVideoOptions o, d;
    const char *text;
    FILE *f;
    int fr = 0;

    ico_video_defaults(&o);
    CHECK(ico_video_parse_resolution("auto", &o) == 0 && o.resScale == ICO_RES_AUTO &&
          o.resW == 0 && o.resH == 0);
    CHECK(strcmp(ico_video_resolution_name(&o, buf, sizeof(buf)), "auto") == 0);
    CHECK(ico_video_parse_resolution("AUTO", &o) == 0 && o.resScale == ICO_RES_AUTO);
    CHECK(ico_video_parse_resolution("auto2", &o) == -1 && o.resScale == ICO_RES_AUTO);
    /* the PC rules: "auto" is not Enhanced's; Enhanced keeps "window"; the
       default frame rate stays "uncapped" */
    ico_video_set_android(0);
    ico_video_defaults(&d);
    CHECK(d.framerate == ICO_FRAMERATE_UNCAPPED && d.resScale == 1);
    ico_video_set_preset(&d, ICO_VIDEO_ENHANCED);
    CHECK(d.resScale == 0 && ico_video_preset(&d) == ICO_VIDEO_ENHANCED);
    d.resScale = ICO_RES_AUTO;
    CHECK(ico_video_preset(&d) == ICO_VIDEO_CUSTOM);
    /* the round trip: Enhanced with resolution = auto, read, kept, saved */
    snprintf(path, sizeof(path), "%s/options_auto_test.toml", dir);
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        failures++;
        return;
    }
    fputs("[video]\npreset = \"enhanced\"\nresolution = \"auto\"\n", f);
    fclose(f);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.resScale == ICO_RES_AUTO && o.resW == 0 && o.resH == 0);
    ico_video_set(&o); /* sanitize keeps it */
    ico_video_get(&o);
    CHECK(o.resScale == ICO_RES_AUTO);
    CHECK(ico_video_save() == 0);
    text = read_text(path);
    CHECK(text != NULL && strstr(text, "resolution = \"auto\"") != NULL);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.resScale == ICO_RES_AUTO);
    /* the getter the Options row reads */
    ico_video_set_auto_scale(2);
    CHECK(ico_video_auto_scale() == 2);
    ico_video_set_auto_scale(-3);
    CHECK(ico_video_auto_scale() == 0);
    /* the Android rules */
    ico_video_defaults_for(&d, 1);
    CHECK(d.framerate == 60 && ico_video_default_framerate(1) == 60);
    CHECK(ico_video_default_framerate(0) == ICO_FRAMERATE_UNCAPPED);
    CHECK(d.resScale == 1 && d.aspect == ICO_ASPECT_4_3 && d.filter == ICO_FILTER_ORIGINAL &&
          !d.fullHeight);
    CHECK(strcmp(ico_video_framerate_name(d.framerate, buf, sizeof(buf)), "60") == 0 &&
          ico_video_parse_framerate(buf, &fr) == 0 && fr == 60);
    ico_video_set_android(1);
    CHECK(ico_video_android() == 1);
    ico_video_defaults(&o);
    CHECK(o.framerate == 60 && ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    ico_video_set_preset(&o, ICO_VIDEO_ENHANCED);
    CHECK(o.resScale == ICO_RES_AUTO && o.aspect == ICO_ASPECT_AUTO &&
          o.filter == ICO_FILTER_ANISOTROPIC && o.fullHeight);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
    o.resScale = 0; /* "window" is Custom under these rules */
    CHECK(ico_video_preset(&o) == ICO_VIDEO_CUSTOM);
    ico_video_set_preset(&o, ICO_VIDEO_ORIGINAL);
    CHECK(o.resScale == 1 && ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    /* a framerate out of range falls back to the Android default */
    o.framerate = 5;
    ico_video_set(&o);
    ico_video_get(&o);
    CHECK(o.framerate == 60);
    /* a file with Enhanced and no resolution or framerate key */
    f = fopen(path, "wb");
    if (f != NULL) {
        fputs("[video]\npreset = \"enhanced\"\naspect = \"auto\"\ntexture_filter = "
              "\"anisotropic\"\nfull_height = true\n",
              f);
        fclose(f);
    }
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.resScale == ICO_RES_AUTO && o.framerate == 60 &&
          ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
    /* and no file at all: Original at 1x, 60 a second */
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.resScale == 1 && o.framerate == 60 && ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    ico_video_set_android(0);
    remove(path);
    ico_video_reload();
}

int main(int argc, char **argv)
{
    test_defaults();
    test_config(argc > 1 ? argv[1] : ".");
    test_mirror_slots(argc > 1 ? argv[1] : ".");
    test_brain();
    test_video_effects(argc > 1 ? argv[1] : ".");
    test_video_effects_depth(argc > 1 ? argv[1] : ".");
    test_video_models(argc > 1 ? argv[1] : ".");
    test_video_auto(argc > 1 ? argv[1] : ".");
    if (failures != 0) {
        fprintf(stderr, "options_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("options_test: ok\n");
    return 0;
}
