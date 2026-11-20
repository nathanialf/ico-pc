/* rd_ledger_test.c: the scene ledger's tally, text and kept stages
 * (port/render/rd_ledger.h), CPU only, from synthetic scenes:
 *   - a model left out by each reason shows in the stage's text with that
 *     reason's words, its ticks or pictures (with those between ticks) and
 *     its count, and a tick counted once however many drops it had;
 *   - a model built and never drawn nor left out shows as "never drawn, no
 *     reason recorded"; one built in the stage before and never drawn there
 *     is carried in as "built before the stage began"; a drawn model is
 *     not carried;
 *   - a drawn row's text reads back (sscanf) as the counts put in;
 *   - the replay's half added into the game's (the same name one row);
 *   - the table full counts the names past it; a long name is cut;
 *   - the effects line; a stage with none says none;
 *   - the text's bounds (a short buffer, more rows than RD_LEDGER_ROWS:
 *     the count of rows not listed);
 *   - the kept stages: the last RD_LEDGER_KEEP, oldest first, then the
 *     stage in play;
 *   - the cost: a draw's tally (rd_ledger_note_draw on a known row) under
 *     40 ns on average over 4 million calls or within 6 times a control
 *     loop of the same three additions in the same run, whichever is
 *     looser (a busy runner slows both).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rd_ledger.h"

static int s_fails;

#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                   \
            s_fails++;                                                                             \
        }                                                                                          \
    } while (0)

static RdLedgerStage s_a, s_b;
static RdLedgerTable s_rep;
static char s_text[RD_LEDGER_TEXT];

static double nowNs(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

/* the row of name in the text (its "name:" after a ' ' or ';'), NULL none */
static const char *rowOf(const char *text, const char *name)
{
    char key[64];
    snprintf(key, sizeof key, " %s:", name);
    return strstr(text, key);
}

/* the row's text up to its end ("; " of the next row is not told apart
   from a reason's: the row ends at the next model's name or the line) */
static void rowText(const char *row, char *out, size_t cap)
{
    size_t n = 0;
    while (row[n] != '\0' && row[n] != '\n' && n + 1 < cap) {
        n++;
    }
    memcpy(out, row, n);
    out[n] = '\0';
}

static void testReasons(void)
{
    for (int r = 0; r < RD_LEDGER_REASONS; r++) {
        rd_ledger_stage_begin(&s_a, 7, "st07 (TEST)", 100);
        s_a.lastTick = 149; /* 50 ticks */
        char name[24];
        snprintf(name, sizeof name, "model_%d", r);
        const int k = rd_ledger_slot(&s_a.models, name);
        CHECK(k == 0, "the first name is row 0");
        /* left out in ticks (or pictures) 100..109, twice in each of
           them; the odd ones with the cameras between ticks / between
           ticks */
        for (uint32_t t = 100; t < 110; t++) {
            rd_ledger_note_drop(&s_a.models, k, r, t, (int)(t & 1u), 1);
            rd_ledger_note_drop(&s_a.models, k, r, t, (int)(t & 1u), 1);
        }
        CHECK(s_a.models.e[k].pics[r] == 10 && s_a.models.e[k].drops[r] == 20 &&
                  s_a.models.e[k].alt[r] == 5,
              "ten ticks, twenty drops, five with the cameras between ticks");
        (void)rd_ledger_format(&s_a, "ended", s_text, sizeof s_text);
        char want[256];
        const int byPic = rd_ledger_reason_by_picture(r);
        snprintf(want, sizeof want, "model_%d: never drawn; left out by %s in 10 %s (5 %s), 20 %s",
                 r, rd_ledger_reason_words(r), byPic ? "pictures" : "ticks",
                 byPic ? "between ticks" : "with the cameras between ticks",
                 r == RD_LEDGER_BOX_OBJECT ? "times" : "draws or parts");
        if (strstr(s_text, want) == NULL) {
            printf("  want: %s\n  text: %s\n", want, s_text);
        }
        CHECK(strstr(s_text, want) != NULL, "the reason's row");
        CHECK(strstr(s_text, "1 models: 0 drawn, 1 left out at times, 1 never drawn (0 with no "
                             "reason recorded)") != NULL,
              "the header's counts");
        CHECK(strstr(s_text, "scene ledger: stage 7 (st07 (TEST)) ended: ticks 100-149 (50)") ==
                  s_text,
              "the header first");
    }
    CHECK(strcmp(rd_ledger_reason_words(RD_LEDGER_REASONS), "?") == 0, "past the reasons: ?");
    CHECK(rd_ledger_reason_by_picture(RD_LEDGER_FRUSTUM) &&
              rd_ledger_reason_by_picture(RD_LEDGER_OCCLUDED) &&
              !rd_ledger_reason_by_picture(RD_LEDGER_BOX_OBJECT),
          "the port's test counts pictures, the game's side ticks");
}

