#include "shockdriver.h"
#include <libpad.h>
#include "debug.h"

/* .data, all zero: the voice set manager record and the request pool.
   .sdata, all zero: the current manager, the common and stage voice sets
   charFileManager loads, the manager's two-slot voice set table and the
   request allocator record. */
int ShockDriver[4] = {0};

SHOCKREQUEST ShockRequest[16] = {0};

ShockMgr *System_shock_driver = 0;

ShockVoiceSet *ShockVoiceSetCommon = 0;

ShockVoiceSet *ShockVoiceSetStage = 0;

ShockVoiceSet *ShockVoiceSetBuf[2] = {0};

ShockReqAlloc ShockRequestMemory = {0};

int Vibration_ShotDecode(SHOCKREQUEST *p, void (*callback)(SHOCKREQUEST *req, unsigned char *cmd))
{
    unsigned char *q;
    int ret = 0;
    int n;
    unsigned short pos;
    unsigned char c;

    if ((p->flags & 1) == 0) {
        return 0;
    }
    if (p->shot.buf == 0) {
        p->flags &= 0xFE;
        return 0;
    }
    for (;;) {
        q = p->shot.buf + p->shot.pos;
        c = *q;
        if (p->shot.cnt != 0) {
            p->shot.time = p->shot.time + 1;
            if (c & 0x40) {
                if (p->volume >> 7) {
                    if ((p->volume >> 4) == 0xF) {
                        ret = 1;
                    } else {
                        ret = 0;
                    }
                    if (p->shot.time % ((p->volume >> 4) - 5) != 0) {
                        ret = 1;
                    }
                } else {
                    ret = (p->shot.time % (9 - (p->volume >> 4))) == 0;
                }
            }
            if (p->shot.time >= p->shot.len) {
                p->shot.cnt = 0;
                p->shot.pos = p->shot.pos + 1;
                c = p->shot.buf[p->shot.pos];
                if (c == 0x80) {
                    p->flags &= 0xFE;
                }
            }
            break;
        }

        if (c == 0x80) {
            p->flags &= 0xFE;
            break;
        }
        if (c & 0x80) {
            switch (c & 0x3F) {
            case 0x3F:
                p->shot.pos = p->shot.pos + ((signed char)q[1] + 2);
                if (callback != 0) {
                    callback(p, p->shot.buf + p->shot.pos);
                }
                break;
            case 1:
                p->shot.pos = p->shot.pos + (signed char)q[1];
                break;
            case 2:
                p->shotRep = q[1];
                p->shot.pos = p->shot.pos + 2;
                break;
            case 3:
                pos = p->shot.pos;
                if (p->shotRep != 0) {
                    p->shotRep = p->shotRep - 1;
                    p->shot.pos = pos + (signed char)q[1];
                } else {
                    p->shot.pos = pos + 2;
                }
                break;
            }
        } else {
            n = c & 0x3F;
            p->shot.len = (p->shot.acc + n) * p->timeScale / 64;
            p->shot.prev = p->shot.time;
            p->shot.cnt = p->shot.len - p->shot.time;
            p->shot.acc = p->shot.acc + n;
            if (p->shot.cnt <= 0) {
                p->shot.len = p->shot.time + 1;
                p->shot.cnt = 1;
            }
        }
    }
    return ret;
}

