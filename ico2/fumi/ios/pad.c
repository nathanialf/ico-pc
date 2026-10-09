#include "debug.h"
#include "pad.h"
#include "message.h"
#include "shockdriver.h"
#include "Matrix.h"
#include <libvu0.h>
#include <string.h>
#include "typedef.h"
#include "ios.h"
#include "debug_exception.h"
#include "matrixDrive.h"
#include "thread.h"
#include <libpad.h>
#include "gv.h"
#include "main.h"
#include <assert.h>

/* the terminal id the reconnect check compares against, which nothing in the
   retail build writes, and the enable flag iosPadEnable and iosPadDisable
   set */
static int padTermId; /* derived name */

static int padEnabled; /* derived name */

/* .data: the default and custom configurations with the scePadGetState name
   table between them, the two device records, the manager thread record and
   its queue.
   .sdata: iosPadActRequestEnable, the state names of five bytes or fewer
   (emitted with the table, last entry first, as are the two longer ones in
   .rodata), the literals of controler_stable_check and the reads in
   first-use order, and the vibration request key. */
PadConf iosPadConfDefault = {
    {{5, 120},
     {5, 120},
     {5, 120},
     {5, 120},
     {5, 120},
     {5, 120},
     {5, 120},
     {5, 120},
     {5, 120},
     {5, 120},
     {5, 120},
     {5, 120}},
    {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5},
    {{30, 5},
     {30, 5},
     {30, 5},
     {30, 5},
     {30, 5},
     {30, 5},
     {30, 5},
     {30, 5},
     {30, 5},
     {30, 5},
     {30, 5},
     {30, 5},
     {30, 5},
     {30, 5},
     {30, 5},
     {30, 5}},
    {0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080, 0x0100, 0x0200, 0x0400, 0x0800,
     0x1000, 0x2000, 0x4000, 0x8000},
};

int iosPadActRequestEnable = 1;

static char *padStateName[8] = {/* derived name */
                                "DISCONNECT", "FINDPAD", "FINDCTP1", "",
                                "",           "EXECCMD", "STABLE",   "ERROR"};

PadConf iosPadConfCustom = {0};

IosPadDevRec iosPadDev[2] = {0};

IOSThread th_iosPadDevManager = {{0}};

IosMsgQueue padDevMgrMsgQ = {0};

