/*
 * port/game/options.c
 *
 * The port's gameplay options (options.h).  Five of them (yorda_safe,
 * pit_time, shadow_speed, yorda_trips, shadows_together) are rows of the
 * journey's difficulty record (difficulty.h): their accessors keep their
 * names and read the active record; their setters edit it.
 */
#include "options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "appearance.h"
#include "config.h"
#include "difficulty.h"
#include "ico_rows.h"
#include "pit_time.h"
#include "region.h" /* port/core: the per-disc keys (ico_region_key_prefixed) */
#include "variants.h"

/* options.h: the model viewer is up */
int ico_mv_active;

/* -1: not read yet; 0 or 1 */
static int s_stick_fix = -1;
static int s_door_fix = -1;
static unsigned long s_shadows_together_went; /* the turns the option skipped, this run */
static int s_natural_chains = -1;
static int s_skip_opening = -1;
static int s_second_card = -1;
static int s_mirror = -1;
static int s_developer_mode = -1;
static int s_circle_back = -1;
static int s_ui_language = -2;   /* -2: not read yet */
static int s_text_language = -2; /* -2: not read yet */

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
    return ico_difficulty_row(ICO_DIFF_YORDA_SAFE);
}

void ico_opt_set_yorda_safe(int on)
{
    ico_difficulty_set_row(ICO_DIFF_YORDA_SAFE, on != 0);
}

/* the pit time last logged (-1: none yet): the line comes at the first read
   and on every change of the journey's row */
static int s_pit_logged = -1;

/* the ticks a second the pull-in's length is counted in (pit_time.h) */
#define PIT_TICK_HZ ICO_PIT_TICK_HZ

int ico_opt_pit_chain_ticks_for(const char *slug)
{
    return ico_pit_chain_ticks_for(slug);
}

int ico_opt_pit_chain_ticks(void)
{
    return ico_opt_pit_chain_ticks_for(ico_region_cur()->slug);
}

static int pit_base_for(int ticks)
{
    return (ticks + PIT_TICK_HZ / 2) / PIT_TICK_HZ;
}

int ico_opt_pit_base_seconds(void)
{
    return pit_base_for(ico_opt_pit_chain_ticks());
}

/* seconds held to the range; the base itself is Original */
static int pit_hold(int seconds, int ticks, int *note)
{
    if (seconds < ICO_PIT_MIN_SECONDS || seconds > ICO_PIT_MAX_SECONDS) {
        seconds = seconds < ICO_PIT_MIN_SECONDS ? ICO_PIT_MIN_SECONDS : ICO_PIT_MAX_SECONDS;
        if (note != NULL) {
            *note = 1;
        }
    }
    return seconds == pit_base_for(ticks) ? ICO_PIT_ORIGINAL : seconds;
}

int ico_opt_pit_parse(const char *text, int ticks, int *note)
{
    /* the earlier versions' words: that many times the disc's own time */
    static const struct {
        const char *word;
        int num, den;
    } kWords[] = {{"half", 1, 2}, {"longer", 3, 2}, {"double", 2, 1}, {"triple", 3, 1}};

    char *end = NULL;
    long v;
    size_t i;

    if (note != NULL) {
        *note = 0;
    }
    if (text == NULL || strcmp(text, "original") == 0) {
        return ICO_PIT_ORIGINAL;
    }
    for (i = 0; i < sizeof(kWords) / sizeof(kWords[0]); i++) {
        if (strcmp(text, kWords[i].word) == 0) {
            const int t = ticks * kWords[i].num / kWords[i].den;
            int held = 0;
            const int r = pit_hold((t + PIT_TICK_HZ / 2) / PIT_TICK_HZ, ticks, &held);

            if (note != NULL && held) {
                *note = 2; /* a known word, held to the range: not a bad value */
            }
            return r;
        }
    }
    v = strtol(text, &end, 10);
    if (end == text || *end != '\0') {
        if (note != NULL) {
            *note = 1;
        }
        return ICO_PIT_ORIGINAL;
    }
    return pit_hold(v < 0 ? 0 : v > 1000 ? 1000 : (int)v, ticks, note);
}

