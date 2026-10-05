/*
 * ico2/seki/include/DisplayP2O.h
 *
 * The declarations of what DisplayP2O.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef DISPLAYP2O_H
#define DISPLAYP2O_H

/* DisplayP2O.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
void p2o_SetDefaultEnviroment(void);

#include "typedef.h"
#include "eeword.h"

/* one morph target entry, 32 bytes, the list ending at an index of -1: the
 * offset to add (weighted), whether it moves a vertex (1.0) or a normal (0.0),
 * and the vertex or normal it moves */
typedef struct PObjMorph { /* field names derived */
    float delta[3];        /* 0x00 */
    float isVertex;        /* 0x0C */
    int index;             /* 0x10 */
    char pad14[12];
} PObjMorph; /* derived name */

/* one material definition of a part, 16 bytes: the texture wrap mode
 * (pac_makeMaterialTable clamps it to 3), whether the frame-buffer alpha
 * correction is off, and the alpha, half or more of which blends */
typedef struct PObjMatDef { /* field names derived */
    char pad00[5];
    unsigned char wrap;   /* 0x05 */
    unsigned char fbaOff; /* 0x06 */
    char pad07[5];
    float alpha; /* 0x0C */
} PObjMatDef;    /* derived name */

/* one texture a part names, 0x90 bytes: the texture's name and the scale its
 * texture coordinates are multiplied by */
typedef struct PObjTexDef { /* field names derived */
    char name[132];         /* 0x00 */
    float scaleU;           /* 0x84 */
    float scaleV;           /* 0x88 */
    char pad8C[4];
} PObjTexDef; /* derived name */

/* The display's view of the model record ico2/common/src/PObj.c builds (its
 * PObj) and of the 0x180-byte part record the model's 0x40 points at (its
 * PObjSub).  Sub15C + 0x854 holds the object's model and + 0x858 the shadow
 * model.  A part keeps its vertices and normals, its polygon and strip lists,
 * its morph targets (reg_setShape adds them in by the object's weights), its
 * own matrix and two vertex buffers: RegistPacket.c keeps the rest pose in
 * them, Shadow.c the two caps of a shadow volume. */
typedef struct PObjPart { /* field names derived */
    char pad00[144];
    char *vtx;             /* 0x90 */
    unsigned int vtxCount; /* 0x94 */
    char pad98[8];
    char *nrm;             /* 0xA0 */
    unsigned int nrmCount; /* 0xA4 */
    char padA8[8];
    char *uv; /* 0xB0 */
    char padB4[12];
    char *col; /* 0xC0 */
    char padC4[12];
    PObjMatDef *mats;      /* 0xD0 */
    unsigned int matCount; /* 0xD4 */
    char padD8[8];
    PObjTexDef *texDefs; /* 0xE0 */
    int texCount;        /* 0xE4 */
    char padE8[8];
    void *polys;            /* 0xF0 */
    unsigned int polyCount; /* 0xF4 */
    char padF8[8];
    void *strips;            /* 0x100 */
    unsigned int stripCount; /* 0x104 */
    char pad108[8];
    struct PObjLine *lines; /* 0x110 */
    unsigned int lineCount; /* 0x114 */
    char pad118[8];
    PObjMorph **morphs;      /* 0x120 */
    unsigned int morphCount; /* 0x124 */
    char pad128[8];
    float mtx[4][4]; /* 0x130 */
    char pad170[4];
    char *vtxSave; /* 0x174 */
    char *nrmSave; /* 0x178 */
    char pad17C[4];
} PObjPart; /* derived name */

/* The .p2o model file (also .p2c, .p2g, .plo, .p2v and .p2s) as it is read
 * into memory: a header, the object table (one word a part, the offset of the
 * part's record), the texture table and the 0x180-byte part records.
 * ico2/common/src/PObj.c's AllocPObj relocates the offsets in place, adding
 * the image's address; the host keeps each relocated word an EE word
 * (eeword.h).  The file is freed once the model is built. */
typedef struct ObjHdr {   /* field names derived */
    char pad0[4];         /* 0x00 */
    int objTbl;           /* 0x04, the object table, a file offset, then an address */
    unsigned int objNum;  /* 0x08, the part count */
    unsigned int clstNum; /* 0x0C, the cluster count */
    int texTbl;           /* 0x10, the texture table, likewise; 0 for none */
    unsigned int texNum;  /* 0x14, the texture table's count */
} ObjHdr;                 /* derived name */

/* one entry of a part's polygon table (ObjRec.polys), 0x10 bytes: the
 * address of a vertex-weight list, relocated like the others, and the
 * cluster number at 0x04 (Packet.c's pac_getWeight reads both) */
typedef struct ObjEnt {   /* field names derived */
    ICO_EEWORD(void *) p; /* 0x00 */
    char pad4[12];
} ObjEnt; /* derived name */

/* The part record in the file, 0x180 bytes: PObjPart's layout on the EE,
 * with each address a word.  AllocPObj copies it into the model's part table
 * (the EE copies it whole; the host decodes it field by field into the
 * PObjPart, ico2/common/src/PObj.c).  The strip table holds stripCount words
 * and the morph table morphCount words (0 for none), each an address
 * after relocation. */
