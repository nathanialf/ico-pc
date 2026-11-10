/*
 * port/game/test/difficulty_test.c
 *
 * The journey's difficulty record (port/game/difficulty.h) on the CPU, on
 * the disc named (the presets' toughness and the barrier are the disc's):
 *
 *   presets     Easy, Normal and Hard row by row (plan.md "Presets"), Hard's
 *               toughness per disc, each classified as itself, one change
 *               Custom, the disc's own barrier and seconds equal to Normal's
 *   steps       every row's Left/Right (values the disc does not offer passed
 *               over, the barrier's three values, the seconds held at 4 and
 *               30 with the disc's own reading Original, the limit 0..9)
 *   text        format / parse round trip of every preset and a custom
 *               record, a row left out is Normal's, a word that is no value
 *               counted and ignored, the earlier pit_time words
 *   carry-over  from the retired config keys (every key, the words of the
 *               old readers, a bad word the default, all-default Normal,
 *               else Custom, origin config) and from an old state's keys
 *               text (origin state, the same text the same journey id)
 *   lifecycle   begin (a new journey id, counts 0), reset at the title
 *               (ico_opt_mirror_reset), set_active, set_row, the run's slot;
 *               the accessors of options.h and variants.h read the record;
 *               a moved variant's step writes no config key; no difficulty
 *               row switches achievements off
 *   knock-down  the limit off counts nothing; once a fall (a chained hit, a
 *               landing, a knock-down before the get-up timer is out, a fall
 *               longer than the timer); another actor never; the limit's
 *               return and over_pending; each room (room entry starts again)
 *               and the whole journey (the count goes back to 0 after the game
 *               over the limit causes, not after another)
 *   Permadeath  any game over ends the journey (finished, the listener),
 *               states locked while the row is on
 *   No Shadows  ico_no_shadows follows the row
 *   state       the block's name, size and flag; a restored record applied;
 *               a damaged version and a missing block rebuilt from the keys
 *
 *   difficulty_test DIR [REGION]
 */
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "difficulty.h"
#include "options.h"
#include "../../core/region.h"
#include "state_registry.h"
#include "variants.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static char s_path[512];

static const char *slug(void)
{
    return ico_region_cur()->slug;
}

static int on(const char *s)
{
    return strcmp(slug(), s) == 0;
}

static int y2001(void)
{
    return on("ntsc") || on("aug6") || on("jun26");
}

/* config.toml with TEXT as its body (NULL: no file), the options reloaded and
   the record back to the title's Normal */
static void use_config(const char *text)
{
    FILE *f;

    remove(s_path);
    if (text != NULL) {
        f = fopen(s_path, "wb");
        if (f == NULL) {
            fprintf(stderr, "cannot write %s\n", s_path);
            failures++;
            return;
        }
        fputs(text, f);
        fclose(f);
    }
    ico_config_reset(s_path, "/nonexistent/difficulty_test.ini");
    ico_opt_reload();
    ico_difficulty_reset();
}

/* the listener's events */
static int s_ev[4], s_ev_a, s_ev_b;

static void listener(IcoDiffEvent ev, int a, int b)
{
    if (ev >= 0 && ev < 4) {
        s_ev[ev]++;
    }
    s_ev_a = a;
    s_ev_b = b;
}

static void clear_events(void)
{
    memset(s_ev, 0, sizeof(s_ev));
    s_ev_a = s_ev_b = -1;
}

static int hard_toughness(void)
{
    return on("jun26") ? ICO_TOUGHNESS_ORIGINAL
           : y2001()   ? ICO_TOUGHNESS_AMERICAN
                       : ICO_TOUGHNESS_THIRD_JOURNEY;
}

