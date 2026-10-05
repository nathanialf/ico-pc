/*
 * port/platform/host_config.c
 *
 * The executable's folder, ico-pc.ini, SHA-1, the file dialog and the error
 * box (host_config.h). Windows needs user32 (MessageBoxA) and comdlg32
 * (GetOpenFileNameA).
 */
#include "host_config.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    char buf[ICO_PATH_MAX];
    char *slash;
    long n;

#ifdef _WIN32
    n = (long)GetModuleFileNameA(NULL, buf, (DWORD)sizeof(buf));
    if (n <= 0 || n >= (long)sizeof(buf)) {
        copy(out, size, ".");
        return -1;
    }
    buf[n] = '\0';
    slash = strrchr(buf, '\\');
    if (strrchr(buf, '/') > slash) {
        slash = strrchr(buf, '/');
    }
#else
    n = (long)readlink("/proc/self/exe", buf, sizeof(buf) - 1);
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

void ico_path_join(char *out, size_t size, const char *dir, const char *name)
{
    size_t n;

    if (ico_path_is_absolute(name) || dir == NULL || dir[0] == '\0') {
        copy(out, size, name);
        return;
    }
    n = strlen(dir);
    if (n > 0 && (dir[n - 1] == '/' || dir[n - 1] == '\\')) {
        snprintf(out, size, "%s%s", dir, name);
    } else {
        snprintf(out, size, "%s%c%s", dir, SEP, name);
    }
}

int ico_file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

int ico_make_dir(const char *path)
{
#ifdef _WIN32
    if (_mkdir(path) == 0 || errno == EEXIST) {
        return 0;
    }
#else
    if (mkdir(path, 0777) == 0 || errno == EEXIST) {
        return 0;
    }
#endif
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
    FILE *f = fopen(path, "rb");
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

int ico_ini_load(IcoIni *ini, const char *path)
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

int ico_ini_store(const char *path, const char *key, const char *value)
{
    char *text = read_text(path);
    const char *p = text ? text : "";
    FILE *f;
    int done = 0;

    f = fopen(path, "wb");
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
    return fclose(f) == 0 ? 0 : -1;
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

    FILE *f = fopen(path, "rb");
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
    FILE *probe = fopen(log_path, "w");

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
    /* the probe emptied the file; both streams append, unbuffered, so their
       lines interleave in order instead of overwriting each other */
    if (freopen(log_path, "a", stdout) == NULL) {
        return -1;
    }
    if (freopen(log_path, "a", stderr) == NULL) {
        return -1;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    return 0;
}

/* --- the dialog and the error box --------------------------------------- */

int ico_host_pick_iso(char *out, size_t size)
{
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char path[ICO_PATH_MAX] = "";

    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "ICO disc image (*.iso)\0*.iso\0All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = (DWORD)sizeof(path);
    ofn.lpstrTitle = "Choose your ICO (PAL, SCES-50760) disc image";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameA(&ofn)) {
        return -1;
    }
    copy(out, size, path);
    return 0;
#else
    (void)out;
    (void)size;
    return -1;
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
#ifdef _WIN32
    {
        char box[3072];

        snprintf(box, sizeof(box), "%s\n\nLog: %s", msg, log_path ? log_path : "(none)");
        MessageBoxA(NULL, box, "ICO PC", MB_OK | MB_ICONERROR);
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
