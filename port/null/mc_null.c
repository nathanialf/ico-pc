/*
 * port/null/mc_null.c
 *
 * libmc with both card slots empty.  Every request is accepted (returns 0)
 * and its sceMcSync result is ICO_MC_NULL_RESULT (-10), the "no card"
 * result of sceMcGetInfo; sceMcGetInfo also reports card type 0, no free
 * space and not formatted.  What the game does with that:
 *   - fumi/ios/mcard.c iosMcMgrGetInfo stores -10 as the slot's cardState;
 *     iosMcMgrChdirProduct maps it to -9 ("not insert memory card");
 *   - common/src/layout_action.c _la_memory_card_check reads type 0 at its
 *     step 3 and returns 99 (no card), the path the boot and save screens
 *     take for an empty slot.
 * The sceMcSync protocol is libmc's (sce/libmc/libmc.c): it returns -1 when
 * no request is pending, else 1 with *cmd the request's function number
 * and *result its result.
 *
 * port/save/mc_host.c replaces this in Phase 4.
 */
#include "null_devices.h"

#include <libmc.h>

#include <stddef.h>

/* libmc's function numbers, the *cmd sceMcSync reports */
enum {
    MC_FUNC_GETINFO = 0x01,
    MC_FUNC_OPEN = 0x02,
    MC_FUNC_CLOSE = 0x03,
    MC_FUNC_SEEK = 0x04,
    MC_FUNC_READ = 0x05,
    MC_FUNC_WRITE = 0x06,
    MC_FUNC_FLUSH = 0x0A,
    MC_FUNC_MKDIR = 0x0B,
    MC_FUNC_CHDIR = 0x0C,
    MC_FUNC_GETDIR = 0x0D,
    MC_FUNC_DELETE = 0x0F,
    MC_FUNC_FORMAT = 0x10,
    MC_FUNC_UNFORMAT = 0x11,
};

static int pendingFunc; /* 0: nothing pending */

static int request(int func)
{
    pendingFunc = func;
    return 0;
}

int sceMcInit(void)
{
    pendingFunc = 0;
    return 0;
}

int sceMcGetInfo(int port, int slot, int *type, int *free, int *format)
{
    (void)port;
    (void)slot;
    if (type != NULL) {
        *type = 0;
    }
    if (free != NULL) {
        *free = 0;
    }
    if (format != NULL) {
        *format = 0;
    }
    return request(MC_FUNC_GETINFO);
}

int sceMcSync(int mode, int *cmd, int *result)
{
    (void)mode;
    if (pendingFunc == 0) {
        return -1;
    }
    if (cmd != NULL) {
        *cmd = pendingFunc;
    }
    if (result != NULL) {
        *result = ICO_MC_NULL_RESULT;
    }
    pendingFunc = 0;
    return 1;
}

int sceMcOpen(int port, int slot, char *name, int flags)
{
    (void)port;
    (void)slot;
    (void)name;
    (void)flags;
    return request(MC_FUNC_OPEN);
}

int sceMcClose(int arg)
{
    (void)arg;
    return request(MC_FUNC_CLOSE);
}

int sceMcSeek(int fd, int offset, int origin)
{
    (void)fd;
    (void)offset;
    (void)origin;
    return request(MC_FUNC_SEEK);
}

int sceMcRead(int fd, void *buf, int len)
{
    (void)fd;
    (void)buf;
    (void)len;
    return request(MC_FUNC_READ);
}

int sceMcWrite(int fd, void *buf, int len)
{
    (void)fd;
    (void)buf;
    (void)len;
    return request(MC_FUNC_WRITE);
}

int sceMcFlush(int arg)
{
    (void)arg;
    return request(MC_FUNC_FLUSH);
}

int sceMcMkdir(int port, int slot, char *name)
{
    (void)port;
    (void)slot;
    (void)name;
    return request(MC_FUNC_MKDIR);
}

int sceMcChdir(int port, int slot, char *name, char *pwd)
{
    (void)port;
    (void)slot;
    (void)name;
    (void)pwd;
    return request(MC_FUNC_CHDIR);
}

int sceMcGetDir(int port, int slot, char *name, int flags, int nblk, struct sceMcTblGetDir *table)
{
    (void)port;
    (void)slot;
    (void)name;
    (void)flags;
    (void)nblk;
    (void)table;
    return request(MC_FUNC_GETDIR);
}

int sceMcDelete(int port, int slot, char *name)
{
    (void)port;
    (void)slot;
    (void)name;
    return request(MC_FUNC_DELETE);
}

int sceMcFormat(int port, int slot)
{
    (void)port;
    (void)slot;
    return request(MC_FUNC_FORMAT);
}

int sceMcUnformat(int port, int slot)
{
    (void)port;
    (void)slot;
    return request(MC_FUNC_UNFORMAT);
}
