#include "typedef.h"
#include "debug.h"
#include "debug_exception.h"
#include "memory.h"
#include "lws_kyomi.h"
#include "Basic.h"
#include "Light.h"
#include "geometryManager.h"
#include "matrixDrive.h"
#include "particleEffect.h"
#include "quaternion.h"
#include "tableSin.h"
#include <stdio.h>
#include <string.h>
#include "GsBase.h"
#include "ios.h"
#include "main.h"
#include "Matrix.h"
#include "enemy_act.h"
#include "gobj.h"
#include "BgAnimation.h"
#include "lightning.h"
#include "gamesys.h"
#include <assert.h>

/* bgaAnimDefault is the 0x30-byte default record
   bga_InitData block-copies into its mallocseki() allocation (two
   (0,0,0,1.0f) vectors then four words); bgaParticlePos is the (0,0,0,1.0f)
   position vector bga_ApplyDObject hands to
   SetParticleEffectActiveSensing. */
static BgaAnim bgaAnimDefault = {
    /* derived name */
    {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, 0, -1, 1, 0,
};

static float bgaParticlePos[4] = {0.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

/* A node's object word (BgaDObjEnt.u) as a pointer and back.  On the host
   the word is an arena offset (eeword.h), except for bgaDummyLight, a static
   outside the arena that light objects take while the stage is not lit
   through Light.c: it has a word of its own, BGA_DUMMY_WORD. */
#ifdef ICO_HOST

static IcoEEWord bga_objWord(void *p);
static void *bga_objPtr(IcoEEWord w);

#define BGA_OBJ(T, w) ((T)bga_objPtr(w))
#define BGA_OBJW(p) bga_objWord(p)
#else
#define BGA_OBJ(T, w) ((T)(w))
#define BGA_OBJW(p) (p)
#endif

/* The six words the file's functions share.  bgaStreamSync is read and
   cleared by streamMotionManager.c (_infoUpdate), so it is global. */
int bgaStreamSync = 0; /* derived name */

static int bgaFrame = 0; /* derived name */

static float bgaZoom = 0.0f; /* derived name */

static int bgaUniqAnimationFlag = 1; /* derived name */

static int bgaCameraForceOff = 0; /* derived name */

static struct BgaLightning *bgaLightningList = 0; /* derived name */

/* Four static helpers bga_InitData inlines. */

static inline void bga_addSiblingTail(BgaDObjEnt *c, BgaDObjEnt *d) /* derived name */
{
    while (c->sibling != 0) {
        c = ICO_EEPTR(BgaDObjEnt *, c->sibling);
    }
    c->sibling = BGA_W(d);
}

static inline void bga_linkToParent(BgaDObjEnt *q, BgaDObjEnt *d, int no) /* derived name */
{
    do {
        if (q->num == no) {
            if (q->child == 0) {
                q->child = BGA_W(d);
            } else {
                bga_addSiblingTail(ICO_EEPTR(BgaDObjEnt *, q->child), d);
            }
        }
        if (q->u.next == 0) {
            break;
        }
        q = ICO_EEPTR(BgaDObjEnt *, q->u.next);
    } while (1);
}

static inline void bga_linkTree(BgaHeader *p) /* derived name */
{
    BgaDObjEnt *d;
    int no;

    d = ICO_EEPTR(BgaDObjEnt *, p->dobjs);
    do {
        no = d->parent;
        if (no != -1) {
            bga_linkToParent(ICO_EEPTR(BgaDObjEnt *, p->dobjs), d, no);
        }
        if (d->u.next == 0) {
            break;
        }
        d = ICO_EEPTR(BgaDObjEnt *, d->u.next);
    } while (1);
}

static inline void bga_makeRootList(BgaHeader *p) /* derived name */
{
    BgaDObjEnt *d;
    int n;

    d = ICO_EEPTR(BgaDObjEnt *, p->dobjs);
    n = 0;
    do {
        if (d->parent == -1) {
            n++;
        }
    } while ((d = ICO_EEPTR(BgaDObjEnt *, d->u.next)) != 0);

    /* the list is written as int words, which may alias the int p->dobjs, so
       the ROM reloads it after them; pointer stores would not (measured) */
    p->roots = BGA_W(mallocseki((n + 1) * 4));
    ICO_EEPTR(int *, p->roots)[n] = 0;
    d = ICO_EEPTR(BgaDObjEnt *, p->dobjs);
    n = 0;
    do {
        if (d->parent == -1) {
            ICO_EEPTR(int *, p->roots)[n] = ICO_EEW(d);
            n++;
        }
        if (d->u.next == 0) {
            break;
        }
        d = ICO_EEPTR(BgaDObjEnt *, d->u.next);
    } while (1);
}

char *bga_InitData(char *data)
{
    BgaHeader *p = (BgaHeader *)data;
    BgaDObjEnt *d;
    int i;
    unsigned int j;
    int k;

    if (strncmp(p->magic, "BGA", 3) != 0) {
        debug_StdPrintfDummy("this is not bga file.\n");
        debug_assert(__FILE__, 952);
        __assert(__FILE__, 952, "FALSE");
    }
    p->dobjs += ICO_EEW(p);
    p->mode = -1;
    p->anim = BGA_W(mallocseki(sizeof(BgaAnim)));
    *BGA_ANIM(p) = bgaAnimDefault;
    d = ICO_EEPTR(BgaDObjEnt *, p->dobjs);
    while (1) {
        d->motion += ICO_EEW(p);
        if (d->env != 0) {
            i = 0;
            d->env += ICO_EEW(p);
            while (ICO_EEPTR(BgaEnvEnt *, d->env)[i].data != 0) {
                ICO_EEPTR(BgaEnvEnt *, d->env)[i].data += ICO_EEW(p);
                switch (ICO_EEPTR(BgaEnvEnt *, d->env)[i].type) {
                case 0:
                case 1:
                case 2:
                case 3:
                    *ICO_EEPTR(int *, ICO_EEPTR(BgaEnvEnt *, d->env)[i].data) += ICO_EEW(p);
                    break;
                case 6:
                    *ICO_EEPTR(int *, ICO_EEPTR(BgaEnvEnt *, d->env)[i].data) += ICO_EEW(p);
                    for (j = 0;
                         j < ICO_EEPTR(BgaMotion *, ICO_EEPTR(BgaEnvEnt *, d->env)[i].data)->n;
                         j++) {
                        float sum = 0.0f;
                        float scale = 1.0f;
                        float *v =
                            ICO_EEPTR(BgaKey *,
                                      ICO_EEPTR(BgaMotion *, ICO_EEPTR(BgaEnvEnt *, d->env)[i].data)
                                          ->key)[j]
                                .v;

                        for (k = 0; k < 6; k++) {
                            if (v[k] < 0.0f) {
                                sum -= v[k];
                            } else {
                                sum += v[k];
                            }
                        }
                        if (sum != 0.0f) {
                            scale = 1.0f / sum;
                        }
                        for (k = 0; k < 6; k++) {
                            float t = v[k] * scale;

                            if (t < 0.0f) {
                                v[k] = v[k] * -t;
                            } else {
                                v[k] = v[k] * t;
                            }
                        }
                    }
                    break;
                case 4:
                case 5:
                case 7:
                case 8:
                case 9:
                    break;
                }
                i++;
            }
        }
        if (d->u.next == 0) {
            break;
        }
        d->u.next += ICO_EEW(p);
        d = ICO_EEPTR(BgaDObjEnt *, d->u.next);
    }
    bga_makeRootList(p);
    bga_linkTree(p);
    return data;
}

inline char *bga_InitSdfCamera(char *data)
{
    BgaSdfCam *p = (BgaSdfCam *)data;

    if (strncmp(p->id, "SDF", 3) != 0) {
        debug_StdPrintfDummy("this is not sdf camera file.\n");
        debug_assert(__FILE__, 1045);
        __assert(__FILE__, 1045, "FALSE");
    }
    return data;
}

/* an object's display object (typedef.h's Sub15C) as bga_ApplyDObject reads
   it: the node count it hands out node numbers from and the model's name */
typedef struct BgaGeom { /* field names derived */
    /* 0x000 */ char pad00[4];
    /* 0x004 */ int parentNode;
    /* 0x008 */ int nodeNum;
    /* 0x00C */ char pad0C[2120];
    /* 0x854 */ char *name;
} BgaGeom; /* derived name */

typedef struct BgaGObj { /* field names derived */
    /* 0x000 */ char pad00[348];
    /* 0x15C */ void *geom;
} BgaGObj; /* derived name */

/* an object's display object and the two fields read from it: by name on
   the host (GObj.dobj, Sub15C.nodeNum and the model, whose name opens it),
   through the two views above on the EE */
#ifdef ICO_HOST
#define BGA_GOBJ_GEOM(o) (((GObj *)(o))->dobj)
#define BGA_GEOM_NAME(g) ((char *)(g)->model)
#else
#define BGA_GOBJ_GEOM(o) ((BgaGeom *)((BgaGObj *)(o))->geom)
#define BGA_GEOM_NAME(g) ((g)->name)
#endif

/* The particle entry's word at +0x20 packs three fields: the loop flag in
   bits 0-1, the effect handle in bits 2-16 and the particle id in bits
   17-31, read and written as the whole doubleword. */
typedef union BgaParticleBits { /* field names derived */

    struct {
        int loop : 2;
        int eff : 15;
        int id : 15;
    } b;

    long long w;
} BgaParticleBits; /* derived name */

typedef struct BgaParticleEnt { /* field names derived */
    /* 0x00 */ float pos[4];
    /* 0x10 */ int quat[4];
    /* 0x20 */ BgaParticleBits u;
} BgaParticleEnt; /* derived name */

/* the fields the light envelopes write: a light's colour at 0x20 (Light.c's
   Light), an ambient volume's colour at 0x40 and the inverse extents of its
   inner and outer shells (Light.c's AmbientVolume) */
typedef struct BgaLightEnv { /* field names derived */
    /* 0x00 */ char pad00[32];
    /* 0x20 */ float col[4];
    /* 0x30 */ char pad30[16];
    /* 0x40 */ float col2[4];
    /* 0x50 */ float inner[4];
    /* 0x60 */ float outer[3];
#ifdef ICO_HOST
    /* 0x6C */ char pad6C[4];
    /* 0x70 */ float size[3]; /* BgaObj's rscale, at the volume's 0x70 (bga_CalcObject) */
#endif
} BgaLightEnv; /* derived name */

#ifdef ICO_HOST

/* Light.c asserts AmbientVolume's col, inner, outer and size at these */
_Static_assert(__builtin_offsetof(BgaLightEnv, col2) == 0x40, "BgaLightEnv.col2 at 0x40");

_Static_assert(__builtin_offsetof(BgaLightEnv, inner) == 0x50, "BgaLightEnv.inner at 0x50");

_Static_assert(__builtin_offsetof(BgaLightEnv, outer) == 0x60, "BgaLightEnv.outer at 0x60");

_Static_assert(__builtin_offsetof(BgaLightEnv, size) == 0x70, "BgaLightEnv.size at 0x70");

#endif

static void bga_initLightEnvelope(BgaDObjEnt *p)
{
    BgaEnvEnt *e;
    unsigned char *d;

    e = ICO_EEPTR(BgaEnvEnt *, p->env);
    if (e == 0) {
        return;
    }
    while ((d = ICO_EEPTR(unsigned char *, e->data)) != 0) {
        switch (e->type) {
        case 4:
            switch (p->type) {
            case 6:
            case 11:
                if (p->u.light != 0) {
                    BGA_OBJ(struct BgaLightEnv *, p->u.light)->col[0] = (float)d[0] / 255.0f;
                    BGA_OBJ(struct BgaLightEnv *, p->u.light)->col[1] = (float)d[1] / 255.0f;
                    BGA_OBJ(struct BgaLightEnv *, p->u.light)->col[2] = (float)d[2] / 255.0f;
                    BGA_OBJ(struct BgaLightEnv *, p->u.light)->col[3] = 1.0f;
                } else {
                    debug_Assert("Light Object not exists.\n");
                    debug_assert(__FILE__, 1089);
                    __assert(__FILE__, 1089, "0");
                }
                break;
            case 7:
            case 8:
            case 9:
                if (p->u.light != 0) {
                    BGA_OBJ(struct BgaLightEnv *, p->u.light)->col2[0] = (float)d[0] / 255.0f;
                    BGA_OBJ(struct BgaLightEnv *, p->u.light)->col2[1] = (float)d[1] / 255.0f;
                    BGA_OBJ(struct BgaLightEnv *, p->u.light)->col2[2] = (float)d[2] / 255.0f;
                    BGA_OBJ(struct BgaLightEnv *, p->u.light)->col2[3] = 1.0f;
                } else {
                    debug_Assert("Shadow Object not exists.\n");
                    debug_assert(__FILE__, 1109);
                    __assert(__FILE__, 1109, "0");
                }
                break;
            }
            break;
        case 5:
            switch (p->type) {
            case 8:
            case 9:
                if (p->u.light != 0) {
                    BGA_OBJ(struct BgaLightEnv *, p->u.light)->inner[0] =
                        1.0f / (((float *)d)[0] * 50.0f);
                    BGA_OBJ(struct BgaLightEnv *, p->u.light)->inner[1] =
                        1.0f / (((float *)d)[1] * 50.0f);
                    BGA_OBJ(struct BgaLightEnv *, p->u.light)->inner[2] =
                        1.0f / (((float *)d)[2] * 50.0f);
                    BGA_OBJ(struct BgaLightEnv *, p->u.light)->outer[0] =
                        1.0f / (((float *)d)[3] * 50.0f);
                    BGA_OBJ(struct BgaLightEnv *, p->u.light)->outer[1] =
                        1.0f / (((float *)d)[4] * 50.0f);
                    BGA_OBJ(struct BgaLightEnv *, p->u.light)->outer[2] =
                        1.0f / (((float *)d)[5] * 50.0f);
                } else {
                    debug_assert(__FILE__, 1141);
                    __assert(__FILE__, 1141, "0");
                }
                break;
            }
            break;
        /* the next envelope type, nothing to do */
        case 6:
            break;
        }
        e++;
    }
}

void bga_ApplyDObject(BgaDObjEnt *p, GObj **objs, int n, int no)
{
    char buf[1024];
    int i;

    switch (p->type) {
    case 13:
        i = GetParticleIDWithName(p->name);
        if (i != -1) {
            p->u.obj = BGA_OBJW(iosMallocDebug(ios_partition_seki, 48, __FILE__, 1177));
            BGA_OBJ(BgaParticleEnt *, p->u.obj)->u.b.id = i;
            BGA_OBJ(BgaParticleEnt *, p->u.obj)->u.b.loop =
                GetParticleLoopFlag(BGA_OBJ(BgaParticleEnt *, p->u.obj)->u.b.id);
            if (BGA_OBJ(BgaParticleEnt *, p->u.obj)->u.b.loop) {
                BGA_OBJ(BgaParticleEnt *, p->u.obj)->u.b.eff =
                    SetParticleEffectActiveSensing(BGA_OBJ(BgaParticleEnt *, p->u.obj)->u.b.id,
                                                   bgaParticlePos, IdentityQuaternion);
            } else {
                BGA_OBJ(BgaParticleEnt *, p->u.obj)->u.b.eff = -1;
            }
            break;
        }
    case 1:
    case 2:
    case 5:
        p->u.obj = 0;
        break;
    case 0:
    case 4:
    case 10:
        p->u.obj = 0;
        for (i = 0; i < n; i++) {
            if (BGA_GEOM_NAME(BGA_GOBJ_GEOM(objs[i])) == 0) {
                sprintf(buf, "OBJECT FILE \"%s\" NOT EXISTS.\n", p->name);
                /* "model data file [%s] does not exist" */
                debug_StdPrintfDummy("モデルデータファイル[%s]がありません.\n\n", p->name);
                debug_assertMessage(__FILE__, 1201, buf);
                __assert(__FILE__, 1201, "e");
            }
            if (strcmp(BGA_GEOM_NAME(BGA_GOBJ_GEOM(objs[i])), p->name) == 0) {
                p->u.obj = BGA_OBJW(BGA_GOBJ_GEOM(objs[i]));
                p->num = BGA_GOBJ_GEOM(objs[i])->nodeNum++;
            }
        }
        break;
    case 7:
        p->u.obj = BGA_OBJW(light_AddAmbientObject(0));
        bga_initLightEnvelope(p);
        break;
    case 8:
        p->u.obj = BGA_OBJW(light_AddAmbientObject(2));
        bga_initLightEnvelope(p);
        break;
    case 9:
        p->u.obj = BGA_OBJW(light_AddAmbientObject(1));
        bga_initLightEnvelope(p);
        break;
    case 12:
        p->u.obj = BGA_OBJW(CreateKyomiGObj(no));
        break;
    }
    if (p->child) {
        bga_ApplyDObject(ICO_EEPTR(BgaDObjEnt *, p->child), objs, n, no);
    }
    if (p->sibling) {
        bga_ApplyDObject(ICO_EEPTR(BgaDObjEnt *, p->sibling), objs, n, no);
    }
}

static inline int bga_findKey(BgaKey *k, int n, float f) /* derived name */
{
    int lo = 0;
    int hi = n - 1;

    if (f < (float)k[0].time) {
        return 0;
    }
    if (hi < 2) {
        return 0;
    }
    while (lo < hi) {
        int mid = (lo + hi) >> 1;

        if ((float)k[mid].time <= f && f < (float)k[mid + 1].time) {
            return mid;
        }
        if ((float)k[mid + 1].time <= f) {
            lo = mid + 1;
        } else if (f <= (float)k[mid].time) {
            hi = mid - 1;
        }
        if (lo == hi) {
            return lo;
        }
    }
    return 0;
}

static inline void bga_hermite(float t, float *h0, float *h1, float *h2,
                               float *h3) /* derived name */
{
    float s = t * t;
    float c = t * s;
    float b = s * 3.0f - c - c;

    *h0 = 1.0f - b;
    *h1 = b;
    *h3 = c - s;
    *h2 = *h3 - s + t;
}

static inline int bga_findPtKey(BgaPtKey *k, int n, float f) /* derived name */
{
    int lo = 0;
    int hi = n - 1;

    if (f < (float)k[0].time) {
        return 0;
    }
    if (hi < 2) {
        return 0;
    }
    while (lo < hi) {
        int mid = (lo + hi) >> 1;

        if ((float)k[mid].time <= f && f < (float)k[mid + 1].time) {
            return mid;
        }
        if ((float)k[mid + 1].time <= f) {
            lo = mid + 1;
        } else if (f <= (float)k[mid].time) {
            hi = mid - 1;
        }
        if (lo == hi) {
            return lo;
        }
    }
    return 0;
}

static void bga_GetMotion(float *pos, int *rot, float *col, BgaPtMotion *m)
{
    BgaPtKey *k;
    BgaPtKey *k1;
#ifdef ICO_HOST
    BgaPtKey lastKey;
#endif
    float f;
    float u;
    float s0;
    float s1;
    float dv;
    float m0;
    float m1;
    int i;
    int d;

    f = m->frame;
    if (systemStatus[0]) {
        f *= 1.2075409f;
    }

    if (m->n == 1) {
        BgaPtKey *p = ICO_EEPTR(BgaPtKey *, m->key);

        pos[0] = p->pos[0];
        pos[1] = p->pos[1];
        pos[2] = p->pos[2];
        pos[3] = 1.0f;
        rot[0] = (short)(p->rot[0] * (65536.0f / 360.0f));
        rot[1] = (short)(p->rot[1] * (65536.0f / 360.0f));
        rot[2] = (short)(p->rot[2] * (65536.0f / 360.0f));
        col[0] = p->col[0];
        col[1] = p->col[1];
        col[2] = p->col[2];
        return;
    }

    k = ICO_EEPTR(BgaPtKey *, m->key);
    s0 = 0.0f;
    s1 = 0.0f;
    k = &k[bga_findPtKey(k, m->n, f)];
#ifdef ICO_HOST
    /* At the last key (f reaches its time once the PAL frame is scaled by
       1.2075409) the EE takes k1 from the record after the keys. With
       u = 0 its values drop out (times 0); they are finite on the EE but
       can be NaN bits on the host (the stage 45 lightning: a NaN node, then
       DrawLightning2's segment index from (int)NaN). The host uses a copy of
       the last key for k1, past the motion's end, and u = 0. */
    k1 = k + 1;
    f -= (float)k->time;
    if (k1 == ICO_EEPTR(BgaPtKey *, m->key) + m->n) {
        lastKey = *k;
        lastKey.time = (int)m->len > k->time ? (int)m->len : k->time + 1;
        k1 = &lastKey;
        d = k1->time - k->time;
        u = 0.0f;
    } else {
        d = k1->time - k->time;
        u = f / (float)d;
    }
#else
    k1 = k + 1;
    f -= (float)k->time;
    d = k1->time - k->time;
    u = f / (float)d;
#endif

    if (k1->linear == 0) {
        float h00;
        float h01;
        float h10;
        float h11;
        float ta;
        float tb;
        float tc;
        float td;

        ta = (1.0f - k->tension) * (k->bias + 1.0f);
        tb = (1.0f - k->tension) * (1.0f - k->bias);
        tc = (1.0f - k1->tension) * (1.0f - k1->bias);
        td = (1.0f - k1->tension) * (k1->bias + 1.0f);
        bga_hermite(u, &h00, &h01, &h10, &h11);
        if (k->time != 0) {
            s0 = (float)d / (float)(k1->time - k[-1].time);
        }
        if (k1->time < m->len) {
            s1 = (float)d / (float)(k1[1].time - k->time);
        }

        for (i = 0; i < 3; i++) {
            dv = k1->pos[i] - k->pos[i];

            if (k->time == 0) {
                m0 = (ta + tb) * dv;
            } else {
                m0 = s0 * (ta * (k->pos[i] - k[-1].pos[i]) + tb * dv);
            }
            if (k1->time >= m->len) {
                m1 = (tc + td) * dv;
            } else {
                m1 = s1 * (tc * dv + td * (k1[1].pos[i] - k1->pos[i]));
            }
            pos[i] = k->pos[i] * h00 + k1->pos[i] * h01 + m0 * h10 + m1 * h11;
        }
        for (i = 0; i < 3; i++) {
            dv = k1->rot[i] - k->rot[i];

            if (k->time == 0) {
                m0 = (ta + tb) * dv;
            } else {
                m0 = s0 * (ta * (k->rot[i] - k[-1].rot[i]) + tb * dv);
            }
            if (k1->time >= m->len) {
                m1 = (tc + td) * dv;
            } else {
                m1 = s1 * (tc * dv + td * (k1[1].rot[i] - k1->rot[i]));
            }
            rot[i] = (short)((k->rot[i] * h00 + k1->rot[i] * h01 + m0 * h10 + m1 * h11) *
                             (65536.0f / 360.0f));
        }
        for (i = 0; i < 3; i++) {
            dv = k1->col[i] - k->col[i];

            if (k->time == 0) {
                m0 = (ta + tb) * dv;
            } else {
                m0 = s0 * (ta * (k->col[i] - k[-1].col[i]) + tb * dv);
            }
            if (k1->time >= m->len) {
                m1 = (tc + td) * dv;
            } else {
                m1 = s1 * (tc * dv + td * (k1[1].col[i] - k1->col[i]));
            }
            col[i] = k->col[i] * h00 + k1->col[i] * h01 + m0 * h10 + m1 * h11;
        }
    } else {
        for (i = 0; i < 3; i++) {
            if (k->rot[i] < 0.0f ? -k->rot[i] < 0.1f : k->rot[i] < 0.1f) {
                k->rot[i] = 0.0f;
            }
            if (k1->rot[i] < 0.0f ? -k1->rot[i] < 0.1f : k1->rot[i] < 0.1f) {
                k1->rot[i] = 0.0f;
            }
            dv = k1->pos[i] - k->pos[i];
            pos[i] = k->pos[i] + u * dv;
            dv = k1->rot[i] - k->rot[i];
            rot[i] = (short)((k->rot[i] + (180.0f < u * dv
                                               ? u * dv - 360.0f
                                               : (u * dv < -180.0f ? u * dv + 360.0f : u * dv))) *
                             (65536.0f / 360.0f));
            dv = k1->col[i] - k->col[i];
            col[i] = k->col[i] + u * dv;
        }
    }
    pos[3] = 1.0f;
    col[3] = 1.0f;
}

static void bga_GetMotionParticle(float *pos, int *rot, float *col, BgaPtMotion *m)
{
    BgaPtKey *k;
    BgaPtKey *k1;
#ifdef ICO_HOST
    BgaPtKey lastKey;
#endif
    float f;
    float u;
    float s0;
    float s1;
    float dv;
    float m0;
    float m1;
    int i;
    int d;
    float w[2][4];

    f = m->frame;
    if (systemStatus[0]) {
        f *= 1.2075409f;
    }

    if (m->n == 1) {
        BgaPtKey *p = ICO_EEPTR(BgaPtKey *, m->key);

        pos[0] = p->pos[0];
        pos[1] = p->pos[1];
        pos[2] = p->pos[2];
        pos[3] = 1.0f;
        rot[0] = (short)(p->rot[0] * (65536.0f / 360.0f));
        rot[1] = (short)(p->rot[1] * (65536.0f / 360.0f));
        rot[2] = (short)(p->rot[2] * (65536.0f / 360.0f));
        col[0] = p->col[0];
        col[1] = p->col[1];
        col[2] = p->col[2];
        return;
    }

    k = ICO_EEPTR(BgaPtKey *, m->key);
    s0 = 0.0f;
    s1 = 0.0f;
    k = &k[bga_findPtKey(k, m->n, f)];
#ifdef ICO_HOST
    /* At the last key (f reaches its time once the PAL frame is scaled by
       1.2075409) the EE takes k1 from the record after the keys. With
       u = 0 its values drop out (times 0); they are finite on the EE but
       can be NaN bits on the host (the stage 45 lightning: a NaN node, then
       DrawLightning2's segment index from (int)NaN). The host uses a copy of
       the last key for k1, past the motion's end, and u = 0. */
    k1 = k + 1;
    f -= (float)k->time;
    if (k1 == ICO_EEPTR(BgaPtKey *, m->key) + m->n) {
        lastKey = *k;
        lastKey.time = (int)m->len > k->time ? (int)m->len : k->time + 1;
        k1 = &lastKey;
        d = k1->time - k->time;
        u = 0.0f;
    } else {
        d = k1->time - k->time;
        u = f / (float)d;
    }
#else
    k1 = k + 1;
    f -= (float)k->time;
    d = k1->time - k->time;
    u = f / (float)d;
#endif

    {
        float *q = &w[0][0];
        BgaPtKey *e = k;

        for (i = 0; i < 2; i++) {
            float s = e->col[0] + e->col[1] + e->col[2];

            if (4.0f < s) {
                q[1] = 1.0f;
                q[0] = q[2] = 0.0f;
            } else if (0.0f < s && s <= 4.0f) {
                q[0] = 1.0f;
                q[2] = 0.0f;
                q[1] = 0.0f;
            } else {
                q[0] = q[1] = q[2] = 0.0f;
            }
            q += 4;
            e++;
        }
    }

    if (k1->linear == 0) {
        float h00;
        float h01;
        float h10;
        float h11;
        float ta;
        float tb;
        float tc;
        float td;

        ta = (1.0f - k->tension) * (k->bias + 1.0f);
        tb = (1.0f - k->tension) * (1.0f - k->bias);
        tc = (1.0f - k1->tension) * (1.0f - k1->bias);
        td = (1.0f - k1->tension) * (k1->bias + 1.0f);
        bga_hermite(u, &h00, &h01, &h10, &h11);
        if (k->time != 0) {
            s0 = (float)d / (float)(k1->time - k[-1].time);
        }
        if (k1->time < m->len) {
            s1 = (float)d / (float)(k1[1].time - k->time);
        }

        for (i = 0; i < 3; i++) {
            dv = k1->pos[i] - k->pos[i];

            if (k->time == 0) {
                m0 = (ta + tb) * dv;
            } else {
                m0 = s0 * (ta * (k->pos[i] - k[-1].pos[i]) + tb * dv);
            }
            if (k1->time >= m->len) {
                m1 = (tc + td) * dv;
            } else {
                m1 = s1 * (tc * dv + td * (k1[1].pos[i] - k1->pos[i]));
            }
            pos[i] = k->pos[i] * h00 + k1->pos[i] * h01 + m0 * h10 + m1 * h11;
        }
        for (i = 0; i < 3; i++) {
            dv = k1->rot[i] - k->rot[i];

            if (k->time == 0) {
                m0 = (ta + tb) * dv;
            } else {
                m0 = s0 * (ta * (k->rot[i] - k[-1].rot[i]) + tb * dv);
            }
            if (k1->time >= m->len) {
                m1 = (tc + td) * dv;
            } else {
                m1 = s1 * (tc * dv + td * (k1[1].rot[i] - k1->rot[i]));
            }
            rot[i] = (short)((k->rot[i] * h00 + k1->rot[i] * h01 + m0 * h10 + m1 * h11) *
                             (65536.0f / 360.0f));
        }
        for (i = 0; i < 3; i++) {
            dv = w[1][i] - w[0][i];

            if (k->time == 0) {
                m0 = (ta + tb) * dv;
            } else {
                m0 = s0 * (ta * (w[0][i] - k[-1].col[i]) + tb * dv);
            }
            if (k1->time >= m->len) {
                m1 = (tc + td) * dv;
            } else {
                m1 = s1 * (tc * dv + td * (k1[1].col[i] - w[1][i]));
            }
            col[i] = w[0][i] * h00 + w[1][i] * h01 + m0 * h10 + m1 * h11;
        }
    } else {
        for (i = 0; i < 3; i++) {
            dv = k1->pos[i] - k->pos[i];
            pos[i] = k->pos[i] + u * dv;
            dv = k1->rot[i] - k->rot[i];
            rot[i] = (short)((k->rot[i] + u * dv) * (65536.0f / 360.0f));
            dv = w[1][i] - w[0][i];
            col[i] = w[0][i] + u * dv;
        }
    }
    pos[3] = 1.0f;
    col[3] = 1.0f;
}

static void bga_GetMotionLightning(float *pos, int *rot, float *col, BgaPtMotion *m)
{
    BgaPtKey *k;
    BgaPtKey *k1;
#ifdef ICO_HOST
    BgaPtKey lastKey;
#endif
    float f;
    float u;
    float s0;
    float s1;
    float dv;
    float m0;
    float m1;
    int i;
    int d;

    f = m->frame;
    if (systemStatus[0]) {
        f *= 1.2075409f;
    }

    if (m->n == 1) {
        BgaPtKey *p = ICO_EEPTR(BgaPtKey *, m->key);

        pos[0] = p->pos[0];
        pos[1] = p->pos[1];
        pos[2] = p->pos[2];
        pos[3] = 1.0f;
        rot[0] = (short)(p->rot[0] * (65536.0f / 360.0f));
        rot[1] = (short)(p->rot[1] * (65536.0f / 360.0f));
        rot[2] = (short)(p->rot[2] * (65536.0f / 360.0f));
        col[0] = p->col[0];
        col[1] = p->col[1];
        col[2] = p->col[2];
        return;
    }

    k = ICO_EEPTR(BgaPtKey *, m->key);
    s0 = 0.0f;
    s1 = 0.0f;
    k = &k[bga_findPtKey(k, m->n, f)];
#ifdef ICO_HOST
    /* At the last key (f reaches its time once the PAL frame is scaled by
       1.2075409) the EE takes k1 from the record after the keys. With
       u = 0 its values drop out (times 0); they are finite on the EE but
       can be NaN bits on the host (the stage 45 lightning: a NaN node, then
       DrawLightning2's segment index from (int)NaN). The host uses a copy of
       the last key for k1, past the motion's end, and u = 0. */
    k1 = k + 1;
    f -= (float)k->time;
    if (k1 == ICO_EEPTR(BgaPtKey *, m->key) + m->n) {
        lastKey = *k;
        lastKey.time = (int)m->len > k->time ? (int)m->len : k->time + 1;
        k1 = &lastKey;
        d = k1->time - k->time;
        u = 0.0f;
    } else {
        d = k1->time - k->time;
        u = f / (float)d;
    }
#else
    k1 = k + 1;
    f -= (float)k->time;
    d = k1->time - k->time;
    u = f / (float)d;
#endif

    if (k1->linear == 0) {
        float h00;
        float h01;
        float h10;
        float h11;
        float ta;
        float tb;
        float tc;
        float td;

        ta = (1.0f - k->tension) * (k->bias + 1.0f);
        tb = (1.0f - k->tension) * (1.0f - k->bias);
        tc = (1.0f - k1->tension) * (1.0f - k1->bias);
        td = (1.0f - k1->tension) * (k1->bias + 1.0f);
        bga_hermite(u, &h00, &h01, &h10, &h11);
        if (k->time != 0) {
            s0 = (float)d / (float)(k1->time - k[-1].time);
        }
        if (k1->time < m->len) {
            s1 = (float)d / (float)(k1[1].time - k->time);
        }

        for (i = 0; i < 3; i++) {
            dv = k1->pos[i] - k->pos[i];

            if (k->time == 0) {
                m0 = (ta + tb) * dv;
            } else {
                m0 = s0 * (ta * (k->pos[i] - k[-1].pos[i]) + tb * dv);
            }
            if (k1->time >= m->len) {
                m1 = (tc + td) * dv;
            } else {
                m1 = s1 * (tc * dv + td * (k1[1].pos[i] - k1->pos[i]));
            }
            pos[i] = k->pos[i] * h00 + k1->pos[i] * h01 + m0 * h10 + m1 * h11;
        }
        for (i = 0; i < 3; i++) {
            dv = k1->rot[i] - k->rot[i];

            if (k->time == 0) {
                m0 = (ta + tb) * dv;
            } else {
                m0 = s0 * (ta * (k->rot[i] - k[-1].rot[i]) + tb * dv);
            }
            if (k1->time >= m->len) {
                m1 = (tc + td) * dv;
            } else {
                m1 = s1 * (tc * dv + td * (k1[1].rot[i] - k1->rot[i]));
            }
            rot[i] = (short)((k->rot[i] * h00 + k1->rot[i] * h01 + m0 * h10 + m1 * h11) *
                             (65536.0f / 360.0f));
        }
        for (i = 0; i < 3; i++) {
            dv = k1->col[i] - k->col[i];

            if (k->time == 0) {
                m0 = (ta + tb) * dv;
            } else {
                m0 = s0 * (ta * (k->col[i] - k[-1].col[i]) + tb * dv);
            }
            if (k1->time >= m->len) {
                m1 = (tc + td) * dv;
            } else {
                m1 = s1 * (tc * dv + td * (k1[1].col[i] - k1->col[i]));
            }
            col[i] = k->col[i] * h00 + k1->col[i] * h01 + m0 * h10 + m1 * h11;
        }
    } else {
        for (i = 0; i < 3; i++) {
            if (k->rot[i] < 0.0f ? -k->rot[i] < 0.1f : k->rot[i] < 0.1f) {
                k->rot[i] = 0.0f;
            }
            if (k1->rot[i] < 0.0f ? -k1->rot[i] < 0.1f : k1->rot[i] < 0.1f) {
                k1->rot[i] = 0.0f;
            }
            dv = k1->pos[i] - k->pos[i];
            pos[i] = k->pos[i] + u * dv;
            dv = k1->rot[i] - k->rot[i];
            rot[i] = (short)((k->rot[i] + (180.0f < u * dv
                                               ? u * dv - 360.0f
                                               : (u * dv < -180.0f ? u * dv + 360.0f : u * dv))) *
                             (65536.0f / 360.0f));
            col[i] = k->col[i];
        }
    }
    pos[3] = 1.0f;
    col[3] = 1.0f;
}

static inline int bga_findExtKey(BgaExtKey *k, int n, float f) /* derived name */
{
    int lo = 0;
    int hi = n - 1;

    if (f < (float)k[0].time) {
        return 0;
    }
    if (hi < 2) {
        return 0;
    }
    while (lo < hi) {
        int mid = (lo + hi) >> 1;

        if ((float)k[mid].time <= f && f < (float)k[mid + 1].time) {
            return mid;
        }
        if ((float)k[mid + 1].time <= f) {
            lo = mid + 1;
        } else if (f <= (float)k[mid].time) {
            hi = mid - 1;
        }
        if (lo == hi) {
            return lo;
        }
    }
    return 0;
}

static float bga_GetExtMotion(BgaExtMotion *m)
{
    BgaExtKey *k;
    BgaExtKey *k1;
#ifdef ICO_HOST
    BgaExtKey lastKey;
#endif
    float f;
    float s0;
    float s1;
    float u;
    float dv;
    float t;
    int i;
    int d;

    f = m->frame;
    if (systemStatus[0]) {
        f *= 1.2075409f;
    }
    if (m->n == 1) {
        return ICO_EEPTR(BgaExtKey *, m->key)->value;
    }
    s0 = 0.0f;
    s1 = 0.0f;
    k = ICO_EEPTR(BgaExtKey *, m->key);
    i = bga_findExtKey(k, m->n, f);
    k = &k[i];
#ifdef ICO_HOST
    /* At the last key (f reaches its time once the PAL frame is scaled by
       1.2075409) the EE takes k1 from the record after the keys. With
       u = 0 its values drop out (times 0); they are finite on the EE but
       can be NaN bits on the host (the stage 45 lightning: a NaN node, then
       DrawLightning2's segment index from (int)NaN). The host uses a copy of
       the last key for k1, past the motion's end, and u = 0. */
    k1 = k + 1;
    f -= (float)k->time;
    if (k1 == ICO_EEPTR(BgaExtKey *, m->key) + m->n) {
        lastKey = *k;
        lastKey.time = (int)m->len > k->time ? (int)m->len : k->time + 1;
        k1 = &lastKey;
        d = k1->time - k->time;
        u = 0.0f;
    } else {
        d = k1->time - k->time;
        u = f / (float)d;
    }
#else
    k1 = k + 1;
    f -= (float)k->time;
    d = k1->time - k->time;
    u = f / (float)d;
#endif
    dv = k1->value - k->value;

    if (k1->linear == 0) {
        float h00;
        float h01;
        float h10;
        float h11;
        float ta;
        float tb;
        float tc;
        float td;
        float m0;
        float m1;

        ta = (1.0f - k->tension) * (k->bias + 1.0f);
        tb = (1.0f - k->tension) * (1.0f - k->bias);
        tc = (1.0f - k1->tension) * (1.0f - k1->bias);
        td = (1.0f - k1->tension) * (k1->bias + 1.0f);
        bga_hermite(u, &h00, &h01, &h10, &h11);
        if (k->time != 0) {
            s0 = (float)d / (float)(k1->time - k[-1].time);
        }
        if (k1->time < m->len) {
            s1 = (float)d / (float)(k1[1].time - k->time);
        }
        if (k->time == 0) {
            m0 = (ta + tb) * dv;
        } else {
            m0 = s0 * (ta * (k->value - k[-1].value) + tb * dv);
        }
        if (k1->time >= m->len) {
            m1 = (tc + td) * dv;
        } else {
            m1 = s1 * (tc * dv + td * (k1[1].value - k1->value));
        }
        return k->value * h00 + k1->value * h01 + m0 * h10 + m1 * h11;
    }
    t = u * dv;
    return k->value + t;
}

static void bga_GetGizmoMotion(BgaMotion *m, float *dst)
{
    BgaKey *k;
    BgaKey *k1;
#ifdef ICO_HOST
    BgaKey lastKey;
#endif
    float f;
    float u;
    float s0;
    float s1;
    float dv;
    float m0;
    float m1;
    int i;
    int d;

    f = m->frame;
    if (systemStatus[0]) {
        f *= 1.2075409f;
    }

    if (m->n == 1) {
        float *src = ICO_EEPTR(float *, m->key);

        for (i = 5; i >= 0; i--, src++, dst++) {
            *dst = *src;
        }
        return;
    }

    k = ICO_EEPTR(BgaKey *, m->key);
    s0 = 0.0f;
    s1 = 0.0f;
    i = bga_findKey(k, m->n, f);
    k = &k[i];
#ifdef ICO_HOST
    /* At the last key (f reaches its time once the PAL frame is scaled by
       1.2075409) the EE takes k1 from the record after the keys. With
       u = 0 its values drop out (times 0); they are finite on the EE but
       can be NaN bits on the host (the stage 45 lightning: a NaN node, then
       DrawLightning2's segment index from (int)NaN). The host uses a copy of
       the last key for k1, past the motion's end, and u = 0. */
    k1 = k + 1;
    f -= (float)k->time;
    if (k1 == ICO_EEPTR(BgaKey *, m->key) + m->n) {
        lastKey = *k;
        lastKey.time = (int)m->len > k->time ? (int)m->len : k->time + 1;
        k1 = &lastKey;
        d = k1->time - k->time;
        u = 0.0f;
    } else {
        d = k1->time - k->time;
        u = f / (float)d;
    }
#else
    k1 = k + 1;
    f -= (float)k->time;
    d = k1->time - k->time;
    u = f / (float)d;
#endif

    if (k1->linear == 0) {
        float h00;
        float h01;
        float h10;
        float h11;
        float ta;
        float tb;
        float tc;
        float td;

        ta = (1.0f - k->tension) * (k->bias + 1.0f);
        tb = (1.0f - k->tension) * (1.0f - k->bias);
        tc = (1.0f - k1->tension) * (1.0f - k1->bias);
        td = (1.0f - k1->tension) * (k1->bias + 1.0f);
        bga_hermite(u, &h00, &h01, &h10, &h11);
        if (k->time != 0) {
            s0 = (float)d / (float)(k1->time - k[-1].time);
        }
        if (k1->time < m->len) {
            s1 = (float)d / (float)(k1[1].time - k->time);
        }

        for (i = 0; i < 6; i++) {
            dv = k1->v[i] - k->v[i];

            if (k->time == 0) {
                m0 = (ta + tb) * dv;
            } else {
                m0 = s0 * (ta * (k->v[i] - k[-1].v[i]) + tb * dv);
            }
            if (k1->time >= m->len) {
                m1 = (tc + td) * dv;
            } else {
                m1 = s1 * (tc * dv + td * (k1[1].v[i] - k1->v[i]));
            }
            dst[i] = k->v[i] * h00 + k1->v[i] * h01 + m0 * h10 + m1 * h11;
        }
    } else {
        for (i = 0; i < 6; i++) {
            dv = k1->v[i] - k->v[i];
            dst[i] = k->v[i] + u * dv;
        }
    }
}

/* Light.c's 0x50-byte light record as this file uses it: bga_CalcObject
   hands its own instance to the light objects when the stage is not lit
   through Light.c, and bga_initLightEnvelope writes only the colour at 0x20.
   The size is the record's (light_AddLight allocates 0x50). */
typedef struct BgaLight { /* field names derived */
    /* 0x00 */ char pad00[32];
    /* 0x20 */ float col[4];
    /* 0x30 */ char pad30[32];
} BgaLight; /* derived name */

/* Whether an SDF camera is running and the Z roll the object walk has
   accumulated; then the camera matrix, the camera position at the last
   frame jump, the pivot the light-vector objects rotate about and the
   matrix built around it, the position, scale and rotation the motion
   readers fill for the object being walked, and the light record the
   light objects use when the stage has none. */
static int bgaCameraActive; /* derived name */

static short bgaRollZ; /* derived name */

static float bgaCameraMatrix[4][4]; /* derived name */

static float bgaLastCameraPos[4]; /* derived name */

static float bgaPivot[4]; /* derived name */

static float bgaPivotMatrix[4][4]; /* derived name */

static float bgaPos[4]; /* derived name */

static float bgaScale[4]; /* derived name */

static int bgaRot[4]; /* derived name */

static BgaLight bgaDummyLight; /* derived name */

#ifdef ICO_HOST
/* an EE address no heap block has (the EE heap starts at 0x760000) */
#define BGA_DUMMY_WORD 0x10u

static IcoEEWord bga_objWord(void *p)
{
    return p == (void *)&bgaDummyLight ? BGA_DUMMY_WORD : ico_eew(p);
}

static void *bga_objPtr(IcoEEWord w)
{
    return w == BGA_DUMMY_WORD ? (void *)&bgaDummyLight : ico_eeptr(w);
}

#endif

/* a word read either as an int or as a float, the form this programmer
   gives such words (StageAnimation.c's AnimWord, Packet.c's PacketFloat) */
typedef union { /* field names derived */
    int i;
    float f;
} BgaWord; /* derived name */

/* The flag bga_CalcObject tests before translating by the pivot. */
static int bgaPivotFlag = 0; /* derived name */

static inline float bga_palFrame(float f) /* derived name */
{
    if (systemStatus[0]) {
        f *= 0.82812935f;
    }
    return f;
}

/* Step an envelope's motion by dt and, once it runs
   past its length (scaled for PAL, as bga_CalcSdfCamera scales it), wrap it
   to 0 when looping or hold it at the end.  The helper reads the entry's data
   word itself. */
static inline void bga_stepEnvelope(BgaEnvEnt *e, float dt, int loop) /* derived name */
{
    BgaExtMotion *m = ICO_EEPTR(BgaExtMotion *, e->data);

    m->frame += dt;
    if ((float)m->len * (systemStatus[0] ? 0.82812935f : 1.0f) < m->frame) {
        if (loop) {
            m->frame = 0.0f;
        } else {
            m->frame = bga_palFrame((float)m->len);
        }
    }
}

/* Apply a node's envelopes: each entry's type says
 * what its motion drives, the node work record's float for the object, the
 * SDF camera zoom, a light's two parameters, the gizmo, the light vector or
 * the node's object pointer; types 4 and 5 (the colour envelopes
 * bga_initLightEnvelope reads) are skipped, and any other type is reported
 * with its entry and type and asserted. */
static void bga_calcEnvelope(BgaDObjEnt *p, float dt, float w, int cut, int loop)
{
    BgaEnvEnt *e;
    float v;

    e = ICO_EEPTR(BgaEnvEnt *, p->env);
    if (e == 0) {
        return;
    }
    for (; e->data != 0; e++) {
        switch (e->type) {
        case 0:
            if (p->u.obj != 0) {
                BGA_OBJ(Sub15C *, p->u.obj)->nodes[p->num].fade =
                    bga_GetExtMotion(ICO_EEPTR(BgaExtMotion *, e->data));
                BGA_OBJ(Sub15C *, p->u.obj)->nodes[p->num].flags.ll |= 1;
                bga_stepEnvelope(e, dt, loop);
            }
            break;
        case 1:
            if (bgaCameraActive != 0) {
                if (cut != 0) {
                    bgaZoom = bga_GetExtMotion(ICO_EEPTR(BgaExtMotion *, e->data)) *
                              (float)ScreenWidth / 2.66f;
                }
            }
            bga_stepEnvelope(e, dt, loop);
            break;
        case 2:
            v = bga_GetExtMotion(ICO_EEPTR(BgaExtMotion *, e->data));
            switch (p->type) {
            case 6:
            case 11:
                *(float *)(BGA_OBJ(char *, p->u.obj) + 0x30) = v;
                bga_stepEnvelope(e, dt, loop);
                break;
            case 8:
            case 9:
                *(float *)(BGA_OBJ(char *, p->u.obj) + 0x80) = v;
                bga_stepEnvelope(e, dt, loop);
                break;
            }
            break;
        case 3:
            v = bga_GetExtMotion(ICO_EEPTR(BgaExtMotion *, e->data));
            switch (p->type) {
            case 6:
            case 11:
                *(float *)(BGA_OBJ(char *, p->u.obj) + 0x34) = v;
                bga_stepEnvelope(e, dt, loop);
                break;
            }
            break;
        case 4:
        case 5:
            break;
        case 6:
            if (p->u.obj != 0) {
                bga_GetGizmoMotion(ICO_EEPTR(BgaMotion *, e->data),
                                   BGA_OBJ(Sub15C *, p->u.obj)->morphWeight);
                bga_stepEnvelope(e, dt, loop);
            }
            break;
        case 7:
            bgaPivot[0] = ICO_EEPTR(BgaWord *, e->data)[0].f;
            bgaPivot[1] = -ICO_EEPTR(BgaWord *, e->data)[1].f;
            bgaPivot[2] = ICO_EEPTR(BgaWord *, e->data)[2].f;
            bgaPivot[3] = 1.0f;
            bgaPivotFlag = 1;
            break;
        case 8:
        case 9:
            p->u.obj = e->data;
            break;
        default:
            debug_StdPrintfDummy("Illegal Envelope Type : %p(%d)\n", e, e->type);
            debug_assert(__FILE__, 2116);
            __assert(__FILE__, 2116, "0");
            break;
        }
    }
}

/* The rotation is applied Y, then X, then Z, with the three sines derived
   from the cosines (sin = sign(angle) * sqrt(1 - cos^2)) instead of a second
   table lookup.  The VU0 block is the _TransCurrentMatrix body followed by
   the three _RotCurrentMatrix* bodies with the cos/sin pairs already in
   $vf21..$vf26; $vf27..$vf29 hold the vmr32 chain so it is built once. */
static void _RotTransCurrentMatrixYXZ(void *t, int *rot)
{
    float cx, cy, cz, sx, sy, sz;

    cx = GetTableCos(rot[0]);
    cy = GetTableCos(rot[1]);
    cz = GetTableCos(rot[2]);
    sx = SIGNF(rot[0]) * _Sqrt(1.0f - cx * cx);
    sy = SIGNF(rot[1]) * _Sqrt(1.0f - cy * cy);
    sz = SIGNF(rot[2]) * _Sqrt(1.0f - cz * cz);

#ifdef ICO_HOST
    /* translate by t, then rotate about Y, X and Z: each rotation is built
       in full and multiplied on the right (docs/port/MATH.md) */
    {
        float ry[4][4] = {{cy, 0.0f, 0.0f - sy, 0.0f},
                          {0.0f, 1.0f, 0.0f, 0.0f},
                          {sy, 0.0f, cy, 0.0f},
                          {0.0f, 0.0f, 0.0f, 1.0f}};
        float rx[4][4] = {{1.0f, 0.0f, 0.0f, 0.0f},
                          {0.0f, cx, sx, 0.0f},
                          {0.0f, 0.0f - sx, cx, 0.0f},
                          {0.0f, 0.0f, 0.0f, 1.0f}};
        float rz[4][4] = {{cz, sz, 0.0f, 0.0f},
                          {0.0f - sz, cz, 0.0f, 0.0f},
                          {0.0f, 0.0f, 1.0f, 0.0f},
                          {0.0f, 0.0f, 0.0f, 1.0f}};

        _TransCurrentMatrix(t);
        _MulCurrentMatrixR(ry);
        _MulCurrentMatrixR(rx);
        _MulCurrentMatrixR(rz);
    }
#else
    /* The rotation pairs go into $vf21..$vf26 while the vmr32 chain builds
       the identity rows in $vf14..$vf17.  The sequence is one asm block
       because it is ordered by hand: every mfc1 is separated from the qmtc2
       that consumes its GPR, and the three vmr32 sit in those gaps. */
    __asm__ __volatile__("vmove.xyzw $vf17, $vf0\n\t"
                         "lqc2 $vf8, 0(%6)\n\t"
                         "mfc1 $4, %0\n\t"
                         "mfc1 $5, %1\n\t"
                         "vmr32.xyzw $vf16, $vf17\n\t"
                         "mfc1 $6, %2\n\t"
                         "mfc1 $7, %3\n\t"
                         "mfc1 $8, %4\n\t"
                         "vmr32.xyzw $vf15, $vf16\n\t"
                         "mfc1 $9, %5\n\t"
                         "qmtc2.ni $4, $vf21\n\t"
                         "qmtc2.ni $5, $vf22\n\t"
                         "vmr32.xyzw $vf14, $vf15\n\t"
                         "qmtc2.ni $6, $vf23\n\t"
                         "qmtc2.ni $7, $vf24\n\t"
                         "qmtc2.ni $8, $vf25\n\t"
                         "qmtc2.ni $9, $vf26"
                         :
                         : "f"(cy), "f"(sy), "f"(cx), "f"(sx), "f"(cz), "f"(sz), "r"(t)
                         : "$4", "$5", "$6", "$7", "$8", "$9", "memory");
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 8, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 8, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 8, z);
    VU0_V2OP(vmove.xyzw, 27, 14);
    VU0_V2OP(vmove.xyzw, 29, 16);
    VU0_V3OP_BC(vaddx.x, 14, 0, 21, x);
    VU0_V3OP_BC(vaddx.x, 16, 0, 22, x);
    VU0_V3OP_BC(vmaddw.xyzw, 7, 7, 8, w);
    VU0_V2OP(vmove.xyzw, 28, 15);
    VU0_V3OP_BC(vsubx.z, 14, 0, 22, x);
    VU0_V3OP_BC(vaddx.z, 16, 0, 21, x);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 14, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 14, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 14, z);
    VU0_V3OP_BC(vmaddw.xyzw, 10, 7, 14, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 15, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 15, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 15, z);
    VU0_V3OP_BC(vmaddw.xyzw, 11, 7, 15, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 16, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 16, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 16, z);
    VU0_V3OP_BC(vmaddw.xyzw, 12, 7, 16, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 17, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 17, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 17, z);
    VU0_V3OP_BC(vmaddw.xyzw, 13, 7, 17, w);
    VU0_V2OP(vmove.xyzw, 15, 28);
    VU0_V2OP(vmove.xyzw, 16, 29);
    VU0_V2OP(vmove.xyzw, 14, 27);
    VU0_V2OP(vmove.xyzw, 17, 0);
    VU0_V3OP_BC(vaddx.y, 15, 0, 23, x);
    VU0_V3OP_BC(vsubx.y, 16, 0, 24, x);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 10, 14, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 11, 14, y);
    VU0_V3OP_BC(vaddx.z, 15, 0, 24, x);
    VU0_V3OP_BC(vaddx.z, 16, 0, 23, x);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 12, 14, z);
    VU0_V3OP_BC(vmaddw.xyzw, 4, 13, 14, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 10, 15, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 11, 15, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 12, 15, z);
    VU0_V3OP_BC(vmaddw.xyzw, 5, 13, 15, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 10, 16, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 11, 16, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 12, 16, z);
    VU0_V3OP_BC(vmaddw.xyzw, 6, 13, 16, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 10, 17, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 11, 17, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 12, 17, z);
    VU0_V3OP_BC(vmaddw.xyzw, 7, 13, 17, w);
    VU0_V2OP(vmove.xyzw, 14, 27);
    VU0_V2OP(vmove.xyzw, 15, 28);
    VU0_V2OP(vmove.xyzw, 16, 29);
    VU0_V2OP(vmove.xyzw, 17, 0);
    VU0_V3OP_BC(vaddx.x, 14, 0, 25, x);
    VU0_V3OP_BC(vsubx.x, 15, 0, 26, x);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 16, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 16, y);
    VU0_V3OP_BC(vaddx.y, 14, 0, 26, x);
    VU0_V3OP_BC(vaddx.y, 15, 0, 25, x);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 16, z);
    VU0_V3OP_BC(vmaddw.xyzw, 12, 7, 16, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 14, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 14, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 14, z);
    VU0_V3OP_BC(vmaddw.xyzw, 10, 7, 14, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 15, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 15, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 15, z);
    VU0_V3OP_BC(vmaddw.xyzw, 11, 7, 15, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 17, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 17, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 17, z);
    VU0_V3OP_BC(vmaddw.xyzw, 13, 7, 17, w);
    VU0_V2OP(vmove.xyzw, 4, 10);
    VU0_V2OP(vmove.xyzw, 5, 11);
    VU0_V2OP(vmove.xyzw, 6, 12);
    VU0_V2OP(vmove.xyzw, 7, 13);
