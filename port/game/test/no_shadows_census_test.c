/*
 * port/game/test/no_shadows_census_test.c
 *
 * No Shadows (the journey's Customize row, port/game/no_shadows.h) cannot be
 * played here, so its proof is the source:
 *
 * census   every call in the stage scripts (ico2/script/src/ *.c) of the checks
 *          and calls that read or bring out a Shadow (scpCheckExistAliveEnemy,
 *          actEnemyFlagCheckDead, isEnemyActive, InqCapsuleGhostBossEnd,
 *          ACTEnemyForceSwitchToCarry, WakeUpSpidersFromGenerator,
 *          DirectCallEnemy, Generator_QuickCall) is in the reviewed table
 *          below, by file, enclosing function and callee, with its count and
 *          its class:
 *            passes      a wait for "no live Shadow" that passes at once, as
 *                        after the room's fight (every Shadow starts killed)
 *            gated       a wait that would pass too early in the story: held by
 *                        the started-gate (st13b.c's final fight)
 *            refused     the call does nothing under No Shadows (its hook)
 *            unaffected  the outcome does not change the story under No Shadows
 *          A new site, a moved one, a changed count, or a table row with no
 *          site fails: the reviewer reads what it waits for and classifies it
 *          (docs/port/GAME_FEATURES.md "No Shadows").
 * hooks    each hook in each disc copy of its function (the European body,
 *          _ntsc, _aug6, _jun26 and the shared twins), before the statement it
 *          guards; a family with a copy more or fewer than reviewed fails.
 * log      no_shadows.c (compiled in, with this file's ico_no_shadows): off,
 *          every call answers 0 and prints nothing; on, the room's line once
 *          two ticks pass with no new Shadow (a Shadow started before the first
 *          tick of its room counts there), the left room's refusal counts, the
 *          forced conditions' lines, the rate limit (the first 8, then every
 *          100th).
 *
 * The scan blanks comments, string and character literals and preprocessor
 * lines first; a function is a brace block at file level after a name and its
 * parameter list (not after '=').  The files are read as bytes (some keep
 * their legacy Japanese encoding).
 *
 * Usage: no_shadows_census_test <source dir>   (ICO_SOURCE_DIR without it)
 */
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../no_shadows.h"
#include "../../test/ico_check.h"

#ifndef ICO_SOURCE_DIR
#define ICO_SOURCE_DIR "."
#endif

/* --- the source --------------------------------------------------------------------- */

typedef struct Text {
    char *raw;  /* the file */
    char *code; /* the same length: comments, literals and directives blanked */
    size_t size;
} Text;

typedef struct Func {
    char name[96];
    size_t lo, hi; /* the body, braces included, in Text.code */
} Func;

enum { MAX_FUNCS = 4096 };

static void blank(char *c, size_t lo, size_t hi)
{
    for (size_t k = lo; k < hi; k++) {
        if (c[k] != '\n') {
            c[k] = ' ';
        }
    }
}

