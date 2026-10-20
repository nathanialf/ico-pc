/*
 * port/platform/steam_shortcut.c
 *
 * Add to Steam (steam_shortcut.h): the binary VDF reader and writer for
 * shortcuts.vdf, the Steam folder search, the running-Steam check, and the
 * file update. Written for this port; no third-party code.
 *
 * shortcuts.vdf is a tree of typed, named nodes ending in one 0x08 byte per
 * map:
 *   0x00 name\0 children... 0x08      a map
 *   0x01 name\0 value\0               a string
 *   0x02 name\0 int32 (little endian) an integer
 * The file is the root map ("shortcuts" is its one child, holding one map per
 * entry named "0", "1", ...), so an empty library is
 * 00 "shortcuts" 00 08 08.
 */
#include "steam_shortcut.h"
#include "host_fs.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

/* --- CRC and id ----------------------------------------------------------- */

uint32_t ico_steam_crc32(const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t c = 0xFFFFFFFFu;

    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
    }
    return c ^ 0xFFFFFFFFu;
}

int32_t ico_steam_appid(const char *quotedExe, const char *appName)
{
    char buf[2 * ICO_STEAM_PATH_MAX + 64];
    const int n = snprintf(buf, sizeof(buf), "%s%s", quotedExe, appName);
    const size_t len = n < 0 ? 0 : (size_t)n >= sizeof(buf) ? sizeof(buf) - 1 : (size_t)n;

    return (int32_t)(ico_steam_crc32(buf, len) | 0x80000000u);
}

/* --- the binary VDF walk --------------------------------------------------- */

typedef struct Rd {
    const uint8_t *p;
    size_t n, i;
} Rd;

enum { MAP_ROOT, MAP_SHORTCUTS, MAP_ENTRY, MAP_OTHER };

typedef struct Walk {
    const char *exe; /* the Exe to look for, or NULL */
    char quoted[ICO_STEAM_PATH_MAX + 2];
    int caseless;
    int found;
    long maxIndex;     /* the highest entry index, -1 for none */
    size_t scEnd;      /* offset of the 0x08 ending the shortcuts map */
    int haveShortcuts; /* the shortcuts map was seen */
} Walk;

/* a NUL-terminated string at the read position */
static int rd_str(Rd *r, const char **s)
{
    const uint8_t *z;

    if (r->i >= r->n) {
        return -1;
    }
    z = memchr(r->p + r->i, 0, r->n - r->i);
    if (z == NULL) {
        return -1;
    }
    *s = (const char *)(r->p + r->i);
    r->i = (size_t)(z - r->p) + 1;
    return 0;
}

static int same(const char *a, const char *b, int caseless)
{
    if (!caseless) {
        return strcmp(a, b) == 0;
    }
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
    }
    return *a == *b;
}

static int walk_map(Rd *r, int kind, int depth, Walk *w)
{
    if (depth > 8) {
        return -1;
    }
    for (;;) {
        const char *name, *val;
        int t;

        if (r->i >= r->n) {
            return -1; /* the map never ended */
        }
        t = r->p[r->i];
        if (t == 0x08) {
            if (kind == MAP_SHORTCUTS) {
                w->scEnd = r->i;
            }
            r->i++;
            return 0;
        }
        r->i++;
        if (rd_str(r, &name) != 0) {
            return -1;
        }
        switch (t) {
        case 0x00: {
            int sub = MAP_OTHER;

            if (kind == MAP_ROOT && same(name, "shortcuts", 1)) {
                sub = MAP_SHORTCUTS;
                w->haveShortcuts = 1;
            } else if (kind == MAP_SHORTCUTS) {
                char *end;
                long idx = strtol(name, &end, 10);

                if (end != name && *end == '\0' && idx > w->maxIndex) {
                    w->maxIndex = idx;
                }
                sub = MAP_ENTRY;
            }
            if (walk_map(r, sub, depth + 1, w) != 0) {
                return -1;
            }
            break;
        }
        case 0x01:
            if (rd_str(r, &val) != 0) {
                return -1;
            }
            if (kind == MAP_ENTRY && w->exe != NULL && same(name, "exe", 1) &&
                (same(val, w->quoted, w->caseless) || same(val, w->exe, w->caseless))) {
                w->found = 1;
            }
            break;
        case 0x02:
        case 0x03: /* float */
        case 0x06: /* color */
            if (r->n - r->i < 4) {
                return -1;
            }
            r->i += 4;
            break;
        case 0x07: /* uint64 */
        case 0x0A: /* int64 */
            if (r->n - r->i < 8) {
                return -1;
            }
            r->i += 8;
            break;
        default:
            return -1; /* a node type this reader does not know: leave the file alone */
        }
    }
}

