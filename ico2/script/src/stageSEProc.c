#include "gobj.h"
#include "s_init.h"
#include "boyact.h"
#include "camera-ico2.h"
#include "gflag.h"
#include "script.h"
#include "geometryManager.h"
#include "torch.h"
#include "weapon.h"
#include "windManager.h"
#include <libvu0.h>
#include <string.h>
#include "stageSEProc.h"
#include "main.h"

/* where a stage sound plays from, the three floats SeSlot.pos points at */
typedef struct { /* field names derived */
    float x;
    float y;
    float z;
} SEPos; /* derived name */

typedef union { /* field names derived */
    float f[4];

    struct {
        long long a;
        long long b;
    } q;
} Blk16; /* derived name */

typedef struct { /* field names derived */
    Blk16 a;
    Blk16 b;
} Blk32; /* derived name */

/* .rodata: the two points stageSE02astrong measures the camera against, and
   the centre and half size of the two trigger boxes stageSE08astrong and
   stageSE10lstrong hand scpTriggerPosBox. */
static const Blk32 se02aPoints = {{{-1137.0f, -659.0f, 432.0f, 0.0f}},
                                  {{-1822.0f, -1071.0f, 2165.0f, 0.0f}}}; /* derived name */

static const Blk16 se08aBoxCenter = {{-2050.0f, -2005.0f, 3529.0f, 0.0f}}; /* derived name */

static const Blk16 se08aBoxSize = {{1400.0f, 1400.0f, 2400.0f, 0.0f}}; /* derived name */

static const Blk16 se10lBoxCenter = {{141.0f, 1328.0f, -122.0f, 0.0f}}; /* derived name */

static const Blk16 se10lBoxSize = {{600.0f, 700.0f, 1000.0f, 0.0f}}; /* derived name */

/* the host calls camera-root.h's GetCameraPos(void) as declared: the
   argument some routines pass (an a0 the EE loads and the callee ignores)
   is dropped */
extern void *GetCameraPos(void);

#define GetCameraPos(...) GetCameraPos()

/* the two box tests are each their own routine, inlined at every site, and
   each names its own pair */
static inline int se08aInStrongBox(void) /* derived name */
{
    float *pos = (float *)GetCameraPos();
    Blk16 center = se08aBoxCenter;
    Blk16 size = se08aBoxSize;

    return scpTriggerPosBox(pos, (float *)&center, (float *)&size);
}

static inline int se10lInStrongBox(float *pos) /* derived name */
{
    Blk16 center = se10lBoxCenter;
    Blk16 size = se10lBoxSize;
    return scpTriggerPosBox(pos, (float *)&center, (float *)&size);
}

/* the stage sound object each stageSE routine is handed: the sound slot
   (fumi/include/s_init.h's SeSlot, 0x40 bytes on the EE) */

/* The wind-speed cache the strong-wind routines share: the last value of
   GetRegularizedWindSpeed and the frame_count it was read on, both in .sdata
   with an explicit 0. */
static float windCache = 0.0f; /* derived name */

static int windCacheFrame = 0; /* derived name */

int stageSEtaimatsu(SeSlot *self)
{
    Blk16 v;
    Blk16 d;
    GObj *g = isysGObjSearchFromObjKindID_begin(10);
    GObj *best = 0;
    GObj *torch = 0;
    float bd = 3.40282347e+38f; /* FLT_MAX */
    int rv = 1;
    float *campos;
    int i;

    if (gflagChk(389)) {
        return 0;
    }
    campos = (float *)GetCameraPos();
    self->stereoRate = 0.5f;
    if (boyGObj != 0) {
        GObj *w = GetBoyWeaponGObj();

        if (w != 0) {
            torch = GetTorchGObjOfWeapon(w);
        }
    }
    for (i = 0; g != 0; i++, g = isysGObjSearchFromObjKindID_next(g)) {
        int id;
        float t;

        if (IsTorchLightOn(g) == 0) {
            continue;
        }
        if (g == torch) {
            continue;
        }
        id = self->env->se;
        if ((i & 1) == 0) {
            id -= 426;
        } else {
            id -= 428;
        }
        if ((unsigned int)id >= 2) {
            continue;
        }
        GetRootPosition(&v, g);
        sceVu0SubVector(&d, campos, &v);
        t = sceVu0InnerProduct(&d, &d);
        if (t < bd) {
            bd = t;
            best = g;
        }
    }
    if (best != 0) {
        /* the translation row of the torch's first node matrix */
        sceVu0CopyVector(self->pos, (float *)GOBJ_SUB(best)->nodeMtx + 12);
    } else {
        rv = 0;
    }
    return rv;
}

