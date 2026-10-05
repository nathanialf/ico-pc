/*
 * ico2/omori/include/ebrain.h
 *
 * The declarations of what ebrain.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef EBRAIN_H
#define EBRAIN_H

#include "typedef.h"

/* how many enemies chase the boy and the girl */
extern int eBrainBoyChaseCount;
extern int eBrainGirlChaseCount;

struct GObj;

typedef struct EBSlot { /* field names derived */
    unsigned short status; /* 0x00, 0 idle, 1 chasing the boy, 2 chasing the girl */
    char pad2[2];
    struct GObj *target;   /* 0x04, the GObj the enemy is sent after */
    float dist[2];         /* 0x08, [0] to the boy, [1] to the girl */
    int message;           /* 0x10, the brain message waiting for the enemy */
    int chaseFrames;       /* 0x14, frames spent chasing the boy */
    struct GObj *owner;    /* 0x18, the enemy GObj the slot belongs to */
} EBSlot; /* derived name */

void eBrainInit(void);
void eBrainProcess(void);
ICO_WORD eBrainStatusSet(struct GObj *gop, int status);
void eBrainSendMes(struct GObj *gop, int mes);
int GetStageFromLabel(int label);
int eBrainGetTargetGeneratorFromLabelStage(int label, int stage);

EBSlot *eBrainGetTarget(struct GObj *gop);
int eBrainGetTargetGeneratorFromLabel(int label);


#endif /* EBRAIN_H */
