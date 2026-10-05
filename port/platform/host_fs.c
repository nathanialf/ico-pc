/*
 * port/platform/host_fs.c
 *
 * Host file calls on UTF-8 paths (host_fs.h).
 */
#include "host_fs.h"
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <direct.h>

wchar_t *ico_widen(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    wchar_t *w;

    if (n <= 0) {
        return NULL;
    }
    w = malloc((size_t)n * sizeof(*w));
    if (w != NULL && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n) != n) {
        free(w);
        w = NULL;
    }
    return w;
}

int ico_narrow(const wchar_t *s, char *out, size_t size)
{
    int n;

    if (size == 0 || size > 0x7FFFFFFF) {
        return -1;
    }
    n = WideCharToMultiByte(CP_UTF8, 0, s, -1, out, (int)size, NULL, NULL);
    if (n <= 0) {
        out[0] = '\0';
        return -1;
    }
    return 0;
}

FILE *ico_fopen(const char *path, const char *mode)
{
    wchar_t *wp = ico_widen(path);
    wchar_t *wm = ico_widen(mode);
    FILE *fp = NULL;

    if (wp != NULL && wm != NULL) {
        fp = _wfopen(wp, wm);
    } else {
        fp = fopen(path, mode);
    }
    free(wp);
    free(wm);
    return fp;
}

int ico_mkdir(const char *path)
{
    wchar_t *wp = ico_widen(path);
    int r = wp != NULL ? _wmkdir(wp) : _mkdir(path);

    free(wp);
    return r;
}

int ico_rmdir(const char *path)
{
    wchar_t *wp = ico_widen(path);
    int r = wp != NULL ? _wrmdir(wp) : _rmdir(path);

    free(wp);
    return r;
}

int ico_remove(const char *path)
{
    wchar_t *wp = ico_widen(path);
    int r = wp != NULL ? _wremove(wp) : remove(path);

    free(wp);
    return r;
}

int ico_rename_replace(const char *from, const char *to)
{
    wchar_t *wf = ico_widen(from);
    wchar_t *wt = ico_widen(to);
    int ok;

    if (wf != NULL && wt != NULL) {
        ok = MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    } else {
        ok = MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    }
    free(wf);
    free(wt);
    return ok ? 0 : -1;
}

int ico_path_kind(const char *path, unsigned long long *size, long long *mtime)
{
    wchar_t *wp = ico_widen(path);
    struct _stat64 st;
    int r = wp != NULL ? _wstat64(wp, &st) : _stat64(path, &st);

    free(wp);
    if (r != 0) {
        return -1;
    }
    if (size != NULL) {
        *size = (unsigned long long)st.st_size;
    }
    if (mtime != NULL) {
        *mtime = (long long)st.st_mtime;
    }
    return (st.st_mode & _S_IFDIR) != 0 ? 1 : 0;
}

#else

#include <unistd.h>

FILE *ico_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

int ico_mkdir(const char *path)
{
    return mkdir(path, 0777);
}

int ico_rmdir(const char *path)
{
    return rmdir(path);
}

int ico_remove(const char *path)
{
    return remove(path);
}

int ico_rename_replace(const char *from, const char *to)
{
    return rename(from, to);
}

int ico_path_kind(const char *path, unsigned long long *size, long long *mtime)
{
    struct stat st;

    if (stat(path, &st) != 0) {
        return -1;
    }
    if (size != NULL) {
        *size = (unsigned long long)st.st_size;
    }
    if (mtime != NULL) {
        *mtime = (long long)st.st_mtime;
    }
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

#endif