/* .sbss: the running level of each river sound effect, held across frames so
   SEFadeOut can walk it down to silence. */
static float river04eLevelA; /* derived name */

static float river04eLevelB; /* derived name */

static float river06aLevel; /* derived name */

/* the river fade-out, inlined into stageSE04eriver (twice) and
 * stageSE06ariver */
static inline int SEFadeOut(SeSlot *self, float *lvl) /* derived name */
{
    float v = *lvl - riverFadeSpeed;

    *lvl = v;
    self->volumeRate = v;
    if (0.0f < v) {
        return 1;
    }
    self->volumeRate = 0.0f;
    *lvl = 0.0f;
    return 0;
}

int stageSE04eriver(SeSlot *self)
{
    float *p = (float *)GetCameraPos();
    float z = p[2];
    float r;

    if (stage_no == 21) {
        if (gflagChk(230)) {
            return SEFadeOut(self, &river04eLevelA);
        }
        river04eLevelA = 1.0f;
    } else {
        if (gflagChk(231)) {
            return SEFadeOut(self, &river04eLevelB);
        }
        river04eLevelB = 1.0f;
    }
    if (-5500.0f < z) {
        if (z < -5500.0f) {
            r = 0.0f;
        } else if (-2000.0f < z) {
            r = 1.0f;
        } else {
            r = (z - -5500.0f) / 3500.0f;
        }
        self->volumeRate = 1.0f - r;
    } else {
        if (z < -5750.0f) {
            r = 0.0f;
        } else if (-5500.0f < z) {
            r = 1.0f;
        } else {
            r = (z - -5750.0f) / 250.0f;
        }
        self->volumeRate = r;
    }
    if (self->volumeRate < 0.5f && p[1] < -1000.0f && -6500.0f < p[2]) {
        self->volumeRate = 0.5f;
    }
    return -1;
}

int stageSE06ariver(SeSlot *self)
{
    float *p = (float *)GetCameraPos();

    if (gflagChk(106)) {
        return SEFadeOut(self, &river06aLevel);
    }
    river06aLevel = 1.0f;
    if (p[0] < 300.0f && 848.0f < p[2]) {
        if (gflagChk(107) == 0) {
            self->volumeRate = 0.05f;
        } else {
            self->volumeRate = 0.4f;
        }
        self->stereoRate = 0.1f;
        self->pos[0] = -490.0f;
        self->pos[1] = -757.0f;
        self->pos[2] = 759.0f;
        return 1;
    }
    self->volumeRate = 0.7f;
    self->flag.all &= 0xEFFFFFFF;
    self->stereoRate = 0.75f;
    self->pos[0] = 740.0f;
    self->pos[1] = p[1];
    self->pos[2] = p[2];
    return 1;
}

int stageSE10lstrong2(SeSlot *self)
{
    float *p = (float *)GetCameraPos();
    float f;
    float w;

    if (p[1] < 227.0f) {
        f = 0.0f;
    } else if (1000.0f < p[1]) {
        f = 1.0f;
    } else {
        f = (p[1] - 227.0f) / 773.0f;
    }
    self->volumeRate = f;
    if (f < 0.2f) {
        self->volumeRate = 0.2f;
    }
    if (windCacheFrame == frame_count) {
        w = windCache;
    } else {
        float e;
        windCacheFrame = frame_count;
        e = GetRegularizedWindSpeed((void *)GetCameraPos());
        e = e * 0.5f + 0.5f;
        windCache = e;
        w = e;
    }
    self->volumeRate = self->volumeRate * w;
    if (se10lInStrongBox(p) != 0) {
        self->volumeRate = self->volumeRate * 0.5f;
    }
    return -1;
}

static inline Blk16 *SENearestPoint(Blk16 *list, int n) /* derived name */
{
    Blk16 d;
    Blk16 *best;
    float bd;
    void *campos;
    int i;

    best = 0;
    bd = 3.40282347e+38f; /* FLT_MAX */
    campos = (void *)GetCameraPos();
    for (i = n - 1; i != -1; i--) {
        float t;
        sceVu0SubVector(&d, campos, list);
        t = sceVu0InnerProduct(&d, &d);
        if (t < bd) {
            best = list;
            bd = t;
        }
        list++;
    }
    return best;
}

