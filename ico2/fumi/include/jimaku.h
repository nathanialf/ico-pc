/*
 * ico2/fumi/include/jimaku.h
 *
 * The declarations of what jimaku.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef JIMAKU_H
#define JIMAKU_H

#include "message.h" /* IosMsgWord */

/* the subtitle request's body, shared by jimaku.c and the script files that
   queue subtitles; the offsets are those within JimakuArg */
typedef struct JimakuSub { /* field names derived */
    char pad0[44];         /* 0x0C */
    int block;             /* 0x38 */
    int n;                 /* 0x3C */
    int ringPos;           /* 0x40 */
    int jump;              /* 0x44 */
    void *cur;             /* 0x48 */
    void *bg;              /* 0x4C */
} JimakuSub; /* derived name */

/* the subtitle request jimakuManager reads */
typedef struct JimakuArg { /* field names derived */
    int cmd;               /* 0x00 */
    char pad4[4];
    int done;      /* 0x08 */
    JimakuSub sub; /* 0x0C */
} JimakuArg; /* derived name */

/* jimaku.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
void jimakuManager(void);
void jimakuUndisp(JimakuArg *msg);
void jimakuBegin(JimakuArg *msg);
void jimakuEnd(JimakuArg *msg);
void jimakuJump(JimakuArg *msg);
/* jimaku.c's globals */
extern struct IOSThread jimakuThread;
extern char jimakuThreadStack[];
extern struct IosMsgQueue jimakuMsgQ;
extern int jimakuOn;
extern IosMsgWord jimakuMsgBuf[2];
extern JimakuArg jimaku_msg;
void jimakuDisp(JimakuArg *msg);

/* unmapped_0055FBD0: one subtitle file name, 0x20 bytes. Reader:
 * ico2/fumi/src/jimaku.c (char [][32]). Owner: ico2/fumi/include/jimaku.h. */
typedef struct {   /* field names derived */
    char path[32]; /* 0x00, "text/data_EG01.jim" ... */
} JimakuFileName;  /* derived name */
extern const JimakuFileName jimakuFileName[];

#endif /* JIMAKU_H */
