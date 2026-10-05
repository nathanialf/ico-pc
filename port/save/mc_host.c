/*
 * port/save/mc_host.c
 *
 * libmc over a host folder (docs/port/SAVES.md). Port 0 is a formatted 8 MB
 * card whose root is the card folder; port 1 is empty. The files are
 * stored as the card holds them, byte for byte, in the folder layout PCSX2's
 * folder memory cards use: <card>/BESCES-50760ico/{icon.sys, boy_blk.ico,
 * BESCES-50760ico, game.000 ...}. Nothing else is written, and nothing at
 * all before the game's first Mkdir.
 *
 * Asynchrony as port/data/cdvd_host.c: a request does its work when it is
 * issued, and completes at the next simulated vsync, when sceMcSync(1)
 * starts returning it. The vsync also signals fumi/ios/mcard.c's lock
 * semaphore (IosMcLock): iosMcMgrSync waits on it between sceMcSync polls,
 * so each poll happens once per vsync.
 *
 * Result codes and the conventions the game relies on are in libmc.h's
 * sceMcRes* and docs/port/SAVES.md.
 */
#include "mc_host.h"
#include <eekernel.h>
#include <libmc.h>
#include "host_config.h"
#include "host_loop.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32

#include <direct.h>

#define HOST_MKDIR(p) _mkdir(p)
#define HOST_RMDIR(p) _rmdir(p)
#else

#include <unistd.h>

#define HOST_MKDIR(p) mkdir((p), 0777)
#define HOST_RMDIR(p) rmdir(p)
#endif

/* fumi/ios/mcard.c's lock: -1 while no iosMcMgrSync is running */
extern int IosMcLock;

#define MC_PORTS 2
#define MC_HANDLES 3 /* libmc's open file limit */
#define MC_NAME_MAX 31
#define MC_DEPTH 8
#define MC_REL_MAX 256
#define MC_ROOT_MAX 1024
#define MC_PATH_MAX (MC_ROOT_MAX + MC_REL_MAX + 2)
#define MC_DIR_MAX 512
#define MC_ATTR_FILE 0x8497
#define MC_ATTR_DIR 0x8427

typedef struct McHandle {
    FILE *fp;
    int canRead;
    int canWrite;
} McHandle;

static struct {
    int hooked;
    int rootSet;
    char root[MC_ROOT_MAX];
    int changed[MC_PORTS];          /* a "new card": sceMcGetInfo returns -1 once */
    int unformatted[MC_PORTS];      /* after sceMcUnformat, until sceMcFormat */
    char cwd[MC_PORTS][MC_REL_MAX]; /* the current directory, relative to the root */
    int dirNext;                    /* sceMcGetDir's continuation index */
    McHandle handle[MC_HANDLES];
    int active; /* a request awaits its sceMcSync */
    int done;
    int cmd;
    int result;
} mc;

/* --- paths ------------------------------------------------------------------ */

static int valid_name(const char *s, size_t n)
{
    size_t i;

    if (n == 0 || n > MC_NAME_MAX) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || strchr("\\:*?\"<>|", c) != NULL) {
            return 0;
        }
    }
    return 1;
}

/* Resolves name against the port's current directory into rel ("a/b", ""
   for the root). 0, or -1 for a name the card cannot hold. */
static int resolve(int port, const char *name, char *rel, size_t size)
{
    char comps[MC_DEPTH][MC_NAME_MAX + 1];
    int n = 0;
    const char *p;
    size_t pos = 0;
    int pass;
    int i;

    rel[0] = '\0';
    /* pass 0 walks the current directory, pass 1 the name (which starts
       over at the root when it is absolute) */
    for (pass = 0; pass < 2; pass++) {
        p = pass == 0 ? mc.cwd[port] : name;
        if (pass == 1 && name[0] == '/') {
            n = 0;
        }
        while (*p != '\0') {
            const char *e;
            size_t len;

            while (*p == '/') {
                p++;
            }
            e = p;
            while (*e != '\0' && *e != '/') {
                e++;
            }
            len = (size_t)(e - p);
            if (len == 1 && p[0] == '.') {
                /* the same directory */
            } else if (len == 2 && p[0] == '.' && p[1] == '.') {
                if (n > 0) {
                    n--;
                }
            } else if (len > 0) {
                if (!valid_name(p, len) || n == MC_DEPTH) {
                    return -1;
                }
                memcpy(comps[n], p, len);
                comps[n][len] = '\0';
                n++;
            }
            p = e;
        }
    }
    for (i = 0; i < n; i++) {
        size_t len = strlen(comps[i]);
        if (pos + len + 2 > size) {
            return -1;
        }
        if (i > 0) {
            rel[pos++] = '/';
        }
        memcpy(rel + pos, comps[i], len);
        pos += len;
        rel[pos] = '\0';
    }
    return 0;
}

