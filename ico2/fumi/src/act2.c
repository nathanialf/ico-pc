#include "act.h"
#include "act2.h"
#include "isys.h"

void BeforeFunc2(GObj *self)
{
    Act *act = GOBJ_ACT(self);

    if (act != 0) {
        /* the mail box and the three lists to walk, the last a -1 terminator */
        IosMailBox *mb = &self->mailBox;
        ActMail *lists[3] = {act->mail, act->mainMail, (ActMail *)-1};
        ActMail *p;
        int i;
        int j;

        for (i = 0; lists[i] != (ActMail *)-1; i++) {
            p = lists[i];
            if (p == 0) {
                continue;
            }
            while (p->mail != PAL_NTSC_AUG6JUN26(429, 396, 392)) {
                for (j = 0; j < mb->num; j++) {
                    if (mb->mail[j].type == p->mail) {
                        goto found;
                    }
                }
                p++;
            }
        }
        mb->num = 0;
        return;

    found:
        act->intrKind = mb->mail[j].type;
        mb->num = 0;
        if (p->func != 0) {
            actChangeActMain(isysCurrentGObj, p->func, &act->actProc);
        }
        if (p->sub != 0) {
            actCreateSubThread(p->sub, 20);
        }
        if (p->motion != 0) {
            actCreateMotionThread(p->motion, 21, &act->motProc);
        }
    }
}

void actDummy(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
}
