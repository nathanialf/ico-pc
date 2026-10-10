/*
 * ico2/seki/include/Primitive.h
 *
 * The declarations of what Primitive.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef PRIMITIVE_H
#define PRIMITIVE_H

/* One vertex-buffer entry of a 3D mesh: a quadword per vertex. */
typedef struct { /* field names derived */
    float x, y, z, w;
} Prim3DVec __attribute__((aligned(16))); /* derived name */

/* The 144-byte mesh prim_InitMesh3D allocates and returns: the grid counts,
   the two wrap flags prim_makeNormal tests, the UV placement matrix, the
   strip counts the packet builder walks, the GIF register word, the colour
   and the three per-vertex buffers.  The buffer at 0x70 is the normal buffer (prim_makeNormal fills it and
   prim_UpdateMesh3D copies it under flag 4), the one at 0x74 the texture
   coordinates (flag 8; pool, flag and clothAnimation write s and t there). */
typedef struct { /* field names derived */
    /* 0x00 */ int nx;
    /* 0x04 */ int ny;
    /* 0x08 */ int wrapX; /* the last column joins the first (prim_makeNormal) */
    /* 0x0C */ int wrapY; /* the last row joins the first */
    /* 0x10 */ float mtx[4][4];
    /* 0x50 */ int stripLen; /* the vertices of one strip, two a column */
    /* 0x54 */ int strips;   /* the strips, one a row pair */
    /* 0x58 */ int lit;      /* the microcode lights the mesh, so its normals are sent */
    /* 0x5C */ int pad5C;
    /* 0x60 */ long long prim; /* the GIF register word */
    /* 0x68 */ unsigned int col;
    /* 0x6C */ Prim3DVec *pos;
    /* 0x70 */ Prim3DVec *nrm;
    /* 0x74 */ Prim3DVec *st;
    /* 0x78 */ int qwc; /* the size of one packet buffer in quadwords */
    /* 0x7C */ void *bufs[2];
    /* 0x84 */ int pad84[3];
} Mesh3D; /* derived name */

/* A 2D fan: n rim vertices round a centre, each a colour and a position. */
typedef struct { /* field names derived */
    /* 0x00 */ int cr;
    /* 0x04 */ int cg;
    /* 0x08 */ int cb;
    /* 0x0C */ int ca;
    /* 0x10 */ float x;
    /* 0x14 */ float y;
    /* 0x18 */ float z;
    /* 0x1C */ float w;
} Fan2DVtx __attribute__((aligned(16))); /* derived name */

typedef struct { /* field names derived */
    /* 0x00 */ int n;
    /* 0x04 */ int blend; /* set when a vertex colour is not opaque */
    /* 0x08 */ Fan2DVtx *buf;
} Fan2D; /* derived name */

/* A particle object: two double-buffered packet heads, the counts, the
   texture name and the two object buffers. */
typedef struct { /* field names derived */
    /* 0x00 */ int head[4];
    /* 0x10 */ float mtx[4][4];
    /* 0x50 */ float lmtx[4][4];
    /* 0x90 */ int tail[4];
} PrimParticleBuf; /* derived name */

/* one particle object buffer, the VU1 data of a particle object: the VIF
   codes and the unpack, the particle count, the two GIF tags, the screen clip
   window's two corners, the object position, then 32 bytes a particle and the
   closing VIF codes (FLUSHA, MSCNT) */
typedef struct { /* field names derived */
    /* 0x00 */ int vif[4];
    /* 0x10 */ int num[4];
    /* 0x20 */ long long tag[2][2];
    /* 0x40 */ float clipMin[4];
    /* 0x50 */ float clipMax[4];
    /* 0x60 */ float pos[4];
    /* 0x70 */ int vtx[0][8];
} PrimParticleObj; /* derived name */

typedef struct { /* field names derived */
    /* 0x000 */ PrimParticleBuf buf[2];
    /* 0x140 */ int headQwc; /* the size of one packet head in quadwords */
    /* 0x144 */ int word144;
    /* 0x148 */ int num;
    /* 0x14C */ float x;
    /* 0x150 */ float y;
    /* 0x154 */ float z;
    /* 0x158 */ int word158; /* the init call's fifth argument */
    /* 0x15C */ char name[32];
    /* 0x17C */ int tex;
    /* 0x180 */ int cur;
    /* 0x184 */ int objSize; /* the object buffers' size, bytes until init ends, then quadwords */
    /* 0x188 */ PrimParticleObj *objs[2];
    /* 0x190 */ void *vtx;     /* the vertices of the buffer being drawn (objs + 0x70) */
    /* 0x194 */ void *vtxNext; /* the vertices of the other buffer */
} PrimParticle;              /* derived name */

void prim_DeleteParticle(PrimParticle *p);
#ifdef ICO_RD
/* PC port: the life of a particle emitter's slot, for the presenter's key
   (Primitive.c); prim_InitParticleByPartition stamps a new one,
   prim_DeleteParticle forgets it (the count kept for the next) */
void prim_HostParticleStamp(const PrimParticle *p);
unsigned int prim_HostParticleGen(const PrimParticle *p);
void prim_HostParticleForget(const PrimParticle *p);
#endif
void prim_DispFan2D(Fan2D *f, int mode);
void prim_DispMesh3D(Mesh3D *m, void *la, void *lb, int tex);
void prim_DispParticle(PrimParticle *p, void *mtx);
void prim_DispWireBox(float *sz, void *col);
void prim_DispWireSphere(float r, void *col, int nu, int nv);
Fan2D *prim_InitFan2D(int n, float r, float *pos, unsigned int cc, unsigned int rc);
Mesh3D *prim_InitMesh3D(int nx, int ny, int rot, long long col, unsigned int col2, int lit);
PrimParticle *prim_InitParticle(int num, float x, float y, float z, int a1, char *name, int a3);

PrimParticle *prim_InitParticleByPartition(int num, float x, float y, float z, int a1, char *name,
                                           int a3, void *heap);

void prim_SetFan2D(Fan2D *f, float r, float *pos, unsigned int cc, unsigned int rc);
void prim_UpdateMesh3D(Mesh3D *m, int flags, int idx);
void prim_DispWireYCylinder(void *col, int n, int flag, float r, float y0, float y1);

#endif /* PRIMITIVE_H */
