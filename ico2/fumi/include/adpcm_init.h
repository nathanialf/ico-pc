/*
 * ico2/fumi/include/adpcm_init.h
 *
 * The declarations of what adpcm_init.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ADPCM_INIT_H
#define ADPCM_INIT_H

/* adpcm_init.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
void adpcmPauseRequest(int val);

/* the six-word open request
   soundDataOpen fills in and soundDataOpenSync reads back. */
typedef struct AdpcmOpenReq { /* field names derived */
    int mode;                 /* 0x00, soundDataOpen's mode, 2 for ADPCM */
    int id;                   /* 0x04, the sound id */
    int ch;                   /* 0x08 */
    int iopBuf;               /* 0x0C */
    int loopNum;              /* 0x10 */
    struct CdvdBgReq *bg;     /* 0x14, the background loader handle */
} AdpcmOpenReq; /* derived name */

struct AdpcmStreamTag;

struct CdvdBgReq;

struct SqEntry;

typedef struct { /* field names derived */
    int ch;      /* 0x00 */
    int attr;    /* 0x04 */
    int iopAddr; /* 0x08 */
    int iopSize; /* 0x0C */
    int spuAddr; /* 0x10 */
    int vol;     /* 0x14 */
} AdpcmChReq; /* derived name */

typedef struct AdpcmStreamTag { /* field names derived */
    int used;                   /* 0x00 */
    int n;                      /* 0x04 */
    int ch[2];                  /* 0x08 */
    int seekSize;               /* 0x10 */
    int pitch;                  /* 0x14 */
    int iopBuf;                 /* 0x18 */
    int ringSize;               /* 0x1C */
    int loopStart;              /* 0x20 */
    int dataSize;               /* 0x24 */
    struct CdvdBgReq *bg;       /* 0x28 */
    char pad2C[4];
    long long mask;  /* 0x30 */
    int chAttr;      /* 0x38 */
    short volL[2];   /* 0x3C */
    short volR[2];   /* 0x40 */
    short fadeStep;  /* 0x44 */
    short loopNum;   /* 0x46 */
    short loopCount; /* 0x48 */
    char pad4A[2];
    int lastAddr; /* 0x4C */
    int remain;   /* 0x50 */
    char pad54[4];
} AdpcmStream; /* derived name */

void AdpcmStreamHeap(void);
void AdpcmStreamInit(void);
int AdpcmIopBuffAlloc(void);
int AdpcmNotUseIopAreaFree(void);
struct SqEntry *AdpcmOpenSync(AdpcmOpenReq *self);
void AdpcmFadeCloseAll(short step);
int AdpcmUseAreaGet(void);
int AdpcmFreeAreaGet(void);
void AdpcmInterStereoVolumeSetAll(void);
short AdpcmInterLeaveVolumeGet(struct SqEntry *self, int idx);
inline short AdpcmVolumeGet(struct SqEntry *self);
inline int adpcmTickProc(struct CdvdBgReq *self, struct SqEntry *obj);
void AdpcmInterStereoVolumeSet(void *stream, int ch);
void AdpcmOpen(AdpcmOpenReq *self, int no, int ch, int loopNum);
void AdpcmClose(struct SqEntry *obj);
void AdpcmPlay(AdpcmStream *self);
void AdpcmVolumeSet(struct SqEntry *self, int vol);
struct SqEntry *adpcmDataSet(int src, int no, int bank, int ch, int size, int iopBuf, int loopNum);
void adpcmTickProc2(struct SqEntry *obj);
/* adpcm_init.o's .sdata global */
extern int debugAdpcmOn;
void AdpcmStreamFree(void);

/* adpcmfile: one ADPCM stream, 0x40 bytes. Reader: ico2/fumi/sound/
 * adpcm_init.c (AdpcmDataRec). Owner: ico2/fumi/include/adpcm_init.h. */
typedef struct {   /* field names derived */
    char path[48]; /* 0x00 */
    int loopStart; /* 0x30, the loop start in sectors */
    int sectors;   /* 0x34, shifted left 11 for the size */
    int pitch;     /* 0x38 */
    int channels;  /* 0x3C */
} AdpcmDataRec;    /* derived name */

extern const AdpcmDataRec adpcmFile[];

#endif /* ADPCM_INIT_H */