static int controler_stable_check(IosPadDevRec *dev)
{
    int port = dev->port;
    int slot = dev->slot;
    int prev = dev->state;
    int phase = dev->phase;
    int cnt = dev->errCount;
    int id = dev->lastTermId;
    int state;
    int mode;
    int orig;
    int exid;
    int i;

    state = scePadGetState(port, slot);
    if ((unsigned int)state < 8) {
        if (state != prev) {
            debug_StdPrintfDummy("pad:port:%d slot:%d %s\n", port, slot, padStateName[state]);
        }
    } else {
        debug_StdPrintfDummy("pad:?%d\n", state);
    }
    if (state == 0) {
        cnt = 0;
        phase = 0;
    }
    if (port == 0 && slot == 0) {
        debug_StdPrintfDummy("phase %d\n", phase);
    }
    switch (phase) {
    case 0:
        dev->flags &= ~0x40000;
        phase++;
    case 1:
        if (state == 0) {
            dev->flags |= 0x10000;
        }
        if (state != 6 && state != 2) {
            dev->error = 1;
            break;
        }
        dev->flags &= ~0x10000;
        mode = scePadInfoMode(port, slot, 1, 0);
        orig = mode;
        debug_StdPrintfDummy("pad id:%d\n", mode);
        debug_StdPrintfDummy("pad id:%d\n", mode);
        if (mode == 0) {
            break;
        }
        exid = scePadInfoMode(port, slot, 2, 0);
        debug_StdPrintfDummy("pad: exid %d\n", exid);
        debug_StdPrintfDummy("pad: exid %d\n", exid);
        if (exid >= 1) {
            mode = exid;
        }
        switch (mode) {
        default:
            debug_StdPrintfDummy("pad:default 0x%x\n", mode);
            phase = 99;
            break;
        case 4:
            debug_StdPrintfDummy("pad:4\n");
            phase = 40;
            if (orig != mode) {
                phase = 30;
            } else {
                dev->flags |= 0x40000;
            }
            break;
        case 7:
            debug_StdPrintfDummy("pad:7\n");
            phase = 70;
            if (((int)(dev->flags >> 18) & 1) == 0) {
                phase = 0;
            }
            break;
        }
        debug_StdPrintfDummy("pad:%03x\n", mode);
        break;
    case 30:
        if (scePadSetMainMode(port, slot, 0, 2) == 1) {
            phase++;
        }
        break;
    case 31:
        debug_StdPrintfDummy("pad:31\n");
        if (scePadGetReqState(port, slot) == 1) {
            phase--;
        }
        if (scePadGetReqState(port, slot) != 0) {
            break;
        }
        phase = 0;
        debug_StdPrintfDummy("pad:switch to ANALOG mode\n");
        break;
    case 40:
        if (scePadInfoMode(port, slot, 4, -1) == 0) {
            phase = 99;
            break;
        }
        phase++;
    case 41:
        debug_StdPrintfDummy("pad:41\n");
        if (scePadSetMainMode(port, slot, 1, 3) == 1) {
            phase++;
        }
        break;
    case 42:
        debug_StdPrintfDummy("pad:42\n");
        if (scePadGetReqState(port, slot) == 1) {
            phase--;
        }
        if (scePadGetReqState(port, slot) != 0) {
            break;
        }
        phase = 1;
        debug_StdPrintfDummy("pad:switch to ANALOG mode\n");
        break;
    case 70:
        debug_StdPrintfDummy("pad:70\n");
        if (scePadInfoPressMode(port, slot) == 1) {
            phase++;
        } else {
            phase = 75;
        }
        break;
    case 71:
        debug_StdPrintfDummy("pad:71\n");
        if (scePadEnterPressMode(port, slot) == 1) {
            phase++;
        }
        break;
    case 72:
        debug_StdPrintfDummy("pad:72\n");
        if (scePadGetReqState(port, slot) == 1) {
            phase--;
        }
        if (scePadGetReqState(port, slot) != 0) {
            break;
        }
        phase = 75;
        debug_StdPrintfDummy("pad:switch to PRESSURE SENSE mode\n");
        break;
    case 75:
        debug_StdPrintfDummy("pad:75\n");
        if (scePadInfoAct(port, slot, -1, 0) == 0) {
            phase = 99;
        }
        dev->act[0] = 0;
        dev->act[1] = 1;
        for (i = 2; i < 6; i++) {
            dev->act[i] = 255;
        }
        if (scePadSetActAlign(port, slot, dev->act) != 0) {
            phase++;
        }
        break;
    case 76:
        debug_StdPrintfDummy("pad:76\n");
        if (scePadGetState(port, slot) != 5) {
            phase = 99;
        }
        break;
    default:
        if (state == 7) {
            cnt++;
            dev->state = prev;
            dev->phase = -1;
            dev->errCount = cnt;
            dev->lastTermId = id;
            debug_assert(__FILE__, 468);
            __assert(__FILE__, 468, "0");
            return -1;
        }
        if (state == 6 || state == 2) {
            /* the line after assigns id again; the object keeps this arm's
               own copy of the assignment */
            if (id != 0 && padTermId != id) {
                phase = 0;
                id = padTermId;
            }
            dev->termId = id = padTermId;
        }
        break;
    }
    dev->state = state;
    dev->phase = phase;
    dev->errCount = cnt;
    dev->lastTermId = id;
    return phase;
}

static void iosPadDevManager(void);

int iosPadDevInit(void *desc)
{
    int i;

    iosThreadCreateS(&th_iosPadDevManager, 10, iosPadDevManager, 0, ios_partition_root, 4096, 17);
    iosThreadStart(&th_iosPadDevManager);

    if (scePadInit(0) != 1) {
        debug_StdPrintfDummy("pad:init error\n");
        debug_assert(__FILE__, 553);
        __assert(__FILE__, 553, "0");
        return 0;
    }
    for (i = 0; i < 2; i++) {
        IosPadDevRec *dev = &iosPadDev[i];

        dev->port = i;
        dev->slot = 0;
        dev->phase = 0;
        dev->error = 0xFFFFFFFFu;
        dev->state = 0xFFFF;
        if (scePadPortOpen(i, 0, dev->dmaBuf) == 0) {
            debug_StdPrintfDummy("ERROR: scePadPortOpen port%d slot%d\n", i, 0);
            debug_assert(__FILE__, 569);
            __assert(__FILE__, 569, "0");
        }
    }
    iosPadConfCustom = iosPadConfDefault;
    return 1;
}

