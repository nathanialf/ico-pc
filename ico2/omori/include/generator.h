/*
 * ico2/omori/include/generator.h
 *
 * The declarations of what generator.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef GENERATOR_H
#define GENERATOR_H

/* one row of generatorSubPosition, the data-only member
   generator-sub-position.o: an offset GetGeneratorSafePosition tries around
   a generator, and the generator label it belongs to */
typedef struct SafePosOffset { /* field names derived */
    float x;
    float y;
    float z;
    int kind;
} SafePosOffset; /* derived name */

extern const SafePosOffset generatorSubPosition[];

struct GObj;

struct SObjSimpleSetting;

struct GenWork *InitGeneratorGeo(struct GObj *gobj, struct SObjSimpleSetting *src);
void Generator_Call(struct GObj *gobj);
void Generator_ResetCount(struct GObj *gobj);
void Generator_Mask(struct GObj *gobj);
void Generator_MaskOff(struct GObj *gobj);
inline void SetMotherGenerator(int no, int label);
void Generator_Init(void);
int *GetbufpGeneratorPacket(void);
int GetsizeGeneratorPacket(void);
int RestoreGeneratorGeo(float *dst, float *src);
int RestoreGeneratorExtGeo(struct GObj *gobj, short *info);
int MemoryGenerator(short *info, struct GObj *gobj);
struct GObj *IsEnableCallEnemy(struct GObj *self);

struct GObj *DirectCallEnemy(struct GObj *gobj, struct GObj *mother, float *pos, float *dir,
                             int kind);

void LockEnemyGenerate(struct GObj *gobj);
void UnlockEnemyGenerate(struct GObj *gobj);
void RestoreReviveCount(struct GObj *gobj);
void ReturnEnemyToGenerator(int label);
int GeneratorWorkEnd(struct GObj *gobj);
int SearchActiveGenerator(void);
void ResetReviveCountEnemy(struct GObj *gobj);
void SetInfoSpKidnapGenerator(short *info);
void SetInfoSpKidnapEnemy(short *work);
inline int IsOpenGenerator(struct GObj *gobj);
int IsEnableCallEnemyByTargetGObj(struct GObj *gobj);
int CheckGeneratorCollision(struct GObj *gobj, float *dir);
void Generator_Delete(struct GObj *gobj);
void Generator_QuickCall(struct GObj *gobj);
void GetGeneratorSafePosition(float *dst, struct GObj *gobj);
int GetMotherGenerator(int label);
void MakeGeneratorPacket(void);
void ReadGeneratorPacket(void);

#endif /* GENERATOR_H */
