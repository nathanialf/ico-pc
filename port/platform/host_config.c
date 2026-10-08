/*
 * port/platform/host_config.c
 *
 * The executable's folder, ico-pc.ini, SHA-1, the file dialog and the error
 * box (host_config.h). Windows needs user32 (MessageBoxW) and comdlg32
 * (GetOpenFileNameW). Paths are UTF-8 throughout and reach the file system
 * through host_fs.h's wide helpers.
 */
#include "host_config.h"
#include "host_fs.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ICO_HOST_FORCE_PORTABLE (the Android build of ico_pc, and
   host_config_portable_test): the user folder is always the executable's
   folder itself, whatever portable= says. On Android that folder is the
   app's files folder (port/platform/android/host_android.h) and the error
   paths show SDL's message box. */
#if defined(__ANDROID__) && defined(ICO_HOST_FORCE_PORTABLE)
#define ICO_HOST_ANDROID 1
#include "android/host_android.h"
#endif
#ifdef ICO_HOST_SDL_PREFPATH

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#endif
#ifdef _WIN32

#include <windows.h>
#include <commdlg.h>
#include <direct.h>

#define SEP '\\'
#else

#include <sys/stat.h>
#include <unistd.h>

#define SEP '/'
#endif

static void copy(char *out, size_t size, const char *s)
{
    size_t n = strlen(s);

    if (size == 0) {
        return;
    }
    if (n >= size) {
        n = size - 1;
    }
    memcpy(out, s, n);
    out[n] = '\0';
}

/* --- paths --------------------------------------------------------------- */

int ico_host_exe_dir(char *out, size_t size)
{
#ifdef ICO_HOST_ANDROID
    /* no folder of its own: the app's files folder */
    if (ico_android_files_dir(out, size) != 0) {
        copy(out, size, ".");
        return -1;
    }
    return 0;
#else
    char buf[ICO_PATH_MAX];
    char *slash;

#ifdef _WIN32
    {
        wchar_t wbuf[ICO_PATH_MAX];
        DWORD wn = GetModuleFileNameW(NULL, wbuf, (DWORD)(sizeof(wbuf) / sizeof(wbuf[0])));

        if (wn == 0 || wn >= sizeof(wbuf) / sizeof(wbuf[0]) ||
            ico_narrow(wbuf, buf, sizeof(buf)) != 0) {
            copy(out, size, ".");
            return -1;
        }
    }
    slash = strrchr(buf, '\\');
    if (strrchr(buf, '/') > slash) {
        slash = strrchr(buf, '/');
    }
#else
    long n = (long)readlink("/proc/self/exe", buf, sizeof(buf) - 1);

    if (n <= 0) {
        copy(out, size, ".");
        return -1;
    }
    buf[n] = '\0';
    slash = strrchr(buf, '/');
#endif
    if (slash == NULL) {
        copy(out, size, ".");
        return -1;
    }
    if (slash == buf) {
        slash++; /* the root folder */
    }
    *slash = '\0';
    copy(out, size, buf);
    return 0;
#endif
}

int ico_path_is_absolute(const char *path)
{
    if (path[0] == '/' || path[0] == '\\') {
        return 1;
    }
#ifdef _WIN32
    if (isalpha((unsigned char)path[0]) && path[1] == ':') {
        return 1;
    }
#endif
    return 0;
}

int ico_path_join(char *out, size_t size, const char *dir, const char *name)
{
    size_t n;
    int len;

    if (size == 0) {
        return -1;
    }
    if (ico_path_is_absolute(name) || dir == NULL || dir[0] == '\0') {
        len = snprintf(out, size, "%s", name);
    } else {
        n = strlen(dir);
        if (n > 0 && (dir[n - 1] == '/' || dir[n - 1] == '\\')) {
            len = snprintf(out, size, "%s%s", dir, name);
        } else {
            len = snprintf(out, size, "%s%c%s", dir, SEP, name);
        }
    }
    if (len < 0 || (size_t)len >= size) {
        out[0] = '\0'; /* never a cut-off path that names another file */
        return -1;
    }
    return 0;
}

