/*
 * ico2/ito/include/act_bird.h
 *
 * The declarations of what act_bird.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ACT_BIRD_H
#define ACT_BIRD_H

#include "StageAnimation.h"

/* The bird's own work record, hung on its sub-object (Sub15C work). */
typedef struct BirdWork { /* field names derived */
    float home[4];        /* 0x00, where the bird was placed */
    char scared;          /* 0x10, a mail 423 sender came within 200 */
    char pad11[15];       /* 0x11 */
    float scarer[4];      /* 0x20, where that sender stood */
    BgaPlayNode *bga;     /* 0x30, the take-off's play node */
    char pad34[12];       /* 0x34 */
} BirdWork;               /* derived name */

float vector_angle_degree(void *a, void *b);
void subBirdControl(void *volatile gobj);
void subBirdCollision(void *volatile gobj);
void actBirdStart(void *gobj);
BirdWork *InitBirdGeo(struct GObj *gobj, void *home);
void BirdAI(void);


void _ACTSendMailToBirdAll(int mail, void *data);
void _ACTSendMailToBird(void *obj, int mail, void *data);
void subBirdBrainMain(void *gobj);

#endif /* ACT_BIRD_H */
