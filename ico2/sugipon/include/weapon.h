/*
 * ico2/sugipon/include/weapon.h
 *
 * The declarations of what weapon.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef WEAPON_H
#define WEAPON_H

struct GObj;

/* weapon-def: one weapon kind, 0x24 bytes: the blur trail's length, the grip
   and the hit power (attackhit's damage factor), the weight GetWeaponWeight
   gives, the blend mode of the blur the weapon trails (-1 for none) and the
   blur's colour.
   Readers: weapon.c, ico2/omori/src/attackhit.c (power and flags),
   ico2/fumi/src/act-game.c (motOrient). */
typedef struct {            /* field names derived */
    float length;           /* 0x00, the blade tip's distance */
    float grip;             /* 0x04 */
    float power;            /* 0x08 */
    int weight;             /* 0x0C */
    int word10;             /* 0x10 */
    int blur;               /* 0x14, the blur's blend mode, -1 for none */
    unsigned char color[4]; /* 0x18, the blur's colour */
    int motOrient;          /* 0x1C, the motion orient the holder takes (act-game.c) */
    unsigned int flags;     /* 0x20, bit 0 unguardable, bit 1 the swing sweeps */
} WeaponDef;                /* derived name */

/* weapon-fumble-def: one fumble placement, 0x18 bytes, three to a weapon
   slot: the target position and the three turns the dropped weapon is given,
   in degrees. */
typedef struct {  /* field names derived */
    float pos[3]; /* 0x00 */
    float rotY;   /* 0x0C */
    float rotX;   /* 0x10 */
    float rotZ;   /* 0x14 */
} FumbleRow;      /* derived name */

struct GObj *CheckSwapableWeapon(struct GObj *self, float dist);
int CheckWeaponKind(struct GObj *self);
void ExecWeaponHitReaction(struct GObj *self);
struct GObj *GetTorchGObjOfWeapon(struct GObj *self);
void LightTorchOffOfWeapon(struct GObj *self);
void LightTorchOnOfWeapon(struct GObj *self);

void LightTorchOnOfWeaponWithNoSE(struct GObj *self);

void PickupWeapon(struct GObj *self, struct GObj *holder, int focus);
void ReleaseWeapon(struct GObj *self);
int ReleaseWeaponWithFumbleSequential(struct GObj *g);

void SetWeaponOffsetMode(struct GObj *self, int mode);

void SetWeaponTorchChainReactionFlagAll(int flag);
void WeaponCurPos(struct GObj *self, void *center, void *from, void *to);
void dispInsectNet(struct GObj *g);
/* unprototyped: the third argument is weapon.c's own layout record type. */
void weaponHitReactionSE(struct GObj *);
float GetWeaponWeight(struct GObj *self);

struct SObjSimpleSetting;

void torchOnOfWeaponSE(struct GObj *torch);
void torchOffOfWeaponSE(struct GObj *torch);
void weaponFumbleSE(struct GObj *g);
void weaponStickSE(struct GObj *g);
void ReleaseWeaponWithFumbleTargetPos(struct GObj *g, void *pos, void *quat, void *rot, float t);
void WeaponHitEffect(struct GObj *self, void *enemy);
void *InitWeaponGeo(struct GObj *g, struct SObjSimpleSetting *lay);
void WeaponGeo(struct GObj *g);
void WeaponDL(struct GObj *g);
void ReleaseWeaponWithFumble(struct GObj *self, float *move, float *quat);
int InitWeaponFumbleSequence(struct GObj *self);

void *InitDemoQueensSword(struct GObj *g, struct SObjSimpleSetting *lay);
void ExecDemoQueensSword(struct GObj *g);

#endif /* WEAPON_H */