#endif
}

/* Externs and record views bga_CalcObject uses.  BgaNodeBits is typedef.h's
   DObjNode with its flag bits named as RegistPacket.c reads them. */

typedef struct BgaNodeBits { /* field names derived */

    /* 0x00 */ char pad00[48];
    /* 0x30 */ float fade;
    /* 0x34 */ int alpha;
    /* 0x38 */ union {
        struct {
            int fade : 1;      /* the node fades by fade and alpha */
            int screenPos : 1; /* drawn unrotated at pos in view space (type 10) */
            int billboard : 1; /* faces the camera, turned only by rotZ (type 4) */
            short rotZ;
        } b;

        long long w;
    } flags;

    /* 0x40 */ float pos[4];
} BgaNodeBits; /* derived name */

typedef struct BgaObj { /* field names derived */
    /* 0x00 */ short id;
    /* 0x02 */ char pad02[10];
    /* 0x0C */ float (*mtx)[16];
    /* 0x10 */ float (*quat)[4];
    /* 0x14 */ char pad14[12];
    /* 0x20 */ char pad20[8];
    /* 0x28 */ char pad28[72];
    /* 0x70 */ float rscale[3];
    /* 0x7C */ char pad7C[2036];
    /* 0x870 */ BgaNodeBits *work;
} BgaObj; /* derived name */

