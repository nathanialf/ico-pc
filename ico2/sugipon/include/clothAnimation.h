/*
 * ico2/sugipon/include/clothAnimation.h
 *
 * The declarations of what clothAnimation.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef CLOTHANIMATION_H
#define CLOTHANIMATION_H

struct GObj;

#include "typedef.h"
#include "Primitive.h"
#include <libvu0.h>

/* One extended weight of a chain node: the chain position it sits at (a
   node index plus a fraction, negative while the slot is free), its point,
   its old point and its velocity, and the two lengths SetChainExtendedWeight
   is given. */
typedef struct { /* field names derived */
    float w;
    char pad[12];
    VECTOR v0;
    VECTOR v1;
    VECTOR v2;
    float len0; /* 0x40, SetChainExtendedWeight's w0 */
    float len1; /* 0x44, its w1 */
    char pad48[8];
} ExW; /* derived name */

/* the points of one chain InitChains builds, 0x1A0 bytes */
typedef struct { /* field names derived */
    float (*pos)[4]; /* 0x0, the chain's points */
    float (*vel)[4]; /* 0x4, their velocities */
    float *len;      /* 0x8, one a point, the step down the chain */
    int exNum;   /* 0xC, the extended weights in use */
    ExW ex[5];
} ChainNode; /* derived name */

/* the parameters of one chain: the skeleton focus node it hangs from, the
   length of one segment, where it starts and the weight the extended weights
   are bound against */
typedef struct { /* field names derived */
    int node;    /* 0x00, -1 when the chain hangs from no node */
    float step;  /* 0x04 */
    int pad08[2];
    sceVu0FVECTOR root; /* 0x10 */
    int pad20[4];
    float weight; /* 0x30 */
    int pad34[3];
} ChainParam; /* derived name */

/* one record of the list InitChains walks, 0x50 bytes: the node count (-1
   ends the list) and the chain's parameters */
typedef struct { /* field names derived */
    int num;
    int pad04[3];
    ChainParam pm;
} ChainCfg; /* derived name */

/* the chain system InitChains returns: the parameter records it was built
   from, the chain count and the chains */
typedef struct { /* field names derived */
    ChainCfg *cfg;
    int num;
    ChainNode *nodes;
    int oddFrame; /* flipped every GetChainAnimation */
} ChainSet;       /* derived name */

/* The InitClothes config record, 0x1C bytes, built by clothTest.c and
   flag.c (rows, spacing, columns, anchors, texture, weight). */
typedef struct ClothCfg { /* field names derived */
    int num;              /* 0x00  rows, and -1 ends the array */
    float segLength;      /* 0x04  the spacing between rows */
    int div;              /* 0x08  columns */
    int wrap;             /* 0x0C  nonzero when the last column joins the first */
    void *anchors;        /* 0x10 */
    void *tex;            /* 0x14  null means the untextured mesh */
    float weight;         /* 0x18  the fall added to each point a step */
} ClothCfg;               /* derived name */

/* the texture record tex_GetTextureData returns, copied whole with
   doubleword moves */
typedef struct { /* field names derived */
    long long q[89];
} TexBlob; /* derived name */

/* one cloth InitClothes builds, 0x2E0 bytes: the mesh, the rows of points
   and velocities, a mark per point and the texture record copied in */
typedef struct {  /* field names derived */
    Mesh3D *mesh; /* 0x00 */
    VECTOR **pos; /* 0x04, one row a cloth row, into the mesh's positions */
    VECTOR **vel; /* 0x08, one row a cloth row */
    int **mark;   /* 0x0C, one row a cloth row, -1 each at init */
    int textured; /* 0x10, nonzero when the config names a texture */
    int pad14;
    TexBlob tex; /* 0x18 */
} ClothRec;      /* derived name */

/* the clothes InitClothes returns */
typedef struct { /* field names derived */
    int num;
    ClothRec *rec;
} ClothSet; /* derived name */

/* one cloth InitCloth4D builds (clothAnimation.c) */
typedef struct Cloth4D Cloth4D; /* derived name */

void DispCloth4D(Cloth4D *c, void *la, void *lb);
void DispCloth4DWithAdd(Cloth4D *c, void *la, void *lb);
void DispClothMesh(ClothRec *rec, void *la, void *lb);
void DispMeshWire(Prim3DVec **rows, int nx, int ny);
void GetChainAnimation(ChainSet *sys, struct GObj *obj, float (*mtx)[4]);
float GetChainCollision(ChainSet *sys, void *pos, float r);
float GetChainNodeID(ChainCfg *cfg, float f);
void GetCloth4D(Cloth4D *c, float x, float y);
void GetCloth4DWithDetail(Cloth4D *c, float x, float y, float z, float w);
void GetCloth4DWithTight(Cloth4D *c, float x, float y, float z, float w, void *qa, void *qb);

void GetClothAnimation(VECTOR **pos, VECTOR **vel, struct GObj *obj, void *m, ClothCfg *cfg,
                       int nwall, ICO_WORD_PTR(struct GObj *) wallOwner, int fixEnd);

void GetClothAnimationFix4Points(VECTOR **pa, VECTOR **pv, ClothCfg *cfg, void *mtx);
ChainSet *InitChains(ChainCfg *cfg);

