/*
 * ico2/script/include/st04e.h
 *
 * The declarations of what st04e.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ST04E_H
#define ST04E_H

#include "typedef.h"

void actSt04eWaterFlagOn(GObj *volatile self);

void actSt04eWaterMain(GObj *volatile self);
void actSt04eWaterStop(GObj *volatile self);
void actSt04eWaterSwitch(GObj *volatile self);

#endif /* ST04E_H */