static void test_presets(void)
{
    IcoDifficulty e, n, h, c;

    ico_difficulty_preset(ICO_DIFF_EASY, &e);
    ico_difficulty_preset(ICO_DIFF_NORMAL, &n);
    ico_difficulty_preset(ICO_DIFF_HARD, &h);
    CHECK(sizeof(IcoDifficulty) == 256);
    CHECK(n.version == ICO_DIFF_VERSION && n.mode == ICO_DIFF_NORMAL &&
          n.origin == ICO_DIFF_ORIGIN_NONE && n.journey[0] == 0 && n.journey[1] == 0);
    /* Normal: the game as the disc ships it */
    CHECK(n.row[ICO_DIFF_SHADOW_SPEED] == ICO_SPEED_ORIGINAL);
    CHECK(n.row[ICO_DIFF_SHADE_TOUGHNESS] == ICO_TOUGHNESS_ORIGINAL);
    CHECK(n.row[ICO_DIFF_AGGRESSION] == ICO_AGGRESSION_OWN);
    CHECK(n.row[ICO_DIFF_AUTO_GUARD] == 1 && n.row[ICO_DIFF_ATTACK_TURN] == 1 &&
          n.row[ICO_DIFF_SHADOWS_WAIT_DOWN] == 1);
    CHECK(n.row[ICO_DIFF_BARRIER_HITS] == 0 && n.row[ICO_DIFF_PIT_TIME] == ICO_PIT_ORIGINAL);
    {
        static const IcoDiffRow kOff[] = {
            ICO_DIFF_PEACEFUL,    ICO_DIFF_YORDA_SAFE, ICO_DIFF_SHADOWS_TOGETHER,
            ICO_DIFF_NO_SHADOWS,  ICO_DIFF_ONE_HIT,    ICO_DIFF_NEVER_DODGE,
            ICO_DIFF_YORDA_TRIPS, ICO_DIFF_NO_HINTS,   ICO_DIFF_KNOCK_LIMIT,
            ICO_DIFF_KNOCK_SCOPE, ICO_DIFF_PERMADEATH};
        for (size_t i = 0; i < sizeof(kOff) / sizeof(kOff[0]); i++) {
            CHECK(n.row[kOff[i]] == 0);
            /* no preset sets One hit, Peaceful, No Shadows, Yorda trips, No
               hints, Permadeath */
            if (kOff[i] != ICO_DIFF_YORDA_SAFE && kOff[i] != ICO_DIFF_NEVER_DODGE &&
                kOff[i] != ICO_DIFF_SHADOWS_TOGETHER && kOff[i] != ICO_DIFF_KNOCK_LIMIT) {
                CHECK(e.row[kOff[i]] == 0 && h.row[kOff[i]] == 0);
            }
        }
    }
    /* Easy */
    CHECK(e.mode == ICO_DIFF_EASY);
    CHECK(e.row[ICO_DIFF_SHADOW_SPEED] == ICO_SPEED_SLOWER);
    CHECK(e.row[ICO_DIFF_SHADE_TOUGHNESS] == ICO_TOUGHNESS_NO_PLUS);
    CHECK(e.row[ICO_DIFF_AGGRESSION] == ICO_AGGRESSION_WARY_PATIENT);
    CHECK(e.row[ICO_DIFF_YORDA_SAFE] == 1 && e.row[ICO_DIFF_NEVER_DODGE] == 1);
    CHECK(e.row[ICO_DIFF_SHADOWS_TOGETHER] == 0);
    CHECK(e.row[ICO_DIFF_AUTO_GUARD] == 1 && e.row[ICO_DIFF_ATTACK_TURN] == 1 &&
          e.row[ICO_DIFF_SHADOWS_WAIT_DOWN] == 1);
    CHECK(e.row[ICO_DIFF_BARRIER_HITS] == 0 && e.row[ICO_DIFF_PIT_TIME] == 25);
    CHECK(e.row[ICO_DIFF_KNOCK_LIMIT] == 0);
    /* Hard (docs/port/HARD_MODE.md), the toughness per disc */
    CHECK(h.mode == ICO_DIFF_HARD);
    CHECK(h.row[ICO_DIFF_SHADOW_SPEED] == ICO_SPEED_FASTER);
    CHECK(h.row[ICO_DIFF_SHADE_TOUGHNESS] == hard_toughness());
    CHECK(h.row[ICO_DIFF_AGGRESSION] == ICO_AGGRESSION_BOLD_QUICK);
    CHECK(h.row[ICO_DIFF_YORDA_SAFE] == 0 && h.row[ICO_DIFF_NEVER_DODGE] == 0);
    CHECK(h.row[ICO_DIFF_SHADOWS_TOGETHER] == 1);
    CHECK(h.row[ICO_DIFF_AUTO_GUARD] == 0 && h.row[ICO_DIFF_ATTACK_TURN] == 0 &&
          h.row[ICO_DIFF_SHADOWS_WAIT_DOWN] == 0);
    CHECK(h.row[ICO_DIFF_BARRIER_HITS] == 7 && h.row[ICO_DIFF_PIT_TIME] == 8);
    CHECK(h.row[ICO_DIFF_KNOCK_LIMIT] == 3 && h.row[ICO_DIFF_KNOCK_SCOPE] == ICO_DIFF_SCOPE_ROOM);
    /* every row a value of its own; the spares 0 */
    for (int r = 0; r < ICO_DIFF_ROW_COUNT; r++) {
        CHECK(ico_difficulty_row_valid((IcoDiffRow)r, e.row[r]) &&
              ico_difficulty_row_valid((IcoDiffRow)r, n.row[r]) &&
              ico_difficulty_row_valid((IcoDiffRow)r, h.row[r]));
    }
    for (int r = ICO_DIFF_ROW_COUNT; r < ICO_DIFF_ROW_SLOTS; r++) {
        CHECK(e.row[r] == 0 && n.row[r] == 0 && h.row[r] == 0);
    }
    /* classified as themselves; Custom gives Normal's rows */
    CHECK(ico_difficulty_classify(&e) == ICO_DIFF_EASY);
    CHECK(ico_difficulty_classify(&n) == ICO_DIFF_NORMAL);
    CHECK(ico_difficulty_classify(&h) == ICO_DIFF_HARD);
    ico_difficulty_preset(ICO_DIFF_CUSTOM, &c);
    CHECK(c.mode == ICO_DIFF_CUSTOM && ico_difficulty_classify(&c) == ICO_DIFF_NORMAL);
    CHECK(ico_difficulty_classify(NULL) == ICO_DIFF_CUSTOM);
    /* one change of any row: Custom (the barrier and the seconds excepted when
       the value is the disc's own) */
    for (int r = 0; r < ICO_DIFF_ROW_COUNT; r++) {
        IcoDifficulty x = h;
        ico_difficulty_row_step(&x, (IcoDiffRow)r, 1);
        if (r == ICO_DIFF_BARRIER_HITS && on("jun26")) {
            continue; /* 7 -> the disc's own (7): the same barrier */
        }
        if (x.row[r] != h.row[r]) {
            CHECK(ico_difficulty_classify(&x) == ICO_DIFF_CUSTOM);
        }
    }
    /* the disc's own barrier and seconds named explicitly: still Normal */
    c = n;
    c.row[ICO_DIFF_BARRIER_HITS] = on("jun26") ? 7 : 5;
    c.row[ICO_DIFF_PIT_TIME] = ico_opt_pit_base_seconds();
    CHECK(ico_difficulty_classify(&c) == ICO_DIFF_NORMAL);
    c.row[ICO_DIFF_BARRIER_HITS] = on("jun26") ? 5 : 7;
    CHECK(ico_difficulty_classify(&c) == ICO_DIFF_CUSTOM);
    /* the names */
    CHECK(strcmp(ico_difficulty_mode_name(ICO_DIFF_HARD), "hard") == 0 &&
          strcmp(ico_difficulty_mode_name(ICO_DIFF_CUSTOM), "custom") == 0 &&
          strcmp(ico_difficulty_mode_name(ICO_DIFF_MODE_COUNT), "") == 0);
    CHECK(ico_difficulty_mode_parse("easy") == ICO_DIFF_EASY &&
          ico_difficulty_mode_parse("normal") == ICO_DIFF_NORMAL &&
          ico_difficulty_mode_parse("x") == -1 && ico_difficulty_mode_parse(NULL) == -1);
    CHECK(strcmp(ico_difficulty_origin_name(ICO_DIFF_ORIGIN_CONFIG), "config") == 0 &&
          ico_difficulty_origin_parse("state") == ICO_DIFF_ORIGIN_STATE &&
          ico_difficulty_origin_parse("new") == ICO_DIFF_ORIGIN_NEW &&
          ico_difficulty_origin_parse("?") == -1);
    CHECK(strcmp(ico_difficulty_row_key(ICO_DIFF_SHADOW_SPEED), "shadow_speed") == 0 &&
          strcmp(ico_difficulty_row_key(ICO_DIFF_PERMADEATH), "permadeath") == 0 &&
          strcmp(ico_difficulty_row_key(ICO_DIFF_ROW_COUNT), "") == 0);
}

