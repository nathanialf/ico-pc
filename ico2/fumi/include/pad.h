/*
 * ico2/fumi/include/pad.h
 *
 * The declarations of what pad.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef PAD_H
#define PAD_H

#include "shockdriver.h"

struct PadConf;

/* One sampled pad buffer: libpad's status byte (0 on a good read), the
   terminal byte (the controller type in its high nibble: 7 analog, 4
   digital), and the two button bytes the device leaves at +2 and +3, active
   low. */
typedef struct {          /* field names derived */
    unsigned char status; /* 0x00 */
    unsigned char termId; /* 0x01 */
    unsigned char hi;     /* 0x02 */
    unsigned char lo;     /* 0x03 */
    unsigned char rx;     /* 0x04 */
    unsigned char ry;     /* 0x05 */
    unsigned char lx;     /* 0x06 */
    unsigned char ly;     /* 0x07 */
    unsigned char pad8[24];
} IosPadBuf; /* derived name */

/* The device record iosPadDev carries one of per port: the buffer the last
   read filled is chosen by the index at +0xC, and +0x194 is set while the
   port has no controller. */
typedef struct {          /* field names derived */
    ShockRequestBox box;  /* 0x00 the player Init_Player sets up */
    unsigned char motor0; /* 0x10 */
    unsigned char motor1; /* 0x11 */
    char pad12[2];
    ShockReq motor; /* 0x14 the motor state Shock_SetMotor keeps, which Init_Controler clears */
} IosPadShock;      /* derived name */

typedef struct IosPadDevRec { /* field names derived */
    int port;                 /* 0x00 */
    int slot;                 /* 0x04 */
    int termId;               /* 0x08 */
    int idx;                  /* 0x0C */
    IosPadBuf buf[2];         /* 0x10 */
    char pad50[48];
    /* 0x80, scePadPortOpen's DMA buffer, which the library requires
       64-byte aligned: the record's stride of 0x200 and the 64-aligned
       start of pad.o's .data follow from it */
    unsigned char dmaBuf[256] __attribute__((aligned(64)));
    int state;      /* 0x180 the last scePadGetState */
    int phase;      /* 0x184 controler_stable_check's step, 99 when stable */
    int errCount;   /* 0x188 */
    int lastTermId; /* 0x18C */
    char pad190[4];
    unsigned int error;   /* 0x194 */
    unsigned char act[6]; /* 0x198 */
    char pad19E[6];
    IosPadShock shock;        /* 0x1A4 */
    unsigned long long flags; /* 0x1C0 */
} IosPadDevRec;               /* derived name */

/* The caller's pad handle, the record iosPadConnect fills in: the device and
   configuration, then the button words iosPadRead derives.  Every holder
   sets aside 0x60 bytes for it (actInitialize clears 0x60 at Act.pad, the
   tools' handles are 96 bytes). */
typedef struct IosPadCtx { /* field names derived */
    IosPadDevRec *dev;     /* 0x00 */
    struct PadConf *conf;  /* 0x04 */
    unsigned int now;      /* 0x08 */
    unsigned int trg;      /* 0x0C */
    unsigned int rel;      /* 0x10 */
    int word14;            /* 0x14 masked and cleared with the three above; nothing sets it */
    int now2;              /* 0x18 */
    int trg2;              /* 0x1C */
    int rel2;              /* 0x20 */
    int word24;            /* 0x24 the copy of word14 */
    char pad28[56];
} IosPadCtx; /* derived name */

/* The stick reading iosPadGetStick hands back: the raw pair, the stick's
   angle to the facing direction the actor keeps beside it, and the
   normalised direction and magnitude.  The actor record carries one at
   0x338. */
typedef struct IosPadStick { /* field names derived */
    int x;                   /* 0x00 */
    int y;                   /* 0x04 */
    int angle;               /* 0x08 */
    float dx;                /* 0x0C */
    float dz;                /* 0x10 */
    float mag;               /* 0x14 */
} IosPadStick;               /* derived name */

int iosPadActRequest(IosPadCtx *pad, int id);
void iosPadActStop(int key);
void iosPadActStopAll(void);

/* One actuator request iosPadActRequest hands out: its key, the player
   box, the shockList row's voice and life, the parameter Shock_Request is
   handed and the volume iosPadActVolumeSet sets. */
typedef struct {          /* field names derived */
    int key;              /* 0x00 */
    ShockRequestBox *box; /* 0x04 */
    int voice;            /* 0x08 the shockList row's voice */
    ShockParam prm;       /* 0x0C */
    short life;           /* 0x10 */
    short tick;           /* 0x12 */
    unsigned char volume; /* 0x14 */
    unsigned char pad[3];
} PadAct; /* derived name */

PadAct *iosPadActVolumeSet(int key, unsigned int val);
int iosPadConnect(IosPadCtx *pad, int a1, int port, struct PadConf *conf);
int iosPadDevInit(void *desc);
int iosPadDevRead(void);
void iosPadDisable(void);
void iosPadEnable(void);
/* out is an IosPadStick; way_tool hands it a 32-byte buffer (its .bss
   layout fixes the size) */
int iosPadGetStick(IosPadCtx *pad, void *out, int mode, int a3, int a4, int simulate);
int iosPadRead(IosPadCtx *pad);
void iosPadStickCameraCoord(void *out, IosPadStick *stick);
/* pad.c's vibration enable flag (.sdata). */
extern int iosPadActRequestEnable;
/* pad.c's custom pad configuration, the record iosPadConnect takes for the
   player's own button layout */
extern struct PadConf iosPadConfCustom;
/* pad.c's default pad configuration, the layout the debug tools connect with */
extern struct PadConf iosPadConfDefault;
void iosPadActInit(void);

/* shocklist: one pad vibration, 8 bytes. Readers: ico2/fumi/ios/pad.c
 * (PadActDef), ico2/fumi/sound/s_init.c (SeInfo). Owner:
 * ico2/fumi/include/pad.h. */
typedef struct { /* field names derived */
    int word0;   /* 0x00 */
    short voice; /* 0x04 the voice Shock_Request plays */
    short life;  /* 0x06 */
} PadActDef;     /* derived name */

extern const PadActDef shockList[];

#endif /* PAD_H */
