#include "typedef.h"
#include "cageFix.h"
#include "cage.h"
#include "gobj.h"
#include "DisplayP2O.h"
#include "matrixDrive.h"

inline int InitCageFixGeo(void)
{
    return 0;
}

void CageFixGeo(GObj *self)
{
    GObj *g = isysGObjSearchFromObjKindID_begin(PAL_NTSCAUG6JUN26(44, 43));
    if (g != 0) {
        CopyMatrix(MatrixDrive_GetMatrix(), (char *)GOBJ_SUB(self)->nodeMtx);
        SetCageFixGeometry(g, MatrixDrive_GetMatrix()[3], (float *)GOBJ_SUB(self)->nodeQuat);
    }
}

void CageFixDL(GObj *self)
{
    Sub15C *d = self->dobj;
    if (d->disp != 0) {
        p2o_SetDefaultEnviroment();
        p2o_DispVU1DObjMulti(d);
    }
}