static void test_steps(void)
{
    IcoDifficulty d;
    int offered = 0;

    ico_difficulty_preset(ICO_DIFF_NORMAL, &d);
    /* the toughness: the offered values only, around both ways */
    for (int v = 0; v < ICO_TOUGHNESS_COUNT; v++) {
        offered += ico_difficulty_row_valid(ICO_DIFF_SHADE_TOUGHNESS, v);
        CHECK(ico_difficulty_row_valid(ICO_DIFF_SHADE_TOUGHNESS, v) ==
              ico_variant_offered(ICO_VARIANT_SHADE_TOUGHNESS, v));
    }
    for (int k = 0; k < offered; k++) {
        CHECK(ico_variant_offered(ICO_VARIANT_SHADE_TOUGHNESS,
                                  ico_difficulty_row_step(&d, ICO_DIFF_SHADE_TOUGHNESS, 1)));
    }
    CHECK(d.row[ICO_DIFF_SHADE_TOUGHNESS] == ICO_TOUGHNESS_ORIGINAL);
    CHECK(ico_difficulty_row_step(&d, ICO_DIFF_SHADE_TOUGHNESS, -1) ==
          (on("jun26") || y2001() ? ICO_TOUGHNESS_NO_PLUS : ICO_TOUGHNESS_THIRD_JOURNEY));
    /* the barrier: disc's, 5, 7, disc's */
    CHECK(ico_difficulty_row_step(&d, ICO_DIFF_BARRIER_HITS, 1) == 5);
    CHECK(ico_difficulty_row_step(&d, ICO_DIFF_BARRIER_HITS, 1) == 7);
    CHECK(ico_difficulty_row_step(&d, ICO_DIFF_BARRIER_HITS, 1) == 0);
    CHECK(ico_difficulty_row_step(&d, ICO_DIFF_BARRIER_HITS, -1) == 7);
    /* the seconds: from Original (the disc's 15) down to 4 and held there, up
       to 30 and held, the disc's own seconds Original again */
    d.row[ICO_DIFF_PIT_TIME] = ICO_PIT_ORIGINAL;
    CHECK(ico_difficulty_row_step(&d, ICO_DIFF_PIT_TIME, -1) == 14);
    for (int k = 0; k < 20; k++) {
        ico_difficulty_row_step(&d, ICO_DIFF_PIT_TIME, -1);
    }
    CHECK(d.row[ICO_DIFF_PIT_TIME] == ICO_PIT_MIN_SECONDS);
    for (int k = 4; k < 14; k++) {
        ico_difficulty_row_step(&d, ICO_DIFF_PIT_TIME, 1);
    }
    CHECK(d.row[ICO_DIFF_PIT_TIME] == 14);
    CHECK(ico_difficulty_row_step(&d, ICO_DIFF_PIT_TIME, 1) == ICO_PIT_ORIGINAL);
    CHECK(ico_difficulty_row_step(&d, ICO_DIFF_PIT_TIME, 1) == 16);
    for (int k = 0; k < 40; k++) {
        ico_difficulty_row_step(&d, ICO_DIFF_PIT_TIME, 1);
    }
    CHECK(d.row[ICO_DIFF_PIT_TIME] == ICO_PIT_MAX_SECONDS);
    /* the limit: Off, 1..9, Off */
    for (int k = 1; k <= 9; k++) {
        CHECK(ico_difficulty_row_step(&d, ICO_DIFF_KNOCK_LIMIT, 1) == k);
    }
    CHECK(ico_difficulty_row_step(&d, ICO_DIFF_KNOCK_LIMIT, 1) == 0);
    CHECK(ico_difficulty_row_step(&d, ICO_DIFF_KNOCK_LIMIT, -1) == 9);
    /* the switches toggle; the scope is one */
    CHECK(ico_difficulty_row_step(&d, ICO_DIFF_PERMADEATH, 1) == 1 &&
          ico_difficulty_row_step(&d, ICO_DIFF_PERMADEATH, -1) == 0);
    CHECK(ico_difficulty_row_step(&d, ICO_DIFF_KNOCK_SCOPE, 1) == ICO_DIFF_SCOPE_JOURNEY &&
          ico_difficulty_row_step(&d, ICO_DIFF_KNOCK_SCOPE, 1) == ICO_DIFF_SCOPE_ROOM);
    /* the speed and aggression wrap */
    d.row[ICO_DIFF_SHADOW_SPEED] = ICO_SPEED_FASTEST;
    CHECK(ico_difficulty_row_step(&d, ICO_DIFF_SHADOW_SPEED, 1) == ICO_SPEED_SLOWER);
    d.row[ICO_DIFF_AGGRESSION] = ICO_AGGRESSION_OWN;
    CHECK(ico_difficulty_row_step(&d, ICO_DIFF_AGGRESSION, -1) == ICO_AGGRESSION_WARY_QUICK);
    /* out of range: nothing */
    CHECK(ico_difficulty_row_step(&d, ICO_DIFF_ROW_COUNT, 1) == 0);
    CHECK(ico_difficulty_row_step(NULL, ICO_DIFF_PIT_TIME, 1) == 0);
    /* the values a row may hold */
    CHECK(!ico_difficulty_row_valid(ICO_DIFF_BARRIER_HITS, 6));
    CHECK(!ico_difficulty_row_valid(ICO_DIFF_PIT_TIME, 3) &&
          !ico_difficulty_row_valid(ICO_DIFF_PIT_TIME, 31));
    CHECK(!ico_difficulty_row_valid(ICO_DIFF_KNOCK_LIMIT, 10) &&
          !ico_difficulty_row_valid(ICO_DIFF_KNOCK_LIMIT, -1));
    CHECK(!ico_difficulty_row_valid(ICO_DIFF_PERMADEATH, 2));
    CHECK(!ico_difficulty_row_valid(ICO_DIFF_ROW_COUNT, 0));
}

