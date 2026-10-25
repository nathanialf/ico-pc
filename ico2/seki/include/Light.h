/*
 * ico2/seki/include/Light.h
 *
 * The declarations of what Light.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef LIGHT_H
#define LIGHT_H

#include <libvu0.h>

struct GObj;

struct Sub15C;

/* the per-object light matrix record at *(Sub15C + 0x874) (typedef.h's
 * Sub15C keeps the slot as void *).  light_MakeLightMatrix builds the normal
 * light matrix at 0x00 from the three directions at 0x80 and the colour
 * matrix at 0x40 from the colours at 0xB0 and the ambient at 0xE0 (its last
 * word, 0xEC, is the scale), and tests the mode at 0xF0; light_getNearLight
 * fills the directions and colours; DObj.c copies the mode and tests it
 * against 4, Packet.c hands it to pac_makePacket and RegistPacket.c gates the
 * light packet on it.  The members are the libvu0 vector and matrix types. */
typedef struct LightMatrix { /* field names derived */
    sceVu0FMATRIX normal;    /* 0x00 */
    sceVu0FMATRIX color;     /* 0x40 */
    sceVu0FVECTOR dir[3];    /* 0x80 */
    sceVu0FVECTOR col[3];    /* 0xB0 */
    sceVu0FVECTOR ambient;   /* 0xE0 */
    int mode;                /* 0xF0 */
} LightMatrix;               /* derived name */

/* obj-light: one object light, 0x10 bytes. Reader: ico2/seki/src/Light.c
 * (float [][4]: the colour scaled by 1/256). */
typedef struct {  /* field names derived */
    float col[3]; /* 0x00, 0..255 */
    float range;  /* 0x0C */
} ObjLight;       /* derived name */

/* the object light table light_AddLight indexes by the object's light number */
extern const ObjLight objectLight[];

struct Light; /* Light.c's light list node */

struct AmbientVolume; /* Light.c's ambient volume node */

struct AmbientVolume *light_AddAmbientObject(int obj);
struct Light *light_AddLight(struct GObj *self, int b, int kind);
void light_DispVolume(void);
void light_KillAllFixLight(void);
void light_MakeLightMatrix(struct Sub15C *self, int idx);
void light_resetFlatLight(void);
int light_Tool(void);

void light_ResetLight(void);

void light_InitLight(void);

#endif /* LIGHT_H */