/* the whole file; 0 when it is a shortcuts file (or empty) */
static int walk_file(const uint8_t *vdf, size_t n, Walk *w)
{
    Rd r = {vdf, n, 0};

    w->found = 0;
    w->maxIndex = -1;
    w->scEnd = 0;
    w->haveShortcuts = 0;
    if (n == 0) {
        return 0;
    }
    if (walk_map(&r, MAP_ROOT, 0, w) != 0 || r.i != n || !w->haveShortcuts) {
        return -1;
    }
    return 0;
}

static void walk_init(Walk *w, const char *exe, int caseless)
{
    memset(w, 0, sizeof(*w));
    w->exe = exe;
    w->caseless = caseless;
    if (exe != NULL) {
        snprintf(w->quoted, sizeof(w->quoted), "\"%s\"", exe);
    }
}

int ico_steam_vdf_find_exe(const uint8_t *vdf, size_t n, const char *exe, int caseless)
{
    Walk w;

    walk_init(&w, exe, caseless);
    if (walk_file(vdf, n, &w) != 0) {
        return -1;
    }
    return w.found;
}

/* --- building an entry ----------------------------------------------------- */

typedef struct Buf {
    uint8_t *p;
    size_t n, cap;
    int bad;
} Buf;

static void b_put(Buf *b, const void *d, size_t n)
{
    if (b->bad) {
        return;
    }
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        uint8_t *q;

        while (cap < b->n + n) {
            cap *= 2;
        }
        q = (uint8_t *)realloc(b->p, cap);
        if (q == NULL) {
            b->bad = 1;
            return;
        }
        b->p = q;
        b->cap = cap;
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
}

static void b_byte(Buf *b, int v)
{
    const uint8_t c = (uint8_t)v;

    b_put(b, &c, 1);
}

static void b_cstr(Buf *b, const char *s)
{
    b_put(b, s, strlen(s) + 1);
}

static void b_str(Buf *b, const char *name, const char *val)
{
    b_byte(b, 0x01);
    b_cstr(b, name);
    b_cstr(b, val);
}

static void b_int(Buf *b, const char *name, uint32_t v)
{
    const uint8_t le[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};

    b_byte(b, 0x02);
    b_cstr(b, name);
    b_put(b, le, 4);
}

/* the fields Steam writes for a shortcut, in its order */
static void b_entry(Buf *b, long index, const IcoSteamEntry *e)
{
    char idx[24], quotedExe[ICO_STEAM_PATH_MAX + 2], quotedDir[ICO_STEAM_PATH_MAX + 2];

    snprintf(idx, sizeof(idx), "%ld", index);
    snprintf(quotedExe, sizeof(quotedExe), "\"%s\"", e->exe);
    snprintf(quotedDir, sizeof(quotedDir), "\"%s\"", e->startDir);
    b_byte(b, 0x00);
    b_cstr(b, idx);
    b_int(b, "appid", (uint32_t)ico_steam_appid(quotedExe, e->name));
    b_str(b, "AppName", e->name);
    b_str(b, "Exe", quotedExe);
    b_str(b, "StartDir", quotedDir);
    b_str(b, "icon", e->icon != NULL ? e->icon : "");
    b_str(b, "ShortcutPath", "");
    b_str(b, "LaunchOptions", "");
    b_int(b, "IsHidden", 0);
    b_int(b, "AllowDesktopConfig", 1);
    b_int(b, "AllowOverlay", 1);
    b_int(b, "OpenVR", 0);
    b_int(b, "Devkit", 0);
    b_str(b, "DevkitGameID", "");
    b_int(b, "DevkitOverrideAppID", 0);
    b_int(b, "LastPlayTime", 0);
    b_str(b, "FlatpakAppID", "");
    b_byte(b, 0x00);
    b_cstr(b, "tags");
    b_byte(b, 0x08);
    b_byte(b, 0x08);
}

