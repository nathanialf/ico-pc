/*
 * port/platform/test/backup_test.c
 *
 * ctest backup: Options > Extras' backup and restore (backup.h) on a user
 * folder of the test's own, backup_test.d, under argv[1] (the build folder;
 * not backup_test: that is the test program itself there):
 *   - the names a backup may hold (ico_backup_name_ok): its places, no
 *     "..", no absolute or drive path, no '\\', no empty part;
 *   - a backup holds the memory card folders, states/, config.toml and
 *     achievements.toml, and none of ico-pc.ini, logs/, dumps/, the
 *     textures, a slot's developer dump or a file still being written; its
 *     manifest comes last and says the version, platform, discs, time and
 *     the file count; a save state is stored, a text deflated;
 *   - inspect reads the manifest back and says when another disc made it;
 *   - a restore unpacks into the staging folder and changes nothing else;
 *     the next start's step puts it in place: every file byte-identical to
 *     the backed-up one, files added since gone, nothing the backup does
 *     not hold touched (ico-pc.ini, logs), no old copy left, the staging
 *     gone, and a second call finds nothing waiting;
 *   - refusals: a zip that is not a backup, a file that is not a zip, a
 *     newer format, an entry count the manifest does not give, a path that
 *     climbs out ("states/../../x"), an entry past the size bound, a busy
 *     memory card or save state write (nothing unpacked), nothing to back
 *     up;
 *   - a move that fails while the restore is put in place (each of the
 *     first moves in turn): every folder and file as it was, byte for
 *     byte, no old copy left, the staging deleted;
 *   - a move that fails and the move back of that thing too: the thing
 *     named with its old copy (the report's left), the copy holding the
 *     player's data, the rest as it was; the next start names the copy;
 *     an old copy of a place beside the data is named at every start
 *     (the report's stale), another name is not;
 *   - an unpacking a run left unfinished is deleted at the next start;
 *   - ico_remove_tree takes hidden files too; the suggested file name.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../test/ico_check.h"
#include "backup.h"
#include "host_fs.h"
#include "miniz.h"

static char s_root[1024];

static void at(char *out, size_t n, const char *rel)
{
    snprintf(out, n, "%s/%s", s_root, rel);
}

/* rel's folders made, then the file with text */
static void put(const char *rel, const char *text)
{
    char path[1200];
    at(path, sizeof(path), rel);
    for (char *p = path + strlen(s_root) + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            ico_mkdir(path);
            *p = '/';
        }
    }
    FILE *f = ico_fopen(path, "wb");
    CHECK(f != NULL, "cannot write %s", path);
    if (f != NULL) {
        fwrite(text, 1, strlen(text), f);
        fclose(f);
    }
}

/* the file's bytes as a string ("" when missing; NULL never) */
static const char *get(const char *rel)
{
    static char buf[8][4096];
    static int k;
    char path[1200];
    char *b = buf[k++ & 7];
    at(path, sizeof(path), rel);
    FILE *f = ico_fopen(path, "rb");
    size_t n = 0;
    if (f != NULL) {
        n = fread(b, 1, sizeof(buf[0]) - 1, f);
        fclose(f);
    }
    b[n] = '\0';
    return b;
}

static int there(const char *rel)
{
    char path[1200];
    at(path, sizeof(path), rel);
    return ico_path_kind(path, NULL, NULL) >= 0;
}

