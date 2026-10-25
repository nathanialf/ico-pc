/*
 * ico2/omori/include/attackhit.h
 *
 * The declarations of what attackhit.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ATTACKHIT_H
#define ATTACKHIT_H

/* one row of attackData, the data-only member attack-def.o: an attack
   kind's motion, the node it hits from, its reach and power and its flags.
   No code reads the first four words (-1 in every row but the first). */
typedef struct { /* field names derived */
    /* 0x00 */ int word0[4];
    /* 0x10 */ int motion;    /* the motion the row belongs to */
    /* 0x14 */ int focusNode; /* the focus node a body attack hits from */
    /* 0x18 */ float radius;
    /* 0x1C */ float power;
    /* 0x20 */ unsigned int weapon : 1; /* the attack is the held weapon's */
    unsigned int down : 1;              /* it knocks the target down */
    unsigned int unguardable : 1;
    unsigned int stone : 1;  /* it is a stone hit */
    unsigned int toRoot : 1; /* the reach runs from the focus node to the root */
    unsigned int padBits : 27;
} AttackKindEntry; /* derived name */

extern const AttackKindEntry attackData[];

struct GObj;

void CommonAttackCenter(struct GObj *gobj);


struct GObj *AttackCenter(struct GObj *gop, int group, float *pos, float *ofs, float radius);

struct GObj *_AttackCenter(struct GObj *gop, int group, float *pos, float *ofs, float radius,
                           struct GObj *spare);

void AttackCenter_WithDir(struct GObj *gop, int group, float *pos, float *dir, float radius);
void EnemyAttackCenter(struct GObj *gobj);
void BoyAttackCenter(struct GObj *gobj);

#endif /* ATTACKHIT_H */
