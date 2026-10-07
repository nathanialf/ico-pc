/*
 * port/config/test/config_test.c
 *
 * config.toml: the reader, the writer (render and atomic save), round trips,
 * the ini > config.toml > default precedence; the language sceScfGetLanguage
 * answers; the wall clock and the EE timers.
 *
 * Usage: config_test [scratch folder]
 */
#include "clock.h"
#include "config.h"
#include "host_config.h"
#include "sysconf.h"
#include <eeregs.h>
#include <libscf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "host_loop.h"

/* stands in for the game's mode word (common/src/main.c:37) */
static int systemStatus[12];

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define CHECK_STR(got, want)                                                                       \
    do {                                                                                           \
        const char *g_ = (got);                                                                    \
        const char *w_ = (want);                                                                   \
        if (g_ == NULL || strcmp(g_, w_) != 0) {                                                   \
            fprintf(stderr, "%s:%d: got [%s], want [%s]\n", __FILE__, __LINE__,                    \
                    g_ != NULL ? g_ : "(null)", w_);                                               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static char scratch[ICO_PATH_MAX];

static void set_env(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value != NULL ? value : "");
#else
    if (value != NULL) {
        setenv(name, value, 1);
    } else {
        unsetenv(name);
    }
#endif
}

static void path_in(char *out, const char *name)
{
    ico_path_join(out, ICO_PATH_MAX, scratch, name);
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");

    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        failures++;
        return;
    }
    fputs(text, f);
    fclose(f);
}

/* the file's text, malloc'd; NULL if it cannot be read */
static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *text;
    long n;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = malloc((size_t)n + 1);
    n = (long)fread(text, 1, (size_t)n, f);
    text[n] = '\0';
    fclose(f);
    return text;
}

static void test_parse(void)
{
    IcoToml *t = ico_toml_parse("version = 1\n"
                                "# a comment\n"
                                "[video]\n"
                                "preset = \"original\"  # trailing\n"
                                "vsync = true\n"
                                "[audio]\n"
                                "volume = 0.5\n"
                                "[input.kb]\n"
                                "cross = \"Z\"\n"
                                "[input]\n"
                                "kb.circle = 'X'\n"
                                "pads = [\"a\", \"b\"]\n"
                                "[game]\n"
                                "path = \"C:\\\\Games\\\\\\\"ICO\\\"\"\n");

    CHECK(t != NULL);
    CHECK(ico_toml_get_int(t, "version", 0) == 1);
    CHECK_STR(ico_toml_get(t, "video.preset"), "original");
    CHECK(ico_toml_get_bool(t, "video.vsync", 0) == 1);
    CHECK(ico_toml_get_float(t, "audio.volume", 0) == 0.5);
    CHECK_STR(ico_toml_get(t, "input.kb.cross"), "Z");
    CHECK_STR(ico_toml_get(t, "input.kb.circle"), "X");
    CHECK_STR(ico_toml_get(t, "input.pads"), "[\"a\", \"b\"]");
    CHECK_STR(ico_toml_get(t, "game.path"), "C:\\Games\\\"ICO\"");
    CHECK(ico_toml_get(t, "video.missing") == NULL);
    CHECK(ico_toml_has(t, "video.vsync") && !ico_toml_has(t, "video.nope"));
    ico_toml_free(t);
}