static void test_text(void)
{
    char text[ICO_DIFF_ROWS_TEXT_MAX];
    IcoDifficulty d, back;
    int len;

    for (int m = 0; m < ICO_DIFF_MODE_COUNT; m++) {
        ico_difficulty_preset((IcoDiffMode)m, &d);
        len = ico_difficulty_rows_format(&d, text, sizeof(text));
        CHECK(len > 0 && len < (int)sizeof(text) && (int)strlen(text) == len);
        memset(&back, 0x55, sizeof(back));
        CHECK(ico_difficulty_rows_parse(text, &back) == 0);
        CHECK(memcmp(back.row, d.row, sizeof(d.row)) == 0);
    }
    /* Hard's text as the sidecar keeps it */
    ico_difficulty_preset(ICO_DIFF_HARD, &d);
    ico_difficulty_rows_format(&d, text, sizeof(text));
    CHECK(strncmp(text, "shadow_speed=faster shade_toughness=", 36) == 0);
    CHECK(strstr(text, " aggression=bold_quick ") != NULL);
    CHECK(strstr(text, " barrier_hits=7 pit_time=8 ") != NULL);
    CHECK(strstr(text, " auto_guard=false attack_turn=false shadows_wait_down=false ") != NULL);
    CHECK(strstr(text, " knock_limit=3 knock_scope=room permadeath=false") != NULL);
    ico_difficulty_preset(ICO_DIFF_NORMAL, &d);
    ico_difficulty_rows_format(&d, text, sizeof(text));
    CHECK(strstr(text, " barrier_hits=disc pit_time=original ") != NULL);
    /* a custom record of every row moved */
    ico_difficulty_preset(ICO_DIFF_NORMAL, &d);
    for (int r = 0; r < ICO_DIFF_ROW_COUNT; r++) {
        ico_difficulty_row_step(&d, (IcoDiffRow)r, r % 2 ? 1 : -1);
    }
    ico_difficulty_rows_format(&d, text, sizeof(text));
    CHECK(ico_difficulty_rows_parse(text, &back) == 0);
    CHECK(memcmp(back.row, d.row, sizeof(d.row)) == 0);
    /* a short buffer: cut, NUL-terminated, the whole length returned */
    {
        char shortbuf[12];
        CHECK(ico_difficulty_rows_format(&d, shortbuf, sizeof(shortbuf)) == (int)strlen(text));
        CHECK(strlen(shortbuf) == sizeof(shortbuf) - 1);
    }
    /* left out: Normal's; the other fields untouched; bad words counted */
    memset(&back, 0, sizeof(back));
    back.knocks_journey = 4;
    CHECK(ico_difficulty_rows_parse("knock_limit=2  permadeath=true\nshadow_speed=fastest",
                                    &back) == 0);
    CHECK(back.row[ICO_DIFF_KNOCK_LIMIT] == 2 && back.row[ICO_DIFF_PERMADEATH] == 1 &&
          back.row[ICO_DIFF_SHADOW_SPEED] == ICO_SPEED_FASTEST &&
          back.row[ICO_DIFF_AUTO_GUARD] == 1 && back.knocks_journey == 4);
    CHECK(ico_difficulty_rows_parse("knock_limit=12 colour=red barrier_hits=6 permadeath "
                                    "pit_time=soon one_hit=true",
                                    &back) == 5);
    CHECK(back.row[ICO_DIFF_KNOCK_LIMIT] == 0 && back.row[ICO_DIFF_BARRIER_HITS] == 0 &&
          back.row[ICO_DIFF_ONE_HIT] == 1);
    /* the earlier versions' pit_time words and a number held to the range */
    CHECK(ico_difficulty_rows_parse("pit_time=double", &back) == 0 &&
          back.row[ICO_DIFF_PIT_TIME] == 30);
    CHECK(ico_difficulty_rows_parse("pit_time=45", &back) == 0 &&
          back.row[ICO_DIFF_PIT_TIME] == 30);
    CHECK(ico_difficulty_rows_parse("pit_time=15", &back) == 0 &&
          back.row[ICO_DIFF_PIT_TIME] == ICO_PIT_ORIGINAL);
    /* a value the disc does not offer is no value here */
    CHECK(ico_difficulty_rows_parse("shade_toughness=american", &back) == (on("jun26") ? 1 : 0));
    CHECK(ico_difficulty_rows_parse("", &back) == 0 && ico_difficulty_rows_parse(NULL, &back) == 0);
    CHECK(ico_difficulty_rows_parse("x", NULL) == 0);
}

