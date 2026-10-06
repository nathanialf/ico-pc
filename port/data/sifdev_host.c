/*
 * port/data/sifdev_host.c
 *
 * sifdev's file calls (sceOpen, sceClose, sceRead, sceWrite, sceLseek) on
 * the host, for the development kit's host0: device.  The development
 * build's tools read and wrote their files on the PC the kit hung off
 * (way_tool.c's way data, camera-editor.c's camera sets, effectTool.c's
 * particle files, GsBase.c's stage settings, debug.c's option table, start
 * stage and snapshots).  Here a host0: path is a file under <pref>/dev/
 * (ico_host_pref_dir):
 *
 *   host0:object/stagesetting/st04a.ssb  ->  <pref>/dev/object/stagesetting/st04a.ssb
 *
 * Back slashes become slashes, a leading "./", "/" or "\" and an ISO ";1"
 * version suffix are dropped, and a path with a ".." component is refused.
 * Opening with SCE_CREAT makes the missing folders.  Any other device
 * (cdrom0:, the retail build's debugSceOpen) fails with -1 as it did before
 * this file (the disc is read through libcdvd, port/data/cdvd_host.c).
 *
 * Descriptors are small numbers from 0, the first free one: the tools that
 * write to descriptor 0 without keeping the one sceOpen returned
 * (debug.c's debug_SaveStartStageFile, camera-editor.c's close) relied on
 * the kit handing out 0 first.
 */
#include <sifdev.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32

#include <io.h>

#else

#include <unistd.h>

#endif

#include "host_config.h"
#include "host_fs.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

/* libkernl's sifdev.h open flags */
enum {
    SCE_RDONLY = 0x0001,
    SCE_WRONLY = 0x0002,
    SCE_RDWR = 0x0003,
    SCE_APPEND = 0x0100,
    SCE_CREAT = 0x0200,
    SCE_TRUNC = 0x0400,
    SCE_EXCL = 0x0800
};

#define HOST0_FILES 16

static int host0Fd[HOST0_FILES] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

static char host0Root[1024]; /* tests: ico_host0_set_root */

void ico_host0_set_root(const char *dir)
{
    if (dir == NULL) {
        host0Root[0] = '\0';
    } else {
        snprintf(host0Root, sizeof(host0Root), "%s", dir);
    }
}

/* open(2) on a UTF-8 path (the wide call on Windows, as host_fs.h's
   helpers); the CRT there takes only _S_IREAD | _S_IWRITE as the mode */
static int hostOpen(const char *path, int oflags)
{
#ifdef _WIN32
    wchar_t *wp = ico_widen(path);
    int fd = wp != NULL ? _wopen(wp, oflags, _S_IREAD | _S_IWRITE)
                        : _open(path, oflags, _S_IREAD | _S_IWRITE);

    free(wp);
    return fd;
#else
    return open(path, oflags, 0666);
#endif
}

/* mkdir -p of the folders above the file at path */
static void makeParents(char *path)
{
    char *p;

    for (p = path + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            if (ico_mkdir(path) != 0 && errno != EEXIST) {
                *p = '/';
                return;
            }
            *p = '/';
        }
    }
}

int ico_host0_path(const char *name, char *out, size_t size, int makeDirs)
{
    char rel[512];
    const char *s = name;
    size_t n;
    size_t i;
    int r;

    if (name == NULL || strncmp(name, "host0:", 6) != 0) {
        return -1;
    }
    s += 6;
    while (*s == '/' || *s == '\\' || (s[0] == '.' && (s[1] == '/' || s[1] == '\\'))) {
        s += *s == '.' ? 2 : 1;
    }
    n = strlen(s);
    if (n == 0 || n >= sizeof(rel)) {
        return -1;
    }
    memcpy(rel, s, n + 1);
    if (n >= 2 && rel[n - 2] == ';' && rel[n - 1] >= '0' && rel[n - 1] <= '9') {
        rel[n - 2] = '\0';
    }
    for (i = 0; rel[i] != '\0'; i++) {
        if (rel[i] == '\\') {
            rel[i] = '/';
        }
    }
    /* no ".." component: the tools' names never climb out of the folder */
    for (i = 0; rel[i] != '\0'; i++) {
        if ((i == 0 || rel[i - 1] == '/') && rel[i] == '.' && rel[i + 1] == '.' &&
            (rel[i + 2] == '/' || rel[i + 2] == '\0')) {
            return -1;
        }
    }
    if (host0Root[0] != '\0') {
        r = snprintf(out, size, "%s/%s", host0Root, rel);
    } else {
        char pref[1024];

        ico_host_pref_dir(pref, sizeof(pref));
        r = snprintf(out, size, "%s/dev/%s", pref, rel);
    }
    if (r < 0 || (size_t)r >= size) {
        return -1;
    }
    if (makeDirs) {
        makeParents(out);
    }
    return 0;
}

int sceOpen(unsigned char *name, int flags, ...)
{
    char path[1100];
    int oflags = O_BINARY;
    int fd;
    int i;

    if (ico_host0_path((const char *)name, path, sizeof(path), (flags & SCE_CREAT) != 0) != 0) {
        return -1;
    }
    switch (flags & 3) {
    case SCE_WRONLY:
        oflags |= O_WRONLY;
        break;
    case SCE_RDWR:
        oflags |= O_RDWR;
        break;
    default:
        oflags |= O_RDONLY;
        break;
    }
    if (flags & SCE_APPEND) {
        oflags |= O_APPEND;
    }
    if (flags & SCE_CREAT) {
        oflags |= O_CREAT;
    }
    if (flags & SCE_TRUNC) {
        oflags |= O_TRUNC;
    }
    if (flags & SCE_EXCL) {
        oflags |= O_EXCL;
    }
    for (i = 0; i < HOST0_FILES && host0Fd[i] >= 0; i++) {}
    if (i == HOST0_FILES) {
        return -1;
    }
    fd = hostOpen(path, oflags);
    if (fd < 0) {
        return -1;
    }
    host0Fd[i] = fd;
    return i;
}

static int osFd(long fd)
{
    return fd >= 0 && fd < HOST0_FILES ? host0Fd[fd] : -1;
}

int sceClose(unsigned int fd)
{
    int h = osFd((long)fd);

    if (h < 0) {
        return -1;
    }
    host0Fd[fd] = -1;
    return close(h) == 0 ? 0 : -1;
}

int sceRead(int fd, void *buf, int nbyte)
{
    int h = osFd(fd);

    if (h < 0 || nbyte < 0) {
        return -1;
    }
    return (int)read(h, buf, (unsigned int)nbyte);
}

int sceWrite(int fd, void *buf, int nbyte)
{
    int h = osFd(fd);

    if (h < 0 || nbyte < 0) {
        return -1;
    }
    return (int)write(h, buf, (unsigned int)nbyte);
}

int sceLseek(unsigned int fd, int offset, int whence)
{
    int h = osFd((long)fd);

    if (h < 0 || whence < 0 || whence > 2) {
        return -1;
    }
    /* SCE_SEEK_SET/CUR/END are 0/1/2, as SEEK_* */
    return (int)lseek(h, offset, whence);
}