int Vibration_WaveDecode(SHOCKREQUEST *p, void (*callback)(SHOCKREQUEST *req, unsigned char *cmd))
{
    unsigned char *q;
    int sum = 0;
    int ret = 0;
    int num = 0;
    int n;
    unsigned short pos;
    unsigned char c;

    if ((p->flags & 0x10) == 0) {
        return 0;
    }
    if (p->wave.buf == 0) {
        p->flags &= 0xEF;
        return 0;
    }
    for (;;) {
        q = p->wave.buf + p->wave.pos;
        c = *q;
        if (p->wave.cnt != 0) {
            p->wave.time = p->wave.time + 1;
            n = (p->waveFrom * (p->wave.len - p->wave.time) +
                 p->waveTo * (p->wave.time - p->wave.prev)) /
                p->wave.cnt;
            ret = n * p->volume / 255;
            if (p->wave.time >= p->wave.len) {
                p->wave.cnt = 0;
                p->wave.pos = p->wave.pos + 2;
                p->waveFrom = p->waveTo;
                c = p->wave.buf[p->wave.pos];
                if (c == 0x80) {
                    p->flags &= 0xEF;
                }
            }
            break;
        }

        if (c == 0x80) {
            p->flags &= 0xEF;
            break;
        }
        if (c & 0x80) {
            switch (c & 0x3F) {
            case 0x3F:
                p->shot.pos = p->shot.pos + ((signed char)q[1] + 2);
                if (callback != 0) {
                    callback(p, q);
                }
                break;
            case 0:
                p->wave.pos = p->wave.pos + (signed char)q[1];
                break;
            case 1:
                p->waveRep = q[1];
                p->wave.pos = p->wave.pos + 2;
                break;
            case 2:
                pos = p->wave.pos;
                if (p->waveRep != 0) {
                    p->waveRep = p->waveRep - 1;
                    p->wave.pos = pos + (signed char)q[1];
                } else {
                    p->wave.pos = pos + 2;
                }
                break;
            }
        } else {
            n = c & 0x3F;
            p->waveTo = q[1];
            p->wave.prev = p->wave.time;
            p->wave.len = (p->wave.acc + n) * p->timeScale / 64;
            p->wave.cnt = p->wave.len - p->wave.time;
            p->wave.acc = p->wave.acc + n;
            if (p->wave.cnt <= 0) {
                p->wave.cnt = 0;
                p->wave.pos = p->wave.pos + 2;
                num = num + 1;
                sum = sum + p->waveTo;
            } else if (num != 0) {
                p->waveTo = sum / num;
                p->wave.len = p->wave.time + 1;
                p->wave.cnt = 1;
                p->wave.pos = p->wave.pos - 2;
                p->wave.acc = p->wave.acc - n;
                num = 0;
                sum = 0;
            }
        }
    }
    return ret;
}

/* declared void ahead of its definition */
extern void ShockRequestBox_Regst(ShockRequestBox *box, SHOCKREQUEST *req);

/* file-static copies of ShockDriver_GetShockVoiceSet, ShockDriver_GetShockVoice
 * and ShockRequestBox_Request, which Shock_Request inlines */
static inline ShockVoiceSet *getShockVoiceSet(unsigned idx) /* derived name */
{
    if (idx >= (unsigned)System_shock_driver->count)
        return 0;
    return System_shock_driver->arr[idx];
}

static inline int *getShockVoice(int voice, int n) /* derived name */
{
    ShockVoiceSet *set = getShockVoiceSet(voice);
    return (set != 0 && (unsigned)n < set->top.half[4]) ? set->voice + n : 0;
}

static inline SHOCKREQUEST *requestBoxRequest(ShockRequestBox *box, ShockParam *p, ShockParam v,
                                              int key, int arg) /* derived name */
{
    ShockVoiceSet *vs;
    SHOCKREQUEST *req;
    unsigned char *shot;
    unsigned char *wave;
    int t;

    if (System_shock_driver == 0)
        return 0;
    if (box == 0)
        return 0;

    vs = System_shock_driver->arr[v.voice];
    if (vs == 0)
        return 0;

    req = box->alloc(box->pool, arg);
    if (req == 0)
        return 0;

    req->voice = v.voice;
    req->arg = arg;
    req->key = key;

    if (p->voice != 0xFF) {
        shot = (unsigned char *)vs->shot + vs->shot[p->voice];
    } else {
        shot = 0;
    }
    if (p->waveId != 0xFF) {
        wave = (unsigned char *)vs->wave + vs->wave[p->waveId];
    } else {
        wave = 0;
    }
    Vibration_SetDecodeData(req, shot, wave, 255, 64);
    req->waveId = v.waveId;
    t = p->volume * v.volume / 255;
    req->volume = (t < 256) ? t : 255;
    t = p->timeScale * v.timeScale / 64;
    if (t >= 256)
        t = 255;
    req->timeScale = t;
    ShockRequestBox_Regst(box, req);
    return req;
}

SHOCKREQUEST *Shock_Request(ShockRequestBox *box, int voice, ShockParam v, int key, int arg)
{
    ShockParam *p;
    SHOCKREQUEST *req;

    p = (ShockParam *)getShockVoice(v.voice, voice);
    if (p == 0) {
        debug_StdPrintfDummy("voice error? %d\n", v.voice);
        return 0;
    }
    req = requestBoxRequest(box, p, v, key, arg);
    if (req != 0) {
        req->org = p;
    }
    return req;
}

