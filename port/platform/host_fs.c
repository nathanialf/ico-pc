/*
 * port/platform/host_fs.c
 *
 * Host file calls on UTF-8 paths (host_fs.h).
 */
#include "host_fs.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <direct.h>
#include <io.h>

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

int ico_fsync(FILE *f)
{
    if (fflush(f) != 0) {
        return -1;
    }
    return _commit(_fileno(f)) == 0 ? 0 : -1;
}

/* GetLastError() as an errno value, for the codes a move over a file meets */
static int ico_errno_from_win32(DWORD e)
{
    switch (e) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return ENOENT;
    case ERROR_ACCESS_DENIED:
    case ERROR_WRITE_PROTECT:
        return EACCES;
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS:
        return EEXIST;
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
        return EBUSY;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return ENOMEM;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
        return ENOSPC;
    default:
        return EIO;
    }
}

int ico_rename_replace(const char *from, const char *to)
{
    wchar_t *wf = ico_widen(from);
    wchar_t *wt = ico_widen(to);
    int ok;
    DWORD err = 0;

    if (wf != NULL && wt != NULL) {
        ok = MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    } else {
        ok = MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    }
    if (!ok) {
        err = GetLastError();
    }
    free(wf);
    free(wt);
    if (!ok) {
        errno = ico_errno_from_win32(err);
        return -1;
    }
    return 0;
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

#include <fcntl.h>
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

/* the folder holding path, synced so the new name is on the disk */
static void sync_parent(const char *path)
{
    char dir[4096];
    const char *slash = strrchr(path, '/');
    size_t n = slash != NULL ? (size_t)(slash - path) : 0;
    int fd;

    if (slash == NULL) {
        strcpy(dir, ".");
    } else if (n == 0) {
        strcpy(dir, "/");
    } else if (n < sizeof(dir)) {
        memcpy(dir, path, n);
        dir[n] = '\0';
    } else {
        return;
    }
    fd = open(dir, O_RDONLY);
    if (fd >= 0) {
        (void)fsync(fd);
        close(fd);
    }
}

int ico_rename_replace(const char *from, const char *to)
{
    if (rename(from, to) != 0) {
        return -1;
    }
    sync_parent(to);
    return 0;
}

int ico_fsync(FILE *f)
{
    if (fflush(f) != 0) {
        return -1;
    }
    return fsync(fileno(f)) == 0 ? 0 : -1;
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

/* --- ico_dir_walk ------------------------------------------------------- */

#include <dirent.h>

static int walk_name_cmp(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* The entries of dir except the hidden ones (".", "..", ".DS_Store"), as
   malloc'd UTF-8 names in *out, sorted: the count, or -1 when dir cannot
   be read. */
static int walk_list(const char *dir, char ***out)
{
#ifdef _WIN32
    wchar_t *wp = ico_widen(dir);
    _WDIR *d = wp != NULL ? _wopendir(wp) : NULL;
    struct _wdirent *e;
#else
    DIR *d = opendir(dir);
    struct dirent *e;
#endif
    char **names = NULL;
    int n = 0, cap = 0;

#ifdef _WIN32
    free(wp);
#endif
    *out = NULL;
    if (d == NULL) {
        return -1;
    }
    for (;;) {
#ifdef _WIN32
        char name[1024];

        e = _wreaddir(d);
        if (e == NULL) {
            break;
        }
        if (ico_narrow(e->d_name, name, sizeof(name)) != 0) {
            continue;
        }
#else
        const char *name;

        e = readdir(d);
        if (e == NULL) {
            break;
        }
        name = e->d_name;
#endif
        if (name[0] == '.') {
            continue;
        }
        if (n == cap) {
            int ncap = cap != 0 ? cap * 2 : 64;
            char **g = realloc(names, (size_t)ncap * sizeof(*names));

            if (g == NULL) {
                break;
            }
            names = g;
            cap = ncap;
        }
        names[n] = malloc(strlen(name) + 1);
        if (names[n] == NULL) {
            break;
        }
        strcpy(names[n], name);
        n++;
    }
#ifdef _WIN32
    _wclosedir(d);
#else
    closedir(d);
#endif
    if (n > 1) {
        qsort(names, (size_t)n, sizeof(*names), walk_name_cmp);
    }
    *out = names;
    return n;
}

/* 1 when path is a symbolic link (on Windows, a symbolic link or a
   junction; not the other reparse points, such as OneDrive's folders)
   rather than a folder of its own */
static int walk_is_link(const char *path)
{
#ifdef _WIN32
    wchar_t *wp = ico_widen(path);
    WIN32_FIND_DATAW fd;
    HANDLE h = wp != NULL ? FindFirstFileW(wp, &fd) : INVALID_HANDLE_VALUE;
    int link = 0;

    free(wp);
    if (h != INVALID_HANDLE_VALUE) {
        link = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
               (fd.dwReserved0 == IO_REPARSE_TAG_SYMLINK ||
                fd.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT);
        FindClose(h);
    }
    return link;
#else
    struct stat st;

    return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
#endif
}

static int walk_dir(const char *dir, int depth, IcoDirWalkFn fn, void *user, int *files)
{
    char **names;
    int n = walk_list(dir, &names);
    int stop = 0;

    if (n < 0) {
        return -1;
    }
    for (int i = 0; i < n; i++) {
        size_t len = strlen(dir) + 1 + strlen(names[i]) + 1;
        char *path = stop ? NULL : malloc(len);

        if (path != NULL) {
            int kind;

            snprintf(path, len, "%s/%s", dir, names[i]);
            kind = ico_path_kind(path, NULL, NULL);
            if (kind == 0) {
                (*files)++;
                stop = fn(path, names[i], user) != 0;
            } else if (kind == 1 && depth > 0 && !walk_is_link(path)) {
                /* a folder link is not followed: one pointing back up would
                   repeat the walk below it, twice for two such links, and so
                   on at every level down */
                stop = walk_dir(path, depth - 1, fn, user, files) == 1;
            }
            free(path);
        }
        free(names[i]);
    }
    free(names);
    return stop ? 1 : 0;
}

int ico_dir_walk(const char *dir, int depth, IcoDirWalkFn fn, void *user)
{
    int files = 0;

    if (dir == NULL || dir[0] == '\0' || fn == NULL || walk_dir(dir, depth, fn, user, &files) < 0) {
        return -1;
    }
    return files;
}