static void iosPadActTickProc(void);

static int iosPadDevReadFunc(void)
{
    int i;

    for (i = 0; i < 2; i++) {
        IosPadDevRec *dev = &iosPadDev[i];
        IosPadShock *sh;
        int port;

        dev->idx ^= 1;
        if (dev->phase == 99) {
            dev->error = 0;
        }
        if (dev->error != 0) {
            controler_stable_check(dev);
            if (((unsigned int)frame_count) % 120 == 0) {
                debug_StdPrintfDummy("pad:checking controler... ");
                debug_StdPrintfDummy("port:%d, slot:%d\n", dev->port, dev->slot);
            }
        } else {
            if (scePadRead(dev->port, dev->slot, &dev->buf[dev->idx]) == 0) {
                debug_StdPrintfDummy("err %d\n", scePadGetState(dev->port, dev->slot));
                if (scePadGetState(dev->port, dev->slot) == 0) {
                    dev->phase = 0;
                    dev->error = 1;
                }
            } else {
                IosPadBuf *b = &dev->buf[dev->idx];
                /* bits 12-15 of the buffer's first word: termId's high nibble */
                unsigned int t = b->termId >> 4;

                if (t != 7 && t != 5) {
                    b->rx = b->ry = b->lx = b->ly = 127;
                }
            }
        }
        sh = &dev->shock;
        port = dev->port;
        Shock_Decode(&sh->box, &dev->shock.motor0, &dev->shock.motor1);
        if (((*(unsigned int *)((char *)dev + (dev->idx << 5) + 0x10) >> 12) & 0xF) != 7 ||
            dev->error != 0) {
            port = -1;
        }
        Shock_SetMotor(sh->motor0, sh->motor1, &dev->shock.motor, port, dev->slot);
    }
    iosPadActTickProc();
    return 0;
}

/* PC port (issue 46): port/input/pad_host.c */
unsigned int ico_pad_menu_word(const void *libpad_buf);

int iosPadRead(IosPadCtx *ctx)
{
    IosPadDevRec *dev = ctx->dev;
    IosPadBuf *prev;
    IosPadBuf *cur;
    int pbits;
    int cbits;
    int old;
    int now;
    int i;

    prev = &dev->buf[dev->idx ^ 1];
    cur = &dev->buf[dev->idx];

    pbits = ((prev->hi << 8) | prev->lo) ^ 0xFFFF;
    cbits = ((cur->hi << 8) | cur->lo) ^ 0xFFFF;

    /* PC port (issue 46): the default table is the menus' view of the pad:
       bytes 20-21 of the read buffer, where the port keeps the gamepad's face
       buttons and d-pad by position whatever Remap controls says. Readers
       of pad[0] in play see this view, as with the game's own Button
       configuration: a shoulder moved onto a face button still acts as the
       shoulder there (Yorda's escape-mode check of R1, girl_act.c) */
    if (ctx->conf == &iosPadConfDefault) {
        pbits = ico_pad_menu_word(prev);
        cbits = ico_pad_menu_word(cur);
    }

    old = 0;
    now = 0;

    for (i = 0; i < 16; i++) {
        if ((pbits >> i) & 1) {
            old |= ctx->conf->bit[i];
        }
        if ((cbits >> i) & 1) {
            now |= ctx->conf->bit[i];
        }
    }

    ctx->now = now;
    ctx->trg = (now ^ old) & now;
    ctx->rel = (now ^ old) & old;

    ctx->now2 = now;
    ctx->trg2 = ctx->trg;
    ctx->rel2 = ctx->rel;
    ctx->word24 = ctx->word14;

    if (dev->error != 0) {
        ctx->now = 0;
        ctx->trg = 0;
        ctx->rel = 0;
        ctx->word14 = 0;

        ctx->now2 = 0;
        ctx->trg2 = 0;
        ctx->rel2 = 0;
        ctx->word24 = 0;
        return 0;
    }
    if (padEnabled == 0) {
        int mask = debug_hand_camera != 0 ? 2 : 0;
        ctx->now &= mask;
        ctx->trg &= mask;
        ctx->rel &= mask;
        ctx->word14 &= mask;
    }
    return 0;
}

