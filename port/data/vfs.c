/*
 * port/data/vfs.c
 *
 * The backend-independent half of the VFS (vfs.h): path normalization,
 * byte reads over sector reads, and the disc slot the libcdvd layer uses.
 */
#include "vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct IcoVfs {
    const IcoVfsBackend *backend;
    void *state;
};

static IcoVfs *discVfs;

IcoVfs *ico_vfs_mount(const IcoVfsBackend *backend, const char *location)
{
    IcoVfs *vfs;
    void *state = NULL;

    if (backend == NULL || location == NULL) {
        return NULL;
    }
    if (backend->mount(&state, location) != 0) {
        fprintf(stderr, "vfs: %s: cannot mount %s\n", backend->name, location);
        return NULL;
    }
    vfs = calloc(1, sizeof(*vfs));
    if (vfs == NULL) {
        backend->unmount(state);
        return NULL;
    }
    vfs->backend = backend;
    vfs->state = state;
    return vfs;
}

void ico_vfs_unmount(IcoVfs *vfs)
{
    if (vfs == NULL) {
        return;
    }
    if (discVfs == vfs) {
        discVfs = NULL;
    }
    vfs->backend->unmount(vfs->state);
    free(vfs);
}

void ico_vfs_set_disc(IcoVfs *vfs)
{
    discVfs = vfs;
}

IcoVfs *ico_vfs_disc(void)
{
    return discVfs;
}

int ico_vfs_normalize(const char *in, char *out, size_t outsz)
{
    const char *p = in;
    const char *colon;
    size_t n = 0;

    if (in == NULL || out == NULL || outsz == 0) {
        return -1;
    }
    /* a device prefix: everything up to the first ':' when it comes before
       any separator ("cdrom0:\\X;1") */
    colon = strchr(p, ':');
    if (colon != NULL && strcspn(p, "/\\") > (size_t)(colon - p)) {
        p = colon + 1;
    }
    while (*p == '/' || *p == '\\') {
        p++;
    }
    for (; *p != '\0' && *p != ';'; p++) {
        char c = *p;

        if (c == '\\') {
            c = '/';
        } else if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        /* collapse doubled separators */
        if (c == '/' && n > 0 && out[n - 1] == '/') {
            continue;
        }
        if (n + 1 >= outsz) {
            return -1;
        }
        out[n++] = c;
    }
    while (n > 0 && out[n - 1] == '/') {
        n--;
    }
    out[n] = '\0';
    return 0;
}

int ico_vfs_stat(IcoVfs *vfs, const char *path, IcoVfsEntry *out)
{
    char norm[ICO_VFS_PATH_MAX];
    IcoVfsEntry e;

    if (vfs == NULL || ico_vfs_normalize(path, norm, sizeof(norm)) != 0) {
        return -1;
    }
    if (vfs->backend->lookup(vfs->state, norm, &e) != 0) {
        return -1;
    }
    if (out != NULL) {
        *out = e;
    }
    return 0;
}

int ico_vfs_open(IcoVfs *vfs, const char *path, IcoVfsFile *f)
{
    if (f == NULL || ico_vfs_stat(vfs, path, &f->entry) != 0) {
        return -1;
    }
    f->vfs = vfs;
    return 0;
}

uint32_t ico_vfs_size(const IcoVfsFile *f)
{
    return f->entry.size;
}

int64_t ico_vfs_read(const IcoVfsFile *f, uint64_t offset, void *dst, size_t len)
{
    unsigned char sec[ICO_VFS_SECTOR];
    unsigned char *d = dst;
    uint64_t end;
    int64_t done = 0;

    if (f == NULL || f->vfs == NULL) {
        return -1;
    }
    if (offset >= f->entry.size) {
        return 0;
    }
    /* in this order: offset + len can wrap for a huge len */
    end = len > f->entry.size - offset ? f->entry.size : offset + len;
    while (offset < end) {
        uint32_t lsn = f->entry.lsn + ico_vfs_offset_to_lsn(offset);
        uint32_t in = (uint32_t)(offset % ICO_VFS_SECTOR);
        uint64_t left = end - offset;

        if (in == 0 && left >= ICO_VFS_SECTOR) {
            /* whole sectors straight into the caller's buffer */
            uint32_t n = (uint32_t)(left / ICO_VFS_SECTOR);

            if (ico_vfs_read_sectors(f->vfs, lsn, n, d) != 0) {
                return -1;
            }
            d += (size_t)n * ICO_VFS_SECTOR;
            offset += (uint64_t)n * ICO_VFS_SECTOR;
            done += (int64_t)n * ICO_VFS_SECTOR;
        } else {
            uint32_t n = ICO_VFS_SECTOR - in;

            if (n > left) {
                n = (uint32_t)left;
            }
            if (ico_vfs_read_sectors(f->vfs, lsn, 1, sec) != 0) {
                return -1;
            }
            memcpy(d, sec + in, n);
            d += n;
            offset += n;
            done += n;
        }
    }
    return done;
}

int ico_vfs_read_sectors(IcoVfs *vfs, uint32_t lsn, uint32_t count, void *dst)
{
    if (vfs == NULL) {
        return -1;
    }
    if (count == 0) {
        return 0;
    }
    return vfs->backend->read_sectors(vfs->state, lsn, count, dst);
}

uint32_t ico_vfs_volume_sectors(IcoVfs *vfs)
{
    if (vfs == NULL) {
        return 0;
    }
    return vfs->backend->volume_sectors(vfs->state);
}
