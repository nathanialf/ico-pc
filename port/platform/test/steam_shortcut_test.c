/*
 * port/platform/test/steam_shortcut_test.c
 *
 * Add to Steam (port/platform/steam_shortcut.c): the CRC and the shortcut's
 * id; a sample shortcuts.vdf built here, read, and appended to with every
 * existing byte kept and the new entry's bytes as Steam writes them; the
 * duplicate check; files that are not shortcuts files refused; loginusers.vdf
 * read for the most recent user; the whole job over a fake Steam folder,
 * including the refusal while Steam runs (a pid file, and stand-ins for the
 * registry's ActiveProcess pid and the steam.exe process check) and the
 * .bak copy; the program path the shortcut records, the AppImage file when
 * run from one.
 *
 * argv[1]: a folder the test may write in (the build folder).
 */
#include "steam_shortcut.h"
#include "host_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* --- a byte buffer to build samples with -------------------------------------- */

typedef struct B {
    uint8_t d[4096];
    size_t n;
} B;

static void put(B *b, const void *p, size_t n)
{
    memcpy(b->d + b->n, p, n);
    b->n += n;
}

static void cs(B *b, const char *s)
{
    put(b, s, strlen(s) + 1);
}

static void byte(B *b, int v)
{
    b->d[b->n++] = (uint8_t)v;
}

static void s_field(B *b, const char *k, const char *v)
{
    byte(b, 1);
    cs(b, k);
    cs(b, v);
}

static void i_field(B *b, const char *k, uint32_t v)
{
    const uint8_t le[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};

    byte(b, 2);
    cs(b, k);
    put(b, le, 4);
}

/* an entry as an older Steam wrote it: fewer fields, a tag */
static void old_entry(B *b, const char *idx, const char *name, const char *exe)
{
    byte(b, 0);
    cs(b, idx);
    i_field(b, "appid", 0xDEADBEEFu);
    s_field(b, "appname", name);
    s_field(b, "exe", exe);
    s_field(b, "StartDir", "\"/home/n/\"");
    s_field(b, "icon", "");
    i_field(b, "IsHidden", 0);
    byte(b, 0);
    cs(b, "tags");
    s_field(b, "0", "favorite");
    byte(b, 8);
    byte(b, 8);
}

/* the file around a list of entries */
static void file_open(B *b)
{
    byte(b, 0);
    cs(b, "shortcuts");
}

static void file_close(B *b)
{
    byte(b, 8);
    byte(b, 8);
}

static void sample(B *b)
{
    b->n = 0;
    file_open(b);
    old_entry(b, "0", "Some Game", "\"/usr/bin/some game\"");
    old_entry(b, "1", "Other", "\"/opt/other/run.sh\"");
    file_close(b);
}

/* the entry this module writes, built here from the format */
static void new_entry(B *b, const char *idx, const char *name, const char *exe, const char *dir,
                      const char *icon)
{
    char qe[600], qd[600];

    snprintf(qe, sizeof(qe), "\"%s\"", exe);
    snprintf(qd, sizeof(qd), "\"%s\"", dir);
    byte(b, 0);
    cs(b, idx);
    i_field(b, "appid", (uint32_t)ico_steam_appid(qe, name));
    s_field(b, "AppName", name);
    s_field(b, "Exe", qe);
    s_field(b, "StartDir", qd);
    s_field(b, "icon", icon);
    s_field(b, "ShortcutPath", "");
    s_field(b, "LaunchOptions", "");
    i_field(b, "IsHidden", 0);
    i_field(b, "AllowDesktopConfig", 1);
    i_field(b, "AllowOverlay", 1);
    i_field(b, "OpenVR", 0);
    i_field(b, "Devkit", 0);
    s_field(b, "DevkitGameID", "");
    i_field(b, "DevkitOverrideAppID", 0);
    i_field(b, "LastPlayTime", 0);
    s_field(b, "FlatpakAppID", "");
    byte(b, 0);
    cs(b, "tags");
    byte(b, 8);
    byte(b, 8);
}