float iosPadNormalizeStick(IosPadStick *st)
{
    Vec4 v = {{(float)st->x - 127.5f, 0.0f, (float)st->y - 127.5f, 0.0f}};
    float len;

    len = FSqrt(v.f[0] * v.f[0] + v.f[2] * v.f[2]);

    if (len <= 48.0f) {
        st->dx = 0.0f;
        st->dz = 0.0f;
        return st->dx;
    }
    {
        Vec4 n;

        sceVu0Normalize(&n, &v);
        st->dx = n.f[0];
        st->dz = n.f[2];
    }
    if (48.0f < len) {
        int deg = (int)(_GetDirection(v.f) / 3.14159274f * 180.0f);
        int m;
        int d;

        m = (deg < 0 ? -deg : deg) % 90;
        d = m < 46 ? m : 90 - m;
        len = len / ((float)d * 0.2f / 45.0f + 1.0f);
        if (len < 48.0f) {
            len = 48.0f;
        }
    }
    if (120.0f <= len) {
        return 1.0f;
    }
    return (len - 48.0f) / 72.0f;
}

static int iosPadGetStick_func(IosPadCtx *ctx, IosPadStick *st, int mode, int a3, int a4,
                               int simulate)
{
    IosPadDevRec *rec = ctx->dev;
    IosPadBuf *buf = &rec->buf[rec->idx];
    float ox;
    float oy;

    if (rec->error != 0) {
        st->x = 127;
        st->y = 127;
        st->mag = 0.0f;
        return 0;
    }
    switch (mode) {
    case 1:
        st->x = buf->rx;
        st->y = buf->ry;
        st->mag = iosPadNormalizeStick(st);
        break;
    case 0:
        ox = (float)st->x;
        oy = (float)st->y;

        st->x = buf->lx;
        st->y = buf->ly;
        st->mag = iosPadNormalizeStick(st);

        if (st->mag == 0.0f && simulate != 0) {
            st->x = (int)ox;
            st->y = (int)oy;

            if (ctx->now & 0x2000) {
                st->x = (int)((float)(st->x + 255) * 0.5f);
            } else if (ctx->now & 0x8000) {
                st->x = (int)((float)st->x * 0.5f);
            } else {
                st->x = (int)((float)(st->x + 127) * 0.5f);
            }
            if (ctx->now & 0x1000) {
                st->y = (int)((float)st->y * 0.5f);
            } else if (ctx->now & 0x4000) {
                st->y = (int)((float)(st->y + 255) * 0.5f);
            } else {
                st->y = (int)((float)(st->y + 127) * 0.5f);
            }
            st->mag = iosPadNormalizeStick(st);
        }
        break;
    }
    return 0;
}

/* the device manager's message queue buffer and the sixteen actuator
   requests iosPadActRequest hands out */
static IosMsgWord padDevMgrMsgBuf[8]; /* derived name */

static PadAct padActs[16]; /* derived name */

static int padActKey = 1; /* derived name */

int iosPadActRequest(IosPadCtx *pad, int id)
{
    PadAct *p = padActs;
    PadAct *entry;
    int i = 15;

    while (1) {
        if (p->key == 0) {
            goto found;
        }
        i--;
        if (i == -1) {
            goto notfound;
        }
        p++;
    }
notfound:
    entry = 0;
    goto go;
found:
    entry = p;
go:
    if (pad == 0 || iosPadActRequestEnable == 0 || entry == 0) {
        return 0;
    }
    entry->voice = shockList[id].voice;
    entry->life = shockList[id].life;
    entry->tick = 0;
    entry->box = &pad->dev->shock.box;
    entry->prm.voice = entry->prm.waveId = 0;
    entry->prm.volume = 255;
    entry->volume = 255;
    entry->prm.timeScale = 32;
    if (Shock_Request(entry->box, entry->voice, entry->prm, padActKey, 0) == 0) {
        return 0;
    }
    entry->key = padActKey++;
    if (padActKey == 0) {
        padActKey = 1;
    }
    return entry->key;
}

int iosPadDevRead(void)
{
    iosMsgSend(&padDevMgrMsgQ, 0, 0);
    return 0;
}

int iosPadGetPort(int a0, int dev)
{
    return iosPadDev[dev].port;
}

int iosPadGetSlot(int a0, int dev)
{
    return iosPadDev[dev].slot;
}

