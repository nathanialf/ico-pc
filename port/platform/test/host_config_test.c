/*
 * port/platform/test/host_config_test.c
 *
 * ico-pc.ini parsing and rewriting, SHA-1 against FIPS 180 test vectors,
 * path joining, the [dev] keys exported as environment switches and the
 * fixed blend of a run started again after a device loss, the logs folder
 * override and the program folder of an AppImage
 * (port/platform/host_config.c, host_fs.c ico_appimage_file). Built a second time with
 * ICO_HOST_FORCE_PORTABLE (host_config_portable_test, as the Android build
 * compiles host_config.c), where the user folder is always the executable's
 * own folder.
 */
#include "host_config.h"
#include "host_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__) && !defined(__ANDROID__)
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
    static const char *const vars[] = {
        "ICO_RD_BLEND",           "ICO_RD_FOG_PATH",        "ICO_GPU_CHECKPOINTS",
        "ICO_GPU_ROBUST",         "ICO_VK_ATTACHMENT_READ", "ICO_RD_DATE_PATH",
        "ICO_RD_STORE_PLAN",      "ICO_RD_BLUR_FOLD",       "ICO_RD_UINT_VIEW",
        "ICO_VK_FAKE_NO_DYNAMIC", "ICO_RD_ALPHA_PLAN",      "ICO_RD_FX_PLAN",
        "ICO_RD_FX_DST_LOCAL",    "ICO_RD_VU_PLANES"};
    IcoIni ini;
    size_t i;

    CHECK(strcmp(ico_dev_env_name("blend"), "ICO_RD_BLEND") == 0);
    CHECK(strcmp(ico_dev_env_name("fog_path"), "ICO_RD_FOG_PATH") == 0);
    CHECK(strcmp(ico_dev_env_name("gpu_checkpoints"), "ICO_GPU_CHECKPOINTS") == 0);
    CHECK(strcmp(ico_dev_env_name("gpu_robust"), "ICO_GPU_ROBUST") == 0);
    CHECK(ico_dev_env_name("ticks") == NULL);
    CHECK(ico_dev_env_name(NULL) == NULL);
    /* blend: the fixed-function blend is the default (nothing set); fixed
       pins it, attachment and storage opt into that exact blend, exact
       (the old default's word) into the device's best exact mode
       (rd_dst_read.c rd__dst_asked_env) */
    CHECK(strcmp(ico_dev_env_value("blend", "fixed"), "fixed") == 0);
    CHECK(strcmp(ico_dev_env_value("blend", "exact"), "exact") == 0);
    CHECK(ico_dev_env_value("blend", "rov") == NULL);
    CHECK(strcmp(ico_dev_env_value("blend", "attachment"), "attachment") == 0);
    CHECK(strcmp(ico_dev_env_value("blend", "storage"), "storage") == 0);
    CHECK(ico_dev_env_value("blend", "auto") == NULL); /* the default: nothing set */
    /* the attachment read on Qualcomm's driver (vk_device.c) */
    CHECK(strcmp(ico_dev_env_name("attachment_read"), "ICO_VK_ATTACHMENT_READ") == 0);
    CHECK(strcmp(ico_dev_env_value("attachment_read", "true"), "1") == 0);
    CHECK(strcmp(ico_dev_env_value("attachment_read", "0"), "0") == 0);
    CHECK(ico_dev_env_value("attachment_read", "maybe") == NULL);
    /* fog_path: the four paths as they are (local: the device profile's
       fogLocal, port/render/rd_profile.c) */
    CHECK(strcmp(ico_dev_env_value("fog_path", "local"), "local") == 0);
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
    /* joystick_directinput: SDL's DirectInput controller scan (issue 105) */
    CHECK(strcmp(ico_dev_env_name("joystick_directinput"), "ICO_JOYSTICK_DIRECTINPUT") == 0);
    CHECK(strcmp(ico_dev_env_value("joystick_directinput", "false"), "0") == 0);
    CHECK(strcmp(ico_dev_env_value("joystick_directinput", "true"), "1") == 0);
    CHECK(ico_dev_env_value("joystick_directinput", "maybe") == NULL);
    CHECK(strcmp(ico_config_ini_key("dev.joystick_directinput"), "joystick_directinput") == 0);
    /* pipeline_set: the start-up set or none (issues 93 and 107) */
    CHECK(strcmp(ico_dev_env_name("pipeline_set"), "ICO_PIPELINE_SET") == 0);
    CHECK(strcmp(ico_dev_env_value("pipeline_set", "lazy"), "lazy") == 0);
    CHECK(strcmp(ico_dev_env_value("pipeline_set", "full"), "full") == 0);
    /* ... and the careful set (window_host.c: rd_pipeline_policy_careful) */
    CHECK(strcmp(ico_dev_env_value("pipeline_set", "careful"), "careful") == 0);
    CHECK(ico_dev_env_value("pipeline_set", "slow") == NULL);
    /* the retired vu_plain_only and vu_extended: still read (an ini key,
       config.toml's [dev] path), never exported */
    CHECK(ico_dev_env_name("vu_plain_only") == NULL);
    CHECK(ico_dev_env_name("vu_extended") == NULL);
    CHECK(ico_dev_env_value("vu_plain_only", "true") == NULL);
    CHECK(ico_dev_env_value("vu_extended", "1") == NULL);
    CHECK(strcmp(ico_config_ini_key("dev.vu_plain_only"), "vu_plain_only") == 0);
    CHECK(strcmp(ico_config_ini_key("dev.vu_extended"), "vu_extended") == 0);
    CHECK(ico_dev_env_value("pipeline_set", "true") == NULL);
    CHECK(ico_dev_env_value("pipeline_set", "") == NULL);
    CHECK(strcmp(ico_config_ini_key("dev.pipeline_set"), "pipeline_set") == 0);
    /* vu_prepass: the VU vertex pre-pass's A/B switch (port/render/rd_replay_vu.c
       rd__vu_prepass_init) */
    CHECK(strcmp(ico_dev_env_name("vu_prepass"), "ICO_RD_VU_PREPASS") == 0);
    CHECK(strcmp(ico_dev_env_value("vu_prepass", "false"), "0") == 0);
    CHECK(strcmp(ico_dev_env_value("vu_prepass", "true"), "1") == 0);
    CHECK(ico_dev_env_value("vu_prepass", "maybe") == NULL);
    CHECK(strcmp(ico_config_ini_key("dev.vu_prepass"), "vu_prepass") == 0);
    /* vu_prepass_batch: the pre-pass's batches' A/B switch
       (port/render/rd_replay_vu.c preBatchInit) */
    CHECK(strcmp(ico_dev_env_name("vu_prepass_batch"), "ICO_RD_VU_PREPASS_BATCH") == 0);
    CHECK(strcmp(ico_dev_env_value("vu_prepass_batch", "false"), "0") == 0);
    CHECK(strcmp(ico_dev_env_value("vu_prepass_batch", "true"), "1") == 0);
    CHECK(strcmp(ico_config_ini_key("dev.vu_prepass_batch"), "vu_prepass_batch") == 0);
    /* crack_fill: the crack filler above 1x's A/B switch
       (port/render/rd_crack_fill.c rd__crack_fill_init) */
    CHECK(strcmp(ico_dev_env_name("crack_fill"), "ICO_RD_CRACK_FILL") == 0);
    CHECK(strcmp(ico_dev_env_value("crack_fill", "false"), "0") == 0);
    CHECK(strcmp(ico_dev_env_value("crack_fill", "true"), "1") == 0);
    CHECK(ico_dev_env_value("crack_fill", "maybe") == NULL);
    CHECK(strcmp(ico_config_ini_key("dev.crack_fill"), "crack_fill") == 0);
    /* perf_passes: every replay timed pass by pass (port/render/rd_perf.c
       rd__perf_passes_init) */
    CHECK(strcmp(ico_dev_env_name("perf_passes"), "ICO_RD_PERF_PASSES") == 0);
    CHECK(strcmp(ico_dev_env_value("perf_passes", "true"), "1") == 0);
    CHECK(strcmp(ico_dev_env_value("perf_passes", "false"), "0") == 0);
    CHECK(ico_dev_env_value("perf_passes", "maybe") == NULL);
    CHECK(strcmp(ico_config_ini_key("dev.perf_passes"), "perf_passes") == 0);
    /* perf_list_stamps: no timestamp inside a render pass (rd_perf.c stamp) */
    CHECK(strcmp(ico_dev_env_name("perf_list_stamps"), "ICO_RD_PERF_LIST_STAMPS") == 0);
    CHECK(strcmp(ico_dev_env_value("perf_list_stamps", "false"), "0") == 0);
    CHECK(strcmp(ico_dev_env_value("perf_list_stamps", "true"), "1") == 0);
    CHECK(strcmp(ico_config_ini_key("dev.perf_list_stamps"), "perf_list_stamps") == 0);
    /* still_represent: a between-tick picture shown again (rd_reshow.c switchOn) */
    CHECK(strcmp(ico_dev_env_name("still_represent"), "ICO_RD_STILL_REPRESENT") == 0);
    CHECK(strcmp(ico_dev_env_value("still_represent", "false"), "0") == 0);
    CHECK(strcmp(ico_dev_env_value("still_represent", "true"), "1") == 0);
    CHECK(strcmp(ico_config_ini_key("dev.still_represent"), "still_represent") == 0);
    /* the device profile's flags (port/render/rd_profile.c rd__profile_init):
       date_path stencil or snapshot as they are, store_plan and blur_fold
       on/off, uint_view on/off or work */
    CHECK(strcmp(ico_dev_env_name("date_path"), "ICO_RD_DATE_PATH") == 0);
    CHECK(strcmp(ico_dev_env_value("date_path", "stencil"), "stencil") == 0);
    CHECK(strcmp(ico_dev_env_value("date_path", "snapshot"), "snapshot") == 0);
    CHECK(ico_dev_env_value("date_path", "true") == NULL);
    CHECK(ico_dev_env_value("date_path", "") == NULL);
    CHECK(strcmp(ico_config_ini_key("dev.date_path"), "date_path") == 0);
    CHECK(strcmp(ico_dev_env_name("store_plan"), "ICO_RD_STORE_PLAN") == 0);
    CHECK(strcmp(ico_dev_env_value("store_plan", "false"), "0") == 0);
    CHECK(strcmp(ico_dev_env_value("store_plan", "true"), "1") == 0);
    CHECK(ico_dev_env_value("store_plan", "planned") == NULL);
    CHECK(strcmp(ico_config_ini_key("dev.store_plan"), "store_plan") == 0);
    CHECK(strcmp(ico_dev_env_name("blur_fold"), "ICO_RD_BLUR_FOLD") == 0);
    CHECK(strcmp(ico_dev_env_value("blur_fold", "off"), "0") == 0);
    CHECK(strcmp(ico_dev_env_value("blur_fold", "on"), "1") == 0);
    CHECK(ico_dev_env_value("blur_fold", "folded") == NULL);
    CHECK(strcmp(ico_config_ini_key("dev.blur_fold"), "blur_fold") == 0);
    CHECK(strcmp(ico_dev_env_name("uint_view"), "ICO_RD_UINT_VIEW") == 0);
    CHECK(strcmp(ico_dev_env_value("uint_view", "true"), "1") == 0);
    CHECK(strcmp(ico_dev_env_value("uint_view", "false"), "0") == 0);
    CHECK(strcmp(ico_dev_env_value("uint_view", "work"), "work") == 0);
    CHECK(ico_dev_env_value("uint_view", "scene") == NULL);
    CHECK(strcmp(ico_config_ini_key("dev.uint_view"), "uint_view") == 0);
    /* the pixel diet's flags: alpha_plan, fx_plan, fx_dst_local and
       vu_planes on/off */
    CHECK(strcmp(ico_dev_env_name("alpha_plan"), "ICO_RD_ALPHA_PLAN") == 0);
    CHECK(strcmp(ico_dev_env_value("alpha_plan", "false"), "0") == 0);
    CHECK(strcmp(ico_dev_env_value("alpha_plan", "on"), "1") == 0);
    CHECK(ico_dev_env_value("alpha_plan", "planned") == NULL);
    CHECK(strcmp(ico_config_ini_key("dev.alpha_plan"), "alpha_plan") == 0);
    CHECK(strcmp(ico_dev_env_name("fx_plan"), "ICO_RD_FX_PLAN") == 0);
    CHECK(strcmp(ico_dev_env_value("fx_plan", "off"), "0") == 0);
    CHECK(strcmp(ico_dev_env_value("fx_plan", "true"), "1") == 0);
    CHECK(ico_dev_env_value("fx_plan", "constant") == NULL);
    CHECK(strcmp(ico_config_ini_key("dev.fx_plan"), "fx_plan") == 0);
    CHECK(strcmp(ico_dev_env_name("fx_dst_local"), "ICO_RD_FX_DST_LOCAL") == 0);
    CHECK(strcmp(ico_dev_env_value("fx_dst_local", "no"), "0") == 0);
    CHECK(strcmp(ico_dev_env_value("fx_dst_local", "yes"), "1") == 0);
    CHECK(ico_dev_env_value("fx_dst_local", "local") == NULL);
    CHECK(strcmp(ico_config_ini_key("dev.fx_dst_local"), "fx_dst_local") == 0);
    CHECK(strcmp(ico_dev_env_name("vu_planes"), "ICO_RD_VU_PLANES") == 0);
    CHECK(strcmp(ico_dev_env_value("vu_planes", "false"), "0") == 0);
    CHECK(strcmp(ico_dev_env_value("vu_planes", "1"), "1") == 0);
    CHECK(ico_dev_env_value("vu_planes", "fractional") == NULL);
    CHECK(strcmp(ico_config_ini_key("dev.vu_planes"), "vu_planes") == 0);
    /* shadow_one: the shadow count's one draw on/off (rd_replay.c) */
    CHECK(strcmp(ico_dev_env_name("shadow_one"), "ICO_RD_SHADOW_ONE") == 0);
    CHECK(strcmp(ico_dev_env_value("shadow_one", "false"), "0") == 0);
    CHECK(strcmp(ico_dev_env_value("shadow_one", "on"), "1") == 0);
    CHECK(ico_dev_env_value("shadow_one", "seven") == NULL);
    CHECK(strcmp(ico_config_ini_key("dev.shadow_one"), "shadow_one") == 0);
    /* dynamic_state: the variable names what is static (vk_device.c) */
    CHECK(strcmp(ico_dev_env_name("dynamic_state"), "ICO_VK_FAKE_NO_DYNAMIC") == 0);
    CHECK(strcmp(ico_dev_env_value("dynamic_state", "true"), "0") == 0);
    CHECK(strcmp(ico_dev_env_value("dynamic_state", "false"), "1") == 0);
    CHECK(strcmp(ico_dev_env_value("dynamic_state", "depth"), "blend") == 0);
    CHECK(ico_dev_env_value("dynamic_state", "blend") == NULL);
    CHECK(strcmp(ico_config_ini_key("dev.dynamic_state"), "dynamic_state") == 0);
    /* audio_sync: the audio on the game's clock (port/audio/audio_host.h) */
    CHECK(strcmp(ico_dev_env_name("audio_sync"), "ICO_AUDIO_SYNC") == 0);
    CHECK(strcmp(ico_dev_env_value("audio_sync", "true"), "1") == 0);
    CHECK(strcmp(ico_dev_env_value("audio_sync", "false"), "0") == 0);
    CHECK(ico_dev_env_value("audio_sync", "always") == NULL);
    CHECK(strcmp(ico_config_ini_key("dev.audio_sync"), "audio_sync") == 0);
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
    /* blend=exact is exported (the opt-in to the device's best exact
       mode); an unknown path leaves the default; false is 0 */
    ico_ini_parse(&ini, "blend=exact\nfog_path=sideways\ngpu_robust=false\n");
    ico_ini_export(&ini, "dev-env-test.ini", 0);
    CHECK(env_is("ICO_RD_BLEND", "exact"));
    CHECK(env_is("ICO_RD_FOG_PATH", NULL));
    CHECK(env_is("ICO_GPU_CHECKPOINTS", NULL));
    CHECK(env_is("ICO_GPU_ROBUST", "0"));
    for (i = 0; i < sizeof(vars) / sizeof(vars[0]); i++) {
        unset_env(vars[i]);
    }
    /* the retired keys are logged ("ico_pc: [dev] vu_plain_only = true is not
       used ...; ignored") and set nothing */
    unset_env("ICO_RD_VU_PLAIN");
    unset_env("ICO_RD_VU_EXTENDED");
    ico_ini_parse(&ini, "[dev]\nvu_plain_only = true\nvu_extended = 1\n");
    ico_ini_export(&ini, "dev-env-test.ini", 0);
    CHECK(env_is("ICO_RD_VU_PLAIN", NULL));
    CHECK(env_is("ICO_RD_VU_EXTENDED", NULL));
    /* blend=attachment and attachment_read=true (config.toml's [dev]) */
    ico_ini_parse(&ini, "[dev]\nblend = attachment\nattachment_read = true\n");
    ico_ini_export(&ini, "dev-env-test.ini", 0);
    CHECK(env_is("ICO_RD_BLEND", "attachment"));
    CHECK(env_is("ICO_VK_ATTACHMENT_READ", "1"));
    for (i = 0; i < sizeof(vars) / sizeof(vars[0]); i++) {
        unset_env(vars[i]);
    }
    /* the profile's keys from config.toml's [dev] (rd_profile.c reads the
       variables) */
    ico_ini_parse(&ini, "[dev]\nfog_path = local\ndate_path = stencil\nstore_plan = false\n"
                        "blur_fold = true\nuint_view = work\n");
    ico_ini_export(&ini, "dev-env-test.ini", 0);
    CHECK(env_is("ICO_RD_FOG_PATH", "local"));
    CHECK(env_is("ICO_RD_DATE_PATH", "stencil"));
    CHECK(env_is("ICO_RD_STORE_PLAN", "0"));
    CHECK(env_is("ICO_RD_BLUR_FOLD", "1"));
    CHECK(env_is("ICO_RD_UINT_VIEW", "work"));
    for (i = 0; i < sizeof(vars) / sizeof(vars[0]); i++) {
        unset_env(vars[i]);
    }
    ico_ini_parse(&ini, "[dev]\nalpha_plan = false\nfx_plan = true\nfx_dst_local = on\n"
                        "vu_planes = off\n");
    ico_ini_export(&ini, "dev-env-test.ini", 0);
    CHECK(env_is("ICO_RD_ALPHA_PLAN", "0"));
    CHECK(env_is("ICO_RD_FX_PLAN", "1"));
    CHECK(env_is("ICO_RD_FX_DST_LOCAL", "1"));
    CHECK(env_is("ICO_RD_VU_PLANES", "0"));
    for (i = 0; i < sizeof(vars) / sizeof(vars[0]); i++) {
        unset_env(vars[i]);
    }
    ico_ini_parse(&ini, "date_path=snapshot\nuint_view=false\nstore_plan=maybe\n"
                        "dynamic_state=depth\n");
    ico_ini_export(&ini, "dev-env-test.ini", 0);
    CHECK(env_is("ICO_RD_DATE_PATH", "snapshot"));
    CHECK(env_is("ICO_RD_UINT_VIEW", "0"));
    CHECK(env_is("ICO_RD_STORE_PLAN", NULL));
    CHECK(env_is("ICO_VK_FAKE_NO_DYNAMIC", "blend"));
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
    CHECK(strcmp(ico_dev_env_export_value("blend", "exact", NULL), "exact") ==
          0); /* the opt-in word (rd_dst_read.c rd__dst_asked_env) */
    CHECK(strcmp(ico_dev_env_export_value("blend", "fixed", NULL), "fixed") == 0);
    /* the other switches are not touched by the restart */
    CHECK(ico_dev_env_export_value("fog_path", NULL, "blend:vulkan") == NULL);
    CHECK(strcmp(ico_dev_env_export_value("gpu_robust", "1", "blend:vulkan"), "1") == 0);
    CHECK(ico_dev_env_export_value("gpu_robust", NULL, "blend:vulkan") == NULL);
    /* the loss order's robust step: robust access on over gpu_robust, its
       blend the run's own */
    CHECK(strcmp(ico_dev_env_export_value("gpu_robust", NULL, "robust:vulkan"), "1") == 0);
    CHECK(strcmp(ico_dev_env_export_value("gpu_robust", "false", "robust:d3d12"), "1") == 0);
    CHECK(ico_dev_env_export_value("blend", NULL, "robust:vulkan") == NULL);
    CHECK(strcmp(ico_dev_env_export_value("vu_prepass", NULL, "robust:vulkan"), "0") == 0);

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
    /* no restart: blend=exact is the player's opt-in, exported */
    unset_env("ICO_STARTUP_RETRY");
    unset_env("ICO_RD_BLEND");
    ico_ini_parse(&ini, "blend=exact\n");
    ico_ini_export(&ini, "dev-env-test.ini", 0);
    CHECK(env_is("ICO_RD_BLEND", "exact"));
    unset_env("ICO_RD_BLEND");
    /* the robust step's run: ICO_GPU_ROBUST=1 over the ini's gpu_robust */
    unset_env("ICO_GPU_ROBUST");
    set_env("ICO_STARTUP_RETRY", "robust:vulkan");
    ico_ini_parse(&ini, "gpu_robust=false\n");
    ico_ini_export(&ini, "dev-env-test.ini", 0);
    CHECK(env_is("ICO_GPU_ROBUST", "1"));
    CHECK(env_is("ICO_RD_BLEND", NULL));
    unset_env("ICO_STARTUP_RETRY");
    unset_env("ICO_GPU_ROBUST");
}