typedef struct ObjRec { /* field names derived */
    char pad0[128];
    int magic; /* 0x80, "OBJH" */
    char pad84[12];
    ICO_EEWORD(char *) vtx; /* 0x90 */
    unsigned int vtxCount;  /* 0x94 */
    char pad98[8];
    ICO_EEWORD(char *) nrm; /* 0xA0 */
    unsigned int nrmCount;  /* 0xA4 */
    char padA8[8];
    ICO_EEWORD(char *) uv; /* 0xB0 */
    char padB4[12];
    ICO_EEWORD(char *) col; /* 0xC0 */
    char padC4[12];
    ICO_EEWORD(char *) mats; /* 0xD0 */
    unsigned int matCount;   /* 0xD4 */
    char padD8[8];
    ICO_EEWORD(char *) texDefs; /* 0xE0 */
    int texCount;               /* 0xE4 */
    char padE8[8];
    ICO_EEWORD(char *) polys; /* 0xF0 */
    unsigned int polyCount;   /* 0xF4 */
    char padF8[8];
    ICO_EEWORD(char *) strips; /* 0x100 */
    unsigned int stripCount;   /* 0x104 */
    char pad108[8];
    ICO_EEWORD(char *) lines; /* 0x110 */
    unsigned int lineCount;   /* 0x114 */
    char pad118[8];
    ICO_EEWORD(char *) morphs; /* 0x120 */
    unsigned int morphCount;   /* 0x124 */
    char pad128[8];
    float mtx[4][4]; /* 0x130 */
    char pad170[4];
    ICO_EEWORD(char *) vtxSave; /* 0x174 */
    ICO_EEWORD(char *) nrmSave; /* 0x178 */
    char pad17C[4];
} ObjRec; /* derived name */

/* one material, 0x70 bytes: the six-quadword GS packet reg_transMaterialPacket
 * sends, then the attribute word: bit 0 the microprogram mode, bits 1 and 2
 * the blend (nonzero draws the material at priority 1 or 2), bits 3 and 4
 * the texture wrap, bits 5 and 6 the microprogram variant, bit 7 whether the
 * part has texture coordinates, bit 8 vertex colours, bit 9 the frame-buffer
 * alpha correction; read as the low word, the doubleword and the bits */
typedef struct PObjMaterial { /* field names derived */
    char packet[96];          /* 0x00 */

    union {
        long long bits;
        int word;

        struct {
            unsigned long long mode : 1;
            unsigned long long blend : 2;
            unsigned long long wrap : 2;
            unsigned long long variant : 2;
            unsigned long long hasUv : 1;
            unsigned long long hasCol : 1;
            unsigned long long fba : 1;
        } b;
    } attr; /* 0x60 */

    char pad68[8];
} PObjMaterial; /* derived name */

/* one texture slot of a part, 80 bytes, pac_getTextureInfo fills it: the
 * texture's name and the names with "_l" and "_ref" appended, the three
 * texture numbers (-1 where there is none), and a bit per number found:
 * bit 0 the texture (or no texture asked for), bit 1 "_l", bit 2 "_ref" */
typedef struct PObjTexInfo { /* field names derived */
    char name[24];           /* 0x00 */
    char nameL[24];          /* 0x18 */
    char nameRef[24];        /* 0x30 */
    short tex;               /* 0x48 */
    short texL;              /* 0x4A */
    short texRef;            /* 0x4C */
    unsigned short found;    /* 0x4E */
} PObjTexInfo;               /* derived name */

/* one part's group record, 48 bytes a part: the part's material table (0x70
 * bytes a material), its texture-info table, its first packet (a PacHeader;
 * a line part's record is Packet.c's MatLine, whose 0x08 is the line set),
 * the copies of the packets the morphs rewrite and the two table counts */
typedef struct PObjGroup {   /* field names derived */
    PObjMaterial *materials; /* 0x00 */
    PObjTexInfo *texs;       /* 0x04 */
    void *packets;           /* 0x08 */
    void *morph;             /* 0x0C */
    short matCount;          /* 0x10 */
    short texCount;          /* 0x12 */
    char name[28];           /* 0x14, the model's name, in the first part's record */
} PObjGroup;                 /* derived name */

typedef struct PObjModel { /* field names derived */
    char name[32];         /* 0x00 */
    int serial;            /* 0x20, the load serial charFileManager stamps on the model */
    int pad24;
    Sub15C *dobj;          /* 0x28 */
    short spare;           /* 0x2C, PObj's spare (cleared at set-up, never read) */
    signed char partCount; /* 0x2E */
    signed char disp;      /* 0x2F */

    union {
        unsigned long long bits; /* bits 16 and 17 the display type, bit 26 the shadow off */

        struct {
            short id;
            unsigned short type : 2;  /* 0x32, the display type */
            unsigned short shade : 4; /* the shade the strips carry */
            unsigned short lod : 4;   /* the level of detail the packet is built for */
            float lightScale;         /* 0x34, scales the colours of the lights */
        } s;
    } mode; /* 0x30 */

    float ambientScale; /* 0x38 */
    float shadowLength; /* 0x3C */
    PObjPart *parts;    /* 0x40 */
    char *boxes;        /* 0x44, eight vectors a part */
    PObjGroup *groups;  /* 0x48 */
    char pad4C[4];
#ifdef ICO_HOST
    /* a quadword boundary on every host, as on the EE (0x50) */
    float box[8][4] __attribute__((aligned(16))); /* 0x50 */
#else
    float box[8][4]; /* 0x50 */
#endif
} PObjModel; /* derived name */

void p2o_DispVU1(GObj *self);
void p2o_DispVU1DObj(void *req);
void p2o_DispVU1DObjMulti(void *req);
void p2o_DispVU1Default(GObj *self);
void p2o_DispVU1Multi(GObj *self);
void p2o_MakePacket(Sub15C *dobj);
void p2o_TransMicroProgram(void);
void p2o_DispShadowVolume(GObj *self);
void p2o_HideDispVU1(int count);
void p2o_DispVU1MultiDefault(GObj *self);

#endif /* DISPLAYP2O_H */