void Shock_SetMotor(int flags, int level, ShockReq *box, int port, int slot)
{
    int n = level & 0xFF;
    int type = flags & 0xFF;
    int cur = box->out;
    int diff = n - cur;
    int r = n;
    short w;
    int sum;
    unsigned int outv;

    if (diff <= 0)
        goto Ldec;
    if (diff >= 41)
        goto Lff;
    if (n >= 61)
        goto L40;
    if (cur >= 61)
        goto L40;
    if (cur >= 51)
        goto Lquad;
Lff:
    r = 0xFF;
    goto Ltail;
Lquad:
    diff = 60 - n;
    diff = (diff * 0xFF) * diff / 100;
    r = (cur < diff) ? diff : n;
    goto Ltail;
L40:
    diff = diff * 0xFF / 40;
    r = (cur < diff) ? diff : n;
    goto Ltail;
Ldec:
    if (diff >= 0)
        goto Ltail;
    if (diff < -30) {
        r = 0;
        goto Ltail;
    }
    diff = diff * 0xFF / 30 + 0xFF;
    r = (diff < cur) ? diff : n;
Ltail:
    n = r & 0xFF;
    sum = box->acc + n;
    box->acc = sum;
    if ((short)sum >= 1035) {
        box->acc = 1034;
    } else if ((short)sum < 0) {
        box->acc = 0;
    }
    w = (short)box->acc;
    outv = ((unsigned int)w << 8) / 1035;
    box->type = type;
    box->acc = (unsigned int)(w * 3) >> 2;
    box->val = n;
    box->out = outv;
    if (port >= 0) {
        scePadSetActDirect(port, slot, &box->type);
    }
}

void Init_ShockVoiceSet(ShockVoiceSet *set, int *data)
{
    set->top.word = data;
    set->voice = data + ((unsigned short *)data)[5];
    set->shot = data + ((unsigned short *)data)[1];
    set->wave = data + ((unsigned short *)data)[3];
}

void Vibration_SetDecodeData(SHOCKREQUEST *req, unsigned char *shot, unsigned char *wave,
                             unsigned char volume, unsigned char timeScale)
{
    req->timeScale = timeScale;
    req->flags = 0x11;
    req->shot.buf = shot;
    req->wave.buf = wave;
    req->volume = volume;
    req->shot.pos = 0;
    req->shot.cnt = 0;
    req->shot.time = 0;
    req->shot.prev = 0;
    req->shot.acc = 0;
    req->shotRep = 0;
    req->wave.pos = 0;
    req->wave.cnt = 0;
    req->wave.time = 0;
    req->wave.prev = 0;
    req->wave.acc = 0;
    req->waveRep = 0;
    req->waveFrom = 0;
}

/* a file-static copy of Init_ShockRequestBox, which Init_Player inlines */
static inline void initShockRequestBox(ShockRequestBox *box,
                                       SHOCKREQUEST *(*alloc)(ShockReqAlloc *pool, int arg),
                                       void (*free)(SHOCKREQUEST *req, ShockReqAlloc *pool),
                                       ShockReqAlloc *pool) /* derived name */
{
    box->head = 0;
    if (alloc) {
        box->alloc = alloc;
    } else {
        box->alloc = dumyAllocFunc;
    }
    box->free = free;
    box->pool = pool;
}

void Init_ShockRequestBox(ShockRequestBox *box,
                          SHOCKREQUEST *(*alloc)(ShockReqAlloc *pool, int arg),
                          void (*free)(SHOCKREQUEST *req, ShockReqAlloc *pool), ShockReqAlloc *pool)
{
    initShockRequestBox(box, alloc, free, pool);
}

void ShockRequestBox_Clear(ShockRequestBox *self)
{
    SHOCKREQUEST *node = self->head;
    if (self->free == 0) {
        goto end;
    }
    if (node == 0) {
        goto end;
    }
    do {
        SHOCKREQUEST *cur = node;
        node = node->next;
        self->free(cur, self->pool);
    } while (node != 0);
end:
    self->head = 0;
}

void ShockRequestBox_Regst(ShockRequestBox *box, SHOCKREQUEST *req)
{
    SHOCKREQUEST *old = box->head;
    req->prev = 0;
    req->next = old;
    if (old != 0) {
        old->prev = req;
    }
    box->head = req;
}

