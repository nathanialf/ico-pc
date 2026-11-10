/*
 * port/game/difficulty.c
 *
 * The journey's difficulty record (difficulty.h): the presets per disc, the
 * classification, the active record and its lifecycle, the rows text, the
 * carry-over from the retired config keys and from an old state's keys, the
 * knock-down limit, Permadeath, No Shadows, the save-state block and the
 * listener.  Names no game symbol: the game hands over what it reads (the
 * hook sites call the functions below), so the tests link it without the
 * game.  The sidecar (difficulty.toml) is difficulty_file.c's.
 */
#include "difficulty.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "options.h"
#include "region.h" /* port/core: the disc's predicates (ICO_JUN26, ICO_NTSCAUG6JUN26) */
#include "variants.h"

#include "../platform/state_registry.h"

_Static_assert(sizeof(IcoDifficulty) == 256, "the save-state block keeps its size");
_Static_assert(ICO_DIFF_ROW_COUNT <= ICO_DIFF_ROW_SLOTS, "the rows fit the record");

static IcoDifficulty s_active;
static int s_init;
static IcoDiffListener s_listener;

static const char *const kModeNames[ICO_DIFF_MODE_COUNT] = {"easy", "normal", "hard", "custom"};
/* the log's words for a mode */
static const char *const kModeShown[ICO_DIFF_MODE_COUNT] = {"Easy", "Normal", "Hard", "Custom"};
static const char *const kOriginNames[ICO_DIFF_ORIGIN_COUNT] = {"none", "new", "config", "state"};
static const char *const kSpeedWords[ICO_SPEED_COUNT] = {"slower", "original", "faster", "fastest"};

/* the rows text's keys, IcoDiffRow order: the retired config words for the
   fifteen old rows */
static const char *const kRowKeys[ICO_DIFF_ROW_COUNT] = {
    "shadow_speed",     "shade_toughness", "aggression",   "peaceful_shadows", "yorda_safe",
    "shadows_together", "no_shadows",      "auto_guard",   "attack_turn",      "shadows_wait_down",
    "one_hit",          "never_dodge",     "barrier_hits", "pit_time",         "yorda_trips",
    "no_hints",         "knock_limit",     "knock_scope",  "permadeath"};

/* The fifteen retired config keys (port/platform/sim_keys.c
   ICO_SIM_KEYS_RETIRED, the same order) and the row each one was */
static const struct {
    const char *key;
    IcoDiffRow row;
} kRetired[] = {
    {"gameplay.yorda_safe", ICO_DIFF_YORDA_SAFE},
    {"gameplay.pit_time", ICO_DIFF_PIT_TIME},
    {"gameplay.shadow_speed", ICO_DIFF_SHADOW_SPEED},
    {"gameplay.yorda_trips", ICO_DIFF_YORDA_TRIPS},
    {"gameplay.shadows_together", ICO_DIFF_SHADOWS_TOGETHER},
    {"variants.shade_toughness", ICO_DIFF_SHADE_TOUGHNESS},
    {"variants.one_hit", ICO_DIFF_ONE_HIT},
    {"variants.never_dodge", ICO_DIFF_NEVER_DODGE},
    {"variants.aggression", ICO_DIFF_AGGRESSION},
    {"variants.no_hints", ICO_DIFF_NO_HINTS},
    {"variants.barrier_hits", ICO_DIFF_BARRIER_HITS},
    {"variants.auto_guard", ICO_DIFF_AUTO_GUARD},
    {"variants.attack_turn", ICO_DIFF_ATTACK_TURN},
    {"variants.shadows_wait_down", ICO_DIFF_SHADOWS_WAIT_DOWN},
    {"variants.peaceful_shadows", ICO_DIFF_PEACEFUL},
};

#define RETIRED_COUNT ((int)(sizeof(kRetired) / sizeof(kRetired[0])))
_Static_assert(sizeof(kRetired) / sizeof(kRetired[0]) == 15, "the fifteen moved keys");

/* --- the rows' values --------------------------------------------------------- */

/* the Queen's barrier as the disc ships it (queen.c: 7 on the June 2001 demo) */
static int shipped_barrier(void)
{
    return ICO_JUN26 ? 7 : 5;
}

int ico_difficulty_row_valid(IcoDiffRow row, int value)
{
    switch (row) {
    case ICO_DIFF_SHADOW_SPEED:
        return value >= 0 && value < ICO_SPEED_COUNT;
    case ICO_DIFF_SHADE_TOUGHNESS:
        return ico_variant_offered(ICO_VARIANT_SHADE_TOUGHNESS, value);
    case ICO_DIFF_AGGRESSION:
        return value >= 0 && value < ICO_AGGRESSION_COUNT;
    case ICO_DIFF_BARRIER_HITS:
        return value == 0 || value == 5 || value == 7;
    case ICO_DIFF_PIT_TIME:
        return value == ICO_PIT_ORIGINAL ||
               (value >= ICO_PIT_MIN_SECONDS && value <= ICO_PIT_MAX_SECONDS);
    case ICO_DIFF_KNOCK_LIMIT:
        return value >= 0 && value <= ICO_DIFF_KNOCK_MAX;
    default:
        return row >= 0 && row < ICO_DIFF_ROW_COUNT && (value == 0 || value == 1);
    }
}

