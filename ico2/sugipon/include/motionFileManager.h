/*
 * ico2/sugipon/include/motionFileManager.h
 *
 * The declarations of what motionFileManager.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef MOTIONFILEMANAGER_H
#define MOTIONFILEMANAGER_H

/* The declarations below lead this header because their order is load-bearing:
 * gcc 2.9 emits the deferred out-of-line copy of a plain-inline function in
 * first-declaration order, so this is the order motionFileManager.c's inline tail has. */
void ResetDynamicMotionManager(void);
void ResetStatic2MotionManager(int seg);
int CheckMotionIncludeFacialData(unsigned int *self);
int AddMotionMemorySize(int size, int seg);
int GetMotionMemorySize(int seg);
void InitMotionFile(void *buf, char *name);
extern int *motionTable[];
void InitMotionMemorySize(void);

#include "eeword.h"

/* The .mob motion file (charFileManager.c's ReadMotionFile loads it,
 * InitMotionFile relocates it in place and motionManager2.c's _getMotion and
 * kin read it).  Every relocated word stays 4 bytes: an address on the EE,
 * an EE address word on the host (eeword.h). */

/* a node of formats 3 and 6: the per-frame tables of its first and last
   element, which _getMotion indexes by frame (relocated words) */
typedef struct NodeRec { /* field names derived */
    int nTable;          /* 0x00 */
    int lastTable;       /* 0x04 */
} NodeRec;               /* derived name */

/* The optional facial block: a count and the table of per-entry offsets,
   relocated in place like the header. */
typedef struct FacialRec {   /* field names derived */
    int count;               /* 0x0 */
    ICO_EEWORD(void **) tbl; /* 0x4 */
} FacialRec;                 /* derived name */

/* The motion file header as InitMotionFile leaves it, every offset turned
   into an address: pursueNodeList walks nodeList against typeList, and the
   facial block exists when typeList does not start right after the 16-byte
   header. */
typedef struct MotFileHdr {                /* field names derived */
    int frames;                            /* 0x00, the frame count */
    ICO_EEWORD(char *) rootPos;            /* 0x04, three floats of root position a frame */
    ICO_EEWORD(unsigned char *) typeList;  /* 0x08 */
    ICO_EEWORD(void **) nodeList;          /* 0x0C */
    ICO_EEWORD(FacialRec *) facial;        /* 0x10 */
} MotFileHdr;                              /* derived name */

#endif /* MOTIONFILEMANAGER_H */
