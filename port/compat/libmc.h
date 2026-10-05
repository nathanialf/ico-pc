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

/* sceMcSync's *result for a finished request (sce/libmc results; the codes
   fumi/ios/mcard.c and common/src/layout_action.c compare against) */
enum {
    sceMcResSucceed = 0,
    sceMcResChangedCard = -1,   /* sceMcGetInfo: a card was inserted since the last call */
    sceMcResNoFormat = -2,      /* the card is not formatted */
    sceMcResFullDevice = -3,    /* no room */
    sceMcResNoEntry = -4,       /* no such file or directory; Mkdir: it exists */
    sceMcResDeniedPermit = -5,  /* bad handle, or the open mode forbids the call */
    sceMcResNotEmpty = -6,      /* Delete of a directory that has entries */
    sceMcResUpLimitHandle = -7, /* more than three files open */
    sceMcResFailDetect = -9,    /* no card in the slot */
    sceMcResFailDetect2 = -10   /* sceMcGetInfo: no card in the slot */
};

/* sceMcOpen flags and sceMcSeek origins */
enum {
    SCE_RDONLY = 0x0001,
    SCE_WRONLY = 0x0002,
    SCE_RDWR = 0x0003,
    SCE_CREAT = 0x0200,
    SCE_TRUNC = 0x0400
};

enum { SCE_SEEK_SET = 0, SCE_SEEK_CUR = 1, SCE_SEEK_END = 2 };

/* the function numbers sceMcSync reports in *cmd */
enum {
    sceMcFuncNoCardInfo = 0x01,
    sceMcFuncNoOpen = 0x02,
    sceMcFuncNoClose = 0x03,
    sceMcFuncNoSeek = 0x04,
    sceMcFuncNoRead = 0x05,
    sceMcFuncNoWrite = 0x06,
    sceMcFuncNoFlush = 0x0A,
    sceMcFuncNoMkdir = 0x0B,
    sceMcFuncNoChDir = 0x0C,
    sceMcFuncNoGetDir = 0x0D,
    sceMcFuncNoDelete = 0x0F,
    sceMcFuncNoFormat = 0x10,
    sceMcFuncNoUnformat = 0x11
};

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