typedef struct { /* field names derived */
    float x;
    float y;
    float z;
    float w;
} __attribute__((aligned(16))) SEVec; /* derived name */

int stageSE19ataki(SeSlot *self)
{
    float *p = (float *)GetCameraPos();
    SEVec pts[6] = {
        {-1227.0f, p[1], 759.0f}, {-1194.0f, p[1], -208.0f}, {-171.0f, p[1], -1837.0f},
        {114.0f, p[1], -1526.0f}, {114.0f, p[1], -1526.0f},  {-987.0f, p[1], -2788.0f},
    };

    self->stereoRate = 0.8f;
    self->flag.all &= 0xEFFFFFFF;
    sceVu0CopyVector(self->pos, SENearestPoint((Blk16 *)pts, 6));
    return 1;
}

/* the per-frame fade step SEFadeOut subtracts; st04e, st05e and st06a set it
   (0.005f, or 1000.0f to cut the river at once) */
float riverFadeSpeed = 0.0f; /* derived name */

int stageSE02astrong(SeSlot *self)
{
    Blk32 v;
    float w;

    v = se02aPoints;
    sceVu0CopyVector(self->pos, SENearestPoint((Blk16 *)&v, 2));
    if (windCacheFrame == frame_count) {
        w = windCache;
    } else {
        windCacheFrame = frame_count;
        w = GetRegularizedWindSpeed((void *)GetCameraPos());
        w = w * 0.5f + 0.5f;
        windCache = w;
    }
    self->volumeRate = w;
    return 1;
}

int stageSE02ataki(SeSlot *self)
{
    float *p = self->pos;
    p[0] = 785.0f;
    p[2] = 482.0f;
    self->stereoRate = 0.5f;
    if (gflagChk(106)) {
        self->volumeRate = 0.0f;
    }
    return 1;
}

int stageSE02atakib(SeSlot *self)
{
    float *p = self->pos;
    p[0] = 785.0f;
    p[1] = 1786.0f;
    p[2] = 482.0f;
    self->stereoRate = 0.5f;
    if (gflagChk(106)) {
        self->volumeRate = 0.0f;
    }
    return 1;
}

int stageSE03tsuiro(void)
{
    int r = GetCameraGroupCurrent();
    if (r == 3 || r == 9) {
        return -1;
    }
    return 0;
}

int stageSE03tnotSuiro(void)
{
    int r = GetCameraGroupCurrent();
    int busy = (r == 3 || r == 9) ? -1 : 0;
    if (busy != 0) {
        return 0;
    }
    return -1;
}

int stageSE04agate(SeSlot *self)
{
    float x = ((float *)GetCameraPos(self))[2];
    float ratio = 1.0f;
    float w;
    if (x < -1300.0f) {
        return 0;
    }
    if (gflagChk(140) == 0) {
        return 0;
    }
    if (x < 750.0f) {
        ratio = (x - -1300.0f) / 2050.0f;
    }
    if (windCacheFrame == frame_count) {
        w = windCache;
    } else {
        windCacheFrame = frame_count;
        w = GetRegularizedWindSpeed((void *)GetCameraPos());
        w = w * 0.5f + 0.5f;
        windCache = w;
    }
    self->volumeRate = ratio * w;
    return -1;
}

int stageSE04bstrong(SeSlot *self)
{
    float v;
    if (windCacheFrame == frame_count) {
        v = windCache;
    } else {
        windCacheFrame = frame_count;
        v = GetRegularizedWindSpeed((void *)GetCameraPos()) * 0.5f + 0.5f;
        windCache = v;
    }
    self->volumeRate = v;
    return -1;
}

int stageSE04ewind(SeSlot *self)
{
    float x = ((float *)GetCameraPos())[2];
    float f;
    if (x < -5770.0f) {
        f = 0.0f;
    } else if (x > -4900.0f) {
        f = 1.0f;
    } else {
        f = (x - -5770.0f) / 870.0f;
    }
    self->volumeRate = 1.0f - f;
    return -1;
}

int stageSE04eriverDown(SeSlot *self)
{
    float x = ((float *)GetCameraPos(self))[2];
    float f;
    if (stage_no == 21) {
        if (gflagChk(230)) {
            return 0;
        }
    } else {
        if (gflagChk(231)) {
            return 0;
        }
    }
    if (x < -5770.0f) {
        f = 0.0f;
    } else if (-3680.0f < x) {
        f = 1.0f;
    } else {
        f = (x - -5770.0f) / 2090.0f;
    }
    self->volumeRate = f;
    return -1;
}

