/*
 * ico2/sugipon/include/staticBlur.h
 *
 * The declarations of what staticBlur.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef STATICBLUR_H
#define STATICBLUR_H

void SetAuraInspireParam(float z);
void SetMotionBlur(int val);
void SetStaticBlur(int x);
void MotionBlur(void);
void FullScreenEffectBefore(void);
void FullScreenEffectAfter(void);
void makeFullScreenFlareBefore(int mode);
void makeFullScreenFlareAfter(int mode);
void depthField(float depth, float width, float rate);
void GetSunWorldPos(float *pos);
int InitStaticBlur(int unused, float *dir);
void StaticBlur(void);
void StaticBlurDL(void);
void SetDepthFadeParam(float start, float width, int level);
void InitializeStaticBlur(void);
void _initStaticBlur(void);
void SetAuraEffect(void);

#endif /* STATICBLUR_H */