const char *ico_difficulty_row_key(IcoDiffRow row)
{
    return row >= 0 && row < ICO_DIFF_ROW_COUNT ? kRowKeys[row] : "";
}

const char *ico_difficulty_mode_name(IcoDiffMode mode)
{
    return mode >= 0 && mode < ICO_DIFF_MODE_COUNT ? kModeNames[mode] : "";
}

int ico_difficulty_mode_parse(const char *word)
{
    for (int i = 0; word != NULL && i < ICO_DIFF_MODE_COUNT; i++) {
        if (strcmp(word, kModeNames[i]) == 0) {
            return i;
        }
    }
    return -1;
}

const char *ico_difficulty_origin_name(IcoDiffOrigin origin)
{
    return origin >= 0 && origin < ICO_DIFF_ORIGIN_COUNT ? kOriginNames[origin] : "";
}

int ico_difficulty_origin_parse(const char *word)
{
    for (int i = 0; word != NULL && i < ICO_DIFF_ORIGIN_COUNT; i++) {
        if (strcmp(word, kOriginNames[i]) == 0) {
            return i;
        }
    }
    return -1;
}

/* the word a row's value is written as (difficulty.h rows_format) */
static const char *row_word(IcoDiffRow row, int v, char *buf, size_t size)
{
    switch (row) {
    case ICO_DIFF_SHADOW_SPEED:
        return v >= 0 && v < ICO_SPEED_COUNT ? kSpeedWords[v] : "original";
    case ICO_DIFF_SHADE_TOUGHNESS:
        return ico_variant_value_name(ICO_VARIANT_SHADE_TOUGHNESS, v);
    case ICO_DIFF_AGGRESSION:
        return ico_variant_value_name(ICO_VARIANT_AGGRESSION, v);
    case ICO_DIFF_BARRIER_HITS:
        return v == 5 ? "5" : v == 7 ? "7" : "disc";
    case ICO_DIFF_PIT_TIME:
        if (v == ICO_PIT_ORIGINAL) {
            return "original";
        }
        snprintf(buf, size, "%d", v);
        return buf;
    case ICO_DIFF_KNOCK_LIMIT:
        snprintf(buf, size, "%d", v);
        return buf;
    case ICO_DIFF_KNOCK_SCOPE:
        return v == ICO_DIFF_SCOPE_JOURNEY ? "journey" : "room";
    default:
        return v ? "true" : "false";
    }
}

/* text[0, n) is word */
static int word_is(const char *text, size_t n, const char *word)
{
    return strlen(word) == n && memcmp(text, word, n) == 0;
}

/* A row's value from its word (n bytes): 1 and *out, or 0 when the word is
   none of the row's.  The pit time takes every word ico_opt_pit_parse reads
   (a number held to 4..30 is a value; *held set for the log). */
static int parse_value(IcoDiffRow row, const char *text, size_t n, int *out, int *held)
{
    char buf[32];

    if (held != NULL) {
        *held = 0;
    }
    switch (row) {
    case ICO_DIFF_SHADOW_SPEED:
        for (int i = 0; i < ICO_SPEED_COUNT; i++) {
            if (word_is(text, n, kSpeedWords[i])) {
                *out = i;
                return 1;
            }
        }
        return 0;
    case ICO_DIFF_SHADE_TOUGHNESS:
    case ICO_DIFF_AGGRESSION: {
        const IcoVariantId id =
            row == ICO_DIFF_SHADE_TOUGHNESS ? ICO_VARIANT_SHADE_TOUGHNESS : ICO_VARIANT_AGGRESSION;
        for (int i = 0; i < ico_variant_count(id); i++) {
            if (word_is(text, n, ico_variant_value_name(id, i))) {
                *out = i;
                return 1;
            }
        }
        return 0;
    }
    case ICO_DIFF_BARRIER_HITS:
        if (word_is(text, n, "disc") || word_is(text, n, "0")) {
            *out = 0;
        } else if (word_is(text, n, "5") || word_is(text, n, "7")) {
            *out = text[0] - '0';
        } else {
            return 0;
        }
        return 1;
    case ICO_DIFF_PIT_TIME: {
        int note = 0;
        int v;

        if (n == 0 || n >= sizeof(buf)) {
            return 0;
        }
        memcpy(buf, text, n);
        buf[n] = '\0';
        v = ico_opt_pit_parse(buf, ico_opt_pit_chain_ticks(), &note);
        if (note == 1) {
            /* a number held to 4..30 is a value; a word ico_opt_pit_parse does
               not know is none */
            char *end = NULL;
            (void)strtol(buf, &end, 10);
            if (end == buf || *end != '\0') {
                return 0;
            }
        }
        *out = v;
        if (held != NULL) {
            *held = note;
        }
        return 1;
    }
    case ICO_DIFF_KNOCK_LIMIT:
        if (word_is(text, n, "off")) {
            *out = 0;
            return 1;
        }
        if (n == 1 && text[0] >= '0' && text[0] <= '9') {
            *out = text[0] - '0';
            return 1;
        }
        return 0;
    case ICO_DIFF_KNOCK_SCOPE:
        if (word_is(text, n, "room")) {
            *out = ICO_DIFF_SCOPE_ROOM;
        } else if (word_is(text, n, "journey")) {
            *out = ICO_DIFF_SCOPE_JOURNEY;
        } else {
            return 0;
        }
        return 1;
    default:
        if (word_is(text, n, "true") || word_is(text, n, "1")) {
            *out = 1;
        } else if (word_is(text, n, "false") || word_is(text, n, "0")) {
            *out = 0;
        } else {
            return 0;
        }
        return 1;
    }
}

