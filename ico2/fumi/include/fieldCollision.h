/*
 * ico2/fumi/include/fieldCollision.h
 *
 * The declarations of what fieldCollision.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef FIELDCOLLISION_H
#define FIELDCOLLISION_H

#include "typedef.h"
#include "eeword.h"

/* a collision filter callback (ClipWorkCB's filter), passed as a word */
typedef int (*ClipFilterFn)(void *);

struct GObj;

/* The empty wall-hit record (no object, node -1, no wall) a character's
   collision filter is reset to. */
extern WallCfg InitialColInfo;
/* The empty object and node pair (no object, node -1) a clip resets its wall
   and floor sources to and a display object's parent link is cleared to. */
extern ObjNode InitialObjPointer;
extern int collision_pick;

/* one wall of a collision set, 0x50 bytes (the table
 * stride), a record of the .cl file. The corners are what GetWallGlobalInfo
 * transforms, the height and normal are what clip_wall_1 reads, the angle is
 * GetWallGlobalInfo's 0x44 short and the attribute is the word the _clipW
 * filters test. The normal is not in the file: ReadCollisionFile
 * (charFileManager.c) allocates a sine and cosine pair per wall from the
 * angle and stores its address here, an EE address word (eeword.h); read it
 * with FC_WALL_NORMAL. */
typedef struct FcWallEnt { /* field names derived */
    float pt[4][4];        /* 0x00 corners */
    float height;          /* 0x40 */
    short angle;           /* 0x44 */
    char pad46[2];
    int attr;                      /* 0x48 */
    ICO_EEWORD(float *) normal;    /* 0x4C */
} FcWallEnt;                       /* derived name */

#define FC_WALL_NORMAL(w) ICO_EEPTR(float *, (w)->normal)

/* The head of a .cl collision file as ReadCollisionFile (charFileManager.c)
 * leaves it: the wall and floor counts, then five words the loader turns
 * from file offsets into EE address words (eeword.h): the wall table
 * (FcWallEnt, count of them), the floor table (FcFloorEnt), the 32x32 wall
 * and floor block grids, whose nonzero cells it relocates too (each the
 * address of a block's index list of shorts, ended by a negative one), and
 * the origin the blocks are counted from (three floats).  fieldCollision.c
 * reads it as FuzioCtx on the EE. */
typedef struct FcColl { /* field names derived */
    char pad0[8];
    int count;               /* 0x08, the wall count */
    int nfloor;              /* 0x0C, the floor count */
    ICO_EEWORD(int) wcl;     /* 0x10 */
    ICO_EEWORD(int) fcl;     /* 0x14 */
    ICO_EEWORD(int) wblk;    /* 0x18 */
    ICO_EEWORD(int) fblk;    /* 0x1C */
    ICO_EEWORD(int) ofs;     /* 0x20 */
} FcColl;                    /* derived name */

typedef struct { /* field names derived */
    float x, y, z, w;
} FcVec4; /* derived name */

/* one floor of a collision set, 0x70 bytes (the table stride): the polygon
 * clip_floor_1 tests, its plane, and the attribute the _clipF filters,
 * ClipFloorByGObj and MakeExitAttributeIndex read (its low nibble the exit
 * slot). */
typedef struct FcFloorEnt { /* field names derived */
    FcVec4 v[4];            /* 0x00, the polygon's corners */
    float nx, ny, nz, npad; /* 0x40, the plane normal */
    float d;                /* 0x50, the plane distance */
    int nex;                /* 0x54, the corners past the first three */
    char pad58[8];
    int attr; /* 0x60 */
    char pad64[12];
} FcFloorEnt; /* derived name */

/* fieldCollision.c's `inline` functions (all but the sixteen it compiles in
 * place), in the order of their definitions' out-of-line copies at the end of
 * the object (first-declaration order). */
void ClipWallDebug(ClipWork *work);
inline void ClipWall(ClipWork *work);
void ClipWallR(ClipWork *work);
void ClipWallWaveForce(ClipWork *work);
void ClipWallFuchiHangWalkStop(ClipWork *work);
void ClipWallField(ClipWork *work);
void ClipWallEField(ClipWork *work);
void ClipWallBoxStop(ClipWork *work);
void ClipWallAdjustPos(ClipWork *work);
void ClipWallE(ClipWork *work);
void ClipWallCheckCB(ClipWork *work, ICO_WORD_PTR(ClipFilterFn) filter);
void ClipWallFieldCheckCB(ClipWork *work, ICO_WORD_PTR(ClipFilterFn) filter);
void ClipFloor(ClipWork *work);
void ClipFloorE(ClipWork *work);
void ClipFloorR(ClipWork *work);
void ClipFloorIH(ClipWork *work);
void ClipFloorCheckCB(ClipWork *work, ICO_WORD_PTR(ClipFilterFn) filter);
void ClipCollision(ClipWork *self);
int ChangeFieldCollisionDebugMode(int drawRay);
void LoadCollision(void **self, char *fname);
void DrawCollision(int mode);
int ClipPlane(ClipWork *work);
void GetOrientOfWall(void *out, void *wallEnt, ObjNode *src);
void SetSimplePlane(float *self, float a, float b, float c, float d);
int GetWallAttribute(ClipWork *w);
int GetFloorAttribute(ClipWork *w);
int CompareAttribute(unsigned int a, unsigned int b);
void GetWallGlobalInfo(void *pts, void *nrm, char *w, void *m);
inline float GetDistanceFromPlane(void *plane, void *pos);
float GetYDistanceFromPlane(float *plane, float *pos);
float GetYProjectionOfPlane(float *plane, float *pos);
void ResetCollisionPC(void);
int PositionOfExit(float *pos, int attr);
void GetGlobalWallPlane(float *plane, WallCfg *wall);
/* compiled in place */
void ClipFloorByGObj(ClipWork *work, struct GObj *gobj);
void DrawCollisionRay(ClipWork *ray);
void DrawGObjFloorCollision(struct GObj *gobj, int col);
void DrawGObjWallCollision(struct GObj *gobj, int col);
void GetReflectionElement(ClipWork *work, float arg0, float arg1);
void MakeExitAttributeIndex(void);
void MakeCollisionDependGObjList(void);

#endif /* FIELDCOLLISION_H */
