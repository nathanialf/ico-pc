/*
 * port/game/options.c
 *
 * The port's gameplay options (options.h).
 */
#include "options.h"

#include <stdio.h>
#include <string.h>

#include "appearance.h"
#include "config.h"
#include "region.h" /* port/core: the per-disc keys (ico_region_key_prefixed) */
#include "variants.h"

/* options.h: the model viewer is up */
int ico_mv_active;

/* -1: not read yet; 0 or 1 */
static int s_stick_fix = -1;
static int s_yorda_safe = -1;
static int s_door_fix = -1;
static int s_natural_chains = -1;
static int s_skip_opening = -1;
static int s_mirror = -1;
static int s_developer_mode = -1;
static int s_circle_back = -1;
static int s_ui_language = -2; /* -2: not read yet */

static int get_or(int *v, const char *path, int def)
{
    if (*v < 0) {
        *v = ico_config_get_bool(path, def) != 0;
    }
    return *v;
}

static int get(int *v, const char *path)
{
    return get_or(v, path, 0);
}

int ico_opt_stick_fix_default_for(int android)
{
    /* Off on every platform, Android included (where a touch stick is
       round); a file where the player chose a value keeps it (no file is
       rewritten) */
    (void)android;
    return 0;
}

int ico_opt_stick_fix_default(void)
{
#ifdef __ANDROID__
    return ico_opt_stick_fix_default_for(1);
#else
    return ico_opt_stick_fix_default_for(0);
#endif
}

int ico_opt_stick_fix(void)
{
    return get_or(&s_stick_fix, "gameplay.stick_fix", ico_opt_stick_fix_default());
}

void ico_opt_set_stick_fix(int on)
{
    s_stick_fix = on != 0;
}

int ico_opt_yorda_safe(void)
{
    return get(&s_yorda_safe, "gameplay.yorda_safe");
}

void ico_opt_set_yorda_safe(int on)
{
    s_yorda_safe = on != 0;
}

/* [gameplay] pit_time (options.h): -1 not read yet, else ICO_PIT_* */
static int s_pit_time = -1;
static const char *const kPitNames[ICO_PIT_COUNT] = {"half", "original", "longer", "double",
                                                     "triple"};
static const float kPitFactors[ICO_PIT_COUNT] = {0.5f, 1.0f, 1.5f, 2.0f, 3.0f};

static void log_pit_time(int v)
{
    fprintf(stderr, "gameplay: pit time %s (x%.1f)\n", kPitNames[v], (double)kPitFactors[v]);
}

int ico_opt_pit_time(void)
{
    if (s_pit_time < 0) {
        const char *v = ico_config_get_string("gameplay.pit_time", "original");
        int i;

        s_pit_time = ICO_PIT_ORIGINAL;
        for (i = 0; v != NULL && i < ICO_PIT_COUNT; i++) {
            if (strcmp(v, kPitNames[i]) == 0) {
                s_pit_time = i;
                break;
            }
        }
        if (v != NULL && i == ICO_PIT_COUNT) {
            fprintf(stderr,
                    "options: [gameplay] pit_time = \"%s\" is not one of half, original, longer, "
                    "double, triple\n",
                    v);
        }
        log_pit_time(s_pit_time);
    }
    return s_pit_time;
}

void ico_opt_set_pit_time(int v)
{
    v = v >= 0 && v < ICO_PIT_COUNT ? v : ICO_PIT_ORIGINAL;
    if (v != s_pit_time) {
        log_pit_time(v);
    }
    s_pit_time = v;
}

float ico_opt_pit_time_factor(void)
{
    return kPitFactors[ico_opt_pit_time()];
}

float ico_opt_pit_dive_ratio(int camera)
{
    return (camera != 0 ? 2.0f : 1.0f) / ico_opt_pit_time_factor();
}