int ico_file_exists(const char *path)
{
    FILE *f = ico_fopen(path, "rb");

    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

int ico_make_dir(const char *path)
{
    if (ico_mkdir(path) == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

/* --- ico-pc.ini ---------------------------------------------------------- */

/* Splits one line into trimmed key and value; 1 if it is a setting. */
static int split_line(char *line, char **key, char **value)
{
    char *p = line;
    char *eq;
    char *end;

    while (*p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '\0' || *p == '#' || *p == ';') {
        return 0;
    }
    eq = strchr(p, '=');
    if (eq == NULL) {
        return 0;
    }
    *key = p;
    end = eq;
    while (end > p && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    p = eq + 1;
    while (*p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }
    end = p + strlen(p);
    while (end > p && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    if (end - p >= 2 && p[0] == '"' && end[-1] == '"') {
        end[-1] = '\0';
        p++;
    }
    *value = p;
    return (*key)[0] != '\0';
}

void ico_ini_parse(IcoIni *ini, const char *text)
{
    const char *p = text;

    memset(ini, 0, sizeof(*ini));
    while (*p != '\0') {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char line[ICO_PATH_MAX + 128];
        char *key;
        char *value;

        if (len >= sizeof(line)) {
            len = sizeof(line) - 1;
        }
        memcpy(line, p, len);
        line[len] = '\0';
        if (split_line(line, &key, &value) && ini->count < ICO_INI_MAX_KEYS &&
            ico_ini_get(ini, key) == NULL) {
            copy(ini->key[ini->count], sizeof(ini->key[0]), key);
            copy(ini->value[ini->count], sizeof(ini->value[0]), value);
            ini->count++;
        }
        p = eol ? eol + 1 : p + len;
    }
}

static char *read_text(const char *path)
{
    FILE *f = ico_fopen(path, "rb");
    char *text = NULL;
    size_t len = 0;
    size_t cap = 0;
    size_t got;

    if (f == NULL) {
        return NULL;
    }
    do {
        if (cap - len < 1024) {
            char *grown = realloc(text, cap + 16384);

            if (grown == NULL) {
                free(text);
                fclose(f);
                return NULL;
            }
            text = grown;
            cap += 16384;
        }
        got = fread(text + len, 1, cap - len - 1, f);
        len += got;
    } while (got != 0);
    fclose(f);
    text[len] = '\0';
    return text;
}

/* dump_every=N (renderer wave 3): the renderer (port/render, which does not
   link this file) writes every Nth frame it replays as an rd dump into the
   folder dump_dir= names (default: dumps beside the ini; created), for
   rd_replay_tool.  Handed over in the environment, ICO_RD_DUMP_EVERY and
   ICO_RD_DUMP_DIR, which rd_Init reads.  dump_interp=1 (renderer wave 7,
   R7d) also writes each dumped frame interpolated half way from the one
   before (rd-NNNNN-i50.rddump), handed over as ICO_RD_DUMP_INTERP, which is
   always set with the other two so the ini or config.toml decides.
   dump_from=N (package S2) dumps no frame numbered below N, handed over as
   ICO_RD_DUMP_FROM (0 when unset). */
static void export_dump_keys(const IcoIni *ini, const char *path)
{
    const char *every = ico_ini_get(ini, "dump_every");
    const char *dir = ico_ini_get(ini, "dump_dir");
    const char *interp = ico_ini_get(ini, "dump_interp");
    const char *from = ico_ini_get(ini, "dump_from");
    const char *interpOn = "0";
    char base[ICO_PATH_MAX], full[ICO_PATH_MAX];
    const char *slash;
    size_t n;

    if (every == NULL || atoi(every) <= 0) {
        return;
    }
    slash = strrchr(path, '/');
#ifdef _WIN32
    if (strrchr(path, '\\') > slash) {
        slash = strrchr(path, '\\');
    }
#endif
    n = slash != NULL ? (size_t)(slash - path) : 1;
    if (n >= sizeof(base)) {
        n = sizeof(base) - 1;
    }
    memcpy(base, slash != NULL ? path : ".", n);
    base[n] = '\0';
    if (ico_path_join(full, sizeof(full), base, dir != NULL ? dir : "dumps") != 0 ||
        ico_make_dir(full) != 0) {
        fprintf(stderr, "ico_pc: dump_dir: no usable folder; no dumps\n");
        return;
    }
    if (interp != NULL && (strcmp(interp, "1") == 0 || strcmp(interp, "true") == 0 ||
                           strcmp(interp, "on") == 0 || strcmp(interp, "yes") == 0)) {
        interpOn = "1";
    }
#ifdef _WIN32
    _putenv_s("ICO_RD_DUMP_EVERY", every);
    _putenv_s("ICO_RD_DUMP_DIR", full);
    _putenv_s("ICO_RD_DUMP_INTERP", interpOn);
    _putenv_s("ICO_RD_DUMP_FROM", from != NULL ? from : "0");
#else
    setenv("ICO_RD_DUMP_EVERY", every, 1);
    setenv("ICO_RD_DUMP_DIR", full, 1);
    setenv("ICO_RD_DUMP_INTERP", interpOn, 1);
    setenv("ICO_RD_DUMP_FROM", from != NULL ? from : "0", 1);
#endif
}

/* audio_dump=PATH and audio=0 (Phase 4B): handed to port/audio/audio_host.c
   (which ico_platform does not link to here) in the environment, as
   ICO_AUDIO_DUMP (PATH joined to the ini's folder; audio_dump=1 means
   logs/audio.wav there) and ICO_AUDIO. */
static void export_audio_keys(const IcoIni *ini, const char *path)
{
    const char *dump = ico_ini_get(ini, "audio_dump");
    const char *enable = ico_ini_get(ini, "audio");
    char base[ICO_PATH_MAX], full[ICO_PATH_MAX];
    const char *slash = strrchr(path, '/');
    size_t n;

#ifdef _WIN32
    if (strrchr(path, '\\') > slash) {
        slash = strrchr(path, '\\');
    }
#endif
    if (enable != NULL) {
#ifdef _WIN32
        _putenv_s("ICO_AUDIO", enable);
#else
        setenv("ICO_AUDIO", enable, 1);
#endif
    }
    if (dump == NULL || dump[0] == '\0' || strcmp(dump, "0") == 0) {
        return;
    }
    n = slash != NULL ? (size_t)(slash - path) : 1;
    if (n >= sizeof(base)) {
        n = sizeof(base) - 1;
    }
    memcpy(base, slash != NULL ? path : ".", n);
    base[n] = '\0';
    if (strcmp(dump, "1") == 0) {
        char logs[ICO_PATH_MAX];

        if (ico_path_join(logs, sizeof(logs), base, "logs") != 0 || ico_make_dir(logs) != 0 ||
            ico_path_join(full, sizeof(full), logs, "audio.wav") != 0) {
            fprintf(stderr, "ico_pc: audio_dump: cannot use the logs folder; no dump\n");
            return;
        }
    } else if (ico_path_join(full, sizeof(full), base, dump) != 0) {
        fprintf(stderr, "ico_pc: audio_dump: the path is too long; no dump\n");
        return;
    }
#ifdef _WIN32
    _putenv_s("ICO_AUDIO_DUMP", full);
#else
    setenv("ICO_AUDIO_DUMP", full, 1);
#endif
}

static void put_env(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

int ico_ini_load_file(IcoIni *ini, const char *path)
{
    char *text = read_text(path);

    if (text == NULL) {
        memset(ini, 0, sizeof(*ini));
        return -1;
    }
    ico_ini_parse(ini, text);
    free(text);
    return 0;
}

/* config.toml paths and the ini keys they stand for; the ini wins when it has
   the key */
static const struct {
    const char *toml;
    const char *ini;
} ini_map[] = {
    {"paths.iso", "iso"},
    {"paths.saves", "saves"},
    /* the port-1 card folder; empty or absent: no card in port 1 (package S1) */
    {"paths.saves2", "saves2"},
    {"audio.enabled", "audio"},
    {"dev.ticks", "ticks"},
    {"dev.watchdog", "watchdog"},
    {"dev.trace", "trace"},
    {"dev.dump_every", "dump_every"},
    {"dev.dump_dir", "dump_dir"},
    /* 1: each dump also half way interpolated (renderer wave 7, R7d) */
    {"dev.dump_interp", "dump_interp"},
    /* the first frame number dump_every dumps (package S2) */
    {"dev.dump_from", "dump_from"},
    {"dev.audio_dump", "audio_dump"},
    {"dev.pad_script", "pad_script"},
    /* the pad recording, logs/input-<time>.txt (package Q1; main_host.c:
       default on in the window build, off headless; a path writes there) */
    {"dev.input_record", "input_record"},
    {"dev.verify", "verify"},
    {"dev.headless", "headless"},
    {"dev.fixed_clock", "fixed_clock"},
    /* 1: mount the disc image directly instead of the extracted ico.o2r
       (main_host.c; default 1 headless, 0 in the window build) */
    {"dev.use_iso", "use_iso"},
    /* 1: write config.toml with its defaults when there is none (main_host.c;
       default 1 in the window build, 0 headless, so test runs leave no file) */
    {"dev.write_config", "write_config"},
    /* the stage Main starts in (developer key, renderer wave 5, R5b) */
    {"dev.start_stage", "start_stage"},
    /* one forced stage change: the stage, and the Main tick from which it
       is taken (developer keys, package X5; common/src/main.c,
       ico_dev_switch_stage) */
    {"dev.switch_to", "switch_to"},
    {"dev.switch_at", "switch_at"},
    /* Settings > Extras > Credits unlocked whatever the achievements say
       (package CRED; port/game/credits.c) */
    {"dev.unlock_credits", "unlock_credits"},
    /* test popups from Main tick 100 (Phase 6, 6B; port/ui/popup.h) */
    {"dev.popup_test", "popup_test"},
    /* the renderer backend of the window build, "vulkan" (default) or "d3d12"
       (renderer wave 6, R6c; window_host.c, port/rhi/rhi.h rhi_CreateBackend) */
    {"video.backend", "backend"},
};

const char *ico_config_ini_key(const char *toml_path)
{
    size_t i;

    for (i = 0; i < sizeof(ini_map) / sizeof(ini_map[0]); i++) {
        if (strcmp(ini_map[i].toml, toml_path) == 0) {
            return ini_map[i].ini;
        }
    }
    return NULL;
}

static void ini_add(IcoIni *ini, const char *key, const char *value)
{
    if (ini->count < ICO_INI_MAX_KEYS && ico_ini_get(ini, key) == NULL) {
        copy(ini->key[ini->count], sizeof(ini->key[0]), key);
        copy(ini->value[ini->count], sizeof(ini->value[0]), value);
        ini->count++;
    }
}

int ico_host_fixed_clock(const IcoIni *ini)
{
    const char *v = ico_ini_get(ini, "fixed_clock");

    if (v != NULL && v[0] != '\0') {
        return strcmp(v, "1") == 0 || strcmp(v, "true") == 0;
    }
#ifdef ICO_HEADLESS
    return 1;
#else
    v = ico_ini_get(ini, "headless");
    if (v != NULL && (strcmp(v, "1") == 0 || strcmp(v, "true") == 0)) {
        return 1;
    }
    v = ico_ini_get(ini, "trace");
    return v != NULL && v[0] != '\0' && strcmp(v, "0") != 0 && strcmp(v, "none") != 0 &&
           strcmp(v, "false") != 0;
#endif
}

/* the executable's own ini over config.toml: the file's keys, the missing
   ones filled from the toml (ini > toml). No environment, no folders. */
int ico_ini_load_layered(IcoIni *ini, const char *path)
{
    char own[ICO_PATH_MAX];
    int r = ico_ini_load_file(ini, path);

    ico_host_ini_path(own, sizeof(own));
    if (strcmp(own, path) == 0) {
        char dir[ICO_PATH_MAX], toml_path[ICO_PATH_MAX];
        IcoToml *t = NULL;
        size_t i;

        /* before the first look at the user folder: portable= says where it is */
        ico_host_set_portable(ico_host_portable_value(ico_ini_get(ini, "portable")));
        ico_host_pref_dir(dir, sizeof(dir));
        if (ico_path_join(toml_path, sizeof(toml_path), dir, "config.toml") == 0) {
            t = ico_toml_load(toml_path);
        }
        for (i = 0; t != NULL && i < sizeof(ini_map) / sizeof(ini_map[0]); i++) {
            const char *v = ico_toml_get(t, ini_map[i].toml);

            if (v != NULL && v[0] != '\0') {
                ini_add(ini, ini_map[i].ini,
                        strcmp(v, "true") == 0    ? "1"
                        : strcmp(v, "false") == 0 ? "0"
                                                  : v);
            }
        }
        ico_toml_free(t);
    }
    return r;
}

/* The keys other libraries read from the environment, and the folders they
   write into (dumps, logs); once, at start-up (main_host.c), and for the
   executable's own ini only. */
void ico_ini_export(const IcoIni *ini, const char *path, int r)
{
    char own[ICO_PATH_MAX], dir[ICO_PATH_MAX], toml_path[ICO_PATH_MAX];

    ico_host_ini_path(own, sizeof(own));
    if (strcmp(own, path) == 0) {
        IcoToml *t = NULL;

        ico_host_pref_dir(dir, sizeof(dir));
        if (ico_path_join(toml_path, sizeof(toml_path), dir, "config.toml") == 0) {
            t = ico_toml_load(toml_path);
        }
        /* the audio host's gains and device (port/audio/audio_host.h) */
        if (t != NULL && ico_toml_get(t, "audio.volume") != NULL) {
            put_env("ICO_AUDIO_VOLUME", ico_toml_get(t, "audio.volume"));
        }
        if (t != NULL && ico_toml_get(t, "audio.music") != NULL) {
            put_env("ICO_AUDIO_MUSIC", ico_toml_get(t, "audio.music"));
        }
        if (t != NULL && ico_toml_get(t, "audio.effects") != NULL) {
            put_env("ICO_AUDIO_EFFECTS", ico_toml_get(t, "audio.effects"));
        }
        if (t != NULL && ico_toml_get(t, "audio.device") != NULL) {
            put_env("ICO_AUDIO_DEVICE", ico_toml_get(t, "audio.device"));
        }
        ico_toml_free(t);
        put_env("ICO_FIXED_CLOCK", ico_host_fixed_clock(ini) ? "1" : "0");
    }
    if (r == 0 || ini->count > 0) {
        export_dump_keys(ini, path);
        export_audio_keys(ini, path);
        /* start_stage=N (developer key, renderer wave 5): the stage Main
           starts in instead of stage 1, through debug_TryToGetStartStage
           (port/null/debug_null.c), which reads ICO_START_STAGE */
        if (ico_ini_get(ini, "start_stage") != NULL) {
            put_env("ICO_START_STAGE", ico_ini_get(ini, "start_stage"));
        }
        /* switch_to=N, switch_at=T (developer keys, package X5): one
           forced stage change to N from Main tick T, through an exit of the
           current stage (common/src/main.c, ico_dev_switch_stage, which
           reads ICO_SWITCH_TO and ICO_SWITCH_AT) */
        if (ico_ini_get(ini, "switch_to") != NULL) {
            put_env("ICO_SWITCH_TO", ico_ini_get(ini, "switch_to"));
        }
        if (ico_ini_get(ini, "switch_at") != NULL) {
            put_env("ICO_SWITCH_AT", ico_ini_get(ini, "switch_at"));
        }
        /* popup_test=true (developer key, Phase 6 6B): port/ui/ui_host.c
           queues a test popup at Main tick 100 and every 150 ticks after;
           it reads ICO_UI_POPUP_TEST (1, true, on, yes) */
        if (ico_ini_get(ini, "popup_test") != NULL) {
            put_env("ICO_UI_POPUP_TEST", ico_ini_get(ini, "popup_test"));
        }
    }
}

int ico_ini_load(IcoIni *ini, const char *path)
{
    int r = ico_ini_load_layered(ini, path);

    ico_ini_export(ini, path, r);
    return r;
}

const char *ico_ini_get(const IcoIni *ini, const char *key)
{
    int i;

    for (i = 0; i < ini->count; i++) {
        if (strcmp(ini->key[i], key) == 0) {
            return ini->value[i];
        }
    }
    return NULL;
}

/* The ini rewritten into <path>.tmp, then moved over it, so a failed or
   interrupted write leaves the old file whole. */
int ico_ini_store(const char *path, const char *key, const char *value)
{
    char *text = read_text(path);
    const char *p = text ? text : "";
    char tmp[ICO_PATH_MAX + 8];
    FILE *f;
    int done = 0;
    int ok;

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = ico_fopen(tmp, "wb");
    if (f == NULL) {
        free(text);
        return -1;
    }
    while (*p != '\0') {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char line[ICO_PATH_MAX + 128];
        char *k;
        char *v;
        int replace = 0;

        if (len < sizeof(line)) {
            memcpy(line, p, len);
            line[len] = '\0';
            replace = !done && split_line(line, &k, &v) && strcmp(k, key) == 0;
        }
        if (replace) {
            fprintf(f, "%s=%s\r\n", key, value);
            done = 1;
        } else {
            fwrite(p, 1, len, f);
            fputc('\n', f);
        }
        p = eol ? eol + 1 : p + len;
    }
    if (!done) {
        fprintf(f, "%s=%s\r\n", key, value);
    }
    free(text);
    ok = ico_fsync(f) == 0 && !ferror(f); /* on the disk before the move */
    ok = fclose(f) == 0 && ok;
    if (!ok || ico_rename_replace(tmp, path) != 0) {
        ico_remove(tmp);
        return -1;
    }
    return 0;
}

/* --- SHA-1 (FIPS 180-4) -------------------------------------------------- */

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void sha1_block(IcoSha1 *s, const unsigned char *b)
{
    unsigned int w[80];
    unsigned int a, bb, c, d, e, t;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = (unsigned int)b[4 * i] << 24 | (unsigned int)b[4 * i + 1] << 16 |
               (unsigned int)b[4 * i + 2] << 8 | b[4 * i + 3];
    }
    for (i = 16; i < 80; i++) {
        t = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        w[i] = ROL(t, 1);
    }
    a = s->h[0];
    bb = s->h[1];
    c = s->h[2];
    d = s->h[3];
    e = s->h[4];
    for (i = 0; i < 80; i++) {
        unsigned int f;
        unsigned int k;

        if (i < 20) {
            f = (bb & c) | (~bb & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = bb ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (bb & c) | (bb & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = bb ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        t = ROL(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = ROL(bb, 30);
        bb = a;
        a = t;
    }
    s->h[0] += a;
    s->h[1] += bb;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
}

void ico_sha1_init(IcoSha1 *s)
{
    s->h[0] = 0x67452301u;
    s->h[1] = 0xEFCDAB89u;
    s->h[2] = 0x98BADCFEu;
    s->h[3] = 0x10325476u;
    s->h[4] = 0xC3D2E1F0u;
    s->length = 0;
    s->used = 0;
}

void ico_sha1_update(IcoSha1 *s, const void *data, size_t n)
{
    const unsigned char *p = data;

    s->length += n;
    if (s->used != 0) {
        size_t take = 64 - s->used < n ? 64 - s->used : n;

        memcpy(s->block + s->used, p, take);
        s->used += (unsigned int)take;
        p += take;
        n -= take;
        if (s->used < 64) {
            return;
        }
        sha1_block(s, s->block);
        s->used = 0;
    }
    while (n >= 64) {
        sha1_block(s, p);
        p += 64;
        n -= 64;
    }
    memcpy(s->block, p, n);
    s->used = (unsigned int)n;
}

void ico_sha1_final(IcoSha1 *s, unsigned char digest[20])
{
    unsigned long long bits = s->length * 8;
    unsigned char pad[72];
    size_t padlen = (s->used < 56 ? 56 : 120) - s->used;
    int i;

    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    for (i = 0; i < 8; i++) {
        pad[padlen + i] = (unsigned char)(bits >> (56 - 8 * i));
    }
    ico_sha1_update(s, pad, padlen + 8);
    for (i = 0; i < 20; i++) {
        digest[i] = (unsigned char)(s->h[i / 4] >> (24 - 8 * (i % 4)));
    }
}

int ico_sha1_file(const char *path, char hex[41], unsigned long long *bytes)
{
    enum { CHUNK = 1 << 20 };

    FILE *f = ico_fopen(path, "rb");
    unsigned char *buf;
    unsigned char digest[20];
    IcoSha1 s;
    size_t got;
    int i;

    if (f == NULL) {
        return -1;
    }
    buf = malloc(CHUNK);
    if (buf == NULL) {
        fclose(f);
        return -1;
    }
    ico_sha1_init(&s);
    while ((got = fread(buf, 1, CHUNK, f)) != 0) {
        ico_sha1_update(&s, buf, got);
    }
    i = ferror(f);
    fclose(f);
    free(buf);
    if (i) {
        return -1;
    }
    *bytes = s.length;
    ico_sha1_final(&s, digest);
    for (i = 0; i < 20; i++) {
        snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    }
    return 0;
}

/* --- output ------------------------------------------------------------- */

#ifndef _WIN32

static int console_fd = -1; /* the original stderr, kept for fatal errors */

#endif

int ico_host_redirect_output(const char *log_path)
{
    FILE *probe = ico_fopen(log_path, "w");

    if (probe == NULL) {
        return -1;
    }
    fclose(probe);
    fflush(stdout);
    fflush(stderr);
#ifndef _WIN32
    if (console_fd < 0) {
        console_fd = dup(2);
    }
#endif
#ifdef ICO_HOST_ANDROID
    /* the file and logcat (the streams' fds go to the mirror's pipe) */
    if (ico_android_log_mirror_start(log_path) == 0) {
        return 0;
    }
#endif
    /* the probe emptied the file; both streams append, so neither overwrites
       the other's lines (unbuffered on POSIX, so they interleave in order;
       on Windows each is flushed whole, below) */
#ifdef _WIN32
    {
        wchar_t *wp = ico_widen(log_path);
        int bad = wp == NULL || _wfreopen(wp, L"a", stdout) == NULL ||
                  _wfreopen(wp, L"a", stderr) == NULL;

        free(wp);
        if (bad) {
            return -1;
        }
    }
#else
    if (freopen(log_path, "a", stdout) == NULL) {
        return -1;
    }
    if (freopen(log_path, "a", stderr) == NULL) {
        return -1;
    }
#endif
#ifdef _WIN32
    /* Package Q1: fully buffered, flushed by the host loop once a vsync
       (ico_host_log_flush) and by the fatal paths (diag_host.c).  msvcrt
       has no line buffering and writes an unbuffered stream one character
       per OS call, each after a seek to the end (append mode): a player's
       10 s window lines (about 1 KB) took 225 ms on a network drive and
       stalled the pacer every block. */
    setvbuf(stdout, NULL, _IOFBF, 16 * 1024);
    setvbuf(stderr, NULL, _IOFBF, 64 * 1024);
#else
    /* glibc formats each call before its one write */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
#endif
    return 0;
}

void ico_host_log_flush(void)
{
    fflush(stdout);
    fflush(stderr);
}

#ifdef ICO_HOST_ANDROID

/* the log written out before the process ends without exit() */
static void flush_all(void)
{
    fflush(stdout);
    fflush(stderr);
    ico_android_log_mirror_flush();
}

#endif

/* --- the dialog and the error box --------------------------------------- */

int ico_host_pick_iso(char *out, size_t size)
{
#ifdef _WIN32
    OPENFILENAMEW ofn;
    wchar_t path[ICO_PATH_MAX] = L"";
    char utf8[ICO_PATH_MAX];

    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"ICO disc image (*.iso, *.chd)\0*.iso;*.chd\0All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = (DWORD)(sizeof(path) / sizeof(path[0]));
    ofn.lpstrTitle = L"Choose your ICO (PAL, SCES-50760) disc image";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn) || ico_narrow(path, utf8, sizeof(utf8)) != 0) {
        return -1;
    }
    copy(out, size, utf8);
    return 0;
#else
    (void)out;
    (void)size;
    return -1;
#endif
}

int ico_host_attach_console(void)
{
#ifdef _WIN32
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);

    if (err != NULL && err != INVALID_HANDLE_VALUE) {
        return 1; /* a console program, or redirected by the parent */
    }
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        return 0;
    }
    if (freopen("CONOUT$", "w", stdout) == NULL || freopen("CONOUT$", "w", stderr) == NULL) {
        return 0;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    return 1;
#else
    return 1;
#endif
}

void ico_host_message_box(const char *text, int error)
{
#if defined(ICO_HOST_ANDROID)
    flush_all();
    ico_android_message_box(text, error);
#elif defined(_WIN32)
    wchar_t *w = ico_widen(text);

    if (w != NULL) {
        MessageBoxW(NULL, w, L"ICO PC", MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
        free(w);
    } else {
        MessageBoxA(NULL, text, "ICO PC", MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
    }
#else
    (void)error;
    fprintf(stderr, "%s\n", text);
#endif
}

void ico_host_fatal(const char *log_path, const char *fmt, ...)
{
    char msg[2048];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    fprintf(stderr, "ico_pc: error: %s\n", msg);
    fflush(stderr);
    fflush(stdout);
#if defined(ICO_HOST_ANDROID)
    /* the box, the log written out, then _exit: the next start is a fresh
       process (exit()'s handlers would run on this thread while SDL's Java
       side still holds the activity) */
    {
        char box[3072];

        snprintf(box, sizeof(box), "%s\n\nLog: %s", msg, log_path ? log_path : "(none)");
        ico_host_message_box(box, 1);
    }
    flush_all();
    _exit(1);
#elif defined(_WIN32)
    {
        char box[3072];

        snprintf(box, sizeof(box), "%s\n\nLog: %s", msg, log_path ? log_path : "(none)");
        ico_host_message_box(box, 1);
    }
#else
    if (console_fd >= 0) {
        char line[2304];
        int n = snprintf(line, sizeof(line), "ico_pc: error: %s (log: %s)\n", msg,
                         log_path ? log_path : "none");

        if (n > 0 && write(console_fd, line,
                           (size_t)(n < (int)sizeof(line) ? n : (int)sizeof(line) - 1)) < 0) {
            /* nowhere left to report it */
        }
    }
#endif
    exit(1);
}

/* --- config.toml: a small TOML subset ------------------------------------ */

typedef struct IcoTomlEntry {
    char *path;  /* "section.key", or "key" before any section */
    char *value; /* the text, a quoted string without its quotes */
    int raw;     /* 1: written bare (bool, number, array); 0: a quoted string */
} IcoTomlEntry;

struct IcoToml {
    IcoTomlEntry *entry;
    int count;
    int cap;
};

static char *dup_range(const char *s, size_t n)
{
    char *p = malloc(n + 1);

    if (p != NULL) {
        memcpy(p, s, n);
        p[n] = '\0';
    }
    return p;
}

static int toml_find(const IcoToml *t, const char *path)
{
    int i;

    for (i = 0; i < t->count; i++) {
        if (strcmp(t->entry[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

/* Takes ownership of path and value. */
static int toml_put(IcoToml *t, char *path, char *value, int raw)
{
    int i = toml_find(t, path);

    if (i >= 0) { /* the later line wins */
        free(t->entry[i].value);
        free(path);
        t->entry[i].value = value;
        t->entry[i].raw = raw;
        return 0;
    }
    if (t->count == t->cap) {
        int cap = t->cap != 0 ? t->cap * 2 : 32;
        IcoTomlEntry *grown = realloc(t->entry, (size_t)cap * sizeof(*grown));

        if (grown == NULL) {
            free(path);
            free(value);
            return -1;
        }
        t->entry = grown;
        t->cap = cap;
    }
    t->entry[t->count].path = path;
    t->entry[t->count].value = value;
    t->entry[t->count].raw = raw;
    t->count++;
    return 0;
}

/* A value's text: a "..." or '...' string loses its quotes (and "\\" and
   "\"" are unescaped; *raw is 0), else a trailing # comment and blanks are
   dropped; an array (one line, [a, "b"]) stays as written, brackets
   included (*raw is 1). */
static char *toml_value(const char *s, int *raw)
{
    size_t n = strlen(s);
    char *out;
    size_t i, o = 0;
    char q;

    while (*s == ' ' || *s == '\t') {
        s++;
        n--;
    }
    out = malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    if (*s == '"' || *s == '\'') {
        *raw = 0;
        q = *s++;
        for (i = 0; s[i] != '\0' && s[i] != q; i++) {
            if (q == '"' && s[i] == '\\' && (s[i + 1] == '\\' || s[i + 1] == '"')) {
                i++;
            }
            out[o++] = s[i];
        }
        out[o] = '\0';
        return out;
    }
    *raw = 1;
    q = 0;
    for (i = 0; s[i] != '\0'; i++) {
        if (q != 0) {
            if (s[i] == q) {
                q = 0;
            }
        } else if (s[i] == '"' || s[i] == '\'') {
            q = s[i];
        } else if (s[i] == '#') {
            break;
        }
        out[o++] = s[i];
    }
    while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '\t' || out[o - 1] == '\r')) {
        o--;
    }
    out[o] = '\0';
    return out;
}

enum { LINE_NONE, LINE_SECTION, LINE_KEY };

/* Reads one line [line, eol): a section header updates `section` (the name
   trimmed); a key gives its range and where its value text starts (after the
   '='); anything else, and what the reader ignores (a header without ']', a
   key of length 0 or over 120), is LINE_NONE. */
static int toml_scan(const char *line, const char *eol, char *section, size_t secsize,
                     const char **kb, size_t *kn, const char **val)
{
    const char *eq;
    const char *k1;
    size_t n;

    while (line < eol && (*line == ' ' || *line == '\t')) {
        line++;
    }
    if (line == eol || *line == '#' || *line == '\r') {
        return LINE_NONE;
    }
    if (*line == '[') {
        const char *end = memchr(line, ']', (size_t)(eol - line));
        const char *a = line + 1;

        if (end == NULL) {
            return LINE_NONE;
        }
        while (a < end && (*a == ' ' || *a == '\t')) {
            a++;
        }
        n = (size_t)(end - a);
        while (n > 0 && (a[n - 1] == ' ' || a[n - 1] == '\t')) {
            n--;
        }
        if (n >= secsize) {
            n = secsize - 1;
        }
        memcpy(section, a, n);
        section[n] = '\0';
        return LINE_SECTION;
    }
    eq = memchr(line, '=', (size_t)(eol - line));
    if (eq == NULL) {
        return LINE_NONE;
    }
    k1 = eq;
    while (k1 > line && (k1[-1] == ' ' || k1[-1] == '\t')) {
        k1--;
    }
    n = (size_t)(k1 - line);
    if (n == 0 || n > 120) {
        return LINE_NONE;
    }
    *kb = line;
    *kn = n;
    *val = eq + 1;
    return LINE_KEY;
}

/* "section.key", or "key" before any section; malloc'd. */
static char *toml_path(const char *section, const char *key, size_t n)
{
    size_t sl = strlen(section);
    char *path = malloc(sl + 1 + n + 1);

    if (path == NULL) {
        return NULL;
    }
    if (sl != 0) {
        memcpy(path, section, sl);
        path[sl] = '.';
        memcpy(path + sl + 1, key, n);
        path[sl + 1 + n] = '\0';
    } else {
        memcpy(path, key, n);
        path[n] = '\0';
    }
    return path;
}

IcoToml *ico_toml_parse(const char *text)
{
    IcoToml *t = calloc(1, sizeof(*t));
    char section[128] = "";
    const char *p = text;

    if (t == NULL) {
        return NULL;
    }
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) {
        p += 3;
    }
    while (*p != '\0') {
        const char *eol = p;
        const char *line = p;
        const char *kb;
        const char *val;
        size_t kn;

        while (*eol != '\0' && *eol != '\n') {
            eol++;
        }
        p = *eol != '\0' ? eol + 1 : eol;
        if (toml_scan(line, eol, section, sizeof(section), &kb, &kn, &val) == LINE_KEY) {
            char *path = toml_path(section, kb, kn);
            char *lineval = dup_range(val, (size_t)(eol - val));
            char *value;
            int raw = 1;

            if (path == NULL || lineval == NULL) {
                free(path);
                free(lineval);
                break;
            }
            value = toml_value(lineval, &raw);
            free(lineval);
            if (value == NULL || toml_put(t, path, value, raw) != 0) {
                free(value);
                break;
            }
        }
    }
    return t;
}

IcoToml *ico_toml_load(const char *path)
{
    char *text = read_text(path);
    IcoToml *t;

    if (text == NULL) {
        return NULL;
    }
    t = ico_toml_parse(text);
    free(text);
    return t;
}

void ico_toml_free(IcoToml *t)
{
    int i;

    if (t == NULL) {
        return;
    }
    for (i = 0; i < t->count; i++) {
        free(t->entry[i].path);
        free(t->entry[i].value);
    }
    free(t->entry);
    free(t);
}

const char *ico_toml_get(const IcoToml *t, const char *path)
{
    int i;

    if (t == NULL) {
        return NULL;
    }
    i = toml_find(t, path);
    return i >= 0 ? t->entry[i].value : NULL;
}

int ico_toml_has(const IcoToml *t, const char *path)
{
    return t != NULL && toml_find(t, path) >= 0;
}

int ico_toml_get_bool(const IcoToml *t, const char *path, int def)
{
    const char *v = ico_toml_get(t, path);

    if (v == NULL) {
        return def;
    }
    if (strcmp(v, "true") == 0 || strcmp(v, "1") == 0) {
        return 1;
    }
    if (strcmp(v, "false") == 0 || strcmp(v, "0") == 0) {
        return 0;
    }
    return def;
}

double ico_toml_get_float(const IcoToml *t, const char *path, double def)
{
    const char *v = ico_toml_get(t, path);
    char *end;
    double d;

    if (v == NULL || v[0] == '\0') {
        return def;
    }
    d = strtod(v, &end);
    return *end == '\0' ? d : def;
}

long long ico_toml_get_int(const IcoToml *t, const char *path, long long def)
{
    const char *v = ico_toml_get(t, path);
    char *end;
    long long n;

    if (v == NULL || v[0] == '\0') {
        return def;
    }
    n = strtoll(v, &end, 10);
    return *end == '\0' ? n : def;
}

static int toml_set(IcoToml *t, const char *path, const char *value, int raw)
{
    char *p = dup_range(path, strlen(path));
    char *v = dup_range(value, strlen(value));

    if (p == NULL || v == NULL) {
        free(p);
        free(v);
        return -1;
    }
    return toml_put(t, p, v, raw);
}

int ico_toml_set_string(IcoToml *t, const char *path, const char *value)
{
    return toml_set(t, path, value, 0);
}

int ico_toml_set_bool(IcoToml *t, const char *path, int value)
{
    return toml_set(t, path, value ? "true" : "false", 1);
}

int ico_toml_set_int(IcoToml *t, const char *path, long long value)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "%lld", value);
    return toml_set(t, path, buf, 1);
}

int ico_toml_set_float(IcoToml *t, const char *path, double value)
{
    char buf[48];

    snprintf(buf, sizeof(buf), "%.9g", value);
    if (strpbrk(buf, ".en") == NULL) { /* TOML floats need a fraction or an exponent */
        strcat(buf, ".0");
    }
    return toml_set(t, path, buf, 1);
}

/* --- writing config.toml -------------------------------------------------- */

typedef struct Buf {
    char *p;
    size_t len;
    size_t cap;
    int bad;
} Buf;

static void buf_add(Buf *b, const char *s, size_t n)
{
    if (b->bad || n == 0) {
        return;
    }
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap != 0 ? b->cap : 1024;
        char *grown;

        while (b->len + n + 1 > cap) {
            cap *= 2;
        }
        grown = realloc(b->p, cap);
        if (grown == NULL) {
            b->bad = 1;
            return;
        }
        b->p = grown;
        b->cap = cap;
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}

static void buf_str(Buf *b, const char *s)
{
    buf_add(b, s, strlen(s));
}

/* The value as it is written: bare, or quoted with \\ and \" escaped. */
static void buf_value(Buf *b, const IcoTomlEntry *e)
{
    const char *s;

    if (e->raw) {
        buf_str(b, e->value);
        return;
    }
    buf_add(b, "\"", 1);
    for (s = e->value; *s != '\0'; s++) {
        if (*s == '\\' || *s == '"') {
            buf_add(b, "\\", 1);
        }
        buf_add(b, s, 1);
    }
    buf_add(b, "\"", 1);
}

enum { SEC_ENDS = 64 };

typedef struct SecEnd {
    char name[128];
    size_t off; /* where a new key of that section goes in the output */
} SecEnd;

typedef struct Insert {
    size_t off;
    int seq;
    Buf text;
} Insert;

static void sec_end_set(SecEnd *ends, int *n, const char *name, size_t off)
{
    int i;

    for (i = 0; i < *n; i++) {
        if (strcmp(ends[i].name, name) == 0) {
            ends[i].off = off;
            return;
        }
    }
    if (*n < SEC_ENDS) {
        snprintf(ends[*n].name, sizeof(ends[*n].name), "%s", name);
        ends[*n].off = off;
        (*n)++;
    }
}

/* The section part of an entry path: everything before its last '.'. */
static size_t path_section_len(const char *path)
{
    const char *dot = strrchr(path, '.');

    return dot != NULL ? (size_t)(dot - path) : 0;
}

char *ico_toml_render(const IcoToml *t, const char *existing)
{
    Buf out = {0};
    Buf result = {0};
    char *used = calloc((size_t)(t->count > 0 ? t->count : 1), 1);
    SecEnd ends[SEC_ENDS];
    int n_ends = 0;
    size_t first_header = (size_t)-1;
    const char *nl = "\n";
    char section[128] = "";
    const char *p;
    Insert *ins;
    int n_ins = 0;
    int seq = 0;
    int i, j;

    if (used == NULL) {
        return NULL;
    }
    ins = calloc((size_t)(t->count + 1), sizeof(*ins));
    if (ins == NULL) {
        free(used);
        return NULL;
    }
    if (existing == NULL) {
        existing = "";
    }
    if (strstr(existing, "\r\n") != NULL) {
        nl = "\r\n";
    }
    p = existing;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) {
        buf_add(&out, p, 3);
        p += 3;
    }
    while (*p != '\0') {
        const char *eol = strchr(p, '\n');
        const char *line_end = eol != NULL ? eol : p + strlen(p);
        const char *kb;
        const char *val;
        size_t kn;
        int kind = toml_scan(p, line_end, section, sizeof(section), &kb, &kn, &val);

        if (kind == LINE_SECTION) {
            if (first_header == (size_t)-1) {
                first_header = out.len;
            }
            buf_add(&out, p, (size_t)(line_end - p));
            if (eol != NULL) {
                buf_add(&out, "\n", 1);
            } else {
                buf_str(&out, nl);
            }
            sec_end_set(ends, &n_ends, section, out.len);
        } else if (kind == LINE_KEY) {
            char *path = toml_path(section, kb, kn);
            int idx = path != NULL ? toml_find(t, path) : -1;
            int same = 0;

            free(path);
            if (idx >= 0) {
                char *lineval = dup_range(val, (size_t)(line_end - val));
                int raw = 1;
                char *cur = lineval != NULL ? toml_value(lineval, &raw) : NULL;

                same = cur != NULL && strcmp(cur, t->entry[idx].value) == 0 &&
                       raw == t->entry[idx].raw;
                free(cur);
                free(lineval);
                used[idx] = 1;
            }
            if (idx >= 0 && !same) {
                const char *cr = line_end > p && line_end[-1] == '\r' ? line_end - 1 : line_end;

                buf_add(&out, p, (size_t)(val - p)); /* up to and including the '=' */
                buf_str(&out, " ");
                buf_value(&out, &t->entry[idx]);
                buf_add(&out, cr, (size_t)(line_end - cr));
            } else {
                buf_add(&out, p, (size_t)(line_end - p));
            }
            if (eol != NULL) {
                buf_add(&out, "\n", 1);
            } else {
                buf_str(&out, nl);
            }
            sec_end_set(ends, &n_ends, section, out.len);
        } else {
            buf_add(&out, p, (size_t)(line_end - p));
            if (eol != NULL) {
                buf_add(&out, "\n", 1);
            } else {
                buf_str(&out, nl);
            }
        }
        p = eol != NULL ? eol + 1 : line_end;
    }

    /* the keys the text does not have, one insert per section in the order t
       holds them; a section the text has gets them after its last key, a new
       one is added at the end, top-level keys go before the first header */
    for (i = 0; i < t->count; i++) {
        size_t sl;
        char name[128];
        Buf block = {0};
        int existing_sec = 0;
        size_t off = 0;
        int seq_add = 0;

        if (used[i]) {
            continue;
        }
        sl = path_section_len(t->entry[i].path);
        if (sl >= sizeof(name)) {
            sl = sizeof(name) - 1;
        }
        memcpy(name, t->entry[i].path, sl);
        name[sl] = '\0';
        for (j = i; j < t->count; j++) {
            const char *path = t->entry[j].path;

            if (!used[j] && path_section_len(path) == sl && strncmp(path, name, sl) == 0) {
                const char *key = sl != 0 ? path + sl + 1 : path;

                buf_str(&block, key);
                buf_str(&block, " = ");
                buf_value(&block, &t->entry[j]);
                buf_str(&block, nl);
                used[j] = 2; /* handled */
            }
        }
        for (j = 0; j < n_ends; j++) {
            if (strcmp(ends[j].name, name) == 0) {
                existing_sec = 1;
                off = ends[j].off;
            }
        }
        if (sl == 0 && !existing_sec) { /* no top-level key yet: before the first header */
            existing_sec = 1;
            off = first_header != (size_t)-1 ? first_header : out.len;
            if (first_header != (size_t)-1) {
                buf_str(&block, nl);
            }
        }
        if (!existing_sec) {
            Buf text = {0};

            if (out.len > 0 || n_ins > 0) {
                if (!(n_ins == 0 && out.len >= 2 && memcmp(out.p + out.len - 2, "\n\n", 2) == 0) &&
                    !(n_ins == 0 && out.len >= 4 &&
                      memcmp(out.p + out.len - 4, "\r\n\r\n", 4) == 0)) {
                    buf_str(&text, nl);
                }
            }
            buf_str(&text, "[");
            buf_str(&text, name);
            buf_str(&text, "]");
            buf_str(&text, nl);
            buf_add(&text, block.p != NULL ? block.p : "", block.len);
            free(block.p);
            block = text;
            off = out.len;
            seq_add = 1000000;
        }
        ins[n_ins].off = off;
        ins[n_ins].seq = seq++ + seq_add;
        ins[n_ins].text = block;
        n_ins++;
    }
    /* by offset, then in the order made (appended sections last) */
    for (i = 1; i < n_ins; i++) {
        Insert key = ins[i];

        for (j = i; j > 0 && (ins[j - 1].off > key.off ||
                              (ins[j - 1].off == key.off && ins[j - 1].seq > key.seq));
             j--) {
            ins[j] = ins[j - 1];
        }
        ins[j] = key;
    }
    {
        size_t pos = 0;

        for (i = 0; i < n_ins; i++) {
            buf_add(&result, out.p + pos, ins[i].off - pos);
            buf_add(&result, ins[i].text.p != NULL ? ins[i].text.p : "", ins[i].text.len);
            pos = ins[i].off;
            free(ins[i].text.p);
        }
        buf_add(&result, out.p != NULL ? out.p + pos : "", out.len - pos);
    }
    free(used);
    free(ins);
    free(out.p);
    if (result.bad) {
        free(result.p);
        return NULL;
    }
    if (result.p == NULL) {
        result.p = calloc(1, 1);
    }
    return result.p;
}

int ico_toml_save(const IcoToml *t, const char *path)
{
    char *existing = read_text(path);
    char *text = ico_toml_render(t, existing);
    char tmp[ICO_PATH_MAX + 8];
    FILE *f;
    size_t n;
    int ok;

    free(existing);
    if (text == NULL) {
        return -1;
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = ico_fopen(tmp, "wb");
    if (f == NULL) {
        free(text);
        return -1;
    }
    n = strlen(text);
    ok = fwrite(text, 1, n, f) == n;
    ok = ico_fsync(f) == 0 && ok; /* on the disk before the move */
    ok = fclose(f) == 0 && ok;
    free(text);
    if (ok) {
        ok = ico_rename_replace(tmp, path) == 0;
    }
    if (!ok) {
        ico_remove(tmp);
        return -1;
    }
    return 0;
}

/* portable mode: -1 the folder decides, 0 off (portable=0), 1 on */
#ifndef ICO_HOST_FORCE_PORTABLE
static int portable_setting = -1;
#endif
/* the reason portable mode could not be used was logged */
static int portable_logged;

int ico_host_portable_value(const char *v)
{
    char w[8];
    size_t n = 0;

    if (v == NULL) {
        return -1;
    }
    while (isspace((unsigned char)*v)) {
        v++;
    }
    while (*v != '\0' && !isspace((unsigned char)*v)) {
        if (n + 1 >= sizeof(w)) {
            return 1; /* longer than any word for off */
        }
        w[n++] = (char)tolower((unsigned char)*v++);
    }
    w[n] = '\0';
    if (n == 0) {
        return -1;
    }
    return strcmp(w, "0") != 0 && strcmp(w, "false") != 0 && strcmp(w, "no") != 0 &&
           strcmp(w, "off") != 0;
}

void ico_host_set_portable(int setting)
{
#ifdef ICO_HOST_FORCE_PORTABLE
    if (setting >= 0 && !portable_logged) {
        portable_logged = 1;
        fprintf(stderr,
                "ico_pc: portable=%d in ico-pc.ini is ignored: the settings and saves "
                "always live in the program's own folder here\n",
                setting != 0);
    }
#else
    portable_setting = setting < 0 ? -1 : (setting != 0);
    portable_logged = 0;
#endif
}

/* the userdata folder beside the program when portable mode is on (made if
   missing), else 0 */
static int portable_dir(char *out, size_t size)
{
#ifdef ICO_HOST_FORCE_PORTABLE
    /* the executable's folder itself (Android: the app's files folder) */
    return ico_host_exe_dir(out, size) == 0;
#else
    char exe[ICO_PATH_MAX], dir[ICO_PATH_MAX];

    if (portable_setting == 0 || ico_host_exe_dir(exe, sizeof(exe)) != 0 ||
        ico_path_join(dir, sizeof(dir), exe, "userdata") != 0) {
        return 0;
    }
    if (portable_setting < 0 && ico_path_kind(dir, NULL, NULL) != 1) {
        return 0;
    }
    if (ico_make_dir(dir) != 0) {
        /* cannot be made: the user profile rather than no saves, and the
           log says so once (a program folder the player may not write to,
           such as Program Files) */
        if (!portable_logged) {
            portable_logged = 1;
            fprintf(stderr,
                    "ico_pc: portable mode is on, but the folder %s cannot be made (%s); "
                    "using the user folder instead\n",
                    dir, strerror(errno));
        }
        return 0;
    }
    copy(out, size, dir);
    return 1;
#endif
}

int ico_host_pref_is_portable(void)
{
    char dir[ICO_PATH_MAX];

    return portable_dir(dir, sizeof(dir));
}

int ico_host_pref_dir(char *out, size_t size)
{
    if (portable_dir(out, size)) {
        return 0;
    }
#ifdef ICO_HOST_SDL_PREFPATH
    static char cached[ICO_PATH_MAX];

    if (cached[0] == '\0') {
        char *p = SDL_GetPrefPath("ico-pc", "ico-pc");

        if (p != NULL) {
            size_t n = strlen(p);

            while (n > 1 && (p[n - 1] == '/' || p[n - 1] == '\\')) {
                p[--n] = '\0';
            }
            copy(cached, sizeof(cached), p);
            SDL_free(p);
        }
    }
    if (cached[0] != '\0') {
        copy(out, size, cached);
        return 0;
    }
#endif
    return ico_host_exe_dir(out, size);
}

int ico_host_ini_path(char *out, size_t size)
{
    char dir[ICO_PATH_MAX];
    int r = ico_host_exe_dir(dir, sizeof(dir));

    if (ico_path_join(out, size, dir, "ico-pc.ini") != 0) {
        return -1;
    }
    return r;
}

/* ico_host_saves2_dir and ico_host_saves_dir (below) are a read of
   ico-pc.ini over config.toml and nothing else: no environment variable is
   set and no folder made, so they can be called from the game fiber
   (sceMcInit). */

/* The folder a card path key names: relative paths from the executable's
   folder. 1 when the key is set, 0 when it is absent or empty, -1 when the
   path does not fit. */
static int card_dir_key(const IcoIni *ini, const char *dir, const char *key, char *out, size_t size)
{
    const char *v = ico_ini_get(ini, key);

    if (v == NULL || v[0] == '\0') {
        return 0;
    }
    return ico_path_join(out, size, dir, v) == 0 ? 1 : -1;
}

int ico_host_saves2_dir(char *out, size_t size)
{
    char dir[ICO_PATH_MAX];
    char ini_path[ICO_PATH_MAX];
    IcoIni *ini = malloc(sizeof(*ini));
    int r;

    if (size > 0) {
        out[0] = '\0';
    }
    if (ini == NULL || ico_host_exe_dir(dir, sizeof(dir)) != 0 ||
        ico_path_join(ini_path, sizeof(ini_path), dir, "ico-pc.ini") != 0) {
        free(ini);
        return -1;
    }
    ico_ini_load_layered(ini, ini_path);
    r = card_dir_key(ini, dir, "saves2", out, size);
    free(ini);
    if (r < 0 && size > 0) {
        out[0] = '\0';
    }
    return r;
}

int ico_host_saves_dir(char *out, size_t size)
{
    char dir[ICO_PATH_MAX];
    char ini_path[ICO_PATH_MAX];
    IcoIni *ini = malloc(sizeof(*ini));
    const char *v;
    int r = 0;

    if (ini == NULL || ico_host_exe_dir(dir, sizeof(dir)) != 0 ||
        ico_path_join(ini_path, sizeof(ini_path), dir, "ico-pc.ini") != 0) {
        free(ini);
        copy(out, size, "memcard");
        return -1;
    }
    ico_ini_load_layered(ini, ini_path);
    v = ico_ini_get(ini, "saves");
    if (v != NULL && v[0] != '\0') {
        r = card_dir_key(ini, dir, "saves", out, size) == 1 ? 0 : -1;
    } else {
        r = ico_host_pref_dir(dir, sizeof(dir));
        if (ico_path_join(out, size, dir, "memcard") != 0) {
            r = -1;
        }
    }
    free(ini);
    if (r != 0 && out[0] == '\0') {
        copy(out, size, "memcard");
    }
    return r;
}