/* the player's folder as the tests start it */
static const char *const kKept[][2] = {
    {"user/memcard/BESCES-50760ico/BESCES-50760ico", "the game's save, a card block"},
    {"user/memcard/BESCES-50760ico/icon.sys", "icon"},
    {"user/memcard/_pcsx2_superblock", "superblock"},
    {"user/memcard-preview/BASLUS-20718ico/x", "preview save"},
    {"user/states/pal/slot1.ics", "ICS1 a packed state"},
    {"user/states/pal/slot1.png", "PNG picture"},
    {"user/states/jpn/slot2.ics", "ICS1 a Japanese state"},
    {"user/config.toml", "version = 1\n[game]\ndisc = \"pal\"\n"},
    {"user/achievements.toml", "[achievements]\nfirst = 1\n"},
};
static const char *const kLeft[][2] = {
    {"user/ico-pc.ini", "iso=D:/Ico_PAL.iso\n"},
    {"user/logs/ico-pc.log", "ico_pc: started\n"},
    {"user/dumps/frame-1.rddump", "dump"},
    {"user/textures/SCES-50760/replacements/a.png", "pack"},
    {"user/states/pal/dump-100-0/arena.bin", "dev dump"},
    {"user/states/pal/slot3.ics.tmp", "a write in flight"},
};

static void fill_user(void)
{
    char path[1200];
    at(path, sizeof(path), "user");
    ico_remove_tree(path);
    ico_mkdir(path);
    for (size_t i = 0; i < sizeof(kKept) / sizeof(kKept[0]); i++) {
        put(kKept[i][0], kKept[i][1]);
    }
    for (size_t i = 0; i < sizeof(kLeft) / sizeof(kLeft[0]); i++) {
        put(kLeft[i][0], kLeft[i][1]);
    }
}

static int kept_identical(void)
{
    int same = 1;
    for (size_t i = 0; i < sizeof(kKept) / sizeof(kKept[0]); i++) {
        if (strcmp(get(kKept[i][0]), kKept[i][1]) != 0) {
            fprintf(stderr, "backup_test: %s differs: \"%s\"\n", kKept[i][0], get(kKept[i][0]));
            same = 0;
        }
    }
    return same;
}

/* names in the user folder ending .old-... (an old copy left behind) */
static int s_olds;

static int old_visit(const char *name, int kind, void *user)
{
    (void)kind;
    (void)user;
    s_olds += strstr(name, ".old-") != NULL;
    return 0;
}

static int old_copies(void)
{
    char path[1200];
    at(path, sizeof(path), "user");
    s_olds = 0;
    ico_dir_entries(path, 1, old_visit, NULL);
    return s_olds;
}

static const IcoBackupInfo kInfo = {"v0.10.0", "abc123", "linux-x64", "pal", 1793530000ll};
static const IcoBackupGates kIdle = {0, 0, 0};

static void test_names(void)
{
    static const struct {
        const char *name;
        int folder, ok;
    } k[] = {
        {"states/pal/slot1.ics", 0, 1},
        {"memcard/BESCES-50760ico/icon.sys", 0, 1},
        {"memcard-preview/a", 0, 1},
        {"memcard2/a", 0, 1},
        {"config.toml", 0, 1},
        {"achievements.toml", 0, 1},
        {"backup.txt", 0, 1},
        {"states", 1, 1},
        {"memcard", 1, 1},
        {"states", 0, 0},
        {"ico-pc.ini", 0, 0},
        {"logs/ico-pc.log", 0, 0},
        {"memcards.x/a", 0, 0},
        {"../states/x", 0, 0},
        {"states/../../x", 0, 0},
        {"states/./x", 0, 0},
        {"states//x", 0, 0},
        {"states/x/", 0, 0},
        {"/states/x", 0, 0},
        {"C:/states/x", 0, 0},
        {"states\\x", 0, 0},
        {"states/a\nb", 0, 0},
        {"", 0, 0},
        {"states/a/b/c/d/e/f/g/h", 0, 0},
    };

    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        CHECK(ico_backup_name_ok(k[i].name, k[i].folder) == k[i].ok, "name \"%s\" (folder %d): %d",
              k[i].name, k[i].folder, ico_backup_name_ok(k[i].name, k[i].folder));
    }
}

