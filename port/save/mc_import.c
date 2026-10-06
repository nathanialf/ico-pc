/*
 * port/save/mc_import.c
 *
 * ICO's save out of a PS2 memory card image or a save archive, into the card
 * folder mc_host.c reads (docs/port/SAVES.md, "Importing saves"):
 *
 *   raw images (.ps2, .bin): the card's own file system. A superblock at
 *     page 0 ("Sony PS2 Memory Card Format "), a FAT reached through the
 *     superblock's indirect FAT cluster list, and directories of 512-byte
 *     entries. Pages are page_len bytes (512 on the 8 MB card), each followed
 *     by page_len / 32 bytes of ECC in images that keep them (PCSX2's and
 *     mymc's .ps2) and by nothing in plain dumps; which one is told by the
 *     file's size. The ECC is not checked.
 *   .psu (uLaunchELF, mymc): the save directory's entry, "." and "..", then
 *     each file's entry followed by its data padded to 1024 bytes. Entries
 *     have the card's directory entry layout.
 *   .max (Action Replay MAX, "Ps2PowerSave" + LZARI) and .cbs (CodeBreaker,
 *     "CFU", encrypted and deflated) are recognised and refused.
 *
 * Only the game's directory (BESCES-50760ico) is imported; every other
 * entry is counted and named in the result. Every file is read before any
 * is written, and each is written to a dot-file copy moved over its name,
 * as mc_host.c writes.
 */
#include "mc_import.h"
#include "host_fs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/ico_endian.h"

#define NAME_MAX_ 31
#define MAX_FILES 64
/* the largest card PCSX2 makes is 64 MB; with ECC spares a little more */
#define IMAGE_MAX (80u * 1024u * 1024u)
#define ARCHIVE_MAX (16u * 1024u * 1024u)

#define DF_FILE 0x0010
#define DF_DIR 0x0020
#define DF_EXISTS 0x8000

#define FAT_ALLOCATED 0x80000000u
#define FAT_NEXT_MASK 0x7FFFFFFFu

typedef struct {
    char name[NAME_MAX_ + 1];
    unsigned char *data;
    uint32_t len;
} File;

typedef struct {
    IcoMcImport *res;
    File file[MAX_FILES];
    int nfile;
    int found; /* the game's directory was in the source */
} Job;

static int fail(Job *j, const char *msg, const char *arg)
{
    snprintf(j->res->why, sizeof(j->res->why), msg, arg != NULL ? arg : "");
    return -1;
}

/* a directory entry's name: at most 32 bytes, NUL-terminated or not */
static void entry_name(const unsigned char *ent, char out[NAME_MAX_ + 2])
{
    memcpy(out, ent + 0x40, 32);
    out[32] = '\0';
}

/* a name mc_host.c can hold (its valid_name), not a dot file */
static int name_ok(const char *s)
{
    size_t n = strlen(s);
    size_t i;

    if (n == 0 || n > NAME_MAX_ || s[0] == '.') {
        return 0;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || strchr("\\/:*?\"<>|", c) != NULL) {
            return 0;
        }
    }
    return 1;
}

static void skip(Job *j, const char *name)
{
    IcoMcImport *r = j->res;

    if (r->skipped < ICO_MC_IMPORT_LIST) {
        snprintf(r->skippedName[r->skipped], sizeof(r->skippedName[0]), "%s", name);
    }
    r->skipped++;
}

/* takes data (malloc'd) into the job */
static int add_file(Job *j, const char *name, unsigned char *data, uint32_t len)
{
    int i;

    if (!name_ok(name)) {
        free(data);
        return fail(j, "the save holds a file the card folder cannot name: \"%s\"", name);
    }
    for (i = 0; i < j->nfile; i++) {
        if (strcmp(j->file[i].name, name) == 0) {
            free(data);
            return fail(j, "the save holds \"%s\" twice", name);
        }
    }
    if (j->nfile == MAX_FILES) {
        free(data);
        return fail(j, "the save holds too many files%s", NULL);
    }
    snprintf(j->file[j->nfile].name, sizeof(j->file[0].name), "%s", name);
    j->file[j->nfile].data = data;
    j->file[j->nfile].len = len;
    j->nfile++;
    return 0;
}