/* One row of the table InitCloth4D's third argument points at: a collision
 * cylinder of the cloth, hung from a skeleton node.  The cylinder runs along
 * the node's Y from bottom to top; the init scales bottom, top and radius by
 * the actor's scale and copies the row into the cloth's own cylinder record,
 * which the cloth step reads: posX, posY, the word at 0x28 (0 in every table)
 * and posW are the cylinder's place in its node's frame, turn (1 or -1) the
 * way a point pushed round it turns.  The tables are boy.c's, girl.c's and
 * queen.c's. */
typedef struct {  /* field names derived */
    int enable;   /* 0x00, -1 ends the table */
    float bottom; /* 0x04 */
    float top;    /* 0x08 */
    float radius; /* 0x0C */
    int node;     /* 0x10, the argument GetSkeltonFocusNode is called with */
    char pad14[12];
    float posX; /* 0x20 */
    float posY; /* 0x24 */
    char pad28[4];
    float posW;     /* 0x2C */
    float turn;     /* 0x30 */
    char pad34[12]; /* 0x34, where the copy keeps 1 / (radius + radius) */
} ClothHangCfg;     /* derived name */

/* The generated cloth mesh InitCloth4D's second argument points at: one
 * column of the cloth, its attachment point and the pair of skeleton nodes the
 * column is blended between, read by index.  InitCloth4D reads the column's ny texture
 * coordinates through uv; the cloth step reads the rest.  The tables are
 * boy.c's, girl.c's and queen.c's. */
/* one skeleton node a column is blended from, and its weight */
typedef struct { /* field names derived */
    int node;
    float weight;
} Cloth4DLink; /* derived name */

typedef struct {  /* field names derived */
    float length; /* 0x00, the column's length, scaled by the actor scale */
    char pad04[12];
    float pos[4];        /* 0x10, the point the column hangs from */
    float dir[4];        /* 0x20, a unit vector */
    Cloth4DLink link[2]; /* 0x30, the second node -1 when the column hangs from the first alone */
    float (*uv)[2];      /* 0x40, ny texture coordinates */
    char pad44[12];
    float restDir
        [4]; /* 0x50, the direction the column's segments are pulled toward (GetCloth4DWithDetail's z) */
} __attribute__((aligned(16))) Cloth4DCol; /* derived name */

/* The head of one generated cloth mesh: the mesh size, a colour nothing
 * reads (InitCloth4D gives prim_InitMesh3D a constant one), the texture name,
 * the nx columns and the spacing the collision sweep keeps.  word0C and
 * word2C are read by nothing. */
typedef struct { /* field names derived */
    int nx;
    int ny;
    int wrap;   /* 0x08, nonzero when the last column joins the first */
    int word0C; /* 0x0C */
    int r;      /* 0x10 */
    int g;      /* 0x14 */
    int b;      /* 0x18 */
    int a;      /* 0x1C */
    const char *tex;
    Cloth4DCol *cols;
    float
        colSpacing; /* 0x28, the distance the collision sweep keeps between neighbouring columns */
    int word2C;     /* 0x2C */
} Cloth4DCfg;       /* derived name */

/* one collision cylinder of a cloth: InitCloth4D's copy of a ClothHangCfg row,
   scaled by the actor's scale */
typedef struct { /* field names derived */
    int enable;
    float bottom; /* 0x04, the cylinder runs along its node's Y from bottom to top */
    float top;
    float radius;
    char pad10[16];
    float pos[4];      /* 0x20, the cylinder's place in its node's frame */
    float turn;        /* 0x30, 1 or -1, the way a point pushed round the cylinder turns */
    float invDiameter; /* 0x34, 1 / (radius + radius), stored by InitCloth4D */
    char pad38[8];
} ClothPoint; /* derived name */

/* one cloth InitCloth4D builds: its owner, the mesh it draws and the point
   rows the cloth step walks */
struct Cloth4D { /* field names derived */
    GObj *gobj;
    Mesh3D *mesh;
    Prim3DVec **pos; /* 0x8, one row a column, into the mesh's positions */
    Prim3DVec **vel; /* 0xC, one row a column, the points' velocities */
    Prim3DVec **nrm; /* 0x10, one row a column, into the mesh's normals */
    int pad14;
    TexBlob tex;
    Cloth4DCfg *cfg;
    int colNum;            /* 0x2E4, the count of collision cylinders (ClothHangCfg rows) */
    int *colNode;          /* 0x2E8, the focus node each cylinder hangs from */
    sceVu0FMATRIX *colMtx; /* 0x2EC, one matrix a cylinder, the node matrices of the last step */
    ClothPoint *col;       /* 0x2F0, the cylinders */
    int sweepRight; /* 0x2F4, nonzero runs each row's collision sweep from the far edge first; InitCloth4D clears it and nothing sets it */
    int collision;  /* 0x2F8, nonzero while the cylinders collide (girl.c's debug_hair_collision) */
}; /* derived name */

Cloth4D *InitCloth4D(struct GObj *gobj, Cloth4DCfg *cfg, ClothHangCfg *tbl);
ClothSet *InitClothes(ClothCfg *cfg);
ClothSet *InitClothesNoShade(ClothCfg *cfg);
int SetChainExtendedWeight(ChainNode *node, int idx, float w0, float w1);
void TestDispChainAnimation(ChainSet *sys);
float getXZLength(void *v);
float getXZInvLength(void *v);
float getXZLengthSquare(void *v);
float subAndGetInvLength(void *d, const void *a, const void *b);

#endif /* CLOTHANIMATION_H */
