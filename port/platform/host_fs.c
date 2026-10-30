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
#include <stdio.h>
#if !defined(_WIN32)
#if defined(__APPLE__)
#include <sys/mount.h>
#include <sys/param.h>
#else
#include <sys/vfs.h>
#endif
#endif

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

/* --- ico_appimage_file -------------------------------------------------- */

int ico_appimage_file(char *out, size_t size, const char **why)
{
#if defined(__linux__) && !defined(__ANDROID__)
    const char *file = getenv("APPIMAGE");
    const char *appdir = getenv("APPDIR");
    const char *no = NULL;
    char exe[4096], dir[4096];
    ssize_t len = 0;
    size_t n = 0;

    if (size > 0) {
        out[0] = '\0';
    }
    if (why != NULL) {
        *why = NULL;
    }
    if (file == NULL || file[0] == '\0') {
        return 0;
    }
    if (file[0] != '/') {
        no = "APPIMAGE is not a full path";
    } else if (appdir == NULL || appdir[0] == '\0') {
        no = "APPDIR is not set";
    } else if ((len = readlink("/proc/self/exe", exe, sizeof(exe) - 1)) <= 0) {
        no = "the program's own path cannot be read";
    } else if (realpath(appdir, dir) == NULL) {
        no = "APPDIR does not exist";
    } else if ((n = strlen(dir)) <= 1) {
        no = "APPDIR is the root folder";
    } else if ((exe[len] = '\0', strncmp(exe, dir, n) != 0 || exe[n] != '/')) {
        no = "the program does not run from inside APPDIR";
    } else if (ico_path_kind(file, NULL, NULL) != 0) {
        no = "APPIMAGE is not a file";
    } else if (strlen(file) >= size) {
        no = "APPIMAGE's path is too long";
    }
    if (no != NULL) {
        if (why != NULL) {
            *why = no;
        }
        return 0;
    }
    memcpy(out, file, strlen(file) + 1);
    return 1;
#else
    (void)why;
    if (size > 0) {
        out[0] = '\0';
    }
    return 0;
#endif
}

/* --- ico_dir_walk ------------------------------------------------------- */

#include <dirent.h>

static int walk_name_cmp(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* The entries of dir except the hidden ones (a leading '.'), as
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

/* ---- the drive a path sits on ---- */

static int drive_word(char *out, size_t n, const char *w)
{
    if (n == 0) {
        return -1;
    }
    if (strlen(w) >= n) {
        out[0] = '\0';
        return -1;
    }
    strcpy(out, w);
    return 0;
}

int ico_host_drive_kind(const char *path, char *out, size_t n)
{
    if (out == NULL || n == 0) {
        return -1;
    }
    out[0] = '\0';
    if (path == NULL || path[0] == '\0') {
        return drive_word(out, n, "unknown");
    }
#ifdef _WIN32
    {
        wchar_t *wp = ico_widen(path);
        wchar_t full[MAX_PATH * 2];
        wchar_t root[4];
        const char *w = "unknown";
        DWORD got;

        if (wp == NULL) {
            return drive_word(out, n, "unknown");
        }
        got = GetFullPathNameW(wp, (DWORD)(sizeof(full) / sizeof(full[0])), full, NULL);
        free(wp);
        if (got == 0 || got >= sizeof(full) / sizeof(full[0])) {
            return drive_word(out, n, "unknown");
        }
        if (full[0] == L'\\' && full[1] == L'\\' && full[2] != L'?' && full[2] != L'.') {
            w = "network"; /* \\server\share */
        } else if (full[0] == L'\\' && full[1] == L'\\' && full[2] == L'?' && full[3] == L'\\' &&
                   (full[4] == L'U' || full[4] == L'u') && (full[5] == L'N' || full[5] == L'n') &&
                   (full[6] == L'C' || full[6] == L'c') && full[7] == L'\\') {
            w = "network"; /* \\?\UNC\server\share */
        } else {
            const wchar_t *d = full;

            if (full[0] == L'\\' && full[1] == L'\\' && (full[2] == L'?' || full[2] == L'.') &&
                full[3] == L'\\') {
                d = full + 4;
            }
            if (d[0] != L'\0' && d[1] == L':') {
                root[0] = d[0];
                root[1] = L':';
                root[2] = L'\\';
                root[3] = L'\0';
                switch (GetDriveTypeW(root)) {
                case DRIVE_FIXED:
                    w = "fixed";
                    break;
                case DRIVE_REMOVABLE:
                    w = "removable";
                    break;
                case DRIVE_REMOTE:
                    w = "network";
                    break;
                case DRIVE_RAMDISK:
                    w = "RAM disk";
                    break;
                case DRIVE_CDROM:
                    w = "CD-ROM";
                    break;
                default:
                    break;
                }
            }
        }
        return drive_word(out, n, w);
    }
#else
    {
        struct statfs sf;
        const char *w = "fixed";

        if (statfs(path, &sf) != 0) {
            return drive_word(out, n, "unknown");
        }
#ifdef __APPLE__
        if (strcmp(sf.f_fstypename, "nfs") == 0 || strcmp(sf.f_fstypename, "smbfs") == 0 ||
            strcmp(sf.f_fstypename, "afpfs") == 0 || strcmp(sf.f_fstypename, "webdav") == 0) {
            w = "network";
        }
#else
        switch ((unsigned long)sf.f_type) {
        case 0x6969UL:     /* NFS */
        case 0x517BUL:     /* SMB */
        case 0xFF534D42UL: /* CIFS */
        case 0xFE534D42UL: /* SMB2 */
        case 0x00C36400UL: /* Ceph */
        case 0x5346414FUL: /* AFS */
        case 0x01021997UL: /* 9p */
            w = "network";
            break;
        case 0x65735546UL: /* FUSE (sshfs, ntfs-3g, exfat ...: the medium is not known) */
            w = "fuse";
            break;
        case 0x01021994UL: /* tmpfs */
        case 0x858458F6UL: /* ramfs */
            w = "RAM disk";
            break;
        default:
            break;
        }
#endif
        return drive_word(out, n, w);
    }
#endif
}

static char drive_kinds[2][16] = {"unknown", "unknown"};

static const char *drive_suffix(const char *kind)
{
    if (strcmp(kind, "network") == 0) {
        return " on a network drive";
    }
    if (strcmp(kind, "removable") == 0) {
        return " on a removable drive";
    }
    if (strcmp(kind, "CD-ROM") == 0) {
        return " on a CD or DVD drive";
    }
    return "";
}

const char *ico_host_drive_note(int which, const char *path)
{
    char kind[16];

    if (which < 0 || which > 1) {
        return "";
    }
    if (ico_host_drive_kind(path, kind, sizeof(kind)) != 0) {
        snprintf(kind, sizeof(kind), "unknown");
    }
    snprintf(drive_kinds[which], sizeof(drive_kinds[which]), "%s", kind);
    return drive_suffix(kind);
}

void ico_host_drive_warn(int which)
{
    static const char *const place[2] = {"the game data", "the save folder"};
    const char *note;

    if (which < 0 || which > 1) {
        return;
    }
    note = drive_suffix(drive_kinds[which]);
    if (note[0] != '\0') {
        fprintf(stderr,
                "ico_pc: %s is%s: loads%s can stall and the sound can cut out while they do; a "
                "local drive is faster\n",
                place[which], note, which == ICO_DRIVE_USER ? " and saves" : "");
    }
}

void ico_host_drive_summary(char *out, size_t n)
{
    snprintf(out, n, "drives: data %s, user %s", drive_kinds[ICO_DRIVE_DATA],
             drive_kinds[ICO_DRIVE_USER]);
}
