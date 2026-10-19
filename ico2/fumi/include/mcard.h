/*
 * ico2/fumi/include/mcard.h
 *
 * The declarations of what mcard.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef MCARD_H
#define MCARD_H

struct McMgr;

/* mcard.c's globals (typedef.h declares IosMcProductFile with its record) */
extern int IosMcMgrSleep;
extern int IosMcLock;
extern char *iconName[];
extern char *iOSMcSaveSeg_pal[], *iOSMcSaveSeg_jpn[]; /* mcard.c: iOSMcSaveSeg per disc */
extern int IosMcPreviewInfo[];
extern struct IosMsgQueue McMsgQ;
/* mcard.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
void iosMcMgrSync(struct McMgr *mp);
void iosMcTest(void);
int iosMcSync(struct McMgr *mp);
int iosMcGetInfo(struct McMgr *mp);
int iosMcFormat(struct McMgr *mp);
int iosMcUnformat(struct McMgr *mp);
int iosMcGetDir(struct McMgr *mp);
int iosMcDelete(struct McMgr *mp);
int iosMcSaveIconBlock(struct McMgr *mp);
int iosMcSaveProductBlock(struct McMgr *mp);
int iosMcLoadProductBlock(struct McMgr *mp);
int iosMcSaveGameBlock(struct McMgr *mp, void *arg);
int iosMcLoadGameBlock(struct McMgr *mp, void *arg);
int iosMcChdirProduct(struct McMgr *mp);
int iosMcGetBlockSaveInfo(struct McMgr *mp);
void iosMcHandlerRead(struct McMgr *mp, unsigned char *buf, int len);
void iosMcHandlerWrite(struct McMgr *mp, unsigned char *buf, int len);
void iosMcManager(void);

/* iconfile: one memory card icon file, 0x24 bytes. Reader: ico2/fumi/ios/
 * mcard.c. Owner: ico2/fumi/include/mcard.h. */
typedef struct IconFile { /* field names derived */
    char name[32]; /* 0x00 */
    int size;      /* 0x20 */
} IconFile;        /* derived name */

extern const IconFile iconFile[];

#endif /* MCARD_H */