/* ICO_RD_VU_PREPASS in a restart: 0 at the crash order's noprepass step
   and every step after it (fixedblend, d3d12, lazy) and at every loss
   step (lossnoprepass without the fixed blend), over the ini's
   vu_prepass=; the cache step and a first run keep the ini's */
static void test_retry_prepass(void)
{
    static const struct {
        const char *retry;
        const char *prepass, *blend;
    } runs[] = {
        {"noprepass:vulkan", "0", NULL},    {"fixedblend:vulkan", "0", "fixed"},
        {"d3d12:d3d12", "0", NULL},         {"lazy:vulkan", "0", NULL},
        {"lossnoprepass:d3d12", "0", NULL}, {"robust:vulkan", "0", NULL},
        {"blend:vulkan", "0", "fixed"},     {"backend:d3d12", "0", "fixed"},
        {"cache:vulkan", "1", NULL},        {NULL, "1", NULL},
    };

    IcoIni ini;

    /* the pure rule */
    CHECK(strcmp(ico_dev_env_export_value("vu_prepass", "true", "noprepass:vulkan"), "0") == 0);
    CHECK(strcmp(ico_dev_env_export_value("vu_prepass", NULL, "lossnoprepass:vulkan"), "0") == 0);
    CHECK(ico_dev_env_export_value("vu_prepass", NULL, "cache:vulkan") == NULL);
    CHECK(strcmp(ico_dev_env_export_value("vu_prepass", "true", "cache:vulkan"), "1") == 0);
    CHECK(ico_dev_env_export_value("blend", NULL, "lossnoprepass:vulkan") == NULL);
    CHECK(ico_dev_env_export_value("blend", NULL, "noprepass:vulkan") == NULL);
    for (size_t i = 0; i < sizeof(runs) / sizeof(runs[0]); i++) {
        unset_env("ICO_RD_VU_PREPASS");
        unset_env("ICO_RD_BLEND");
        if (runs[i].retry != NULL) {
            set_env("ICO_STARTUP_RETRY", runs[i].retry);
        } else {
            unset_env("ICO_STARTUP_RETRY");
        }
        ico_ini_parse(&ini, "vu_prepass=true\n");
        ico_ini_export(&ini, "dev-env-test.ini", 0);
        if (!env_is("ICO_RD_VU_PREPASS", runs[i].prepass) ||
            !env_is("ICO_RD_BLEND", runs[i].blend)) {
            fprintf(stderr, "retry %s:\n", runs[i].retry != NULL ? runs[i].retry : "(none)");
        }
        CHECK(env_is("ICO_RD_VU_PREPASS", runs[i].prepass));
        CHECK(env_is("ICO_RD_BLEND", runs[i].blend));
    }
    unset_env("ICO_STARTUP_RETRY");
    unset_env("ICO_RD_VU_PREPASS");
    unset_env("ICO_RD_BLEND");
    unset_env("ICO_GPU_ROBUST"); /* the robust step's */
}

