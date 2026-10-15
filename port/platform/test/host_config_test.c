/*
 * port/platform/test/host_config_test.c
 *
 * ico-pc.ini parsing and rewriting, SHA-1 against FIPS 180 test vectors,
 * path joining, the [dev] keys exported as environment switches and the
 * fixed blend of a run started again after a device loss
 * (port/platform/host_config.c). Built a second time with
 * ICO_HOST_FORCE_PORTABLE (host_config_portable_test, as the Android build
 * compiles host_config.c), where the user folder is always the executable's
 * own folder.
 */
#include "host_config.h"
#include "host_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void hex(const unsigned char d[20], char out[41])
{
    int i;

    for (i = 0; i < 20; i++) {
        snprintf(out + 2 * i, 3, "%02x", d[i]);
    }
}

static void sha1_str(const char *s, size_t repeat, char out[41])
{
    IcoSha1 c;
    unsigned char d[20];
    size_t i;

    ico_sha1_init(&c);
    for (i = 0; i < repeat; i++) {
        ico_sha1_update(&c, s, strlen(s));
    }
    ico_sha1_final(&c, d);
    hex(d, out);
}

static void test_sha1(void)
{
    char h[41];
    unsigned long long bytes = 0;
    const char *path = "host_config_test.bin";
    FILE *f;

    sha1_str("", 1, h);
    CHECK(strcmp(h, "da39a3ee5e6b4b0d3255bfef95601890afd80709") == 0);
    sha1_str("abc", 1, h);
    CHECK(strcmp(h, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0);
    sha1_str("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 1, h);
    CHECK(strcmp(h, "84983e441c3bd26ebaae4aa1f95129e5e54670f1") == 0);
    sha1_str("a", 1000000, h); /* one byte at a time through the buffer */
    CHECK(strcmp(h, "34aa973cd4c4daa4f61eeb2bdbad27316534016f") == 0);

    f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f != NULL) {
        fputs("abc", f);
        fclose(f);
        CHECK(ico_sha1_file(path, h, &bytes) == 0);
        CHECK(bytes == 3 && strcmp(h, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0);
        remove(path);
    }
    CHECK(ico_sha1_file("no/such/file.iso", h, &bytes) == -1);
}

static void test_ini(void)
{
    IcoIni ini;
    const char *path = "host_config_test.ini";
    FILE *f;

    ico_ini_parse(&ini, "# comment\r\n"
                        "; also a comment = not a key\n"
                        "  iso =  C:\\Games\\My ICO\\Ico_PAL.iso  \r\n"
                        "ticks=3000\n"
                        "pad_script=\"D:\\with space\\pad.txt\"\n"
                        "novalue\n"
                        "ticks=1\n"
                        "empty=\n"
                        "hash=a#b;c");
    CHECK(strcmp(ico_ini_get(&ini, "iso"), "C:\\Games\\My ICO\\Ico_PAL.iso") == 0);
    CHECK(strcmp(ico_ini_get(&ini, "ticks"), "3000") == 0); /* the first one wins */
    CHECK(strcmp(ico_ini_get(&ini, "pad_script"), "D:\\with space\\pad.txt") == 0);
    CHECK(strcmp(ico_ini_get(&ini, "empty"), "") == 0);
    CHECK(strcmp(ico_ini_get(&ini, "hash"), "a#b;c") == 0);
    CHECK(ico_ini_get(&ini, "novalue") == NULL);
    CHECK(ico_ini_get(&ini, "also a comment") == NULL);
    CHECK(ini.count == 5);

    /* rewriting keeps the other lines, replaces the key, or appends it */
    f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    fputs("# shipped\r\nticks=3000\r\niso=old.iso\r\n", f);
    fclose(f);
    CHECK(ico_ini_store(path, "iso", "E:\\new path\\Ico_PAL.iso") == 0);
    CHECK(ico_ini_store(path, "verify", "0") == 0);
    CHECK(ico_ini_load(&ini, path) == 0);
    CHECK(ini.count == 3);
    CHECK(strcmp(ico_ini_get(&ini, "iso"), "E:\\new path\\Ico_PAL.iso") == 0);
    CHECK(strcmp(ico_ini_get(&ini, "ticks"), "3000") == 0);
    CHECK(strcmp(ico_ini_get(&ini, "verify"), "0") == 0);
    remove(path);
    CHECK(ico_ini_store(path, "iso", "x.iso") == 0); /* creates the file */
    CHECK(ico_ini_load(&ini, path) == 0 && strcmp(ico_ini_get(&ini, "iso"), "x.iso") == 0);
    remove(path);
    CHECK(ico_ini_load(&ini, path) == -1 && ini.count == 0);
}

static void test_paths(void)
{
    char out[ICO_PATH_MAX];
    char dir[ICO_PATH_MAX];

    ico_path_join(out, sizeof(out), "/opt/ico", "logs");
#ifdef _WIN32
    CHECK(strcmp(out, "/opt/ico\\logs") == 0);
    CHECK(ico_path_is_absolute("C:\\x") && ico_path_is_absolute("\\\\server\\share"));
#else
    CHECK(strcmp(out, "/opt/ico/logs") == 0);
#endif
    ico_path_join(out, sizeof(out), "/opt/ico/", "a b.txt");
    CHECK(strcmp(out, "/opt/ico/a b.txt") == 0);
    ico_path_join(out, sizeof(out), "/opt/ico", "/abs/x.iso");
    CHECK(strcmp(out, "/abs/x.iso") == 0);
    CHECK(!ico_path_is_absolute("rel/x"));
    CHECK(ico_host_exe_dir(dir, sizeof(dir)) == 0 && dir[0] != '\0');
    /* the logs folder: <exe dir>/logs here (no app bundle) */
    {
        char logs[ICO_PATH_MAX], want[ICO_PATH_MAX];

        ico_path_join(want, sizeof(want), dir, "logs");
        CHECK(ico_host_logs_dir(logs, sizeof(logs)) == 0 && strcmp(logs, want) == 0);
        CHECK(ico_host_logs_dir(logs, 4) == -1 && logs[0] == '\0');
    }
    CHECK(ico_make_dir("host_config_test_dir") == 0 && ico_make_dir("host_config_test_dir") == 0);
    remove("host_config_test_dir"); /* rmdir on POSIX; left behind on Windows */
}

/* a join that does not fit is an error and leaves "", never a cut-off path */
static void test_join_overflow(void)
{
    char small[16], out[ICO_PATH_MAX];

    CHECK(ico_path_join(small, sizeof(small), "/opt/ico", "logs") == 0);
    CHECK(strlen(small) == strlen("/opt/ico/logs"));
    CHECK(ico_path_join(small, sizeof(small), "/opt/ico", "a-name-too-long.txt") == -1);
    CHECK(small[0] == '\0');
    CHECK(ico_path_join(small, sizeof(small), "/opt/ico", "/an/absolute/path/too/long") == -1);
    CHECK(small[0] == '\0');
    CHECK(ico_path_join(small, sizeof(small), "", "exactly-15-chars") == -1); /* needs 17 */
    CHECK(ico_path_join(small, sizeof(small), "", "fourteen-chars") == 0);
    CHECK(ico_path_join(small, 0, "a", "b") == -1);
    memset(out, 'x', sizeof(out) - 1);
    out[sizeof(out) - 1] = '\0';
    CHECK(ico_path_join(small, sizeof(small), out, "n") == -1 && small[0] == '\0');
}

/* ico_host_saves_dir is a pure read: it sets no environment variable and
   makes no folder, whatever dump and audio keys the ini holds */
static void test_saves_dir_pure(void)
{
    char ini_path[ICO_PATH_MAX], exe[ICO_PATH_MAX], dumps[ICO_PATH_MAX], logs[ICO_PATH_MAX];
    char out[ICO_PATH_MAX];
    FILE *f;

    ico_host_ini_path(ini_path, sizeof(ini_path));
    ico_host_exe_dir(exe, sizeof(exe));
    ico_path_join(dumps, sizeof(dumps), exe, "pure-dumps");
    ico_path_join(logs, sizeof(logs), exe, "logs");
    if (ico_file_exists(ini_path) || ico_path_kind(dumps, NULL, NULL) >= 0 ||
        ico_path_kind(logs, NULL, NULL) >= 0) {
        fprintf(stderr, "host_config_test: %s, %s or %s exists; purity not tested\n", ini_path,
                dumps, logs);
        return;
    }
#ifdef _WIN32
    _putenv_s("ICO_RD_DUMP_EVERY", "");
    _putenv_s("ICO_AUDIO_DUMP", "");
    _putenv_s("ICO_FIXED_CLOCK", "");
#else
    unsetenv("ICO_RD_DUMP_EVERY");
    unsetenv("ICO_AUDIO_DUMP");
    unsetenv("ICO_FIXED_CLOCK");
#endif
    f = fopen(ini_path, "wb");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    fputs("saves=pure-cards\ndump_every=5\ndump_dir=pure-dumps\naudio_dump=1\n", f);
    fflush(f);
    /* no saves2: port 1 has no card */
    CHECK(ico_host_saves2_dir(out, sizeof(out)) == 0 && out[0] == '\0');
    fputs("saves2=pure-cards-2\n", f);
    fclose(f);
    CHECK(ico_host_saves_dir(out, sizeof(out)) == 0);
    CHECK(strstr(out, "pure-cards") != NULL);
    CHECK(ico_host_saves2_dir(out, sizeof(out)) == 1);
    CHECK(strstr(out, "pure-cards-2") != NULL);
    CHECK(ico_path_kind(dumps, NULL, NULL) < 0);
    CHECK(ico_path_kind(logs, NULL, NULL) < 0);
    CHECK(getenv("ICO_RD_DUMP_EVERY") == NULL || getenv("ICO_RD_DUMP_EVERY")[0] == '\0');
    CHECK(getenv("ICO_AUDIO_DUMP") == NULL || getenv("ICO_AUDIO_DUMP")[0] == '\0');
    CHECK(getenv("ICO_FIXED_CLOCK") == NULL || getenv("ICO_FIXED_CLOCK")[0] == '\0');
    /* the layered read alone is as pure */
    {
        IcoIni ini;

        CHECK(ico_ini_load_layered(&ini, ini_path) == 0);
        CHECK(ico_ini_get(&ini, "dump_every") != NULL);
        CHECK(getenv("ICO_RD_DUMP_EVERY") == NULL || getenv("ICO_RD_DUMP_EVERY")[0] == '\0');
        CHECK(ico_path_kind(dumps, NULL, NULL) < 0);
        /* the full load is the one that exports */
        CHECK(ico_ini_load(&ini, ini_path) == 0);
        CHECK(getenv("ICO_RD_DUMP_EVERY") != NULL && strcmp(getenv("ICO_RD_DUMP_EVERY"), "5") == 0);
        CHECK(ico_path_kind(dumps, NULL, NULL) == 1);
    }
    remove(ini_path);
    remove(dumps);
    remove(logs);
}

/* The card folders come from the ini the game reads (ico_host_ini_path)
   over config.toml in the user folder: [paths] saves/saves2 with no ini,
   then the ini's saves= over the toml; relative to the executable's folder */
static void test_saves_dir_layered(void)
{
    char ini_path[ICO_PATH_MAX], exe[ICO_PATH_MAX], pref[ICO_PATH_MAX], toml[ICO_PATH_MAX];
    char out[ICO_PATH_MAX], want[ICO_PATH_MAX];
    FILE *f;

    ico_host_ini_path(ini_path, sizeof(ini_path));
    CHECK(ico_host_exe_dir(exe, sizeof(exe)) == 0);
    CHECK(ico_host_pref_dir(pref, sizeof(pref)) == 0);
    ico_path_join(toml, sizeof(toml), pref, "config.toml");
    if (ico_file_exists(ini_path) || ico_file_exists(toml)) {
        fprintf(stderr, "host_config_test: %s or %s exists; layered card folders not tested\n",
                ini_path, toml);
        return;
    }
    f = fopen(toml, "wb");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    fputs("[paths]\nsaves = \"toml-cards\"\nsaves2 = \"toml-cards-2\"\n", f);
    fclose(f);
    ico_path_join(want, sizeof(want), exe, "toml-cards");
    CHECK(ico_host_saves_dir(out, sizeof(out)) == 0 && strcmp(out, want) == 0);
    ico_path_join(want, sizeof(want), exe, "toml-cards-2");
    CHECK(ico_host_saves2_dir(out, sizeof(out)) == 1 && strcmp(out, want) == 0);
    f = fopen(ini_path, "wb");
    CHECK(f != NULL);
    if (f != NULL) {
        fputs("saves=ini-cards\n", f);
        fclose(f);
        ico_path_join(want, sizeof(want), exe, "ini-cards");
        CHECK(ico_host_saves_dir(out, sizeof(out)) == 0 && strcmp(out, want) == 0);
        ico_path_join(want, sizeof(want), exe, "toml-cards-2");
        CHECK(ico_host_saves2_dir(out, sizeof(out)) == 1 && strcmp(out, want) == 0);
        remove(ini_path);
    }
    remove(toml);
}

/* portable mode: portable=1 in the ini, or a userdata folder beside the
   program, makes the user folder <exe dir>/userdata; portable=0 turns it off */
static void test_portable_value(void)
{
    /* the words for off, in any case; anything else on; nothing: the folder */
    CHECK(ico_host_portable_value(NULL) == -1);
    CHECK(ico_host_portable_value("") == -1);
    CHECK(ico_host_portable_value("  ") == -1);
    CHECK(ico_host_portable_value("0") == 0);
    CHECK(ico_host_portable_value("false") == 0);
    CHECK(ico_host_portable_value("No") == 0);
    CHECK(ico_host_portable_value("OFF") == 0);
    CHECK(ico_host_portable_value(" off ") == 0);
    CHECK(ico_host_portable_value("1") == 1);
    CHECK(ico_host_portable_value("true") == 1);
    CHECK(ico_host_portable_value("yes") == 1);
    CHECK(ico_host_portable_value("offline") == 1);
}

#ifndef ICO_HOST_FORCE_PORTABLE

static void test_portable(void)
{
    char exe[ICO_PATH_MAX], want[ICO_PATH_MAX], got[ICO_PATH_MAX], ini_path[ICO_PATH_MAX];
    char toml[ICO_PATH_MAX];
    IcoIni ini;
    FILE *f;

    ico_host_exe_dir(exe, sizeof(exe));
    ico_path_join(want, sizeof(want), exe, "userdata");
    ico_host_ini_path(ini_path, sizeof(ini_path));
    if (ico_path_kind(want, NULL, NULL) >= 0 || ico_file_exists(ini_path)) {
        fprintf(stderr, "host_config_test: %s or %s exists; portable mode not tested\n", want,
                ini_path);
        return;
    }
    /* neither the key nor the folder: not portable */
    ico_host_set_portable(-1);
    CHECK(!ico_host_pref_is_portable());
    CHECK(ico_path_kind(want, NULL, NULL) < 0);
    /* the key through the setter makes and returns the folder */
    ico_host_set_portable(1);
    CHECK(ico_host_pref_is_portable());
    CHECK(ico_host_pref_dir(got, sizeof(got)) == 0 && strcmp(got, want) == 0);
    CHECK(ico_path_kind(want, NULL, NULL) == 1);
    /* the folder alone is enough; portable=0 overrides it */
    ico_host_set_portable(-1);
    CHECK(ico_host_pref_is_portable());
    CHECK(ico_host_pref_dir(got, sizeof(got)) == 0 && strcmp(got, want) == 0);
    ico_host_set_portable(0);
    CHECK(!ico_host_pref_is_portable());
    ico_host_set_portable(-1);
    /* a config.toml in the folder is read through the executable's own ini */
    ico_path_join(toml, sizeof(toml), want, "config.toml");
    f = fopen(toml, "wb");
    CHECK(f != NULL);
    if (f != NULL) {
        fputs("[paths]\niso = \"portable.iso\"\n", f);
        fclose(f);
        f = fopen(ini_path, "wb");
        CHECK(f != NULL);
        if (f != NULL) {
            fputs("portable=1\n", f);
            fclose(f);
            ico_host_set_portable(-1);
            CHECK(ico_ini_load_layered(&ini, ini_path) == 0);
            CHECK(ico_ini_get(&ini, "iso") != NULL &&
                  strcmp(ico_ini_get(&ini, "iso"), "portable.iso") == 0);
            CHECK(ico_host_pref_is_portable());
            remove(ini_path);
        }
        remove(toml);
    }
    /* the folder gone and the key off again: the user profile */
    remove(want); /* rmdir on POSIX; left behind on Windows */
    ico_host_set_portable(-1);
    CHECK(ico_path_kind(want, NULL, NULL) < 0 || ico_host_pref_is_portable());
}

#else

/* forced portable mode: the user folder is the executable's folder itself
   whatever portable= or the setter says, no userdata folder is made, and
   the cards, config.toml and ico-pc.ini all live there */
static void test_forced_portable(void)
{
    char exe[ICO_PATH_MAX], got[ICO_PATH_MAX], want[ICO_PATH_MAX], ini_path[ICO_PATH_MAX];
    char toml[ICO_PATH_MAX], userdata[ICO_PATH_MAX];
    IcoIni ini;
    FILE *f;
    int s;

    CHECK(ico_host_exe_dir(exe, sizeof(exe)) == 0);
    ico_path_join(userdata, sizeof(userdata), exe, "userdata");
    ico_host_ini_path(ini_path, sizeof(ini_path));
    ico_path_join(toml, sizeof(toml), exe, "config.toml");
    if (ico_path_kind(userdata, NULL, NULL) >= 0 || ico_file_exists(ini_path) ||
        ico_file_exists(toml)) {
        fprintf(stderr, "host_config_test: %s, %s or %s exists; forced portable mode not tested\n",
                userdata, ini_path, toml);
        return;
    }
    for (s = -1; s <= 1; s++) {
        ico_host_set_portable(s);
        CHECK(ico_host_pref_is_portable() == 1);
        CHECK(ico_host_pref_dir(got, sizeof(got)) == 0 && strcmp(got, exe) == 0);
    }
    CHECK(ico_path_kind(userdata, NULL, NULL) < 0);
    ico_path_join(want, sizeof(want), exe, "ico-pc.ini");
    CHECK(strcmp(ini_path, want) == 0);
    /* the cards: <exe>/memcard with no saves= */
    ico_path_join(want, sizeof(want), exe, "memcard");
    CHECK(ico_host_saves_dir(got, sizeof(got)) == 0 && strcmp(got, want) == 0);
    /* portable=0 in the file changes nothing; config.toml beside it is read */
    f = fopen(toml, "wb");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    fputs("[paths]\niso = \"forced.iso\"\n", f);
    fclose(f);
    f = fopen(ini_path, "wb");
    CHECK(f != NULL);
    if (f != NULL) {
        fputs("portable=0\n", f);
        fclose(f);
        CHECK(ico_ini_load_layered(&ini, ini_path) == 0);
        CHECK(ico_ini_get(&ini, "iso") != NULL &&
              strcmp(ico_ini_get(&ini, "iso"), "forced.iso") == 0);
        CHECK(ico_host_pref_is_portable() == 1);
        CHECK(ico_host_pref_dir(got, sizeof(got)) == 0 && strcmp(got, exe) == 0);
        CHECK(ico_path_kind(userdata, NULL, NULL) < 0);
        remove(ini_path);
    }
    remove(toml);
}

#endif

static void unset_env(const char *name)
{
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

static int env_is(const char *name, const char *want)
{
    const char *v = getenv(name);

    if (want == NULL) {
        return v == NULL || v[0] == '\0';
    }
    return v != NULL && strcmp(v, want) == 0;
}

/* the [dev] A/B switches: the mapping (ico_dev_env_name/_value), the
   config.toml paths they take, and ico_ini_export setting them (an ini
   other than the executable's own: the export block alone) */
static void test_dev_env(void)
{
    static const char *const vars[] = {"ICO_RD_BLEND", "ICO_RD_FOG_PATH", "ICO_GPU_CHECKPOINTS",
                                       "ICO_GPU_ROBUST", "ICO_VK_ATTACHMENT_READ"};
    IcoIni ini;
    size_t i;

    CHECK(strcmp(ico_dev_env_name("blend"), "ICO_RD_BLEND") == 0);
    CHECK(strcmp(ico_dev_env_name("fog_path"), "ICO_RD_FOG_PATH") == 0);
    CHECK(strcmp(ico_dev_env_name("gpu_checkpoints"), "ICO_GPU_CHECKPOINTS") == 0);
    CHECK(strcmp(ico_dev_env_name("gpu_robust"), "ICO_GPU_ROBUST") == 0);
    CHECK(ico_dev_env_name("ticks") == NULL);
    CHECK(ico_dev_env_name(NULL) == NULL);
    /* blend: fixed is exported, exact is the default (nothing set) */
    CHECK(strcmp(ico_dev_env_value("blend", "fixed"), "fixed") == 0);
    CHECK(ico_dev_env_value("blend", "exact") == NULL);
    CHECK(ico_dev_env_value("blend", "rov") == NULL);
    /* the exact paths forced by name (issue 89: the self-test's verdict
       overridden for a test) */
    CHECK(strcmp(ico_dev_env_value("blend", "attachment"), "attachment") == 0);
    CHECK(strcmp(ico_dev_env_value("blend", "storage"), "storage") == 0);
    CHECK(ico_dev_env_value("blend", "auto") == NULL); /* the default: nothing set */
    /* the attachment read on Qualcomm's driver (vk_device.c) */
    CHECK(strcmp(ico_dev_env_name("attachment_read"), "ICO_VK_ATTACHMENT_READ") == 0);
    CHECK(strcmp(ico_dev_env_value("attachment_read", "true"), "1") == 0);
    CHECK(strcmp(ico_dev_env_value("attachment_read", "0"), "0") == 0);
    CHECK(ico_dev_env_value("attachment_read", "maybe") == NULL);
    /* fog_path: the three paths as they are */
    CHECK(strcmp(ico_dev_env_value("fog_path", "inplace"), "inplace") == 0);
    CHECK(strcmp(ico_dev_env_value("fog_path", "copy"), "copy") == 0);
    CHECK(strcmp(ico_dev_env_value("fog_path", "buffer"), "buffer") == 0);
    CHECK(ico_dev_env_value("fog_path", "test") == NULL);
    CHECK(ico_dev_env_value("fog_path", "") == NULL);
    /* the switches: on and off words, anything else nothing */
    CHECK(strcmp(ico_dev_env_value("gpu_checkpoints", "true"), "1") == 0);
    CHECK(strcmp(ico_dev_env_value("gpu_checkpoints", "1"), "1") == 0);
    CHECK(strcmp(ico_dev_env_value("gpu_robust", "yes"), "1") == 0);
    CHECK(strcmp(ico_dev_env_value("gpu_robust", "false"), "0") == 0);
    CHECK(strcmp(ico_dev_env_value("gpu_robust", "off"), "0") == 0);
    CHECK(ico_dev_env_value("gpu_robust", "maybe") == NULL);
    CHECK(ico_dev_env_value("gpu_robust", NULL) == NULL);
    CHECK(ico_dev_env_value("ticks", "1") == NULL);
    /* pass_split: a count of VU draws as it is, 0 or off for none */
    CHECK(strcmp(ico_dev_env_name("pass_split"), "ICO_RD_PASS_SPLIT") == 0);
    CHECK(strcmp(ico_dev_env_value("pass_split", "8"), "8") == 0);
    CHECK(strcmp(ico_dev_env_value("pass_split", "0"), "0") == 0);
    CHECK(strcmp(ico_dev_env_value("pass_split", "off"), "0") == 0);
    CHECK(ico_dev_env_value("pass_split", "8x") == NULL);
    CHECK(ico_dev_env_value("pass_split", "-1") == NULL);
    CHECK(ico_dev_env_value("pass_split", "99999") == NULL);
    /* storage_pass_merge: the storage pass merge's switch (issue 103) */
    CHECK(strcmp(ico_dev_env_name("storage_pass_merge"), "ICO_RD_PASS_MERGE") == 0);
    CHECK(strcmp(ico_dev_env_value("storage_pass_merge", "false"), "0") == 0);
    CHECK(strcmp(ico_dev_env_value("storage_pass_merge", "true"), "1") == 0);
    CHECK(ico_dev_env_value("storage_pass_merge", "sometimes") == NULL);
    CHECK(strcmp(ico_config_ini_key("dev.storage_pass_merge"), "storage_pass_merge") == 0);
    /* config.toml's [dev] keys reach the same ini keys */
    CHECK(strcmp(ico_config_ini_key("dev.blend"), "blend") == 0);
    CHECK(strcmp(ico_config_ini_key("dev.fog_path"), "fog_path") == 0);
    CHECK(strcmp(ico_config_ini_key("dev.gpu_checkpoints"), "gpu_checkpoints") == 0);
    CHECK(strcmp(ico_config_ini_key("dev.gpu_robust"), "gpu_robust") == 0);
    CHECK(strcmp(ico_config_ini_key("dev.pass_split"), "pass_split") == 0);
    CHECK(strcmp(ico_config_ini_key("dev.attachment_read"), "attachment_read") == 0);

    for (i = 0; i < sizeof(vars) / sizeof(vars[0]); i++) {
        unset_env(vars[i]);
    }
    ico_ini_parse(&ini, "[dev]\nblend = fixed\nfog_path = copy\ngpu_checkpoints = true\n"
                        "gpu_robust = 1\n");
    ico_ini_export(&ini, "dev-env-test.ini", 0);
    CHECK(env_is("ICO_RD_BLEND", "fixed"));
    CHECK(env_is("ICO_RD_FOG_PATH", "copy"));
    CHECK(env_is("ICO_GPU_CHECKPOINTS", "1"));
    CHECK(env_is("ICO_GPU_ROBUST", "1"));
    for (i = 0; i < sizeof(vars) / sizeof(vars[0]); i++) {
        unset_env(vars[i]);
    }
    /* blend=exact and an unknown path leave the defaults; false is 0 */
    ico_ini_parse(&ini, "blend=exact\nfog_path=sideways\ngpu_robust=false\n");
    ico_ini_export(&ini, "dev-env-test.ini", 0);
    CHECK(env_is("ICO_RD_BLEND", NULL));
    CHECK(env_is("ICO_RD_FOG_PATH", NULL));
    CHECK(env_is("ICO_GPU_CHECKPOINTS", NULL));
    CHECK(env_is("ICO_GPU_ROBUST", "0"));
    for (i = 0; i < sizeof(vars) / sizeof(vars[0]); i++) {
        unset_env(vars[i]);
    }
    /* blend=attachment and attachment_read=true (config.toml's [dev]) */
    ico_ini_parse(&ini, "[dev]\nblend = attachment\nattachment_read = true\n");
    ico_ini_export(&ini, "dev-env-test.ini", 0);
    CHECK(env_is("ICO_RD_BLEND", "attachment"));
    CHECK(env_is("ICO_VK_ATTACHMENT_READ", "1"));
    for (i = 0; i < sizeof(vars) / sizeof(vars[0]); i++) {
        unset_env(vars[i]);
    }
}

static void set_env(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

/* ICO_RD_BLEND in a run started again after a device loss
   (ICO_STARTUP_RETRY=blend:... or backend:..., startup_retry.h) or at the
   crash order's fixedblend step: the restart's fixed blend wins over the
   ini's blend= and over an ICO_RD_BLEND already in the environment, for
   that run; any other run (none, or another crash step) keeps the order
   ini > environment > default */
static void test_retry_blend(void)
{
    IcoIni ini;

    /* the pure rule */
    CHECK(strcmp(ico_dev_env_export_value("blend", NULL, "blend:vulkan"), "fixed") == 0);
    CHECK(strcmp(ico_dev_env_export_value("blend", "exact", "backend:d3d12"), "fixed") == 0);
    CHECK(strcmp(ico_dev_env_export_value("blend", "fixed", "blend:d3d12"), "fixed") == 0);
    CHECK(strcmp(ico_dev_env_export_value("blend", NULL, "fixedblend:vulkan"), "fixed") == 0);
    CHECK(ico_dev_env_export_value("blend", NULL, "lazy:vulkan") == NULL);
    CHECK(ico_dev_env_export_value("blend", NULL, "d3d12:d3d12") == NULL);
    CHECK(ico_dev_env_export_value("blend", NULL, "cache:vulkan") == NULL);
    CHECK(ico_dev_env_export_value("blend", "exact", NULL) == NULL);
    CHECK(strcmp(ico_dev_env_export_value("blend", "fixed", NULL), "fixed") == 0);
    /* the other switches are not touched by the restart */
    CHECK(ico_dev_env_export_value("fog_path", NULL, "blend:vulkan") == NULL);
    CHECK(strcmp(ico_dev_env_export_value("gpu_robust", "1", "blend:vulkan"), "1") == 0);

    /* the export: a loss restart with no blend= */
    unset_env("ICO_RD_BLEND");
    set_env("ICO_STARTUP_RETRY", "blend:vulkan");
    ico_ini_parse(&ini, "audio=1\n");
    ico_ini_export(&ini, "dev-env-test.ini", 0);
    CHECK(env_is("ICO_RD_BLEND", "fixed"));
    /* no ini at all (nothing else is exported then) */
    unset_env("ICO_RD_BLEND");
    memset(&ini, 0, sizeof(ini));
    ico_ini_export(&ini, "dev-env-test.ini", -1);
    CHECK(env_is("ICO_RD_BLEND", "fixed"));
    /* blend=exact in the ini and another value in the environment: the
       restart wins */
    set_env("ICO_RD_BLEND", "storage");
    set_env("ICO_STARTUP_RETRY", "backend:d3d12");
    ico_ini_parse(&ini, "blend=exact\n");
    ico_ini_export(&ini, "dev-env-test.ini", 0);
    CHECK(env_is("ICO_RD_BLEND", "fixed"));
    /* a crash restart: the ini and the environment as in a first run */
    set_env("ICO_RD_BLEND", "storage");
    set_env("ICO_STARTUP_RETRY", "lazy:vulkan");
    ico_ini_parse(&ini, "audio=1\n");
    ico_ini_export(&ini, "dev-env-test.ini", 0);
    CHECK(env_is("ICO_RD_BLEND", "storage"));
    ico_ini_parse(&ini, "blend=fixed\n");
    ico_ini_export(&ini, "dev-env-test.ini", 0);
    CHECK(env_is("ICO_RD_BLEND", "fixed"));
    /* no restart */
    unset_env("ICO_STARTUP_RETRY");
    unset_env("ICO_RD_BLEND");
    ico_ini_parse(&ini, "blend=exact\n");
    ico_ini_export(&ini, "dev-env-test.ini", 0);
    CHECK(env_is("ICO_RD_BLEND", NULL));
    unset_env("ICO_RD_BLEND");
}

int main(void)
{
    test_sha1();
    test_ini();
    test_join_overflow();
    test_saves_dir_pure();
    test_saves_dir_layered();
    test_paths();
    test_portable_value();
    test_dev_env();
    test_retry_blend();
#ifdef ICO_HOST_FORCE_PORTABLE
    test_forced_portable();
#else
    test_portable();
#endif
    printf("host_config_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