static void test_render(void)
{
    const char *text = "# ICO PC settings\n"
                       "version = 1\n"
                       "\n"
                       "[video]\n"
                       "preset = \"original\"  # keep this comment? no: rewritten lines lose it\n"
                       "vsync = true\n"
                       "\n"
                       "# the pad\n"
                       "[input]\n"
                       "kb.cross = \"Z\"\n"
                       "future_key = [1, 2,\n"
                       "   3]\n"
                       "\n"
                       "[audio]\n"
                       "volume = 1.0\n";
    IcoToml *t = ico_toml_parse(text);
    char *out;
    IcoToml *again;

    /* unchanged: byte for byte */
    out = ico_toml_render(t, text);
    CHECK_STR(out, text);
    free(out);

    /* one change; a key added to an existing section lands at its end, a
       top-level key before the first header, a new section at the end */
    ico_toml_set_float(t, "audio.volume", 0.25);
    ico_toml_set_bool(t, "video.fullscreen", 1);
    ico_toml_set_string(t, "game.language", "fr");
    ico_toml_set_int(t, "extra", 7);
    ico_toml_set_string(t, "input.kb.cross", "A\\B\"C");
    out = ico_toml_render(t, text);
    CHECK_STR(out, "# ICO PC settings\n"
                   "version = 1\n"
                   "extra = 7\n"
                   "\n"
                   "[video]\n"
                   "preset = \"original\"  # keep this comment? no: rewritten lines lose it\n"
                   "vsync = true\n"
                   "fullscreen = true\n"
                   "\n"
                   "# the pad\n"
                   "[input]\n"
                   "kb.cross = \"A\\\\B\\\"C\"\n"
                   "future_key = [1, 2,\n"
                   "   3]\n"
                   "\n"
                   "[audio]\n"
                   "volume = 0.25\n"
                   "\n"
                   "[game]\n"
                   "language = \"fr\"\n");
    again = ico_toml_parse(out);
    CHECK_STR(ico_toml_get(again, "input.kb.cross"), "A\\B\"C");
    CHECK(ico_toml_get_float(again, "audio.volume", 0) == 0.25);
    CHECK_STR(ico_toml_get(again, "game.language"), "fr");
    CHECK(ico_toml_get_int(again, "extra", 0) == 7);
    ico_toml_free(again);
    free(out);
    ico_toml_free(t);
}

static void test_render_edges(void)
{
    IcoToml *t = ico_toml_parse("");
    char *out;

    /* a new file: top-level keys, then sections in the order set */
    ico_toml_set_int(t, "version", 1);
    ico_toml_set_string(t, "video.preset", "original");
    ico_toml_set_bool(t, "video.vsync", 1);
    ico_toml_set_float(t, "audio.volume", 1.0);
    out = ico_toml_render(t, NULL);
    CHECK_STR(out, "version = 1\n\n[video]\npreset = \"original\"\nvsync = true\n\n[audio]\n"
                   "volume = 1.0\n");
    free(out);
    ico_toml_free(t);

    /* CRLF files stay CRLF; a last line without a newline gets one */
    t = ico_toml_parse("[a]\r\nx = 1\r\n[b]\r\ny = 2");
    ico_toml_set_int(t, "a.z", 3);
    ico_toml_set_int(t, "b.y", 5);
    ico_toml_set_int(t, "b.w", 6);
    out = ico_toml_render(t, "[a]\r\nx = 1\r\n[b]\r\ny = 2");
    CHECK_STR(out, "[a]\r\nx = 1\r\nz = 3\r\n[b]\r\ny = 5\r\nw = 6\r\n");
    free(out);
    ico_toml_free(t);

    /* a repeated key: the reader keeps the last, every line is brought to it */
    t = ico_toml_parse("[a]\nx = 1\nx = 2\n");
    CHECK(ico_toml_get_int(t, "a.x", 0) == 2);
    ico_toml_free(t);

    /* floats keep a fraction */
    t = ico_toml_parse("");
    ico_toml_set_float(t, "f", 3.0);
    CHECK_STR(ico_toml_get(t, "f"), "3.0");
    ico_toml_set_float(t, "f", 0.1);
    CHECK(ico_toml_get_float(t, "f", 0) > 0.0999 && ico_toml_get_float(t, "f", 0) < 0.1001);
    ico_toml_free(t);
}

