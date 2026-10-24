/*
 * ico2/fumi/include/act.h
 *
 * The declarations of what act.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ACT_H
#define ACT_H

struct GObj;

struct Act;

struct GProc;

struct IosPadStick;

/* act.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
void actInitialize_geo(void *self);
int ACTReserveTarget(struct GObj *self, void *arg, int mail);
void _ACTRun(int n);
void _ACTWait(int frames);
struct GProc *actCreateSubThreadGOppArg(void (*fn)(), int pri);
void actSetInterrupt(char *self, int val);
void ConvertStickToAbsCoord(void *out, struct IosPadStick *stick);
void ActSetStartBrainStatus(struct GObj *self, int status);
void actWaitCondition(int value, int mask);

void ACTDebugMove(struct GObj *self, int a1);
void actChangeActBrain(struct GObj *self, void (*fn)(), struct GProc **slot);
void actChangeActMain(struct GObj *self, void (*fn)(), struct GProc **slot);
void actCreateMotionThread(void (*fn)(), int pri, struct GProc **slot);
struct GProc *actCreateSubThread(void (*fn)(), int pri);
struct Act *actInitialize(struct GObj *self);
void actInitialize_ext_charcter(struct GObj *self);
void actInitialize_only_charcter(char *self);

/* act-mode-def: one act mode, 0x50 bytes, indexed by Act+0x34. Readers:
 * ico2/fumi/src/act.c (the act per actor kind, the mail, the intr-list
 * start, the flag bits), commonact.c, act-game.c, boyact.c, enemy_act.c,
 * girl_act.c (the flag bits, the name). Owner: ico2/fumi/include/act.h. */
typedef struct { /* field names derived */

    struct {
        void (*act)(); /* +0x00, the mode's act function */
        int word4;     /* +0x04, compared across a mode change */
        int word8;     /* +0x08 */
    } ent[3];          /* 0x00, one per actor kind (Act+0x48) */

    char name[32];      /* 0x24, the debug name */
    int intrList;       /* 0x44, the first actIntrList row */
    int mail;           /* 0x48, ACTSendMailCorrect's mail */
    /* 0x4C, the mode's flag bits */
    unsigned int carried : 1;  /* bit 0, the girl is carried */
    unsigned int bit1 : 1;
    unsigned int onChain : 1;  /* bit 2, ACTGame_isHangChain */
    unsigned int attractWait : 1; /* bit 3, the girl holds an attract turn 10 s */
    unsigned int hintCount : 1; /* bit 4, the girl counts hint frames */
    unsigned int bit5 : 1;
    unsigned int keepHand : 1; /* bit 6, the boy keeps the girl's hand */
    unsigned int bit7 : 1;
    unsigned int softIk : 1; /* bit 8, the girl's soft IK rate */
    unsigned int bit9 : 1;
    unsigned int bit10 : 1;
    unsigned int skipMarked : 1; /* bit 11, the flagged mail lists are skipped */
    unsigned int bit12 : 1;
    unsigned int escort : 1; /* bit 13, an escort status */
    unsigned int bit14 : 1;
    unsigned int : 17;
    /* PC port: host-only, after the European record's 0x50 bytes: flag bits of
       the USA disc's word at 0x4C (bits 1, 3, 6, 8 there) that no European mode
       has (config/data_schema.ntsc.txt's extend rows); a disc without them
       leaves them 0 */
    unsigned int holdDir : 1;  /* Act flags18 bit 45: the stick does not turn the boy */
    unsigned int saveInfo : 1; /* ACTGame_SaveActorInformation saves the position */
    unsigned int pullUp : 1;   /* handoff_heroin does not let the girl go */
    unsigned int stopMail : 1; /* a stopped enemy without the girl mails 242 */
    unsigned int : 28;
} ActModeRec; /* derived name */
extern const ActModeRec actModeTbl[];

/* act-intrlist: one actor mail entry, 0x18 bytes; lists end at the entry
 * whose kind is 429. Readers: ico2/fumi/src/act.c (BeforeFunc,
 * act_check_intr_list, act_check_mail) and commonact.c (ACTRunIntrCorrect).
 * Owner: ico2/fumi/include/act.h. */
typedef struct IntrMail {                           /* field names derived */
    void (*motion)(int);                            /* 0x00, motion thread 21 */
    void (*extra)(int);                             /* 0x04, motion thread 22 */
    void (*handler)(char *self, int id, void *arg); /* 0x08, every frame the mail is held */
    void (*accept)(char *self, int id, void *arg);  /* 0x0C, when the mail is accepted */
    unsigned short kind;                            /* 0x10, the mail id */
    short mode;                                     /* 0x12, the act mode it switches to */
    unsigned int flags;                             /* 0x14, bit 18: entry live */
} IntrMail; /* derived name */
extern IntrMail actIntrList[];

#endif /* ACT_H */