/* ico_host_choose_disc: the rules in order (pin, the image given, the only
   archive, the disc played last, the European disc) and the words for the
   log; values outside the rows name none. ico_host_choose_image: the image
   used when several discs' images are beside the program. */
static void test_choose_disc(void)
{
    const char *why = NULL;

    /* nothing: the European disc */
    CHECK(ico_host_choose_disc(-1, -1, 0u, -1, 2, &why) == 0 &&
          strcmp(why, "the European disc") == 0);
    /* the pin wins over everything */
    CHECK(ico_host_choose_disc(1, 0, 1u, 0, 2, &why) == 1 && strcmp(why, "[game] disc") == 0);
    CHECK(ico_host_choose_disc(0, 1, 2u, 1, 2, &why) == 0 && strcmp(why, "[game] disc") == 0);
    /* then the image given, over the archives and the last disc */
    CHECK(ico_host_choose_disc(-1, 1, 1u, 0, 2, &why) == 1 &&
          strcmp(why, "the disc image given") == 0);
    CHECK(ico_host_choose_disc(-1, 0, 3u, 1, 2, &why) == 0 &&
          strcmp(why, "the disc image given") == 0);
    /* then the only archive there */
    CHECK(ico_host_choose_disc(-1, -1, 2u, 0, 2, &why) == 1 &&
          strcmp(why, "the only game data there") == 0);
    CHECK(ico_host_choose_disc(-1, -1, 1u, 1, 2, &why) == 0 &&
          strcmp(why, "the only game data there") == 0);
    /* both archives (or none): the disc played last, else the European one */
    CHECK(ico_host_choose_disc(-1, -1, 3u, 1, 2, &why) == 1 &&
          strcmp(why, "the disc played last") == 0);
    CHECK(ico_host_choose_disc(-1, -1, 0u, 1, 2, &why) == 1 &&
          strcmp(why, "the disc played last") == 0);
    CHECK(ico_host_choose_disc(-1, -1, 3u, -1, 2, &why) == 0 &&
          strcmp(why, "the European disc") == 0);
    /* rows that are not there name none; an archive bit past the rows does
       not count; NULL words are allowed */
    CHECK(ico_host_choose_disc(2, 5, 4u, 7, 2, &why) == 0 && strcmp(why, "the European disc") == 0);
    CHECK(ico_host_choose_disc(-1, -1, 2u | 4u, -1, 2, NULL) == 1);
    /* three rows: the Korean disc as the pin, the image given, the only
       archive and the disc played last */
    CHECK(ico_host_choose_disc(2, 0, 1u, 1, 3, &why) == 2 && strcmp(why, "[game] disc") == 0);
    CHECK(ico_host_choose_disc(-1, 2, 7u, 0, 3, &why) == 2 &&
          strcmp(why, "the disc image given") == 0);
    CHECK(ico_host_choose_disc(-1, -1, 4u, 0, 3, &why) == 2 && why != NULL);
    CHECK(ico_host_choose_disc(-1, -1, 7u, 2, 3, &why) == 2 &&
          strcmp(why, "the disc played last") == 0);
    CHECK(ico_host_choose_disc(-1, -1, 7u, -1, 3, &why) == 0 &&
          strcmp(why, "the European disc") == 0);

    /* ico_host_choose_image (find_beside, both images and no pin): the
       image whose disc has no game data yet first, so a second disc's
       image put beside the first one's is set up at the next start */
    CHECK(ico_host_choose_image(3u, 1u, -1, 2, &why) == 1 &&
          strcmp(why, "the disc not set up yet") == 0);
    CHECK(ico_host_choose_image(3u, 2u, 1, 2, &why) == 0 &&
          strcmp(why, "the disc not set up yet") == 0);
    /* neither set up: the disc played last, else the European one */
    CHECK(ico_host_choose_image(3u, 0u, 1, 2, &why) == 1 &&
          strcmp(why, "the disc not set up yet, the disc played last") == 0);
    CHECK(ico_host_choose_image(3u, 0u, -1, 2, &why) == 0 &&
          strcmp(why, "the disc not set up yet, the European disc") == 0);
    /* both set up: the disc played last, else the European one */
    CHECK(ico_host_choose_image(3u, 3u, 1, 2, &why) == 1 &&
          strcmp(why, "the disc played last") == 0);
    CHECK(ico_host_choose_image(3u, 3u, -1, 2, &why) == 0 && strcmp(why, "the European disc") == 0);
    CHECK(ico_host_choose_image(3u, 3u, 7, 2, &why) == 0 && strcmp(why, "the European disc") == 0);
    /* one image: that one, set up or not */
    CHECK(ico_host_choose_image(2u, 3u, 0, 2, &why) == 1 &&
          strcmp(why, "the only image there") == 0);
    CHECK(ico_host_choose_image(1u, 0u, 1, 2, &why) == 0 &&
          strcmp(why, "the disc not set up yet") == 0);
    /* none (or only bits past the rows): -1 */
    CHECK(ico_host_choose_image(0u, 3u, 0, 2, &why) == -1 && strcmp(why, "no image") == 0);
    CHECK(ico_host_choose_image(4u, 0u, 0, 2, NULL) == -1);
    /* a third row: the first one not set up, the last one among those */
    CHECK(ico_host_choose_image(7u, 1u, -1, 3, &why) == 1 &&
          strcmp(why, "the disc not set up yet, the first row there") == 0);
    CHECK(ico_host_choose_image(7u, 1u, 2, 3, &why) == 2);
}

