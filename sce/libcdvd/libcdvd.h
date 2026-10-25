/*
 * sce/libcdvd/libcdvd.h
 *
 * Declarations of the entry points this tree calls, under the public name
 * of the PS2 SDK and newlib header for them (libcdvd.h).  Each one is
 * the signature of the member that defines the symbol in sce/.  Nothing here
 * is copied from an SDK header.
 *
 * Only what this tree uses is declared.
 */
#ifndef SCE_LIBCDVD_LIBCDVD_H
#define SCE_LIBCDVD_LIBCDVD_H

/* The read mode sceCdRead and sceCdStream take. */
typedef struct {
    unsigned char trycount;
    unsigned char spindlctrl;
    unsigned char datapattern;
    unsigned char pad;
} CdRMode; /* derived name */

/* the records sceCdSearchFile and sceCdReadClock fill in.  Their bodies are
   in the members that define those calls, and seki's FileManager.c carries
   its own sceCdlFILE body. */
struct sceCdlFILE;

struct sceCdCLOCK;

int sceCdBreak(void);

int sceCdDiskReady(int mode);
int sceCdGetDiskType(void);
int sceCdGetError(void);
int sceCdInit(int mode);
int sceCdMmode(int media);
int sceCdRead(int lsn, int sectors, void *buf, CdRMode *mode); /* definition in sce/ */

int sceCdReadClock(struct sceCdCLOCK *clock); /* definition in sce/ */

int sceCdReadIOPm(int lsn, int sectors, void *buf, CdRMode *mode); /* definition in sce/ */
int sceCdSearchFile(struct sceCdlFILE *fp, const char *name);      /* definition in sce/ */
int sceCdStInit(int bufmax, int bankmax, void *buf);               /* definition in sce/ */
int sceCdStRead(int sectors, void *buf, int mode, int *err);       /* definition in sce/ */
int sceCdStStart(int lsn, CdRMode *mode);                          /* definition in sce/ */
int sceCdStStat(void);                                             /* definition in sce/ */
int sceCdStStop(void);                                             /* definition in sce/ */

int sceCdStatus(void);

int sceCdStream(int lsn, int sectors, void *buf, int cmd, CdRMode *mode); /* definition in sce/ */
int sceCdSync(int mode);                                                  /* definition in sce/ */
int sceCdSyncS(int mode);                                                 /* definition in sce/ */
int sceFsReset(void);                                                     /* definition in sce/ */

#endif /* SCE_LIBCDVD_LIBCDVD_H */