static void test_carry_over(void)
{
    IcoDifficulty d, d2;
    char keys[1024];

    /* every retired key at a value of its own */
    use_config("[gameplay]\nyorda_safe = true\npit_time = \"double\"\nshadow_speed = \"fastest\"\n"
               "yorda_trips = true\nshadows_together = true\n"
               "[variants]\nshade_toughness = \"american\"\none_hit = true\nnever_dodge = true\n"
               "aggression = \"wary_quick\"\nno_hints = true\nbarrier_hits = 7\n"
               "auto_guard = false\nattack_turn = false\nshadows_wait_down = false\n"
               "peaceful_shadows = true\n");
    ico_difficulty_from_config(&d);
    CHECK(d.version == ICO_DIFF_VERSION && d.origin == ICO_DIFF_ORIGIN_CONFIG &&
          d.mode == ICO_DIFF_CUSTOM);
    CHECK(d.journey[0] != 0 || d.journey[1] != 0);
    CHECK(d.knocks_room == 0 && d.knocks_journey == 0 && d.finished == 0);
    CHECK(d.row[ICO_DIFF_YORDA_SAFE] == 1 && d.row[ICO_DIFF_PIT_TIME] == 30 &&
          d.row[ICO_DIFF_SHADOW_SPEED] == ICO_SPEED_FASTEST && d.row[ICO_DIFF_YORDA_TRIPS] == 1 &&
          d.row[ICO_DIFF_SHADOWS_TOGETHER] == 1);
    /* American is not offered on the June 2001 demo: Original there */
    CHECK(d.row[ICO_DIFF_SHADE_TOUGHNESS] ==
          (on("jun26") ? ICO_TOUGHNESS_ORIGINAL : ICO_TOUGHNESS_AMERICAN));
    CHECK(d.row[ICO_DIFF_ONE_HIT] == 1 && d.row[ICO_DIFF_NEVER_DODGE] == 1 &&
          d.row[ICO_DIFF_AGGRESSION] == ICO_AGGRESSION_WARY_QUICK &&
          d.row[ICO_DIFF_NO_HINTS] == 1 && d.row[ICO_DIFF_BARRIER_HITS] == 7);
    CHECK(d.row[ICO_DIFF_AUTO_GUARD] == 0 && d.row[ICO_DIFF_ATTACK_TURN] == 0 &&
          d.row[ICO_DIFF_SHADOWS_WAIT_DOWN] == 0 && d.row[ICO_DIFF_PEACEFUL] == 1);
    /* the new rows at Normal: no config key ever named them */
    CHECK(d.row[ICO_DIFF_NO_SHADOWS] == 0 && d.row[ICO_DIFF_KNOCK_LIMIT] == 0 &&
          d.row[ICO_DIFF_PERMADEATH] == 0);
    /* two carry-overs: two journeys */
    ico_difficulty_from_config(&d2);
    CHECK(memcmp(d2.row, d.row, sizeof(d.row)) == 0);
    CHECK(d2.journey[0] != d.journey[0] || d2.journey[1] != d.journey[1]);
    /* an integer as the Settings menu wrote it; words that are no value read
       as the default, as the old readers did */
    use_config("[gameplay]\npit_time = 8\nshadow_speed = \"sideways\"\n"
               "[variants]\nbarrier_hits = 6\naggression = \"furious\"\nauto_guard = \"maybe\"\n");
    ico_difficulty_from_config(&d);
    CHECK(d.row[ICO_DIFF_PIT_TIME] == 8 && d.row[ICO_DIFF_SHADOW_SPEED] == ICO_SPEED_ORIGINAL &&
          d.row[ICO_DIFF_BARRIER_HITS] == 0 && d.row[ICO_DIFF_AGGRESSION] == ICO_AGGRESSION_OWN &&
          d.row[ICO_DIFF_AUTO_GUARD] == 1);
    CHECK(d.mode == ICO_DIFF_CUSTOM);
    /* every key at the game's own (or absent): Normal, even though a carry-over
       of Easy's exact rows would classify as Easy */
    use_config("[gameplay]\npit_time = \"original\"\nshadow_speed = \"original\"\n"
               "[variants]\nauto_guard = true\none_hit = false\n");
    ico_difficulty_from_config(&d);
    CHECK(d.mode == ICO_DIFF_NORMAL && d.origin == ICO_DIFF_ORIGIN_CONFIG);
    use_config("[gameplay]\nyorda_safe = true\npit_time = 25\nshadow_speed = \"slower\"\n"
               "[variants]\nshade_toughness = \"no_plus\"\naggression = \"wary_patient\"\n"
               "never_dodge = true\n");
    ico_difficulty_from_config(&d);
    CHECK(ico_difficulty_classify(&d) == ICO_DIFF_EASY && d.mode == ICO_DIFF_CUSTOM);
    use_config(NULL);
    ico_difficulty_from_config(&d);
    CHECK(d.mode == ICO_DIFF_NORMAL);

    /* an old state's keys text: never config (config says otherwise here) */
    use_config("[gameplay]\nyorda_safe = true\n");
    snprintf(keys, sizeof(keys),
             "video.video_mode=\ngameplay.mirror=false\ngameplay.yorda_safe=\n"
             "gameplay.pit_time=8\ngameplay.shadow_speed=faster\nvariants.aggression=bold_quick\n"
             "variants.barrier_hits=\nvariants.auto_guard=false\nvariants.one_hit=true\n");
    ico_difficulty_from_keys(keys, &d);
    CHECK(d.origin == ICO_DIFF_ORIGIN_STATE && d.mode == ICO_DIFF_CUSTOM);
    CHECK(d.row[ICO_DIFF_YORDA_SAFE] == 0 && d.row[ICO_DIFF_PIT_TIME] == 8 &&
          d.row[ICO_DIFF_SHADOW_SPEED] == ICO_SPEED_FASTER &&
          d.row[ICO_DIFF_AGGRESSION] == ICO_AGGRESSION_BOLD_QUICK &&
          d.row[ICO_DIFF_BARRIER_HITS] == 0 && d.row[ICO_DIFF_AUTO_GUARD] == 0 &&
          d.row[ICO_DIFF_ONE_HIT] == 1);
    CHECK(d.journey[0] != 0 || d.journey[1] != 0);
    /* the same text: the same journey; another: another */
    ico_difficulty_from_keys(keys, &d2);
    CHECK(d2.journey[0] == d.journey[0] && d2.journey[1] == d.journey[1]);
    ico_difficulty_from_keys("gameplay.pit_time=9\n", &d2);
    CHECK(d2.journey[0] != d.journey[0] || d2.journey[1] != d.journey[1]);
    /* no keys (or none of the fifteen): Normal */
    ico_difficulty_from_keys(NULL, &d);
    CHECK(d.mode == ICO_DIFF_NORMAL && d.row[ICO_DIFF_AUTO_GUARD] == 1);
    ico_difficulty_from_keys("gameplay.mirror=true\n", &d);
    CHECK(d.mode == ICO_DIFF_NORMAL);
    use_config(NULL);
}