int ico_steam_vdf_append(const uint8_t *vdf, size_t n, const IcoSteamEntry *e, uint8_t **out,
                         size_t *outN)
{
    static const uint8_t kEmpty[] = {0x00, 's', 'h', 'o',  'r',  't', 'c',
                                     'u',  't', 's', 0x00, 0x08, 0x08};
    Walk w;
    Buf b = {0};
    size_t at;

    if (strlen(e->exe) > ICO_STEAM_PATH_MAX || strlen(e->startDir) > ICO_STEAM_PATH_MAX) {
        return -1;
    }
    walk_init(&w, NULL, 0);
    if (n == 0) {
        vdf = kEmpty;
        n = sizeof(kEmpty);
    }
    if (walk_file(vdf, n, &w) != 0) {
        return -1;
    }
    at = w.scEnd;
    b_put(&b, vdf, at);
    b_entry(&b, w.maxIndex + 1, e);
    b_put(&b, vdf + at, n - at);
    if (b.bad) {
        free(b.p);
        return -1;
    }
    *out = b.p;
    *outN = b.n;
    return 0;
}

/* --- loginusers.vdf (text) -------------------------------------------------- */

/* The next token of the text format: a quoted string (into tok, 0 on
   overflow truncation), '{' or '}'; 0 at the end. */
static int vdf_token(const char *s, size_t n, size_t *i, char *tok, size_t tokSize)
{
    for (;;) {
        while (*i < n && isspace((unsigned char)s[*i])) {
            (*i)++;
        }
        if (*i + 1 < n && s[*i] == '/' && s[*i + 1] == '/') {
            while (*i < n && s[*i] != '\n') {
                (*i)++;
            }
            continue;
        }
        break;
    }
    if (*i >= n) {
        return 0;
    }
    if (s[*i] == '{' || s[*i] == '}') {
        return s[(*i)++];
    }
    if (s[*i] == '"') {
        size_t k = 0;

        (*i)++;
        while (*i < n && s[*i] != '"') {
            if (s[*i] == '\\' && *i + 1 < n) {
                (*i)++;
            }
            if (k + 1 < tokSize) {
                tok[k++] = s[*i];
            }
            (*i)++;
        }
        tok[k] = '\0';
        if (*i < n) {
            (*i)++;
        }
        return '"';
    }
    (*i)++; /* a stray byte */
    return -1;
}

int ico_steam_login_users(const char *text, size_t n, uint32_t *ids, int max)
{
    char tok[128], key[128], userKey[128];
    int haveKey = 0, depth = 0, recent = 0, count = 0, recentCount = 0;
    size_t i = 0;
    int t;

    while ((t = vdf_token(text, n, &i, tok, sizeof(tok))) != 0) {
        if (t == '"') {
            if (!haveKey) {
                memcpy(key, tok, sizeof(key));
                haveKey = 1;
            } else {
                if (depth == 2 && same(key, "MostRecent", 1) && strcmp(tok, "1") == 0) {
                    recent = 1;
                }
                haveKey = 0;
            }
        } else if (t == '{') {
            depth++;
            if (depth == 2) {
                if (haveKey) {
                    memcpy(userKey, key, sizeof(userKey));
                } else {
                    userKey[0] = 0;
                }
                recent = 0;
            }
            haveKey = 0;
        } else if (t == '}') {
            if (depth == 2 && count < max) {
                char *end;
                unsigned long long v = strtoull(userKey, &end, 10);
                const uint32_t id = (uint32_t)(v & 0xFFFFFFFFull);

                if (end != userKey && *end == '\0' && id != 0) {
                    if (recent) {
                        /* the most recent first: shift the rest down */
                        memmove(ids + recentCount + 1, ids + recentCount,
                                (size_t)(count - recentCount) * sizeof(ids[0]));
                        ids[recentCount++] = id;
                    } else {
                        ids[count] = id;
                    }
                    count++;
                }
            }
            if (depth > 0) {
                depth--;
            }
            haveKey = 0;
        }
    }
    return count;
}

/* --- Steam running ---------------------------------------------------------- */

#ifndef _WIN32
/* 1 when text holds "steam" in any case */
static int has_steam(const char *text)
{
    static const char kWord[] = "steam";

    for (; *text != '\0'; text++) {
        size_t k = 0;

        while (kWord[k] != '\0' && text[k] != '\0' && tolower((unsigned char)text[k]) == kWord[k]) {
            k++;
        }
        if (kWord[k] == '\0') {
            return 1;
        }
    }
    return 0;
}
#endif

/* A pid file outlives a Steam that was killed, and its pid can be given to
   another process later, so a live pid counts only when the process is a
   Steam one: its name has "steam" in it (Linux: /proc/<pid>/comm; Windows:
   the image name steam.exe). Without /proc the pid alone decides. */