static void test_save(void)
{
    char toml[ICO_PATH_MAX], tmp[ICO_PATH_MAX + 8];
    char *text;
    IcoToml *t;

    path_in(toml, "save-test.toml");
    snprintf(tmp, sizeof(tmp), "%s.tmp", toml);
    remove(toml);
    ico_config_reset(toml, "no-such.ini");
    CHECK_STR(ico_config_get_string("game.language", "auto"), "auto");
    CHECK(ico_config_set_string("game.language", "de") == 0);
    CHECK(ico_config_save() == 0);
    CHECK(!ico_file_exists(tmp)); /* the temp file was renamed away */
    text = read_file(toml);
    CHECK(text != NULL);
    if (text != NULL) {
        CHECK(strstr(text, "version = 1\n") != NULL);
        CHECK(strstr(text, "[video]\npreset = \"original\"\nvsync = true\nfullscreen = false\n") !=
              NULL);
        CHECK(strstr(text, "[audio]\nenabled = true\nvolume = 1.0\nmusic = 1.0\neffects = 1.0\n"
                           "output = \"auto\"\ndevice = \"\"\n") != NULL);
        CHECK(strstr(text, "[game]\nlanguage = \"de\"\n") != NULL);
        CHECK(strstr(text, "[paths]\niso = \"\"\n") != NULL);
        free(text);
    }
    t = ico_toml_load(toml);
    CHECK(ico_toml_get_int(t, "version", 0) == ICO_CONFIG_VERSION);
    CHECK_STR(ico_toml_get(t, "game.language"), "de");
    ico_toml_free(t);

    /* the sound options round trip */
    ico_config_reset(toml, "no-such.ini");
    CHECK(ico_config_get_float("audio.music", 0.0) == 1.0);
    CHECK(ico_config_get_float("audio.effects", 0.0) == 1.0);
    CHECK_STR(ico_config_get_string("audio.output", "?"), "auto");
    CHECK_STR(ico_config_get_string("audio.device", "?"), "");
    CHECK(ico_config_set_float("audio.music", 0.3) == 0);
    CHECK(ico_config_set_float("audio.effects", 0.7) == 0);
    CHECK(ico_config_set_string("audio.output", "mono") == 0);
    CHECK(ico_config_set_string("audio.device", "USB Audio \"Headset\" #2") == 0);
    CHECK(ico_config_save() == 0);
    ico_config_reset(toml, "no-such.ini");
    CHECK(ico_config_get_float("audio.music", 0.0) > 0.2999 &&
          ico_config_get_float("audio.music", 0.0) < 0.3001);
    CHECK(ico_config_get_float("audio.effects", 0.0) > 0.6999 &&
          ico_config_get_float("audio.effects", 0.0) < 0.7001);
    CHECK_STR(ico_config_get_string("audio.output", "?"), "mono");
    CHECK_STR(ico_config_get_string("audio.device", "?"), "USB Audio \"Headset\" #2");

    /* a hand-edited file: comments, an unknown key, 4C's bindings survive a
       save that changes one value */
    write_file(toml, "version = 1\n# mine\n[input]\nkb.cross = \"Q\"\n[future]\nx = [1, 2]\n"
                     "[game]\nlanguage = \"it\"  # c\n");
    ico_config_reset(toml, "no-such.ini");
    CHECK_STR(ico_config_get_string("game.language", "auto"), "it");
    ico_config_set_string("game.language", "es");
    CHECK(ico_config_save() == 0);
    text = read_file(toml);
    CHECK(text != NULL);
    if (text != NULL) {
        CHECK(strstr(text, "# mine\n[input]\nkb.cross = \"Q\"\n[future]\nx = [1, 2]\n") != NULL);
        CHECK(strstr(text, "language = \"es\"\n") != NULL);
        CHECK(strstr(text, "[video]") != NULL);
        free(text);
    }
    /* the same save again changes nothing */
    {
        char *a = read_file(toml);
        char *b;

        ico_config_reset(toml, "no-such.ini");
        CHECK(ico_config_save() == 0);
        b = read_file(toml);
        CHECK(a != NULL && b != NULL && strcmp(a, b) == 0);
        free(a);
        free(b);
    }
    /* the retired [game] classic_menu_text and port_font (package TXT2):
       read without complaint, logged once, kept by a save */
    write_file(toml, "version = 1\n[game]\nclassic_menu_text = true\nport_font = \"arimo\"\n"
                     "language = \"fr\"\n");
    ico_config_reset(toml, "no-such.ini");
    CHECK_STR(ico_config_get_string("game.language", "auto"), "fr");
    CHECK(ico_config_save() == 0);
    text = read_file(toml);
    CHECK(text != NULL && strstr(text, "classic_menu_text = true\n") != NULL &&
          strstr(text, "port_font = \"arimo\"\n") != NULL);
    free(text);
    /* a failed save leaves the old file alone */
    {
        char *a = read_file(toml);
        char bad[ICO_PATH_MAX];
        char *b;

        path_in(bad, "no-such-folder/x.toml");
        ico_config_reset(bad, "no-such.ini");
        CHECK(ico_config_save() == -1);
        b = read_file(toml);
        CHECK(a != NULL && b != NULL && strcmp(a, b) == 0);
        free(a);
        free(b);
    }
    remove(toml);
}