/* --- presets ---------------------------------------------------------------- */

static void normal_rows(int32_t *row)
{
    memset(row, 0, sizeof(int32_t) * ICO_DIFF_ROW_SLOTS);
    row[ICO_DIFF_SHADOW_SPEED] = ICO_SPEED_ORIGINAL;
    row[ICO_DIFF_SHADE_TOUGHNESS] = ICO_TOUGHNESS_ORIGINAL;
    row[ICO_DIFF_AGGRESSION] = ICO_AGGRESSION_OWN;
    row[ICO_DIFF_AUTO_GUARD] = 1;
    row[ICO_DIFF_ATTACK_TURN] = 1;
    row[ICO_DIFF_SHADOWS_WAIT_DOWN] = 1;
    row[ICO_DIFF_BARRIER_HITS] = 0;
    row[ICO_DIFF_PIT_TIME] = ICO_PIT_ORIGINAL;
    row[ICO_DIFF_KNOCK_SCOPE] = ICO_DIFF_SCOPE_ROOM;
}

/* Hard's Shadow toughness on the selected disc (plan.md "Presets") */
static int hard_toughness(void)
{
    if (ICO_JUN26) {
        return ICO_TOUGHNESS_ORIGINAL;
    }
    return ICO_NTSCAUG6JUN26 ? ICO_TOUGHNESS_AMERICAN : ICO_TOUGHNESS_THIRD_JOURNEY;
}

/* each row a value this disc's record can hold: another (a damaged state, a
   value the disc does not offer) is Normal's, logged */
static int make_valid(IcoDifficulty *d, const char *what)
{
    int32_t normal[ICO_DIFF_ROW_SLOTS];
    char buf[16];
    int fixed = 0;

    normal_rows(normal);
    for (int r = 0; r < ICO_DIFF_ROW_COUNT; r++) {
        if (!ico_difficulty_row_valid((IcoDiffRow)r, d->row[r])) {
            fprintf(stderr, "difficulty: %s: %s = %d is not offered on this disc (%s); using %s\n",
                    what, kRowKeys[r], (int)d->row[r], ico_region_cur()->slug,
                    row_word((IcoDiffRow)r, normal[r], buf, sizeof(buf)));
            d->row[r] = normal[r];
            fixed++;
        }
    }
    for (int r = ICO_DIFF_ROW_COUNT; r < ICO_DIFF_ROW_SLOTS; r++) {
        d->row[r] = 0;
    }
    if (d->mode < 0 || d->mode >= ICO_DIFF_MODE_COUNT) {
        d->mode = ICO_DIFF_CUSTOM;
        fixed++;
    }
    if (d->origin < 0 || d->origin >= ICO_DIFF_ORIGIN_COUNT) {
        d->origin = ICO_DIFF_ORIGIN_NONE;
        fixed++;
    }
    return fixed;
}