/* [gameplay] shadow_speed (options.h): -1 not read yet, else ICO_SPEED_* */
static int s_shadow_speed = -1;
static const char *const kSpeedNames[ICO_SPEED_COUNT] = {"slower", "original", "faster", "fastest"};
static const float kSpeedFactors[ICO_SPEED_COUNT] = {0.75f, 1.0f, 1.25f, 1.5f};

static void log_shadow_speed(int v)
{
    fprintf(stderr, "gameplay: shadow speed %s (x%.2f)\n", kSpeedNames[v],
            (double)kSpeedFactors[v]);
}

int ico_opt_shadow_speed(void)
{
    if (s_shadow_speed < 0) {
        const char *v = ico_config_get_string("gameplay.shadow_speed", "original");
        int i;

        s_shadow_speed = ICO_SPEED_ORIGINAL;
        for (i = 0; v != NULL && i < ICO_SPEED_COUNT; i++) {
            if (strcmp(v, kSpeedNames[i]) == 0) {
                s_shadow_speed = i;
                break;
            }
        }
        if (v != NULL && i == ICO_SPEED_COUNT) {
            fprintf(stderr,
                    "options: [gameplay] shadow_speed = \"%s\" is not one of slower, original, "
                    "faster, fastest\n",
                    v);
        }
        log_shadow_speed(s_shadow_speed);
    }
    return s_shadow_speed;
}

void ico_opt_set_shadow_speed(int v)
{
    v = v >= 0 && v < ICO_SPEED_COUNT ? v : ICO_SPEED_ORIGINAL;
    if (v != s_shadow_speed) {
        log_shadow_speed(v);
    }
    s_shadow_speed = v;
}

float ico_opt_shadow_speed_factor(void)
{
    return kSpeedFactors[ico_opt_shadow_speed()];
}

int ico_opt_door_fix(void)
{
    return get(&s_door_fix, "gameplay.door_fix");
}

void ico_opt_set_door_fix(int on)
{
    s_door_fix = on != 0;
}

int ico_opt_natural_chains(void)
{
    return get(&s_natural_chains, "gameplay.natural_chains");
}

void ico_opt_set_natural_chains(int on)
{
    s_natural_chains = on != 0;
}

int ico_opt_skip_opening(void)
{
    return get(&s_skip_opening, "gameplay.skip_opening");
}

void ico_opt_set_skip_opening(int on)
{
    s_skip_opening = on != 0;
}

int ico_opt_mirror(void)
{
    return get(&s_mirror, "gameplay.mirror");
}

static void (*s_mirror_listener)(int on);

void ico_opt_set_mirror(int on)
{
    s_mirror = on != 0;
    if (s_mirror_listener != NULL) {
        s_mirror_listener(s_mirror);
    }
}

void ico_opt_mirror_reset(void)
{
    ico_opt_set_mirror(ico_config_get_bool("gameplay.mirror", 0) != 0);
}

void ico_opt_set_mirror_listener(void (*fn)(int on))
{
    s_mirror_listener = fn;
    if (fn != NULL) {
        fn(ico_opt_mirror());
    }
}

/* [mirror] slot_N, slot_N_sum; jpn_slot_N, jpn_slot_N_sum; kor_slot_N,
   kor_slot_N_sum (options.h) */
#define MIRROR_NO_SUM (-1LL - 0xFFFFFFFFLL) /* outside the uint32 range */

static int section_slot_keys(const char *section, int slot, char *flag, char *sum, size_t size)
{
    char base[24];

    if (slot < 0 || slot > 99) {
        return -1;
    }
    /* per disc, since each disc's saves are another card directory:
       slot_N for the European disc (the keys as always), jpn_slot_N and
       kor_slot_N for the Japanese and Korean ones (the region's key_prefix) */
    snprintf(base, sizeof(base), "slot_%d", slot);
    if (ico_region_key_prefixed(section, base, flag, (int)size) != 0) {
        return -1;
    }
    snprintf(base, sizeof(base), "slot_%d_sum", slot);
    return ico_region_key_prefixed(section, base, sum, (int)size);
}