static void test_precedence(void)
{
    char toml[ICO_PATH_MAX], ini[ICO_PATH_MAX];

    path_in(toml, "prec.toml");
    path_in(ini, "prec.ini");
    write_file(toml, "[audio]\nenabled = true\nvolume = 0.5\n[dev]\nticks = 9\nwatchdog = 12\n"
                     "headless = false\n[paths]\niso = \"from-toml.iso\"\n");
    write_file(ini, "ticks=5\naudio=0\n");
    ico_config_reset(toml, ini);
    CHECK(ico_config_get_int("dev.ticks", -1) == 5);         /* ini > toml */
    CHECK(ico_config_get_int("dev.watchdog", -1) == 12);     /* toml only */
    CHECK(ico_config_get_bool("audio.enabled", 1) == 0);     /* ini audio=0 > toml true */
    CHECK(ico_config_get_float("audio.volume", 1.0) == 0.5); /* no ini key for it */
    CHECK_STR(ico_config_get_string("paths.iso", "x"), "from-toml.iso");
    CHECK(ico_config_get_int("dev.dump_every", 0) == 0); /* default */
    CHECK(ico_config_get_bool("dev.trace", 1) == 1);     /* default */
    /* a set value is written to the toml layer; the ini still overrides it */
    ico_config_set_int("dev.ticks", 77);
    CHECK(ico_config_get_int("dev.ticks", -1) == 5);
    ico_config_set_int("dev.watchdog", 77);
    CHECK(ico_config_get_int("dev.watchdog", -1) == 77); /* no ini key: the toml's */
}