/* a geometry object's node matrices, node quaternions and node records: by
   name on the host (Sub15C.nodeMtx, nodeQuat, nodes), through BgaObj on the
   EE.  The node record (DObjNode) has no pointers, so BgaNodeBits fits it on
   every host.  The lightning objects keep the BgaObj view: their id lies
   before any pointer of the record it views.  An ambient volume's size
   (rscale, 0x70) does not on the host, where BgaObj's mtx and quat pointers
   move rscale to 0x7C: bga_CalcObject writes it through BgaLightEnv's size,
   at the volume's 0x70 (Light.c's AmbientVolume). */
#ifdef ICO_HOST
#define BGA_GEOM_MTX(w) ((float (*)[16])BGA_OBJ(Sub15C *, w)->nodeMtx)
#define BGA_GEOM_QUAT(w) ((float (*)[4])BGA_OBJ(Sub15C *, w)->nodeQuat)
#define BGA_GEOM_WORK(w) ((BgaNodeBits *)BGA_OBJ(Sub15C *, w)->nodes)
#else
#define BGA_GEOM_MTX(w) (BGA_OBJ(BgaObj *, w)->mtx)
#define BGA_GEOM_QUAT(w) (BGA_OBJ(BgaObj *, w)->quat)
#define BGA_GEOM_WORK(w) (BGA_OBJ(BgaObj *, w)->work)
#endif