static void host_path(char *out, size_t size, const char *rel)
{
    if (rel[0] != '\0') {
        snprintf(out, size, "%s/%s", mc.root, rel);
    } else {
        snprintf(out, size, "%s", mc.root);
    }
}

/* 1 a directory, 0 a file, -1 neither exists */
static int kind_of(const char *path, unsigned long *size, time_t *mtime)
{
    struct stat st;

    if (stat(path, &st) != 0) {
        return -1;
    }
    if (size != NULL) {
        *size = (unsigned long)st.st_size;
    }
    if (mtime != NULL) {
        *mtime = st.st_mtime;
    }
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

/* Creates the card folder and any missing parent. */
static int make_root(void)
{
    char path[MC_PATH_MAX];
    size_t i;

    snprintf(path, sizeof(path), "%s", mc.root);
    for (i = 1; path[i] != '\0'; i++) {
        if (path[i] == '/' || path[i] == '\\') {
            char c = path[i];
            path[i] = '\0';
            if (kind_of(path, NULL, NULL) < 0) {
                HOST_MKDIR(path);
            }
            path[i] = c;
        }
    }
    if (kind_of(path, NULL, NULL) < 0 && HOST_MKDIR(path) != 0) {
        return -1;
    }
    return kind_of(path, NULL, NULL) == 1 ? 0 : -1;
}

/* --- the card's contents ------------------------------------------------------ */

static int name_cmp(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

/* The entries of a host directory a card could hold, sorted, into names
   (MC_NAME_MAX + 1 bytes each). The count, or -1 if it cannot be read. */
static int list_dir(const char *path, char (*names)[MC_NAME_MAX + 1], int max)
{
    DIR *d = opendir(path);
    struct dirent *e;
    int n = 0;

    if (d == NULL) {
        return -1;
    }
    while ((e = readdir(d)) != NULL && n < max) {
        size_t len = strlen(e->d_name);
        /* hidden files (.DS_Store, ...) are the host's, not the card's */
        if (e->d_name[0] == '.' || !valid_name(e->d_name, len)) {
            continue;
        }
        memcpy(names[n], e->d_name, len + 1);
        n++;
    }
    closedir(d);
    qsort(names, (size_t)n, MC_NAME_MAX + 1, name_cmp);
    return n;
}

/* 1 KB clusters the tree under path takes: a file's size rounded up, a
   directory one cluster */
static long used_clusters(const char *path, int depth)
{
    static char names[MC_DEPTH][MC_DIR_MAX][MC_NAME_MAX + 1];
    long used = 0;
    int n;
    int i;

    if (depth >= MC_DEPTH) {
        return 0;
    }
    n = list_dir(path, names[depth], MC_DIR_MAX);
    for (i = 0; i < n; i++) {
        char sub[MC_PATH_MAX];
        unsigned long size = 0;
        int k;

        snprintf(sub, sizeof(sub), "%s/%s", path, names[depth][i]);
        k = kind_of(sub, &size, NULL);
        if (k == 1) {
            used += 1 + used_clusters(sub, depth + 1);
        } else if (k == 0) {
            used += (long)((size + 1023) / 1024);
        }
    }
    return used;
}

static int wild(const char *pat, const char *s)
{
    if (*pat == '\0') {
        return *s == '\0';
    }
    if (*pat == '*') {
        return wild(pat + 1, s) || (*s != '\0' && wild(pat, s + 1));
    }
    return *s != '\0' && (*pat == '?' || *pat == *s) && wild(pat + 1, s + 1);
}

static void fill_date(sceMcStDateTime *d, time_t t)
{
    struct tm *tm = gmtime(&t);

    memset(d, 0, sizeof(*d));
    if (tm != NULL) {
        d->Sec = (unsigned char)tm->tm_sec;
        d->Min = (unsigned char)tm->tm_min;
        d->Hour = (unsigned char)tm->tm_hour;
        d->Day = (unsigned char)tm->tm_mday;
        d->Month = (unsigned char)(tm->tm_mon + 1);
        d->Year = (unsigned short)(tm->tm_year + 1900);
    }
}

static void fill_entry(sceMcTblGetDir *e, const char *name, const char *path, int kind)
{
    unsigned long size = 0;
    time_t mtime = 0;

    if (path != NULL) {
        kind_of(path, &size, &mtime);
    }
    memset(e, 0, sizeof(*e));
    fill_date(&e->_Create, mtime);
    fill_date(&e->_Modify, mtime);
    e->FileSizeByte = kind == 1 ? 0 : (unsigned int)size;
    e->AttrFile = (unsigned short)(kind == 1 ? MC_ATTR_DIR : MC_ATTR_FILE);
    memcpy(e->EntryName, name, strlen(name));
}

/* --- requests ---------------------------------------------------------------- */

static int slot_ok(int port, int slot)
{
    return port == 0 && slot == 0;
}

static int begin(int cmd, int result)
{
    mc.active = 1;
    mc.done = 0;
    mc.cmd = cmd;
    mc.result = result;
    return 0;
}

static void vsync(void *user)
{
    (void)user;
    if (mc.active) {
        mc.done = 1;
    }
    if (IosMcLock >= 0) {
        iSignalSema(IosMcLock);
    }
}

void ico_mc_host_set_root(const char *dir)
{
    snprintf(mc.root, sizeof(mc.root), "%s", dir);
    mc.rootSet = 1;
}

const char *ico_mc_host_root(void)
{
    return mc.root;
}

int ico_mc_host_pending(void)
{
    return mc.active;
}

int sceMcInit(void)
{
    int i;

    for (i = 0; i < MC_HANDLES; i++) {
        if (mc.handle[i].fp != NULL) {
            fclose(mc.handle[i].fp);
        }
        memset(&mc.handle[i], 0, sizeof(mc.handle[i]));
    }
    for (i = 0; i < MC_PORTS; i++) {
        mc.changed[i] = 1;
        mc.unformatted[i] = 0;
        mc.cwd[i][0] = '\0';
    }
    mc.active = 0;
    mc.dirNext = 0;
    if (!mc.rootSet) {
        ico_host_saves_dir(mc.root, sizeof(mc.root));
        mc.rootSet = 1;
    }
    if (!mc.hooked && ico_host_on_vsync_register(vsync, NULL) == 0) {
        mc.hooked = 1;
    }
    return 0;
}

int sceMcSync(int mode, int *cmd, int *result)
{
    /* mode 0 waits for the request: it is done at once */
    if (!mc.active) {
        return -1;
    }
    if (!mc.done && mode != 0) {
        return 0;
    }
    if (cmd != NULL) {
        *cmd = mc.cmd;
    }
    if (result != NULL) {
        *result = mc.result;
    }
    mc.active = 0;
    return 1;
}

int sceMcGetInfo(int port, int slot, int *type, int *free, int *format)
{
    int result = 0;
    long used;
    long left;

    if (type != NULL) {
        *type = 0;
    }
    if (free != NULL) {
        *free = 0;
    }
    if (format != NULL) {
        *format = 0;
    }
    if (!slot_ok(port, slot)) {
        return begin(sceMcFuncNoCardInfo, sceMcResFailDetect2);
    }
    if (type != NULL) {
        *type = 2;
    }
    if (mc.unformatted[port]) {
        result = sceMcResNoFormat;
    } else {
        used = used_clusters(mc.root, 0);
        left = ICO_MC_HOST_CLUSTERS - used;
        if (free != NULL) {
            *free = left > 0 ? (int)left : 0;
        }
        if (format != NULL) {
            *format = 1;
        }
        if (mc.changed[port]) {
            result = sceMcResChangedCard;
        }
    }
    mc.changed[port] = 0;
    return begin(sceMcFuncNoCardInfo, result);
}

int sceMcFormat(int port, int slot)
{
    if (!slot_ok(port, slot)) {
        return begin(sceMcFuncNoFormat, sceMcResFailDetect);
    }
    /* the folder is never wiped: the card folder may hold other games' saves */
    mc.unformatted[port] = 0;
    mc.cwd[port][0] = '\0';
    return begin(sceMcFuncNoFormat, 0);
}

int sceMcUnformat(int port, int slot)
{
    if (!slot_ok(port, slot)) {
        return begin(sceMcFuncNoUnformat, sceMcResFailDetect);
    }
    mc.unformatted[port] = 1;
    return begin(sceMcFuncNoUnformat, 0);
}

/* the result for a call on a port that is empty or unformatted, or 0 */
static int unusable(int port, int slot)
{
    if (!slot_ok(port, slot)) {
        return sceMcResFailDetect;
    }
    return mc.unformatted[port] ? sceMcResNoFormat : 0;
}

int sceMcMkdir(int port, int slot, char *name)
{
    char rel[MC_REL_MAX];
    char parent[MC_REL_MAX];
    char path[MC_PATH_MAX];
    char *slash;
    int r = unusable(port, slot);

    if (r != 0) {
        return begin(sceMcFuncNoMkdir, r);
    }
    if (resolve(port, name, rel, sizeof(rel)) != 0 || rel[0] == '\0') {
        return begin(sceMcFuncNoMkdir, sceMcResNoEntry);
    }
    host_path(path, sizeof(path), rel);
    if (kind_of(path, NULL, NULL) >= 0) {
        return begin(sceMcFuncNoMkdir, sceMcResNoEntry); /* it exists */
    }
    snprintf(parent, sizeof(parent), "%s", rel);
    slash = strrchr(parent, '/');
    if (slash != NULL) {
        *slash = '\0';
        host_path(path, sizeof(path), parent);
        if (kind_of(path, NULL, NULL) != 1) {
            return begin(sceMcFuncNoMkdir, sceMcResNoEntry);
        }
    } else if (make_root() != 0) {
        return begin(sceMcFuncNoMkdir, sceMcResFullDevice);
    }
    host_path(path, sizeof(path), rel);
    return begin(sceMcFuncNoMkdir, HOST_MKDIR(path) == 0 ? 0 : sceMcResFullDevice);
}

int sceMcChdir(int port, int slot, char *name, char *pwd)
{
    char rel[MC_REL_MAX];
    char path[MC_PATH_MAX];
    int r = unusable(port, slot);

    if (r != 0) {
        return begin(sceMcFuncNoChDir, r);
    }
    if (resolve(port, name, rel, sizeof(rel)) != 0) {
        return begin(sceMcFuncNoChDir, sceMcResNoEntry);
    }
    host_path(path, sizeof(path), rel);
    if (rel[0] != '\0' && kind_of(path, NULL, NULL) != 1) {
        return begin(sceMcFuncNoChDir, sceMcResNoEntry);
    }
    snprintf(mc.cwd[port], sizeof(mc.cwd[port]), "%s", rel);
    if (pwd != NULL) {
        /* the game's buffer is 20 bytes */
        char text[MC_REL_MAX + 2];

        snprintf(text, sizeof(text), "/%s", rel);
        memcpy(pwd, text, strlen(text) < 19 ? strlen(text) + 1 : 19);
        pwd[19] = '\0';
    }
    return begin(sceMcFuncNoChDir, 0);
}

int sceMcGetDir(int port, int slot, char *name, int flags, int nblk, struct sceMcTblGetDir *table)
{
    char dirpart[MC_REL_MAX];
    char rel[MC_REL_MAX];
    char path[MC_PATH_MAX];
    static char names[MC_DIR_MAX][MC_NAME_MAX + 1];
    const char *pat = name;
    const char *slash;
    int count;
    int out = 0;
    int skip;
    int idx = 0;
    int i;
    int r = unusable(port, slot);

    if (r != 0) {
        return begin(sceMcFuncNoGetDir, r);
    }
    slash = strrchr(name, '/');
    if (slash != NULL) {
        size_t n = (size_t)(slash - name);
        if (n >= sizeof(dirpart)) {
            return begin(sceMcFuncNoGetDir, sceMcResNoEntry);
        }
        memcpy(dirpart, name, n);
        dirpart[n] = '\0';
        if (n == 0) {
            strcpy(dirpart, "/");
        }
        pat = slash + 1;
    } else {
        dirpart[0] = '\0';
    }
    if (resolve(port, dirpart, rel, sizeof(rel)) != 0) {
        return begin(sceMcFuncNoGetDir, sceMcResNoEntry);
    }
    host_path(path, sizeof(path), rel);
    count = kind_of(path, NULL, NULL) == 1 ? list_dir(path, names, MC_DIR_MAX) : -1;
    if (count < 0) {
        /* a card folder that does not exist yet is an empty card */
        if (rel[0] == '\0') {
            count = 0;
        } else {
            return begin(sceMcFuncNoGetDir, sceMcResNoEntry);
        }
    }
    skip = flags != 0 ? mc.dirNext : 0;
    /* a subdirectory lists "." and ".." first, as a card does */
    for (i = -2; i < count && out < nblk; i++) {
        const char *entry = i == -2 ? "." : i == -1 ? ".." : names[i];
        char sub[MC_PATH_MAX];
        int kind;

        if (i < 0 && rel[0] == '\0') {
            continue;
        }
        if (!wild(pat, entry)) {
            continue;
        }
        if (idx++ < skip) {
            continue;
        }
        if (i < 0) {
            kind = 1;
            fill_entry(&table[out], entry, path, 1);
        } else {
            snprintf(sub, sizeof(sub), "%s/%s", path, entry);
            kind = kind_of(sub, NULL, NULL);
            fill_entry(&table[out], entry, sub, kind);
        }
        out++;
    }
    mc.dirNext = idx;
    return begin(sceMcFuncNoGetDir, out);
}

int sceMcDelete(int port, int slot, char *name)
{
    char rel[MC_REL_MAX];
    char path[MC_PATH_MAX];
    int r = unusable(port, slot);
    int k;

    if (r != 0) {
        return begin(sceMcFuncNoDelete, r);
    }
    if (resolve(port, name, rel, sizeof(rel)) != 0 || rel[0] == '\0') {
        return begin(sceMcFuncNoDelete, sceMcResNoEntry);
    }
    host_path(path, sizeof(path), rel);
    k = kind_of(path, NULL, NULL);
    if (k < 0) {
        return begin(sceMcFuncNoDelete, sceMcResNoEntry);
    }
    if (k == 1) {
        char names[1][MC_NAME_MAX + 1];

        if (list_dir(path, names, 1) > 0) {
            return begin(sceMcFuncNoDelete, sceMcResNotEmpty);
        }
        return begin(sceMcFuncNoDelete, HOST_RMDIR(path) == 0 ? 0 : sceMcResNotEmpty);
    }
    return begin(sceMcFuncNoDelete, remove(path) == 0 ? 0 : sceMcResDeniedPermit);
}

int sceMcOpen(int port, int slot, char *name, int flags)
{
    char rel[MC_REL_MAX];
    char path[MC_PATH_MAX];
    int r = unusable(port, slot);
    int mode = flags & 3;
    int k;
    int fd;
    const char *fmode;
    FILE *fp;

    if (r != 0) {
        return begin(sceMcFuncNoOpen, r);
    }
    if (mode == 0 || resolve(port, name, rel, sizeof(rel)) != 0 || rel[0] == '\0') {
        return begin(sceMcFuncNoOpen, sceMcResNoEntry);
    }
    for (fd = 0; fd < MC_HANDLES; fd++) {
        if (mc.handle[fd].fp == NULL) {
            break;
        }
    }
    if (fd == MC_HANDLES) {
        return begin(sceMcFuncNoOpen, sceMcResUpLimitHandle);
    }
    host_path(path, sizeof(path), rel);
    k = kind_of(path, NULL, NULL);
    if (k == 1) {
        return begin(sceMcFuncNoOpen, sceMcResDeniedPermit);
    }
    if (k < 0) {
        char parent[MC_REL_MAX];
        char *slash;

        if ((flags & SCE_CREAT) == 0) {
            return begin(sceMcFuncNoOpen, sceMcResNoEntry);
        }
        /* the file's directory must exist; the card root counts only once
           the game has made it */
        snprintf(parent, sizeof(parent), "%s", rel);
        slash = strrchr(parent, '/');
        if (slash != NULL) {
            *slash = '\0';
        } else {
            parent[0] = '\0';
        }
        host_path(path, sizeof(path), parent);
        if (kind_of(path, NULL, NULL) != 1) {
            return begin(sceMcFuncNoOpen, sceMcResNoEntry);
        }
        host_path(path, sizeof(path), rel);
        fmode = "w+b";
    } else if (flags & SCE_TRUNC) {
        fmode = "w+b";
    } else {
        fmode = mode == SCE_RDONLY ? "rb" : "r+b";
    }
    fp = fopen(path, fmode);
    if (fp == NULL) {
        return begin(sceMcFuncNoOpen, k < 0 ? sceMcResFullDevice : sceMcResDeniedPermit);
    }
    mc.handle[fd].fp = fp;
    mc.handle[fd].canRead = (mode & SCE_RDONLY) != 0;
    mc.handle[fd].canWrite = (mode & SCE_WRONLY) != 0;
    return begin(sceMcFuncNoOpen, fd);
}

static McHandle *handle_of(int fd)
{
    return fd >= 0 && fd < MC_HANDLES && mc.handle[fd].fp != NULL ? &mc.handle[fd] : NULL;
}

int sceMcClose(int fd)
{
    McHandle *h = handle_of(fd);

    if (h == NULL) {
        return begin(sceMcFuncNoClose, sceMcResDeniedPermit);
    }
    fclose(h->fp);
    memset(h, 0, sizeof(*h));
    return begin(sceMcFuncNoClose, 0);
}

int sceMcSeek(int fd, int offset, int origin)
{
    McHandle *h = handle_of(fd);
    long pos;

    if (h == NULL || origin < SCE_SEEK_SET || origin > SCE_SEEK_END ||
        fseek(h->fp, offset, origin) != 0 || (pos = ftell(h->fp)) < 0) {
        return begin(sceMcFuncNoSeek, sceMcResDeniedPermit);
    }
    return begin(sceMcFuncNoSeek, (int)pos);
}

int sceMcRead(int fd, void *buf, int len)
{
    McHandle *h = handle_of(fd);

    if (h == NULL || !h->canRead || len < 0) {
        return begin(sceMcFuncNoRead, sceMcResDeniedPermit);
    }
    fseek(h->fp, 0, SEEK_CUR); /* a stream changing direction */
    return begin(sceMcFuncNoRead, (int)fread(buf, 1, (size_t)len, h->fp));
}

int sceMcWrite(int fd, void *buf, int len)
{
    McHandle *h = handle_of(fd);

    if (h == NULL || !h->canWrite || len < 0) {
        return begin(sceMcFuncNoWrite, sceMcResDeniedPermit);
    }
    fseek(h->fp, 0, SEEK_CUR);
    if (fwrite(buf, 1, (size_t)len, h->fp) != (size_t)len) {
        return begin(sceMcFuncNoWrite, sceMcResFullDevice);
    }
    return begin(sceMcFuncNoWrite, len);
}

int sceMcFlush(int fd)
{
    McHandle *h = handle_of(fd);

    if (h == NULL) {
        return begin(sceMcFuncNoFlush, sceMcResDeniedPermit);
    }
    fflush(h->fp);
    return begin(sceMcFuncNoFlush, 0);
}