static void test_ini_layer(void)
{
    char exe[ICO_PATH_MAX], ini_path[ICO_PATH_MAX], toml_path[ICO_PATH_MAX];
    IcoIni ini;

    /* ico_ini_load of the executable's own ini fills the keys it lacks from
       the config.toml in the per-user folder (here the exe folder) */
    ico_host_ini_path(ini_path, sizeof(ini_path));
    ico_host_exe_dir(exe, sizeof(exe));
    ico_path_join(toml_path, sizeof(toml_path), exe, "config.toml");
    if (ico_file_exists(ini_path) || ico_file_exists(toml_path)) {
        fprintf(stderr, "config_test: %s or %s exists; layering not tested\n", ini_path, toml_path);
        return;
    }
    write_file(
        toml_path,
        "[paths]\niso = \"t.iso\"\nsaves = \"cards\"\nsaves2 = \"cards2\"\n[audio]\nenabled = false\n"
        "[dev]\nticks = 100\nwatchdog = 0\ntrace = false\nverify = false\n");
    write_file(ini_path, "ticks=3\n");
    CHECK(ico_ini_load(&ini, ini_path) == 0);
    CHECK_STR(ico_ini_get(&ini, "ticks"), "3");   /* the ini wins */
    CHECK_STR(ico_ini_get(&ini, "iso"), "t.iso"); /* filled from toml */
    CHECK_STR(ico_ini_get(&ini, "saves"), "cards");
    CHECK_STR(ico_ini_get(&ini, "saves2"), "cards2"); /* the port-1 card */
    CHECK_STR(ico_ini_get(&ini, "audio"), "0");       /* false reads as 0 */
    CHECK_STR(ico_ini_get(&ini, "watchdog"), "0");
    CHECK_STR(ico_ini_get(&ini, "trace"), "0");
    CHECK_STR(ico_ini_get(&ini, "verify"), "0");
    CHECK(ico_ini_get(&ini, "dump_every") == NULL);
    /* no ini file at all: the toml alone */
    remove(ini_path);
    CHECK(ico_ini_load(&ini, ini_path) == -1);
    CHECK_STR(ico_ini_get(&ini, "ticks"), "100");
    remove(toml_path);
    /* a path that is not the executable's ini is read as it is */
    {
        char other[ICO_PATH_MAX];

        path_in(other, "other.ini");
        write_file(toml_path, "[dev]\nticks = 1\n");
        write_file(other, "iso=a\n");
        CHECK(ico_ini_load(&ini, other) == 0);
        CHECK(ico_ini_get(&ini, "ticks") == NULL);
        remove(other);
        remove(toml_path);
    }
}

/* the first run: config.toml appears once, with the defaults, and is never
   replaced */
static void test_first_run(void)
{
    char toml[ICO_PATH_MAX];
    char *text;

    path_in(toml, "first-run.toml");
    remove(toml);
    ico_config_reset(toml, "no-such.ini");
    CHECK(ico_config_write_first_run() == 0);
    text = read_file(toml);
    CHECK(text != NULL && strstr(text, "[video]") != NULL && strstr(text, "# ") != NULL);
    /* the file reads back as the defaults ico_config_save would add */
    ico_config_reset(toml, "no-such.ini");
    CHECK(ico_config_get_int("version", 0) == ICO_CONFIG_VERSION);
    CHECK_STR(ico_config_get_string("video.preset", "?"), "original");
    CHECK(ico_config_get_bool("video.vsync", 0) == 1);
    CHECK(ico_config_get_bool("video.fullscreen", 1) == 0);
    CHECK(ico_config_get_bool("audio.enabled", 0) == 1);
    CHECK(ico_config_get_float("audio.volume", 0.0) == 1.0);
    CHECK(ico_config_get_float("audio.music", 0.0) == 1.0);
    CHECK(ico_config_get_float("audio.effects", 0.0) == 1.0);
    CHECK_STR(ico_config_get_string("audio.output", "?"), "auto");
    CHECK_STR(ico_config_get_string("audio.device", "?"), "");
    CHECK_STR(ico_config_get_string("game.language", "?"), "auto");
    CHECK_STR(ico_config_get_string("paths.iso", "?"), "");
    /* a second run does nothing, and a player's edited file is kept */
    write_file(toml, "# mine\n[video]\npreset = \"enhanced\"\n");
    ico_config_reset(toml, "no-such.ini");
    CHECK(ico_config_write_first_run() == 1);
    free(text);
    text = read_file(toml);
    CHECK(text != NULL && strcmp(text, "# mine\n[video]\npreset = \"enhanced\"\n") == 0);
    free(text);
    /* saving after the first run keeps its comments */
    remove(toml);
    ico_config_reset(toml, "no-such.ini");
    CHECK(ico_config_write_first_run() == 0);
    ico_config_reset(toml, "no-such.ini");
    ico_config_set_string("video.preset", "enhanced");
    CHECK(ico_config_save() == 0);
    text = read_file(toml);
    CHECK(text != NULL && strstr(text, "preset = \"enhanced\"") != NULL &&
          strstr(text, "# \"original\" is the PS2 picture") != NULL);
    free(text);
    remove(toml);
}

/* dump_interp (R7d): handed to the renderer as ICO_RD_DUMP_INTERP with
   dump_every, so the ini or config.toml decides, 0 when absent */