/* The lightning record bga_addLightning allocates: ten of lightning.h's
   0x20-byte nodes, the live node count, the two flags, the frame, the
   definition it was built from and the list link. */
/* The lightning definition the BGA file carries: the kind at +0x02 picks the
   object the bolt is drawn against, the four bytes at +0x04 are its colour,
   and the nine floats from +0x08 and the short at +0x2E are DrawLightningN's
   parameters, named as lightning.c names them, and the short at +0x2C is the
   node the bolt starts from. */
typedef struct BgaLightningDef { /* field names derived */
    /* 0x00 */ char pad00[2];
    /* 0x02 */ short kind;
    /* 0x04 */ unsigned char col[4];
    /* 0x08 */ float stepMin;
    /* 0x0C */ float stepMax;
    /* 0x10 */ float swayStepMin;
    /* 0x14 */ float swayStepMax;
    /* 0x18 */ float turnMin;
    /* 0x1C */ float turnMax;
    /* 0x20 */ float swayLimit;
    /* 0x24 */ float width;
    /* 0x28 */ float texLen;
    /* 0x2C */ short node;
    /* 0x2E */ short c;
} BgaLightningDef; /* derived name */

typedef struct BgaLightning { /* field names derived */
    /* 0x000 */ LightningNode seg[10];
    /* 0x140 */ int n;
    /* 0x144 */ int id;
    /* 0x148 */ int t0;
    /* 0x14C */ float frame;
    /* 0x150 */ BgaLightningDef *def;
    /* 0x154 */ struct BgaLightning *next;
} BgaLightning; /* derived name */