int ico_steam_pid_alive(long pid)
{
    if (pid <= 0) {
        return 0;
    }
#ifdef _WIN32
    {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
        DWORD code = 0;
        int alive;

        if (h == NULL) {
            return 0;
        }
        alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
        if (alive) {
            wchar_t path[MAX_PATH];
            DWORD n = MAX_PATH;

            if (QueryFullProcessImageNameW(h, 0, path, &n) && n > 0) {
                const wchar_t *base = path;

                for (const wchar_t *c = path; *c != 0; c++) {
                    if (*c == L'\\' || *c == L'/') {
                        base = c + 1;
                    }
                }
                alive = _wcsicmp(base, L"steam.exe") == 0;
            } /* a name that cannot be read: the pid alone decides */
        }
        CloseHandle(h);
        return alive;
    }
#else
    {
        char path[48], comm[64];
        FILE *f;

        if (kill((pid_t)pid, 0) != 0 && errno != EPERM) {
            return 0;
        }
        snprintf(path, sizeof(path), "/proc/%ld/comm", pid);
        f = fopen(path, "rb");
        if (f == NULL) {
            return 1; /* no /proc to ask */
        }
        comm[fread(comm, 1, sizeof(comm) - 1, f)] = '\0';
        fclose(f);
        return has_steam(comm);
    }
#endif
}

static long read_pid_file(const char *path)
{
    FILE *f = ico_fopen(path, "rb");
    char buf[32];
    size_t got;
    char *end;
    long v;

    if (f == NULL) {
        return 0;
    }
    got = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[got] = '\0';
    v = strtol(buf, &end, 10);
    return end != buf ? v : 0;
}

int ico_steam_running(const IcoSteamEnv *env)
{
    int (*alive)(long) = env->pidAlive != NULL ? env->pidAlive : ico_steam_pid_alive;
    long pid;

    for (int i = 0; i < 3; i++) {
        if (env->pidFiles[i][0] != '\0' && alive(read_pid_file(env->pidFiles[i]))) {
            return 1;
        }
    }
    if (env->activeProcessPid != NULL && env->activeProcessPid(&pid) && alive(pid)) {
        return 1;
    }
    return env->processRunning != NULL && env->processRunning();
}

/* --- the update -------------------------------------------------------------- */

static int read_all(const char *path, uint8_t **data, size_t *n)
{
    FILE *f = ico_fopen(path, "rb");
    size_t cap = 4096, len = 0;
    uint8_t *p;

    *data = NULL;
    *n = 0;
    if (f == NULL) {
        return ico_path_kind(path, NULL, NULL) == -1 ? 1 : -1; /* 1: no file */
    }
    p = (uint8_t *)malloc(cap);
    while (p != NULL) {
        size_t got = fread(p + len, 1, cap - len, f);

        len += got;
        if (len < cap) {
            break;
        }
        cap *= 2;
        {
            uint8_t *q = (uint8_t *)realloc(p, cap);

            if (q == NULL) {
                free(p);
                p = NULL;
            } else {
                p = q;
            }
        }
    }
    if (p == NULL || ferror(f)) {
        free(p);
        fclose(f);
        return -1;
    }
    fclose(f);
    *data = p;
    *n = len;
    return 0;
}

static int write_file(const char *path, const uint8_t *d, size_t n)
{
    FILE *f = ico_fopen(path, "wb");
    int ok;

    if (f == NULL) {
        return -1;
    }
    ok = fwrite(d, 1, n, f) == n && ico_fsync(f) == 0;
    ok = fclose(f) == 0 && ok;
    return ok ? 0 : -1;
}

static int path_join(char *out, size_t size, const char *a, const char *b)
{
    const int n = snprintf(out, size, "%s/%s", a, b);

    return n < 0 || (size_t)n >= size ? -1 : 0;
}

static int find_root(const IcoSteamEnv *env, char *out, size_t size)
{
    for (int i = 0; i < 6; i++) {
        char ud[ICO_STEAM_PATH_MAX + 16];

        if (env->roots[i][0] != '\0' && path_join(ud, sizeof(ud), env->roots[i], "userdata") == 0 &&
            ico_path_kind(ud, NULL, NULL) == 1) {
            snprintf(out, size, "%s", env->roots[i]);
            return 0;
        }
    }
    return -1;
}

