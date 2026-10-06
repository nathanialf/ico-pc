/*
 * port/platform/test/host_config_test.c
 *
 * ico-pc.ini parsing and rewriting, SHA-1 against FIPS 180 test vectors,
 * path joining (port/platform/host_config.c).
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
    fclose(f);
    CHECK(ico_host_saves_dir(out, sizeof(out)) == 0);
    CHECK(strstr(out, "pure-cards") != NULL);
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

int main(void)
{
    test_sha1();
    test_ini();
    test_join_overflow();
    test_saves_dir_pure();
    test_paths();
    printf("host_config_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