static void testNeverDrawnAndCarry(void)
{
    rd_ledger_stage_begin(&s_a, 40, "deja", 1000);
    s_a.lastTick = 1830;
    const int walls = rd_ledger_slot(&s_a.models, "st13_deja1");
    const int sky = rd_ledger_slot(&s_a.models, "sky_r_bg");
    const int curtain = rd_ledger_slot(&s_a.models, "curtain01");
    rd_ledger_note_built(&s_a.models, walls);
    rd_ledger_note_built(&s_a.models, sky);
    rd_ledger_note_built(&s_a.models, curtain);
    for (uint32_t t = 1000; t <= 1830; t++) {
        rd_ledger_note_draw(&s_a.models, sky, t, 120);
        rd_ledger_note_draw(&s_a.models, sky, t, 30); /* two draws a tick: one tick */
    }
    rd_ledger_note_drop(&s_a.models, curtain, RD_LEDGER_BOX_PART, 1500, 1, 3);
    (void)rd_ledger_format(&s_a, "ended", s_text, sizeof s_text);
    char row[512];
    const char *w = rowOf(s_text, "st13_deja1");
    CHECK(w != NULL, "the walls' row");
    if (w != NULL) {
        rowText(w, row, sizeof row);
        CHECK(strncmp(row, " st13_deja1: never drawn, no reason recorded, 1 mesh built", 58) == 0,
              "built, never drawn, no reason: said so");
    }
    CHECK(strstr(s_text, "(1 with no reason recorded)") != NULL, "the header counts it");
    /* the never drawn rows come before the drawn ones */
    const char *s = rowOf(s_text, "sky_r_bg");
    CHECK(s != NULL && w != NULL && w < s, "never drawn first");
    if (s != NULL) {
        unsigned ticks = 0, of = 0, draws = 0;
        unsigned long long tris = 0;
        CHECK(sscanf(s, " sky_r_bg: drawn in %u of %u ticks (%u draws, %llu triangles)", &ticks,
                     &of, &draws, &tris) == 4,
              "the drawn row reads back");
        CHECK(ticks == 831 && of == 831 && draws == 1662 && tris == 831ull * 150ull,
              "the drawn row's counts read back as put in");
    }
    /* the next stage: the walls carried (built, never drawn, never left
       out), the sky (drawn) and the curtain (left out) not */
    rd_ledger_stage_begin(&s_b, 41, "next", 1831);
    rd_ledger_carry(&s_b, &s_a);
    CHECK(s_b.models.n == 1 && strcmp(s_b.models.e[0].name, "st13_deja1") == 0 &&
              s_b.models.e[0].carried == 1,
          "only the never drawn row is carried");
    s_b.lastTick = 1900;
    (void)rd_ledger_format(&s_b, "ended", s_text, sizeof s_text);
    CHECK(strstr(s_text, " st13_deja1: never drawn, no reason recorded, built before the stage "
                         "began") != NULL,
          "the carried row says where it was built");
}