/* the zip's entry names and their methods, in order, into out ("name:m;") */
static void zip_names(const char *zip, char *out, size_t n)
{
    mz_zip_archive z;
    memset(&z, 0, sizeof(z));
    out[0] = '\0';
    if (!mz_zip_reader_init_file(&z, zip, 0)) {
        return;
    }
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&z); i++) {
        mz_zip_archive_file_stat st;
        if (mz_zip_reader_file_stat(&z, i, &st)) {
            const size_t have = strlen(out);
            snprintf(out + have, n - have, "%s:%u;", st.m_filename, (unsigned)st.m_method);
        }
    }
    mz_zip_reader_end(&z);
}

/* a zip of the given entries (name, text pairs, NULL-ended) */
static void make_zip(const char *rel, const char *const *entries)
{
    char path[1200];
    mz_zip_archive z;
    at(path, sizeof(path), rel);
    memset(&z, 0, sizeof(z));
    CHECK(mz_zip_writer_init_file(&z, path, 0), "zip %s", rel);
    for (int i = 0; entries[i] != NULL; i += 2) {
        CHECK(mz_zip_writer_add_mem(&z, entries[i], entries[i + 1], strlen(entries[i + 1]),
                                    MZ_BEST_SPEED),
              "entry %s", entries[i]);
    }
    mz_zip_writer_finalize_archive(&z);
    mz_zip_writer_end(&z);
}

static IcoBackupResult inspect(const char *rel, IcoBackupManifest *m, IcoBackupReport *rep)
{
    char path[1200];
    at(path, sizeof(path), rel);
    return ico_backup_inspect(path, "pal", m, rep);
}