static void bga_addLightning(int kind, BgaLightningDef *def, float *vec, int id, int t0, float f);

static inline void bga_checkCameraDistance(void) /* derived name */
{
    if (bgaCameraActive != 0) {
        _InverseCurrentMatrix();
        _GetCurrentMatrix(bgaCameraMatrix);
        if (GlobalTimer != 0) {
            if (_GetLength(bgaCameraMatrix[3], bgaLastCameraPos) < 100.0f) {
                GlobalTimer = 0;
                currentScreenWidth = 0;
            }
        }
    }
}

static inline void bga_stepMotion(BgaExtMotion *m, float dt, int reset) /* derived name */
{
    m->frame += dt;
    if ((float)m->len * (systemStatus[0] ? 0.82812935f : 1.0f) < m->frame) {
        if (reset) {
            m->frame = 0.0f;
        } else {
            m->frame = bga_palFrame((float)m->len);
        }
    }
}

static void bga_CalcObject(BgaDObjEnt *d, float dt, float frame, int cut, int play, int loop)
{
    int save;
    float *pos;

    switch (d->type) {
    case 13:
        bga_GetMotionParticle(bgaPos, bgaRot, bgaScale, (BgaPtMotion *)&d->motion);
        break;
    case 14:
    case 15:
    case 16:
        bga_GetMotionLightning(bgaPos, bgaRot, bgaScale, (BgaPtMotion *)&d->motion);
        break;
    default:
        bga_GetMotion(bgaPos, bgaRot, bgaScale, (BgaPtMotion *)&d->motion);
        break;
    }

    switch (d->type) {
    case 6:
        d->u.obj = BGA_OBJW((systemStatus[5] == 0) ? (void *)light_AddLight(0, 0, 2)
                                                   : (void *)&bgaDummyLight);
        bga_initLightEnvelope(d);
        break;
    case 11:
        d->u.obj = BGA_OBJW((systemStatus[5] == 0) ? (void *)light_AddLight(0, 0, 3)
                                                   : (void *)&bgaDummyLight);
        bga_initLightEnvelope(d);
        break;
    }
    bgaPivotFlag = 0;

    bga_calcEnvelope(d, dt, frame, cut, loop);
    PushQuaternion();
    _PushCurrentMatrix();
    save = bgaRollZ;
    if (bgaPivotFlag != 0) {
        _PushCurrentMatrix();
        _TransCurrentMatrix(bgaPivot);
        _RotTransCurrentMatrixYXZ(bgaPos, bgaRot);
        _ScaleVectorXYZ(bgaPivot, bgaPivot, -1.0f);
        _TransCurrentMatrix(bgaPivot);
        _ScaleCurrentMatrix(bgaScale[0], bgaScale[1], bgaScale[2]);
        _GetCurrentMatrix(bgaPivotMatrix);
        _PopCurrentMatrix();
    }
    _RotTransCurrentMatrixYXZ(bgaPos, bgaRot);
    RotCurrentQuaternionY((short)bgaRot[1]);
    RotCurrentQuaternionX((short)bgaRot[0]);
    RotCurrentQuaternionZ((short)bgaRot[2]);
    bgaRollZ -= (unsigned short)bgaRot[2];

    switch (d->type) {
    case 8:
    case 9:
        if (d->u.obj != 0) {
#ifdef ICO_HOST
            /* the volume's size at its 0x70, which BgaObj's rscale misses on
               the host (see BGA_GEOM_MTX) */
            BGA_OBJ(BgaLightEnv *, d->u.obj)->size[0] = 1.0f / bgaScale[0];
            BGA_OBJ(BgaLightEnv *, d->u.obj)->size[1] = 1.0f / bgaScale[1];
            BGA_OBJ(BgaLightEnv *, d->u.obj)->size[2] = 1.0f / bgaScale[2];
#else
            BGA_OBJ(BgaObj *, d->u.obj)->rscale[0] = 1.0f / bgaScale[0];
            BGA_OBJ(BgaObj *, d->u.obj)->rscale[1] = 1.0f / bgaScale[1];
            BGA_OBJ(BgaObj *, d->u.obj)->rscale[2] = 1.0f / bgaScale[2];
#endif
            _GetCurrentMatrix(BGA_OBJ(void *, d->u.obj));
        }
        break;
    case 6:
    case 11:
        if (d->u.obj != 0) {
            _GetCurrentMatrixTrans(BGA_OBJ(void *, d->u.obj));
        }
        break;
    case 12:
        _GetCurrentMatrixTrans(bgaPos);
        SetParamKyomiGObj(BGA_OBJ(void *, d->u.obj), bgaPos, bgaScale);
        break;
    case 13:
        if (d->u.obj == 0) {
            /* "unknown particle" */
            debug_StdPrintfDummy("不明なパーティクル\n");
            break;
        }
        /* pos is bgaPos for the calls below; the PAL SetParticleEffect call
           names bgaPos itself */
        pos = bgaPos;
        if (bgaUniqAnimationFlag == 0 && BGA_OBJ(BgaParticleEnt *, d->u.obj)->u.b.loop) {
            /* "a PBGA-type animation cannot use looping particles" */
            debug_StdPrintfDummy(
                "PBGAタイプのアニメーションではループのパーティクルは使用できません.\n");
            break;
        }
        _GetCurrentMatrixTrans(pos);
        if (systemStatus[5] != 0) {
            break;
        }
        CopyQuaternion(BGA_OBJ(BgaParticleEnt *, d->u.obj)->quat, GetCurrentQuaternion());
        _CopyVector(BGA_OBJ(BgaParticleEnt *, d->u.obj)->pos, pos);
        if (systemStatus[0] == 0) {
            if (0.0f < bgaScale[1]) {
                BGA_OBJ(BgaParticleEnt *, d->u.obj)->u.b.eff =
                    SetParticleEffectActiveSensing(BGA_OBJ(BgaParticleEnt *, d->u.obj)->u.b.id,
                                                   BGA_OBJ(BgaParticleEnt *, d->u.obj)->pos,
                                                   BGA_OBJ(BgaParticleEnt *, d->u.obj)->quat);
            } else if (0.0f < bgaScale[0]) {
                BGA_OBJ(BgaParticleEnt *, d->u.obj)->u.b.eff = SetParticleEffect(
                    BGA_OBJ(BgaParticleEnt *, d->u.obj)->u.b.id, pos, GetCurrentQuaternion());
            }
        } else {
            if (0.41406468f <= bgaScale[1]) {
                BGA_OBJ(BgaParticleEnt *, d->u.obj)->u.b.eff =
                    SetParticleEffectActiveSensing(BGA_OBJ(BgaParticleEnt *, d->u.obj)->u.b.id,
                                                   BGA_OBJ(BgaParticleEnt *, d->u.obj)->pos,
                                                   BGA_OBJ(BgaParticleEnt *, d->u.obj)->quat);
            } else if (0.41406468f <= bgaScale[0]) {
                BGA_OBJ(BgaParticleEnt *, d->u.obj)->u.b.eff = SetParticleEffect(
                    BGA_OBJ(BgaParticleEnt *, d->u.obj)->u.b.id, bgaPos, GetCurrentQuaternion());
            }
        }
        break;
    case 14:
    case 15:
        if (play != 0 && systemStatus[5] == 0) {
            _GetCurrentMatrixTrans(bgaPos);
            bga_addLightning(d->type, BGA_OBJ(void *, d->u.obj), bgaPos,
                             BGA_OBJ(BgaObj *, d->u.obj)->id, bgaScale[1] == 0.0f, bgaScale[0]);
        }
        break;
    case 16:
        if (play != 0 && systemStatus[5] == 0) {
            _GetCurrentMatrixTrans(bgaPos);
            bga_addLightning(d->type, BGA_OBJ(void *, d->u.obj), bgaPos,
                             BGA_OBJ(BgaObj *, d->u.obj)->id, 0, 0.0f);
        }
        break;
    case 1:
        _ScaleCurrentMatrix(bgaScale[0], bgaScale[1], bgaScale[2]);
        break;
    case 7:
        break;
    default:
        _ScaleCurrentMatrix(bgaScale[0], bgaScale[1], bgaScale[2]);
        if (d->u.obj != 0) {
            RegularizeQuaternion(GetCurrentQuaternion());
            CopyQuaternion(BGA_GEOM_QUAT(d->u.obj)[d->num], GetCurrentQuaternion());
            if (bgaPivotFlag != 0) {
                _CopyMatrix(&BGA_GEOM_MTX(d->u.obj)[d->num], bgaPivotMatrix);
            } else {
                _GetCurrentMatrix(&BGA_GEOM_MTX(d->u.obj)[d->num]);
            }
            BGA_GEOM_WORK(d->u.obj)[d->num].flags.b.screenPos = (d->type == 10);
            if (BGA_GEOM_WORK(d->u.obj)[d->num].flags.b.screenPos) {
                _CopyVector(BGA_GEOM_WORK(d->u.obj)[d->num].pos, bgaPos);
            }
            BGA_GEOM_WORK(d->u.obj)[d->num].flags.b.billboard = (d->type == 4);
            BGA_GEOM_WORK(d->u.obj)[d->num].flags.b.rotZ = bgaRollZ;
        }
        if (cut != 0 && d->type == 2) {
            if (debug_font_flag & 1) {
                debug_Printf(600, ScreenHeight / 2 - 8, 0xCCCCCC00, "LWS");
            }
            bga_checkCameraDistance();
        }
        break;
    }

    if (d->child != 0) {
        bga_CalcObject(ICO_EEPTR(BgaDObjEnt *, d->child), dt, frame, cut, play, loop);
    }
    bgaRollZ = save;
    _PopCurrentMatrix();
    PopQuaternion();
    if (d->sibling != 0) {
        bga_CalcObject(ICO_EEPTR(BgaDObjEnt *, d->sibling), dt, frame, cut, play, loop);
    }
    bga_stepMotion((BgaExtMotion *)&d->motion, dt, loop);
}