static void test_lifecycle(void)
{
    IcoDifficulty d, saved;
    uint32_t j[2];
    char id[20];

    use_config(NULL);
    ico_difficulty_set_listener(listener);
    clear_events();
    /* the title: Normal, no journey; the accessors read the game's own */
    CHECK(ico_difficulty_active()->mode == ICO_DIFF_NORMAL && !ico_difficulty_has_journey());
    CHECK(ico_opt_shadow_speed() == ICO_SPEED_ORIGINAL && ico_opt_pit_time() == ICO_PIT_ORIGINAL &&
          ico_opt_yorda_safe() == 0 && ico_opt_shadows_together() == 0 &&
          ico_opt_yorda_trips() == 0);
    CHECK(ico_variant_auto_guard() == 1 && ico_variant_attack_turn() == 1 &&
          ico_variant_shadows_wait_down() == 1 && ico_variant_one_hit() == 0 &&
          ico_variant_shade_toughness() == ICO_TOUGHNESS_ORIGINAL);
    CHECK(ico_variant_barrier_hits() == (on("jun26") ? 7 : 5));
    ico_difficulty_journey_text(ico_difficulty_active(), id, sizeof(id));
    CHECK(strcmp(id, "0000000000000000") == 0);
    /* a New Game on Hard: a journey, its rows in the accessors */
    ico_difficulty_preset(ICO_DIFF_HARD, &d);
    d.knocks_room = 5; /* the draft's counts are not the journey's */
    ico_difficulty_begin(&d);
    CHECK(s_ev[ICO_DIFF_EV_ACTIVE] == 1);
    CHECK(ico_difficulty_active()->mode == ICO_DIFF_HARD &&
          ico_difficulty_active()->origin == ICO_DIFF_ORIGIN_NEW && ico_difficulty_has_journey());
    CHECK(ico_difficulty_active()->knocks_room == 0 && ico_difficulty_active()->run_has_slot == 0);
    CHECK(ico_opt_shadow_speed() == ICO_SPEED_FASTER && ico_opt_pit_time() == 8 &&
          ico_opt_shadows_together() == 1 && ico_opt_yorda_safe() == 0);
    CHECK(ico_opt_shadow_waits(1) == 0);
    CHECK(ico_variant_auto_guard() == 0 && ico_variant_attack_turn() == 0 &&
          ico_variant_shadows_wait_down() == 0 && ico_variant_down_keeps_hit(1) == 0);
    CHECK(ico_variant_shade_toughness() == hard_toughness() &&
          ico_variant_aggression() == ICO_AGGRESSION_BOLD_QUICK && ico_variant_barrier_hits() == 7);
    CHECK(ico_difficulty_row(ICO_DIFF_KNOCK_LIMIT) == 3 &&
          ico_difficulty_row(ICO_DIFF_ROW_COUNT) == 0);
    /* no difficulty row switches achievements off */
    CHECK(ico_variants_suspend_achievements() == 0);
    j[0] = ico_difficulty_active()->journey[0];
    j[1] = ico_difficulty_active()->journey[1];
    ico_difficulty_journey_text(ico_difficulty_active(), id, sizeof(id));
    CHECK(strlen(id) == 16);
    /* a second New Game: another journey */
    ico_difficulty_begin(&d);
    CHECK(ico_difficulty_active()->journey[0] != j[0] ||
          ico_difficulty_active()->journey[1] != j[1]);
    /* Easy on the accessors */
    ico_difficulty_preset(ICO_DIFF_EASY, &d);
    ico_difficulty_begin(&d);
    CHECK(ico_opt_shadow_speed() == ICO_SPEED_SLOWER && ico_opt_pit_time() == 25 &&
          ico_opt_yorda_safe() == 1 && ico_variant_never_dodge() == 1 &&
          ico_variant_shade_toughness() == ICO_TOUGHNESS_NO_PLUS &&
          ico_variant_aggression() == ICO_AGGRESSION_WARY_PATIENT);
    CHECK(ico_variants_suspend_achievements() == 0);
    /* a custom draft with One hit, Peaceful: achievements stay on */
    d.row[ICO_DIFF_ONE_HIT] = 1;
    d.row[ICO_DIFF_PEACEFUL] = 1;
    ico_difficulty_begin(&d);
    CHECK(ico_difficulty_active()->mode == ICO_DIFF_CUSTOM);
    CHECK(ico_variant_one_hit() == 1 && ico_variant_shadow_brain() == 0);
    CHECK(ico_variants_suspend_achievements() == 0);
    /* a draft value the disc does not offer is made Original */
    d.row[ICO_DIFF_SHADE_TOUGHNESS] = ICO_TOUGHNESS_THIRD_JOURNEY;
    d.row[ICO_DIFF_KNOCK_LIMIT] = 40;
    ico_difficulty_begin(&d);
    CHECK(ico_difficulty_row(ICO_DIFF_SHADE_TOUGHNESS) ==
          (y2001() ? ICO_TOUGHNESS_ORIGINAL : ICO_TOUGHNESS_THIRD_JOURNEY));
    CHECK(ico_difficulty_row(ICO_DIFF_KNOCK_LIMIT) == 0);
    /* NULL: Normal */
    ico_difficulty_begin(NULL);
    CHECK(ico_difficulty_active()->mode == ICO_DIFF_NORMAL && ico_difficulty_has_journey());
    /* the title through the options (the six title twins call it) */
    clear_events();
    ico_opt_mirror_reset();
    CHECK(!ico_difficulty_has_journey() && ico_difficulty_active()->mode == ICO_DIFF_NORMAL);
    CHECK(s_ev[ICO_DIFF_EV_ACTIVE] == 1);
    ico_difficulty_reset(); /* again: no change, nothing told */
    CHECK(s_ev[ICO_DIFF_EV_ACTIVE] == 1);
    /* a config reload keeps the journey's rows */
    ico_difficulty_preset(ICO_DIFF_HARD, &d);
    ico_difficulty_begin(&d);
    ico_opt_reload();
    CHECK(ico_opt_shadow_speed() == ICO_SPEED_FASTER && ico_variant_auto_guard() == 0);
    /* set_active (a save's record) and the run's slot */
    saved = *ico_difficulty_active();
    ico_difficulty_reset();
    ico_difficulty_set_active(&saved);
    CHECK(memcmp(ico_difficulty_active(), &saved, sizeof(saved)) == 0);
    ico_difficulty_set_run(1, 3, 0xABCDu);
    CHECK(ico_difficulty_active()->run_port == 1 && ico_difficulty_active()->run_slot == 3 &&
          ico_difficulty_active()->run_has_slot == 1 &&
          ico_difficulty_active()->run_sum == 0xABCDu);
    saved.row[ICO_DIFF_PIT_TIME] = 99; /* a value it cannot hold: Normal's */
    ico_difficulty_set_active(&saved);
    CHECK(ico_difficulty_row(ICO_DIFF_PIT_TIME) == ICO_PIT_ORIGINAL);
    ico_difficulty_set_active(NULL);
    /* set_row (the interim Settings rows): the mode follows */
    ico_difficulty_preset(ICO_DIFF_HARD, &d);
    ico_difficulty_begin(&d);
    ico_opt_set_shadow_speed(ICO_SPEED_FASTEST);
    CHECK(ico_opt_shadow_speed() == ICO_SPEED_FASTEST &&
          ico_difficulty_active()->mode == ICO_DIFF_CUSTOM);
    ico_opt_set_shadow_speed(ICO_SPEED_FASTER);
    CHECK(ico_difficulty_active()->mode == ICO_DIFF_HARD);
    ico_difficulty_set_row(ICO_DIFF_PIT_TIME, 3); /* no value: unchanged */
    CHECK(ico_opt_pit_time() == 8);
    ico_opt_set_pit_time(99);
    CHECK(ico_opt_pit_time() == ICO_PIT_MAX_SECONDS);
    ico_opt_set_yorda_safe(1);
    ico_opt_set_yorda_trips(1);
    ico_opt_set_shadows_together(0);
    CHECK(ico_difficulty_row(ICO_DIFF_YORDA_SAFE) == 1 &&
          ico_difficulty_row(ICO_DIFF_YORDA_TRIPS) == 1 &&
          ico_difficulty_row(ICO_DIFF_SHADOWS_TOGETHER) == 0);
    /* a moved variant's step edits the record and writes no config key; a
       global one's still writes its key */
    use_config(NULL);
    CHECK(ico_variant_step(ICO_VARIANT_ONE_HIT, 1) == 1 &&
          ico_difficulty_row(ICO_DIFF_ONE_HIT) == 1);
    CHECK(ico_config_get_string("variants.one_hit", NULL) == NULL);
    CHECK(ico_variant_step(ICO_VARIANT_BARRIER_HITS, 1) == (on("jun26") ? 0 : 1));
    CHECK(ico_difficulty_row(ICO_DIFF_BARRIER_HITS) == (on("jun26") ? 5 : 7));
    CHECK(ico_config_get_string("variants.barrier_hits", NULL) == NULL);
    CHECK(ico_variant_step(ICO_VARIANT_WEAPON, 1) == ICO_WEAPON_NET);
    CHECK(ico_config_get_string("variants.weapon", NULL) != NULL);
    ico_variant_set(ICO_VARIANT_WEAPON, ICO_WEAPON_ORIGINAL);
    ico_difficulty_set_listener(NULL);
    use_config(NULL);
}

