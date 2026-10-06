/*
 * ico2/common/include/charFileManager.h
 *
 * The declarations of what charFileManager.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef CHARFILEMANAGER_H
#define CHARFILEMANAGER_H
/* charFileManager.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */

/* the loaders' name argument is the member's name, a char * (cdvd.c's
   PackKind.func), held as a pointer-wide word on the host (ICO_WORD) */
void ReadSoundSqFile(void *h, __INTPTR_TYPE__ name, int size, int id, int kind, int word08,
                     int seg);

void ReadSoundAdpcmFile(void *h, __INTPTR_TYPE__ name, int size, int id, int kind, int word08,
                        int seg);

void InitCharFileManager(void);
void ResetCharFileManager(void);

struct Sub15C;

void CSVSYSTEM_ReadCharFiles(struct Sub15C *rec, int id);
/* the loaded model of a character file id (enemy.c's setup) */
struct PObjModel *GetPObjAddress(int id);

/* texture-path: one texture file, 0x34 bytes. Readers:
 * ico2/common/src/charFileManager.c (TexRec), kanban.c, layout_texture.c.
 * Owner: ico2/common/include/charFileManager.h. */
typedef struct {    /* field names derived */
    char path[48];  /* 0x00 */
    int cameraMove; /* 0x30, compared with NonLinearCameraMove */
} TexRec;           /* derived name */

/* model-path: one model, 0x8C bytes, indexed by the model id. Reader:
 * ico2/common/src/PObj.c (InitPObj, AllocPObj, MakePacket), charFileManager.c
 * (ReadSkeltonFile, ReadCollisionFile: the two paths). */
typedef struct PObjMdl { /* field names derived */
    char path[48];       /* 0x00, the skeleton file, "NULL" for none */
    char collPath[64];   /* 0x30, the collision file */
    float offset[3];     /* 0x70, InitPObj adds it to every vertex and box corner */
    float lightScale;    /* 0x7C, copied into the PObj at 0x34 */
    float ambientScale;  /* 0x80, copied into the PObj at 0x38 */
    float shadowLength;  /* 0x84, copied into the PObj at 0x3C */
    /* 0x88, read as bits */
    unsigned int pktKind : 4; /* the packet header's kind, 4 for none */
    unsigned int shade : 4;   /* MakePacket's tag bits 18-21, the PObj's shade */
    unsigned int lod : 4;     /* MakePacket's tag bits 22-25, the PObj's level of detail */
    unsigned int : 20;
} PObjMdl; /* derived name */

/* the data-only members texture-path and model-path */
extern TexRec texFile[];
extern PObjMdl modelData[];

#endif /* CHARFILEMANAGER_H */