static void test_round_trip(void)
{
    char user[1200], zip[1200], names[2048];
    IcoBackupReport rep;
    IcoBackupManifest m;

    fill_user();
    at(user, sizeof(user), "user");
    at(zip, sizeof(zip), "out/ico-pc-backup.zip");
    {
        char out[1200];
        at(out, sizeof(out), "out");
        ico_remove_tree(out);
        ico_mkdir(out);
    }
    IcoBackupResult r = ico_backup_write(user, zip, &kInfo, &kIdle, NULL, NULL, &rep);
    CHECK(r == ICO_BACKUP_OK, "write: %s (%s)", ico_backup_result_text(r), rep.why);
    CHECK(rep.files == 9, "write: %d files", rep.files);
    CHECK(!there("out/ico-pc-backup.zip.part"), "write: no .part left");
    zip_names(zip, names, sizeof(names));
    CHECK(strcmp(names, "achievements.toml:8;config.toml:8;"
                        "memcard/BESCES-50760ico/BESCES-50760ico:8;"
                        "memcard/BESCES-50760ico/icon.sys:8;memcard/_pcsx2_superblock:8;"
                        "memcard-preview/BASLUS-20718ico/x:8;"
                        "states/jpn/slot2.ics:0;states/pal/slot1.ics:0;states/pal/slot1.png:0;"
                        "backup.txt:8;") == 0,
          "write: the entries \"%s\"", names);

    r = inspect("out/ico-pc-backup.zip", &m, &rep);
    CHECK(r == ICO_BACKUP_OK, "inspect: %s (%s)", ico_backup_result_text(r), rep.why);
    CHECK(m.format == 1 && strcmp(m.version, "v0.10.0") == 0 && strcmp(m.commit, "abc123") == 0 &&
              strcmp(m.platform, "linux-x64") == 0 && strcmp(m.disc, "pal") == 0 &&
              strcmp(m.discs, "pal jpn") == 0 && m.time == 1793530000ll && m.files == 9 &&
              !m.other_disc,
          "inspect: the manifest (%s %s %s disc %s discs \"%s\" %lld %d %d)", m.version, m.commit,
          m.platform, m.disc, m.discs, m.time, m.files, m.other_disc);
    CHECK(strcmp(m.items, "achievements.toml config.toml memcard memcard-preview states") == 0,
          "inspect: items \"%s\"", m.items);
    CHECK(ico_backup_inspect(zip, "jpn", &m, &rep) == ICO_BACKUP_OK && m.other_disc == 1,
          "inspect: another disc said");

    /* the player goes on: new data, a hidden file, then the restore */
    put("user/config.toml", "version = 1\n[game]\ndisc = \"jpn\"\n");
    put("user/memcard/BESCES-50760ico/BESCES-50760ico", "a later save");
    put("user/memcard/added.bin", "added after the backup");
    put("user/memcard/.hidden", "hidden");
    put("user/states/pal/slot4.ics", "a later state");

    IcoBackupGates busy = kIdle;
    busy.state_busy = 1;
    r = ico_backup_restore(zip, "ico-pc-backup.zip", user, "pal", &busy, NULL, NULL, &m, &rep);
    CHECK(r == ICO_BACKUP_BUSY && !there("user/" ICO_BACKUP_STAGING) &&
              !there("user/" ICO_BACKUP_STAGING_PART),
          "restore with a save state write pending: %s, nothing unpacked",
          ico_backup_result_text(r));
    busy = kIdle;
    busy.card_busy = 1;
    CHECK(ico_backup_restore(zip, "x", user, "pal", &busy, NULL, NULL, &m, &rep) == ICO_BACKUP_BUSY,
          "restore with the card busy refused");

    r = ico_backup_restore(zip, "ico-pc-backup.zip", user, "pal", &kIdle, NULL, NULL, &m, &rep);
    CHECK(r == ICO_BACKUP_OK && rep.files == 9, "restore: %s, %d files (%s)",
          ico_backup_result_text(r), rep.files, rep.why);
    CHECK(there("user/" ICO_BACKUP_STAGING "/states/pal/slot1.ics") &&
              !there("user/" ICO_BACKUP_STAGING_PART),
          "restore: unpacked into the staging folder");
    CHECK(strcmp(get("user/config.toml"), "version = 1\n[game]\ndisc = \"jpn\"\n") == 0 &&
              there("user/memcard/added.bin"),
          "restore: nothing in place changed yet");

    r = ico_backup_apply_pending(user, 1000, &m, &rep);
    CHECK(r == ICO_BACKUP_OK, "apply: %s (%s)", ico_backup_result_text(r), rep.why);
    CHECK(rep.files == 9 && strcmp(m.from, "ico-pc-backup.zip") == 0 &&
              strcmp(m.version, "v0.10.0") == 0,
          "apply: %d files from \"%s\" (%s)", rep.files, m.from, m.version);
    CHECK(kept_identical(), "apply: every file byte-identical to the backup's");
    CHECK(!there("user/memcard/added.bin") && !there("user/memcard/.hidden") &&
              !there("user/states/pal/slot4.ics"),
          "apply: what came after the backup is gone");
    for (size_t i = 0; i < sizeof(kLeft) / sizeof(kLeft[0]); i++) {
        const int inStates = strncmp(kLeft[i][0], "user/states/", 12) == 0;
        /* the states folder is the backup's now: its dumps and writes went
           with the old one */
        CHECK(inStates ? !there(kLeft[i][0]) : strcmp(get(kLeft[i][0]), kLeft[i][1]) == 0,
              "apply: %s %s", kLeft[i][0], inStates ? "went with the old states" : "untouched");
    }
    CHECK(old_copies() == 0 && !there("user/" ICO_BACKUP_STAGING),
          "apply: no old copy, no staging");
    CHECK(ico_backup_apply_pending(user, 1001, &m, &rep) == ICO_BACKUP_NONE,
          "apply again: nothing waits");
}

