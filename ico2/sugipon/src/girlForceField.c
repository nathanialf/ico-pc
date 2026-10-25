#include "typedef.h"
#include "sugiCommon.h"
#include "memory.h"
#include "geometryManager.h"
#include "matrixDrive.h"
#include "gamesys.h"

/* Per-object force-field state: InitGirlForceFieldGeo returns it, the object
   creator hangs it at the display object's work and GirlForceFieldDL reads it. */
typedef struct GirlForceFieldWork { /* field names derived */
    float radius;
    float invRadius;
    int frame;
} GirlForceFieldWork; /* derived name */

#include "girlForceField.h"
#include "ios.h"
#include "main.h"
#include "StageAnimation.h"
#include "sceneManager.h"

/* the girl's blue, the colour boy.c's position-sync marker draws her sphere
   in; nothing reads it */
static int forceFieldColor[4] = {64, 96, 128, 128}; /* derived name */

inline GirlForceFieldWork *InitGirlForceFieldGeo(GObj *self, SObjSimpleSetting *param)
{
    GirlForceFieldWork *w = (GirlForceFieldWork *)iosMallocDebug(
        ios_partition_sugipon, sizeof(GirlForceFieldWork), "src/girlForceField.c", 23);
    struct DObjNode *c;
    w->radius = param->scale[2];
    w->invRadius = 1.0f / w->radius;
    w->frame = 0;
    c = ((SubHandle *)&self->dobj)->sub->nodes;
    c->scale[0] = c->scale[1] = c->scale[2] = 1.0f;
    return w;
}

/* the action table the layout rows select into */
extern OaRecB objAction[];

static void GirlForceFieldGeo_aug6jun26(GObj *self)
{
    float pos[4];
    float quat[4];
    float gpos[4];
    GirlForceFieldWork *w = GOBJ_SUB(self)->work;
    float d2;
    float ratio;

    UpdateRootMatrix(self);
    GetRootPosition(pos, self);

    if (girlGObj != 0) {
        GetRootPosition(gpos, girlGObj);
        d2 = distance_squared(gpos, pos);
        if (d2 < w->radius * w->radius) {
            ratio = 1.0f - FSqrt(d2) * w->invRadius;
            ratio = ratio < 0.0f ? 0.0f : ratio;

            GetRootQuaternion(quat, self);
            w->frame = (int)stage_PlayBgAnimationDissolve(
                objAction[objLayout[self->labelId].action].baseMode, pos, quat, (float)w->frame,
                ratio);
            return;
        }
    }
    w->frame = 0;
}

/* PC port: the geo step takes the object, which only the August preview's reads (that build
   dissolves in the geo step and draws nothing in the display list step, the European program
   the other way round) */
inline void GirlForceFieldGeo(GObj *self)
{
    if (ICO_AUG6JUN26) {
        GirlForceFieldGeo_aug6jun26(self);
    }
}

static inline void GirlForceFieldDL_aug6jun26(GObj *self)
{
    (void)self;
}

void GirlForceFieldDL(GObj *self)
{
    if (ICO_AUG6JUN26) {
        GirlForceFieldDL_aug6jun26(self);
        return;
    }
    float pos[4];
    float quat[4];
    float gpos[4];
    GirlForceFieldWork *w = GOBJ_SUB(self)->work;
    float d2;
    float ratio;

    UpdateRootMatrix(self);
    GetRootPosition(pos, self);

    if (girlGObj != 0) {
        GetRootPosition(gpos, girlGObj);
        d2 = distance_squared(gpos, pos);
        if (d2 < w->radius * w->radius) {
            ratio = 1.0f - FSqrt(d2) * w->invRadius;
            ratio = ratio < 0.0f ? 0.0f : ratio;

            GetRootQuaternion(quat, self);
            w->frame = (int)stage_PlayBgAnimationDissolve(
                objAction[objLayout[self->labelId].action].baseMode, pos, quat, (float)w->frame,
                ratio);
            return;
        }
    }
    w->frame = 0;
}
