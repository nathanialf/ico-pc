/*
 * ico2/seki/include/BgAnimation.h
 *
 * The declarations of what BgAnimation.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef BGANIMATION_H
#define BGANIMATION_H

#include "typedef.h" /* GObj */
#include <libvu0.h>

struct BgaAnimObj;
struct BgaDObjEnt;
struct BgaLightningDef;

/* The 0x30-byte animation record bga_InitData allocates for a BGA file and
   fills from bgaAnimDefault: the position and rotation, the object and node
   the animation hangs from, and whether it takes the node's own matrix. */
typedef struct BgaAnim { /* field names derived */
    /* 0x00 */ sceVu0FVECTOR pos;
    /* 0x10 */ sceVu0FVECTOR quat;
    /* 0x20 */ struct BgaAnimObj *obj;
    /* 0x24 */ int idx;
    /* 0x28 */ int root;
    /* 0x2C */ char pad2C[4];
} BgaAnim; /* derived name */

/* The head of a BGA file: its "BGA" magic, the group number stage_Init
   copies in, the play state (-1 off, 0 held, 1 playing), the camera-cut
   flag, the DObj list and the root list bga_InitData builds from it, the
   frame range, the step and the current frame, and the animation record it
   allocates. */
typedef struct BgaHeader { /* field names derived */
    char magic[4];
    int group;                 /* 0x04 */
    char pad8[2];
    signed char mode;          /* 0x0A */
    char cut;                  /* 0x0B */
    int dobjs;                 /* 0x0C */
    struct BgaDObjEnt **roots; /* 0x10 */
    float start;               /* 0x14 */
    float end;                 /* 0x18 */
    float step;                /* 0x1C */
    float frame;               /* 0x20 */
    BgaAnim *anim;             /* 0x24 */
} BgaHeader;                   /* derived name */

/* set when an animation's camera cut restarts the global timer; the stream
   motion player resynchronises on it and clears it */
extern int bgaStreamSync;

/* BgAnimation.c's `inline` functions, in the order of their definitions'
   out-of-line copies at the end of the object (first-declaration order). */
void bga_ResetCamera(void);
int bga_GetCameraMatrix(void *p);
char *bga_InitSdfCamera(char *data);
void bga_SetCamFrame(char *data, int frame, int mode, int loop);
int bga_CheckAnimationFinish(BgaHeader *p);
int bga_CheckAnimationFrame(BgaHeader *p, int frame, int reset);
int bga_CheckAnimationFrameIn(BgaHeader *p, int in, int out);
int bga_CheckSdfCameraFinish(char *data);
int bga_CheckSdfCameraFrame(char *data, int frame, int reset);
int bga_CheckSdfCameraFrameIn(char *data, int in, int out);
void bga_SetCameraForceOff(void);
void bga_InitBGA(void);
void bga_SetUniqAnimationFlag(int val);
void bga_ResetAnimation(void);
float bga_GetZoom(void);

char *bga_InitData(char *data);
void bga_ApplyDObject(struct BgaDObjEnt *p, GObj **objs, int n, int no);
void bga_SetFrame(BgaHeader *p, int frame, int mode, int loop);
void bga_CalcAnimation(BgaHeader *p, int loop, int reset);
void bga_CalcSdfCamera(char *data, int loop);
void bga_DispLightning(void);

#endif /* BGANIMATION_H */
