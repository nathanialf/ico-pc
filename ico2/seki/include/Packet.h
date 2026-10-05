/*
 * ico2/seki/include/Packet.h
 *
 * The declarations of what Packet.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef PACKET_H
#define PACKET_H

#include "typedef.h"
#include <libvu0.h>

/* one packet pac_makePacket builds, 160 bytes: the bounding box's eight
   corners, the material, the texture slot and the slot's three texture
   numbers, the tag and polygon counts, the packet size (the byte count in the
   low 24 bits, the clip type in the top byte), the next packet of the model
   and its DMA data. */
typedef struct PacHeader {  /* field names derived */
    sceVu0FVECTOR box[8];   /* 0x00 */
    short mat;              /* 0x80 */
    short texSlot;          /* 0x82 */
    short tex;              /* 0x84 */
    short tex1;             /* 0x86 */
    short tex2;             /* 0x88 */
    short ntag;             /* 0x8A */
    int npoly;              /* 0x8C */
    unsigned int size : 24; /* 0x90 */
    unsigned char clip;     /* 0x93 */
    struct PacHeader *next; /* 0x94 */
    char *data;             /* 0x98 */
    char pad9C[4];
} PacHeader; /* derived name */

/* one colour of a line record, as the GS RGBAQ register takes it */
typedef struct PacColor { /* field names derived */
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} PacColor; /* derived name */

/* one 192-byte line record pac_makePacket builds and RegistPacket.c draws: a
   line's two vertices and texture coordinates.  A point keeps its last screen
   position in pos[1] and the one before at 0x20, and per trail index its
   screen positions at 0x50 and the ones before at 0x80.  Then the two
   colours, and the word at 0xB8: the texture number, the blend and the type
   (1 a point, 2 a line, 0 the end of the list), read whole by the drawing. */
typedef struct PacLine {        /* field names derived */
    sceVu0FVECTOR pos[2];       /* 0x00 */
    sceVu0IVECTOR scrPrev;      /* 0x20 */
    sceVu0FVECTOR uv[2];        /* 0x30 */
    sceVu0IVECTOR trail[3];     /* 0x50 */
    sceVu0IVECTOR trailPrev[3]; /* 0x80 */
    PacColor col0;              /* 0xB0 */
    PacColor col1;              /* 0xB4 */

    union {
        long long word;

        struct {
            unsigned short tex : 11;
            unsigned short blend : 2;
            short type : 3;
        } b;
    } attr; /* 0xB8 */
} PacLine;  /* derived name */

/* the 144-byte record a line part's group record points at: the line records
   at +0xC, which RegistPacket.c's line display walks */
typedef struct PacLineSet { /* field names derived */
    char pad00[12];
    PacLine *lines; /* 0x0C */
    char pad10[128];
} PacLineSet; /* derived name */

struct PObjMaterial;

struct PObjPart;

struct PObjModel;

struct PObjTexInfo;

/* Packet.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
void pac_Dump(int *data, int size);
void pac_Init(void);
void pac_DispVu1Memory(int idx, int n, int size);
void pac_MakePacket(Sub15C *o);

#ifdef ICO_RD
/* PC port (renderer wave 3, R3ab): the id of the rd mesh (RdMesh.id,
   rd_mesh.h) of a packet pac_makePacket built (its vertex batches), made at
   load and again whenever rd evicted it; 0 for a packet without batches.
   The id is kept in the header's pad word.  pac_HostRefresh re-reads the
   packet's vertex quadwords after reg_setShape rewrote them. */
unsigned int pac_HostMesh(PacHeader *pk);
void pac_HostRefresh(PacHeader *pk);
#endif

#endif /* PACKET_H */