static int readText(const char *path, Text *t)
{
    FILE *f = fopen(path, "rb");
    long n;

    memset(t, 0, sizeof *t);
    if (f == NULL) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        fclose(f);
        return -1;
    }
    t->raw = malloc((size_t)n + 1);
    t->code = malloc((size_t)n + 1);
    if (t->raw == NULL || t->code == NULL || fread(t->raw, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(t->raw);
        free(t->code);
        memset(t, 0, sizeof *t);
        return -1;
    }
    fclose(f);
    t->raw[n] = '\0';
    t->size = (size_t)n;
    memcpy(t->code, t->raw, (size_t)n + 1);

    /* blank comments, literals (their quotes kept), preprocessor lines; newlines kept */
    for (size_t i = 0; i < t->size;) {
        char *c = t->code;
        size_t j;

        if (c[i] == '/' && i + 1 < t->size && c[i + 1] == '*') {
            for (j = i + 2; j + 1 < t->size && !(c[j] == '*' && c[j + 1] == '/'); j++) {}
            j = j + 1 < t->size ? j + 2 : t->size;
            blank(c, i, j);
            i = j;
        } else if (c[i] == '/' && i + 1 < t->size && c[i + 1] == '/') {
            for (j = i; j < t->size && c[j] != '\n'; j++) {}
            blank(c, i, j);
            i = j;
        } else if (c[i] == '"' || c[i] == '\'') {
            const char q = c[i];

            for (j = i + 1; j < t->size && c[j] != q; j++) {
                if (c[j] == '\\') {
                    j++;
                }
            }
            if (j > t->size) {
                j = t->size;
            }
            blank(c, i + 1, j);
            i = j < t->size ? j + 1 : t->size;
        } else if (c[i] == '#' && (i == 0 || c[i - 1] == '\n')) {
            for (j = i; j < t->size && c[j] != '\n'; j++) {
                if (c[j] == '\\' && j + 1 < t->size && c[j + 1] == '\n') {
                    j++;
                }
            }
            blank(c, i, j);
            i = j;
        } else {
            i++;
        }
    }
    return 0;
}

static void freeText(Text *t)
{
    free(t->raw);
    free(t->code);
    memset(t, 0, sizeof *t);
}