SHOCKREQUEST *ShockRequestBox_Request(ShockRequestBox *box, ShockParam *p, ShockParam v, int key,
                                      int arg)
{
    ShockVoiceSet *vs;
    SHOCKREQUEST *req;
    unsigned char *shot;
    unsigned char *wave;
    int t;

    if (System_shock_driver == 0)
        return 0;
    if (box == 0)
        return 0;

    vs = System_shock_driver->arr[v.voice];
    if (vs == 0)
        return 0;

    req = box->alloc(box->pool, arg);
    if (req == 0)
        return 0;

    req->voice = v.voice;
    req->arg = arg;
    req->key = key;

    if (p->voice != 0xFF) {
        shot = (unsigned char *)vs->shot + vs->shot[p->voice];
    } else {
        shot = 0;
    }
    if (p->waveId != 0xFF) {
        wave = (unsigned char *)vs->wave + vs->wave[p->waveId];
    } else {
        wave = 0;
    }
    Vibration_SetDecodeData(req, shot, wave, 255, 64);
    req->waveId = v.waveId;
    t = p->volume * v.volume / 255;
    req->volume = (t < 256) ? t : 255;
    t = p->timeScale * v.timeScale / 64;
    if (t >= 256)
        t = 255;
    req->timeScale = t;
    ShockRequestBox_Regst(box, req);
    return req;
}

/* a file-static copy of ShockRequestBox_DecodeRequest, which Shock_Decode
 * inlines */
static inline int decodeRequestBox(ShockRequestBox *box, unsigned char *pFlags,
                                   unsigned char *pLevel) /* derived name */
{
    SHOCKREQUEST *p;
    int flags = 0;
    int sum = 0;
    int count;

    if (box == 0) {
        return 0;
    }
    p = box->head;
    count = 0;
    while (p != 0) {
        count++;
        sum += Vibration_WaveDecode(p, System_shock_driver->callback);
        flags |= Vibration_ShotDecode(p, System_shock_driver->callback);
        p = p->next;
    }
    count |= sum << 16;
    if (sum >= 256) {
        sum = 255;
    }
    *pLevel = sum;
    *pFlags = flags;
    ShockRequestBox_EndRequestFree(box);
    return count;
}

int ShockRequestBox_DecodeRequest(ShockRequestBox *box, unsigned char *pFlags,
                                  unsigned char *pLevel)
{
    return decodeRequestBox(box, pFlags, pLevel);
}

static inline SHOCKREQUEST *requestFree(ShockRequestBox *box, SHOCKREQUEST *req);

SHOCKREQUEST *ShockRequestBox_EndRequestFree(ShockRequestBox *box)
{
    SHOCKREQUEST *p;
    unsigned char b;
    if (box != 0) {
        p = box->head;
        if (p != 0) {
            do {
                b = p->flags;
                if (b == 0)
                    p = requestFree(box, p);
                else
                    p = p->next;
            } while (p != 0);
        }
    }
    return box->head;
}

static inline SHOCKREQUEST *requestFree(ShockRequestBox *box, SHOCKREQUEST *req)
{
    SHOCKREQUEST *p;

    if (req->prev != 0) {
        req->prev->next = req->next;
    } else {
        box->head = req->next;
    }
    if (req->next != 0) {
        req->next->prev = req->prev;
    }
    p = req;
    req = req->next;
    if (box->free != 0) {
        box->free(p, box->pool);
    }
    return req;
}

SHOCKREQUEST *ShockRequestBox_VoiceSetUseRequestFree(ShockRequestBox *box, int voice)
{
    SHOCKREQUEST *p;
    if (box != 0) {
        p = box->head;
        if (p != 0) {
            do {
                if (p->voice == voice) {
                    p = requestFree(box, p);
                } else {
                    p = p->next;
                }
            } while (p != 0);
        }
    }
    return box->head;
}

SHOCKREQUEST *ShockRequestBox_GetRequest(ShockRequestBox *box, int key)
{
    SHOCKREQUEST *p;
    if (box == 0)
        goto fail;
    p = box->head;
    if (p == 0)
        goto fail;
    do {
        if (p->key == key) {
            return p;
        }
        p = p->next;
    } while (p != 0);
fail:
    return 0;
}