static void test_dump_interp(void)
{
    char ini_path[ICO_PATH_MAX];
    IcoIni ini;
    const char *v;

    CHECK_STR(ico_config_ini_key("dev.dump_interp"), "dump_interp");
    path_in(ini_path, "dumpi.ini");
    write_file(ini_path, "dump_every=7\ndump_dir=dumpi-dumps\ndump_interp=true\n");
    CHECK(ico_ini_load(&ini, ini_path) == 0);
    v = getenv("ICO_RD_DUMP_INTERP");
    CHECK(v != NULL && strcmp(v, "1") == 0);
    write_file(ini_path, "dump_every=7\ndump_dir=dumpi-dumps\n");
    CHECK(ico_ini_load(&ini, ini_path) == 0);
    v = getenv("ICO_RD_DUMP_INTERP");
    CHECK(v != NULL && strcmp(v, "0") == 0);
    remove(ini_path);
}

static void test_fixed_clock_rule(void)
{
    IcoIni ini;

    /* this binary is not ICO_HEADLESS, like the window build */
    ico_ini_parse(&ini, "");
    CHECK(ico_host_fixed_clock(&ini) == 0);
    ico_ini_parse(&ini, "headless=1\n");
    CHECK(ico_host_fixed_clock(&ini) == 1);
    ico_ini_parse(&ini, "trace=logs/t.txt\n");
    CHECK(ico_host_fixed_clock(&ini) == 1);
    ico_ini_parse(&ini, "trace=1\n");
    CHECK(ico_host_fixed_clock(&ini) == 1);
    ico_ini_parse(&ini, "trace=0\n");
    CHECK(ico_host_fixed_clock(&ini) == 0);
    ico_ini_parse(&ini, "headless=1\nfixed_clock=0\n");
    CHECK(ico_host_fixed_clock(&ini) == 0); /* explicit wins */
    ico_ini_parse(&ini, "fixed_clock=1\n");
    CHECK(ico_host_fixed_clock(&ini) == 1);
}

static void test_language(void)
{
    char toml[ICO_PATH_MAX];

    CHECK(ico_scf_language_from_name("en") == 1 && ico_scf_language_from_name("fr") == 2);
    CHECK(ico_scf_language_from_name("es") == 3 && ico_scf_language_from_name("de") == 4);
    CHECK(ico_scf_language_from_name("IT") == 5);
    CHECK(ico_scf_language_from_name("auto") == -1 && ico_scf_language_from_name("") == -1);
    CHECK(ico_scf_language_from_name("fra") == -1 && ico_scf_language_from_name(NULL) == -1);
    CHECK(ico_scf_language_from_locale("fr_FR.UTF-8") == 2);
    CHECK(ico_scf_language_from_locale("de-AT") == 4);
    CHECK(ico_scf_language_from_locale("es") == 3);
    CHECK(ico_scf_language_from_locale("it_IT@euro") == 5);
    CHECK(ico_scf_language_from_locale("en_GB") == 1);
    CHECK(ico_scf_language_from_locale("ja_JP.UTF-8") == 1); /* not in the game */
    CHECK(ico_scf_language_from_locale("pt_BR") == 1);
    CHECK(ico_scf_language_from_locale("eng") == 1);
    CHECK(ico_scf_language_from_locale("C") == 1 && ico_scf_language_from_locale(NULL) == 1);

    set_env("LC_ALL", NULL);
    set_env("LC_MESSAGES", NULL);
    set_env("LANG", NULL);
    CHECK(ico_sysconf_host_language() == 1);
    set_env("LANG", "es_ES.UTF-8");
    CHECK(ico_sysconf_host_language() == 3);
    set_env("LC_MESSAGES", "fr_FR");
    CHECK(ico_sysconf_host_language() == 2);
    set_env("LC_ALL", "C");
    CHECK(ico_sysconf_host_language() == 2); /* "C" is skipped */
    set_env("LC_ALL", "de_DE");
    CHECK(ico_sysconf_host_language() == 4);

    path_in(toml, "lang.toml");
    /* [game] language wins over the locale */
    write_file(toml, "[game]\nlanguage = \"it\"\n");
    ico_config_reset(toml, "no-such.ini");
    ico_sysconf_reset();
    CHECK(sceScfGetLanguage() == 5);
    /* auto: the locale (LC_ALL=de_DE above) */
    write_file(toml, "[game]\nlanguage = \"auto\"\n");
    ico_config_reset(toml, "no-such.ini");
    ico_sysconf_reset();
    CHECK(sceScfGetLanguage() == 4);
    /* absent, or not one of the five: the locale */
    write_file(toml, "version = 1\n");
    ico_config_reset(toml, "no-such.ini");
    ico_sysconf_reset();
    CHECK(sceScfGetLanguage() == 4);
    write_file(toml, "[game]\nlanguage = \"klingon\"\n");
    ico_config_reset(toml, "no-such.ini");
    ico_sysconf_reset();
    CHECK(sceScfGetLanguage() == 4);
    /* nothing at all: English */
    set_env("LC_ALL", NULL);
    set_env("LC_MESSAGES", NULL);
    set_env("LANG", NULL);
    write_file(toml, "");
    ico_config_reset(toml, "no-such.ini");
    ico_sysconf_reset();
    CHECK(sceScfGetLanguage() == 1);
    CHECK(sceScfGetLanguage() == 1);
    CHECK(sceScfGetTimeZone() == 0 && sceScfGetSummerTime() == 0);
    remove(toml);
}

