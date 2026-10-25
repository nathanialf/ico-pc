#include "typedef.h"
#include "particleEffect.h"
#include "particleLayout.h"
#include "ios.h"
#include "memory.h"

inline int *InitParticleLayoutGeo(GObj *self, int *layout)
{
    int *r;
    r = iosMallocDebug(ios_partition_sugipon, 4, "src/particleLayout.c", 12);
    *r = SetParticleEffect(layout[12], layout, GOBJ_SUB(self)->quat);
    return r;
}

inline void DeleteParticleLayout(GObj *self)
{
    int *q = GOBJ_SUB(self)->work;

    if (ICO_NTSCAUG6) {
        DeleteParticleEffect(q[0]);
    } else {
        if (q[0] != -1) {
            DeleteParticleEffect(q[0]);
            q[0] = -1;
        }
    }
}

static inline void ParticleLayoutGeo_ntscaug6(GObj *self) {}

void ParticleLayoutGeo(GObj *self)
{
    if (ICO_NTSCAUG6) {
        ParticleLayoutGeo_ntscaug6(self);
        return;
    }
    int *q = GOBJ_SUB(self)->work;
    if (q[0] == -1) {
        return;
    }
    if (self->drawMask) {
        return SetParticleEffectPauseFlag(q[0], 0);
    } else {
        return SetParticleEffectPauseFlag(q[0], 1);
    }
}

inline void ParticleLayoutDL(void) {}
