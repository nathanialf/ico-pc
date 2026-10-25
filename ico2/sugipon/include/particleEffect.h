/*
 * ico2/sugipon/include/particleEffect.h
 *
 * The declarations of what particleEffect.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef PARTICLEEFFECT_H
#define PARTICLEEFFECT_H

#include "typedef.h"
#include "Primitive.h"

struct IosMemPart;

/* One particle package, 0xA0 bytes, the record an effect file supplies per
   particle kind (SetParticleEffectPackage copies the file's over the default
   below).  The colour is a quadword, so the record is 16-byte aligned. */
typedef struct PEPackage {  /* field names derived */
    int version;            /* 0x00 */
    int mode;               /* 0x04 */
    unsigned int alphaMode; /* 0x08, the GS alpha blend dispParticleEffect sets */
    unsigned short spread;  /* 0x0C */
    char pad0E[2];
    float speed;     /* 0x10 */
    float speedRand; /* 0x14 */
    float drag;      /* 0x18 */
    float gravity;   /* 0x1C */
    short spinY;     /* 0x20 */
    char pad22[2];
    float spinYRand;     /* 0x24 */
    float spinYDecay;    /* 0x28 */
    float size;          /* 0x2C */
    float sizeRand;      /* 0x30 */
    float sizeStep;      /* 0x34 */
    float sizeStepRand;  /* 0x38 */
    float sizeStepDecay; /* 0x3C */
    int count;           /* 0x40 */
    int emit;            /* 0x44 */
    float emitRand;      /* 0x48 */
    float emitStep;      /* 0x4C, the particles emitted a frame */
    float alpha;         /* 0x50 */
    float alphaRand;     /* 0x54 */
    int life;            /* 0x58 */
    float lifeRand;      /* 0x5C */
    char pad60[16];
    sceVu0IVECTOR col; /* 0x70 */
    int u;             /* 0x80 */
    int v;             /* 0x84 */
    short spinX;       /* 0x88 */
    char pad8A[2];
    float spinXRand; /* 0x8C */
    float wind;      /* 0x90 */
    int floorOn;     /* 0x94, nonzero clamps the particles to the floor */
    int floorDepth;  /* 0x98, the floor height, negated */
    char pad9C[4];
} PEPackage; /* derived name */

/* one particle of an effect, 0x70 bytes: its position and velocity, spin,
   size, alpha and life, colour and texture coordinates */
typedef struct PEPartRec { /* field names derived */
    int alive;             /* 0x00, 0 once the particle has run out */
    int spin;              /* 0x04 */
    long long pad08;       /* 0x08 */
    float pos[4];          /* 0x10 */
    float vel[4];          /* 0x20 */
    short spinX;           /* 0x30 */
    short spinY;           /* 0x32 */
    float size;            /* 0x34 */
    float sizeStep;        /* 0x38 */
    float alpha;           /* 0x3C */
    float alphaStep;       /* 0x40 */
    int life;              /* 0x44 */
    char pad48[8];
    int col[4]; /* 0x50 */
    float u;    /* 0x60 */
    float v;    /* 0x64 */
    char pad68[8];
} PEPartRec; /* derived name */

/* The 128-byte per-effect geometry object SetParticleEffectByPartition
   allocates: the emitter's position and orientation, its package, the
   particle records and the primitive that draws them, the emission count, the
   floor clamp and the rate, then the goal the particles converge on
   (ParticleEffects_SetAllGoal) and the user area of a proc that moves the
   particles instead of the integrator (ito's gather effect): its speed, the
   proc, the end callback with its argument and the caller's word. */
typedef struct PEGeo {            /* field names derived */
    float pos[4];                 /* 0x00 */
    float quat[4];                /* 0x10 */
    struct PEPackage *pkg;        /* 0x20 */
    struct PEPartRec *parts;      /* 0x24 */
    PrimParticle *prim;           /* 0x28 */
    float emitted;                /* 0x2C */
    int n;                        /* 0x30 */
    int clip;                     /* 0x34 */
    int floorOn;                  /* 0x38 */
    float floor;                  /* 0x3C */
    float rate;                   /* 0x40 */
    char pad44[12];               /* 0x44 */
    float goal[4];                /* 0x50 */
    float speed;                  /* 0x60 */
    int (*proc)(struct PEGeo *);  /* 0x64, 0 for none; the effect ends when it returns 0 */
    void (*endFunc)(int);         /* 0x68 */
    int id;                       /* 0x6C, the effect's id, endFunc's argument */

    union {
        int capsule;        /* the boss capsule the effect gathers at */
        volatile int *done; /* the releasing thread's done flag */
    } user;                 /* 0x70 */

    char pad74[12];
} PEGeo; /* derived name */

/* particle-effect: one particle effect file, 0x50 bytes. Readers:
 * ico2/common/src/icoMisc.c, ico2/sugipon/src/effectTool.c (the path at
 * 0x20), particleEffect.c. */
typedef struct {      /* field names derived */
    char name[32];    /* 0x00 */
    char path[48];    /* 0x20 */
} ParticleEffectFile; /* derived name */

/* the 61 particle effect files (the particle-effect data member) */
extern const ParticleEffectFile particleEffectFile[];
void DeleteParticleEffect(int no);
void DisableParticleEffectGeometryControl(int id);
void DispParticleEffects(void);
void ExecParticleEffect(int no);
void ExecParticleEffects(void);
PEGeo *GetParticleEffectData(int id);
int *GetParticleEffectPackage(int idx);
int GetParticleIDWithName(char *name);
int GetParticleLoopFlag(int id);
void InitParticleEffects(void);
void ParticleEffects_SetAllGoal(void *goal);
void ResetParticleEffectPackages(int *pkg);
int SetParticleEffect(int id, void *pos, void *quat);
int SetParticleEffectActiveSensing(int id, void *pos, void *quat);
int SetParticleEffectByPartition(int id, void *pos, void *quat, struct IosMemPart *part);
void SetParticleEffectDrainLevel(int id, float level);
void SetParticleEffectGeometry(int id, void *pos, void *quat);
void SetParticleEffectPackage(int no, int *data, int size);
void SetParticleEffectPauseFlag(int id, int pause);
void SetParticleEffectUpperLimit(int no, float f);
void SetParticleEffectClipEnableFlag(int id, int on);
void DeleteParticleEffectsByPackage(int *pkg);
void DeleteParticleEffectsByID(int id);

#endif /* PARTICLEEFFECT_H */