static int bcd(int v)
{
    return (v / 10) << 4 | (v % 10);
}

static int bcd_ok(int b)
{
    return (b & 15) < 10 && (b >> 4) < 10;
}

static void test_clock(void)
{
    IcoClockBcd c;
    time_t now;
    struct tm *tm;

    ico_clock_pack(&c, 2026, 10, 5, 23, 59, 7);
    CHECK(c.stat == 0 && c.second == 0x07 && c.minute == 0x59 && c.hour == 0x23);
    CHECK(c.day == 0x05 && c.month == 0x10 && c.year == 0x26);
    ico_clock_pack(&c, 2000, 1, 1, 0, 0, 0);
    CHECK(c.year == 0 && c.month == 1);
    CHECK(sizeof(c) == 8);

    ico_clock_set_fixed(1);
    ico_clock_now(&c);
    CHECK(c.stat == 0 && c.year == 0x02 && c.month == 1 && c.day == 1 && c.hour == 0 &&
          c.minute == 0 && c.second == 0);

    ico_clock_set_fixed(0);
    now = time(NULL);
    ico_clock_now(&c);
    tm = localtime(&now);
    CHECK(c.stat == 0 && bcd_ok(c.second) && bcd_ok(c.minute) && bcd_ok(c.hour) && bcd_ok(c.day) &&
          bcd_ok(c.month) && bcd_ok(c.year));
    CHECK(c.year == bcd((tm->tm_year + 1900) % 100) || c.year == bcd((tm->tm_year + 1901) % 100));
    CHECK(c.month >= 1 && c.month <= 0x12 && c.hour <= 0x23 && c.minute <= 0x59);
    ico_clock_set_fixed(1);
}