static void test_refusals(void)
{
    IcoBackupManifest m;
    IcoBackupReport rep;
    char user[1200], path[1200];
    static const char kMan1[] = "ICO PC backup 1\nversion v0.10.0\nplatform linux-x64\ndisc "
                                "pal\ndiscs pal\ntime 1\nfiles 1\nbytes 4\n";

    {
        static const char *const e[] = {"hello.txt", "hello", NULL};
        make_zip("out/plain.zip", e);
        CHECK(inspect("out/plain.zip", &m, &rep) == ICO_BACKUP_NOT_BACKUP,
              "a zip without the manifest is not a backup");
    }
    put("out/text.zip", "this is not a zip at all");
    CHECK(inspect("out/text.zip", &m, &rep) == ICO_BACKUP_NOT_BACKUP,
          "a text file is not a backup");
    {
        static const char *const e[] = {"backup.txt", "something else\nfiles 0\n", NULL};
        make_zip("out/other.zip", e);
        CHECK(inspect("out/other.zip", &m, &rep) == ICO_BACKUP_NOT_BACKUP,
              "another program's backup.txt is not ours");
    }
    {
        static const char *const e[] = {"backup.txt", "ICO PC backup 9\nfiles 0\nbytes 0\n", NULL};
        make_zip("out/newer.zip", e);
        CHECK(inspect("out/newer.zip", &m, &rep) == ICO_BACKUP_NEWER, "a newer format is refused");
    }
    {
        static const char *const e[] = {"backup.txt",        kMan1,   "config.toml", "a=1\n",
                                        "achievements.toml", "b=2\n", NULL};
        make_zip("out/count.zip", e);
        CHECK(inspect("out/count.zip", &m, &rep) == ICO_BACKUP_DAMAGED,
              "more entries than the manifest says: damaged (%s)", rep.why);
    }
    {
        static const char *const e[] = {"backup.txt", kMan1, "states/../../evil.txt", "evil", NULL};
        make_zip("out/climb.zip", e);
        CHECK(inspect("out/climb.zip", &m, &rep) == ICO_BACKUP_UNSAFE,
              "a path that climbs out is refused (%s)", rep.why);
        at(path, sizeof(path), "out/climb.zip");
        at(user, sizeof(user), "user");
        CHECK(ico_backup_restore(path, "climb.zip", user, "pal", &kIdle, NULL, NULL, &m, &rep) ==
                      ICO_BACKUP_UNSAFE &&
                  !there("evil.txt") && !there("user/evil.txt") &&
                  !there("user/" ICO_BACKUP_STAGING),
              "... and its restore writes nothing");
    }
    {
        static const char *const e[] = {"backup.txt", kMan1, "logs/ico-pc.log", "log", NULL};
        make_zip("out/logs.zip", e);
        CHECK(inspect("out/logs.zip", &m, &rep) == ICO_BACKUP_UNSAFE,
              "a file outside the places is refused");
    }
    {
        static const char *const e[] = {"backup.txt", kMan1, "config.toml", "abcd", NULL};
        make_zip("out/big.zip", e);
        CHECK(inspect("out/big.zip", &m, &rep) == ICO_BACKUP_OK, "a small backup reads");
        ico_backup_test_bounds(0, 3, 0);
        CHECK(inspect("out/big.zip", &m, &rep) == ICO_BACKUP_TOO_BIG,
              "an entry past the size bound is refused (%s)", rep.why);
        ico_backup_test_bounds(0, 0, 0);
    }
    {
        /* nothing to back up, and a backup past the bounds */
        char zip[1200];
        at(user, sizeof(user), "empty");
        ico_remove_tree(user);
        ico_mkdir(user);
        put("empty/ico-pc.ini", "iso=x\n");
        at(zip, sizeof(zip), "out/empty.zip");
        CHECK(ico_backup_write(user, zip, &kInfo, &kIdle, NULL, NULL, &rep) == ICO_BACKUP_NOTHING &&
                  !there("out/empty.zip"),
              "nothing to back up: no zip");
        fill_user();
        at(user, sizeof(user), "user");
        ico_backup_test_bounds(3, 0, 0);
        CHECK(ico_backup_write(user, zip, &kInfo, &kIdle, NULL, NULL, &rep) == ICO_BACKUP_TOO_BIG &&
                  !there("out/empty.zip"),
              "past the file count bound: no zip");
        ico_backup_test_bounds(0, 0, 0);
        IcoBackupGates busy = kIdle;
        busy.card_busy = 1;
        CHECK(ico_backup_write(user, zip, &kInfo, &busy, NULL, NULL, &rep) == ICO_BACKUP_BUSY,
              "the card busy: no backup");
    }
}

