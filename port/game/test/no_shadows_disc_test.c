/*
 * port/game/test/no_shadows_disc_test.c
 *
 * No Shadows (the journey's Customize row, port/game/no_shadows.h) read
 * against a disc image: the disc's boot ELF fills the data tables
 * (port/data/tables.c, the first descriptor set whose checksums it matches),
 * then:
 *
 *   placed   the Shadow rows (objLayout kind 4) that start in the room rather
 *            than in a pit (flag bit 19, the layout's initial dead bit, clear:
 *            Generator_Init copies it into the dead bit 18) are exactly the
 *            reviewed list of this disc: the final hall's (stage 46, the meeting
 *            and the fight, st13b.c), the deja vu cutscene's (stage 40,
 *            deja.c), the development rooms' (TEMP, cubic, athletic, seki,
 *            itou_boss, boss_test: no exit of the game leads there) and the
 *            left-alone kidnapper (in no stage's label range: backStage.c makes
 *            it, which No Shadows stops).  These are the rows the actEnemyStart
 *            hook newly starts as killed ones; a disc row more or fewer fails.
 *   cage     the cage room's Shadow (150; 133 on the North American disc, 132 on
 *            the August 2001 one, 104 on the June 2001 demo) is a Shadow row of
 *            "st13c (CAGE)" that starts in its pit: HandChk's dead test holds
 *            from the room's start under No Shadows.
 *   kidnap   the stages with a left-alone timer (stageData kidnapSeconds) and
 *            the stages holding Shadows, listed; the backStageProcessMain guard
 *            comes before every stage test in its four copies (backStage.c read
 *            here), so no stage is left out.
 *   queen    the Queen's stage ("st25a (QUEEN)", the 37 queen.c's gene_enemy
 *            tests) has no Shadow row: its gene_enemy counts 0 Shadows and calls
 *            nothing on every disc (queen.c is not hooked).
 *   presets  no preset of this disc sets No Shadows (a Customize row only), and
 *            the row on is what ico_no_shadows and the hooks' port calls read.
 *
 * Nothing starts the game; nothing read is written.  Skipped (77) without a
 * readable disc image, as in a worktree.
 *
 * Usage: no_shadows_disc_test <disc image> <work folder>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "typedef.h"

#include "../../core/region.h"
#include "config.h"
#include "difficulty.h"
#include "no_shadows.h"
#include "tables.h"
#include "vfs.h"
#include "../../test/ico_check.h"

#ifndef ICO_BACKSTAGE_SRC
#define ICO_BACKSTAGE_SRC "ico2/common/src/backStage.c"
#endif

/* port/data/gen/table_defs.c's writable definitions */
extern GenGeo objLayout[];

#define OBJ_SHADOW 4
#define OBJ_GENERATOR 33
#define QUEEN_STAGE 37

typedef struct Span {
    int lo, hi; /* labels lo..hi */
} Span;

typedef struct Placed {
    const char *slug;
    int cage;   /* the cage room's Shadow */
    int kidnap; /* the left-alone kidnapper, in no stage */
    int kidnapStages;
    Span spans[12];
} Placed;

