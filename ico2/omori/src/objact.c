#include "gobj.h"
#include "obj_manager.h"
#include "objact.h"
#include "typedef.h"
#include "act2.h"
#include "gamesys.h"

/* the data-only member obj-action.o, read as typedef.h's OaRecB rows; no
   header declares it */
extern OaRecB objAction[];

/* the object's action record, or none */
static inline OaRecB *objActionRecord(int label) /* derived name */
{
    int e = objLayout[label].action;
    if (e != 0) {
        return &objAction[e];
    }
    return 0;
}

inline void ObjAction_Init(void)
{
    GObj *p = isysGObjGetExist_begin();
    while (p != 0) {
        ObjAction_CorrectGeo(p->labelId, 0);
        p = isysGObjGetExist_next(p);
    }
}

static inline void objActionCorrectFlag(OaRecB *p) /* derived name */
{
    if ((p->flags & 1) == 1u) {
        p->mode = PAL_JPN(972, 949);
    }
}

static inline void objActionCorrectMode(OaRecB *p) /* derived name */
{
    if (p->mode == PAL_JPN(972, 949)) {
        p->mode = p->baseMode;
    }
}

inline void ObjAction_CorrectGeo(int label, int unused)
{
    OaRecB *p;
    if (label < 0)
        return;
    p = objActionRecord(label);
    if (p == 0)
        return;
    objActionCorrectFlag(p);
    objActionCorrectMode(p);
}

inline void ObjAction_Mail(void *data, int mail)
{
    void *p = isysGObjGetExist_begin();
    while (p != 0) {
        iosOmSendMail(p, mail, data);
        p = isysGObjGetExist_next(p);
    }
}

inline void ObjAction_MailCenter(GObj *gobj, int step)
{
    int i;
    const ObjActMailEnt *e;
    int n;

    for (i = 0; i < 33; i++) {
        e = &objTrigger[i];
        if (gobj->labelId != e->labelId)
            continue;
        n = e->triggerNo;
        if (step > 0) {
            if (n < 5)
                continue;
            n += step;
        }
        ObjAction_Mail(gobj, objTriggerDef[n]);
    }
}

void ObjectBeforeFunc(GObj *self)
{
    BeforeFunc2(self);
}