int stageSE06astrong(SeSlot *self)
{
    float *p = (float *)GetCameraPos(self);
    float f;
    float v;
    if (p[0] < 300.0f && 848.0f < p[2]) {
        if (p[0] < -1753.0f) {
            f = 0.0f;
        } else if (-1145.0f < p[0]) {
            f = 1.0f;
        } else {
            f = (p[0] - -1753.0f) / 608.0f;
        }
        v = (1.0f - f) * 0.3f;
        self->volumeRate = v;
        if (v < 0.05f) {
            self->volumeRate = 0.05f;
        }
        return -1;
    } else {
        float *q = self->pos;
        self->volumeRate = 1.0f;
        q[0] = -2400.0f;
        q[1] = p[1];
        q[2] = p[2];
        return 1;
    }
}

int stageSE06abirdIn(int *self)
{
    float *p = (float *)GetCameraPos((ICO_WORD)self);
    if (p[0] < 300.0f && 848.0f < p[2]) {
        return -1;
    }
    return 0;
}

int stageSE06abirdOut(int *self)
{
    float *p = (float *)GetCameraPos((ICO_WORD)self);
    if (p[0] < 300.0f && 848.0f < p[2]) {
        return 0;
    }
    return -1;
}

int stageSE06ataimatsu(int *self)
{
    float *p = (float *)GetCameraPos((ICO_WORD)self);
    if (p[0] < 300.0f) {
        if (848.0f < p[2]) {
            return stageSEtaimatsu(self);
        }
    }
    return 0;
}

int stageSE08astrong(SeSlot *self)
{
    float f;
    if (windCacheFrame == frame_count) {
        f = windCache;
    } else {
        float e;
        windCacheFrame = frame_count;
        e = GetRegularizedWindSpeed((void *)GetCameraPos());
        e = e * 0.5f + 0.5f;
        windCache = e;
        f = e;
    }
    if (se08aInStrongBox() == 0) {
        self->volumeRate = f;
    } else {
        self->volumeRate = f * 0.05f;
    }
    return -1;
}

int stageSE08astrong2(SeSlot *self)
{
    float f;
    float w;
    if (windCacheFrame == frame_count) {
        w = windCache;
    } else {
        float e;
        windCacheFrame = frame_count;
        e = GetRegularizedWindSpeed((void *)GetCameraPos());
        e = e * 0.5f + 0.5f;
        windCache = e;
        w = e;
    }
    f = 1.0f - w;
    if (se08aInStrongBox() == 0) {
        self->volumeRate = f;
    } else {
        self->volumeRate = f * 0.05f;
    }
    return -1;
}

int stageSE08anoise3(SeSlot *self)
{
    if (se08aInStrongBox() == 0) {
        self->volumeRate = 1.0f;
    } else {
        self->volumeRate = 0.2f;
    }
    return -1;
}

int stageSE08ataimatsu(ICO_WORD self)
{
    if (se08aInStrongBox() == 0) {
        return 0;
    }
    return stageSEtaimatsu(self);
}

int stageSE08bcrane(SeSlot *self)
{
    SEPos *p = (SEPos *)self->pos;
    float f;
    p->x = 1148.0f;
    p->y = -4521.0f;
    p->z = 1514.0f;
    if (windCacheFrame == frame_count) {
        f = windCache;
    } else {
        windCacheFrame = frame_count;
        f = GetRegularizedWindSpeed((void *)GetCameraPos());
        f = f * 0.5f + 0.5f;
        windCache = f;
    }
    self->volumeRate = f;
    return 1;
}

int stageSE08brail(SeSlot *self)
{
    SEPos *p = (SEPos *)self->pos;
    float f;
    p->x = -114.0f;
    p->y = -3679.0f;
    p->z = 6186.0f;
    if (windCacheFrame == frame_count) {
        f = windCache;
    } else {
        windCacheFrame = frame_count;
        f = GetRegularizedWindSpeed((void *)GetCameraPos());
        f = f * 0.5f + 0.5f;
        windCache = f;
    }
    self->volumeRate = f;
    return 1;
}

int stageSE09asea(SeSlot *self)
{
    SEPos *p = (SEPos *)self->pos;
    p->x = 1800.0f;
    p->y = 585.0f;
    p->z = -5000.0f;
    self->volumeRate = 1.0f;
    return 1;
}