void ico_difficulty_preset(IcoDiffMode mode, IcoDifficulty *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->version = ICO_DIFF_VERSION;
    out->mode = mode >= 0 && mode < ICO_DIFF_MODE_COUNT ? mode : ICO_DIFF_NORMAL;
    out->origin = ICO_DIFF_ORIGIN_NONE;
    normal_rows(out->row);
    if (mode == ICO_DIFF_EASY) {
        out->row[ICO_DIFF_SHADOW_SPEED] = ICO_SPEED_SLOWER;
        out->row[ICO_DIFF_SHADE_TOUGHNESS] = ICO_TOUGHNESS_NO_PLUS;
        out->row[ICO_DIFF_AGGRESSION] = ICO_AGGRESSION_WARY_PATIENT;
        out->row[ICO_DIFF_YORDA_SAFE] = 1;
        out->row[ICO_DIFF_NEVER_DODGE] = 1;
        out->row[ICO_DIFF_PIT_TIME] = 25;
    } else if (mode == ICO_DIFF_HARD) {
        out->row[ICO_DIFF_SHADOW_SPEED] = ICO_SPEED_FASTER;
        out->row[ICO_DIFF_SHADE_TOUGHNESS] = hard_toughness();
        out->row[ICO_DIFF_AGGRESSION] = ICO_AGGRESSION_BOLD_QUICK;
        out->row[ICO_DIFF_AUTO_GUARD] = 0;
        out->row[ICO_DIFF_ATTACK_TURN] = 0;
        out->row[ICO_DIFF_SHADOWS_WAIT_DOWN] = 0;
        out->row[ICO_DIFF_SHADOWS_TOGETHER] = 1;
        out->row[ICO_DIFF_BARRIER_HITS] = 7;
        out->row[ICO_DIFF_PIT_TIME] = 8;
        out->row[ICO_DIFF_KNOCK_LIMIT] = 3;
        out->row[ICO_DIFF_KNOCK_SCOPE] = ICO_DIFF_SCOPE_ROOM;
    }
    make_valid(out, "the preset");
}

/* a row's value as the disc plays it: a barrier of the disc's own number is
   the disc's, a time to rescue of the disc's own seconds Original */
static int canonical(IcoDiffRow row, int v)
{
    if (row == ICO_DIFF_BARRIER_HITS && v == shipped_barrier()) {
        return 0;
    }
    if (row == ICO_DIFF_PIT_TIME && v == ico_opt_pit_base_seconds()) {
        return ICO_PIT_ORIGINAL;
    }
    return v;
}

static int rows_equal(const int32_t *a, const int32_t *b)
{
    for (int r = 0; r < ICO_DIFF_ROW_COUNT; r++) {
        if (canonical((IcoDiffRow)r, a[r]) != canonical((IcoDiffRow)r, b[r])) {
            return 0;
        }
    }
    return 1;
}

IcoDiffMode ico_difficulty_classify(const IcoDifficulty *d)
{
    static const IcoDiffMode kOrder[3] = {ICO_DIFF_NORMAL, ICO_DIFF_EASY, ICO_DIFF_HARD};
    IcoDifficulty p;

    if (d == NULL) {
        return ICO_DIFF_CUSTOM;
    }
    for (int i = 0; i < 3; i++) {
        ico_difficulty_preset(kOrder[i], &p);
        if (rows_equal(d->row, p.row)) {
            return kOrder[i];
        }
    }
    return ICO_DIFF_CUSTOM;
}

int ico_difficulty_row_step(IcoDifficulty *d, IcoDiffRow row, int dir)
{
    int v;

    if (d == NULL || row < 0 || row >= ICO_DIFF_ROW_COUNT) {
        return 0;
    }
    v = d->row[row];
    if (dir == 0) {
        return v;
    }
    dir = dir > 0 ? 1 : -1;
    switch (row) {
    case ICO_DIFF_SHADOW_SPEED:
        v = ((v + dir) % ICO_SPEED_COUNT + ICO_SPEED_COUNT) % ICO_SPEED_COUNT;
        break;
    case ICO_DIFF_SHADE_TOUGHNESS:
    case ICO_DIFF_AGGRESSION: {
        const int n = row == ICO_DIFF_AGGRESSION ? ICO_AGGRESSION_COUNT : ICO_TOUGHNESS_COUNT;
        const int from = v;
        do {
            v = ((v + dir) % n + n) % n;
        } while (!ico_difficulty_row_valid(row, v) && v != from);
        break;
    }
    case ICO_DIFF_BARRIER_HITS: {
        static const int kCycle[3] = {0, 5, 7};
        int i = v == 5 ? 1 : v == 7 ? 2 : 0;
        v = kCycle[((i + dir) % 3 + 3) % 3];
        break;
    }
    case ICO_DIFF_PIT_TIME: {
        const int base = ico_opt_pit_base_seconds();
        int secs = v == ICO_PIT_ORIGINAL ? base : v;

        secs += dir;
        secs = secs < ICO_PIT_MIN_SECONDS   ? ICO_PIT_MIN_SECONDS
               : secs > ICO_PIT_MAX_SECONDS ? ICO_PIT_MAX_SECONDS
                                            : secs;
        v = secs == base ? ICO_PIT_ORIGINAL : secs;
        break;
    }
    case ICO_DIFF_KNOCK_LIMIT:
        v = ((v + dir) % (ICO_DIFF_KNOCK_MAX + 1) + ICO_DIFF_KNOCK_MAX + 1) %
            (ICO_DIFF_KNOCK_MAX + 1);
        break;
    default:
        v = v ? 0 : 1;
        break;
    }
    d->row[row] = v;
    return v;
}