static int slot_keys(int slot, char *flag, char *sum, size_t size)
{
    return section_slot_keys("mirror", slot, flag, sum, size);
}

int ico_mirror_slot_get(int slot, unsigned int sum)
{
    char flag[32], key[32];

    if (slot_keys(slot, flag, key, sizeof(flag)) != 0) {
        return -1;
    }
    if (ico_config_get_int(key, MIRROR_NO_SUM) != (long long)sum) {
        return -1;
    }
    return ico_config_get_bool(flag, 0) != 0;
}

int ico_mirror_slot_saved(int slot, unsigned int sum)
{
    char flag[32], key[32];

    if (slot_keys(slot, flag, key, sizeof(flag)) != 0) {
        return -1;
    }
    if (ico_config_set_bool(flag, ico_opt_mirror()) != 0 ||
        ico_config_set_int(key, (long long)sum) != 0) {
        return -1;
    }
    return ico_config_save() == 0 ? 0 : -1;
}

int ico_mirror_slot_loaded(int slot, unsigned int sum)
{
    int v = ico_mirror_slot_get(slot, sum);

    ico_opt_set_mirror(v > 0);
    return v > 0;
}

/* [players] slot_N, slot_N_sum (options.h): only for the discs whose card
   has no girlControlMode word (the 2001 discs) */
int ico_players_slot_get(int slot, unsigned int sum)
{
    char count[32], key[32];

    if (section_slot_keys("players", slot, count, key, sizeof(count)) != 0) {
        return -1;
    }
    if (ico_config_get_int(key, MIRROR_NO_SUM) != (long long)sum) {
        return -1;
    }
    return ico_config_get_int(count, 1) == 2 ? 1 : 0;
}

int ico_players_slot_saved(int slot, unsigned int sum, int mode)
{
    char count[32], key[32];

    if (section_slot_keys("players", slot, count, key, sizeof(count)) != 0) {
        return -1;
    }
    if (ico_config_set_int(count, mode != 0 ? 2 : 1) != 0 ||
        ico_config_set_int(key, (long long)sum) != 0 || ico_config_save() != 0) {
        fprintf(stderr, "options: cannot write [players] %s\n", count);
        return -1;
    }
    fprintf(stderr, "options: Players %d kept for save slot %d on %s\n", mode != 0 ? 2 : 1, slot,
            ico_region_cur()->disc_id);
    return 0;
}

int ico_players_slot_loaded(int slot, unsigned int sum)
{
    const int v = ico_players_slot_get(slot, sum);

    fprintf(stderr, "options: Players %d from save slot %d on %s%s\n", v > 0 ? 2 : 1, slot,
            ico_region_cur()->disc_id, v < 0 ? " (no entry for this save: 1)" : "");
    return v > 0;
}

void ico_players_note(int mode)
{
    static int s_last = -1;

    if ((mode != 0) == s_last) {
        return;
    }
    s_last = mode != 0;
    fprintf(stderr, "options: Players %d on %s (Yorda %s)\n", s_last ? 2 : 1,
            ico_region_cur()->disc_id,
            s_last ? "on the second controller, port 1" : "led by the game");
}

int ico_opt_developer_mode(void)
{
    return get(&s_developer_mode, "gameplay.developer_mode");
}

void ico_opt_set_developer_mode(int on)
{
    s_developer_mode = on != 0;
}

int ico_opt_circle_back(void)
{
    if (s_circle_back < 0) {
        s_circle_back = ico_config_get_bool("game.circle_back", 1) != 0;
    }
    return s_circle_back;
}

void ico_opt_set_circle_back(int on)
{
    s_circle_back = on != 0;
}

