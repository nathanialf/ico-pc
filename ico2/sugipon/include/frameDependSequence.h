/*
 * ico2/sugipon/include/frameDependSequence.h
 *
 * The declarations of what frameDependSequence.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef FRAMEDEPENDSEQUENCE_H
#define FRAMEDEPENDSEQUENCE_H

struct GObj;

/* prog-se-link: one SE package, 0x0C bytes; the list ends at id -1. */
typedef struct { /* field names derived */
    int se[2];   /* 0x00 */
    int id;      /* 0x08 */
} SePackage;     /* derived name */

/* motion-se-random-def: one random sound, 8 bytes: the sound and its share
   of the draw. */
typedef struct { /* field names derived */
    int se;      /* 0x00 */
    float rate;  /* 0x04 */
} SERandEntry;   /* derived name */

/* motion-se-condition-def: one sound condition, 0x0C bytes. */
typedef struct { /* field names derived */
    int kind;    /* 0x00 */
    int cond;    /* 0x04 */
    int se;      /* 0x08 */
} SECondEntry;   /* derived name */

/* motion-eff-def: one motion effect, 0x24 bytes: the offset and turn from
   the node, the particle effect, the node and the flags.  Readers:
   frameDependSequence.c, ico2/common/src/icoMisc.c (EffEnt). */
typedef struct { /* field names derived */
    float x;     /* 0x00 */
    float y;     /* 0x04 */
    float z;     /* 0x08 */
    float rx;    /* 0x0C */
    float ry;    /* 0x10 */
    float rz;    /* 0x14 */
    int eff;     /* 0x18 */
    int node;           /* 0x1C */
    unsigned int flags; /* 0x20 */
} EffEntry; /* derived name */

/* motion-eff-condition-def: the shared condition table, 0x0C bytes a row:
   execEff reads its cond and actId pair as an effect id, execVibCondition
   reads actId as a pad actuator id. */
typedef struct { /* field names derived */
    int kind;    /* 0x00 */
    int cond;    /* 0x04 */
    int actId;   /* 0x08 */
} VibCondEntry;  /* derived name */

extern SePackage progSELink[];
extern const SERandEntry randomSEKind[];
extern const SECondEntry motSECondKind[];
extern const EffEntry motionEffKind[];
extern const VibCondEntry motEffCondKind[];
extern const int randomEffKind[];

int ExecuteDirectSE(struct GObj *gobj, int id);

void ExecuteSEPackage(struct GObj *gobj, int id);
void ExecuteSEPackageWithGroupVariation(struct GObj *gobj, int id, int grp);
void ExecuteSEPackageWithVolumeRate(struct GObj *gobj, int id, float rate);

void InitFrameDependSequence(void *flags);

void StopFDSVibration(void *flags);

void StopSEPackage(struct GObj *gobj);
void StopSEPackageWithGroupVariation(struct GObj *gobj, int grp);
void ExecFrameDependSequence(struct GObj *gobj);

#endif /* FRAMEDEPENDSEQUENCE_H */