static const IcoSteamEntry kEntry = {"ICO", "/games/ico/ico_pc", "/games/ico", ""};

/* --- the pure parts ------------------------------------------------------------ */

static void test_crc_and_id(void)
{
    CHECK(ico_steam_crc32("123456789", 9) == 0xCBF43926u);
    CHECK(ico_steam_crc32("", 0) == 0);
    {
        const int32_t id = ico_steam_appid("\"/games/ico/ico_pc\"", "ICO");
        const uint32_t want = ico_steam_crc32("\"/games/ico/ico_pc\"ICO", 22) | 0x80000000u;

        CHECK((uint32_t)id == want);
        CHECK(id < 0); /* the top bit, stored as a signed value */
        CHECK(ico_steam_appid("\"/other\"", "ICO") != id);
    }
}

static void test_append(void)
{
    static B in, want, tail;
    uint8_t *out = NULL;
    size_t outN = 0;

    /* into an empty library (no file at all, and the empty map) */
    want.n = 0;
    file_open(&want);
    new_entry(&want, "0", "ICO", "/games/ico/ico_pc", "/games/ico", "");
    file_close(&want);
    CHECK(ico_steam_vdf_append(NULL, 0, &kEntry, &out, &outN) == 0);
    CHECK(outN == want.n && memcmp(out, want.d, want.n) == 0);
    free(out);
    in.n = 0;
    file_open(&in);
    file_close(&in);
    CHECK(ico_steam_vdf_append(in.d, in.n, &kEntry, &out, &outN) == 0);
    CHECK(outN == want.n && memcmp(out, want.d, want.n) == 0);
    free(out);

    /* after two entries: every old byte kept, the new entry is index 2, the
       closing bytes follow */
    sample(&in);
    CHECK(ico_steam_vdf_append(in.d, in.n, &kEntry, &out, &outN) == 0);
    tail.n = 0;
    new_entry(&tail, "2", "ICO", "/games/ico/ico_pc", "/games/ico", "");
    CHECK(outN == in.n + tail.n);
    CHECK(memcmp(out, in.d, in.n - 2) == 0);                /* the old entries */
    CHECK(memcmp(out + in.n - 2, tail.d, tail.n) == 0);     /* the new one */
    CHECK(memcmp(out + outN - 2, in.d + in.n - 2, 2) == 0); /* the two closing bytes */
    /* the appended file is a shortcuts file that finds both old and new Exe */
    CHECK(ico_steam_vdf_find_exe(out, outN, "/usr/bin/some game", 0) == 1);
    CHECK(ico_steam_vdf_find_exe(out, outN, "/games/ico/ico_pc", 0) == 1);
    /* and takes a second entry as index 3 */
    {
        uint8_t *out2 = NULL;
        size_t out2N = 0;
        const IcoSteamEntry two = {"ICO", "/games/b/ico_pc", "/games/b", "/games/b/ico_pc"};

        CHECK(ico_steam_vdf_append(out, outN, &two, &out2, &out2N) == 0);
        tail.n = 0;
        new_entry(&tail, "3", "ICO", "/games/b/ico_pc", "/games/b", "/games/b/ico_pc");
        CHECK(out2N == outN + tail.n);
        CHECK(memcmp(out2, out, outN - 2) == 0);
        CHECK(memcmp(out2 + outN - 2, tail.d, tail.n) == 0);
        free(out2);
    }
    free(out);

    /* an index gap: the next index is one past the highest */
    in.n = 0;
    file_open(&in);
    old_entry(&in, "7", "Gap", "\"/g\"");
    file_close(&in);
    CHECK(ico_steam_vdf_append(in.d, in.n, &kEntry, &out, &outN) == 0);
    tail.n = 0;
    new_entry(&tail, "8", "ICO", "/games/ico/ico_pc", "/games/ico", "");
    CHECK(outN == in.n + tail.n && memcmp(out + in.n - 2, tail.d, tail.n) == 0);
    free(out);
}