static void log_pit_time(int v)
{
    const int ticks = ico_opt_pit_chain_ticks();
    char value[16];

    if (v == ICO_PIT_ORIGINAL) {
        snprintf(value, sizeof(value), "original");
    } else {
        snprintf(value, sizeof(value), "%d s", v);
    }
    fprintf(stderr,
            "gameplay: pit time %s (this disc's pull-in %d ticks = %.1f s, shown as %d s; "
            "x%.3f)\n",
            value, ticks, (double)ticks / PIT_TICK_HZ, pit_base_for(ticks),
            (double)ico_opt_pit_time_factor());
}

int ico_opt_pit_time(void)
{
    int v = ico_difficulty_row(ICO_DIFF_PIT_TIME);

    /* the disc's own seconds read as Original (a record made on a disc whose
       base is another) */
    if (v != ICO_PIT_ORIGINAL) {
        v = pit_hold(v, ico_opt_pit_chain_ticks(), NULL);
    }
    if (v != s_pit_logged) {
        s_pit_logged = v;
        log_pit_time(v);
    }
    return v;
}

void ico_opt_set_pit_time(int seconds)
{
    const int v = seconds == ICO_PIT_ORIGINAL ? ICO_PIT_ORIGINAL
                                              : pit_hold(seconds, ico_opt_pit_chain_ticks(), NULL);

    ico_difficulty_set_row(ICO_DIFF_PIT_TIME, v);
    (void)ico_opt_pit_time(); /* its line on a change */
}

int ico_opt_pit_seconds(void)
{
    const int v = ico_opt_pit_time();

    return v != ICO_PIT_ORIGINAL ? v : ico_opt_pit_base_seconds();
}

float ico_opt_pit_time_factor(void)
{
    const int v = ico_opt_pit_time();

    /* the seconds over the disc's own length: 8 s on a 462-tick pull-in is 240/462 */
    return v == ICO_PIT_ORIGINAL ? 1.0f
                                 : (float)(v * PIT_TICK_HZ) / (float)ico_opt_pit_chain_ticks();
}

float ico_opt_pit_dive_ratio(int camera)
{
    return (camera != 0 ? 2.0f : 1.0f) / ico_opt_pit_time_factor();
}

/* the shadow speed last logged (-1: none yet) */
static int s_speed_logged = -1;
static const char *const kSpeedNames[ICO_SPEED_COUNT] = {"slower", "original", "faster", "fastest"};
static const float kSpeedFactors[ICO_SPEED_COUNT] = {0.75f, 1.0f, 1.25f, 1.5f};

static void log_shadow_speed(int v)
{
    fprintf(stderr, "gameplay: shadow speed %s (x%.2f)\n", kSpeedNames[v],
            (double)kSpeedFactors[v]);
}

int ico_opt_shadow_speed(void)
{
    int v = ico_difficulty_row(ICO_DIFF_SHADOW_SPEED);

    v = v >= 0 && v < ICO_SPEED_COUNT ? v : ICO_SPEED_ORIGINAL;
    if (v != s_speed_logged) {
        s_speed_logged = v;
        log_shadow_speed(v);
    }
    return v;
}

void ico_opt_set_shadow_speed(int v)
{
    v = v >= 0 && v < ICO_SPEED_COUNT ? v : ICO_SPEED_ORIGINAL;
    ico_difficulty_set_row(ICO_DIFF_SHADOW_SPEED, v);
    (void)ico_opt_shadow_speed(); /* its line on a change */
}

float ico_opt_shadow_speed_factor(void)
{
    return kSpeedFactors[ico_opt_shadow_speed()];
}

