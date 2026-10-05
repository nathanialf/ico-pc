#include "typedef.h"
#include "gobj.h"
#include "matrixDrive.h"
#include "chandelier.h"
#include "rope.h"
#include "DisplayP2O.h"

inline int InitChandelierGeo(void)
{
    return 0;
}

void ChandelierGeo(GObj *self)
{
    GObj *obj = isysGObjSearchFromObjKindID_begin(20);
    if (obj != 0) {
        CopyMatrix(MatrixDrive_GetMatrix(), (float *)GOBJ_SUB(self)->nodeMtx);
        MatrixDrive_TransMatrix(0.0f, 50.0f, 250.0f);
        SetRopeFixPoint(obj, MatrixDrive_GetMatrix()[3], 0);
    }
}

void ChandelierDL(GObj *self)
{
    Sub15C *d = self->dobj;
    if (d->disp != 0) {
        p2o_SetDefaultEnviroment();
        p2o_DispVU1DObjMulti(d);
    }
}