/* --- the rows text ------------------------------------------------------------- */

int ico_difficulty_rows_format(const IcoDifficulty *d, char *out, size_t size)
{
    size_t at = 0;
    int total = 0;

    if (out != NULL && size > 0) {
        out[0] = '\0';
    }
    if (d == NULL) {
        return 0;
    }
    for (int r = 0; r < ICO_DIFF_ROW_COUNT; r++) {
        char buf[16];
        const int n =
            snprintf(out != NULL && at < size ? out + at : NULL,
                     out != NULL && at < size ? size - at : 0, "%s%s=%s", r == 0 ? "" : " ",
                     kRowKeys[r], row_word((IcoDiffRow)r, d->row[r], buf, sizeof(buf)));
        if (n < 0) {
            break;
        }
        total += n;
        at += (size_t)n;
    }
    return total;
}

int ico_difficulty_rows_parse(const char *text, IcoDifficulty *out)
{
    const char *p = text;
    int bad = 0;

    if (out == NULL) {
        return 0;
    }
    normal_rows(out->row);
    while (p != NULL && *p != '\0') {
        const char *start, *eq, *end;
        int r, v = 0;

        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        start = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
            p++;
        }
        end = p;
        eq = memchr(start, '=', (size_t)(end - start));
        for (r = 0; eq != NULL && r < ICO_DIFF_ROW_COUNT; r++) {
            if (word_is(start, (size_t)(eq - start), kRowKeys[r])) {
                break;
            }
        }
        if (eq == NULL || r == ICO_DIFF_ROW_COUNT ||
            !parse_value((IcoDiffRow)r, eq + 1, (size_t)(end - eq - 1), &v, NULL) ||
            !ico_difficulty_row_valid((IcoDiffRow)r, v)) {
            if (++bad <= 8) {
                fprintf(stderr, "difficulty: the rows text's \"%.*s\" is no row value; ignored\n",
                        (int)(end - start), start);
            }
            continue;
        }
        out->row[r] = v;
    }
    return bad;
}

/* --- the active record ------------------------------------------------------------ */

static void ensure(void)
{
    if (!s_init) {
        s_init = 1;
        ico_difficulty_preset(ICO_DIFF_NORMAL, &s_active);
    }
}

void ico_difficulty_journey_text(const IcoDifficulty *d, char *out, size_t size)
{
    if (out == NULL || size == 0) {
        return;
    }
    snprintf(out, size, "%08x%08x", d != NULL ? (unsigned)d->journey[0] : 0u,
             d != NULL ? (unsigned)d->journey[1] : 0u);
}

/* the rows of d that differ from Normal, as "key=word key=word" ("" for none) */
static void diff_text(const IcoDifficulty *d, char *out, size_t size)
{
    int32_t normal[ICO_DIFF_ROW_SLOTS];
    size_t at = 0;

    normal_rows(normal);
    out[0] = '\0';
    for (int r = 0; r < ICO_DIFF_ROW_COUNT && at < size; r++) {
        char buf[16];
        int n;

        if (canonical((IcoDiffRow)r, d->row[r]) == canonical((IcoDiffRow)r, normal[r])) {
            continue;
        }
        n = snprintf(out + at, size - at, "%s%s=%s", at == 0 ? "" : " ", kRowKeys[r],
                     row_word((IcoDiffRow)r, d->row[r], buf, sizeof(buf)));
        if (n < 0) {
            break;
        }
        at += (size_t)n;
    }
}

static void log_record(const char *why, const IcoDifficulty *d)
{
    char diff[ICO_DIFF_ROWS_TEXT_MAX];
    char id[20];

    diff_text(d, diff, sizeof(diff));
    ico_difficulty_journey_text(d, id, sizeof(id));
    fprintf(stderr, "difficulty: %s in force (%s; journey %s, origin %s, disc %s): %s\n",
            kModeShown[d->mode >= 0 && d->mode < ICO_DIFF_MODE_COUNT ? d->mode : ICO_DIFF_CUSTOM],
            why, id, ico_difficulty_origin_name((IcoDiffOrigin)d->origin), ico_region_cur()->slug,
            diff[0] != '\0' ? diff : "every row the game's own");
    if (d->row[ICO_DIFF_KNOCK_LIMIT] > 0 || d->knocks_journey > 0 || d->finished) {
        fprintf(stderr,
                "difficulty: knock-downs %d in this room, %d on this journey (limit %d, %s); "
                "finished %d\n",
                (int)d->knocks_room, (int)d->knocks_journey, (int)d->row[ICO_DIFF_KNOCK_LIMIT],
                d->row[ICO_DIFF_KNOCK_SCOPE] == ICO_DIFF_SCOPE_JOURNEY ? "whole journey"
                                                                       : "each room",
                (int)d->finished);
    }
}