int ico_opt_shadow_speed_motion(int motion)
{
    /* options.h: each disc's locomotion motions, -1 ending the list */
    static const int kPal[] = {836, 838, 841, 842, 843, 844, 845, 846, 941, 942, -1};
    static const int kNtsc[] = {790, 792, 795, 796, 797, 798, 799, 800, 888, 889, -1};
    static const int kAug6[] = {792, 794, 797, 798, 799, 800, 801, 802, 890, 891, -1};
    static const int kJun26[] = {823, 825, 828, 829, 830, 831, 832,
                                 833, 923, 924, 942, 944, 955, -1};
    const char *slug = ico_region_cur()->slug;
    const int *m = strcmp(slug, "ntsc") == 0    ? kNtsc
                   : strcmp(slug, "aug6") == 0  ? kAug6
                   : strcmp(slug, "jun26") == 0 ? kJun26
                                                : kPal;

    for (; *m >= 0; m++) {
        if (*m == motion) {
            return 1;
        }
    }
    return 0;
}

float ico_opt_shadow_turn_limit(int kind, int motion, float s)
{
    static unsigned scaled;
    float f;

    if (kind != 4 || !ico_opt_shadow_speed_motion(motion)) {
        return s;
    }
    f = ico_opt_shadow_speed_factor();
    if (f == 1.0f) {
        return s;
    }
    /* counted for the 10 s summaries: how often a shadow's turn limit was scaled */
    if (++scaled <= 8 || scaled % 100 == 0) {
        fprintf(stderr, "gameplay: shadow turn x%.2f (%.1f -> %.1f degrees a tick, %u)\n",
                (double)f, (double)s, (double)(s * f), scaled);
    }
    return s * f;
}

int ico_opt_door_fix(void)
{
    return get(&s_door_fix, "gameplay.door_fix");
}

void ico_opt_set_door_fix(int on)
{
    s_door_fix = on != 0;
}

/* Yorda trips and Shadows attack together last logged (-1: none yet) */
static int s_trips_logged = -1;
static int s_together_logged = -1;

int ico_opt_yorda_trips(void)
{
    const int on = ico_difficulty_row(ICO_DIFF_YORDA_TRIPS) != 0;

    if (on != s_trips_logged) {
        s_trips_logged = on;
        fprintf(stderr, "gameplay: yorda trips %s\n", on ? "on" : "off");
    }
    return on;
}

void ico_opt_set_yorda_trips(int on)
{
    ico_difficulty_set_row(ICO_DIFF_YORDA_TRIPS, on != 0);
    (void)ico_opt_yorda_trips(); /* its line on a change */
}

static void log_shadows_together(int on)
{
    if (on) {
        fprintf(stderr, "gameplay: shadows attack together: all at once (no turns)\n");
    } else {
        fprintf(stderr, "gameplay: shadows attack together: 1 at once (the game's turns)\n");
    }
}

int ico_opt_shadows_together(void)
{
    const int on = ico_difficulty_row(ICO_DIFF_SHADOWS_TOGETHER) != 0;

    if (on != s_together_logged) {
        s_together_logged = on;
        log_shadows_together(on);
    }
    return on;
}

void ico_opt_set_shadows_together(int on)
{
    ico_difficulty_set_row(ICO_DIFF_SHADOWS_TOGETHER, on != 0);
    (void)ico_opt_shadows_together(); /* its line on a change */
}

int ico_opt_shadow_waits(int other_attacking)
{
    if (!other_attacking) {
        return 0;
    }
    if (!ico_opt_shadows_together()) {
        return 1; /* the game's turn */
    }
    s_shadows_together_went++;
    if (s_shadows_together_went <= 8 || s_shadows_together_went % 100 == 0) {
        fprintf(stderr,
                "gameplay: shadows attack together: a shadow did not wait for another's attack "
                "(%lu this run)\n",
                s_shadows_together_went);
    }
    return 0;
}