int ShockRequestBox_RequestCancel(ShockRequestBox *box, int key)
{
    SHOCKREQUEST *node;
    SHOCKREQUEST *next;
    SHOCKREQUEST *prev;
    void (*fn)(SHOCKREQUEST *req, ShockReqAlloc *pool);
    node = ShockRequestBox_GetRequest(box, key);
    if (node == 0) {
        return 0;
    }
    prev = node->prev;
    if (prev != 0) {
        prev->next = node->next;
        next = node->next;
    } else {
        next = node->next;
        box->head = next;
    }
    if (next != 0) {
        next->prev = node->prev;
    }
    fn = box->free;
    if (fn != 0) {
        fn(node, box->pool);
    }
    return 1;
}

int ShockRequestBox_RequestDirectCancel(ShockRequestBox *box, SHOCKREQUEST *req)
{
    SHOCKREQUEST *next;
    SHOCKREQUEST *prev;
    void (*fn)(SHOCKREQUEST *req, ShockReqAlloc *pool);
    if (req == 0) {
        return 0;
    }
    prev = req->prev;
    if (prev != 0) {
        prev->next = req->next;
        next = req->next;
    } else {
        next = req->next;
        box->head = next;
    }
    if (next != 0) {
        next->prev = req->prev;
    }
    fn = box->free;
    if (fn != 0) {
        fn(req, box->pool);
    }
    return 1;
}

/* the driver manager's setup, which Init_ShockDriver and Init_Shock
   inline; after the guards the manager is reached through the global it
   has just been stored in */
static inline void initShockDriver(ShockMgr *m, ShockVoiceSet **arr, int num) /* derived name */
{
    int i;
    if (m == 0)
        return;
    if (arr == 0)
        return;
    System_shock_driver = m;
    System_shock_driver->count = num;
    System_shock_driver->arr = arr;
    for (i = 0; i < num; i++)
        System_shock_driver->arr[i] = 0;
    System_shock_driver->callback = 0;
}

void Init_ShockDriver(ShockMgr *m, ShockVoiceSet **arr, int num)
{
    initShockDriver(m, arr, num);
}

int ShockDriver_VoiceSet_NumberRegist(unsigned int idx, ShockVoiceSet *val)
{
    ShockMgr *m = System_shock_driver;
    if (idx >= (unsigned int)m->count)
        return -1;
    m->arr[idx] = val;
    return idx;
}

int ShockDriver_VoiceSet_Regist(ShockVoiceSet *value)
{
    int i;
    for (i = 0; i < System_shock_driver->count; i++) {
        if (System_shock_driver->arr[i] == 0)
            break;
    }
    if (i == System_shock_driver->count)
        return -1;
    System_shock_driver->arr[i] = value;
    return i;
}

int ShockDriver_VoiceSet_Remove(unsigned int idx)
{
    ShockMgr *m = System_shock_driver;
    if (idx >= (unsigned int)m->count)
        return -1;
    m->arr[idx] = 0;
    return idx;
}

int ShockDriver_GetShockVoiceMax(int idx)
{
    int p; /* the set, then its image: one register in the ROM */
    if ((unsigned int)idx < (unsigned int)System_shock_driver->count) {
        goto body;
    }
    p = 0;
    goto check;
body:
    /* PC port: the set and its image are pointers, which an int truncates on
       x64; the same reads, typed (no caller in the game) */
    {
        ShockVoiceSet *s = System_shock_driver->arr[idx];

        if (s != 0) {
            return s->top.half[4];
        }
        return 0;
    }
check:
    if (p != 0) {
        p = *(int *)p;
        return *(unsigned short *)(p + 8);
    }
    return 0;
}

ShockVoiceSet *ShockDriver_GetShockVoiceSet(unsigned idx)
{
    ShockMgr *m = System_shock_driver;
    if (idx >= (unsigned)m->count)
        return 0;
    return m->arr[idx];
}

int *ShockDriver_GetShockVoice(int idx, int n)
{
    ShockVoiceSet *p;
    if ((unsigned int)idx < (unsigned int)System_shock_driver->count) {
        goto body;
    }
    p = 0;
    goto check;
body:
    p = System_shock_driver->arr[idx];
check:
    if (p == 0) {
        goto ret_a;
    }
    if ((unsigned int)n >= (unsigned int)p->top.half[4]) {
        goto ret_b;
    }
    return p->voice + n;
ret_b:
    return 0;
ret_a:
    return 0;
}

void Init_ShockEmulator(short *emu)
{
    emu[1] = 0;
    emu[0] = 0;
}

int ShockEmulator_EmulationShot(int emu, int level)
{
    return level;
}