static void test_find(void)
{
    static B in;

    sample(&in);
    CHECK(ico_steam_vdf_find_exe(in.d, in.n, "/opt/other/run.sh", 0) == 1);
    CHECK(ico_steam_vdf_find_exe(in.d, in.n, "/opt/other/RUN.sh", 0) == 0);
    CHECK(ico_steam_vdf_find_exe(in.d, in.n, "/opt/other/RUN.sh", 1) == 1);
    CHECK(ico_steam_vdf_find_exe(in.d, in.n, "/games/ico/ico_pc", 0) == 0);
    /* the plain form too: a hand-made entry may be unquoted */
    in.n = 0;
    file_open(&in);
    byte(&in, 0);
    cs(&in, "0");
    s_field(&in, "Exe", "/plain/exe");
    byte(&in, 8);
    file_close(&in);
    CHECK(ico_steam_vdf_find_exe(in.d, in.n, "/plain/exe", 0) == 1);
    /* an Exe string outside an entry (in the tags map) does not count */
    in.n = 0;
    file_open(&in);
    byte(&in, 0);
    cs(&in, "0");
    byte(&in, 0);
    cs(&in, "tags");
    s_field(&in, "Exe", "\"/x\"");
    byte(&in, 8);
    byte(&in, 8);
    file_close(&in);
    CHECK(ico_steam_vdf_find_exe(in.d, in.n, "/x", 0) == 0);
}

static void test_malformed(void)
{
    static B in;
    uint8_t *out = NULL;
    size_t outN = 0;

    sample(&in);
    for (size_t cut = 1; cut < in.n; cut += 7) { /* truncated anywhere */
        CHECK(ico_steam_vdf_find_exe(in.d, cut, "/x", 0) == -1);
        CHECK(ico_steam_vdf_append(in.d, cut, &kEntry, &out, &outN) == -1);
    }
    in.d[in.n++] = 0; /* a byte after the end */
    CHECK(ico_steam_vdf_find_exe(in.d, in.n, "/x", 0) == -1);
    in.n = 0;
    file_open(&in);
    byte(&in, 0x55); /* a node type this reader does not know */
    cs(&in, "x");
    file_close(&in);
    CHECK(ico_steam_vdf_append(in.d, in.n, &kEntry, &out, &outN) == -1);
    in.n = 0; /* a text file */
    cs(&in, "\"shortcuts\" { }");
    CHECK(ico_steam_vdf_find_exe(in.d, in.n, "/x", 0) == -1);
    in.n = 0; /* a vdf without a shortcuts map */
    byte(&in, 0);
    cs(&in, "other");
    byte(&in, 8);
    byte(&in, 8);
    CHECK(ico_steam_vdf_append(in.d, in.n, &kEntry, &out, &outN) == -1);
}

static void test_login_users(void)
{
    static const char t[] =
        "\"users\"\n{\n"
        "\t\"76561198000000001\"\n\t{\n\t\t\"AccountName\"\t\t\"first\"\n\t\t\"MostRecent\"\t\t\"0\"\n"
        "\t\t\"Timestamp\"\t\t\"1\"\n\t}\n"
        "\t// a comment\n"
        "\t\"76561198000000002\"\n\t{\n\t\t\"AccountName\"\t\t\"second\"\n\t\t\"MostRecent\"\t\t\"1\"\n\t}\n"
        "\t\"76561198000000003\"\n\t{\n\t\t\"MostRecent\"\t\t\"0\"\n\t}\n}\n";
    uint32_t ids[16];
    int n = ico_steam_login_users(t, sizeof(t) - 1, ids, 16);

    CHECK(n == 3);
    CHECK(ids[0] == 39734274u); /* 76561198000000002 - 76561197960265728 */
    CHECK(ids[1] == 39734273u);
    CHECK(ids[2] == 39734275u);
    CHECK(ico_steam_login_users(t, sizeof(t) - 1, ids, 2) == 2);
    CHECK(ico_steam_login_users("", 0, ids, 16) == 0);
    CHECK(ico_steam_login_users("\"users\" {", 9, ids, 16) == 0);
}