static unsigned char *read_all(const char *path, size_t max, size_t *len)
{
    FILE *f = ico_fopen(path, "rb");
    unsigned char *buf;
    size_t cap = 1 << 16;
    size_t n = 0;
    size_t got;

    if (f == NULL) {
        return NULL;
    }
    buf = malloc(cap);
    while (buf != NULL && (got = fread(buf + n, 1, cap - n, f)) > 0) {
        n += got;
        if (n == cap) {
            unsigned char *nb;

            if (cap > max) {
                free(buf);
                buf = NULL;
                break;
            }
            nb = realloc(buf, cap * 2);
            if (nb == NULL) {
                free(buf);
                buf = NULL;
                break;
            }
            buf = nb;
            cap *= 2;
        }
    }
    if (buf != NULL && (ferror(f) || n > max)) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *len = n;
    return buf;
}

/* --- raw card images ------------------------------------------------------------ */

typedef struct {
    const unsigned char *img;
    size_t size;
    uint32_t pageLen;
    uint32_t pagesPerCluster;
    uint32_t stride; /* page_len plus the spare */
    uint32_t clusterSize;
    uint32_t clusters;
    uint32_t allocOffset;
    uint32_t rootCluster;
    uint32_t ifc[32];
} Card;

static const char SB_MAGIC[] = "Sony PS2 Memory Card Format ";

/* an absolute cluster's bytes into out (clusterSize); -1 out of range */
static int read_cluster(const Card *c, uint32_t abs, unsigned char *out)
{
    uint32_t k;

    if (abs >= c->clusters) {
        return -1;
    }
    for (k = 0; k < c->pagesPerCluster; k++) {
        size_t off = ((size_t)abs * c->pagesPerCluster + k) * c->stride;

        if (off + c->pageLen > c->size) {
            return -1;
        }
        memcpy(out + (size_t)k * c->pageLen, c->img + off, c->pageLen);
    }
    return 0;
}

/* the FAT entry of allocatable cluster n; -1 when the FAT cannot be read */
static int fat_entry(const Card *c, uint32_t n, uint32_t *value, unsigned char *tmp)
{
    uint32_t per = c->clusterSize / 4;
    uint32_t indirect = n / per;
    uint32_t dbl = indirect / per;
    uint32_t fatCluster;

    if (dbl >= 32 || read_cluster(c, c->ifc[dbl], tmp) != 0) {
        return -1;
    }
    fatCluster = ico_le32(tmp + 4 * (indirect % per));
    if (read_cluster(c, fatCluster, tmp) != 0) {
        return -1;
    }
    *value = ico_le32(tmp + 4 * (n % per));
    return 0;
}

/* len bytes of the chain starting at allocatable cluster first; NULL when
   the chain is broken or short (why set) */
static unsigned char *read_chain(Job *j, const Card *c, uint32_t first, uint32_t len)
{
    unsigned char *out;
    unsigned char *tmp;
    unsigned char *cl;
    uint32_t done = 0;
    uint32_t cur = first;
    uint32_t steps = 0;

    /* no file is larger than the card (whose clusters read_raw matched to
       the image's size, at most IMAGE_MAX): a damaged length is refused
       before it sizes an allocation */
    if ((uint64_t)len > (uint64_t)c->clusters * c->clusterSize) {
        fail(j, "the card image names a file larger than the card%s", NULL);
        return NULL;
    }
    out = malloc(len > 0 ? len : 1);
    tmp = malloc(c->clusterSize);
    cl = malloc(c->clusterSize);
    if (out == NULL || tmp == NULL || cl == NULL) {
        fail(j, "out of memory%s", NULL);
        goto bad;
    }
    while (done < len) {
        uint32_t n = len - done < c->clusterSize ? len - done : c->clusterSize;
        uint32_t v;

        if (++steps > c->clusters || read_cluster(c, c->allocOffset + cur, cl) != 0) {
            fail(j, "the card image's file chain leaves the card%s", NULL);
            goto bad;
        }
        memcpy(out + done, cl, n);
        done += n;
        if (done == len) {
            break;
        }
        if (fat_entry(c, cur, &v, tmp) != 0 || (v & FAT_ALLOCATED) == 0 ||
            (v & FAT_NEXT_MASK) == FAT_NEXT_MASK) {
            fail(j, "the card image's FAT ends a file early%s", NULL);
            goto bad;
        }
        cur = v & FAT_NEXT_MASK;
    }
    free(tmp);
    free(cl);
    return out;
bad:
    free(out);
    free(tmp);
    free(cl);
    return NULL;
}