/* ico_host_set_logs_dir: every later ico_host_logs_dir names the folder
   set, "" goes back to <exe dir>/logs */
static void test_logs_override(void)
{
    char exe[ICO_PATH_MAX], want[ICO_PATH_MAX], logs[ICO_PATH_MAX];

    ico_host_exe_dir(exe, sizeof(exe));
    ico_path_join(want, sizeof(want), exe, "logs");
    ico_host_set_logs_dir("/home/player/.local/share/ico-pc/ico-pc/logs");
    CHECK(ico_host_logs_dir(logs, sizeof(logs)) == 0 &&
          strcmp(logs, "/home/player/.local/share/ico-pc/ico-pc/logs") == 0);
    CHECK(ico_host_logs_dir(logs, 8) == -1 && logs[0] == '\0');
    ico_host_set_logs_dir("");
    CHECK(ico_host_logs_dir(logs, sizeof(logs)) == 0 && strcmp(logs, want) == 0);
    ico_host_set_logs_dir(NULL);
    CHECK(ico_host_logs_dir(logs, sizeof(logs)) == 0 && strcmp(logs, want) == 0);
}

#if defined(__linux__) && !defined(__ANDROID__)
/* An AppImage: with APPIMAGE the file's path and APPDIR a folder the
   running program lies under (here the test's own folder stands in for
   the mount), the program's folder is the AppImage file's folder, and
   ico-pc.ini, logs/ and userdata/ follow it. APPDIR not holding the
   program, missing, the root folder or a name that only starts the same
   way, and APPIMAGE relative, a folder or empty: nothing moves. */
