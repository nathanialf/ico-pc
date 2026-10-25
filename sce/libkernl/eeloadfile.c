/* libkernl.a(eeloadfile.o) */

#include <sifrpc.h>
#include <string.h>
#include <sifcmd.h>

/* eeloadfile.o's .data: -1 until _lf_bind has bound the loadfile server, then
   the stamp _lf_version accepts besides the library's own (its four dots are
   the member's first .rodata, 0x636710; Aug-6 prototype 0x628830, Jun-26 demo
   0x676280). */
static int lf_bind_state = -1; /* derived name */

static char *lf_stamp = "...."; /* derived name */

/* the member's .bss: the loadfile RPC buffer (0x200 bytes sent), the
   server's client record and the IOP module's version stamp */
static char lf_buf[0x200] __attribute__((aligned(64))); /* derived name */

static sceSifRpcClientData lf_cd __attribute__((aligned(64))); /* derived name */

static char lf_iopVersion[4]; /* derived name */

int _lf_bind(void)
{
    int i;
    int r;
    int val;
    if (lf_bind_state >= 0)
        goto ret0;
loop:
    r = sceSifBindRpc(&lf_cd, 0x80000006, 0);
    if (r < 0)
        return -1;
    val = lf_cd.serve;
    if (val == 0)
        goto delay;
    lf_bind_state = 0;
    r = sceSifCallRpc(&lf_cd, 0xFF, 0, 0, 0, lf_buf, 4, 0, 0);
    if (r < 0)
        return 0xFFFEFFFF;
    __builtin_memcpy(lf_iopVersion, lf_buf, 4);
    return 0;
delay:
    /* IOP-side retry back-off: spin 0x100000 times, no memory touched. */
    i = 0x100000;
    do {
        i--;
    } while (i != -1);
    goto loop;
ret0:
    return 0;
}

int _lf_version(void)
{
    void *s3 = __ps2_klibinfo__ + 12;
    void *s1 = lf_iopVersion;
    int s2 = 0;
    char *v;
    if (memcmp(s1, s3, 4) == 0)
        goto done;
    v = lf_stamp;
    if (memcmp(s1, v, 4) == 0)
        goto done;
    v = lf_stamp;
    s2 = (0 < (unsigned int)memcmp(s3, v, 4));
done:
    return s2;
}

int sceSifLoadFileReset(void)
{
    lf_bind_state = -1;
    memset(lf_iopVersion, 0, 4);
    return 0;
}

/* The request's 0xFC-byte argument block.  The oversize copy below is a record
   assignment, which gcc expands inline through movstrsi; memcpy stays a call at
   -fno-builtin, so the inline ldl/ldr run can only come from a record. */
typedef struct { /* field names derived */
    char v[0xFC];
} SceLfArgBuf; /* derived name */

/* The loadfile RPC send buffer at lf_buf (0x200 bytes sent, 8 read back). */
typedef struct { /* field names derived */
    int addr;
    int arglen;
    char name[0xFC];
    SceLfArgBuf args;
} SceLfRpcBuf; /* derived name */

int _sceSifLoadModuleBuffer(void *addr, int arglen, int args, void *ret)
{
    SceLfRpcBuf *p;
    int r;

    if (_lf_bind() < 0) {
        return 0xFFFF0000;
    }
    if (_lf_version() != 0) {
        return 0xFFFEFFFC;
    }
    p = (SceLfRpcBuf *)lf_buf;
    ((SceLfRpcBuf *)lf_buf)->addr = (int)addr;
    if (args != 0) {
        if (arglen >= 0xFD) {
            p->args = *(SceLfArgBuf *)args;
            /* this store goes through the buffer itself, not p */
            ((SceLfRpcBuf *)lf_buf)->arglen = 0xFC;
        } else {
            memcpy(&p->args, (char *)args, arglen);
            p->arglen = arglen;
        }
    } else {
        p->arglen = 0;
    }
    if (sceSifCallRpc(&lf_cd, 6, 0, lf_buf, 0x200, lf_buf, 8, 0, 0) < 0) {
        return 0xFFFEFFFF;
    }
    r = ((SceLfRpcBuf *)lf_buf)->addr;
    *(int *)ret = ((SceLfRpcBuf *)lf_buf)->arglen;
    return r;
}

void sceSifLoadModuleBuffer(void *addr, int arglen, int args)
{
    int local[4];
    _sceSifLoadModuleBuffer(addr, arglen, args, &local);
}

int sceSifLoadStartModuleBuffer(void *addr, int arglen, int args, void *ret)
{
    return _sceSifLoadModuleBuffer(addr, arglen, args, ret);
}