/* a directory's entries: the "." entry at its first cluster gives the count */
static unsigned char *read_dir(Job *j, const Card *c, uint32_t first, uint32_t *count)
{
    unsigned char *head = read_chain(j, c, first, 512);
    uint32_t n;

    if (head == NULL) {
        return NULL;
    }
    n = ico_le32(head + 4);
    free(head);
    if (n < 2 || n > c->clusters * (c->clusterSize / 512)) {
        fail(j, "the card image has a damaged directory%s", NULL);
        return NULL;
    }
    *count = n;
    return read_chain(j, c, first, n * 512);
}

static int import_save_dir(Job *j, const Card *c, const unsigned char *dirent)
{
    uint32_t count;
    unsigned char *ents = read_dir(j, c, ico_le32(dirent + 0x10), &count);
    uint32_t i;
    int r = 0;

    if (ents == NULL) {
        return -1;
    }
    j->found = 1;
    for (i = 2; i < count && r == 0; i++) {
        const unsigned char *e = ents + (size_t)i * 512;
        uint16_t mode = ico_le16(e);
        char name[NAME_MAX_ + 2];
        unsigned char *data;

        if ((mode & DF_EXISTS) == 0) {
            continue; /* deleted */
        }
        entry_name(e, name);
        if ((mode & DF_FILE) == 0) {
            char full[NAME_MAX_ + 2 + sizeof(ICO_MC_SAVE_DIR)];

            snprintf(full, sizeof(full), ICO_MC_SAVE_DIR "/%s", name);
            skip(j, full);
            continue;
        }
        data = read_chain(j, c, ico_le32(e + 0x10), ico_le32(e + 4));
        r = data != NULL ? add_file(j, name, data, ico_le32(e + 4)) : -1;
    }
    free(ents);
    return r;
}

static int read_raw(Job *j, const unsigned char *img, size_t size)
{
    Card c;
    uint32_t pages;
    uint32_t count;
    unsigned char *root;
    uint32_t i;
    int r = 0;

    memset(&c, 0, sizeof(c));
    c.img = img;
    c.size = size;
    if (size < 0x154) {
        return fail(j, "the card image is too short%s", NULL);
    }
    c.pageLen = ico_le16(img + 0x28);
    c.pagesPerCluster = ico_le16(img + 0x2A);
    c.clusters = ico_le32(img + 0x30);
    c.allocOffset = ico_le32(img + 0x34);
    c.rootCluster = ico_le32(img + 0x3C);
    for (i = 0; i < 32; i++) {
        c.ifc[i] = ico_le32(img + 0x50 + 4 * i);
    }
    if ((c.pageLen != 512 && c.pageLen != 1024) || c.pagesPerCluster == 0 ||
        c.pagesPerCluster > 16 || c.clusters == 0 || c.clusters > (1u << 20) ||
        c.allocOffset >= c.clusters) {
        return fail(j, "the card image's superblock is not one this tool reads%s", NULL);
    }
    c.clusterSize = c.pageLen * c.pagesPerCluster;
    pages = c.clusters * c.pagesPerCluster;
    if (size == (size_t)pages * c.pageLen) {
        c.stride = c.pageLen;
    } else if (size == (size_t)pages * (c.pageLen + c.pageLen / 32)) {
        c.stride = c.pageLen + c.pageLen / 32;
    } else {
        return fail(j, "the card image's size matches neither a plain nor an ECC image%s", NULL);
    }
    root = read_dir(j, &c, c.rootCluster, &count);
    if (root == NULL) {
        return -1;
    }
    for (i = 2; i < count && r == 0; i++) {
        const unsigned char *e = root + (size_t)i * 512;
        char name[NAME_MAX_ + 2];

        if ((ico_le16(e) & DF_EXISTS) == 0) {
            continue;
        }
        entry_name(e, name);
        if ((ico_le16(e) & DF_DIR) != 0 && strcmp(name, ICO_MC_SAVE_DIR) == 0) {
            r = import_save_dir(j, &c, e);
        } else {
            skip(j, name);
        }
    }
    free(root);
    return r;
}