/* the reviewed rows (read off each disc, 2026-11-10) */
static const Placed kPlaced[] = {
    {"pal",
     150,
     3757,
     26,
     {{2464, 2470},
      {2549, 2549},
      {2906, 2909},
      {2926, 2928},
      {2945, 2945},
      {3483, 3483},
      {3561, 3565},
      {3591, 3606},
      {3757, 3757}}},
    {"jpn",
     150,
     3734,
     26,
     {{2464, 2470},
      {2549, 2549},
      {2902, 2905},
      {2922, 2924},
      {2941, 2941},
      {3460, 3460},
      {3538, 3542},
      {3568, 3583},
      {3734, 3734}}},
    {"kor",
     150,
     3734,
     26,
     {{2464, 2470},
      {2549, 2549},
      {2902, 2905},
      {2922, 2924},
      {2941, 2941},
      {3460, 3460},
      {3538, 3542},
      {3568, 3583},
      {3734, 3734}}},
    {"cn",
     150,
     3734,
     26,
     {{2464, 2470},
      {2549, 2549},
      {2902, 2905},
      {2922, 2924},
      {2941, 2941},
      {3460, 3460},
      {3538, 3542},
      {3568, 3583},
      {3734, 3734}}},
    {"ntsc",
     133,
     3453,
     26,
     {{2258, 2264},
      {2341, 2341},
      {2698, 2701},
      {2718, 2720},
      {2737, 2737},
      {3212, 3212},
      {3290, 3294},
      {3320, 3335},
      {3453, 3453}}},
    {"aug6",
     132,
     3415,
     26,
     {{2233, 2237},
      {2312, 2312},
      {2668, 2671},
      {2688, 2690},
      {2707, 2707},
      {3182, 3182},
      {3260, 3264},
      {3290, 3305},
      {3415, 3415}}},
    {"jun26",
     104,
     3246,
     25,
     {{2139, 2140},
      {2206, 2206},
      {2520, 2523},
      {2540, 2542},
      {2559, 2559},
      {3025, 3025},
      {3122, 3137},
      {3246, 3246}}},
};

static int inSpans(const Placed *p, int label)
{
    for (int i = 0; i < 12 && p->spans[i].hi != 0; i++) {
        if (label >= p->spans[i].lo && label <= p->spans[i].hi) {
            return 1;
        }
    }
    return 0;
}

static int spanCount(const Placed *p)
{
    int n = 0;

    for (int i = 0; i < 12 && p->spans[i].hi != 0; i++) {
        n += p->spans[i].hi - p->spans[i].lo + 1;
    }
    return n;
}

static int stageOf(int label)
{
    const int n = (int)ico_tables_count(ICO_TID_stageData);

    for (int s = 0; s < n; s++) {
        if (label >= stageData[s].labelTop && label < stageData[s].labelEnd) {
            return s;
        }
    }
    return -1;
}

static int shadowsIn(int stage)
{
    int n = 0;

    for (int i = stageData[stage].labelTop; i < stageData[stage].labelEnd; i++) {
        n += objLayout[i].kind == OBJ_SHADOW;
    }
    return n;
}

static char *readSource(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *s;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    s = n > 0 ? malloc((size_t)n + 1) : NULL;
    if (s == NULL || fread(s, 1, (size_t)n, f) != (size_t)n) {
        free(s);
        fclose(f);
        return NULL;
    }
    fclose(f);
    s[n] = '\0';
    return s;
}

/* backStage.c: in each backStageProcessMain copy the guard comes before the copy's first
   test of the stage (stage_no ==) and its timer switch */
static void testGuard(void)
{
    static const char *const kCopies[4] = {
        "static void backStageProcessMain_ntsc(void)\n{",
        "static void backStageProcessMain_jun26(void)\n{",
        "static void backStageProcessMain_aug6(void)\n{",
        "void backStageProcessMain(void)\n{",
    };
    static const char kGuard[] = "if (ico_no_shadows_tick(stage_no) || ico_opt_yorda_safe()) {";
    char *src = readSource(ICO_BACKSTAGE_SRC);

    CHECK(src != NULL, "cannot read %s", ICO_BACKSTAGE_SRC);
    if (src == NULL) {
        return;
    }
    for (int c = 0; c < 4; c++) {
        const char *body = strstr(src, kCopies[c]);
        const char *guard = body != NULL ? strstr(body, kGuard) : NULL;
        const char *stage = body != NULL ? strstr(body, "stage_no ==") : NULL;
        const char *timer = body != NULL ? strstr(body, "switch (kidnapState)") : NULL;

        CHECK(body != NULL && guard != NULL && stage != NULL && timer != NULL,
              "backStage.c: the copy %d (%s) or its guard is missing", c, kCopies[c]);
        if (guard != NULL && stage != NULL && timer != NULL) {
            CHECK(guard < stage && guard < timer,
                  "backStage.c: the No Shadows guard of copy %d is not before its stage test", c);
        }
    }
    free(src);
}

