/*
 * ico2/sugipon/include/girl.h
 *
 * The declarations of what girl.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef GIRL_H
#define GIRL_H

struct GObj;

/* The declarations below lead this header because their order is load-bearing:
 * gcc 2.9 emits the deferred out-of-line copy of a plain-inline function in
 * first-declaration order, so this is the order girl.c's inline tail has. */
void SetGirlClothDispSwitch(struct GObj *gobj, int part, int on);

void SetGirlHairDispSwitch(struct GObj *gobj, int on);

struct SObjSimpleSetting;

void *InitGirlGeo(struct GObj *gobj, struct SObjSimpleSetting *csv);
void GirlGeo(struct GObj *gobj);

void GirlAI(struct GObj *gobj);

void GirlDL(struct GObj *gobj);

#endif /* GIRL_H */