/* --- the whole job over a fake Steam folder ------------------------------------- */

static char s_base[900];

static int rm_cb(const char *path, const char *name, void *user)
{
    (void)name;
    (void)user;
    ico_remove(path);
    return 0;
}

static void p(char *out, size_t n, const char *rel)
{
    const int len = snprintf(out, n, "%s/%s", s_base, rel);
    if (len < 0 || (size_t)len >= n) {
        fprintf(stderr, "steam_shortcut_test: path too long: %s/%s\n", s_base, rel);
        exit(1);
    }
}

static void mk(const char *rel)
{
    char path[1100];

    p(path, sizeof(path), rel);
    ico_mkdir(path);
}

static void wr(const char *rel, const void *d, size_t n)
{
    char path[1100];
    FILE *f;

    p(path, sizeof(path), rel);
    f = ico_fopen(path, "wb");
    if (f != NULL) {
        fwrite(d, 1, n, f);
        fclose(f);
    }
}

static int rd(const char *rel, uint8_t *d, size_t cap, size_t *n)
{
    char path[1100];
    FILE *f;

    p(path, sizeof(path), rel);
    f = ico_fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    *n = fread(d, 1, cap, f);
    fclose(f);
    return 0;
}

static int has(const char *rel)
{
    char path[1100];

    p(path, sizeof(path), rel);
    return ico_path_kind(path, NULL, NULL) >= 0;
}

static void fresh(void)
{
    static const char *dirs[] = {"steam/userdata/39734274/config",
                                 "steam/userdata/39734274",
                                 "steam/userdata/39734273/config",
                                 "steam/userdata/39734273",
                                 "steam/userdata",
                                 "steam/config",
                                 "steam",
                                 ""};
    char path[1100];

    p(path, sizeof(path), "");
    ico_dir_walk(s_base, 5, rm_cb, NULL);
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        p(path, sizeof(path), dirs[i]);
        if (dirs[i][0] != '\0') {
            ico_rmdir(path);
        }
    }
}

static const char kUsers[] =
    "\"users\"\n{\n\t\"76561198000000001\"\n\t{\n\t\t\"MostRecent\"\t\t\"0\"\n\t}\n"
    "\t\"76561198000000002\"\n\t{\n\t\t\"MostRecent\"\t\t\"1\"\n\t}\n}\n";

static void env_for(IcoSteamEnv *env)
{
    memset(env, 0, sizeof(*env));
    p(env->roots[0], sizeof(env->roots[0]), "nowhere");
    p(env->roots[1], sizeof(env->roots[1]), "steam");
}

static void steam_folder(void)
{
    mk("steam");
    mk("steam/config");
    mk("steam/userdata");
    mk("steam/userdata/39734273");
    mk("steam/userdata/39734274");
    mk("steam/userdata/39734274/config");
    wr("steam/config/loginusers.vdf", kUsers, sizeof(kUsers) - 1);
}

static int s_alive;

static int fakeAlive(long pid)
{
    return pid == 4242 && s_alive;
}

static int s_regPid;

static int fakeActive(long *pid)
{
    if (s_regPid == 0) {
        return 0;
    }
    *pid = s_regPid;
    return 1;
}

static int s_exeRunning;

static int fakeProcess(void)
{
    return s_exeRunning;
}

