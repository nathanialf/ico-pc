/*
 * port/compat/libmc.h
 *
 * The host build's libmc.h: the declarations the game uses, from
 * sce/libmc/libmc.h (this project's own clean-room header, MIT), with
 * pointer-sized and 64-bit types made host-correct.
 */
#ifndef ICO_COMPAT_LIBMC_H
#define ICO_COMPAT_LIBMC_H

/* a file's date and time as the card stores it */
typedef struct sceMcStDateTime {
    unsigned char Resv2;
    unsigned char Sec;
    unsigned char Min;
    unsigned char Hour;
    unsigned char Day;
    unsigned char Month;
    unsigned short Year;
} sceMcStDateTime;

/* the 64-byte directory entry sceMcGetDir has the IOP fill, one per entry
   asked for */
typedef struct sceMcTblGetDir {
    sceMcStDateTime _Create;     /* 0x00 */
    sceMcStDateTime _Modify;     /* 0x08 */
    unsigned int FileSizeByte;   /* 0x10 */
    unsigned short AttrFile;     /* 0x14 */
    unsigned short Reserve1;     /* 0x16 */
    unsigned int Reserve2;       /* 0x18 */
    unsigned int PdaAplNo;       /* 0x1C */
    unsigned char EntryName[32]; /* 0x20 */
} sceMcTblGetDir;

int sceMcChdir(int port, int slot, char *name, char *pwd);
int sceMcClose(int arg);
int sceMcDelete(int port, int slot, char *name);
int sceMcFlush(int arg);
int sceMcFormat(int port, int slot);
int sceMcGetDir(int port, int slot, char *name, int flags, int nblk, struct sceMcTblGetDir *table);
int sceMcGetInfo(int port, int slot, int *type, int *free, int *format);
int sceMcInit(void);
int sceMcMkdir(int port, int slot, char *name);
int sceMcOpen(int port, int slot, char *name, int flags);
int sceMcRead(int fd, void *buf, int len);
int sceMcSeek(int fd, int offset, int origin);
int sceMcSync(int mode, int *cmd, int *result);
int sceMcUnformat(int port, int slot);
int sceMcWrite(int fd, void *buf, int len);

#endif /* ICO_COMPAT_LIBMC_H */