/* [ui] language's names, UiLang order (port/ui/strings.h) */
static const char *const kUiLanguages[] = {"en", "fr", "de", "it", "es", "ja", "ko", "zh"};
#define UI_LANGUAGES ((int)(sizeof(kUiLanguages) / sizeof(kUiLanguages[0])))

int ico_opt_ui_language(void)
{
    if (s_ui_language < -1) {
        const char *v = ico_config_get_string("ui.language", "auto");
        int i;

        s_ui_language = -1;
        for (i = 0; v != NULL && i < UI_LANGUAGES; i++) {
            if (strcmp(v, kUiLanguages[i]) == 0) {
                s_ui_language = i;
            }
        }
        if (s_ui_language < 0 && v != NULL && strcmp(v, "auto") != 0) {
            fprintf(
                stderr,
                "options: [ui] language = \"%s\" is not one of auto, en, fr, de, it, es, ja, ko, zh\n",
                v);
        }
    }
    return s_ui_language;
}

void ico_opt_set_ui_language(int lang)
{
    s_ui_language = lang >= 0 && lang < UI_LANGUAGES ? lang : -1;
}

const char *ico_opt_ui_language_name(int lang)
{
    return lang >= 0 && lang < UI_LANGUAGES ? kUiLanguages[lang] : "auto";
}

int ico_opt_debug_option(void)
{
    long long v = ico_config_get_int("dev.debug_option", 0);

    return v < -0x7FFFFFFF || v > 0x7FFFFFFF ? 0 : (int)v;
}

/* [audio] output (options.h): -2 not read yet, else ICO_OUTPUT_* */
static int s_output = -2;
/* the game's own mode (the card's, the Options row's), kept as the game
   set it; s_game_known 0 until it is */
static int s_game_output;
static int s_game_known;

int ico_opt_output_mode(void)
{
    if (s_output == -2) {
        const char *v = ico_config_get_string("audio.output", "auto");
        s_output = strcmp(v, "stereo") == 0 ? ICO_OUTPUT_STEREO
                   : strcmp(v, "mono") == 0 ? ICO_OUTPUT_MONO
                                            : ICO_OUTPUT_AUTO;
    }
    return s_output;
}

void ico_opt_set_output_mode(int mode)
{
    s_output = mode < 0 ? ICO_OUTPUT_AUTO : mode == 0 ? ICO_OUTPUT_STEREO : ICO_OUTPUT_MONO;
}

const char *ico_opt_output_name(int mode)
{
    return mode < 0 ? "auto" : mode == 0 ? "stereo" : "mono";
}

int ico_opt_output_resolve(int current)
{
    int m = ico_opt_output_mode();

    if (!s_game_known) {
        s_game_output = current;
        s_game_known = 1;
    }
    return m != ICO_OUTPUT_AUTO ? m : s_game_output;
}

int ico_opt_output_card(int card_mode)
{
    s_game_output = card_mode;
    s_game_known = 1;
    return ico_opt_output_resolve(card_mode);
}

int ico_opt_output_game(int live)
{
    static int s_logged = -3;
    int g = s_game_known ? s_game_output : live;

    if (g != live && s_logged != g * 4 + live) {
        s_logged = g * 4 + live;
        fprintf(
            stderr,
            "options: the card saves the game's own sound output (%s), not the override in force (%s)\n",
            g == 1 ? "mono" : "stereo", live == 1 ? "mono" : "stereo");
    }
    return g;
}

void ico_opt_output_record(int mode)
{
    s_game_output = mode;
    s_game_known = 1;
}

void ico_opt_output_toggled(int mode)
{
    ico_opt_output_record(mode);
    if (ico_opt_output_mode() != ICO_OUTPUT_AUTO) {
        ico_opt_set_output_mode(mode);
        if (ico_config_set_string("audio.output", ico_opt_output_name(s_output)) != 0 ||
            ico_config_save() != 0) {
            fprintf(stderr, "options: cannot write [audio] output\n");
        }
    }
}

