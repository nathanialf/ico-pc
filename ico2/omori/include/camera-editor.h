/*
 * ico2/omori/include/camera-editor.h
 *
 * The declarations of what camera-editor.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef CAMERA_EDITOR_H
#define CAMERA_EDITOR_H

#include "eeword.h"
#include "typedef.h"

/* A camera pin, one item record of a camera set (0x5C bytes, copied whole):
 * the camera position and the point it looks at, two offsets of the look-at
 * point, the on flag, the field of view, the radius inside which the pin damps
 * the other pins' weights, and the hand camera's eye and look-at rates and two
 * angle limits.  CameraMove (camera-ico2.c) blends the pins by distance; the
 * editor steps pos, look, on and fov, draws range, and gives its default the
 * stage's hand-camera rate as eyeRate. */
typedef struct { /* field names derived */
    float pos[3];         /* 0x00, the camera position */
    float look[3];        /* 0x0C, the point the camera looks at */
    float ofs[3];         /* 0x18, the look-at offset, turned to the target's facing */
    int on;               /* 0x24, the pin takes part in the blend */
    float fov;            /* 0x28 */
    int word2C;           /* 0x2C */
    float range;          /* 0x30 */
    int word34;           /* 0x34 */
    float eyeRate;        /* 0x38, the hand camera's eye rate */
    float atRate;         /* 0x3C, the hand camera's look-at rate */
    float limitP;         /* 0x40, the hand camera's angle limits */
    float limitV;         /* 0x44 */
    float float48;        /* 0x48, in the version-2 file record; nothing reads it */
    float float4C;        /* 0x4C, likewise */
    float ofsB[3];        /* 0x50, the look-at offset added as it is */
} PinRec; /* derived name */

/* A camera group (box) of a camera set, 0x4C bytes: the name, the box's centre
 * and half-size, the range of the group's pins, the blend mode and the kind,
 * and the group's pin records.  The groups lie at a 76-byte stride:
 * camera-ico2.c reads them from the loaded set, camera-editor.c edits them in
 * its two copies and draws and dumps them. */
typedef struct CamGroup { /* field names derived */
    char name[32];   /* 0x00 */
    float center[3]; /* 0x20 */
    float range[3];  /* 0x2C, the box's half-size */
    int first;       /* 0x38, the group's first pin */
    int end;         /* 0x3C, one past its last pin */
    int mode;        /* 0x40, 2: ChaseCamera instead of the pins */
    int kind;        /* 0x44, a change of kind restarts the monitor camera */
    ICO_EEWORD(PinRec *) items; /* 0x48, the group's pin records (an EE address word, eeword.h) */
} CamGroup; /* derived name */

/* CamGroup.items as a pointer, and its store (eeword.h): the field is a
 * 32-bit word on the host, so the record keeps its 76-byte stride. */
#ifdef ICO_HOST
#define CAMGROUP_ITEMS(g) ICO_EEPTR(PinRec *, (g)->items)
#define CAMGROUP_SET_ITEMS(g, p) ((g)->items = (IcoEEWord)ICO_EEW(p))
#else
#define CAMGROUP_ITEMS(g) ((g)->items)
#define CAMGROUP_SET_ITEMS(g, p) ((g)->items = (p))
#endif

/* The head of a camera-set file (.gcm), 16 bytes: the magic, the file
 * version (0 to 3; ReadCameraSet converts the older three), the group count
 * and the total pin count.  The groups (CamGroup, 76 bytes each) follow,
 * then the pins, whose stride is the version's (92 bytes in version 3). */
typedef struct CamSetFile { /* field names derived */
    int magic;              /* 0x00 */
    int ver;                /* 0x04 */
    int count;              /* 0x08 */
    int total;              /* 0x0C */
} CamSetFile;               /* derived name */

extern PinRec cameraPinDefault;
extern CamGroup cameraGroupDefault;
extern ICO_WORD curmenu;
extern int print_y;
extern unsigned char exit_f;
/* A menu of the camera editor: its thread record, then the menu that opened
   it, which it wakes and hands back to on exit, and the menu's argument. */
typedef struct MenuThread { /* field names derived */
#ifdef ICO_HOST
    /* an IOSThread is 152 bytes on a 64-bit host, 112 on the EE and 32-bit hosts */
    char thread[sizeof(void *) == 4 ? 112 : 160] __attribute__((aligned(8)));
#else
    char thread[112];
#endif
    char *parent; /* 0x70 */
    int arg;      /* 0x74 */
} MenuThread; /* derived name */

/* the functions camera-editor.c defines `inline` */
inline void debug_NMarker(float *pos, int r, int g, int b, float size);
inline void debug_Marker(float *pos, int r, int g, int b, float size, float pulse);
inline void debug_Arrow(float len, void *from, void *to, int r, int g, int b);
inline void InitCameraEditor(void);
inline int debug_CameraEditor(void);
inline void CameraEdit_reset_box(int box);
inline void CameraEdit_reset_pin(int box, int pin);
inline void CameraEdit_reflect_box(int box);
inline void CameraEdit_reflect_pin(int box, int pin);
inline int CameraEdit_BOX_NUMBER(void);
inline int CameraEdit_PIN_NUMBER(int box);
inline int CameraEdit_PIN_NUMBER_ALL(CamGroup *box, int n);
inline CamGroup *CameraEdit_BOX(int box);
inline PinRec *CameraEdit_PIN(int box, int pin);
inline void CameraEdit_DispPin(int box, int pin);
inline void ConvertCameraSetBuffer(int n, CamGroup *item, char *groups);
inline void StickToTrans(int stickV, int stickH, int vertical, int heading, float *out, int speed);
inline void menu_2(MenuThread *m);
inline void group_select(MenuThread *m);
/* compiled in place */
int CameraEdit_add_pin(int box, char *src);
void DebugDispBox(float *c, float *s);
void dispCameraGroupType2(int box, unsigned char sel);
void menuGroupEdit(MenuThread *m);
void menuGroupSelect(MenuThread *m);
void menuPinEdit(MenuThread *m);
void menuPinSelect(MenuThread *m);
void test_camedit(void);
void wakeup_cameraedit(void);

/* a quadword read as four floats or as two doublewords */
typedef union Mat4 { /* field names derived */
    float f[4];
    long long q[2];
} Mat4; /* derived name */

int CameraEdit_add_box(CamGroup *src);

#endif /* CAMERA_EDITOR_H */