static void test_job(void)
{
    static uint8_t got[8192], bak[8192];
    static B in, want, tail;
    IcoSteamEnv env;
    size_t gotN = 0, bakN = 0;
    const char *vdf = "steam/userdata/39734274/config/shortcuts.vdf";

    /* no Steam */
    fresh();
    env_for(&env);
    CHECK(ico_steam_add_shortcut(&env, &kEntry) == ICO_STEAM_NOT_FOUND);

    /* a Steam folder with no user it knows */
    mk("steam");
    mk("steam/userdata");
    CHECK(ico_steam_add_shortcut(&env, &kEntry) == ICO_STEAM_NO_USER);
    wr("steam/config/loginusers.vdf", kUsers, sizeof(kUsers) - 1); /* no config folder: stays */
    CHECK(ico_steam_add_shortcut(&env, &kEntry) == ICO_STEAM_NO_USER);

    /* the most recent user (39734274), no shortcuts file yet */
    fresh();
    steam_folder();
    CHECK(ico_steam_add_shortcut(&env, &kEntry) == ICO_STEAM_ADDED);
    want.n = 0;
    file_open(&want);
    new_entry(&want, "0", "ICO", "/games/ico/ico_pc", "/games/ico", "");
    file_close(&want);
    CHECK(rd(vdf, got, sizeof(got), &gotN) == 0);
    CHECK(gotN == want.n && memcmp(got, want.d, want.n) == 0);
    CHECK(!has("steam/userdata/39734274/config/shortcuts.vdf.bak")); /* nothing to back up */
    CHECK(!has("steam/userdata/39734274/config/shortcuts.vdf.tmp"));
    CHECK(!has("steam/userdata/39734273/config"));

    /* the same program again: nothing changes */
    CHECK(ico_steam_add_shortcut(&env, &kEntry) == ICO_STEAM_EXISTS);
    CHECK(rd(vdf, bak, sizeof(bak), &bakN) == 0 && bakN == gotN && memcmp(bak, got, gotN) == 0);

    /* an existing library: appended, the old file copied to .bak */
    fresh();
    steam_folder();
    sample(&in);
    wr(vdf, in.d, in.n);
    CHECK(ico_steam_add_shortcut(&env, &kEntry) == ICO_STEAM_ADDED);
    CHECK(rd(vdf, got, sizeof(got), &gotN) == 0);
    tail.n = 0;
    new_entry(&tail, "2", "ICO", "/games/ico/ico_pc", "/games/ico", "");
    CHECK(gotN == in.n + tail.n);
    CHECK(memcmp(got, in.d, in.n - 2) == 0);
    CHECK(memcmp(got + in.n - 2, tail.d, tail.n) == 0);
    CHECK(rd("steam/userdata/39734274/config/shortcuts.vdf.bak", bak, sizeof(bak), &bakN) == 0);
    CHECK(bakN == in.n && memcmp(bak, in.d, in.n) == 0);
    CHECK(ico_steam_add_shortcut(&env, &kEntry) == ICO_STEAM_EXISTS);
    CHECK(rd(vdf, bak, sizeof(bak), &bakN) == 0 && bakN == gotN);

    /* a file that is not a shortcuts file is left alone */
    fresh();
    steam_folder();
    wr(vdf, "not a vdf", 9);
    CHECK(ico_steam_add_shortcut(&env, &kEntry) == ICO_STEAM_FAILED);
    CHECK(rd(vdf, got, sizeof(got), &gotN) == 0 && gotN == 9 && memcmp(got, "not a vdf", 9) == 0);
    CHECK(!has("steam/userdata/39734274/config/shortcuts.vdf.bak"));

    /* a user in loginusers.vdf without a folder: the next one is used */
    fresh();
    steam_folder();
    {
        char path[1100];

        p(path, sizeof(path), "steam/userdata/39734274/config");
        ico_rmdir(path);
        p(path, sizeof(path), "steam/userdata/39734274");
        ico_rmdir(path);
    }
    mk("steam/userdata/39734273/config");
    CHECK(ico_steam_add_shortcut(&env, &kEntry) == ICO_STEAM_ADDED);
    CHECK(has("steam/userdata/39734273/config/shortcuts.vdf"));
}

