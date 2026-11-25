/*
 * port/platform/test/state_legacy_test.c
 *
 * The committed legacy tables (port/state_legacy, issue 217) read by the
 * loader the game uses (state_legacy.c): every table the index names loads,
 * its counts are its manifest's, its code hashes lie in address order under
 * the current code hash rules, and its symbol file places at an image the
 * way an earlier version's state gives one.  The index's lookup by
 * platform and commit (the release, the rc, another platform, an unknown
 * commit), a table that is missing a file, one whose file is cut short, and
 * one of the first format (no text_index line: its runs do not say where a
 * literal starts), refused by name.
 * The argument is the tables' folder in the source tree; no build output,
 * no game.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../test/ico_check.h"
#include "code_hash.h"
#include "state_legacy.h"
#include "state_syms.h"

typedef struct Dir {
    const char *root;
    const char *drop; /* a path the reader says is missing, or NULL */
    const char *cut;  /* a path the reader returns half of, or NULL */
    int apk;          /* as Android's APK: no asset ends in .gz (packaging un-gzips them) */
    int old_index;    /* every manifest.txt without its text_index line (the first format) */
} Dir;

static int dir_read(void *user, const char *path, void **data, size_t *n, char *err, size_t err_n)
{
    const Dir *d = (const Dir *)user;
    char full[1024];
    FILE *f;
    long len;
    void *b;

    if (d->drop != NULL && strcmp(path, d->drop) == 0) {
        snprintf(err, err_n, "dropped by the test");
        return -1;
    }
    if (d->apk && strlen(path) > 3 && strcmp(path + strlen(path) - 3, ".gz") == 0) {
        snprintf(err, err_n, "an APK holds no .gz asset (its packaging un-gzips and renames it)");
        return -1;
    }
    snprintf(full, sizeof full, "%s/%s", d->root, path);
    f = fopen(full, "rb");
    if (f == NULL) {
        snprintf(err, err_n, "%s", strerror(errno));
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    b = malloc(len > 0 ? (size_t)len : 1);
    if (b == NULL || fread(b, 1, (size_t)len, f) != (size_t)len) {
        free(b);
        fclose(f);
        return -1;
    }
    fclose(f);
    *data = b;
    *n = (size_t)len;
    if (d->cut != NULL && strcmp(path, d->cut) == 0) {
        *n = (size_t)len / 2;
    }
    if (d->old_index && strlen(path) > 13 &&
        strcmp(path + strlen(path) - 13, "/manifest.txt") == 0) {
        char *s = (char *)b, *l = NULL, *e;
        size_t i;

        for (i = 0; i + 11 <= *n; i++) {
            if ((i == 0 || s[i - 1] == '\n') && memcmp(s + i, "text_index ", 11) == 0) {
                l = s + i;
                break;
            }
        }
        if (l != NULL) {
            e = (char *)memchr(l, '\n', *n - (size_t)(l - s));
            e = e != NULL ? e + 1 : s + *n;
            memmove(l, e, *n - (size_t)(e - s));
            *n -= (size_t)(e - l);
        }
    }
    return 0;
}

/* the manifest's value of `key` (its first number), or -1 */
static long manifest_num(const Dir *d, const char *name, const char *key)
{
    char path[256];
    void *m = NULL;
    size_t n = 0;
    long v = -1;
    char *t, *p;

    snprintf(path, sizeof path, "%s/manifest.txt", name);
    char err[160];

    if (dir_read((void *)d, path, &m, &n, err, sizeof err) != 0) {
        return -1;
    }
    t = (char *)malloc(n + 1);
    if (t != NULL) {
        memcpy(t, m, n);
        t[n] = '\0';
        for (p = strtok(t, "\n"); p != NULL; p = strtok(NULL, "\n")) {
            if (strncmp(p, key, strlen(key)) == 0 && p[strlen(key)] == ' ') {
                v = strtol(p + strlen(key) + 1, NULL, 10);
            }
        }
    }
    free(t);
    free(m);
    return v;
}

/* every function the version's symbol file cuts (state_syms.c, as the
   loader places it at an image) has its row in the table's code hashes */
/* 1 when the manifest has a line starting with `key` and a space */
static int manifest_has(const Dir *d, const char *name, const char *key)
{
    char path[256], err[160];
    void *m = NULL;
    size_t n = 0, i, k = strlen(key);
    int hit = 0;

    snprintf(path, sizeof path, "%s/manifest.txt", name);
    if (dir_read((void *)d, path, &m, &n, err, sizeof err) != 0) {
        return 0;
    }
    for (i = 0; i + k < n && !hit; i++) {
        hit = (i == 0 || ((char *)m)[i - 1] == '\n') && memcmp((char *)m + i, key, k) == 0 &&
              ((char *)m)[i + k] == ' ';
    }
    free(m);
    return hit;
}

/* its ADRPs' verdicts (carry.z): loaded, a row per code hash, a judged row
   answering for its own count of ADRPs and refusing another */
static void check_carry(const char *name, const IcoLegacyTable *t)
{
    size_t i, judged = 0, with = 0;

    CHECK(t->carry != NULL && t->carry_n != NULL && t->carry_at != NULL,
          "%s: its ADRPs' verdicts (carry.z) load", name);
    if (t->carry == NULL) {
        return;
    }
    for (i = 0; i < t->n_hash; i++) {
        judged += t->carry_n[i] != ICO_LEGACY_UNJUDGED;
        if (t->carry_n[i] != ICO_LEGACY_UNJUDGED && t->carry_n[i] > 0) {
            const int c = ico_legacy_carried(t, t->hash[3 * i], 0, t->carry_n[i]);

            with += c == 0 || c == 1;
            CHECK(ico_legacy_carried(t, t->hash[3 * i], 0, t->carry_n[i] + 1u) == -1,
                  "%s: another count of ADRPs is not judged", name);
        }
    }
    CHECK(judged + 2 >= t->n_hash && with > 1000, "%s: %zu of %zu functions judged, %zu with ADRPs",
          name, judged, t->n_hash, with);
}

static void check_rows(const char *name, const IcoLegacyTable *t)
{
    IcoSymTabEnv env;
    IcoSymTab *st;
    char why[200];
    size_t i;

    /* placed where an Android state's ranges put an image */
    memset(&env, 0, sizeof env);
    env.image_lo = 0x0000007768720000ull;
    env.image_hi = env.image_lo + 0x3000000u;
    env.text_lo = env.image_lo;
    env.text_hi = env.image_hi;
    env.arch = ICO_CODE_A64;
    st = ico_legacy_symtab(t, &env, why, sizeof why);
    CHECK(st != NULL && st->n_unit > 10000 && st->code_from,
          "%s: its symbol file places at an image (%s)", name, st != NULL ? "" : why);
    if (st != NULL) {
        size_t hit = 0;

        for (i = 0; i < st->n_unit; i++) {
            const uint64_t off = st->unit[i].start - env.image_lo;
            size_t a = 0, b = t->n_hash;

            while (a < b) {
                const size_t mid = a + (b - a) / 2;

                if (t->hash[3 * mid] < off) {
                    a = mid + 1;
                } else {
                    b = mid;
                }
            }
            hit += a < t->n_hash && t->hash[3 * a] == off;
        }
        CHECK(hit == st->n_unit, "%s: every function the table cuts has its hashes (%zu of %zu)",
              name, hit, st->n_unit);
    }
    ico_symtab_free(st);
}

/* a table of format 3 records (v0.11.9 and later): its code hashes alone,
   under the records' rules 2 and the current rules; where it keeps its
   symbol file (the newest release's, which the package gate reads), that
   file cuts every function it hashes */
static void check_table3(const Dir *d, const char *name, const char *commit)
{
    char why[200], found[64], path[128];
    IcoLegacyTable *t;
    void *gz = NULL;
    size_t gz_n = 0, i, differ = 0;
    int sorted = 1;

    CHECK(ico_legacy_find(dir_read, (void *)d, "android-arm64", commit, found, sizeof found) == 1 &&
              strcmp(found, name) == 0,
          "%s: the index finds it by %s", name, commit);
    t = ico_legacy_load(dir_read, (void *)d, name, why, sizeof why);
    CHECK(t != NULL, "%s loads: %s", name, t != NULL ? "" : why);
    if (t == NULL) {
        return;
    }
    CHECK(t->record == 3 && t->verify_rules == 2 && t->hash_rules == ICO_CODE_HASH_RULES &&
              t->n_run == 0 && t->syms == NULL,
          "%s: format 3 records' hashes, rules %d and %d, no texts", name, t->verify_rules,
          t->hash_rules);
    CHECK((long)t->n_hash == manifest_num(d, name, "hashes") && t->n_hash > 10000,
          "%s: %zu code hashes, as its manifest says", name, t->n_hash);
    for (i = 1; i < t->n_hash; i++) {
        sorted &= t->hash[3 * i] > t->hash[3 * (i - 1)];
    }
    for (i = 0; i < t->n_hash; i++) {
        differ += t->hash[3 * i + 1] != t->hash[3 * i + 2];
    }
    CHECK(sorted, "%s: the code hashes in address order", name);
    check_carry(name, t);
    CHECK(differ > 0 && differ < t->n_hash / 100,
          "%s: %zu functions hash apart under rules 2 and 3 (a few: the rules changed little)",
          name, differ);
    snprintf(path, sizeof path, "%s/" ICO_LEGACY_SYMS, name);
    if (!manifest_has(d, name, "file " ICO_LEGACY_SYMS)) {
        /* a table of the code hashes alone (every one but the newest release's) */
    } else if (dir_read((void *)d, path, &gz, &gz_n, why, sizeof why) == 0) {
        size_t n = 0;
        char *txt = ico_symtab_gunzip(gz, gz_n, &n);

        t->syms = txt;
        t->syms_n = n;
        CHECK(txt != NULL, "%s: its symbol file inflates", name);
        if (txt != NULL) {
            check_rows(name, t); /* t->syms goes with the table */
        }
    } else {
        CHECK(0, "%s: its symbol file: %s", name, why);
    }
    printf("state_legacy: %s: format 3 records, %zu code hashes, %zu apart under rules 2 and %d\n",
           name, t->n_hash, differ, ICO_CODE_HASH_RULES);
    free(gz);
    ico_legacy_free(t);
}

static void check_table(const Dir *d, const char *name, const char *platform, const char *commit)
{
    char why[200], found[64];
    IcoLegacyTable *t;
    size_t i;
    int sorted = 1;

    CHECK(ico_legacy_find(dir_read, (void *)d, platform, commit, found, sizeof found) == 1 &&
              strcmp(found, name) == 0,
          "%s: the index finds it by %s %s", name, platform, commit);
    t = ico_legacy_load(dir_read, (void *)d, name, why, sizeof why);
    CHECK(t != NULL, "%s loads: %s", name, t != NULL ? "" : why);
    if (t == NULL) {
        return;
    }
    CHECK(strcmp(t->commit, commit) == 0, "%s: its manifest's commit %s", name, t->commit);
    CHECK((long)t->n_hash == manifest_num(d, name, "hashes") && t->n_hash > 1000,
          "%s: %zu code hashes, as its manifest says", name, t->n_hash);
    CHECK((long)t->n_run == manifest_num(d, name, "texts") && t->n_run > 1000,
          "%s: %zu text runs, as its manifest says", name, t->n_run);
    CHECK(t->record == 2 && t->verify_rules == 1 &&
              (t->hash_rules == ICO_CODE_HASH_RULES ||
               (t->hash_rules == 2 && manifest_has(d, name, "hash_rules_kept"))),
          "%s: format 2 records' hashes, rules 1 and %d (an earlier rules' table says why it "
          "was kept)",
          name, t->hash_rules);
    for (i = 1; i < t->n_hash; i++) {
        sorted &= t->hash[3 * i] > t->hash[3 * (i - 1)];
    }
    CHECK(sorted, "%s: the code hashes in address order", name);
    CHECK(t->ro_hi > t->ro_lo && t->n_run > 0 && t->run[0] >= t->ro_lo &&
              (uint64_t)t->run[2 * (t->n_run - 1)] + t->run[2 * (t->n_run - 1) + 1] <= t->ro_hi,
          "%s: its text runs inside its .rodata", name);
    {
        uint64_t lo = 0, hi = 0;
        const uint32_t off = t->run[2 * (t->n_run / 2)];
        int ls = -1;
        long nls = 0;

        CHECK(ico_legacy_run(t, off, &lo, &hi, &ls) == 1 && lo == off &&
                  t->ro[off - t->ro_lo + (hi - lo) - 1] == 0 && ls == t->lstart[t->n_run / 2],
              "%s: a run found by its start ends in a zero byte, its literal-start flag given",
              name);
        for (i = 0; i < t->n_run; i++) {
            nls += t->lstart[i] != 0;
        }
        CHECK(nls == manifest_num(d, name, "lstarts") && nls > 0 && nls < (long)t->n_run,
              "%s: %ld runs start a literal, as its manifest says", name, nls);
    }
    check_carry(name, t);
    check_rows(name, t);
    ico_legacy_free(t);
}

int main(int argc, char **argv)
{
    Dir d;
    char why[200], found[64];
    IcoLegacyTable *t;

    if (argc < 2) {
        fprintf(stderr, "usage: state_legacy_test <port/state_legacy>\n");
        return 2;
    }
    memset(&d, 0, sizeof d);
    d.root = argv[1];
    d.apk = 1; /* every table loads by the names an APK keeps */
    check_table(&d, "v0.11.7-android", "android-arm64", "v0.11.6-183-g66d9d0dcba");
    check_table(&d, "v0.11.8-android", "android-arm64", "v0.11.7-68-gaeb00e6100");
    check_table(&d, "v0.11.8-rc4-android", "android-arm64", "v0.11.7-66-gaf8507442c");
    check_table3(&d, "v0.11.9-android", "v0.11.8-68-g96fd7c099a");
    check_table3(&d, "v0.11.10-android", "v0.11.9-6-ge9bcc9d3ef");
    check_table3(&d, "v0.11.11-android", "v0.11.10-23-g4832c07665");
    check_table3(&d, "v0.11.12-android", "v0.11.11-29-ga42959af67");
    check_table3(&d, "v0.11.13-android", "v0.11.12-69-g0c585e991a");
    check_table3(&d, "v0.11.14-android", "v0.11.13-42-g0aaa7ac704");
    check_table3(&d, "v0.11.15-rc6-android", "v0.11.14-58-g5ccc7f8365");
    check_table3(&d, "v0.11.15-android", "v0.11.14-94-g5396fcfcd2");
    /* no table: another platform's, an unknown commit, no index */
    CHECK(ico_legacy_find(dir_read, &d, "windows-x64", "v0.11.7-68-gaeb00e6100", found,
                          sizeof found) == 0,
          "no table for another platform");
    CHECK(ico_legacy_find(dir_read, &d, "android-arm64", "v0.11.4-211-g194e7bbe41", found,
                          sizeof found) == 0,
          "no table for the reporter's v0.11.5 (another layout)");
    d.drop = "index.txt";
    CHECK(ico_legacy_find(dir_read, &d, "android-arm64", "v0.11.7-68-gaeb00e6100", found,
                          sizeof found) == -1,
          "no index: -1");
    /* a missing file and one cut short */
    d.drop = "v0.11.8-android/hashes.z";
    t = ico_legacy_load(dir_read, &d, "v0.11.8-android", why, sizeof why);
    CHECK(t == NULL && strstr(why, "v0.11.8-android/hashes.z") != NULL &&
              strstr(why, "dropped by the test") != NULL,
          "no hashes file, named with the reader's reason: %s", why);
    ico_legacy_free(t);
    d.drop = "v0.11.8-android/sections.txt";
    t = ico_legacy_load(dir_read, &d, "v0.11.8-android", why, sizeof why);
    CHECK(t == NULL && strstr(why, "cannot read v0.11.8-android/sections.txt: dropped") != NULL,
          "no sections file, named: %s", why);
    ico_legacy_free(t);
    d.drop = NULL;
    d.cut = "v0.11.8-android/texts.1.z";
    t = ico_legacy_load(dir_read, &d, "v0.11.8-android", why, sizeof why);
    CHECK(t == NULL, "a text file cut short: %s", t == NULL ? why : "loaded");
    ico_legacy_free(t);
    d.cut = NULL;
    d.old_index = 1;
    t = ico_legacy_load(dir_read, &d, "v0.11.8-android", why, sizeof why);
    CHECK(t == NULL && strstr(why, "text index is of format 1, not 2") != NULL &&
              strstr(why, "rebuilt") != NULL,
          "a table of the first format (no text_index): refused, named: %s",
          t == NULL ? why : "loaded");
    ico_legacy_free(t);
    return ico_check_summary("state_legacy");
}