int iosPadGetDevice(int port, int slot)
{
    int *p = (int *)iosPadDev;
    int count = 0;
    do {
        count++;
        if (p[0] == port) {
            if (p[1] == slot) {
                return p[2];
            }
        }
        p = (int *)((char *)p + 0x200);
    } while (count < 2);
    return -1;
}

int iosPadConnect(IosPadCtx *ctx, int a1, int port, PadConf *conf)
{
    ctx->conf = conf;
    ctx->dev = &iosPadDev[port];
    return 0;
}

int iosPadGetStick(IosPadCtx *ctx, void *st, int mode, int a3, int a4, int simulate)
{
    int rv;
    _PushVu0Registers();
    rv = iosPadGetStick_func(ctx, st, mode, a3, a4, simulate);
    _PopVu0Registers();
    return rv;
}

void iosPadStickCameraCoord(void *out, IosPadStick *stick)
{
    Vec4 v = {{stick->dx, 0.0f, -stick->dz, 0.0f}};
    float m[16];
    sceVu0TransposeMatrix(m, (void *)(matrixptr + 0x80));
    sceVu0ApplyMatrix(out, m, &v);
}

void iosPadEnable(void)
{
    padEnabled = 1;
}

void iosPadDisable(void)
{
    padEnabled = 0;
}

int iosPadEnableGet(void)
{
    return padEnabled;
}

void iosPadActInit(void)
{
    IosPadDevRec *dev;
    int i;
    memset(padActs, 0, sizeof(padActs));
    Init_Shock();
    Shock_SetShockVoiceSet(0, ShockVoiceSetCommon);
    dev = iosPadDev;
    for (i = 0; i < 2; i++) {
        Init_Controler(&dev[i].shock.motor);
        Init_Player(&dev[i].shock.box);
    }
}

void iosPadActStop(int key)
{
    if (key == 0) {
        return;
    }
    for (;;) {
        PadAct *p = padActs;
        PadAct *entry;
        int i = 15;
        while (1) {
            if (p->key == key) {
                goto found;
            }
            i--;
            if (i == -1) {
                goto notfound;
            }
            p++;
        }
    notfound:
        entry = 0;
        goto check;
    found:
        entry = p;
    check:
        if (entry == 0) {
            break;
        }
        ShockRequestBox_RequestCancel(entry->box, key);
        entry->key = 0;
    }
}

void iosPadActStopAll(void)
{
    PadAct *p = padActs;
    int i;
    for (i = 15; i != -1; i--) {
        int key = p->key;
        if (key != 0) {
            ShockRequestBox_RequestCancel(p->box, key);
            p->key = 0;
        }
        p++;
    }
}

PadAct *iosPadActVolumeSet(int key, unsigned int val)
{
    PadAct *p = padActs;
    PadAct *rv;
    int i;
    val = val & 0xFF;
    i = 15;
    while (1) {
        if (p->key == key)
            goto found;
        i--;
        if (i == -1)
            goto notfound;
        p++;
    }
notfound:
    rv = 0;
    goto end;
found:
    rv = p;
end:
    if (rv != 0) {
        rv->volume = val;
    }
    return rv;
}

static void iosPadDevManager(void)
{
    IosMsgWord local_buf;
    iosMsgQueueCreate(&padDevMgrMsgQ, padDevMgrMsgBuf, 8);
    while (1) {
        iosMsgRecv(&padDevMgrMsgQ, &local_buf, 1);
        iosPadDevReadFunc();
    }
}

static inline void setRequestVolume(SHOCKREQUEST *req, unsigned int volume) /* derived name */
{
    unsigned int v;
    v = volume * req->org->volume / 255;
    if (v > 255) {
        v = 255;
    }
    req->volume = v;
}

static void iosPadActTickProc(void)
{
    PadAct *p = padActs;
    int i;
    for (i = 0xF; i != -1; i--) {
        if (p->key != 0) {
            SHOCKREQUEST *req = ShockRequestBox_GetRequest(p->box, p->key);
            if (req == 0) {
                p->tick++;
                if (p->life == 0 || p->tick < p->life) {
                    Shock_Request(p->box, p->voice, p->prm, p->key, 0);
                } else {
                    p->key = 0;
                }
            } else {
                setRequestVolume(req, p->volume);
            }
        }
        p++;
    }
}

void iosPadDisconWait(void) {}

void iosPadErrorWait(void) {}
