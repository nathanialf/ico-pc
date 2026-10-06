#include "sceneManager.h"
#include "main.h"

/* int (void *, int, int) here, void (struct GObj *, int, int) in flag.h */
extern int SetFlag4PointFixID(void *gobj, int turns, int id);

/* The 0x40-byte layout record CreateLayoutedGObj takes: three 16-byte vectors
   and the model id the loader resolves (sceneManager.h's SObjSimpleSetting
   with its alignment tail as a pad).  The vectors are copied as two
   doublewords each. */
typedef union { /* field names derived */
    float f[4];
    long long ll[2];
} WmVec; /* derived name */

typedef struct { /* field names derived */
    WmVec pos;   /* 0x00 */
    WmVec rot;   /* 0x10 */
    WmVec scale; /* 0x20 */
    int id;      /* 0x30 */
    char pad34[12];
} WmLayout; /* derived name */

/* the head of the vane's display object, Sub15C's parent link (the object
   and node the vane hangs from), written as words: the owner is held as an
   int, and the slot at 0x15C is read again for each store */
typedef struct { /* field names derived */
    int owner;   /* 0x00, parent.obj */
    int node;    /* 0x04, parent.node */
} WmWork;        /* derived name */

int InitWindMillGeo(ICO_WORD owner, WmLayout *src)
{
    WmLayout lay;
    GObj *gobj;
    GObj *gobj2;
    int i;

    lay = *src;
    for (i = 0; i < 4; i++) {
        if (stage_no == 101) {
            lay.id = 20;
        } else {
            lay.id = 18;
        }
        gobj = CreateLayoutedGObj(46, 656, -1, 0, &lay, -1, 7, 0);
        /* WmWork is the EE layout of parent: obj is 8 bytes here */
        GOBJ_SUB(gobj)->parent.obj = (GObj *)owner;
        GOBJ_SUB(gobj)->parent.node = 0;
        SetFlag4PointFixID(gobj, i, 0);

        if (stage_no == 101) {
            lay.id = 21;
        } else {
            lay.id = 19;
        }
        gobj2 = CreateLayoutedGObj(46, 656, -1, 0, &lay, -1, 7, 0);
        /* WmWork is the EE layout of parent: obj is 8 bytes here */
        GOBJ_SUB(gobj2)->parent.obj = (GObj *)owner;
        GOBJ_SUB(gobj2)->parent.node = 0;
        SetFlag4PointFixID(gobj2, i, 1);
    }
    return 0;
}

void WindMillGeo(void) {}

void WindMillDL(void) {}