typedef struct { /* field names derived */
    /* 0x00 */ int key;
    /* 0x04 */ int n;
    /* 0x08 */ unsigned int len;
    /* 0x0C */ float frame;
} BgaCount; /* derived name */

typedef struct { /* field names derived */
    /* 0x00 */ char pad00[4];
    /* 0x04 */ ICO_EEWORD(BgaCount *) obj;
} BgaCountEnt; /* derived name */

typedef struct BgaCntNode { /* field names derived */
    /* 0x00 */ char pad00[40];
    /* 0x28 */ ICO_EEWORD(BgaCountEnt *) ents;
    /* 0x2C */ ICO_EEWORD(struct BgaCntNode *) child;
    /* 0x30 */ ICO_EEWORD(struct BgaCntNode *) sibling;
    /* 0x34 */ BgaCount count;
} BgaCntNode; /* derived name */

static inline void bga_clampCount(BgaCount *o, float f) /* derived name */
{
    if (f >= 0.0f) {
        float c = (float)o->len;
        float r;

        if (systemStatus[0] ? c * 0.82812935f < f : c < f) {
            float t = (float)o->len;

            r = t;
            if (systemStatus[0]) {
                r *= 0.82812935f;
            }
        } else {
            r = f;
        }
        o->frame = r;
    } else {
        o->frame = 0.0f;
    }
}