static void testAddAndTable(void)
{
    rd_ledger_stage_begin(&s_a, 3, "", 1);
    s_a.lastTick = 10;
    rd_ledger_table_reset(&s_rep);
    const int g = rd_ledger_slot(&s_a.models, "cage");
    rd_ledger_note_draw(&s_a.models, g, 1, 10);
    const int r = rd_ledger_slot(&s_rep, "cage");
    rd_ledger_note_drop(&s_rep, r, RD_LEDGER_OCCLUDED, 77, 1, 1);
    const int r2 = rd_ledger_slot(&s_rep, "(particles)");
    rd_ledger_note_drop(&s_rep, r2, RD_LEDGER_FRUSTUM, 77, 0, 4);
    rd_ledger_table_add(&s_a.models, &s_rep);
    CHECK(s_a.models.n == 2, "the replay's cage is the game's cage row");
    CHECK(s_a.models.e[g].draws == 1 && s_a.models.e[g].pics[RD_LEDGER_OCCLUDED] == 1 &&
              s_a.models.e[g].alt[RD_LEDGER_OCCLUDED] == 1,
          "the two halves in one row");
    (void)rd_ledger_format(&s_a, "ended", s_text, sizeof s_text);
    CHECK(strstr(s_text, "scene ledger: stage 3 ended:") == s_text, "a stage without a name");
    CHECK(strstr(s_text, "left out by the port's occlusion test in 1 pictures (1 between ticks), "
                         "1 draws or parts") != NULL,
          "the replay's reason in the text");

    /* the table full: the names past it counted, nothing overwritten */
    rd_ledger_table_reset(&s_rep);
    char name[32];
    for (int i = 0; i < RD_LEDGER_SLOTS + 5; i++) {
        snprintf(name, sizeof name, "m%d", i);
        const int k = rd_ledger_slot(&s_rep, name);
        CHECK(i < RD_LEDGER_SLOTS ? k == i : k == -1, "rows in order, then -1");
    }
    CHECK(s_rep.n == RD_LEDGER_SLOTS && s_rep.pastNames == 5, "five names past the table");
    CHECK(rd_ledger_slot(&s_rep, "m3") == 3, "a known name still found when full");
    rd_ledger_note_draw(&s_rep, -1, 1, 1); /* a row of -1 is ignored */
    rd_ledger_note_drop(&s_rep, -1, RD_LEDGER_FILTER, 1, 0, 1);
    rd_ledger_note_drop(&s_rep, 0, RD_LEDGER_REASONS, 1, 0, 1);
    CHECK(s_rep.e[0].drops[RD_LEDGER_FILTER] == 0, "no stray counts");
    /* a long name cut to RD_LEDGER_NAME - 1 bytes, and found again cut */
    rd_ledger_table_reset(&s_rep);
    const int a = rd_ledger_slot(&s_rep, "a_model_name_well_past_23_bytes");
    const int b = rd_ledger_slot(&s_rep, "a_model_name_well_past_23_bytes_too");
    CHECK(a == 0 && b == 0 && strlen(s_rep.e[0].name) == RD_LEDGER_NAME - 1,
          "a long name cut, one row");
    CHECK(rd_ledger_slot(&s_rep, NULL) == 1 && strcmp(s_rep.e[1].name, "(no name)") == 0,
          "no name: (no name)");
}

