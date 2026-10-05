#include "ee_view.h"
#include "typedef.h"
#include "debug.h"
#include "memory.h"
#include "gobj.h"
#include "matrixDrive.h"
#include "particleEffect.h"
#include "quaternion.h"
#include "DisplayP2O.h"

typedef struct CandleFlame { /* field names derived */
    int effect;              /* 0x0 */
    int off;                 /* 0x4 */
} CandleFlame;               /* derived name */

#include "candle.h"
#include "ios.h"

ICO_WORD InitCandleGeo(void *self, void *mtx)
{
    Sub15C *w = GOBJ_SUB(self);
    CandleFlame *flame;
    int i;

    if (w->nodeNum >= 2) {
        flame = (CandleFlame *)iosMallocDebug(
            ios_partition_sugipon, w->nodeNum * ICO_MAX_SIZE(CandleFlame, 8), "src/candle.c", 24);
        for (i = 0; i < w->nodeNum; i++) {
            CopyMatrix(MatrixDrive_GetMatrix(), (char *)w->nodeMtx + i * 64);
            MatrixDrive_TransMatrix(0.0f, -40.0f, 0.0f);
            flame[i].effect = SetParticleEffect(4, MatrixDrive_GetMatrix()[3], IdentityQuaternion);
            flame[i].off = 0;
        }
    } else {
        flame = (CandleFlame *)iosMallocDebug(ios_partition_sugipon, ICO_MAX_SIZE(CandleFlame, 8),
                                              "src/candle.c", 35);
        flame->effect = SetParticleEffect(4, mtx, IdentityQuaternion);
        flame->off = 0;
    }
    debug_StdPrintfDummy("\x1b[33mInitialize candle geometries.\x1b[m\n");
    return (ICO_WORD)flame;
}

void CandleGeo(void *self)
{
    Sub15C *w = GOBJ_SUB(self);
    CandleFlame *flame = w->work;
    /* the release pass reads the display object through its own handle */
    Sub15C *cw = GOBJ_SUB(self);
    int i;

    if (w->nodeNum >= 2) {
        for (i = 0; i < w->nodeNum; i++) {
            CopyMatrix(MatrixDrive_GetMatrix(), (char *)GOBJ_SUB(self)->nodeMtx + i * 64);
            MatrixDrive_TransMatrix(0.0f, -40.0f, 0.0f);
            if (flame[i].effect != -1) {
                SetParticleEffectGeometry(flame[i].effect, MatrixDrive_GetMatrix()[3],
                                          IdentityQuaternion);
            }
        }
        if (cw->disp == 0) {
            for (i = 0; i < cw->nodeNum; i++) {
                if (flame[i].effect != -1) {
                    DeleteParticleEffect(flame[i].effect);
                    flame[i].effect = -1;
                    flame[i].off = 1;
                }
            }
        }
    }
}

inline void _deleteLayoutedCandleParticleEffect(void *gobj)
{
    CandleFlame *flame;
    int i;

    flame = GOBJ_SUB(gobj)->work;
    if (GOBJ_SUB(gobj)->nodeNum >= 2) {
        for (i = 0; i < GOBJ_SUB(gobj)->nodeNum; i++) {
            if (flame[i].off == 0) {
                DeleteParticleEffect(flame[i].effect);
                flame[i].effect = -1;
                flame[i].off = 1;
            }
        }
    }
}

inline void DeleteLayoutedCandleParticleEffect(void)
{
    void *gobj;

    gobj = isysGObjSearchFromObjKindID_begin(34);
    while (gobj != 0) {
        _deleteLayoutedCandleParticleEffect(gobj);
        gobj = isysGObjSearchFromObjKindID_next(gobj);
    }
}

void CandleDL(GObj *self)
{
    Sub15C *d = self->dobj;
    if (d->disp != 0) {
        p2o_SetDefaultEnviroment();
        p2o_DispVU1DObjMulti(d);
    }
}
