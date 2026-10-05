#include "ee_view.h"
#include "typedef.h"
#include "debug.h"
#include "commonact.h"
#include "gv.h"
#include "item.h"
#include <libvu0.h>
#include "act-wish.h"
#include "enemy_act.h"
#include "act-game.h"
#include "boyact.h"
#include "main.h"

static inline unsigned char chkOrient(char *s, float *dir, float *w, float deg) /* derived name */
{
    float *q = ICO_RAWP(float *, s, 0x4B0, ((Act *)s)->env.wallOrient);

    if (q[0] == 0.0f && q[1] == 0.0f && q[2] == 0.0f && q[3] == 0.0f) {
        debug_StdPrintfDummy("orient null");
        return 0;
    }
    sceVu0ScaleVector(w, q, -1.0f);
    if ((float)(_RotyGV(dir, w) < 0 ? -_RotyGV(dir, w) : _RotyGV(dir, w)) < deg) {
        return 1;
    }
    return 0;
}

void ACTGetWish_FromPad(GObj *self, float *dir)
{
    float v[4];
    float u[4];
    float p[4];
    GObj *o;
    Act *s = GOBJ_ACT(self);
    float deg;

    v[0] = test_CURRENTROOT(self)[0];
    v[1] = test_CURRENTROOT(self)[1];
    v[2] = test_CURRENTROOT(self)[2];

    s->wish2.ll |= 1ULL << 37;
    s->wish2.ll |= 1ULL << 36;
    s->wish2.ll |= 1ULL << 33;

    s->wish4.ll |= 2;

    if ((s->pad.now & 0x20) || GOBJ_WORK(self)->stickMag < 0.9) {
        s->wish4.ll |= 4;
    }
    s->wish3.ll |= 1ULL << 63;
    s->wish4.ll |= 1;

    s->wish3.ll |= 1ULL << 62;

    s->wish3.ll |= 1ULL << 55;
    s->wish3.ll |= 1ULL << 56;
    s->wish3.ll |= 1ULL << 57;
    s->wish3.ll |= 1ULL << 58;
    s->wish4.ll |= 0x20;
    s->wish4.ll |= 0x40;
    s->wish4.ll |= 0x400;
    s->wish4.ll |= 0x800;
    s->wish4.ll |= 0x1000;
    s->wish2.ll |= 1ULL << 59;
    s->wish2.ll |= 1ULL << 62;
    s->wish2.ll |= 1ULL << 63;
    s->wish3.ll |= 1;
    s->wish3.ll |= 2;

    s->wish3.ll |= 1ULL << 34;

    if (self == boyGObj) {
        if (0.1f < s->stick.mag && ((int)(s->wish1.ll >> 5) & 1) &&
            chkOrient((char *)s, dir, u, 80.0f)) {
            if (!(s->pad.now & 8) || girlGObj == 0 || GOBJ_WORK(girlGObj)->sofaTimer == 0) {
                s->wish3.ll |= 0x20;
            }
        }
    } else {
        s->wish3.ll |= 0x20;
    }

    s->wish3.ll |= 0x800;
    s->wish3.ll |= 0x1000;
    s->wish3.ll |= 0x2000;
    s->wish3.ll |= 0x4000;
    s->wish3.ll |= 0x8000;
    s->wish3.ll |= 0x10000;
    s->wish3.ll |= 0x20000;
    s->wish4.ll |= 0x4000;

    s->wish3.ll |= 1ULL << 59;
    s->wish3.ll |= 1ULL << 60;
    s->wish3.ll |= 1ULL << 61;
    s->wish2.ll |= 1ULL << 60;
    s->wish2.ll |= 1ULL << 61;

    s->wish3.ll |= 0x1000000;
    s->wish3.ll |= 0x4000000;
    s->wish3.ll |= 0x2000000;

    s->wish3.ll |= 1ULL << 50;
    s->wish3.ll |= 1ULL << 51;
    s->wish3.ll |= 1ULL << 49;
    s->wish3.ll |= 0x80;
    if (0.1f < s->stick.mag && (s->stick.angle >= -45 && s->stick.angle <= 45)) {
        s->wish3.ll |= 0x200;
        s->wish3.ll |= 0x100;
    }

    switch ((unsigned int)s->actMode) {
    case 1:
        if (s->modeFrame >= 181) {
            s->wish2.ll |= 1ULL << 38;
        }
        break;
    case 4:
    case 5:
    case 18:
    case 62:
        s->wish3.ll |= 1ULL << 46;
        s->wish3.ll |= 1ULL << 45;
        s->wish3.ll |= 1ULL << 47;
        s->wish3.ll |= 1ULL << 48;
        s->wish3.ll |= 1ULL << 52;
        s->wish3.ll |= 1ULL << 53;
        s->wish3.ll |= 1ULL << 54;
        break;
    case 13:
        s->wish2.ll |= 1ULL << 54;
        break;
    }

    if (s->wish1.ll & 0xC00000) {
        if (self == boyGObj) {
            if (chkOrient((char *)s, dir, u, 80.0f)) {
                s->wish3.ll |= 0x400000;
                if (s->pad.now & 0x10) {
                    s->wish3.ll |= 0x800000;
                }
            }
        } else {
            s->wish3.ll |= 0x400000;
        }
    }

    if (((int)(s->wish1.ll >> 18) & 1) && chkOrient((char *)s, dir, u, 80.0f)) {
        s->wish3.ll |= 0x40000;
    }

    if (((int)(s->wish1.ll >> 3) & 1) && (s->pad.now & 0x10) &&
        chkOrient((char *)s, dir, u, 90.0f)) {
        s->wish3.ll |= 8;
    }

    if (((int)(s->wish0.ll >> 32) & 1) && chkOrient((char *)s, dir, u, 80.0f)) {
        if (self == boyGObj) {
            if (s->pad.now & 0x10) {
                s->wish3.ll |= 0x80000;

                s->wish3.ll |= 0x40;
            }
        } else {
            s->wish3.ll |= 0x80000;
            s->wish3.ll |= 0x8000000;
            s->wish3.ll |= 0x10000000;
            s->wish3.ll |= 0x20000000;
            if (self == girlGObj && girlControlMode == 0 &&
                (s->flags20.ll & (0xC000ULL << 23)) == (0xC000ULL << 23)) {
                s->wish3.ll &= ~0x80000;
            }
        }
    }

    if (((int)(s->wish1.ll >> 20) & 1) && chkOrient((char *)s, dir, u, 80.0f)) {
        s->wish3.ll |= 0x100000;
    }

    if (self == girlGObj && girlControlMode != 0) {
        if (!(s->pad.now & 0x10)) {
            s->wish3.ll &= ~0x80000;
            s->wish3.ll &= ~0x100000;
        }
    }

    s->wish4.ll |= 0x10;

    if (((int)(s->wish2.ll >> 3) & 1) && chkOrient((char *)s, dir, u, 60.0f)) {
        s->wish4.ll |= 8;
    }

    if (0.1f < s->stick.mag || ((int)(s->flags20.ll >> 3) & 1)) {
        s->wish2.ll |= 1ULL << 34;
        s->wish2.ll |= 1ULL << 35;
    }

    if (s->pad.trg & 0x10) {
        s->wish2.ll |= 1ULL << 39;
        if (0.1f < s->stick.mag) {
            s->wish2.ll |= 1ULL << 40;

            s->wish2.ll |= 1ULL << 41;
            GOBJ_WORK(self)->padWish[0] = dir[0];
            GOBJ_WORK(self)->padWish[1] = dir[1];
            GOBJ_WORK(self)->padWish[2] = dir[2];
        }
        s->wish2.ll |= 1ULL << 42;
        s->wish2.ll |= 1ULL << 43;
    }

    if (s->pad.trg & 0x80) {
        /* both arms set the same bit */
        if (self->kind == 1) {
            s->wish2.ll |= 1ULL << 44;
        } else {
            s->wish2.ll |= 1ULL << 44;
        }

        if (0.1f < s->stick.mag) {
            s->wish2.ll |= 1ULL << 45;
        }
    }

    if ((0x3C - systemStatus[0] * 0xA) / systemStatus[1] / 4 < s->handFreeFrame &&
        (s->pad.now & 8)) {
        s->flags18.ll |= 1ULL << 43;
    }

    if (s->pad.now & 8) {
        if (GOBJ_SUB(self)->ctrl.motion == 0xBA) {
            GOBJ_WORK(self)->noInterpTimer = (0x3C - systemStatus[0] * 0xA) / systemStatus[1] / 6;
        }
        if (GOBJ_WORK(self)->wishHoldTimer == 0) {
            GOBJ_WORK(self)->wishHoldTimer =
                (0x3C - systemStatus[0] * 0xA) / systemStatus[1] * 0x50 / 0x3C;
        }
        s->wish2.ll |= 1ULL << 46;
        s->wish4.ll |= 0x80;
        s->wish4.ll |= 0x100;
        s->wish4.ll |= 0x200;
        s->wish2.ll |= 1ULL << 56;
        s->wish2.ll |= 1ULL << 58;
        s->wish2.ll |= 1ULL << 57;
        s->wish4.ll |= 0x2000;
        if (!(0.1f < s->stick.mag)) {
            s->wish3.ll |= 4;
            s->wish3.ll |= 0x10;
        }
    }

    if (GOBJ_WORK(self)->wishHoldTimer != 0) {
        s->wish2.ll |= 1ULL << 46;
    }

    if (optionControlType == 1 ? (s->pad.trg & 8) != 0 : (s->pad.now & 8) != 0) {
        s->wish2.ll |= 1ULL << 47;
    }

    if (optionControlType == 1 ? (s->pad.trg & 8) != 0 : (s->pad.now & 8) == 0) {
        s->wish2.ll |= 1ULL << 48;
    }

    if (s->pad.now & 0x20) {
        s->wish3.ll |= 1ULL << 36;
        s->wish3.ll |= 1ULL << 37;
        s->wish3.ll |= 1ULL << 38;
        s->wish3.ll |= 1ULL << 39;
        s->wish3.ll |= 1ULL << 40;
        s->wish3.ll |= 1ULL << 41;
        s->wish3.ll |= 1ULL << 42;
    }

    if (s->pad.trg & 0x20) {
        s->wish3.ll |= 1ULL << 44;
    }

    if (s->pad.now & 0x20) {
        ACTSearchGObj(self, 0x13, 0x2D, (ICO_WORD *)&o, u, 100.0f);

        if (o != 0 && CheckCarryableItem(o)) {
            deg = self == boyGObj ? 60.0f : 80.0f;
            p[0] = test_CURRENTROOT(o)[0];
            p[1] = test_CURRENTROOT(o)[1];
            p[2] = test_CURRENTROOT(o)[2];
            if (p[1] > v[1] && (float)(p[1] - v[1] < 0.0f ? -(p[1] - v[1]) : (p[1] - v[1])) < deg) {
                s->nextItem.i = (ICO_WORD)o;
                s->wish3.ll |= 1ULL << 43;
            }
        }
        s->wish2.ll |= 1ULL << 54;
        s->wish2.ll |= 1ULL << 55;
    }

    if (s->pad.now & 0x10) {
        s->wish3.ll |= 0x400;
    }

    if (0.1f < s->stick.mag) {
        s->wish2.ll |= 1ULL << 49;
    }

    if (self == boyGObj) {
        if (s->pad.trg & 0x40) {
            if (0.1f < s->stick.mag && (s->stick.angle >= -45 && s->stick.angle <= 45)) {
                s->wish2.ll |= 1ULL << 50;
            } else if (0.1f < s->stick.mag && (s->stick.angle >= 46 && s->stick.angle <= 134)) {
                s->wish2.ll |= 1ULL << 52;
            } else if (0.1f < s->stick.mag && s->stick.angle >= -134 && s->stick.angle <= -46) {
                s->wish2.ll |= 1ULL << 53;
            }
            s->wish2.ll |= 1ULL << 51;
        }
    }

    if (s->pad.trg & 0x20) {
        s->wish3.ll |= 1ULL << 35;
        s->wish3.ll |= 1ULL << 33;
    }

    if ((int)(s->wish3.ll >> 37) & 1) {
        if ((int)(s->wish1.ll >> 37) & 1) {
            ICO_RAW(int, GOBJ_ACT(self)->enemy, 0x2C0, GOBJ_ACT(self)->enemy->word2C0) = 1;
        }
    } else {
        ICO_RAW(int, GOBJ_ACT(self)->enemy, 0x2C0, GOBJ_ACT(self)->enemy->word2C0) = 0;
    }
}