static int isIdent(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* the identifier ending at i is followed by '(' (blanks between) */
static int callFollows(const char *c, size_t i, size_t n)
{
    while (i < n && (c[i] == ' ' || c[i] == '\t' || c[i] == '\n' || c[i] == '\r')) {
        i++;
    }
    return i < n && c[i] == '(';
}

/* the file-level functions: the first name followed by '(' since the last ';' or '}' at
   file level, its block not after an '=' (an initializer) */
static int scanFuncs(const Text *t, Func *out, int max)
{
    const char *c = t->code;
    int depth = 0;
    int n = 0;
    int data = 0;
    char last[96] = "";
    size_t start = 0;

    for (size_t i = 0; i < t->size; i++) {
        if (isIdent(c[i]) && (i == 0 || !isIdent(c[i - 1]))) {
            size_t j = i;

            while (j < t->size && isIdent(c[j])) {
                j++;
            }
            if (depth == 0 && last[0] == '\0' && !(c[i] >= '0' && c[i] <= '9') &&
                j - i < sizeof last && callFollows(c, j, t->size)) {
                memcpy(last, c + i, j - i);
                last[j - i] = '\0';
            }
            i = j - 1;
            continue;
        }
        switch (c[i]) {
        case '{':
            if (depth == 0) {
                start = i;
            }
            depth++;
            break;
        case '}':
            depth--;
            if (depth == 0) {
                if (!data && last[0] != '\0' && n < max) {
                    snprintf(out[n].name, sizeof out[n].name, "%s", last);
                    out[n].lo = start;
                    out[n].hi = i + 1;
                    n++;
                }
                last[0] = '\0';
                data = 0;
            }
            break;
        case ';':
            if (depth == 0) {
                last[0] = '\0';
                data = 0;
            }
            break;
        case '=':
            if (depth == 0) {
                data = 1;
            }
            break;
        default:
            break;
        }
    }
    return n;
}

/* the count of needle in [lo, hi) of the code; *first its first offset */
static int countIn(const Text *t, size_t lo, size_t hi, const char *needle, size_t *first)
{
    const size_t k = strlen(needle);
    int n = 0;

    if (first != NULL) {
        *first = (size_t)-1;
    }
    for (size_t i = lo; i + k <= hi; i++) {
        if (memcmp(t->code + i, needle, k) == 0) {
            if (n == 0 && first != NULL) {
                *first = i;
            }
            n++;
        }
    }
    return n;
}

/* --- the census --------------------------------------------------------------------- */

static const char *const kCallees[] = {
    "scpCheckExistAliveEnemy", "actEnemyFlagCheckDead",      "isEnemyActive",
    "InqCapsuleGhostBossEnd",  "ACTEnemyForceSwitchToCarry", "WakeUpSpidersFromGenerator",
    "DirectCallEnemy",         "Generator_QuickCall",
};

enum { CALLEE_COUNT = (int)(sizeof kCallees / sizeof kCallees[0]) };

typedef struct Site {
    const char *file;
    const char *func;
    const char *callee;
    int count;
    const char *cls; /* passes | gated | refused | unaffected */
    const char *why;
} Site;

/* The reviewed table.  The way-point toggles (WayOnChk / WayOffChk, TakiWay*) switch
   Yorda's paths on once no Shadow is alive and she stands on a floor, and off when one
   comes or she leaves it: with every Shadow killed from the start they behave as after
   the room's fight. */
static const Site kSites[] = {
    {"script.c", "scpCheckExistAliveEnemy", "actEnemyFlagCheckDead", 1, "unaffected",
     "the aggregate itself: its callers are the rows below"},
    {"st00a.c", "actSt00aEneChk_jun26", "WakeUpSpidersFromGenerator", 2, "unaffected",
     "June 2001 demo: the spiders come out of the pits and roam; their revive into a Shadow "
     "is refused (UnlockEnemyGenerate, DirectCallEnemy); nothing waits on them"},
    {"st01b.c", "actSt01bEneChk_jun26", "WakeUpSpidersFromGenerator", 2, "unaffected", "as st00a"},
    {"st04c.c", "actSt04cEneChk_jun26", "WakeUpSpidersFromGenerator", 2, "unaffected",
     "as st00a; the flags after it are set whatever the spiders do"},
    {"st02a.c", "actSt02aWayOnChk", "scpCheckExistAliveEnemy", 1, "passes", "way-points on"},
    {"st02a.c", "actSt02aWayOnChk_ntsc", "scpCheckExistAliveEnemy", 1, "passes", "way-points on"},
    {"st02a.c", "actSt02aWayOffChk", "scpCheckExistAliveEnemy", 1, "unaffected",
     "way-points off when Yorda leaves the floor"},
    {"st02a.c", "actSt02aWayOffChk_ntsc", "scpCheckExistAliveEnemy", 1, "unaffected",
     "way-points off when Yorda leaves the floor"},
    {"st02a.c", "actSt02aTakiWayOnChk", "scpCheckExistAliveEnemy", 1, "passes",
     "waterfall way-points on"},
    {"st02a.c", "actSt02aTakiWayOnChk_ntsc", "scpCheckExistAliveEnemy", 1, "passes",
     "waterfall way-points on"},
    {"st02a.c", "actSt02aTakiWayOffChk", "scpCheckExistAliveEnemy", 1, "unaffected",
     "waterfall way-points off when Yorda leaves the floor"},
    {"st02a.c", "actSt02aTakiWayOffChk_ntsc", "scpCheckExistAliveEnemy", 1, "unaffected",
     "waterfall way-points off when Yorda leaves the floor"},
    {"st03t.c", "actSt03tWayOnChk", "scpCheckExistAliveEnemy", 1, "passes", "way-points on"},
    {"st03t.c", "actSt03tWayOnChk_ntsc", "scpCheckExistAliveEnemy", 1, "passes", "way-points on"},
    {"st03t.c", "actSt03tWayOffChk", "scpCheckExistAliveEnemy", 1, "unaffected",
     "way-points off when Yorda leaves the floor"},
    {"st03t.c", "actSt03tWayOffChk_ntsc", "scpCheckExistAliveEnemy", 1, "unaffected",
     "way-points off when Yorda leaves the floor"},
    {"st06a.c", "actSt06aWayOnChk", "scpCheckExistAliveEnemy", 1, "passes", "way-points on"},
    {"st06a.c", "actSt06aWayOnChk_ntsc", "scpCheckExistAliveEnemy", 1, "passes", "way-points on"},
    {"st06a.c", "actSt06aWayOffChk", "scpCheckExistAliveEnemy", 1, "unaffected",
     "way-points off when Yorda leaves the floor"},
    {"st06a.c", "actSt06aWayOffChk_ntsc", "scpCheckExistAliveEnemy", 1, "unaffected",
     "way-points off when Yorda leaves the floor"},
    {"st10r.c", "actSt10rWayOnChk", "scpCheckExistAliveEnemy", 1, "passes", "way-points on"},
    {"st10r.c", "actSt10rWayOffChk", "scpCheckExistAliveEnemy", 1, "unaffected",
     "way-points off when Yorda leaves the floor"},
    {"st13b.c", "actSt13bBossAfterChk", "InqCapsuleGhostBossEnd", 1, "gated",
     "the final fight's end: true once started (itou_boss.c), held for the fight's music "
     "and 4 s (st13bNoShadowsFightHeld)"},
    {"st13b.c", "actSt13bBossAfterChk_ntsc", "InqCapsuleGhostBossEnd", 1, "gated",
     "as the European body"},
    {"st13b.c", "actSt13bBossAfterChk_aug6", "InqCapsuleGhostBossEnd", 1, "gated",
     "as the European body"},
    {"st13b.c", "actSt13bBossAfterChk_jun26", "actEnemyFlagCheckDead", 2, "gated",
     "June 2001 demo: Shadows 2139 and 2140 start killed; the waiter, armed at the room's "
     "load, holds until actConte10b_jun26 is over (st13bNoShadowsFightHeld)"},
    {"st13c.c", "actSt13cCageFallChk", "ACTEnemyForceSwitchToCarry", 1, "refused",
     "the cage room's forced grab: Yorda is not attached to the absent Shadow 150"},
    {"st13c.c", "actSt13cCageFallChk", "DirectCallEnemy", 1, "refused",
     "the skipped cutscene's call of Shadow 150 brings nothing"},
    {"st13c.c", "actSt13cCageFallChk", "Generator_QuickCall", 1, "refused",
     "the skipped cutscene's pit does not open"},
    {"st13c.c", "actSt13cCageFallChk", "isEnemyActive", 1, "unaffected",
     "0 (no Shadow active): guards the refused DirectCallEnemy"},
    {"st13c.c", "actSt13cCageFallChk_ntsc", "ACTEnemyForceSwitchToCarry", 1, "refused",
     "as the European body, Shadow 133"},
    {"st13c.c", "actSt13cCageFallChk_ntsc", "DirectCallEnemy", 1, "refused",
     "as the European body"},
    {"st13c.c", "actSt13cCageFallChk_ntsc", "Generator_QuickCall", 1, "refused",
     "as the European body"},
    {"st13c.c", "actSt13cCageFallChk_ntsc", "isEnemyActive", 1, "unaffected",
     "as the European body"},
    {"st13c.c", "actSt13cCageFallChk_aug6", "ACTEnemyForceSwitchToCarry", 1, "refused",
     "as the European body, Shadow 132"},
    {"st13c.c", "actSt13cCageFallChk_aug6", "DirectCallEnemy", 1, "refused",
     "as the European body"},
    {"st13c.c", "actSt13cCageFallChk_aug6", "Generator_QuickCall", 1, "refused",
     "as the European body"},
    {"st13c.c", "actSt13cCageFallChk_aug6", "isEnemyActive", 1, "unaffected",
     "as the European body"},
    {"st13c.c", "actSt13cCageFallChk_jun26", "ACTEnemyForceSwitchToCarry", 1, "refused",
     "June 2001 demo, Shadow 104; its skipped-cutscene wait on 104's motion ends (the demo's "
     "EnemyGeo plays a hidden Shadow's motion)"},
    {"st13c.c", "actSt13cHandChk", "actEnemyFlagCheckDead", 1, "passes",
     "Shadow 150 is dead from the room's start: the hand scene, then SekizoChk's idol door"},
    {"st13c.c", "actSt13cHandChk_ntsc", "actEnemyFlagCheckDead", 1, "passes", "Shadow 133"},
    {"st13c.c", "actSt13cHandChk_aug6", "actEnemyFlagCheckDead", 1, "passes", "Shadow 132"},
    {"st13c.c", "actSt13cHandChk_jun26", "actEnemyFlagCheckDead", 1, "passes", "Shadow 104"},
};

enum { SITE_COUNT = (int)(sizeof kSites / sizeof kSites[0]) };

static int s_seen[SITE_COUNT];
static Func s_funcs[MAX_FUNCS];

static void censusFile(const char *dir, const char *name)
{
    char path[1024 + 1 + 256]; /* the folder (testCensus's dir) and a d_name */
    Text t;
    int n;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (readText(path, &t) != 0) {
        CHECK(0, "cannot read %s", path);
        return;
    }
    n = scanFuncs(&t, s_funcs, MAX_FUNCS);
    CHECK(n < MAX_FUNCS, "%s: more than %d functions", name, MAX_FUNCS);
    for (int f = 0; f < n; f++) {
        for (int k = 0; k < CALLEE_COUNT; k++) {
            const char *callee = kCallees[k];
            const size_t len = strlen(callee);
            int count = 0;
            int row = -1;

            for (size_t i = s_funcs[f].lo + 1; i + len < s_funcs[f].hi; i++) {
                if (memcmp(t.code + i, callee, len) == 0 && !isIdent(t.code[i - 1]) &&
                    !isIdent(t.code[i + len]) && callFollows(t.code, i + len, s_funcs[f].hi)) {
                    count++;
                }
            }
            if (count == 0) {
                continue;
            }
            for (int s = 0; s < SITE_COUNT; s++) {
                if (strcmp(kSites[s].file, name) == 0 &&
                    strcmp(kSites[s].func, s_funcs[f].name) == 0 &&
                    strcmp(kSites[s].callee, callee) == 0) {
                    row = s;
                }
            }
            CHECK(row >= 0,
                  "%s %s: %d call(s) of %s not in the reviewed table: classify it (passes, "
                  "gated, refused, unaffected) after reading what it waits for",
                  name, s_funcs[f].name, count, callee);
            if (row >= 0) {
                s_seen[row] += count;
            }
        }
    }
    freeText(&t);
}

static void testCensus(const char *root)
{
    static const char *const kClasses[4] = {"passes", "gated", "refused", "unaffected"};
    char dir[1024];
    DIR *d;
    struct dirent *e;
    int files = 0;
    int counts[4] = {0, 0, 0, 0};

    snprintf(dir, sizeof dir, "%s/ico2/script/src", root);
    d = opendir(dir);
    CHECK(d != NULL, "cannot list %s", dir);
    if (d == NULL) {
        return;
    }
    while ((e = readdir(d)) != NULL) {
        const size_t n = strlen(e->d_name);

        if (n > 2 && strcmp(e->d_name + n - 2, ".c") == 0) {
            censusFile(dir, e->d_name);
            files++;
        }
    }
    closedir(d);
    CHECK(files > 40, "%d script files under %s", files, dir);
    for (int s = 0; s < SITE_COUNT; s++) {
        int c = -1;

        CHECK(s_seen[s] == kSites[s].count, "%s %s: %d call(s) of %s, the table says %d",
              kSites[s].file, kSites[s].func, s_seen[s], kSites[s].callee, kSites[s].count);
        for (int k = 0; k < 4; k++) {
            if (strcmp(kSites[s].cls, kClasses[k]) == 0) {
                c = k;
            }
        }
        CHECK(c >= 0 && kSites[s].why[0] != '\0', "%s %s: class %s", kSites[s].file, kSites[s].func,
              kSites[s].cls);
        if (c >= 0) {
            counts[c] += kSites[s].count;
        }
    }
    printf("no_shadows_census_test: %d script files, %d reviewed rows: %d calls pass at once, "
           "%d gated, %d refused, %d unaffected\n",
           files, SITE_COUNT, counts[0], counts[1], counts[2], counts[3]);
}

/* --- the hooks ------------------------------------------------------------------------ */

typedef struct Hook {
    const char *file;   /* under the source dir */
    const char *family; /* the function, and its disc copies family_<suffix> */
    int copies;         /* the family's functions in the file */
    const char *only;   /* NULL: the hook in every copy; else in this one only (the dispatch) */
    const char *needle; /* once in each */
    const char *before; /* NULL, or what the hook comes before */
} Hook;

static const Hook kHooks[] = {
    {"ico2/omori/src/generator.c", "DirectCallEnemy", 1, NULL, "if (ico_no_shadows_drop(1, 1)) {",
     "gg->flags = (gg->flags | 0x200000)"},
    {"ico2/omori/src/generator.c", "UnlockEnemyGenerate", 1, NULL,
     "if (ico_no_shadows_drop(3, 1)) {", "g->flags = (g->flags | 0x200000)"},
    {"ico2/omori/src/generator.c", "Generator_QuickCall", 1, NULL,
     "if (ico_no_shadows_drop(2, 1)) {", "w->status = 1;"},
    {"ico2/omori/src/generator.c", "GeneratorGeo", 4, NULL,
     "if (ico_no_shadows_drop(0, w->callRequests)) {", "switch (w->callState)"},
    {"ico2/fumi/src/enemy_act.c", "actEnemyStart", 4, NULL,
     "if (ico_no_shadows_hold(((GObj *)self)->labelId, alive)) {", "if (alive != 0)"},
    {"ico2/fumi/src/enemy_act.c", "actEnemyForceSwitchToCarry", 2, "actEnemyForceSwitchToCarry",
     "if (ico_no_shadows()) {", "if (ICO_JUN26)"},
    {"ico2/fumi/src/enemy_act.c", "ACTEnemyForceSwitchToCarry", 1, NULL, "if (ico_no_shadows()) {",
     "ACTSendMailCorrect"},
    {"ico2/common/src/backStage.c", "backStageProcessMain", 4, NULL,
     "if (ico_no_shadows_tick(stage_no) || ico_opt_yorda_safe()) {", "switch (kidnapState)"},
    {"ico2/ito/src/itou_boss.c", "InqCapsuleGhostBossEnd", 2, "InqCapsuleGhostBossEnd",
     "if (ico_no_shadows()) {", "isysGObjSearchFromObjKindID_begin(4)"},
    {"ico2/ito/src/itou_boss.c", "gene_enemy", 2, NULL, "!ico_no_shadows()", "Generator_Call(o)"},
    {"ico2/script/src/st13b.c", "actSt13bBossAfterChk", 4, NULL, "st13bNoShadowsFightHeld()",
     "lt_switch_layout"},
    {"ico2/script/src/st13b.c", "actSt13bBoss", 2, NULL, "nsFightBegun = 0;", "_ACTWait(1)"},
    {"ico2/script/src/st13b.c", "actConte10b", 2, NULL, "st13bNoShadowsFightBegun();", NULL},
    {"ico2/script/src/st13b.c", "actSt13bBossChk", 4, "actSt13bBossChk",
     "st13bNoShadowsFightBegun();", NULL},
};

enum { HOOK_COUNT = (int)(sizeof kHooks / sizeof kHooks[0]) };

/* the disc copies' suffixes (the region twins' names) */
static const char *const kSuffixes[] = {"ntsc",     "jun26",    "aug6",          "aug6jun26",
                                        "ntscaug6", "jpnkorcn", "ntscaug6jun26", "pal"};

static int inFamily(const char *name, const char *family)
{
    const size_t n = strlen(family);

    if (strncmp(name, family, n) != 0) {
        return 0;
    }
    if (name[n] == '\0') {
        return 1;
    }
    if (name[n] != '_') {
        return 0;
    }
    for (size_t s = 0; s < sizeof kSuffixes / sizeof kSuffixes[0]; s++) {
        if (strcmp(name + n + 1, kSuffixes[s]) == 0) {
            return 1;
        }
    }
    return 0;
}

static void testHooks(const char *root)
{
    int total = 0;
    char path[1024];
    Text t;

    for (int h = 0; h < HOOK_COUNT; h++) {
        const Hook *k = &kHooks[h];
        int n;
        int copies = 0;

        snprintf(path, sizeof path, "%s/%s", root, k->file);
        if (readText(path, &t) != 0) {
            CHECK(0, "cannot read %s", path);
            continue;
        }
        n = scanFuncs(&t, s_funcs, MAX_FUNCS);
        for (int f = 0; f < n; f++) {
            size_t at;
            size_t guard;
            int c;

            if (!inFamily(s_funcs[f].name, k->family)) {
                continue;
            }
            copies++;
            if (k->only != NULL && strcmp(s_funcs[f].name, k->only) != 0) {
                continue;
            }
            c = countIn(&t, s_funcs[f].lo, s_funcs[f].hi, k->needle, &at);
            CHECK(c == 1, "%s %s: the No Shadows hook \"%s\" %d time(s), want 1", k->file,
                  s_funcs[f].name, k->needle, c);
            if (c == 1 && k->before != NULL) {
                CHECK(countIn(&t, at, s_funcs[f].hi, k->before, &guard) > 0,
                      "%s %s: the hook is not before \"%s\"", k->file, s_funcs[f].name, k->before);
            }
            total += c == 1;
        }
        CHECK(copies == k->copies,
              "%s: %d copies of %s, the review saw %d: hook the new one and update the table",
              k->file, copies, k->family, k->copies);
        freeText(&t);
    }
    /* the gate's start: defined once, called at the five fight starts */
    snprintf(path, sizeof path, "%s/ico2/script/src/st13b.c", root);
    if (readText(path, &t) == 0) {
        CHECK(countIn(&t, 0, t.size, "st13bNoShadowsFightBegun();", NULL) == 5,
              "st13b.c: st13bNoShadowsFightBegun is called at the five fight starts (BossChk, "
              "Conte10b, Conte10b_jun26, the two begun-fight entries)");
        freeText(&t);
    } else {
        CHECK(0, "cannot read %s", path);
    }
    printf("no_shadows_census_test: %d hooks in their disc copies\n", total);
}

/* --- no_shadows.c ---------------------------------------------------------------------- */

static int s_on;

int ico_no_shadows(void)
{
    return s_on;
}

static char s_log[8192];

/* fd 2 to a file around fn; its text in s_log */
static void captured(void (*fn)(void))
{
    const char *tmp = getenv("TMPDIR");
    char path[512];
    FILE *f;
    size_t n = 0;
    int fd;
    int saved;

    s_log[0] = '\0';
    snprintf(path, sizeof path, "%s/no_shadows_census_%d.log", tmp != NULL ? tmp : ".",
             (int)getpid());
    fflush(stderr);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    saved = dup(2);
    if (fd < 0 || saved < 0) {
        CHECK(0, "cannot capture fd 2 in %s", path);
        if (fd >= 0) {
            close(fd);
        }
        fn();
        return;
    }
    dup2(fd, 2);
    close(fd);
    fn();
    fflush(stderr);
    dup2(saved, 2);
    close(saved);
    f = fopen(path, "rb");
    if (f != NULL) {
        n = fread(s_log, 1, sizeof s_log - 1, f);
        fclose(f);
    }
    s_log[n] = '\0';
    remove(path);
}

static void runOff(void)
{
    s_on = 0;
    ico_no_shadows_reset();
    CHECK(ico_no_shadows_hold(100, 0) == 0 && ico_no_shadows_drop(ICO_NOSH_DROP_PIT, 3) == 0 &&
          ico_no_shadows_tick(5) == 0);
    ico_no_shadows_tick(5);
    ico_no_shadows_tick(6);
    CHECK(ico_no_shadows_drop_count(ICO_NOSH_DROP_PIT) == 0 && ico_no_shadows_lines() == 0);
}

static void runOn(void)
{
    s_on = 1;
    ico_no_shadows_reset();
    /* room 5: two Shadows start in its load's frames, one before the first tick that sees it */
    CHECK(ico_no_shadows_hold(100, 0) == 1);
    CHECK(ico_no_shadows_tick(5) == 1);
    CHECK(ico_no_shadows_hold(101, 1) == 1);
    ico_no_shadows_tick(5); /* quiet 1 */
    CHECK(ico_no_shadows_lines() == 0);
    ico_no_shadows_tick(5); /* quiet 2: the line */
    CHECK(ico_no_shadows_lines() == 1, "the room's line after two quiet ticks (%d lines)",
          ico_no_shadows_lines());
    ico_no_shadows_tick(5);
    CHECK(ico_no_shadows_lines() == 1);
    /* refusals while in it; printed on leaving */
    CHECK(ico_no_shadows_drop(ICO_NOSH_DROP_PIT, 3) == 1);
    CHECK(ico_no_shadows_drop(ICO_NOSH_DROP_DIRECT, 1) == 1);
    CHECK(ico_no_shadows_drop(ICO_NOSH_DROP_PIT, 0) == 1);
    ico_no_shadows_tick(6);
    CHECK(ico_no_shadows_lines() == 2);
    CHECK(ico_no_shadows_drop_count(ICO_NOSH_DROP_PIT) == 3 &&
          ico_no_shadows_drop_count(ICO_NOSH_DROP_DIRECT) == 1 &&
          ico_no_shadows_drop_count(ICO_NOSH_DROP_QUICK) == 0);
    /* room 6 has no Shadow and refuses nothing: no line on leaving it */
    ico_no_shadows_tick(6);
    ico_no_shadows_tick(7);
    CHECK(ico_no_shadows_lines() == 2);
    /* the forced conditions; the rate limit: 120 grabs give 8 + 1 lines (the 100th) */
    for (int i = 0; i < 120; i++) {
        ico_no_shadows_forced(ICO_NOSH_FORCED_GRAB, 150);
    }
    CHECK(ico_no_shadows_lines() == 2 + 9, "%d lines", ico_no_shadows_lines());
    ico_no_shadows_forced(ICO_NOSH_FORCED_FIGHT, 46);
    ico_no_shadows_forced(ICO_NOSH_FORCED_FIGHT_JUN26, 46);
    ico_no_shadows_forced(ICO_NOSH_FORCED_COUNT, 1); /* not a kind: nothing */
    CHECK(ico_no_shadows_lines() == 13, "%d lines", ico_no_shadows_lines());
    s_on = 0;
}

static void testLog(void)
{
    captured(runOff);
    CHECK(s_log[0] == '\0', "lines with No Shadows off: %s", s_log);

    captured(runOn);
    CHECK(strstr(s_log, "no shadows: room 5: 2 Shadow(s) held back (1 placed in the room, 1 "
                        "waiting in its pits)") != NULL,
          "the room's line: %s", s_log);
    CHECK(strstr(s_log, "no shadows: room 5 left: 3 pit call(s) held at 0, 1 direct call(s), 0 "
                        "quick call(s) and 0 spider revive(s) refused\n") != NULL,
          "the room-left line: %s", s_log);
    CHECK(strstr(s_log, "room 6 left") == NULL, "a line for a room that refused nothing");
    CHECK(strstr(s_log, "no shadows: the forced grab of Yorda by Shadow 150 is skipped") != NULL);
    CHECK(strstr(s_log, "no shadows: the final fight (room 46) ends with no Shadows") != NULL);
    CHECK(strstr(s_log, "no shadows: the June 2001 demo's final fight (room 46, Shadows 2139 and "
                        "2140) ends with no Shadows") != NULL);
    printf("no_shadows_census_test: the log lines:\n%s", s_log);
}

int main(int argc, char **argv)
{
    const char *root = argc > 1 && argv[1][0] != '\0' ? argv[1] : ICO_SOURCE_DIR;

    testCensus(root);
    testHooks(root);
    testLog();
    return ico_check_summary("no_shadows_census_test");
}