unsigned short ShockEmulator_EmulationWave(short *emu, int level)
{
    int sum = (unsigned short)emu[1] + level;
    unsigned int q;
    short w;
    emu[1] = sum;
    if ((short)sum >= 1035)
        emu[1] = 1034;
    else if ((short)sum < 0)
        emu[1] = 0;
    w = emu[1];
    q = ((unsigned int)(w << 8)) / 1035;
    emu[0] = q;
    emu[1] = ((unsigned int)(w * 3)) >> 2;
    return (unsigned short)emu[0];
}

/* the request pool's setup, which Init_ShockRequestAlloc and Init_Shock
   inline */
static inline void initShockRequestAlloc(ShockReqAlloc *alloc, SHOCKREQUEST *buf,
                                         int num) /* derived name */
{
    int i;
    if (alloc != 0 && buf != 0) {
        alloc->num = num;
        alloc->buf = buf;
        for (i = 0; i < num; i++) {
            buf[i].flags = 0;
        }
    } else {
        alloc->num = 0;
    }
}

void Init_ShockRequestAlloc(ShockReqAlloc *alloc, SHOCKREQUEST *buf, int num)
{
    initShockRequestAlloc(alloc, buf, num);
}

SHOCKREQUEST *Get_ShockRequestStruct(ShockReqAlloc *pool, int arg)
{
    SHOCKREQUEST *p = pool->buf;
    int i;
    for (i = 0; i < pool->num; i++) {
        if (p->flags == 0) {
            return p;
        }
        p++;
    }
    return 0;
}

void Reset_ShockRequestStruct(SHOCKREQUEST *req, ShockReqAlloc *pool)
{
    req->flags = 0;
}

int ShockRevice_Wave(int level, int cur)
{
    int diff = level - cur;

    if (diff <= 0)
        goto Ldec;
    if (diff >= 41)
        goto Lff;
    if (level >= 61)
        goto L40;
    if (cur >= 61)
        goto L40;
    if (cur >= 51)
        goto Lquad;
Lff:
    level = 0xFF;
    goto Lend;
Lquad:
    diff = 60 - level;
    diff = (diff * 0xFF) * diff / 100;
    level = (cur < diff) ? diff : level;
    goto Lend;
L40:
    diff = diff * 0xFF / 40;
    level = (cur < diff) ? diff : level;
    goto Lend;
Ldec:
    if (diff >= 0)
        goto Lend;
    if (diff < -30) {
        level = 0;
        goto Lend;
    }
    diff = diff * 0xFF / 30 + 0xFF;
    level = (diff < cur) ? diff : level;
Lend:
    return level;
}

/* ShockDriver's four words hold a ShockMgr only with 4-byte pointers */
static ShockMgr shockDriverHost; /* derived name */

void Init_Shock(void)
{
    initShockDriver(&shockDriverHost, ShockVoiceSetBuf, 2);
    initShockRequestAlloc(&ShockRequestMemory, ShockRequest, 16);
}

int Shock_SetShockVoiceSet(int idx, ShockVoiceSet *val)
{
    ShockMgr *m = System_shock_driver;
    ShockVoiceSet **array;
    if ((unsigned int)idx < (unsigned int)m->count)
        goto store;
    idx = -1;
    goto end;
store:
    array = m->arr;
    array[idx] = val;
end:
    return idx;
}

void Init_Player(ShockRequestBox *box)
{
    initShockRequestBox(box, Get_ShockRequestStruct, Reset_ShockRequestStruct, &ShockRequestMemory);
}

void Init_Controler(ShockReq *motor)
{
    motor->acc = 0;
    motor->out = 0;
}

void Shock_RequestClear(ShockRequestBox *self)
{
    SHOCKREQUEST *node = self->head;
    if (self->free == 0) {
        goto end;
    }
    if (node == 0) {
        goto end;
    }
    do {
        SHOCKREQUEST *cur = node;
        node = node->next;
        self->free(cur, self->pool);
    } while (node != 0);
end:
    self->head = 0;
}

void Shock_Decode(ShockRequestBox *box, unsigned char *pFlags, unsigned char *pLevel)
{
    decodeRequestBox(box, pFlags, pLevel);
}

SHOCKREQUEST *dumyAllocFunc(ShockReqAlloc *pool, int arg)
{
    return 0;
}

void Vibration_SetDecodeEnd(unsigned char *p, int shotEnd, int waveEnd)
{
    if (shotEnd)
        *p &= 0xFE;
    if (waveEnd)
        *p &= 0xEF;
}