static void test_running(void)
{
    static uint8_t got[8192];
    IcoSteamEnv env;
    size_t gotN = 0;
    char pid[1100];
    const char *vdf = "steam/userdata/39734274/config/shortcuts.vdf";

    fresh();
    steam_folder();
    env_for(&env);
    p(env.pidFiles[0], sizeof(env.pidFiles[0]), "steam.pid");
    p(pid, sizeof(pid), "steam.pid");
    env.pidAlive = fakeAlive;

    /* no pid file, a pid file of a dead process: not running */
    CHECK(!ico_steam_running(&env));
    wr("steam.pid", "4242\n", 5);
    s_alive = 0;
    CHECK(!ico_steam_running(&env));
    /* a live one: refused, nothing written */
    s_alive = 1;
    CHECK(ico_steam_running(&env));
    CHECK(ico_steam_add_shortcut(&env, &kEntry) == ICO_STEAM_RUNNING);
    CHECK(!has(vdf));
    CHECK(!has("steam/userdata/39734274/config/shortcuts.vdf.tmp"));
    /* a pid file with junk in it */
    wr("steam.pid", "junk", 4);
    CHECK(!ico_steam_running(&env));
    ico_remove(pid);
    CHECK(!ico_steam_running(&env));

    /* the registry's ActiveProcess pid (Windows): 0 or absent is closed */
    env.activeProcessPid = fakeActive;
    s_regPid = 0;
    CHECK(!ico_steam_running(&env));
    s_regPid = 4242;
    s_alive = 0;
    CHECK(!ico_steam_running(&env)); /* a stale value */
    s_alive = 1;
    CHECK(ico_steam_running(&env));
    CHECK(ico_steam_add_shortcut(&env, &kEntry) == ICO_STEAM_RUNNING);
    CHECK(!has(vdf));
    s_regPid = 0;

    /* a steam.exe process without the registry value */
    env.processRunning = fakeProcess;
    s_exeRunning = 1;
    CHECK(ico_steam_add_shortcut(&env, &kEntry) == ICO_STEAM_RUNNING);
    CHECK(!has(vdf));
    s_exeRunning = 0;
    CHECK(ico_steam_add_shortcut(&env, &kEntry) == ICO_STEAM_ADDED);
    CHECK(rd(vdf, got, sizeof(got), &gotN) == 0 && gotN > 0);

#ifndef _WIN32
    /* the platform's own check: an absurd pid is not alive */
    CHECK(!ico_steam_pid_alive(2147483646L));
    CHECK(!ico_steam_pid_alive(0));
    env.pidAlive = NULL;
    wr("steam.pid", "2147483646\n", 11);
    CHECK(!ico_steam_running(&env)); /* a stale pid */
    {
        /* a live process that is not Steam (a pid reused after Steam was
           killed) does not count; one named steam does */
        char other[1100], link[1100], pidText[32];
        char own[16] = "";
        pid_t kid;
        int len;
        FILE *self = fopen("/proc/self/comm", "rb");

        if (self != NULL) {
            own[fread(own, 1, sizeof(own) - 1, self)] = '\0';
            fclose(self);
            own[strcspn(own, "\n")] = '\0';
        }

        p(link, sizeof(link), "steamd");
        p(other, sizeof(other), "notit");
        ico_remove(link);
        ico_remove(other);
        CHECK(symlink("/bin/sleep", link) == 0 && symlink("/bin/sleep", other) == 0);
        for (int isSteam = 0; isSteam < 2; isSteam++) {
            const char *exe = isSteam ? link : other;

            kid = fork();
            if (kid == 0) {
                execl(exe, exe, "30", (char *)NULL);
                _exit(127);
            }
            CHECK(kid > 0);
            /* exec has to have happened for the name to be the link's */
            for (int k = 0; k < 200; k++) {
                char c[64];
                size_t n = 0;
                FILE *f;

                snprintf(c, sizeof(c), "/proc/%ld/comm", (long)kid);
                f = fopen(c, "rb");
                if (f != NULL) {
                    n = fread(c, 1, sizeof(c) - 1, f);
                    fclose(f);
                    c[n] = '\0';
                    if (n > 0 && c[n - 1] == '\n') {
                        c[n - 1] = '\0'; /* comm ends in a newline; own was cut before it */
                    }
                    if (strcmp(c, own) != 0) { /* this program's name: not yet */
                        break;
                    }
                }
                usleep(10000);
            }
            len = snprintf(pidText, sizeof(pidText), "%ld\n", (long)kid);
            wr("steam.pid", pidText, (size_t)len);
            CHECK(ico_steam_running(&env) == isSteam);
            CHECK(ico_steam_pid_alive((long)kid) == isSteam);
            kill(kid, SIGKILL);
            waitpid(kid, NULL, 0);
            CHECK(!ico_steam_running(&env)); /* gone: stale again */
        }
        ico_remove(link);
        ico_remove(other);
    }
#endif
}

