#include "matrixDrive.h"
#include "quaternion.h"
#include "multiBgaManager.h"
#include "ios.h"
#include "Matrix.h"
#include "main.h"
#include "StageAnimation.h"
#include "memory.h"

/* the state InitMultiBgaManager resets every slot to */
BgaAnimeState InitialBgaMultiAnimeState = {
    -1.0f,
    {0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
    -1,
    0,
};

static inline void entryMultiBga(BgaDisp *bga, int no, int kind, void *pos,
                                 void *rot) /* derived name */
{
    BgaDisp *p = &bga[no];

    p->kind = kind;
    if (!ICO_NTSCAUG6JUN26) {
        p->stay = 0;
    }
    CopyVector(p->pos, pos);
    CopyVector(p->vel, ZeroVector);
    CopyQuaternion(p->rot, rot);
    p->frame = 0.0f;
}

BgaDisp *InitMultiBgaManager(int n)
{
    BgaDisp *base = iosMallocDebug(ios_partition_sugipon, n * 0x50, "src/multiBgaManager.c", 11);
    int i;
    for (i = 0; i < n; i++) {
        base[i] = *(BgaDisp *)&InitialBgaMultiAnimeState;
    }
    return base;
}

void EntryMultiBgaManager(BgaDisp *bga, int no, int kind, void *pos, void *rot)
{
    entryMultiBga(bga, no, kind, pos, rot);
}

inline void EntryMultiBgaManagerNoKind(BgaDisp *bga, int no, void *pos)
{
    entryMultiBga(bga, no, -1, pos, IdentityQuaternion);
}

void EntryMultiBgaManagerSensitive(BgaDisp *bga, int no, int kind, void *pos, void *rot, void *sens)
{
    BgaDisp *p = &bga[no];

    p->kind = kind;
    if (!ICO_NTSCAUG6JUN26) {
        p->stay = 0;
    }
    CopyVector(p->pos, pos);
    CopyVector(p->vel, sens);
    CopyQuaternion(p->rot, rot);
    p->frame = 0.0f;
}

void DispMultiBgaManager(BgaDisp *base, int n)
{
    int i;
    int ri;
    float f;
    for (i = 0; i < n; i++) {
        BgaDisp *e = &base[i];
        f = e->frame;
        if (f < 0.0f) {
            continue;
        }
        ri = (int)stage_PlayBgAnimation(e->kind, f, e->pos, e->rot);
        if (systemStatus[5] != 0) {
            continue;
        }
        e->frame = (float)ri;
        _AddVector(e->pos, e->pos, e->vel);
    }
}

inline void DispMultiBgaManagerWithKind(int kind, BgaDisp *base, int n)
{
    int i;
    int ri;
    float f;
    for (i = 0; i < n; i++) {
        BgaDisp *e = &base[i];
        f = e->frame;
        if (f < 0.0f) {
            continue;
        }
        ri = (int)stage_PlayBgAnimation(kind, f, e->pos, e->rot);
        if (systemStatus[5] != 0) {
            continue;
        }
        e->frame = (float)ri;
    }
}