/* userdata/<id> of the most recent user whose folder exists */
static int find_user_dir(const char *root, char *out, size_t size)
{
    char lp[ICO_STEAM_PATH_MAX + 32];
    uint8_t *txt;
    size_t n;
    uint32_t ids[16];
    int count;

    if (path_join(lp, sizeof(lp), root, "config/loginusers.vdf") != 0 ||
        read_all(lp, &txt, &n) != 0) {
        return -1;
    }
    count = ico_steam_login_users((const char *)txt, n, ids, 16);
    free(txt);
    for (int i = 0; i < count; i++) {
        char rel[48], dir[ICO_STEAM_PATH_MAX * 2];

        snprintf(rel, sizeof(rel), "userdata/%u", (unsigned)ids[i]);
        if (path_join(dir, sizeof(dir), root, rel) == 0 && ico_path_kind(dir, NULL, NULL) == 1) {
            snprintf(out, size, "%s", dir);
            return 0;
        }
    }
    return -1;
}

int ico_steam_add_shortcut(const IcoSteamEnv *env, const IcoSteamEntry *e)
{
    char root[ICO_STEAM_PATH_MAX], user[ICO_STEAM_PATH_MAX * 2];
    char cfg[ICO_STEAM_PATH_MAX * 2 + 16], vdf[ICO_STEAM_PATH_MAX * 2 + 40];
    char bak[ICO_STEAM_PATH_MAX * 2 + 48], tmp[ICO_STEAM_PATH_MAX * 2 + 48];
    uint8_t *old = NULL, *neu = NULL;
    size_t oldN = 0, neuN = 0;
    int r, res = ICO_STEAM_FAILED;

    if (find_root(env, root, sizeof(root)) != 0) {
        return ICO_STEAM_NOT_FOUND;
    }
    if (ico_steam_running(env)) {
        return ICO_STEAM_RUNNING;
    }
    if (find_user_dir(root, user, sizeof(user)) != 0) {
        return ICO_STEAM_NO_USER;
    }
    if (path_join(cfg, sizeof(cfg), user, "config") != 0 ||
        path_join(vdf, sizeof(vdf), cfg, "shortcuts.vdf") != 0) {
        return ICO_STEAM_FAILED;
    }
    snprintf(bak, sizeof(bak), "%s.bak", vdf);
    snprintf(tmp, sizeof(tmp), "%s.tmp", vdf);

    r = read_all(vdf, &old, &oldN);
    if (r < 0) {
        return ICO_STEAM_FAILED;
    }
    {
        const int has = ico_steam_vdf_find_exe(old, oldN, e->exe, env->caselessPaths);

        if (has < 0) {
            goto done; /* not a file this reader understands: leave it alone */
        }
        if (has == 1) {
            res = ICO_STEAM_EXISTS;
            goto done;
        }
    }
    if (ico_steam_vdf_append(old, oldN, e, &neu, &neuN) != 0) {
        goto done;
    }
    if (ico_mkdir(cfg) != 0 && errno != EEXIST) {
        goto done;
    }
    if (r == 0 && write_file(bak, old, oldN) != 0) {
        goto done;
    }
    if (write_file(tmp, neu, neuN) != 0 || ico_rename_replace(tmp, vdf) != 0) {
        ico_remove(tmp);
        goto done;
    }
    res = ICO_STEAM_ADDED;
done:
    free(old);
    free(neu);
    return res;
}

/* --- this machine ------------------------------------------------------------- */

#ifdef _WIN32
static int reg_read_string(const wchar_t *sub, const wchar_t *name, char *out, size_t size)
{
    HKEY k;
    wchar_t w[ICO_STEAM_PATH_MAX];
    DWORD type = 0, len = sizeof(w) - sizeof(wchar_t);
    LONG rc;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return -1;
    }
    memset(w, 0, sizeof(w));
    rc = RegQueryValueExW(k, name, NULL, &type, (LPBYTE)w, &len);
    RegCloseKey(k);
    if (rc != ERROR_SUCCESS || type != REG_SZ || w[0] == 0) {
        return -1;
    }
    return ico_narrow(w, out, size);
}

static int win_active_pid(long *pid)
{
    HKEY k;
    DWORD v = 0, type = 0, len = sizeof(v);
    LONG rc;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", 0, KEY_READ,
                      &k) != ERROR_SUCCESS) {
        return 0;
    }
    rc = RegQueryValueExW(k, L"pid", NULL, &type, (LPBYTE)&v, &len);
    RegCloseKey(k);
    if (rc != ERROR_SUCCESS || type != REG_DWORD || v == 0) {
        return 0;
    }
    *pid = (long)v;
    return 1;
}