static void testEffectsAndBounds(void)
{
    rd_ledger_stage_begin(&s_a, 12, "gate", 1);
    s_a.lastTick = 100;
    (void)rd_ledger_format(&s_a, "ended", s_text, sizeof s_text);
    CHECK(strstr(s_text, "scene ledger: stage 12 (gate) effects: none\n") != NULL, "no effects");
    for (uint32_t t = 1; t <= 100; t++) {
        rd_ledger_note_fx(&s_a, RD_LEDGER_FX_FOG, t, 0.25f);
        rd_ledger_note_fx(&s_a, RD_LEDGER_FX_FOG, t, 0.5f); /* one tick, the last value */
    }
    rd_ledger_note_fx(&s_a, RD_LEDGER_FX_SHADOWS, 50, 1676.0f);
    rd_ledger_note_fx(&s_a, RD_LEDGER_FX_COUNT, 50, 1.0f); /* ignored */
    (void)rd_ledger_format(&s_a, "ended", s_text, sizeof s_text);
    CHECK(strstr(s_text, "effects: fog 100 ticks (last strength 0.5), shadow volumes 1 ticks "
                         "(last triangles 1676)\n") != NULL,
          "the effects line");

    /* more rows than RD_LEDGER_ROWS: the rest counted */
    for (int i = 0; i < RD_LEDGER_ROWS + 10; i++) {
        char name[24];
        snprintf(name, sizeof name, "model%03d", i);
        rd_ledger_note_draw(&s_a.models, rd_ledger_slot(&s_a.models, name), 1, 1);
    }
    const size_t n = rd_ledger_format(&s_a, "ended", s_text, sizeof s_text);
    CHECK(n == strlen(s_text) && n < sizeof s_text, "the length written");
    CHECK(strstr(s_text, "scene ledger: stage 12 (gate): 10 more models not listed (10 of them "
                         "drawn and never left out)\n") != NULL,
          "the rows not listed counted");
    /* every line whole and under the log's line */
    const char *p = s_text;
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        CHECK(nl != NULL && nl - p < 1500, "a line ends and fits the log's line");
        if (nl == NULL) {
            break;
        }
        CHECK(strncmp(p, "scene ledger: ", 14) == 0, "every line is the ledger's");
        p = nl + 1;
    }
    char small[40];
    const size_t m = rd_ledger_format(&s_a, "ended", small, sizeof small);
    CHECK(m == sizeof small - 1 && strlen(small) == m, "a short buffer: cut, ended");
    CHECK(rd_ledger_format(&s_a, "ended", small, 0) == 0, "no buffer: nothing");
}

static void testKept(void)
{
    static RdLedgerKept k;
    memset(&k, 0, sizeof k);
    char t[64];
    for (int i = 1; i <= RD_LEDGER_KEEP + 2; i++) {
        snprintf(t, sizeof t, "scene ledger: stage %d ended\n", i);
        rd_ledger_keep(&k, t);
    }
    static char out[RD_LEDGER_TEXT * (RD_LEDGER_KEEP + 1)];
    (void)rd_ledger_kept_text(&k, "scene ledger: stage 99 so far\n", out, sizeof out);
    CHECK(strstr(out, "stage 1 ended") == NULL && strstr(out, "stage 2 ended") == NULL,
          "the oldest stages gone");
    const char *s3 = strstr(out, "stage 3 ended"), *s6 = strstr(out, "stage 6 ended");
    const char *now = strstr(out, "stage 99 so far");
    CHECK(s3 != NULL && s6 != NULL && now != NULL && s3 < s6 && s6 < now,
          "oldest first, then the stage in play");
    CHECK(strstr(out, "the last 4 stages played") != NULL, "the head says how many");
}

static void testCost(void)
{
    rd_ledger_stage_begin(&s_a, 1, "", 1);
    const int k = rd_ledger_slot(&s_a.models, "cost");

    enum { CALLS = 4000000 };

    double t0 = nowNs();
    for (uint32_t i = 0; i < CALLS; i++) {
        rd_ledger_note_draw(&s_a.models, k, i / 3000u, 12);
    }
    const double ledgerNs = (nowNs() - t0) / CALLS;
    /* the control: the same three additions on a volatile row */
    static volatile uint32_t ticks, draws;
    static volatile uint64_t tris;
    t0 = nowNs();
    for (uint32_t i = 0; i < CALLS; i++) {
        ticks = ticks + (i % 3000u == 0);
        draws = draws + 1;
        tris = tris + 12;
    }
    const double controlNs = (nowNs() - t0) / CALLS;
    printf("rd_ledger: a draw's tally %.2f ns (control %.2f ns)\n", ledgerNs, controlNs);
    CHECK(s_a.models.e[k].draws == CALLS && s_a.models.e[k].ticks == CALLS / 3000u + 1u,
          "the tally ran");
    CHECK(ledgerNs < 40.0 || ledgerNs < 6.0 * controlNs, "a draw's tally is a few additions");
}

int main(void)
{
    testReasons();
    testNeverDrawnAndCarry();
    testAddAndTable();
    testEffectsAndBounds();
    testKept();
    testCost();
    if (s_fails) {
        printf("rd_ledger_test: %d failure(s)\n", s_fails);
        return 1;
    }
    printf("rd_ledger_test: ok\n");
    return 0;
}
