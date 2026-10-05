/*
 * ico2/seki/include/BgAnimation.h
 *
 * The declarations of what BgAnimation.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef BGANIMATION_H
#define BGANIMATION_H

#include "typedef.h" /* GObj */
#include "eeword.h"
#include <libvu0.h>

struct BgaAnimObj;
struct BgaDObjEnt;
struct BgaLightningDef;
struct BgaLightEnv;

/* The BGA file's records.  The file is loaded once and kept (stageTable's
 * data), and bga_InitData relocates it in place: every word below typed
 * ICO_EEWORD(...) holds a file offset on disc and an address after the load,
 * or an address the game stores into the image later (the object a node
 * drives, the tree links).  On the host such a word holds the address's
 * offset in the EE RAM arena (eeword.h): the records keep their disc layout
 * on every host, and the code reads the words through ICO_EEPTR and the
 * macros below.  docs/port/LOADERS.md. */

/* one envelope of a node, 8 bytes, the list ending at a null data word: what
   the envelope drives and its motion record (or, for types 7 to 9, its data) */
typedef struct BgaEnvEnt { /* field names derived */
    /* 0x00 */ unsigned short type;
    /* 0x02 */ char pad02[2];
    /* 0x04 */ ICO_EEWORD(unsigned char *) data;
} BgaEnvEnt; /* derived name */

/* one node of the file's object list, 0x48 bytes: its type and node number,
   the model or particle name, the object it drives, its envelope list, the
   tree links bga_InitData builds, its own motion record (at 0x34, read as a
   BgaPtMotion, a BgaExtMotion or bga_resetObjectCounter's count) and the
   parent's node number */
typedef struct BgaDObjEnt { /* field names derived */

    /* 0x00 */ unsigned short type;
    /* 0x02 */ unsigned short num;
    /* 0x04 */ char name[32];
    /* 0x24 */ union {
        ICO_EEWORD(void *) obj; /* particle record, geometry, Kyomi object */
        ICO_EEWORD(struct BgaLightEnv *) light; /* ambient-light record */
        int next; /* the file's flat list, consumed by bga_InitData */
    } u;
    /* 0x28 */ ICO_EEWORD(int) env;
    /* 0x2C */ ICO_EEWORD(struct BgaDObjEnt *) child;
    /* 0x30 */ ICO_EEWORD(struct BgaDObjEnt *) sibling;
    /* 0x34 */ ICO_EEWORD(int) motion;
    /* 0x38 */ char pad38[12];
    /* 0x44 */ short parent;
    /* 0x46 */ char pad46[2];
} BgaDObjEnt; /* derived name */

/* a gizmo envelope's key (envelope type 6), 0x28 bytes: six morph weights,
   which bga_InitData normalises in place, and the spline parameters */
typedef struct BgaKey { /* field names derived */
    /* 0x00 */ float v[6];
    /* 0x18 */ float tension;
    /* 0x1C */ float bias;
    /* 0x20 */ int linear;
    /* 0x24 */ int time;
} BgaKey; /* derived name */

/* a gizmo envelope's motion record */
typedef struct BgaMotion { /* field names derived */
    /* 0x00 */ ICO_EEWORD(BgaKey *) key;
    /* 0x04 */ int n;
    /* 0x08 */ unsigned int len;
    /* 0x0C */ float frame;
} BgaMotion; /* derived name */

/* The particle motion's key: a position, a rotation in degrees, the three
   colour weights, the two tangent weights, the linear flag and the frame the
   key sits on.  bga_GetMotion, bga_GetMotionParticle
   and bga_GetMotionLightning all read this 0x34-byte record. */
typedef struct BgaPtKey { /* field names derived */
    /* 0x00 */ float pos[3];
    /* 0x0C */ float rot[3];
    /* 0x18 */ float col[3];
    /* 0x24 */ float tension;
    /* 0x28 */ float bias;
    /* 0x2C */ int linear;
    /* 0x30 */ int time;
} BgaPtKey; /* derived name */

/* a node's motion record (BgaDObjEnt + 0x34) */
typedef struct BgaPtMotion { /* field names derived */
    /* 0x00 */ ICO_EEWORD(BgaPtKey *) key;
    /* 0x04 */ int n;
    /* 0x08 */ unsigned int len;
    /* 0x0C */ float frame;
} BgaPtMotion; /* derived name */

/* a one-value envelope's key, 0x14 bytes */
typedef struct BgaExtKey { /* field names derived */
    /* 0x00 */ float value;
    /* 0x04 */ float tension;
    /* 0x08 */ float bias;
    /* 0x0C */ int linear;
    /* 0x10 */ int time;
} BgaExtKey; /* derived name */

/* a one-value envelope's motion record (and the step view of a node's own) */
typedef struct BgaExtMotion { /* field names derived */
    /* 0x00 */ ICO_EEWORD(BgaExtKey *) key;
    /* 0x04 */ int n;
    /* 0x08 */ unsigned int len;
    /* 0x0C */ float frame;
} BgaExtMotion; /* derived name */

/* one key of an SDF camera, 0x24 bytes */
typedef struct BgaSdfKey { /* field names derived */
    /* 0x00 */ char pad00[4];
    /* 0x04 */ float pos[3];
    /* 0x10 */ float at[3];
    /* 0x1C */ float roll;
    /* 0x20 */ float fov;
} BgaSdfKey; /* derived name */

/* The SDF camera record (the .cam member, kept as loaded) bga_InitSdfCamera
 * checks and bga_SetCamFrame starts: the "SDF" tag, the key count, the
 * running frame, the play mode and the keys. */
typedef struct BgaSdfCam { /* field names derived */
    /* 0x00 */ char id[4];
    /* 0x04 */ int num;
    /* 0x08 */ float frame;
    /* 0x0C */ int mode;
    /* 0x10 */ BgaSdfKey key[1];
} BgaSdfCam; /* derived name */

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
    ICO_EEWORD(int) dobjs;                      /* 0x0C */
    ICO_EEWORD(struct BgaDObjEnt **) roots;     /* 0x10, a word array, null-ended */
    float start;                                /* 0x14 */
    float end;                                  /* 0x18 */
    float step;                                 /* 0x1C */
    float frame;                                /* 0x20 */
    ICO_EEWORD(BgaAnim *) anim;                 /* 0x24 */
} BgaHeader;                                    /* derived name */

/* The header's words as pointers: the animation record, and root i of the
   root list (null after the last).  BGA_W(p) is the word stored for p. */
#define BGA_ANIM(h) ICO_EEPTR(BgaAnim *, (h)->anim)
#ifdef ICO_HOST
#define BGA_ROOT(h, i) ICO_EEPTR(struct BgaDObjEnt *, ICO_EEPTR(IcoEEWord *, (h)->roots)[i])
#define BGA_W(p) ((IcoEEWord)ico_eew(p))
#else
#define BGA_ROOT(h, i) ((h)->roots[i])
#define BGA_W(p) (p)
#endif

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
