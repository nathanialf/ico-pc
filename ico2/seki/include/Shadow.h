/*
 * ico2/seki/include/Shadow.h
 *
 * The declarations of what Shadow.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef SHADOW_H
#define SHADOW_H

#include "typedef.h"

/* Shadow.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
void shadow_KillShadow(int val);

void shadow_DispCancel(int id, int cancel);
void shadow_SetLength(Sub15C *o, float len);

void shadow_Init(void);
void shadow_Render(Sub15C *o);
void shadow_RenderVolume(Sub15C *o);
void shadow_RenderVolumeMulti(Sub15C *o, int idx);
void shadow_Reset(void);
void shadow_Draw(void);
int shadow_Tool(void);

struct PObjModel;

void shadow_MakeObjectData(struct PObjModel *mdl);

#endif /* SHADOW_H */