static int win_steam_exe_running(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe;
    int found = 0;

    if (snap == INVALID_HANDLE_VALUE) {
        return 0;
    }
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"steam.exe") == 0) {
            found = 1;
            break;
        }
    }
    CloseHandle(snap);
    return found;
}
#endif

void ico_steam_env_default(IcoSteamEnv *env)
{
    memset(env, 0, sizeof(*env));
#ifdef _WIN32
    if (reg_read_string(L"Software\\Valve\\Steam", L"SteamPath", env->roots[0],
                        sizeof(env->roots[0])) != 0) {
        env->roots[0][0] = '\0';
    }
    snprintf(env->roots[1], sizeof(env->roots[1]), "%s", "C:/Program Files (x86)/Steam");
    env->activeProcessPid = win_active_pid;
    env->processRunning = win_steam_exe_running;
    env->iconIsExe = 1;
    env->caselessPaths = 1;
#else
    const char *home = getenv("HOME");

    if (home != NULL && home[0] != '\0') {
        snprintf(env->roots[0], sizeof(env->roots[0]), "%s/.steam/steam", home);
        snprintf(env->roots[1], sizeof(env->roots[1]), "%s/.local/share/Steam", home);
        snprintf(env->roots[2], sizeof(env->roots[2]),
                 "%s/.var/app/com.valvesoftware.Steam/.local/share/Steam", home);
        snprintf(env->pidFiles[0], sizeof(env->pidFiles[0]), "%s/.steam/steam.pid", home);
        snprintf(env->pidFiles[1], sizeof(env->pidFiles[1]),
                 "%s/.var/app/com.valvesoftware.Steam/.steam/steam.pid", home);
    }
#endif
}

int ico_steam_self_exe(char *out, size_t size)
{
#ifdef _WIN32
    wchar_t w[ICO_STEAM_PATH_MAX];
    const DWORD n = GetModuleFileNameW(NULL, w, (DWORD)(sizeof(w) / sizeof(w[0])));

    if (n == 0 || n >= sizeof(w) / sizeof(w[0]) || ico_narrow(w, out, size) != 0) {
        return -1;
    }
    for (char *c = out; *c != '\0'; c++) {
        if (*c == '/') {
            *c = '\\';
        }
    }
    return 0;
#else
    /* run from an AppImage: the AppImage file, which Steam can start again,
       not the program inside its mount (gone once the game ends) */
    if (ico_appimage_file(out, size, NULL)) {
        return 0;
    }
    const long n = (long)readlink("/proc/self/exe", out, size - 1);

    if (n <= 0) {
        return -1;
    }
    out[n] = '\0';
    return 0;
#endif
}

int ico_steam_add_this_game(void)
{
    IcoSteamEnv env;
    IcoSteamEntry e;
    char exe[ICO_STEAM_PATH_MAX], dir[ICO_STEAM_PATH_MAX], *slash, *bs;

    if (ico_steam_self_exe(exe, sizeof(exe)) != 0) {
        return ICO_STEAM_FAILED;
    }
    snprintf(dir, sizeof(dir), "%s", exe);
    slash = strrchr(dir, '/');
    bs = strrchr(dir, '\\');
    if (bs != NULL && (slash == NULL || bs > slash)) {
        slash = bs;
    }
    if (slash == NULL) {
        return ICO_STEAM_FAILED;
    }
    if (slash == dir) {
        slash++;
    }
    *slash = '\0';
    ico_steam_env_default(&env);
    e.name = "ICO";
    e.exe = exe;
    e.startDir = dir;
    e.icon = env.iconIsExe ? exe : "";
    const int r = ico_steam_add_shortcut(&env, &e);
    /* one line per press: which program the entry starts, and the answer */
    fprintf(stderr, "ico_pc: Add to Steam: %s (start folder %s): %s\n", exe, dir,
            r == ICO_STEAM_ADDED       ? "added"
            : r == ICO_STEAM_EXISTS    ? "already there"
            : r == ICO_STEAM_RUNNING   ? "Steam is running, nothing written"
            : r == ICO_STEAM_NOT_FOUND ? "no Steam folder"
            : r == ICO_STEAM_NO_USER   ? "no Steam user"
                                       : "failed");
    return r;
}