static void test_timers(void)
{
    int i;

    ico_clock_set_vsync_hz(50);
    ico_clock_timers_reset();
    CHECK(*T0_COUNT == 0 && *T1_COUNT == 0);

    /* CUE clear: stopped */
    ico_clock_timers_step(65536);
    CHECK(*T0_COUNT == 0);

    /* the game's own setup: T0_MODE = 0x82 (CUE, bus clock / 256): 576 kHz,
       11520 counts per PAL vsync */
    *T0_MODE = 0x82;
    ico_clock_timers_step(65536);
    CHECK(*T0_COUNT == 11520);
    ico_clock_timers_step(32768); /* half a vsync */
    CHECK(*T0_COUNT == 11520 + 5760);
    *T0_COUNT = 0; /* debug_ResetBar, Main: a write is respected */
    ico_clock_timers_step(65536);
    CHECK(*T0_COUNT == 11520);
    /* 16 bits: 6 vsyncs is 69120, which wraps to 3584 and sets OVFF */
    *T0_COUNT = 0;
    *T0_MODE = 0x82;
    for (i = 0; i < 6; i++) {
        ico_clock_timers_step(65536);
    }
    CHECK(*T0_COUNT == 3584);
    CHECK((*T0_MODE & 0x800) != 0);

    /* the vsync hook steps one vsync (attached by the first sceScfGetLanguage) */
    ico_clock_timers_attach();
    ico_clock_timers_attach(); /* once */
    *T0_COUNT = 0;
    ico_host_run_vsync_hooks();
    CHECK(*T0_COUNT == 11520);

    /* the hook follows the game's mode word (systemStatus[0]) once it is
       given: NTSC (0) is 1001/60000 s per vsync, 9609.6 counts at 576 kHz,
       so five hook vsyncs are 48048; back to PAL (1), one vsync is 11520
       again */
    ico_clock_set_mode_word(systemStatus);
    systemStatus[0] = 0;
    *T0_COUNT = 0;
    ico_host_run_vsync_hooks();
    CHECK(*T0_COUNT == 9609);
    for (i = 0; i < 4; i++) {
        ico_host_run_vsync_hooks();
    }
    CHECK(*T0_COUNT == 48048);
    systemStatus[0] = 1;
    *T0_COUNT = 0;
    ico_host_run_vsync_hooks();
    CHECK(*T0_COUNT == 11520);
    ico_clock_set_mode_word(NULL);

    /* T1 as debug_BeginTimer sets it: /16 gives 184320 per vsync, wraps to
       184320 mod 65536 = 53248 */
    *T1_COUNT = 0;
    *T1_MODE = 0x81;
    ico_clock_timers_step(65536);
    CHECK(*T1_COUNT == 184320 % 65536);

    /* the horizontal blank: 15625 Hz, 312.5 lines per vsync, exactly 625 in
       two (the half line is carried) */
    *T1_COUNT = 0;
    *T1_MODE = 0x83;
    ico_clock_timers_step(65536);
    CHECK(*T1_COUNT == 312);
    ico_clock_timers_step(65536);
    CHECK(*T1_COUNT == 625);

    /* NTSC: 1001/60000 s per vsync, 576000 * 1001 / 60000 = 9609.6; five
       vsyncs are 48048 exactly */
    ico_clock_set_vsync_hz(60);
    ico_clock_timers_reset();
    *T0_MODE = 0x82;
    for (i = 0; i < 5; i++) {
        ico_clock_timers_step(65536);
    }
    CHECK(*T0_COUNT == 48048);
    ico_clock_set_vsync_hz(50);
    ico_clock_timers_reset();

    /* deterministic: the same steps give the same count */
    *T0_MODE = 0x82;
    for (i = 0; i < 1000; i++) {
        ico_clock_timers_step(65536);
    }
    {
        int first = (int)*T0_COUNT;

        ico_clock_timers_reset();
        *T0_MODE = 0x82;
        for (i = 0; i < 1000; i++) {
            ico_clock_timers_step(65536);
        }
        CHECK((int)*T0_COUNT == first);
        CHECK(first == (int)((1000ull * 11520) & 0xFFFF));
    }
    ico_clock_timers_reset();
}

int main(int argc, char **argv)
{
    snprintf(scratch, sizeof(scratch), "%s", argc > 1 ? argv[1] : ".");
    test_parse();
    test_render();
    test_render_edges();
    test_save();
    test_precedence();
    test_ini_layer();
    test_first_run();
    test_dump_interp();
    test_fixed_clock_rule();
    test_language();
    test_clock();
    test_timers();
    printf("config_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