/* a draft of Normal with the limit and scope given */
static void begin_limit(int limit, int scope, int permadeath)
{
    IcoDifficulty d;

    ico_difficulty_preset(ICO_DIFF_NORMAL, &d);
    d.row[ICO_DIFF_KNOCK_LIMIT] = limit;
    d.row[ICO_DIFF_KNOCK_SCOPE] = scope;
    d.row[ICO_DIFF_PERMADEATH] = permadeath;
    ico_difficulty_begin(&d);
}

/* Ico gets up: the down act left, the get-up timer run out */
static void get_up(void)
{
    ico_difficulty_tick(1, 0, 10);
    ico_difficulty_tick(1, 0, 0);
}

static void test_knockdown(void)
{
    const IcoDifficulty *a = ico_difficulty_active();

    use_config(NULL);
    ico_difficulty_set_listener(listener);
    /* the limit off (Normal, Easy): nothing counted, never a game over */
    ico_difficulty_begin(NULL);
    for (int i = 0; i < 20; i++) {
        CHECK(ico_difficulty_knockdown(1, 0, 0) == 0);
        get_up();
    }
    CHECK(a->knocks_room == 0 && a->knocks_journey == 0 && a->in_fall == 0);

    /* each room, 3 (Hard's) */
    begin_limit(3, ICO_DIFF_SCOPE_ROOM, 0);
    clear_events();
    CHECK(ico_difficulty_knockdown(1, 0, 0) == 0);
    CHECK(a->knocks_room == 1 && a->in_fall == 1);
    CHECK(s_ev[ICO_DIFF_EV_KNOCK] == 1 && s_ev_a == 1 && s_ev_b == 3);
    /* a chained hit while he lies: the same fall */
    CHECK(ico_difficulty_knockdown(1, 0, 0) == 0 && a->knocks_room == 1);
    /* a fall longer than the get-up timer: the timer is out but he is still
       in the down act: the latch holds */
    ico_difficulty_tick(1, ICO_DIFF_DOWN_ACT_MODE, 0);
    CHECK(a->in_fall == 1);
    /* up, the timer running: not yet back on his feet */
    ico_difficulty_tick(1, 0, 12);
    CHECK(a->in_fall == 1);
    CHECK(ico_difficulty_knockdown(1, 0, 0) == 0 && a->knocks_room == 1);
    /* an invalid sample changes nothing */
    ico_difficulty_tick(0, 0, 0);
    CHECK(a->in_fall == 1);
    ico_difficulty_tick(1, 0, 0);
    CHECK(a->in_fall == 0);
    /* a landing (mails 55/56) is the fall going on; another actor never counts;
       a knock-down while the get-up timer still runs is the same fall */
    CHECK(ico_difficulty_knockdown(1, 1, 0) == 0 && a->knocks_room == 1 && a->in_fall == 0);
    CHECK(ico_difficulty_knockdown(0, 0, 0) == 0 && a->knocks_room == 1);
    CHECK(ico_difficulty_knockdown(1, 0, 7) == 0 && a->knocks_room == 1);
    /* the second */
    CHECK(ico_difficulty_knockdown(1, 0, 0) == 0 && a->knocks_room == 2);
    get_up();
    /* the third: the limit */
    clear_events();
    CHECK(ico_difficulty_knockdown(1, 0, 0) == 1 && a->knocks_room == 3 && a->over_pending == 1);
    CHECK(s_ev[ICO_DIFF_EV_KNOCK] == 1 && s_ev[ICO_DIFF_EV_LIMIT] == 1 && s_ev_a == 3);
    /* another mail replaced the act before the game over: 1 again */
    CHECK(ico_difficulty_knockdown(1, 0, 0) == 1);
    CHECK(ico_difficulty_knockdown(1, 1, 5) == 1);
    CHECK(ico_difficulty_knockdown(0, 0, 0) == 0);
    /* the game over: the pending state ends; the room starts again on the
       Continue's stage switch */
    ico_difficulty_game_over();
    CHECK(a->over_pending == 0 && a->in_fall == 0 && a->knocks_room == 0 && a->finished == 0);
    ico_difficulty_stage_enter(12);
    CHECK(ico_difficulty_knockdown(1, 0, 0) == 0 && a->knocks_room == 1);
    get_up();
    /* room entry starts the count again: two a room never reaches 3 */
    for (int room = 0; room < 5; room++) {
        ico_difficulty_stage_enter(room);
        CHECK(a->knocks_room == 0);
        CHECK(ico_difficulty_knockdown(1, 0, 0) == 0);
        get_up();
        CHECK(ico_difficulty_knockdown(1, 0, 0) == 0);
        get_up();
    }
    CHECK(a->knocks_room == 2 && a->knocks_journey == 14);

    /* the whole journey, 2: across rooms; the count back to 0 after the game
       over it causes, written through */
    begin_limit(2, ICO_DIFF_SCOPE_JOURNEY, 0);
    CHECK(ico_difficulty_knockdown(1, 0, 0) == 0);
    get_up();
    ico_difficulty_stage_enter(3);
    CHECK(a->knocks_room == 0 && a->knocks_journey == 1);
    /* another game over (not the limit's): the journey's count is kept */
    ico_difficulty_game_over();
    CHECK(a->knocks_journey == 1);
    ico_difficulty_stage_enter(3);
    CHECK(ico_difficulty_knockdown(1, 0, 0) == 1 && a->knocks_journey == 2);
    ico_difficulty_game_over();
    CHECK(a->knocks_journey == 0 && a->over_pending == 0);
    get_up();
    CHECK(ico_difficulty_knockdown(1, 0, 0) == 0 && a->knocks_journey == 1);
    get_up();

    /* a limit of 1: the first knock-down */
    begin_limit(1, ICO_DIFF_SCOPE_ROOM, 0);
    CHECK(ico_difficulty_knockdown(1, 0, 0) == 1);
    ico_difficulty_game_over();
    /* a new journey clears every count and flag */
    begin_limit(3, ICO_DIFF_SCOPE_ROOM, 0);
    CHECK(a->knocks_room == 0 && a->knocks_journey == 0 && a->in_fall == 0 &&
          a->over_pending == 0 && a->finished == 0);
    ico_difficulty_set_listener(NULL);
    use_config(NULL);
}