static void tell(IcoDiffEvent ev, int a, int b)
{
    if (s_listener != NULL) {
        s_listener(ev, a, b);
    }
}

/* the record in force changed: the variants' debug switches follow (One hit,
   Never dodge, Aggression), the listener hears it */
static void apply(void)
{
    ico_variants_apply();
    tell(ICO_DIFF_EV_ACTIVE, 0, 0);
}

static uint64_t splitmix(uint64_t *x)
{
    uint64_t z = (*x += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

void ico_difficulty_new_journey(uint32_t out[2])
{
    static uint64_t s_seed;
    static unsigned s_count;
    uint64_t v;
    int local = 0;

    if (s_seed == 0) {
        s_seed = (uint64_t)time(NULL) * 0x100000001B3ull ^ (uint64_t)clock() ^
                 (uint64_t)(uintptr_t)&local;
    }
    s_seed ^= (uint64_t)(++s_count) << 32 ^ (uint64_t)clock();
    do {
        v = splitmix(&s_seed);
    } while (v == 0);
    out[0] = (uint32_t)(v >> 32);
    out[1] = (uint32_t)v;
}

int ico_difficulty_has_journey(void)
{
    ensure();
    return s_active.journey[0] != 0 || s_active.journey[1] != 0;
}

void ico_difficulty_begin(const IcoDifficulty *draft)
{
    IcoDifficulty d;

    ensure();
    if (draft != NULL) {
        d = *draft;
    } else {
        ico_difficulty_preset(ICO_DIFF_NORMAL, &d);
    }
    d.version = ICO_DIFF_VERSION;
    d.origin = ICO_DIFF_ORIGIN_NEW;
    make_valid(&d, "the New Game's draft");
    d.mode = ico_difficulty_classify(&d);
    ico_difficulty_new_journey(d.journey);
    d.knocks_room = d.knocks_journey = d.in_fall = d.over_pending = d.finished = 0;
    d.run_port = d.run_slot = d.run_has_slot = 0;
    d.run_sum = 0;
    memset(d.reserved, 0, sizeof(d.reserved));
    s_active = d;
    log_record("a New Game", &s_active);
    apply();
}

void ico_difficulty_reset(void)
{
    IcoDifficulty n;

    ensure();
    ico_difficulty_preset(ICO_DIFF_NORMAL, &n);
    if (memcmp(&n, &s_active, sizeof(n)) == 0) {
        return;
    }
    s_active = n;
    log_record("the title", &s_active);
    apply();
}

int ico_difficulty_row(IcoDiffRow row)
{
    ensure();
    return row >= 0 && row < ICO_DIFF_ROW_COUNT ? s_active.row[row] : 0;
}

const IcoDifficulty *ico_difficulty_active(void)
{
    ensure();
    return &s_active;
}

void ico_difficulty_set_active(const IcoDifficulty *d)
{
    if (d == NULL) {
        return;
    }
    ensure();
    s_active = *d;
    s_active.version = ICO_DIFF_VERSION;
    make_valid(&s_active, "a save's record");
    log_record("a save's record", &s_active);
    apply();
}

void ico_difficulty_set_row(IcoDiffRow row, int value)
{
    char buf[16];

    ensure();
    if (row < 0 || row >= ICO_DIFF_ROW_COUNT) {
        return;
    }
    if (!ico_difficulty_row_valid(row, value)) {
        fprintf(stderr, "difficulty: %s = %d is not a value of the row on this disc; unchanged\n",
                kRowKeys[row], value);
        return;
    }
    if (s_active.row[row] == value) {
        return;
    }
    s_active.row[row] = value;
    s_active.mode = ico_difficulty_classify(&s_active);
    fprintf(stderr, "difficulty: %s = %s on this journey (now %s)\n", kRowKeys[row],
            row_word(row, value, buf, sizeof(buf)), kModeShown[s_active.mode]);
    tell(ICO_DIFF_EV_ACTIVE, 0, 0);
}

void ico_difficulty_set_run(int port, int slot, uint32_t sum)
{
    ensure();
    s_active.run_port = port;
    s_active.run_slot = slot;
    s_active.run_has_slot = 1;
    s_active.run_sum = sum;
}

/* --- the carry-over ------------------------------------------------------------- */

/* the value text of a retired key: config's, or a state's keys text */
typedef const char *(*KeyGetFn)(void *ctx, const char *key, size_t *n);

static const char *config_get(void *ctx, const char *key, size_t *n)
{
    const char *v = ico_config_get_string(key, NULL);

    (void)ctx;
    *n = v != NULL ? strlen(v) : 0;
    return v;
}

/* key's value in a "key=value\n" text */
static const char *keys_get(void *ctx, const char *key, size_t *n)
{
    const char *line = (const char *)ctx;
    const size_t kn = strlen(key);

    while (line != NULL && *line != '\0') {
        const char *end = strchr(line, '\n');
        const size_t len = end != NULL ? (size_t)(end - line) : strlen(line);

        if (len > kn && memcmp(line, key, kn) == 0 && line[kn] == '=') {
            *n = len - kn - 1;
            return line + kn + 1;
        }
        line = end != NULL ? end + 1 : NULL;
    }
    *n = 0;
    return NULL;
}

/* the fifteen rows from their old keys, the others Normal's; logs what was
   not a value; returns the number of keys that were set */
static int fill_from(IcoDifficulty *out, KeyGetFn get, void *ctx, const char *where)
{
    int set = 0;

    memset(out, 0, sizeof(*out));
    out->version = ICO_DIFF_VERSION;
    normal_rows(out->row);
    for (int i = 0; i < RETIRED_COUNT; i++) {
        size_t n = 0;
        const char *v = get(ctx, kRetired[i].key, &n);
        int value = 0, held = 0;

        if (v == NULL || n == 0) {
            continue; /* unset: the game's own */
        }
        set++;
        if (!parse_value(kRetired[i].row, v, n, &value, &held)) {
            fprintf(stderr, "difficulty: %s %s = \"%.*s\" is not a value; the game's own\n", where,
                    kRetired[i].key, (int)n, v);
            continue;
        }
        if (held) {
            fprintf(stderr, "difficulty: %s %s = \"%.*s\" is held to %d s\n", where,
                    kRetired[i].key, (int)n, v, value);
        }
        out->row[kRetired[i].row] = value;
    }
    make_valid(out, where);
    out->mode = ico_difficulty_classify(out) == ICO_DIFF_NORMAL ? ICO_DIFF_NORMAL : ICO_DIFF_CUSTOM;
    return set;
}

void ico_difficulty_from_config(IcoDifficulty *out)
{
    char diff[ICO_DIFF_ROWS_TEXT_MAX];
    int set;

    if (out == NULL) {
        return;
    }
    set = fill_from(out, config_get, NULL, "config.toml's");
    out->origin = ICO_DIFF_ORIGIN_CONFIG;
    ico_difficulty_new_journey(out->journey);
    diff_text(out, diff, sizeof(diff));
    fprintf(stderr,
            "difficulty: carried over from config.toml (%d of the %d retired keys set): %s (%s)\n",
            set, RETIRED_COUNT, kModeShown[out->mode],
            diff[0] != '\0' ? diff : "every row the game's own");
}

void ico_difficulty_from_keys(const char *state_keys, IcoDifficulty *out)
{
    uint64_t h = 0xCBF29CE484222325ull; /* FNV-1a over the fifteen keys' lines */
    char diff[ICO_DIFF_ROWS_TEXT_MAX];
    int set;

    if (out == NULL) {
        return;
    }
    set = fill_from(out, keys_get, (void *)(state_keys != NULL ? state_keys : ""),
                    "the state's keys:");
    out->origin = ICO_DIFF_ORIGIN_STATE;
    for (int i = 0; i < RETIRED_COUNT; i++) {
        size_t n = 0;
        const char *v =
            keys_get((void *)(state_keys != NULL ? state_keys : ""), kRetired[i].key, &n);
        const char *k = kRetired[i].key;

        for (; *k != '\0'; k++) {
            h = (h ^ (unsigned char)*k) * 0x100000001B3ull;
        }
        h = (h ^ '=') * 0x100000001B3ull;
        for (size_t j = 0; v != NULL && j < n; j++) {
            h = (h ^ (unsigned char)v[j]) * 0x100000001B3ull;
        }
        h = (h ^ '\n') * 0x100000001B3ull;
    }
    if (h == 0) {
        h = 1;
    }
    out->journey[0] = (uint32_t)(h >> 32);
    out->journey[1] = (uint32_t)h;
    diff_text(out, diff, sizeof(diff));
    fprintf(stderr, "difficulty: built from a state's keys (%d of the %d old keys set): %s (%s)\n",
            set, RETIRED_COUNT, kModeShown[out->mode],
            diff[0] != '\0' ? diff : "every row the game's own");
}

/* --- the knock-down limit -------------------------------------------------------- */

static int scope_count(void)
{
    return s_active.row[ICO_DIFF_KNOCK_SCOPE] == ICO_DIFF_SCOPE_JOURNEY ? s_active.knocks_journey
                                                                        : s_active.knocks_room;
}

static const char *scope_words(void)
{
    return s_active.row[ICO_DIFF_KNOCK_SCOPE] == ICO_DIFF_SCOPE_JOURNEY ? "on this journey"
                                                                        : "in this room";
}

int ico_difficulty_knockdown(int is_boy, int landing, int down_timer)
{
    int limit, n;

    ensure();
    if (!is_boy) {
        return 0;
    }
    if (s_active.over_pending) {
        /* another mail replaced the act before the game over fired */
        return 1;
    }
    limit = s_active.row[ICO_DIFF_KNOCK_LIMIT];
    if (limit <= 0 || landing || s_active.in_fall || down_timer != 0) {
        return 0; /* off, or the same fall going on */
    }
    s_active.in_fall = 1;
    s_active.knocks_room++;
    s_active.knocks_journey++;
    n = scope_count();
    fprintf(stderr, "difficulty: knock-down %d of %d %s (room %d, journey %d)\n", n, limit,
            scope_words(), (int)s_active.knocks_room, (int)s_active.knocks_journey);
    tell(ICO_DIFF_EV_KNOCK, n, limit);
    if (n >= limit) {
        s_active.over_pending = 1;
        fprintf(stderr, "difficulty: the knock-down limit (%d %s) is reached: game over\n", limit,
                scope_words());
        tell(ICO_DIFF_EV_LIMIT, n, limit);
        return 1;
    }
    return 0;
}

void ico_difficulty_tick(int valid, int boy_act_mode, int boy_down_timer)
{
    if (!valid || !s_init || !s_active.in_fall) {
        return;
    }
    if (boy_act_mode != ICO_DIFF_DOWN_ACT_MODE && boy_down_timer == 0) {
        s_active.in_fall = 0; /* back on his feet: the next knock-down counts */
    }
}

void ico_difficulty_stage_enter(int stage)
{
    ensure();
    if (s_active.knocks_room > 0 && s_active.row[ICO_DIFF_KNOCK_LIMIT] > 0) {
        fprintf(stderr, "difficulty: room %d entered: the room's %d knock-down(s) start again\n",
                stage, (int)s_active.knocks_room);
    }
    s_active.knocks_room = 0;
}

void ico_difficulty_game_over(void)
{
    const int caused = s_init && s_active.over_pending;

    ensure();
    s_active.over_pending = 0;
    s_active.in_fall = 0;
    if (caused) {
        s_active.knocks_room = 0;
        if (s_active.row[ICO_DIFF_KNOCK_SCOPE] == ICO_DIFF_SCOPE_JOURNEY) {
            fprintf(stderr,
                    "difficulty: the knock-down limit's game over: the journey's count (%d) "
                    "starts again\n",
                    (int)s_active.knocks_journey);
            s_active.knocks_journey = 0;
            ico_difficulty_file_knocks_reset();
        } else {
            fprintf(stderr, "difficulty: the knock-down limit's game over\n");
        }
    }
    if (s_active.row[ICO_DIFF_PERMADEATH] && !s_active.finished) {
        char id[20];

        s_active.finished = 1;
        ico_difficulty_journey_text(&s_active, id, sizeof(id));
        fprintf(stderr,
                "difficulty: Permadeath: the game over ends journey %s (%s; no Continue, its "
                "saves no longer load)\n",
                id, caused ? "the knock-down limit" : "a game over");
        ico_difficulty_file_journey_ended();
        tell(ICO_DIFF_EV_ENDED, 0, 0);
    }
}

int ico_difficulty_finished(void)
{
    ensure();
    return s_active.finished != 0;
}

int ico_no_shadows(void)
{
    ensure();
    return s_active.row[ICO_DIFF_NO_SHADOWS] != 0;
}

/* --- save states --------------------------------------------------------------------- */

int ico_difficulty_state_register(void)
{
    ensure();
    return ico_state_register("difficulty.record", &s_active, sizeof s_active, ICO_STATE_FRESH) != 0
               ? -1
               : 0;
}

void ico_difficulty_state_loaded(int block_absent, const char *saved_keys)
{
    s_init = 1; /* the registry wrote the record's bytes (or kept this run's) */
    if (!block_absent && s_active.version == ICO_DIFF_VERSION) {
        if (make_valid(&s_active, "a state's record") != 0) {
            fprintf(stderr, "difficulty: a state's record had values it cannot hold (above)\n");
        }
        log_record("a save state", &s_active);
    } else {
        IcoDifficulty d;

        if (!block_absent) {
            fprintf(stderr,
                    "difficulty: a state's record is version %d, not %d: built from its keys\n",
                    (int)s_active.version, ICO_DIFF_VERSION);
        }
        ico_difficulty_from_keys(saved_keys, &d);
        s_active = d;
        log_record("a save state made before records", &s_active);
    }
    apply();
}

int ico_difficulty_states_locked(void)
{
    ensure();
    return s_active.row[ICO_DIFF_PERMADEATH] != 0;
}

void ico_difficulty_set_listener(IcoDiffListener fn)
{
    s_listener = fn;
}
