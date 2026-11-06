/* rd_vu_skin_capture.c: the skin check's capture on failure (issue 123;
 * rd_vu_skin_check.h, docs/port/VU_SKIN_CHECK.md "The capture").
 *
 * On the run's first failure the check (rd_vu_skin_check.c) writes one
 * sidecar beside the log, logs/vu-skin-fail-<frame>-<list>-<index>.bin: a
 * fixed header (RdSkinCaptureHeader: the command, the twins' counts, the
 * probe's verdict and the decision's words) and the sections it had: the
 * bone bytes the CPU wrote, the GPU's copy, the ring after the fence, the
 * draw's VuCB, VU memory 0..35, the stream's slice and the probe's rows.
 * Then it asks the window for a frame dump of the next present
 * (rd_vu_skin_capture_due, polled from the window's pump), which puts the
 * sidecar into the dump's zip.  Once a run: a reporter's log names both
 * files, and nothing is asked of the reporter. */
#include <stdio.h>
#include <string.h>

#include "host_fs.h"
#include "rd.h"
#include "rd_vu_skin_check.h"

static char s_dir[1024];
/* the capture asked of the window: 1 due, 0 not (or taken); the path is
 * written before the flag (release), read after it (acquire) */
static int s_due;
static char s_path[1100];

void rd_set_vu_skin_capture_dir(const char *dir)
{
    snprintf(s_dir, sizeof(s_dir), "%s", dir ? dir : "");
}

bool rd_vu_skin_capture_due(char *path, size_t size)
{
    if (__atomic_load_n(&s_due, __ATOMIC_ACQUIRE) == 0) {
        return false;
    }
    if (path && size) {
        snprintf(path, size, "%s", s_path);
    }
    __atomic_store_n(&s_due, 0, __ATOMIC_RELAXED);
    return true;
}

/* the check, after its sidecar (path, "" when none): the window's frame
 * dump asked */
void rd__vu_skin_capture_ask(const char *path)
{
    snprintf(s_path, sizeof(s_path), "%s", path ? path : "");
    __atomic_store_n(&s_due, 1, __ATOMIC_RELEASE);
}

const char *rd__vu_skin_capture_dir(void)
{
    return s_dir;
}

bool rd__vu_skin_capture_write(const char *dir, RdSkinCapture *c, char *path, size_t size)
{
    if (!dir || !dir[0] || !c || !path || size == 0) {
        if (path && size) {
            snprintf(path, size, "no folder for it");
        }
        return false;
    }
    RdSkinCaptureHeader *h = &c->h;
    memcpy(h->magic, RD_SKIN_CAPTURE_MAGIC, 8);
    h->version = RD_SKIN_CAPTURE_VERSION;
    h->headerBytes = (uint32_t)sizeof(*h);

    /* the sections one after another, each at a 16-byte offset */
    struct {
        const void *p;
        uint32_t *off, *bytes;
    } sec[] = {
        {c->cpu, &h->cpuOff, &h->cpuBytes},    {c->gpu, &h->gpuOff, &h->gpuBytes},
        {c->ring, &h->ringOff, &h->ringBytes}, {c->vcb, &h->vcbOff, &h->vcbBytes},
        {c->mem, &h->memOff, &h->memBytes},    {c->stream, &h->streamOff, &h->streamBytes},
        {c->rows, &h->rowsOff, &h->rowsBytes},
    };

    const size_t ns = sizeof(sec) / sizeof(sec[0]);
    uint32_t at = (h->headerBytes + 15u) & ~15u;
    for (size_t i = 0; i < ns; i++) {
        if (!sec[i].p || *sec[i].bytes == 0) {
            *sec[i].off = 0;
            *sec[i].bytes = 0;
            continue;
        }
        *sec[i].off = at;
        at = (at + *sec[i].bytes + 15u) & ~15u;
    }
    const int n =
        snprintf(path, size, "%s/vu-skin-fail-%u-%u-%u.bin", dir, h->frame, h->list, h->index);
    if (n < 0 || (size_t)n >= size) {
        snprintf(path, size, "the folder's path is too long");
        return false;
    }
    FILE *f = ico_fopen(path, "wb");
    if (!f) {
        return false;
    }
    static const uint8_t pad[16];
    bool ok = fwrite(h, sizeof(*h), 1, f) == 1;
    uint32_t pos = h->headerBytes;
    for (size_t i = 0; i < ns && ok; i++) {
        if (*sec[i].bytes == 0) {
            continue;
        }
        ok = fwrite(pad, 1, *sec[i].off - pos, f) == *sec[i].off - pos &&
             fwrite(sec[i].p, 1, *sec[i].bytes, f) == *sec[i].bytes;
        pos = *sec[i].off + *sec[i].bytes;
    }
    ok = fclose(f) == 0 && ok;
    if (!ok) {
        ico_remove(path);
    }
    return ok;
}