static void test_appimage(void)
{
    char real[ICO_PATH_MAX], dir[ICO_PATH_MAX], file[ICO_PATH_MAX], got[ICO_PATH_MAX];
    char want[ICO_PATH_MAX], tmp[ICO_PATH_MAX + 2], self[ICO_PATH_MAX], near[ICO_PATH_MAX];
    const char *why = "unset";
    FILE *f;
    long n;

    unsetenv("APPIMAGE");
    unsetenv("APPDIR");
    CHECK(ico_appimage_file(got, sizeof(got), &why) == 0 && got[0] == '\0' && why == NULL);
    CHECK(ico_host_exe_dir(real, sizeof(real)) == 0);
    ico_path_join(dir, sizeof(dir), real, "appimage_test_dir");
    ico_path_join(file, sizeof(file), dir, "ICO-v0.0.0-linux-x86_64.AppImage");
    CHECK(ico_make_dir(dir) == 0);
    f = fopen(file, "wb");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    fputs("not an AppImage\n", f);
    fclose(f);

    /* the program runs from inside APPDIR: the file's folder */
    setenv("APPIMAGE", file, 1);
    setenv("APPDIR", real, 1);
    why = "unset";
    CHECK(ico_appimage_file(got, sizeof(got), &why) == 1 && strcmp(got, file) == 0 && why == NULL);
    CHECK(ico_host_exe_dir(got, sizeof(got)) == 0 && strcmp(got, dir) == 0);
    ico_path_join(want, sizeof(want), dir, "ico-pc.ini");
    CHECK(ico_host_ini_path(got, sizeof(got)) == 0 && strcmp(got, want) == 0);
    ico_path_join(want, sizeof(want), dir, "logs");
    CHECK(ico_host_logs_dir(got, sizeof(got)) == 0 && strcmp(got, want) == 0);
    /* a path that does not fit: 0 and "" */
    CHECK(ico_appimage_file(got, 8, &why) == 0 && got[0] == '\0' && why != NULL);
#ifndef ICO_HOST_FORCE_PORTABLE
    /* a userdata folder beside the AppImage file turns portable mode on */
    ico_path_join(want, sizeof(want), dir, "userdata");
    CHECK(ico_make_dir(want) == 0);
    ico_host_set_portable(-1);
    CHECK(ico_host_pref_is_portable());
    CHECK(ico_host_pref_dir(got, sizeof(got)) == 0 && strcmp(got, want) == 0);
    remove(want);
    ico_host_set_portable(-1);
#endif
    /* APPDIR with a trailing separator, and a folder above the program */
    snprintf(tmp, sizeof(tmp), "%s/", real);
    setenv("APPDIR", tmp, 1);
    CHECK(ico_appimage_file(got, sizeof(got), NULL) == 1);
    snprintf(tmp, sizeof(tmp), "%s", real);
    if (strrchr(tmp, '/') != NULL && strrchr(tmp, '/') != tmp) {
        *strrchr(tmp, '/') = '\0';
        setenv("APPDIR", tmp, 1);
        CHECK(ico_appimage_file(got, sizeof(got), NULL) == 1);
    }

    /* the program outside APPDIR: a stray APPIMAGE moves nothing */
    setenv("APPDIR", dir, 1);
    why = NULL;
    CHECK(ico_appimage_file(got, sizeof(got), &why) == 0 && got[0] == '\0' && why != NULL);
    CHECK(ico_host_exe_dir(got, sizeof(got)) == 0 && strcmp(got, real) == 0);
    /* a folder whose name only starts the program's path */
    n = (long)readlink("/proc/self/exe", self, sizeof(self) - 1);
    CHECK(n > 1);
    if (n > 1) {
        self[n] = '\0';
        snprintf(near, sizeof(near), "%.*s", (int)(n - 1), self);
        if (ico_make_dir(near) == 0) {
            setenv("APPDIR", near, 1);
            why = NULL;
            CHECK(ico_appimage_file(got, sizeof(got), &why) == 0 && why != NULL);
            remove(near);
        }
    }
    /* APPDIR the root folder, missing, unset */
    setenv("APPDIR", "/", 1);
    why = NULL;
    CHECK(ico_appimage_file(got, sizeof(got), &why) == 0 && why != NULL);
    ico_path_join(tmp, sizeof(tmp), real, "appimage_test_missing");
    setenv("APPDIR", tmp, 1);
    why = NULL;
    CHECK(ico_appimage_file(got, sizeof(got), &why) == 0 && why != NULL);
    unsetenv("APPDIR");
    why = NULL;
    CHECK(ico_appimage_file(got, sizeof(got), &why) == 0 && why != NULL);
    CHECK(ico_host_exe_dir(got, sizeof(got)) == 0 && strcmp(got, real) == 0);
    /* APPIMAGE relative, a folder, empty */
    setenv("APPDIR", real, 1);
    setenv("APPIMAGE", "ICO.AppImage", 1);
    why = NULL;
    CHECK(ico_appimage_file(got, sizeof(got), &why) == 0 && why != NULL);
    setenv("APPIMAGE", dir, 1);
    why = NULL;
    CHECK(ico_appimage_file(got, sizeof(got), &why) == 0 && why != NULL);
    setenv("APPIMAGE", "", 1);
    why = "unset";
    CHECK(ico_appimage_file(got, sizeof(got), &why) == 0 && why == NULL);
    CHECK(ico_host_exe_dir(got, sizeof(got)) == 0 && strcmp(got, real) == 0);

    unsetenv("APPIMAGE");
    unsetenv("APPDIR");
    remove(file);
    remove(dir);
}
#endif

int main(void)
{
    test_choose_disc();
    test_sha1();
    test_ini();
    test_join_overflow();
    test_saves_dir_pure();
    test_saves_dir_layered();
    test_paths();
    test_portable_value();
    test_dev_env();
    test_retry_blend();
    test_retry_prepass();
    test_logs_override();
#if defined(__linux__) && !defined(__ANDROID__)
    test_appimage();
#endif
#ifdef ICO_HOST_FORCE_PORTABLE
    test_forced_portable();
#else
    test_portable();
#endif
    printf("host_config_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
