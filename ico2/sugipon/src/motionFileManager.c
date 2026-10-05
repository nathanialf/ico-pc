#include "debug.h"
#include "motionFileManager.h"
#include "motionOrientManager.h"
#include <eekernel.h>

/* the two running totals AddMotionMemorySize keeps, one per motion class
   (its second argument picks the class; the reset of the area-4, dynamic,
   motions clears the second) */
static int motionMemorySize; /* derived name */

static int motionMemorySizeStatic2; /* derived name */

/* declared here: motionOrientManager.h reaches ico2/fumi's files through
   typedef.h, and commonact.c declares the table char [] */
extern const MotionDef motionKind[];

/* every motion's loaded data, indexed by motion number; charFileManager
   fills the entries, the resets clear them */
int *motionTable[1150] = {0};

inline void ResetDynamicMotionManager(void)
{
    int i;
    for (i = 0; i <= 1146; i++) {
        if (motionKind[i].area == 4) {
            motionTable[i] = 0;
        }
    }
    motionMemorySizeStatic2 = 0;
}

inline void ResetStatic2MotionManager(int seg)
{
    int i;
    for (i = 0; i <= 1146; i++) {
        if (motionKind[i].area == seg) {
            motionTable[i] = 0;
        }
    }
}

/* the top of the motion file InitMotionFile relocates, the base every
   stored offset in it is added to */
static char *motionFileBase = 0; /* derived name */

/* the .mob records (MotFileHdr, FacialRec, NodeRec) are in
   motionFileManager.h */

#ifdef ICO_HOST

/* The host keeps every relocated word an EE address word (eeword.h):
   the same steps as the EE's below, each pointer stored through ICO_EEW. */
static void pursueNodeList(IcoEEWord *node, unsigned char *type)
{
    int i;
    int ofs;

    i = 0;
    while (*node != 0) {
        ofs = (int)*node;
        switch (type[i]) {
        default:
            debug_StdPrintfDummy("Invalid node formatID: (%d)\n", type[i]);
            break;
        case 1:
        case 4:
            *node = ICO_EEW(motionFileBase + ofs);
            break;
        case 2:
        case 5: {
            int *q = (int *)(motionFileBase + ofs);
            int r = ICO_EEW(motionFileBase + *q);
            *node = ICO_EEW(q);
            *q = r;
        } break;
        case 3:
        case 6: {
            NodeRec *q = (NodeRec *)(motionFileBase + ofs);
            q->nTable = ICO_EEW(motionFileBase + q->nTable);
            q->lastTable = ICO_EEW(motionFileBase + q->lastTable);
            *node = ICO_EEW(q);
        } break;
        }
        node++;
        i++;
    }
}

#else

static void pursueNodeList(void **node, unsigned char *type)
{
    int i;
    int ofs;

    i = 0;
    while (*node != 0) {
        ofs = (int)*node;
        switch (type[i]) {
        default:
            debug_StdPrintfDummy("Invalid node formatID: (%d)\n", type[i]);
            /* "this MOB file is broken, or its version is old." */
            debug_StdPrintfDummy("このMOBファイルは壊れているか、バージョンが古いです。\n");
            break;
        case 1:
        case 4:
            *node = (void *)(motionFileBase + ofs);
            break;
        case 2:
        case 5: {
            int *q = (int *)(motionFileBase + ofs);
            int r = (int)(motionFileBase + *q);
            *node = (void *)q;
            *q = r;
        } break;
        case 3:
        case 6: {
            NodeRec *q = (NodeRec *)(motionFileBase + ofs);
            q->nTable = (int)(motionFileBase + q->nTable);
            q->lastTable = (int)(motionFileBase + q->lastTable);
            *node = (void *)q;
        } break;
        }
        node++;
        i++;
    }
}

#endif

inline int CheckMotionIncludeFacialData(unsigned int *self)
{
    int r;
#ifdef ICO_HOST
    /* typeList (self[2]) is an EE address word; so is the header's */
    unsigned int p = (unsigned int)ICO_EEW(self) + 16;
#else
    unsigned int p = (unsigned int)self + 16;
#endif
    if (p < self[2])
        r = 0;
    else
        r = -1;
    return r;
}

#ifdef ICO_HOST

/* relocate the facial table in place, as EE address words */
static inline void relocFacialTable(FacialRec *p) /* derived name */
{
    int i;
    IcoEEWord *tbl;

    if (p->tbl != 0) {
        p->tbl = ICO_EEW(motionFileBase + (int)p->tbl);
        tbl = ICO_EEPTR(IcoEEWord *, p->tbl);
        for (i = 0; i < p->count; i++) {
            if (tbl[i] != 0) {
                tbl[i] = ICO_EEW(motionFileBase + (int)tbl[i]);
            }
        }
    }
}

/* Relocate the header in place, as EE address words. */
static inline int relocMotionFile(MotFileHdr *self) /* derived name */
{
    self->rootPos = ICO_EEW((char *)self + ((unsigned int *)self)[1]);
    self->typeList = ICO_EEW((char *)self + ((unsigned int *)self)[2]);
    self->nodeList = ICO_EEW((char *)self + ((unsigned int *)self)[3]);
    FlushCache(0);
    if (CheckMotionIncludeFacialData((unsigned int *)self) == 0) {
        self->facial = ICO_EEW(motionFileBase + ((unsigned int *)self)[4]);
        relocFacialTable(ICO_EEPTR(FacialRec *, self->facial));
    }
    pursueNodeList(ICO_EEPTR(IcoEEWord *, self->nodeList),
                   ICO_EEPTR(unsigned char *, self->typeList));
    return 0;
}

#else

/* relocate the facial table in place */
static inline void relocFacialTable(FacialRec *p) /* derived name */
{
    int i;

    if (p->tbl != 0) {
        p->tbl = (void **)(motionFileBase + (int)p->tbl);
        for (i = 0; i < p->count; i++) {
            if (p->tbl[i] != 0) {
                p->tbl[i] = (void *)(motionFileBase + (int)p->tbl[i]);
            }
        }
    }
}

/* Relocate the header in place.  Each offset is read through the file's
 * word view (the one CheckMotionIncludeFacialData reads) and the pointer
 * written through the typed header. */
static inline int relocMotionFile(MotFileHdr *self) /* derived name */
{
    self->rootPos = (char *)self + ((unsigned int *)self)[1];
    self->typeList = (unsigned char *)self + ((unsigned int *)self)[2];
    self->nodeList = (void **)((char *)self + ((unsigned int *)self)[3]);
    FlushCache(0);
    if (CheckMotionIncludeFacialData((unsigned int *)self) == 0) {
        self->facial = (FacialRec *)(motionFileBase + ((unsigned int *)self)[4]);
        relocFacialTable(self->facial);
    }
    pursueNodeList(self->nodeList, self->typeList);
    return 0;
}

#endif

void InitMotionFile(void *buf, char *name)
{
    motionFileBase = (char *)buf;
    relocMotionFile((MotFileHdr *)buf);
}

void InitMotionMemorySize(void)
{
    motionMemorySize = 0;
    motionMemorySizeStatic2 = 0;
}

int AddMotionMemorySize(int size, int seg)
{
    int total;
    if (seg != 0) {
        total = motionMemorySizeStatic2 + size;
        motionMemorySizeStatic2 = total;
    } else {
        total = motionMemorySize + size;
        motionMemorySize = total;
    }
    return total;
}

int GetMotionMemorySize(int seg)
{
    return seg ? motionMemorySizeStatic2 : motionMemorySize;
}