/* --- .psu --------------------------------------------------------------------- */

static int read_psu(Job *j, const unsigned char *p, size_t size)
{
    char name[NAME_MAX_ + 2];
    uint32_t count;
    uint32_t i;
    size_t off = 512;

    if (size < 3 * 512 || (ico_le16(p) & (DF_DIR | DF_EXISTS)) != (DF_DIR | DF_EXISTS)) {
        return fail(j, "the .psu file does not start with a directory%s", NULL);
    }
    entry_name(p, name);
    count = ico_le32(p + 4);
    if (count < 2 || count > MAX_FILES + 2) {
        return fail(j, "the .psu file's directory entry is damaged%s", NULL);
    }
    if (strcmp(name, ICO_MC_SAVE_DIR) != 0) {
        skip(j, name);
        return 0;
    }
    j->found = 1;
    for (i = 0; i < count; i++) {
        const unsigned char *e;
        uint16_t mode;
        uint32_t len;
        unsigned char *data;

        if (off + 512 > size) {
            return fail(j, "the .psu file is cut short%s", NULL);
        }
        e = p + off;
        off += 512;
        mode = ico_le16(e);
        entry_name(e, name);
        if ((mode & DF_DIR) != 0) {
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                continue;
            }
            return fail(j, "the .psu file holds a subdirectory, \"%s\"", name);
        }
        len = ico_le32(e + 4);
        if (len > size - off) {
            return fail(j, "the .psu file is cut short in \"%s\"", name);
        }
        data = malloc(len > 0 ? len : 1);
        if (data == NULL) {
            return fail(j, "out of memory%s", NULL);
        }
        memcpy(data, p + off, len);
        off += ((size_t)len + 1023) / 1024 * 1024;
        if (off > size) {
            off = size; /* the last file's padding may be missing */
        }
        if (add_file(j, name, data, len) != 0) {
            return -1;
        }
    }
    return 0;
}

/* --- detection and output ------------------------------------------------------- */

static int detect(const unsigned char *p, size_t n)
{
    if (n >= 28 && memcmp(p, SB_MAGIC, 28) == 0) {
        return ICO_MC_FMT_RAW;
    }
    if (n >= 12 && memcmp(p, "Ps2PowerSave", 12) == 0) {
        return ICO_MC_FMT_MAX;
    }
    if (n >= 4 && memcmp(p, "CFU", 4) == 0) {
        return ICO_MC_FMT_CBS;
    }
    if (n >= 1024 && (ico_le16(p) & (DF_DIR | DF_EXISTS)) == (DF_DIR | DF_EXISTS) &&
        (ico_le16(p + 512) & DF_DIR) != 0 && p[512 + 0x40] == '.' && p[512 + 0x41] == '\0') {
        return ICO_MC_FMT_PSU;
    }
    return ICO_MC_FMT_UNKNOWN;
}

int ico_mc_import_detect(const char *path)
{
    unsigned char head[1024];
    FILE *f = ico_fopen(path, "rb");
    size_t n;

    if (f == NULL) {
        return ICO_MC_FMT_UNKNOWN;
    }
    n = fread(head, 1, sizeof(head), f);
    fclose(f);
    return detect(head, n);
}

/* path and its missing parents */
static int make_dirs(const char *dir)
{
    char path[1100];
    size_t i;

    if (strlen(dir) >= sizeof(path)) {
        return -1;
    }
    snprintf(path, sizeof(path), "%s", dir);
    for (i = 1; path[i] != '\0'; i++) {
        if (path[i] == '/' || path[i] == '\\') {
            char ch = path[i];

            path[i] = '\0';
            if (ico_path_kind(path, NULL, NULL) < 0) {
                ico_mkdir(path);
            }
            path[i] = ch;
        }
    }
    if (ico_path_kind(path, NULL, NULL) < 0) {
        ico_mkdir(path);
    }
    return ico_path_kind(path, NULL, NULL) == 1 ? 0 : -1;
}

