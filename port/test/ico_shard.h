/*
 * port/test/ico_shard.h
 *
 * Splits one slow test program into N ctest entries ("name.1of4" ... "name.4of4")
 * that together run every case exactly once.
 *
 * The program lists its cases once, in the order main() runs them, each with a
 * weight (its measured seconds, rounded):
 *
 *   static const IcoShardCase k_cases[] = {{"effects", 20}, {"aura", 8}, ...};
 *
 * and main() starts with
 *
 *   if (ico_shard_start(argc, argv, k_cases, ICO_SHARD_COUNT(k_cases))) return ico_shard_exit();
 *
 * (that handles --list-cases) and wraps each case:
 *
 *   if (ico_shard_take("effects")) { checkEffects(); }
 *
 * Which shard owns a case is a pure function of the table and N: the cases go,
 * heaviest first (ties by position), to the shard with the least weight so far.
 * A shard with no --shard argument owns every case, so the program still runs
 * whole by hand. --only <name> runs that one case alone (a twin of one case under
 * another environment); the others are passed over as another shard's. ico_shard_take() stops the program when the call does not match
 * the table's next entry, and ico_shard_finish() fails when a case of the table
 * was never reached, so the table cannot drift from main(). --list-cases prints
 * "<shard> <name>" per case for the cmake check (cmake/IcoShardCheck.cmake) that
 * the shards' union is the table and no case is in two shards.
 *
 * Setup that every case needs stays outside ico_shard_take(), so every shard
 * repeats it: keep it small.
 */
#ifndef ICO_PORT_TEST_ICO_SHARD_H
#define ICO_PORT_TEST_ICO_SHARD_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    const char *name;
    int weight; /* measured seconds */
} IcoShardCase;

#define ICO_SHARD_COUNT(t) ((int)(sizeof(t) / sizeof((t)[0])))
#define ICO_SHARD_MAX 64

static const IcoShardCase *ico_shard_cases;
static int ico_shard_ncases;
static int ico_shard_k = 1, ico_shard_n = 1; /* this shard is k of n */
static int ico_shard_owner[ICO_SHARD_MAX];   /* 1-based shard of each case */
static int ico_shard_next;                   /* the table entry main() reaches next */
static int ico_shard_ran;
static int ico_shard_list;
static double ico_shard_t0;
static const char *ico_shard_cur;
static const char *ico_shard_only; /* --only <name>: that case alone */

static inline double ico_shard_now(void)
{
#if defined(_WIN32)
    /* the mingw gcc toolchain's time.h has no timespec_get/TIME_UTC */
    return (double)clock() / (double)CLOCKS_PER_SEC;
#else
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

static inline void ico_shard_assign(void)
{
    int load[ICO_SHARD_MAX] = {0};
    int done[ICO_SHARD_MAX] = {0};

    for (int round = 0; round < ico_shard_ncases; round++) {
        int best = -1;
        for (int i = 0; i < ico_shard_ncases; i++) {
            if (!done[i] &&
                (best < 0 || ico_shard_cases[i].weight > ico_shard_cases[best].weight)) {
                best = i;
            }
        }
        int s = 0;
        for (int j = 1; j < ico_shard_n; j++) {
            if (load[j] < load[s]) {
                s = j;
            }
        }
        done[best] = 1;
        load[s] += ico_shard_cases[best].weight;
        ico_shard_owner[best] = s + 1;
    }
}

/* Returns 1 when the program has nothing more to do (--list-cases or a bad
   argument): return ico_shard_exit(). */
static inline int ico_shard_start(int argc, char **argv, const IcoShardCase *cases, int ncases)
{
    ico_shard_cases = cases;
    ico_shard_ncases = ncases;
    if (ncases > ICO_SHARD_MAX) {
        fprintf(stderr, "ico_shard: more than %d cases\n", ICO_SHARD_MAX);
        ico_shard_list = 2;
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--shard") == 0 && i + 1 < argc) {
            int k = 0, n = 0;
            if (sscanf(argv[++i], "%d/%d", &k, &n) != 2 || n < 1 || k < 1 || k > n ||
                n > ICO_SHARD_MAX) {
                fprintf(stderr, "ico_shard: --shard wants k/N with 1 <= k <= N\n");
                ico_shard_list = 2;
                return 1;
            }
            ico_shard_k = k;
            ico_shard_n = n;
        } else if (strcmp(argv[i], "--list-cases") == 0) {
            ico_shard_list = 1;
        } else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
            ico_shard_only = argv[++i];
        }
    }
    ico_shard_assign();
    if (ico_shard_list == 1) {
        for (int i = 0; i < ncases; i++) {
            printf("%d %s\n", ico_shard_owner[i], cases[i].name);
        }
        return 1;
    }
    if (ico_shard_only) {
        int known = 0;
        for (int i = 0; i < ncases; i++) {
            known |= strcmp(cases[i].name, ico_shard_only) == 0;
        }
        if (!known) {
            fprintf(stderr, "ico_shard: --only \"%s\" is not a case of the table\n",
                    ico_shard_only);
            ico_shard_list = 2;
            return 1;
        }
        printf("the case %s alone\n", ico_shard_only);
    } else if (ico_shard_n > 1) {
        printf("shard %d of %d\n", ico_shard_k, ico_shard_n);
    }
    return 0;
}

static inline int ico_shard_exit(void)
{
    return ico_shard_list == 1 ? 0 : 2;
}

static inline void ico_shard_end_case(void)
{
    if (ico_shard_cur) {
        printf("  case %s: %.1f s\n", ico_shard_cur, ico_shard_now() - ico_shard_t0);
        fflush(stdout);
        ico_shard_cur = NULL;
    }
}

/* 1 when this shard runs the case (the next of the table, named so). */
static inline int ico_shard_take(const char *name)
{
    ico_shard_end_case();
    if (ico_shard_next >= ico_shard_ncases ||
        strcmp(ico_shard_cases[ico_shard_next].name, name) != 0) {
        fprintf(stderr, "ico_shard: main() reached \"%s\" where the table has \"%s\"\n", name,
                ico_shard_next < ico_shard_ncases ? ico_shard_cases[ico_shard_next].name
                                                  : "(its end)");
        exit(1);
    }
    const int mine = ico_shard_only ? strcmp(name, ico_shard_only) == 0
                                    : ico_shard_owner[ico_shard_next] == ico_shard_k;
    ico_shard_next++;
    if (mine) {
        ico_shard_ran++;
        ico_shard_cur = name;
        ico_shard_t0 = ico_shard_now();
    }
    return mine;
}

/* 0 when every case of the table was reached; call before the last verdict. */
static inline int ico_shard_finish(void)
{
    ico_shard_end_case();
    if (ico_shard_next != ico_shard_ncases) {
        fprintf(stderr, "ico_shard: main() stopped before \"%s\"\n",
                ico_shard_cases[ico_shard_next].name);
        return 1;
    }
    printf("shard %d of %d: ran %d of %d cases\n", ico_shard_k, ico_shard_n, ico_shard_ran,
           ico_shard_ncases);
    return 0;
}

#endif