int _sceSifLoadModule(void *name, int arglen, int args, int ret, int rpcno)
{
    char *buf;
    int r;

    if (_lf_bind() < 0) {
        return 0xFFFF0000;
    }
    if (_lf_version() != 0) {
        return 0xFFFEFFFC;
    }
    strncpy((lf_buf + 8), (char *)name, 252);
    buf = (lf_buf + 8) - 8;
    buf[0x103] = 0;
    if (args != 0) {
        if (arglen >= 0xFD) {
            *(SceLfArgBuf *)(buf + 0x104) = *(SceLfArgBuf *)args;
            *(int *)lf_buf = 0xFC;
        } else {
            memcpy(buf + 0x104, (char *)args, arglen);
            *(int *)buf = arglen;
        }
    } else {
        buf[0x104] = 0;
        *(int *)buf = 0;
    }
    if (sceSifCallRpc(&lf_cd, rpcno, 0, lf_buf, 0x200, lf_buf, 8, 0, 0) < 0) {
        return 0xFFFEFFFF;
    }
    r = *(int *)(lf_buf + 0);
    *(int *)ret = *(int *)(lf_buf + 4);
    return r;
}

int sceSifLoadModule(void *name, int arglen, int args)
{
    int local;
    return _sceSifLoadModule(name, arglen, args, (int)&local, 0);
}

int sceSifLoadStartModule(void *name, int arglen, int args, int ret)
{
    return _sceSifLoadModule(name, arglen, args, ret, 0);
}

int _sceSifLoadElfPart(void *name, int sec, int out, int rpcno)
{
    char *buf;
    int r;

    if (_lf_bind() < 0) {
        return 0xFFFF0000;
    }
    if (_lf_version() != 0) {
        return 0xFFFEFFFC;
    }
    strncpy((lf_buf + 8), (char *)name, 252);
    buf = (lf_buf + 8) - 8;
    buf[0x103] = 0;
    strncpy((lf_buf + 8) + 252, (char *)sec, 252);
    buf[0x1FF] = 0;
    if (sceSifCallRpc(&lf_cd, rpcno, 0, buf, 0x200, buf, 0x10, 0, 0) < 0) {
        return 0xFFFEFFFF;
    }
    r = *(int *)buf;
    if (r == 0) {
        return 0xFFFEFFFD;
    }
    ((int *)out)[0] = r;
    ((int *)out)[1] = *(int *)(buf + 4);
    return 0;
}

int sceSifLoadElfPart(void *name, int sec, int out)
{
    return _sceSifLoadElfPart(name, sec, out, 1);
}

int sceSifLoadElf(void *name, int out)
{
    return _sceSifLoadElfPart(name, (int)"all", out, 1);
}

int sceSifGetIopAddr(int addr, void *data, int type)
{
    int r;
    if (_lf_bind() < 0) {
        return 0xFFFF0000;
    }
    if ((unsigned int)type >= 3) {
        return 0xFFFEFFFE;
    }
    *(int *)(lf_buf + 0) = addr;
    *(int *)(lf_buf + 4) = type;
    r = sceSifCallRpc(&lf_cd, 3, 0, lf_buf, 0x20, lf_buf, 0x20, 0, 0);
    if (r < 0) {
        return 0xFFFEFFFF;
    }
    if (type == 0) {
        *(unsigned char *)data = *(unsigned char *)lf_buf;
    } else if (type == 1) {
        *(unsigned short *)data = *(unsigned short *)lf_buf;
    } else if (type == 2) {
        *(int *)data = *(int *)lf_buf;
    } else {
        return 0xFFFEFFFE;
    }
    return 0;
}

int sceSifSetIopAddr(int addr, void *data, int type)
{
    if (_lf_bind() < 0) {
        return 0xFFFF0000;
    }
    *(int *)(lf_buf + 0) = addr;
    *(int *)(lf_buf + 4) = type;
    if (type == 0) {
        *(unsigned char *)(lf_buf + 8) = *(unsigned char *)data;
    } else if (type == 1) {
        *(unsigned short *)(lf_buf + 8) = *(unsigned short *)data;
    } else if (type == 2) {
        *(int *)(lf_buf + 8) = *(int *)data;
    } else {
        return 0xFFFEFFFE;
    }
    if (sceSifCallRpc(&lf_cd, 2, 0, lf_buf, 0x20, lf_buf, 0x10, 0, 0) < 0) {
        return 0xFFFEFFFF;
    }
    return 0;
}