static void bga_resetObjectCounter(BgaCntNode *o, float f, int loop)
{
    BgaCountEnt *e;

    e = ICO_EEPTR(BgaCountEnt *, o->ents);
    if (e != 0) {
        while (e->obj != 0) {
            bga_clampCount(ICO_EEPTR(BgaCount *, e->obj), f);
            e++;
        }
    }
    if (o->child != 0) {
        bga_resetObjectCounter(ICO_EEPTR(BgaCntNode *, o->child), f, loop);
    }
    if (o->sibling != 0) {
        bga_resetObjectCounter(ICO_EEPTR(BgaCntNode *, o->sibling), f, loop);
    }
    bga_clampCount(&o->count, f);
}

void bga_SetFrame(BgaHeader *p, int frame, int mode, int loop)
{
    float f;

    if (p->cut) {
        GlobalTimer = 1;
        bgaStreamSync = 1;
        _CopyVector(bgaLastCameraPos, bgaCameraMatrix[3]);
    }
    switch (frame) {
    case 0:
        p->frame = bga_palFrame(p->start);
        p->mode = mode;
        break;
    case -1:
        debug_StdPrintfDummy("lws animation last %s\n", ICO_EEPTR(char *, p->dobjs) + 4);
        p->frame = bga_palFrame(p->end);
        p->mode = mode;
        break;
    case -2:
        debug_StdPrintfDummy("lws animation off %s\n", ICO_EEPTR(char *, p->dobjs) + 4);
        p->frame = bga_palFrame(p->start);
        p->mode = -1;
        return;
    default:
        f = (float)frame;
        p->frame = bga_palFrame(f);
        if (f < p->start) {
            p->frame = bga_palFrame(p->start);
        } else if (p->end < f) {
            p->frame = bga_palFrame(p->end);
        }
        p->mode = mode;
        break;
    }
    bga_CalcAnimation(p, loop, 1);
}

typedef struct BgaAnimGeom { /* field names derived */
    /* 0x000 */ char pad00[12];
    /* 0x00C */ float (*mtx)[4][4];
    /* 0x010 */ float (*quat)[4];
} BgaAnimGeom; /* derived name */

typedef struct BgaAnimObj { /* field names derived */
    /* 0x000 */ char pad00[348];
    /* 0x15C */ BgaAnimGeom *geom;
} BgaAnimObj; /* derived name */

/* the parent object's node matrices and quaternions: by name on the host
   (GObj.dobj, Sub15C.nodeMtx and nodeQuat), through the views on the EE.
   BGA_AOBJ_GOBJ is the parent as the GObj geometryManager.c's root
   accessors take: a cast on the host, the bare operand on the EE (whose
   call passes the view pointer as it is) */
#ifdef ICO_HOST
#define BGA_AOBJ_MTX(o) ((float (*)[4][4])((GObj *)(o))->dobj->nodeMtx)
#define BGA_AOBJ_QUAT(o) ((float (*)[4])((GObj *)(o))->dobj->nodeQuat)
#define BGA_AOBJ_GOBJ(o) ((GObj *)(o))
#else
#define BGA_AOBJ_MTX(o) ((o)->geom->mtx)
#define BGA_AOBJ_QUAT(o) ((o)->geom->quat)
#define BGA_AOBJ_GOBJ(o) o
#endif

void bga_CalcAnimation(BgaHeader *p, int loop, int reset)
{
    float m[4][4];
    float rm[4][4];
    BgaCntNode *o;
    int i;
    int f1;
    int f2;

    if (p->mode == -1) {
        return;
    }

    if (p->cut) {
        bgaCameraActive = 1;
    }

    GetMatrixFromQuaternionPos(m, BGA_ANIM(p)->quat, BGA_ANIM(p)->pos);
    if (BGA_ANIM(p)->obj) {
        if (BGA_ANIM(p)->root) {
            _SetCurrentMatrix(BGA_AOBJ_MTX(BGA_ANIM(p)->obj)[BGA_ANIM(p)->idx]);
        } else {
            GetRootMatrix(rm, BGA_AOBJ_GOBJ(BGA_ANIM(p)->obj));
            CopyVector(rm[3], BGA_AOBJ_MTX(BGA_ANIM(p)->obj)[BGA_ANIM(p)->idx][3]);
            _SetCurrentMatrix(rm);
        }
    } else {
        _InitCurrentMatrix();
    }
    _MulCurrentMatrixR(m);

    if (BGA_ANIM(p)->obj) {
        if (BGA_ANIM(p)->root) {
            CopyQuaternion(GetCurrentQuaternion(),
                           BGA_AOBJ_QUAT(BGA_ANIM(p)->obj)[BGA_ANIM(p)->idx]);
        } else {
            GetRootQuaternion(GetCurrentQuaternion(), BGA_AOBJ_GOBJ(BGA_ANIM(p)->obj));
        }
    } else {
        SetIdentityQuaternion(GetCurrentQuaternion());
    }
    MultiQuaternion(GetCurrentQuaternion(), GetCurrentQuaternion(), BGA_ANIM(p)->quat);

    for (i = 0;; i++) {
        f2 = (p->mode == 1);
        f1 = p->cut && f2;
        o = (BgaCntNode *)BGA_ROOT(p, i);
        if (o == 0) {
            break;
        }
        PushQuaternion();
        _PushCurrentMatrix();
        if (reset == 1) {
            bga_resetObjectCounter(o, p->frame, loop);
        }
        bga_CalcObject((BgaDObjEnt *)o, p->step, p->frame, f1, f2, loop);
        _PopCurrentMatrix();
        PopQuaternion();
    }

    bgaFrame = (int)p->frame;
    if (reset) {
        return;
    }

    if (p->mode == 1) {
        float end = p->end;

        p->frame += p->step;
        if (systemStatus[0] ? end * 0.82812935f < p->frame : end < p->frame) {
            if (loop == 0) {
                p->frame = bga_palFrame(p->end);
                p->mode = 0;
            } else {
                p->frame = 0.0f;
            }
        }
    }
}

/* the PAL frame counter read back on the 60 Hz timeline: the reciprocal of
   bga_palFrame's 0.82812935f. */
static inline float bga_ntscFrame(float f) /* derived name */
{
    if (systemStatus[0]) {
        f *= 1.2075409f;
    }
    return f;
}

/* The record is read through its fields.  Both frame-rate scales are
 * `x * (PAL ? k : 1.0f)`, and each key's two values are read into locals of
 * their own, fov first. */
void bga_CalcSdfCamera(char *data, int loop)
{
    BgaSdfCam *p = (BgaSdfCam *)data;
    BgaSdfKey *k0;
    BgaSdfKey *k1;
    float fr;
    float t;
    int i;
    int i1;

    if (p->mode == -1) {
        return;
    }
    bgaCameraActive = 1;
    if ((float)p->num * (systemStatus[0] ? 0.82812935f : 1.0f) < p->frame) {
        if (loop == 0) {
            p->frame = bga_palFrame((float)p->num);
            p->mode = 0;
            return;
        }
        p->frame = 0.0f;
    }

    bgaFrame = (int)(p->frame * (systemStatus[0] ? 1.2075409f : 1.0f));
    _PushCurrentMatrix();
    fr = bga_ntscFrame(p->frame);
    i = (int)fr;
    i1 = i + 1;
    if (i > p->num - 1) {
        i = p->num - 1;
    }
    if (i1 > p->num - 1) {
        i1 = p->num - 1;
    }
    k0 = &p->key[i];
    k1 = &p->key[i1];
    t = fr - (float)i;
    {
        VECTOR at;
        VECTOR pos;
        VECTOR pos0 = {k0->pos[0], k0->pos[1], k0->pos[2], 1.0f};
        VECTOR at0 = {k0->at[0], k0->at[1], k0->at[2], 1.0f};
        VECTOR pos1 = {k1->pos[0], k1->pos[1], k1->pos[2], 1.0f};
        VECTOR at1 = {k1->at[0], k1->at[1], k1->at[2], 1.0f};
        VECTOR up;
        VECTOR dir;
        float roll0;
        float fov0;
        float roll1;
        float fov1;
        float roll;
        float fov;
        short a;

        memset(&up, 0, sizeof(up));
        up.y = 1.0f;
        fov0 = k0->fov;
        roll0 = k0->roll;
        fov1 = k1->fov;
        roll1 = k1->roll;
        _InterVectorXYZ(&pos, &pos0, &pos1, 1.0f - t);
        _InterVectorXYZ(&at, &at0, &at1, 1.0f - t);
        fov = fov0 * (1.0f - t) + fov1 * t;
        roll = roll0 * (1.0f - t) + roll1 * t;
        dir.x = at.x - pos.x;
        dir.y = at.y - pos.y;
        dir.z = at.z - pos.z;
        dir.w = 0.0f;
        _NormalizeVector(&dir, &dir);
        _InitCurrentMatrix();
        _RotCurrentMatrixZ((short)(roll * 182.04445f));
        _ApplyCurrentMatrix(&up, &up);
        _SetCameraMatrix(bgaCameraMatrix, &pos, &dir, &up);
        if (GlobalTimer != 0) {
            if (_GetLength(bgaCameraMatrix[3], bgaLastCameraPos) < 100.0f) {
                GlobalTimer = 0;
                currentScreenWidth = 0;
            }
        }
        a = (short)(fov * 3.1415927f / 360.0f * 10430.378f);
        bgaZoom = 224.0f / (GetTableSin(a) / GetTableCos(a));
        _PopCurrentMatrix();
    }
    p->frame += 1.0f;
}