static void test_permadeath(void)
{
    const IcoDifficulty *a = ico_difficulty_active();

    use_config(NULL);
    ico_difficulty_set_listener(listener);
    /* off: a game over ends nothing; states load */
    begin_limit(0, 0, 0);
    CHECK(ico_difficulty_states_locked() == 0);
    ico_difficulty_game_over();
    CHECK(ico_difficulty_finished() == 0);
    /* on: states locked from the start; ANY game over ends the journey */
    begin_limit(0, 0, 1);
    CHECK(ico_difficulty_states_locked() == 1 && ico_difficulty_finished() == 0);
    clear_events();
    ico_difficulty_game_over();
    CHECK(ico_difficulty_finished() == 1 && a->finished == 1 && s_ev[ICO_DIFF_EV_ENDED] == 1);
    ico_difficulty_game_over(); /* told once */
    CHECK(s_ev[ICO_DIFF_EV_ENDED] == 1);
    /* the limit's game over on a Permadeath journey ends it too */
    begin_limit(1, ICO_DIFF_SCOPE_ROOM, 1);
    CHECK(ico_difficulty_finished() == 0);
    CHECK(ico_difficulty_knockdown(1, 0, 0) == 1);
    ico_difficulty_game_over();
    CHECK(ico_difficulty_finished() == 1);
    /* the title: no journey, nothing ended, states load */
    ico_difficulty_reset();
    CHECK(ico_difficulty_finished() == 0 && ico_difficulty_states_locked() == 0);
    /* Hard has no Permadeath */
    {
        IcoDifficulty d;
        ico_difficulty_preset(ICO_DIFF_HARD, &d);
        ico_difficulty_begin(&d);
        CHECK(ico_difficulty_states_locked() == 0);
    }
    ico_difficulty_set_listener(NULL);
    use_config(NULL);
}

static void test_no_shadows(void)
{
    IcoDifficulty d;

    use_config(NULL);
    CHECK(ico_no_shadows() == 0);
    for (int m = 0; m < 3; m++) {
        ico_difficulty_preset((IcoDiffMode)m, &d);
        CHECK(d.row[ICO_DIFF_NO_SHADOWS] == 0); /* no preset sets it */
    }
    d.row[ICO_DIFF_NO_SHADOWS] = 1;
    ico_difficulty_begin(&d);
    CHECK(ico_no_shadows() == 1 && ico_difficulty_active()->mode == ICO_DIFF_CUSTOM);
    ico_difficulty_reset();
    CHECK(ico_no_shadows() == 0);
}

static void test_state(void)
{
    const IcoStateEntry *e = NULL;
    IcoDifficulty d, bytes;

    use_config(NULL);
    ico_state_registry_reset();
    CHECK(ico_difficulty_state_register() == 0);
    CHECK(ico_difficulty_state_register() == -1); /* the name repeats */
    for (int i = 0; i < ico_state_registry_count(); i++) {
        if (strcmp(ico_state_registry_entry(i)->name, "difficulty.record") == 0) {
            e = ico_state_registry_entry(i);
        }
    }
    CHECK(e != NULL);
    if (e == NULL) {
        return;
    }
    CHECK(e->n == 256 && e->flags == ICO_STATE_FRESH &&
          e->p == (const void *)ico_difficulty_active());
    /* a save state of a Hard journey with counts, restored over the title's */
    begin_limit(3, ICO_DIFF_SCOPE_JOURNEY, 0);
    CHECK(ico_difficulty_knockdown(1, 0, 0) == 0);
    memcpy(&bytes, e->p, sizeof(bytes));
    ico_difficulty_reset();
    memcpy(e->p, &bytes, sizeof(bytes)); /* what state_host.c's restore does */
    ico_difficulty_state_loaded(0, "gameplay.pit_time=8\n");
    CHECK(memcmp(ico_difficulty_active(), &bytes, sizeof(bytes)) == 0);
    CHECK(ico_difficulty_active()->knocks_journey == 1 && ico_difficulty_active()->in_fall == 1);
    /* a damaged record (another version): rebuilt from the state's keys */
    bytes.version = 99;
    memcpy(e->p, &bytes, sizeof(bytes));
    ico_difficulty_state_loaded(0, "gameplay.pit_time=8\n");
    CHECK(ico_difficulty_active()->version == ICO_DIFF_VERSION &&
          ico_difficulty_active()->origin == ICO_DIFF_ORIGIN_STATE &&
          ico_difficulty_row(ICO_DIFF_PIT_TIME) == 8 &&
          ico_difficulty_row(ICO_DIFF_KNOCK_LIMIT) == 0);
    /* a row value it cannot hold in a restored record: Normal's */
    ico_difficulty_preset(ICO_DIFF_HARD, &d);
    d.row[ICO_DIFF_BARRIER_HITS] = 6;
    memcpy(e->p, &d, sizeof(d));
    ico_difficulty_state_loaded(0, NULL);
    CHECK(ico_difficulty_row(ICO_DIFF_BARRIER_HITS) == 0 &&
          ico_difficulty_row(ICO_DIFF_SHADOW_SPEED) == ICO_SPEED_FASTER);
    /* a state of v0.11.2 (no block: the registry kept this run's bytes): from
       its keys, never from config */
    use_config("[gameplay]\nshadow_speed = \"fastest\"\n");
    begin_limit(3, 0, 1);
    ico_difficulty_state_loaded(1, "gameplay.shadow_speed=slower\nvariants.one_hit=true\n");
    CHECK(ico_difficulty_row(ICO_DIFF_SHADOW_SPEED) == ICO_SPEED_SLOWER &&
          ico_difficulty_row(ICO_DIFF_ONE_HIT) == 1 &&
          ico_difficulty_row(ICO_DIFF_PERMADEATH) == 0 &&
          ico_difficulty_active()->origin == ICO_DIFF_ORIGIN_STATE);
    CHECK(ico_opt_shadow_speed() == ICO_SPEED_SLOWER && ico_variant_one_hit() == 1);
    ico_difficulty_state_loaded(1, NULL);
    CHECK(ico_difficulty_active()->mode == ICO_DIFF_NORMAL);
    ico_state_registry_reset();
    use_config(NULL);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";

    if (argc > 2 && ico_region_select(argv[2]) != 0) {
        fprintf(stderr, "difficulty_test: no region %s\n", argv[2]);
        return 1;
    }
    snprintf(s_path, sizeof(s_path), "%s/difficulty_test_%s.toml", dir, slug());
    use_config(NULL);
    test_presets();
    test_steps();
    test_text();
    test_carry_over();
    test_lifecycle();
    test_knockdown();
    test_permadeath();
    test_no_shadows();
    test_state();
    remove(s_path);
    if (failures != 0) {
        fprintf(stderr, "difficulty_test (%s): %d failure(s)\n", slug(), failures);
        return 1;
    }
    printf("difficulty_test (%s): ok\n", slug());
    return 0;
}
