/*
 * port/platform/desktop_entry.c: see desktop_entry.h.
 */
#include "desktop_entry.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_fs.h"

#define PATH_MAX_ICO 4096

int ico_desktop_entry_text(char *out, size_t size, const char *exe)
{
    char q[PATH_MAX_ICO * 4 + 8];
    size_t n = 0;
    int w;

    if (size > 0) {
        out[0] = '\0';
    }
    if (exe == NULL || exe[0] == '\0' || size == 0) {
        return -1;
    }
    /* two levels, in the order the specification reads them back in
       reverse: the Exec quoting first (inside the double quotes a
       backslash, double quote, backtick or dollar takes a backslash, a
       percent sign is doubled), then the string value's own escaping,
       which doubles every backslash of that result (the reader unescapes
       the string before it applies the quoting rule).  So a literal
       backslash is four, and a dollar is two backslashes and the dollar */
    for (const unsigned char *p = (const unsigned char *)exe; *p != '\0'; p++) {
        const int escape = *p == '\\' || *p == '"' || *p == '`' || *p == '$';

        if (*p < 0x20 || *p == 0x7f || n + 5 >= sizeof(q)) {
            return -1;
        }
        if (escape) {
            q[n++] = '\\'; /* the quoting's backslash, doubled */
            q[n++] = '\\';
        } else if (*p == '%') {
            q[n++] = '%';
        }
        if (*p == '\\') {
            q[n++] = '\\'; /* the literal backslash, doubled */
        }
        q[n++] = (char)*p;
    }
    q[n] = '\0';
    w = snprintf(out, size,
                 "[Desktop Entry]\n"
                 "Type=Application\n"
                 "Name=ICO\n"
                 "Exec=\"%s\"\n"
                 "Icon=" ICO_DESKTOP_APP_ID "\n"
                 "Categories=Game;\n"
                 "Terminal=false\n"
                 "StartupWMClass=" ICO_DESKTOP_APP_ID "\n",
                 q);
    if (w < 0 || (size_t)w >= size) {
        out[0] = '\0';
        return -1;
    }
    return 0;
}

int ico_desktop_entry_data_home(char *out, size_t size)
{
    const char *x = getenv("XDG_DATA_HOME");
    const char *h = getenv("HOME");
    int w;

    if (size == 0) {
        return -1;
    }
    out[0] = '\0';
    if (x != NULL && x[0] == '/') {
        w = snprintf(out, size, "%s", x);
    } else if (h != NULL && h[0] == '/') {
        w = snprintf(out, size, "%s/.local/share", h);
    } else {
        return -1;
    }
    if (w < 0 || (size_t)w >= size) {
        out[0] = '\0';
        return -1;
    }
    return 0;
}

/* one folder: made, or there already */
static int make_dir(const char *path)
{
    return ico_mkdir(path) == 0 || errno == EEXIST ? 0 : -1;
}

/* mkdir -p: every folder of dir (a path with '/' separators) */
static int make_dirs(const char *dir)
{
    char b[PATH_MAX_ICO];
    const size_t n = strlen(dir);

    if (n == 0 || n >= sizeof(b)) {
        return -1;
    }
    memcpy(b, dir, n + 1);
    for (size_t i = 1; i < n; i++) {
        if (b[i] == '/') {
            b[i] = '\0';
            if (make_dir(b) != 0) {
                return -1;
            }
            b[i] = '/';
        }
    }
    return make_dir(b);
}

/* the bytes to path through a .tmp name and a rename; 0, or -1 */
static int write_atomic(const char *path, const void *data, size_t n)
{
    char tmp[PATH_MAX_ICO];
    FILE *f;
    int ok;

    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        return -1;
    }
    f = ico_fopen(tmp, "wb");
    if (f == NULL) {
        return -1;
    }
    ok = (n == 0 || fwrite(data, 1, n, f) == n) && ico_fsync(f) == 0;
    ok = fclose(f) == 0 && ok;
    if (!ok || ico_rename_replace(tmp, path) != 0) {
        ico_remove(tmp);
        return -1;
    }
    return 0;
}

/* the whole file at path (malloc'd), or NULL */
static unsigned char *slurp(const char *path, size_t *n)
{
    FILE *f = ico_fopen(path, "rb");
    unsigned char *buf = NULL;
    size_t cap = 0, len = 0;

    if (f == NULL) {
        return NULL;
    }
    for (;;) {
        unsigned char *nb;
        size_t got;

        if (len == cap) {
            cap = cap ? cap * 2 : 65536;
            if (cap > ((size_t)64 << 20) || (nb = realloc(buf, cap)) == NULL) {
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = nb;
        }
        got = fread(buf + len, 1, cap - len, f);
        len += got;
        if (got == 0) {
            break;
        }
    }
    fclose(f);
    *n = len;
    return buf;
}

int ico_desktop_entry_install(const char *dataHome, const char *exe, const char *pngPath)
{
    char text[PATH_MAX_ICO * 2 + 256];
    char appDir[PATH_MAX_ICO], iconDir[PATH_MAX_ICO], file[PATH_MAX_ICO];
    int haveIcon = pngPath != NULL && pngPath[0] != '\0';
    unsigned char *png = NULL;
    size_t pngN = 0;

    if (dataHome == NULL || dataHome[0] == '\0' ||
        ico_desktop_entry_text(text, sizeof(text), exe)) {
        return -1;
    }
    if (haveIcon) {
        /* read first: a missing picture refuses before anything is written */
        png = slurp(pngPath, &pngN);
        if (png == NULL || pngN == 0) {
            free(png);
            return -1;
        }
    }
    if (snprintf(appDir, sizeof(appDir), "%s/applications", dataHome) >= (int)sizeof(appDir) ||
        snprintf(iconDir, sizeof(iconDir), "%s/icons/hicolor/256x256/apps", dataHome) >=
            (int)sizeof(iconDir)) {
        free(png);
        return -1;
    }
    if (haveIcon) {
        if (make_dirs(iconDir) != 0 ||
            snprintf(file, sizeof(file), "%s/" ICO_DESKTOP_APP_ID ".png", iconDir) >=
                (int)sizeof(file) ||
            write_atomic(file, png, pngN) != 0) {
            free(png);
            return -1;
        }
    }
    free(png);
    if (make_dirs(appDir) != 0 ||
        snprintf(file, sizeof(file), "%s/" ICO_DESKTOP_APP_ID ".desktop", appDir) >=
            (int)sizeof(file) ||
        write_atomic(file, text, strlen(text)) != 0) {
        return -1;
    }
    return 0;
}
