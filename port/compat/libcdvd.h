/*
 * port/compat/libcdvd.h
 *
 * The host build's libcdvd.h: the declarations the game uses, from
 * sce/libcdvd/libcdvd.h (this project's own clean-room header, MIT), with
 * pointer-sized and 64-bit types made host-correct.
 *
 * The host defines these in port/data/cdvd_host.c over the port's VFS.  sceCdReadIOPm's and sceCdStInit's buffers are IOP
 * addresses carried in a pointer, as on the PS2; the host maps them into
 * ico_iop_ram (port/data/iop_ram.h).
 */
#ifndef ICO_COMPAT_LIBCDVD_H
#define ICO_COMPAT_LIBCDVD_H

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
int sceCdRead(int lsn, int sectors, void *buf, CdRMode *mode);
int sceCdReadClock(struct sceCdCLOCK *clock);
int sceCdReadIOPm(int lsn, int sectors, void *buf, CdRMode *mode);
int sceCdSearchFile(struct sceCdlFILE *fp, const char *name);
int sceCdStInit(int bufmax, int bankmax, void *buf);
int sceCdStPause(void);
int sceCdStRead(int sectors, void *buf, int mode, int *err);
int sceCdStResume(void);
int sceCdStSeek(int lsn);
int sceCdStStart(int lsn, CdRMode *mode);
int sceCdStStat(void);
int sceCdStStop(void);
int sceCdStatus(void);
int sceCdStream(int lsn, int sectors, void *buf, int cmd, CdRMode *mode);
int sceCdSync(int mode);
int sceCdSyncS(int mode);
int sceFsReset(void);

#endif /* ICO_COMPAT_LIBCDVD_H */