static int write_files(Job *j, const char *saves, unsigned flags)
{
    char dir[1100];
    char path[1200];
    char tmp[1200];
    int i;

    if (snprintf(dir, sizeof(dir), "%s/" ICO_MC_SAVE_DIR, saves) >= (int)sizeof(dir)) {
        return fail(j, "the card folder's path is too long%s", NULL);
    }
    /* refuse before writing anything */
    for (i = 0; i < j->nfile; i++) {
        int k;

        snprintf(path, sizeof(path), "%s/%s", dir, j->file[i].name);
        k = ico_path_kind(path, NULL, NULL);
        if (k == 1 || (k == 0 && (flags & ICO_MC_IMPORT_OVERWRITE) == 0)) {
            return fail(j,
                        "the card folder already holds %s (import with overwrite to replace "
                        "it)",
                        j->file[i].name);
        }
    }
    if (make_dirs(dir) != 0) {
        return fail(j, "cannot make the folder %.200s", dir);
    }
    for (i = 0; i < j->nfile; i++) {
        FILE *f;
        int ok;

        snprintf(path, sizeof(path), "%s/%s", dir, j->file[i].name);
        snprintf(tmp, sizeof(tmp), "%s/.%s.tmp", dir, j->file[i].name);
        f = ico_fopen(tmp, "wb");
        if (f == NULL) {
            return fail(j, "cannot write %.200s", tmp);
        }
        ok = fwrite(j->file[i].data, 1, j->file[i].len, f) == j->file[i].len;
        ok = ico_fsync(f) == 0 && ok;
        ok = fclose(f) == 0 && ok;
        if (!ok || ico_rename_replace(tmp, path) != 0) {
            ico_remove(tmp);
            return fail(j, "cannot write %.200s", path);
        }
        j->res->files++;
    }
    return 0;
}

int ico_mc_import(const char *src, const char *saves, unsigned flags, IcoMcImport *res)
{
    Job j;
    unsigned char *buf;
    size_t len = 0;
    int r;
    int i;

    memset(res, 0, sizeof(*res));
    memset(&j, 0, sizeof(j));
    j.res = res;
    buf = read_all(src, IMAGE_MAX, &len);
    if (buf == NULL) {
        return fail(&j, "cannot read %s (missing, unreadable or too large)", src);
    }
    res->format = detect(buf, len);
    switch (res->format) {
    case ICO_MC_FMT_RAW:
        r = read_raw(&j, buf, len);
        break;
    case ICO_MC_FMT_PSU:
        r = len <= ARCHIVE_MAX ? read_psu(&j, buf, len)
                               : fail(&j, "the .psu file is too large%s", NULL);
        break;
    case ICO_MC_FMT_MAX:
        r = fail(&j,
                 "Action Replay MAX (.max) archives are not supported; convert the save to "
                 ".psu or a folder card with another tool%s",
                 NULL);
        break;
    case ICO_MC_FMT_CBS:
        r = fail(&j,
                 "CodeBreaker (.cbs) archives are not supported; convert the save to .psu "
                 "or a folder card with another tool%s",
                 NULL);
        break;
    default:
        r = fail(&j, "%s is not a PS2 card image (.ps2/.bin) or a .psu archive", src);
        break;
    }
    free(buf);
    if (r == 0 && !j.found) {
        snprintf(res->why, sizeof(res->why), "no %s save in %s", ICO_MC_SAVE_DIR, src);
        r = 1;
    } else if (r == 0 && j.nfile == 0) {
        snprintf(res->why, sizeof(res->why), "the %s directory in %s is empty", ICO_MC_SAVE_DIR,
                 src);
        r = 1;
    } else if (r == 0) {
        r = write_files(&j, saves, flags);
    }
    for (i = 0; i < j.nfile; i++) {
        free(j.file[i].data);
    }
    return r;
}