/* a restore whose nth move fails leaves the folder as it was */
static void test_failed_apply(void)
{
    char user[1200], zip[1200];
    IcoBackupReport rep;
    IcoBackupManifest m;

    fill_user();
    at(user, sizeof(user), "user");
    at(zip, sizeof(zip), "out/second.zip");
    CHECK(ico_backup_write(user, zip, &kInfo, &kIdle, NULL, NULL, &rep) == ICO_BACKUP_OK,
          "second backup");
    /* five things move, two moves each: fail each of the first eight */
    for (int nth = 1; nth <= 8; nth++) {
        static const char *const kNow[][2] = {
            {"user/config.toml", "now"},
            {"user/achievements.toml", "now too"},
            {"user/memcard/BESCES-50760ico/BESCES-50760ico", "a later save"},
            {"user/memcard/.hidden", "hidden"},
            {"user/states/pal/slot1.ics", "a later state"},
        };
        for (size_t i = 0; i < sizeof(kNow) / sizeof(kNow[0]); i++) {
            put(kNow[i][0], kNow[i][1]);
        }
        CHECK(ico_backup_restore(zip, "second.zip", user, "pal", &kIdle, NULL, NULL, &m, &rep) ==
                  ICO_BACKUP_OK,
              "move %d: staged", nth);
        ico_backup_test_fail_move(nth);
        const IcoBackupResult r = ico_backup_apply_pending(user, 2000 + nth, &m, &rep);
        ico_backup_test_fail_move(0);
        CHECK(r == ICO_BACKUP_WRITE && rep.why[0] != '\0', "move %d fails: %s (%s)", nth,
              ico_backup_result_text(r), rep.why);
        for (size_t i = 0; i < sizeof(kNow) / sizeof(kNow[0]); i++) {
            CHECK(strcmp(get(kNow[i][0]), kNow[i][1]) == 0, "move %d: %s as it was (\"%s\")", nth,
                  kNow[i][0], get(kNow[i][0]));
        }
        CHECK(strcmp(get("user/memcard/BESCES-50760ico/icon.sys"), "icon") == 0 &&
                  strcmp(get("user/memcard-preview/BASLUS-20718ico/x"), "preview save") == 0 &&
                  strcmp(get("user/states/jpn/slot2.ics"), "ICS1 a Japanese state") == 0 &&
                  strcmp(get("user/ico-pc.ini"), "iso=D:/Ico_PAL.iso\n") == 0,
              "move %d: the rest as it was", nth);
        CHECK(old_copies() == 0 && !there("user/" ICO_BACKUP_STAGING),
              "move %d: no old copy, the staging deleted", nth);
    }
}

/* a move that fails and the move back of that thing failing too (moves 2
   and 3: the first thing's own copy moved aside, the backup's not moved in,
   the copy not moved back): that thing is named with its old copy, which
   holds the player's data, and the rest is as it was; the next start names
   the old copy; an old copy left beside the data is named at every start,
   other names are not */