static void bga_addLightning(int kind, BgaLightningDef *def, float *vec, int id, int t0, float f)
{
    BgaLightning *p;

    for (p = bgaLightningList; p != 0; p = p->next) {
        if (p->id == id) {
            switch (kind) {
            case 15:
                def->kind = -1;
                /* FALLTHROUGH */
            case 14:
                p->def = def;
                p->frame = f;
                p->t0 = t0;
                p->seg[0].key = -1;
                _CopyVector(p->seg[0].pos.f, vec);
                return;
            case 16: {
                LightningNode *e = &p->seg[p->n];

                e->key = def->kind;
                _CopyVector(p->seg[p->n].pos.f, vec);
                p->n = p->n + 1;
                return;
            }
            default:
                debug_StdPrintfDummy("illegal lightning data set.\n");
                debug_assert(__FILE__, 2960);
                __assert(__FILE__, 2960, "0");
                return;
            }
        }
    }
#ifdef ICO_HOST
    /* the EE allocates 352 bytes for the 0x158-byte record; a 64-bit host's
       record is larger */
    p = iosMallocDebug(ios_partition_seki, sizeof(BgaLightning) > 352 ? sizeof(BgaLightning) : 352,
                       __FILE__, 2968);
#else
    p = iosMallocDebug(ios_partition_seki, 352, __FILE__, 2968);
#endif
    p->next = bgaLightningList;
    p->id = id;
    p->n = 1;
    bgaLightningList = p;
    switch (kind) {
    case 15:
        def->kind = -1;
        /* FALLTHROUGH */
    case 14:
        p->def = def;
        p->frame = f;
        p->t0 = t0;
        _CopyVector(p->seg[0].pos.f, vec);
        break;
    case 16: {
        LightningNode *e = &p->seg[p->n];

        e->key = def->kind;
        _CopyVector(p->seg[p->n].pos.f, vec);
        p->def = 0;
        _UnitVector(p->seg[0].pos.f);
        p->n = p->n + 1;
        break;
    }
    default:
        debug_StdPrintfDummy("illegal lightning data set.\n");
        debug_assert(__FILE__, 2994);
        __assert(__FILE__, 2994, "0");
        break;
    }
}

/* DrawLightningN reads the colour as four words, so it is a 16-byte record
   here and not four separate ints. */
typedef struct BgaLightningCol { /* field names derived */
    unsigned int c[4];
} __attribute__((aligned(16))) BgaLightningCol; /* derived name */

/* the definition's four colour bytes widened into the 16-byte record
   DrawLightningN reads */
static inline BgaLightningCol bga_lightningColor(BgaLightningDef *g) /* derived name */
{
    BgaLightningCol c;

    c.c[0] = g->col[0];
    c.c[1] = g->col[1];
    c.c[2] = g->col[2];
    c.c[3] = g->col[3];
    return c;
}

void bga_DispLightning(void)
{
    BgaLightning *p;
    BgaLightningDef *g;
    GObj *o;
    int cnt;
    int i;
    int num;
    int k;
    float z;
    BgaLightning *list[100];
    BgaLightningCol col;

    cnt = 0;
    num = 0;
    for (p = bgaLightningList; p != 0; p = p->next) {
        if (p->def->kind == 0) {
            list[num++] = p;
        }
    }
    if (num > 0) {
        z = 0.0f;
        i = 0;
        for (o = isysGObjSearchFromObjKindID_begin(4); o != 0;
             o = isysGObjSearchFromObjKindID_next(o)) {
            if (isEnemyHyde(o) != 0) {
                continue;
            }
            if (((GObj *)o)->active == 0) {
                continue;
            }
            cnt++;
            if (cnt >= 5) {
                continue;
            }
            p = list[i++];
            i %= num;
            g = p->def;
            if (systemStatus[5] == 0) {
                k = p->n;
                if (k < 10) {
                    p->n = k + 1;
                    _CopyVector(&p->seg[k], (char *)GOBJ_SUB(o)->nodeMtx + (g->node << 6) + 0x30);
                }
            }
            col.c[0] = g->col[0];
            col.c[1] = g->col[1];
            col.c[2] = g->col[2];
            col.c[3] = g->col[3];
            DrawLightningN(p->n, p->seg, &col, g->stepMin, g->stepMax, g->swayStepMin,
                           g->swayStepMax, g->turnMin, g->turnMax, g->swayLimit, g->width,
                           g->texLen, p->frame + z, g->c);
            z += 0.01f;
        }
    }
    for (p = bgaLightningList; p != 0; p = p->next) {
        g = p->def;
        if (g == 0) {
            debug_StdPrintfDummy(
                "Lightning data does not found! maybe, start point < end point.\n");
            debug_assert(__FILE__, 3067);
            __assert(__FILE__, 3067, "0");
            continue;
        }
        col = bga_lightningColor(g);
        if (p->t0 != 0) {
            continue;
        }
        switch (g->kind) {
        case 1:
            o = isysGObjSearchFromObjKindID_begin(2);
            if (o == 0) {
                continue;
            }
            if (systemStatus[5] == 0) {
                k = p->n;
                if (k < 10) {
                    p->n = k + 1;
                    _CopyVector(&p->seg[k], (char *)GOBJ_SUB(o)->nodeMtx + (g->node << 6) + 0x30);
                }
            }
            DrawLightningN(p->n, p->seg, &col, g->stepMin, g->stepMax, g->swayStepMin,
                           g->swayStepMax, g->turnMin, g->turnMax, g->swayLimit, g->width,
                           g->texLen, p->frame, g->c);
            break;
        case 2:
            for (o = isysGObjGetExist_begin(); o != 0; o = isysGObjGetExist_next(o)) {
                if (objLayout[o->labelId].mdl == 71) {
                    if (systemStatus[5] == 0) {
                        k = p->n;
                        if (k < 10) {
                            p->n = k + 1;
                            _CopyVector(&p->seg[k],
                                        (char *)GOBJ_SUB(o)->nodeMtx + (g->node << 6) + 0x30);
                        }
                    }
                    DrawLightningN(p->n, p->seg, &col, g->stepMin, g->stepMax, g->swayStepMin,
                                   g->swayStepMax, g->turnMin, g->turnMax, g->swayLimit, g->width,
                                   g->texLen, p->frame, g->c);
                }
            }
            break;
        case 3:
            for (o = isysGObjGetExist_begin(); o != 0; o = isysGObjGetExist_next(o)) {
                if (objLayout[o->labelId].mdl == 74) {
                    if (systemStatus[5] == 0) {
                        k = p->n;
                        if (k < 10) {
                            p->n = k + 1;
                            _CopyVector(&p->seg[k],
                                        (char *)GOBJ_SUB(o)->nodeMtx + (g->node << 6) + 0x30);
                        }
                    }
                    DrawLightningN(p->n, p->seg, &col, g->stepMin, g->stepMax, g->swayStepMin,
                                   g->swayStepMax, g->turnMin, g->turnMax, g->swayLimit, g->width,
                                   g->texLen, p->frame, g->c);
                }
            }
            break;
        case 0:
            break;
        default:
            DrawLightningN(p->n, p->seg, &col, g->stepMin, g->stepMax, g->swayStepMin,
                           g->swayStepMax, g->turnMin, g->turnMax, g->swayLimit, g->width,
                           g->texLen, p->frame, g->c);
            break;
        }
    }
}

inline void bga_ResetCamera(void)
{
    bgaCameraActive = 0;
}

inline int bga_GetCameraMatrix(void *p)
{
    int v = bgaCameraActive;
    if (v != 0) {
        _CopyMatrix(p, bgaCameraMatrix);
        v = bgaCameraActive;
    } else {
        bgaZoom = 0;
    }
    return v != 0 && bgaCameraForceOff == 0;
}

inline void bga_SetCamFrame(char *data, int frame, int mode, int loop)
{
    BgaSdfCam *p = (BgaSdfCam *)data;

    p->mode = mode;
    bgaCameraActive = 1;
    if (mode == 1) {
        GlobalTimer = mode;
        bgaStreamSync = mode;
        _CopyVector(bgaLastCameraPos, bgaCameraMatrix[3]);
    }
    if (frame == -1) {
        p->frame = bga_palFrame(p->num);
    } else {
        p->frame = bga_palFrame(frame);
    }
}

inline int bga_CheckAnimationFinish(BgaHeader *p)
{
    float f = p->end;
    float t = p->frame;
    int r = 0;

    if (systemStatus[0]) {
        if (f * 0.82812935f <= t || p->mode != 1) {
            r = 1;
        }
    } else {
        if (f <= t || p->mode != 1) {
            r = 1;
        }
    }
    return r;
}

inline int bga_CheckAnimationFrame(BgaHeader *p, int frame, int reset)
{
    float f = frame;
    float t = p->frame;
    int r = 0;

    if (systemStatus[0]) {
        if (f * 0.82812935f <= t || p->mode != 1) {
            r = 1;
        }
    } else {
        if (f <= t || p->mode != 1) {
            r = 1;
        }
    }
    if (r && reset) {
        p->mode = 0;
    }
    return r;
}

inline int bga_CheckAnimationFrameIn(BgaHeader *p, int in, int out)
{
    float a = in;
    float t = p->frame;
    int r = 0;

    if (systemStatus[0] ? a * 0.82812935f <= t : a <= t) {
        float b = out;

        if (systemStatus[0] ? t < b * 0.82812935f : t < b) {
            r = p->mode == 1;
        }
    }
    return r;
}

inline int bga_CheckSdfCameraFinish(char *data)
{
    BgaSdfCam *p = (BgaSdfCam *)data;
    float f = p->num;
    float t = p->frame;

    if (systemStatus[0]) {
        return f * 0.82812935f <= t;
    }
    return f <= t;
}

inline int bga_CheckSdfCameraFrame(char *data, int frame, int reset)
{
    BgaSdfCam *p = (BgaSdfCam *)data;
    float f = frame;
    float t = p->frame;
    int r;

    if (systemStatus[0]) {
        r = f * 0.82812935f <= t;
    } else {
        r = f <= t;
    }
    if (r && reset) {
        p->mode = 0;
    }
    return r;
}

inline int bga_CheckSdfCameraFrameIn(char *data, int in, int out)
{
    BgaSdfCam *p = (BgaSdfCam *)data;
    float t = p->frame;
    float a = in;
    int r = 0;

    if (systemStatus[0] ? a * 0.82812935f <= t : a <= t) {
        float b = out;

        if (systemStatus[0] ? t < b * 0.82812935f : t < b) {
            r = 1;
        }
    }
    return r;
}

inline void bga_SetCameraForceOff(void)
{
    bgaCameraForceOff = 1;
}

inline void bga_InitBGA(void)
{
    bgaCameraForceOff = 0;
    bgaLightningList = 0;
}

inline void bga_SetUniqAnimationFlag(int val)
{
    bgaUniqAnimationFlag = val;
}

inline void bga_ResetAnimation(void)
{
    BgaLightning *p;
    bgaCameraActive = 0;
    if (systemStatus[5] != 0) {
        return;
    }
    p = bgaLightningList;
    bgaLightningList = 0;
    if (p == 0) {
        return;
    }
    do {
        BgaLightning *next = p->next;
        freeseki(p);
        p = next;
    } while (p != 0);
}

inline float bga_GetZoom(void)
{
    return bgaZoom;
}
