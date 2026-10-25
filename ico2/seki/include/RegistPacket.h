/*
 * ico2/seki/include/RegistPacket.h
 *
 * The declarations of what RegistPacket.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef REGISTPACKET_H
#define REGISTPACKET_H

#include "typedef.h"

struct PacHeader;

struct PacLine;

struct PObjMaterial;

void reg_DispAccessoryWithShadow(Sub15C *o, Sub15C *src);
void reg_DispEnemy(void *sub);

void reg_DispMultiPri(Sub15C *o, int pri);

void reg_DispObj(Sub15C *o);
int reg_GetShinePri(int shine);
void reg_Init(void);

void reg_RenderReflection(Sub15C *o, int pri);

void reg_SetScissorSw(int val);

#endif /* REGISTPACKET_H */
