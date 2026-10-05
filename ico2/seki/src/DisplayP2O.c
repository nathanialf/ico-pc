#include "typedef.h"
#include "DisplayP2O.h"
#include "debug.h"
#include "Packet.h"
#include "RegistPacket.h"
#include "MicroCode.h"
#include <libdma.h>
#include "Shadow.h"
#include "Basic.h"

/* debug_PrintFontWindow's format for the display-object counter, in a
   32-byte array. */
static const char dispObjFormat[32] = "display object = %d"; /* derived name */

/* the display-object count p2o_HideDispVU1 records and reports, none yet */
static int dispObjCount = -1; /* derived name */

void p2o_MakePacket(Sub15C *dobj)
{
    dobj->model->dobj = dobj;
    pac_MakePacket(dobj);
}

inline void p2o_SetDefaultEnviroment(void) {}

void p2o_DispShadowVolume(GObj *self)
{
    shadow_Render(self->dobj);
}

void p2o_HideDispVU1(int count)
{
    dispObjCount = count;
    if (debug_window_flag != 0) {
        debug_PrintFontWindow(0xCCCCCC00, dispObjFormat, count);
    }
}

void p2o_DispVU1DObj(void *req)
{
    reg_DispObj(req);
}

void p2o_DispVU1DObjMulti(void *req)
{
    reg_DispObj(req);
}

void p2o_DispVU1Multi(GObj *self)
{
    p2o_DispVU1DObjMulti(GOBJ_SUB(self));
}

void p2o_DispVU1MultiDefault(GObj *self)
{
    p2o_DispVU1Multi(self);
}

void p2o_DispVU1(GObj *self)
{
    p2o_DispVU1DObj(GOBJ_SUB(self));
}

void p2o_DispVU1Default(GObj *self)
{
    p2o_DispVU1(self);
}

void p2o_TransMicroProgram(void)
{
#ifdef ICO_HOST
    /* no microprogram image on the host (MicroCode.c): the kick's address
       word is the table's 0, without reading an int as a pointer */
    sceDmaSend(dmaVif, 0);
#else
    sceDmaSend(dmaVif, MicroCodeAddress[1]);
#endif
}