int main(int argc, char **argv)
{
    const char *disc = argc > 1 ? argv[1] : NULL;
    const char *dir = argc > 2 ? argv[2] : ".";
    FILE *probe = disc != NULL && disc[0] != '\0' ? fopen(disc, "rb") : NULL;
    char err[256], cfg[512];
    const IcoTableSet *set = NULL;
    const Placed *want = NULL;

    if (probe == NULL) {
        printf("no_shadows_disc_test: SKIP (no disc image%s%s)\n", disc ? " at " : "",
               disc ? disc : "");
        return 77;
    }
    fclose(probe);
    IcoVfs *vfs = ico_vfs_mount(&ico_vfs_iso9660, disc);
    if (vfs == NULL) {
        printf("no_shadows_disc_test: SKIP (%s is not a readable disc image)\n", disc);
        return 77;
    }
    for (int i = 0; i < ICO_TABLE_SET_COUNT && set == NULL; i++) {
        if (ico_tables_load_vfs(vfs, ico_table_sets[i], err, sizeof err) == 0) {
            set = ico_table_sets[i];
        }
    }
    ico_vfs_unmount(vfs);
    CHECK(set != NULL, "no descriptor set loads this disc's tables (%s)", err);
    if (set == NULL || ico_region_select(set->slug) != 0) {
        return ico_check_summary("no_shadows_disc_test");
    }
    const char *slug = set->slug;
    for (size_t i = 0; i < sizeof kPlaced / sizeof kPlaced[0]; i++) {
        if (strcmp(kPlaced[i].slug, slug) == 0) {
            want = &kPlaced[i];
        }
    }
    CHECK(want != NULL, "%s: no reviewed list for this disc", slug);
    if (want == NULL) {
        return ico_check_summary("no_shadows_disc_test");
    }
    const int rows = (int)ico_tables_count(ICO_TID_objLayout);
    const int stages = (int)ico_tables_count(ICO_TID_stageData);

    /* the row and the journey's answer, with no config file of the user's */
    snprintf(cfg, sizeof cfg, "%s/no_shadows_disc_%s.toml", dir, slug);
    remove(cfg);
    ico_config_reset(cfg, "/nonexistent/no_shadows_disc_test.ini");

    /* placed */
    {
        int shadows = 0;
        int placed = 0;
        int generators = 0;

        printf("no_shadows_disc_test: %s, the %s tables (%d layout rows, %d stages)\n", disc, slug,
               rows, stages);
        for (int i = 0; i < rows; i++) {
            const GenGeo *g = &objLayout[i];

            generators += g->kind == OBJ_GENERATOR;
            if (g->kind != OBJ_SHADOW) {
                continue;
            }
            shadows++;
            if (((g->flags >> 19) & 1) == 0) {
                const int s = stageOf(i);

                placed++;
                printf("no_shadows_disc_test:   placed Shadow %d: stage %d %s\n", i, s,
                       s >= 0 ? stageData[s].name : "(none: the left-alone kidnapper)");
                CHECK(inSpans(want, i),
                      "%s: Shadow row %d starts in the room (stage %d) and is "
                      "not on the reviewed list: read its room's scripts",
                      slug, i, s);
            }
        }
        for (int k = 0; k < 12 && want->spans[k].hi != 0; k++) {
            for (int i = want->spans[k].lo; i <= want->spans[k].hi; i++) {
                CHECK(i < rows && objLayout[i].kind == OBJ_SHADOW &&
                          ((objLayout[i].flags >> 19) & 1) == 0,
                      "%s: reviewed row %d is not a Shadow placed in its room", slug, i);
            }
        }
        CHECK(placed == spanCount(want), "%s: %d placed Shadows, %d reviewed", slug, placed,
              spanCount(want));
        CHECK(want->kidnap < rows && stageOf(want->kidnap) == -1,
              "%s: the kidnapper %d lies in a stage", slug, want->kidnap);
        printf("no_shadows_disc_test:   %d Shadow rows, %d placed in their rooms, %d generators\n",
               shadows, placed, generators);
    }

    /* cage */
    {
        const int c = want->cage;
        const int s = c < rows ? stageOf(c) : -1;

        CHECK(c < rows && objLayout[c].kind == OBJ_SHADOW && ((objLayout[c].flags >> 19) & 1) == 1,
              "%s: the cage room's Shadow %d is not a Shadow row starting in its pit", slug, c);
        CHECK(s >= 0 && strstr(stageData[s].name, "(CAGE)") != NULL,
              "%s: the cage room's Shadow %d is in stage %d, not the cage room", slug, c, s);
        if (s >= 0) {
            printf("no_shadows_disc_test:   the cage room's Shadow %d: stage %d %s, in its pit\n",
                   c, s, stageData[s].name);
        }
    }

    /* kidnap */
    {
        int timed = 0;

        printf("no_shadows_disc_test:   the left-alone timer's stages:");
        for (int s = 0; s < stages; s++) {
            if (stageData[s].kidnapSeconds != 0) {
                printf(" %d (%u s)", s, (unsigned)stageData[s].kidnapSeconds);
                timed++;
            }
        }
        printf("\nno_shadows_disc_test:   the stages holding Shadows:");
        for (int s = 0; s < stages; s++) {
            const int n = shadowsIn(s);

            if (n > 0) {
                printf(" %d (%d)", s, n);
            }
        }
        printf("\n");
        CHECK(timed == want->kidnapStages, "%s: %d stages with a left-alone timer, %d reviewed",
              slug, timed, want->kidnapStages);
        testGuard();
    }

    /* queen */
    {
        int queen = -1;

        for (int s = 0; s < stages; s++) {
            if (strstr(stageData[s].name, "(QUEEN)") != NULL) {
                queen = s;
            }
        }
        CHECK(queen == QUEEN_STAGE, "%s: the Queen's stage is %d, queen.c tests %d", slug, queen,
              QUEEN_STAGE);
        if (queen >= 0) {
            CHECK(shadowsIn(queen) == 0, "%s: the Queen's stage has %d Shadow rows", slug,
                  shadowsIn(queen));
            printf("no_shadows_disc_test:   the Queen's stage %d %s: %d Shadow rows\n", queen,
                   stageData[queen].name, shadowsIn(queen));
        }
    }

    /* presets */
    {
        IcoDifficulty d;

        for (int m = ICO_DIFF_EASY; m <= ICO_DIFF_HARD; m++) {
            ico_difficulty_preset((IcoDiffMode)m, &d);
            CHECK(d.row[ICO_DIFF_NO_SHADOWS] == 0, "%s: the %s preset sets No Shadows", slug,
                  ico_difficulty_mode_name((IcoDiffMode)m));
        }
        ico_difficulty_reset();
        CHECK(ico_no_shadows() == 0 && ico_no_shadows_hold(1, 0) == 0 &&
                  ico_no_shadows_drop(ICO_NOSH_DROP_PIT, 1) == 0,
              "%s: No Shadows on at the title", slug);
        ico_difficulty_set_row(ICO_DIFF_NO_SHADOWS, 1);
        CHECK(ico_no_shadows() == 1 && ico_no_shadows_drop(ICO_NOSH_DROP_PIT, 1) == 1,
              "%s: the row on is not what the hooks read", slug);
        ico_difficulty_reset();
        CHECK(ico_no_shadows() == 0, "%s: the title did not put the row back", slug);
    }

    remove(cfg);
    return ico_check_summary("no_shadows_disc_test");
}