static void test_failed_undo(void)
{
    char user[1200], zip[1200], name[64], oldName[128], rel[300];
    IcoBackupReport rep;
    IcoBackupManifest m;

    fill_user();
    at(user, sizeof(user), "user");
    at(zip, sizeof(zip), "out/undo.zip");
    CHECK(ico_backup_write(user, zip, &kInfo, &kIdle, NULL, NULL, &rep) == ICO_BACKUP_OK,
          "undo: the backup");
    put("user/config.toml", "a later config");
    CHECK(ico_backup_restore(zip, "undo.zip", user, "pal", &kIdle, NULL, NULL, &m, &rep) ==
              ICO_BACKUP_OK,
          "undo: staged");
    ico_backup_test_fail_move(2);
    ico_backup_test_fail_also(3);
    const IcoBackupResult r = ico_backup_apply_pending(user, 4000, &m, &rep);
    ico_backup_test_fail_move(0);
    name[0] = oldName[0] = '\0';
    CHECK(r == ICO_BACKUP_WRITE &&
              sscanf(rep.left, "%63s (yours is in %127[^)])", name, oldName) == 2,
          "undo: the thing not moved back named with its copy (\"%s\")", rep.left);
    CHECK(strchr(rep.left, ',') == NULL, "undo: one thing left (\"%s\")", rep.left);
    snprintf(rel, sizeof(rel), "user/%s", name);
    CHECK(name[0] != '\0' && !there(rel) && old_copies() == 1 && !there("user/" ICO_BACKUP_STAGING),
          "undo: %s missing, its one old copy kept, the staging deleted", name);
    /* the old copy holds the player's data; everything else as it was */
    const size_t nl = strlen(name);
    for (size_t i = 0; i < sizeof(kKept) / sizeof(kKept[0]); i++) {
        const char *path = kKept[i][0] + 5; /* after "user/" */
        const char *want = strcmp(path, "config.toml") == 0 ? "a later config" : kKept[i][1];
        if (nl > 0 && strncmp(path, name, nl) == 0 && (path[nl] == '\0' || path[nl] == '/')) {
            snprintf(rel, sizeof(rel), "user/%s%s", oldName, path + nl);
        } else {
            snprintf(rel, sizeof(rel), "user/%s", path);
        }
        CHECK(strcmp(get(rel), want) == 0, "undo: %s holds \"%s\" (\"%s\")", rel, want, get(rel));
    }
    /* the next start: nothing waits, the old copy named */
    CHECK(ico_backup_apply_pending(user, 4001, &m, &rep) == ICO_BACKUP_NONE &&
              strcmp(rep.stale, oldName) == 0,
          "undo: the next start names the old copy (\"%s\")", rep.stale);
    /* an old copy of a place beside the data is named; another name is not */
    fill_user();
    put("user/memcard.old-123/x", "an old card");
    put("user/notes.old-5", "not ours");
    CHECK(ico_backup_apply_pending(user, 4002, &m, &rep) == ICO_BACKUP_NONE &&
              strcmp(rep.stale, "memcard.old-123") == 0,
          "an old copy beside the data named at the start (\"%s\")", rep.stale);
}

static void test_leftover_and_tree(void)
{
    char user[1200], path[1200], name[64];
    IcoBackupReport rep;
    IcoBackupManifest m;

    fill_user();
    at(user, sizeof(user), "user");
    put("user/" ICO_BACKUP_STAGING_PART "/states/pal/slot1.ics", "half");
    CHECK(ico_backup_apply_pending(user, 3000, &m, &rep) == ICO_BACKUP_NONE && rep.leftover == 1 &&
              !there("user/" ICO_BACKUP_STAGING_PART),
          "an unfinished unpacking is deleted at the start");
    CHECK(kept_identical(), "... and nothing else changes");

    put("tree/a/.dot/b.txt", "b");
    put("tree/.top", "t");
    at(path, sizeof(path), "tree");
    CHECK(ico_remove_tree(path) == 0 && !there("tree"), "ico_remove_tree takes hidden files");
    CHECK(ico_remove_tree(path) == 0, "ico_remove_tree of nothing is 0");

    ico_backup_default_name(name, sizeof(name), 1793530000ll);
    CHECK(strncmp(name, "ico-pc-backup-20", 16) == 0 && strlen(name) == 28 &&
              strcmp(name + 24, ".zip") == 0,
          "the suggested name \"%s\"", name);
}

int main(int argc, char **argv)
{
    snprintf(s_root, sizeof(s_root), "%s/backup_test.d", argc > 1 ? argv[1] : ".");
    ico_remove_tree(s_root);
    ico_mkdir(s_root);
    test_names();
    test_round_trip();
    test_refusals();
    test_failed_apply();
    test_failed_undo();
    test_leftover_and_tree();
    if (ico_check_failures == 0) {
        ico_remove_tree(s_root);
    }
    return ico_check_summary("backup");
}
