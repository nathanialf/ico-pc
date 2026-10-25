/*
 * ico2/seki/include/StageAnimation.h
 *
 * The declarations of what StageAnimation.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef STAGEANIMATION_H
#define STAGEANIMATION_H

#include <libvu0.h>

/* stage-anim: one stage animation, 0x5C bytes. Reader: ico2/seki/src/
 * StageAnimation.c (stage_ApplyData, the BGA set-up, (r - table) / 0x5C). */
typedef struct {       /* field names derived */
    char path[64];     /* 0x00, the .bga file, "NULL" for none */
    int objFirst;      /* 0x40, the objTableScene range */
    int objLast;       /* 0x44 */
    int group;         /* 0x48, copied into the BGA header's group */
    unsigned char cut; /* 0x4C, copied into the BGA header's cut flag */
    char pad4D[3];
    int loop;   /* 0x50, the loop flag the animation calls take */
    void *data; /* 0x54, the loaded BGA data or the data pointer */
    int no;     /* 0x58, the row's own index, -1 for none */
} StageAnimDef; /* derived name */

/* stage-anim-model: one stage animation object, 8 bytes, the objTableScene
   range a stage animation's 0x40 and 0x44 words bound: the object kind and
   its argument. */
typedef struct { /* field names derived */
    int kind;    /* 0x00 */
    int aux;     /* 0x04 */
} StgObjDat;     /* derived name */

extern const StgObjDat objTableScene[];

/* The play node stage_MakePlayBgAnimation links into bgaPlayList and
   stage_DispBgAnimation walks.  Its first word holds int bit-fields: the
   14-bit animation number, the kill flag and the play flag. */
typedef struct BgaPlayNode { /* field names derived */
    int no : 14; /* 0x00 */
    int play : 1;
    int kill : 1;
    short num;                /* 0x02 */
    float frame;              /* 0x04 */
    float speed;              /* 0x08 */
    float scale;              /* 0x0C */
    struct BgaPlayNode *prev; /* 0x10, the node made after this one */
    struct BgaPlayNode *next; /* 0x14, the node made before it */
    int pad18[2];
    sceVu0FVECTOR pos; /* 0x20 */
    float rot[4];      /* 0x30 */
} BgaPlayNode;         /* derived name */

/* StageAnimation.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
int stage_CheckAnimationFinish(int key);
int stage_CheckAnimationFrame(int key, int frame, int reset);
void stage_SetLoopFlag(int key, int loop);
void stage_SetFrameStep(int target, int val);
void stage_SetParentOfGObj(int key, void *parent);

void stage_SetParentOfGObjWithLocalRotationFlag(int key, void *parent, int localRotation);
void stage_SetLocalizeGeometry(int key, float *pos, float *rot);
void stage_KillPlayBgAnimationIfOverMaxCount(int key, int maxCount);

int stage_CheckAnimationFrameIn(int key, int in, int out);
void stage_ApplyData(char *name, char *data);
int stage_ContinueAnimation(int key, int next);
void stage_DispAnimation(void);
int stage_DispBgAnimation(BgaPlayNode **self);

int stage_DispBgAnimationNoFinish(BgaPlayNode **self);

void stage_KillPlayBgAnimation(BgaPlayNode **self);
BgaPlayNode *stage_MakePlayBgAnimation(int key);
float stage_PlayBgAnimation(int key, float t, void *v, void *q);
float stage_PlayBgAnimationDissolve(int key, void *v, void *q, float t, float dv);
void stage_SetAnimation(int key, int mode, int frame);

void stage_SetScale(int key, float scale);
void stage_ResetAnimation(void);

void stage_CalcAnimationNoParent(void);
void stage_CalcAnimationParent(void);
int stage_Init(void);

#ifdef ICO_RD
struct BgaHeader;
/* PC port: the BGA header of stage animation row key when this stage set the
   row up (stage_Init), else NULL (port/game/photo_view.c, photo mode's Deja
   vu) */
struct BgaHeader *stage_HostBgaHeader(int key);
#endif

#endif /* STAGEANIMATION_H */