int stageSE10lstrong(SeSlot *self)
{
    float *p = (float *)GetCameraPos();
    float f;
    float w;
    if (p[1] < 227.0f) {
        f = 0.0f;
    } else if (1000.0f < p[1]) {
        f = 1.0f;
    } else {
        f = (p[1] - 227.0f) / 773.0f;
    }
    if (windCacheFrame == frame_count) {
        w = windCache;
    } else {
        float e;
        windCacheFrame = frame_count;
        e = GetRegularizedWindSpeed((void *)GetCameraPos());
        e = e * 0.5f + 0.5f;
        windCache = e;
        w = e;
    }
    self->volumeRate = (1.0f - f) * w;
    if (se10lInStrongBox(p) != 0) {
        self->volumeRate = self->volumeRate * 0.5f;
    }
    return -1;
}

int stageSE10rstrong(SeSlot *self)
{
    float x = ((float *)GetCameraPos(self))[2];
    float f;
    if (x < -300.0f) {
        f = 0.0f;
    } else if (400.0f < x) {
        f = 1.0f;
    } else {
        f = (x - -300.0f) / 700.0f;
    }
    self->volumeRate = f * 0.3f;
    return -1;
}

int stageSE10rstrong2(SeSlot *self)
{
    float f;
    if (windCacheFrame == frame_count) {
        f = windCache;
    } else {
        windCacheFrame = frame_count;
        f = GetRegularizedWindSpeed((void *)GetCameraPos());
        f = f * 0.5f + 0.5f;
        windCache = f;
    }
    self->volumeRate = f;
    return -1;
}

int stageSE13arain(SeSlot *self)
{
    float *v1 = self->pos;
    v1[0] = 118.0f;
    v1[1] = -192.0f;
    v1[2] = -46.0f;
    self->stereoRate = 0.5f;
    return 1;
}

int stageSE13cNoise(SeSlot *self)
{
    float *v1 = self->pos;

    v1[0] = -133.0f;
    v1[1] = -5698.0f;
    v1[2] = -966.0f;
    return 1;
}

/* stageSE13dterrace, and below it the static inline copy of the same body
   that stageSE13dstrong calls */
int stageSE13dterrace(void)
{
    float *p = (float *)GetCameraPos();
    if (p[1] > -1000.0f)
        return 0;
    return -1;
}

static inline int stageSE13dterrace_(void) /* derived name */
{
    float *p = (float *)GetCameraPos();
    if (p[1] > -1000.0f)
        return 0;
    return -1;
}

int stageSE13dstrong(SeSlot *self)
{
    int r = stageSE13dterrace_();
    float f;
    if (windCacheFrame == frame_count) {
        f = windCache;
    } else {
        float e;
        windCacheFrame = frame_count;
        e = GetRegularizedWindSpeed((void *)GetCameraPos());
        e = e * 0.5f + 0.5f;
        windCache = e;
        f = e;
    }
    self->volumeRate = 1.0f - f * 0.5f;
    if (r == -1) {
        soundReverbDepthSet(20);
    }
    return r;
}

int stageSE17astrong(SeSlot *self)
{
    float f;
    GetCameraPos(self);
    if (windCacheFrame == frame_count) {
        f = windCache;
    } else {
        windCacheFrame = frame_count;
        f = GetRegularizedWindSpeed((void *)GetCameraPos());
        f = f * 0.5f + 0.5f;
        windCache = f;
    }
    self->volumeRate = f;
    return -1;
}

int stageSE18awind(SeSlot *self)
{
    float x = ((float *)GetCameraPos(self))[0];
    float f;
    if (x < -1000.0f) {
        f = 0.0f;
    } else if (1125.0f < x) {
        f = 1.0f;
    } else {
        f = (x - -1000.0f) / 2125.0f;
    }
    self->volumeRate = f * 0.7f;
    return -1;
}

int stageSE17brain(SeSlot *self)
{
    float x = ((float *)GetCameraPos(self))[0];
    float f;
    if (x < -5500.0f) {
        f = 0.0f;
    } else if (-3800.0f < x) {
        f = 1.0f;
    } else {
        f = (x - -5500.0f) / 1700.0f;
    }
    self->volumeRate = 1.0f - f;
    return -1;
}