unsigned long ico_opt_shadows_together_went(void)
{
    return s_shadows_together_went;
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

int ico_opt_second_memory_card(void)
{
    if (s_second_card < 0) {
        s_second_card = ico_config_get_bool("gameplay.second_memory_card", 0) != 0;
        fprintf(stderr, "gameplay: second memory card %s\n", s_second_card ? "on" : "off");
    }
    return s_second_card;
}

static void (*s_second_card_listener)(int on);

void ico_opt_set_second_memory_card(int on)
{
    if ((on != 0) != s_second_card) {
        fprintf(stderr, "gameplay: second memory card %s\n", on ? "on" : "off");
    }
    s_second_card = on != 0;
    if (s_second_card_listener != NULL) {
        s_second_card_listener(s_second_card);
    }
}

void ico_opt_set_second_memory_card_listener(void (*fn)(int on))
{
    s_second_card_listener = fn;
    if (fn != NULL) {
        fn(ico_opt_second_memory_card());
    }
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
    /* the title belongs to no journey: Normal (difficulty.h) */
    ico_difficulty_reset();
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

/* [ui] language's and [game] text_language's names, UiLang order
   (port/ui/strings.h) */
static const char *const kUiLanguages[] = {
    "en", "fr", "de", "it", "es", "ja", "ko", "zh",
    /* the community translations, from ICO_OPT_TEXT_LANGUAGE_FIRST */
    "ru-bobttz", "ru-cobra838", "pt-br"};
#define UI_LANGUAGES ((int)(sizeof(kUiLanguages) / sizeof(kUiLanguages[0])))
_Static_assert(UI_LANGUAGES == ICO_OPT_UI_LANGUAGE_COUNT, "a name for each UiLang");

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
            fprintf(stderr,
                    "options: [ui] language = \"%s\" is not one of auto, en, fr, de, it, es, ja, "
                    "ko, zh, ru-bobttz, ru-cobra838, pt-br\n",
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

static int textLanguageOk(int lang)
{
    return lang >= ICO_OPT_TEXT_LANGUAGE_FIRST && lang < UI_LANGUAGES;
}

int ico_opt_text_language(void)
{
    if (s_text_language < -1) {
        const char *v = ico_config_get_string("game.text_language", "");

        s_text_language = -1;
        for (int i = ICO_OPT_TEXT_LANGUAGE_FIRST; v != NULL && i < UI_LANGUAGES; i++) {
            if (strcmp(v, kUiLanguages[i]) == 0) {
                s_text_language = i;
            }
        }
        if (s_text_language < 0 && v != NULL && v[0] != '\0') {
            fprintf(stderr,
                    "options: [game] text_language = \"%s\" is not one of \"\", ru-bobttz, "
                    "ru-cobra838, pt-br; the game's own text\n",
                    v);
        } else if (s_text_language >= 0) {
            fprintf(stderr, "options: [game] text_language = \"%s\": a community translation\n",
                    kUiLanguages[s_text_language]);
        }
    }
    return s_text_language;
}

void ico_opt_set_text_language(int lang)
{
    s_text_language = textLanguageOk(lang) ? lang : -1;
}

const char *ico_opt_text_language_name(int lang)
{
    return textLanguageOk(lang) ? kUiLanguages[lang] : "";
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
    /* the difficulty rows (yorda_safe, pit_time, shadow_speed, yorda_trips,
       shadows_together) are the journey's, not the config's: kept */
    s_stick_fix = s_door_fix = s_mirror = s_developer_mode = s_circle_back = -1;
    s_natural_chains = s_skip_opening = s_second_card = -1;
    ico_rows_reload();
    s_output = -2;
    s_ui_language = -2;
    s_text_language = -2;
    s_hold_choice = -1;
    /* the characters' colours ([characters], appearance.h) */
    ico_appearance_reload();
    /* the game variants ([variants], variants.h) */
    ico_variants_reload();
}