/* [gameplay] hold_type (options.h): -1 not read yet, else ICO_HOLD_DISC,
   ICO_HOLD_HOLD or ICO_HOLD_TOGGLE */
static int s_hold_choice = -1;

int ico_opt_hold_choice(void)
{
    if (s_hold_choice < 0) {
        const char *v = ico_config_get_string("gameplay.hold_type", "disc");

        if (v != NULL && strcmp(v, "toggle") == 0) {
            s_hold_choice = ICO_HOLD_TOGGLE;
        } else if (v != NULL && strcmp(v, "hold") == 0) {
            /* every version before 0.9.1 wrote "hold" into a new file: the
               player's choice only beside hold_type_chosen (options.h) */
            s_hold_choice = ico_config_get_bool("gameplay.hold_type_chosen", 0) != 0
                                ? ICO_HOLD_HOLD
                                : ICO_HOLD_DISC;
        } else {
            s_hold_choice = ICO_HOLD_DISC;
            if (v != NULL && strcmp(v, "disc") != 0) {
                fprintf(stderr,
                        "options: [gameplay] hold_type = \"%s\" is not one of disc, hold, "
                        "toggle\n",
                        v);
            }
        }
    }
    return s_hold_choice;
}

int ico_opt_hold_type(void)
{
    /* the choice and disc last logged */
    static int loggedChoice = -1;
    static const IcoRegionInfo *loggedDisc;
    const IcoRegionInfo *r = ico_region_cur();
    const int choice = ico_opt_hold_choice();
    const int type = choice == ICO_HOLD_DISC
                         ? (r->hold_type_own != 0 ? ICO_HOLD_TOGGLE : ICO_HOLD_HOLD)
                         : choice;

    if (choice != loggedChoice || r != loggedDisc) {
        loggedChoice = choice;
        loggedDisc = r;
        fprintf(stderr, "options: hold type %s (%s) on %s\n",
                choice == ICO_HOLD_DISC   ? "disc's own"
                : choice == ICO_HOLD_HOLD ? "hold"
                                          : "toggle",
                type == ICO_HOLD_TOGGLE ? "B" : "A", r->disc_id);
    }
    return type;
}

void ico_opt_set_hold_choice(int choice)
{
    s_hold_choice = choice == ICO_HOLD_HOLD || choice == ICO_HOLD_TOGGLE ? choice : ICO_HOLD_DISC;
}

void ico_opt_set_hold_type(int type)
{
    s_hold_choice = type != 0 ? ICO_HOLD_TOGGLE : ICO_HOLD_HOLD;
}

const char *ico_opt_hold_type_name(int choice)
{
    return choice == ICO_HOLD_HOLD ? "hold" : choice == ICO_HOLD_TOGGLE ? "toggle" : "disc";
}

int ico_opt_hold_choice_store(void)
{
    if (ico_config_set_string("gameplay.hold_type",
                              ico_opt_hold_type_name(ico_opt_hold_choice())) != 0 ||
        ico_config_set_bool("gameplay.hold_type_chosen", 1) != 0) {
        return -1;
    }
    return 0;
}

void ico_opt_hold_type_toggled(int type)
{
    ico_opt_set_hold_type(type);
    if (ico_opt_hold_choice_store() != 0 || ico_config_save() != 0) {
        fprintf(stderr, "options: cannot write [gameplay] hold_type\n");
    }
}

void ico_opt_reload(void)
{
    s_stick_fix = s_yorda_safe = s_door_fix = s_mirror = s_developer_mode = s_circle_back = -1;
    s_pit_time = s_shadow_speed = -1;
    s_natural_chains = s_skip_opening = -1;
    s_output = -2;
    s_ui_language = -2;
    s_hold_choice = -1;
    /* the characters' colours ([characters], appearance.h) */
    ico_appearance_reload();
    /* the game variants ([variants], variants.h) */
    ico_variants_reload();
}