int stageSE17bstrong(SeSlot *self)
{
    float x = ((float *)GetCameraPos(self))[0];
    float f;
    float w;
    if (x < -5500.0f) {
        f = 0.0f;
    } else if (-3800.0f < x) {
        f = 1.0f;
    } else {
        f = (x - -5500.0f) / 1700.0f;
    }
    self->volumeRate = 1.0f - f;
    if (windCacheFrame == frame_count) {
        w = windCache;
    } else {
        float e;
        windCacheFrame = frame_count;
        e = GetRegularizedWindSpeed((void *)GetCameraPos());
        e = e * 0.5f + 0.5f;
        windCache = e;
        w = e;
    }
    self->volumeRate = self->volumeRate * w;
    return -1;
}

int stageSE17btaki(SeSlot *self)
{
    float a, b;
    float *p = self->pos;
    a = -5136.0f;
    b = 1518.0f;
    p[0] = a;
    p[2] = b;
    return 1;
}

int stageSE19astrong(SeSlot *self)
{
    float *p = self->pos;
    float f;
    p[0] = 1548.0f;
    p[2] = -3296.0f;
    if (windCacheFrame == frame_count) {
        f = windCache;
    } else {
        windCacheFrame = frame_count;
        f = GetRegularizedWindSpeed((void *)GetCameraPos());
        f = f * 0.5f + 0.5f;
        windCache = f;
    }
    self->volumeRate = f;
    return 1;
}

int stageSE19arain(SeSlot *self)
{
    float *p = self->pos;
    register float a = 3190.0f;
    register float b = 3163.0f;
    register float c = -1332.0f;
    p[0] = a;
    p[1] = b;
    p[2] = c;
    return 1;
}

int stageSE20astrong(SeSlot *self)
{
    float *p = self->pos;
    float f;
    float a = -746.0f, b = -685.0f;
    p[0] = a;
    p[1] = b;
    p[2] = -398.0f;
    if (windCacheFrame == frame_count) {
        f = windCache;
    } else {
        windCacheFrame = frame_count;
        f = GetRegularizedWindSpeed((void *)GetCameraPos());
        f = f * 0.5f + 0.5f;
        windCache = f;
    }
    self->volumeRate = f;
    return 1;
}

int stageSE20astrong2(SeSlot *self)
{
    float f;
    if (windCacheFrame == frame_count) {
        f = windCache;
    } else {
        windCacheFrame = frame_count;
        f = GetRegularizedWindSpeed((void *)GetCameraPos());
        f = f * 0.5f + 0.5f;
        windCache = f;
    }
    self->volumeRate = f;
    return -1;
}

int stageSE22astrong(SeSlot *self)
{
    float f;
    if (windCacheFrame == frame_count) {
        f = windCache;
    } else {
        float e;
        windCacheFrame = frame_count;
        e = GetRegularizedWindSpeed((void *)GetCameraPos());
        e = e * 0.5f + 0.5f;
        windCache = e;
        f = e;
    }
    self->volumeRate = 1.0f - f * 0.5f;
    return -1;
}

int stageSE22arain(SeSlot *self)
{
    float x = ((float *)GetCameraPos(self))[2];
    float f;
    if (x < -8000.0f) {
        f = 0.0f;
    } else if (-1355.0f < x) {
        f = 1.0f;
    } else {
        f = (x - -8000.0f) / 6645.0f;
    }
    self->volumeRate = f;
    if (f < 0.3f) {
        self->volumeRate = 0.3f;
    }
    return -1;
}

int stageSE24astrong(SeSlot *self)
{
    float f;
    if (windCacheFrame == frame_count) {
        f = windCache;
    } else {
        windCacheFrame = frame_count;
        f = GetRegularizedWindSpeed((void *)GetCameraPos());
        f = f * 0.5f + 0.5f;
        windCache = f;
    }
    self->volumeRate = f;
    return -1;
}

unsigned int stageSE24arain(SeSlot *self)
{
    SEPos *p = (SEPos *)self->pos;
    p->x = 1771.0f;
    p->z = -4949.0f;
    self->stereoRate = 0.5f;
    return 1;
}

int stageSE24ariver(SeSlot *self)
{
    float a, b;
    float *p = self->pos;
    a = 1478.0f;
    b = 1484.0f;
    p[0] = a;
    p[2] = b;
    return 1;
}

int stageSE47anoise(SeSlot *self)
{
    float x = ((float *)GetCameraPos(self))[1];
    float f;
    if (x < -3422.0f) {
        f = 0.0f;
    } else if (-122.0f < x) {
        f = 1.0f;
    } else {
        f = (x - -3422.0f) / 3300.0f;
    }
    self->volumeRate = 1.0f - f;
    return -1;
}