/* The path the shortcut records: the program itself, or the AppImage file
   it runs from when APPIMAGE names a file and the program lies under
   APPDIR (host_fs.c ico_appimage_file); a stray APPIMAGE is not used */
static void test_self_exe(void)
{
#if defined(__linux__)
    char self[ICO_STEAM_PATH_MAX], got[ICO_STEAM_PATH_MAX], appdir[ICO_STEAM_PATH_MAX];
    char base[4096], file[4200]; /* realpath writes up to PATH_MAX */
    long n = (long)readlink("/proc/self/exe", self, sizeof(self) - 1);
    const int based = realpath(s_base, base) != NULL;
    FILE *f;

    CHECK(n > 0 && based);
    if (n <= 0 || !based) {
        return;
    }
    self[n] = '\0';
    unsetenv("APPIMAGE");
    unsetenv("APPDIR");
    CHECK(ico_steam_self_exe(got, sizeof(got)) == 0 && strcmp(got, self) == 0);

    snprintf(file, sizeof(file), "%s/ICO-v0.0.0-linux-x86_64.AppImage", base);
    if (strlen(file) >= sizeof(got)) {
        fprintf(stderr, "steam_shortcut_test: %s is too long; the AppImage path not tested\n",
                file);
        return;
    }
    f = fopen(file, "wb");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    fputs("not an AppImage\n", f);
    fclose(f);
    snprintf(appdir, sizeof(appdir), "%s", self);
    *strrchr(appdir, '/') = '\0'; /* the folder the program runs from, as the mount */
    setenv("APPIMAGE", file, 1);
    setenv("APPDIR", appdir, 1);
    CHECK(ico_steam_self_exe(got, sizeof(got)) == 0 && strcmp(got, file) == 0);
    /* the program outside APPDIR, or no APPDIR: the program itself */
    setenv("APPDIR", base, 1);
    CHECK(ico_steam_self_exe(got, sizeof(got)) == 0 && strcmp(got, self) == 0);
    unsetenv("APPDIR");
    CHECK(ico_steam_self_exe(got, sizeof(got)) == 0 && strcmp(got, self) == 0);
    unsetenv("APPIMAGE");
    ico_remove(file);
#endif
}

int main(int argc, char **argv)
{
    const char *base = argc > 1 ? argv[1] : ".";

    snprintf(s_base, sizeof(s_base), "%s/steam_shortcut_work", base);
    ico_mkdir(s_base);

    test_crc_and_id();
    test_append();
    test_find();
    test_malformed();
    test_login_users();
    test_job();
    test_running();
    test_self_exe();

    fresh();
    ico_rmdir(s_base);
    if (failures == 0) {
        printf("steam_shortcut_test: all passed\n");
        return 0;
    }
    printf("steam_shortcut_test: %d failure(s)\n", failures);
    return 1;
}
