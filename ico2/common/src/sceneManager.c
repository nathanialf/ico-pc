#include "DObj.h"
#include "GobjProc.h"
#include "debug.h"
#include "gamesys.h"
#include "obj_manager.h"
#include "act-game.h"
#include "fieldCollision.h"
#include "way_tool.h"
#include "brain.h"
#include "camera-root.h"
#include "fightSound.h"
#include "GsBase.h"
#include "Light.h"
#include "clipCollisionManager.h"
#include "waySystemManager.h"
#include "Matrix.h"
#include "gobj.h"
#include "enemy_act.h"
#include "gobj_process.h"
#include <assert.h>
#include "main.h"
#include "generator.h"
#include "ebrain.h"
#include "act.h"
#include "debug_exception.h"
#include "Texture.h"

/* .sbss: the three frame counts
   GetStageStartInfo hands back, which boyact's stage-entry action waits out in
   turn (before the motion, during it, after it). */
static int stageStartWait1; /* derived name */

static int stageStartWait2; /* derived name */

static int stageStartWait3; /* derived name */

/* .sdata: set while MoveNextStage_Set's request stands, and the stage it is for. */
static char nextStageSet = 0; /* derived name */

static int nextStageNo = -1; /* derived name */

/* .bss: the position and rotation MoveNextStage_Set keeps for the next
   stage and MoveNextStage_Get restores, as VU0 vectors. */
static sceVu0FVECTOR nextStagePos; /* derived name */

static sceVu0FVECTOR nextStageRot; /* derived name */

#include "sceneManager.h"
#include "backStage.h"
#include "typedef.h"

/* port/game/options.h: the model viewer is up (package MV) */
extern int ico_mv_active;

/* .data, owned by sceneManager.o: the default layout a scene object is
   created with, at the origin, unrotated, at unit scale. */
SObjSimpleSetting InitialSObjSimpleSetting = {
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    {1.0f, 1.0f, 1.0f, 1.0f},
    0,
};

inline void MoveNextStage_Set(float *pos, float *rot, int wait1, int wait2, int wait3, int stage)
{
    nextStagePos[0] = pos[0];
    nextStagePos[1] = pos[1];
    nextStagePos[2] = pos[2];
    stageStartWait1 = wait1;
    stageStartWait2 = wait2;
    stageStartWait3 = wait3;
    nextStageNo = stage;
    nextStageRot[0] = rot[0];
    nextStageRot[1] = rot[1];
    nextStageRot[2] = rot[2];
    nextStageSet = 1;
}

inline void test_nextstage_firstwalk_set(int unused, int wait1, int wait2, int wait3)
{
    stageStartWait1 = wait1;
    stageStartWait2 = wait2;
    stageStartWait3 = wait3;
}

inline int GetStageStartInfo(GObj *self, int a1, int a2, int *wait1, int *wait2, int *wait3)
{
    int ret = 1;
    if (exit_no == 0) {
        *wait3 = 1;
        *wait2 = 1;
        *wait1 = 1;
    } else {
        *wait1 = stageStartWait1;
        *wait2 = stageStartWait2;
        *wait3 = stageStartWait3;
        if (*wait2 == 0)
            ret = 0;
        if (*wait1 == 0)
            *wait1 = 1;
        if (*wait2 == 0)
            *wait2 = 1;
        if (*wait3 == 0)
            *wait3 = 1;
    }
    *wait2 = 0x32;
    return ret;
}

inline void ChangeStageStartInfo(int a0, int a1, int wait1, int wait2, int wait3)
{
    if (wait1 >= 0) {
        stageStartWait1 = wait1;
    }
    if (wait2 >= 0) {
        stageStartWait2 = wait2;
    }
    if (wait3 >= 0) {
        stageStartWait3 = wait3;
    }
}

inline void MoveNextStage_Clear(void)
{
    nextStageSet = 0;
    nextStageNo = -1;
}

static int GetRealModelId(int stageNo, char *gen)
{
    int mdl;
    int first;
    int count;
    int r;

    if (*(unsigned char *)(gen + 0x46) == 4) {
        switch (GetEnemyType(*(float *)gen, *(float *)(gen + 4), *(float *)(gen + 8))) {
        case 0:
            mdl = stageData[stageNo].mdl[2];
            break;
        case 1:
            mdl = stageData[stageNo].mdl[3];
            break;
        case 2:
            mdl = stageData[stageNo].mdl[1];
            break;
        case 3:
            mdl = stageData[stageNo].mdl[0];
            break;
        default:
            goto plain;
        }
        count = enemymodelGroup[mdl].last - enemymodelGroup[mdl].first;
        first = enemymodelGroup[mdl].first;
        if (count != 0) {
            r = (int)(_GetRandom() * 10.0f);
            return enemymodelTable[first + r % count];
        }
    }
plain:
    return *(int *)(gen + 0x2C);
}

/* GlobalStageSetting is the system's StageSetting record (typedef.h).  The
   stage-preset record is read through the stageData[stage] subscript on every
   line. */
void InitStageLight(int stage)
{
    int i;

    /* using the Excel data for the stage information */
    debug_StdPrintfDummy("ステージ情報にエクセルのデータを使用します.\n");

    for (i = 0; i < 3; i++) {
        GlobalStageSetting.flatLightDir[0][i] = -stageData[stage].flatLightDir[i];

        GlobalStageSetting.flatLightCol[0][i] = stageData[stage].flatLightCol[i] * 0.0078125f;
    }

    _NormalizeVector(GlobalStageSetting.flatLightDir[0], GlobalStageSetting.flatLightDir[0]);

    for (i = 0; i < 3; i++) {
        GlobalStageSetting.flatLightDir[1][i] = GlobalStageSetting.flatLightDir[2][i] =
            stageData[stage].flatLightDir[i];

        GlobalStageSetting.flatLightCol[1][i] = GlobalStageSetting.flatLightCol[2][i] =
            stageData[stage].flatLightCol[i] * 0.0078125f * 0.25f;
    }

    _NormalizeVector(GlobalStageSetting.flatLightDir[1], GlobalStageSetting.flatLightDir[1]);

    _NormalizeVector(GlobalStageSetting.flatLightDir[2], GlobalStageSetting.flatLightDir[2]);

    for (i = 0; i < 3; i++) {
        GlobalStageSetting.ambientCol[i] = stageData[stage].ambientCol[i] * 0.0078125f;

        GlobalStageSetting.bgCol[i] = stageData[stage].bgCol[i];
    }
    GlobalStageSetting.ambientCol[3] = GlobalStageSetting.bgCol[3] = 1.0f;

    light_AddLight(0, 0, 0);

    GlobalStageSetting.fogOn = (int)stageData[stage].fog[0];
    GlobalStageSetting.fogColR = (int)stageData[stage].fog[1];
    GlobalStageSetting.fogColG = (int)stageData[stage].fog[2];
    GlobalStageSetting.fogColB = (int)stageData[stage].fog[3];
    GlobalStageSetting.fogColA = (int)stageData[stage].fog[4];
    GlobalStageSetting.fogOffsetA = (int)stageData[stage].fog[5];
    GlobalStageSetting.fogNear = (int)stageData[stage].fog[6];
    GlobalStageSetting.fogFar = (int)stageData[stage].fog[7];
    GlobalStageSetting.fogStrength = 128;

    gsb_SetBGColor(&db, (int)GlobalStageSetting.bgCol[0], (int)GlobalStageSetting.bgCol[1],
                   (int)GlobalStageSetting.bgCol[2]);

    GlobalStageSetting.shadowDepth = stageData[stage].shadowDepth;
    GlobalStageSetting.shadowBlend[0] = 0;
    GlobalStageSetting.shadowBlend[1] = 40;
    GlobalStageSetting.shadowBlend[2] = 80;
    GlobalStageSetting.shadowBlend[3] = 120;
    GlobalStageSetting.shadowColR = 0;
    GlobalStageSetting.shadowColG = 0;
    GlobalStageSetting.shadowColB = 0;

    GlobalStageSetting.reductionCol[0] = 128;
    GlobalStageSetting.reductionCol[1] = 128;
    GlobalStageSetting.reductionCol[2] = 128;

    GlobalStageSetting.viewScale = 100;

    GlobalStageSetting.postEffect = 0;
    GlobalStageSetting.feedbackEffect = 2;
    GlobalStageSetting.depthFieldStart = 100;
    GlobalStageSetting.depthFieldWidth = 500;
    GlobalStageSetting.motionBlur = 32;

    GlobalStageSetting.feedbackCol[0] = 64;
    GlobalStageSetting.feedbackCol[1] = 64;
    GlobalStageSetting.feedbackCol[2] = 64;
    GlobalStageSetting.feedbackCol[3] = 128;

    GlobalStageSetting.antiLevel0 = 24;
    GlobalStageSetting.antiLevel1 = 24;

    for (i = 0; i < 4; i++) {
        int *row = (int *)&GlobalStageSetting + i * 4;

        row[0x130 / 4] = 128;
        row[0x134 / 4] = 128;
        row[0x138 / 4] = 128;
        row[0x13C / 4] = (i + 1) * 8;
        GlobalStageSetting.subMotionBlur[i] = 32;
        GlobalStageSetting.antiLevel[i].a = 0;
        GlobalStageSetting.antiLevel[i].b = 0;
    }

    GlobalStageSetting.grainScale = 3.0f;

    GlobalStageSetting.handCameraLimitP = 120;
    GlobalStageSetting.handCameraLimitV = 80;
    GlobalStageSetting.zoomMaxInDemo = 200;

    tex_RemakeRegistersSampleMin(0);
}

inline GObj *CreateLayoutedGObj(int id, int model, int accessary, int light, void *lay, int label,
                                int key, int useStart)
{
    ObjKindEnt *layout = &objKindData[id];
    GObj *gobj = CreateGObj(layout, id, label, key, useStart);
    Sub15C *dobj = CSVSYSTEM_InitDObj(model, lay);
    void *(*fn)(GObj *, void *);

    /* the 0x15C slot is the int handle GOBJ_SUB reads (typedef.h) */
    gobj->dobj = dobj;
    dobj->accessary = accessary;

    light_AddLight(gobj, light, 1);

    fn = layout->create;
    if (fn != 0) {
        GOBJ_SUB(gobj)->work = fn(gobj, lay);
    }
    return gobj;
}

typedef union { /* field names derived */
    long long flag;
    GamesysObjInfo info;
} GamesysObjInfoFlag; /* derived name */

/* restores the position the previous stage stored through MoveNextStage_Set;
   named after its siblings MoveNextStage_Set and MoveNextStage_Clear */
static inline void MoveNextStage_Get(SObjSimpleSetting *a, int kind) /* derived name */
{
    if (stage_no == nextStageNo && kind == 1) {
        a->pos[0] = nextStagePos[0];
        a->pos[1] = nextStagePos[1];
        a->pos[2] = nextStagePos[2];
        a->rot[1] = nextStageRot[1] * 3.1415927f / 180.0f;
    }
}

static void initSceneGObj(int stage, int no)
{
    SObjSimpleSetting a;
    GenGeo *gen = &objLayout[no];
    ObjKindEnt *lay = &objKindData[gen->kind];
    GamesysObjInfo *info = gamesysObjInfoGet(gen->kind, no);
    int mdl = gen->mdl;
    int st;
    GObj *gobj;
    float ry;
    int sno;
    long long pri;
    unsigned short fld;

    if (info != 0) {
        sno = info->stage;
        if (sno != stage) {
            return;
        }

        ((GamesysObjInfoFlag *)info)->flag |= 1;

        if (stageData[sno].flag1 == 1 && gamesysGirlStageGet() != sno) {
            switch (gen->kind) {
            case 4:
                ReturnEnemyToGenerator(no);
            case 15:
            case 33:
                gamesysObjInfoCls(gen->kind, no);
                info = 0;
                break;
            }
        }
    }

    debug_StdPrintfDummy("try layout index=[%d] model_id=[%d]------------\n", no, mdl);

    st = 0;

    if (lay->layouted != 0) {
        /* one statement: a constructor built in a temporary and assigned
           (the construct ico2/ito/src/lightning.c uses for its
           LightningVtx) */
        a = (SObjSimpleSetting){
            {-gen->pos[0], -gen->pos[1], -gen->pos[2], 1.0f},
            {gen->rot[0] * 3.1415927f / 180.0f, 0.0f, gen->rot[2] * 3.1415927f / 180.0f, 0.0f},
            {gen->scale[0], gen->scale[1], gen->scale[2], 1.0f},
            gen->initArg};

        ry = gen->rot[1];
        if (ry > 180.0f) {
            ry -= 360.0f;
        }
        if (gen->rot[1] < -180.0f) {
            ry += 360.0f;
        }
        a.rot[1] = ry * 3.1415927f / 180.0f;

        if (info != 0) {
            if (lay->infoInit != 0) {
                lay->infoInit(&a, info);
            } else {
                a.pos[0] = info->pos[0];
                a.pos[1] = info->pos[1];
                a.pos[2] = info->pos[2];
                a.rot[0] = info->rot[0];
                a.rot[1] = info->rot[1];
                a.rot[2] = info->rot[2];
                st = info->work[0];
            }
        }

        MoveNextStage_Get(&a, gen->kind);

        gobj = CreateLayoutedGObj(gen->kind, mdl, gen->accessary, gen->light & 0x1F, &a, no,
                                  (gen->flags >> 14) & 7, 0);

        fld = gen->procPri;
        pri = 0x1800;
        if (fld != 0) {
            pri = (long long)fld << 10;
        }

        if (gen->proc != 0) {
            /* PC port (package MV): the model viewer's host stage starts no
               script; its objects stay as they were loaded */
            if (ico_mv_active == 0 || stage == 1) {
                isysGObjProcAddS(gobj, gen->proc, 0, 0x13, pri);
            }
        } else if (lay->start != 0) {
            isysGObjProcAddS(gobj, lay->start, 0, 0x13, pri);
        }

        if (gen->kind == 1) {
            boyGObj = gobj;
        }
        if (gen->kind == 2) {
            girlGObj = gobj;
        }

        if (info != 0 && lay->infoLoad != 0) {
            lay->infoLoad(gobj, info);
        }

        if (st == 4) {
            backStageGirlTargetEnemyGop = gobj;
        }

        if (gen->outGObj != 0) {
            *gen->outGObj = (char *)gobj;
        }

        brainStatusDefaultSet(&brainGirl, gobj, no);

        eBrainStatusSet(gobj, gen->kind);

        ActSetStartBrainStatus(gobj, st);
    }

    MakeCollisionDependGObjList();
}

static void initParentLink(int id)
{
    GenGeo *gen = &objLayout[id];
    int parentId = gen->parent;
    ObjKindEnt *lay = &objKindData[gen->kind];
    ICO_WORD_PTR(GObj *) self;
    ICO_WORD_PTR(GObj *) parent;

    if (lay->layouted != 0 && parentId != 0 && gen->kind != 4) {
        self = (ICO_WORD_PTR(GObj *))isysGObjSearchFromObjLayoutID(id);
        parent = (ICO_WORD_PTR(GObj *))isysGObjSearchFromObjLayoutID(parentId);
        if (parent != 0) {
            if (parent == self) {
                /* tried to make "%s" a parent-child link, but it is trying to be its own
                   parent */
                debug_StdPrintfDummy(
                    "\"%s\"の親子関係づけをしようとしましたが、自分を親にしようとしています。\n",
                    lay);
                debug_assert(__FILE__, 502);
                __assert(__FILE__, 502, "0");
            }
            debug_StdPrintfDummy("Parentize \"%s\"\n", lay);
            GOBJ_SUB(self)->parent.obj = parent;
            GOBJ_SUB(self)->parent.node = 0;
        } else {
            /* tried to make "%s" a parent-child link, but the parent cannot be found */
            debug_StdPrintfDummy("\"%s\"の親子関係づけをしようとしましたが、親が見つかりません。\n",
                                 lay);
            debug_assert(__FILE__, 511);
            __assert(__FILE__, 511, "0");
        }
    }
}

/* three static helpers inlined into InitSceneObjects */

static inline void initSceneGObjRange(int stage, int first, int last) /* derived name */
{
    int i;

    for (i = first; i < last; i++) {
        initSceneGObj(stage, i);
    }

    for (i = first; i < last; i++) {
        initParentLink(i);
    }
}

static inline void setEnemyGeneratorDispFlag(void) /* derived name */
{
    GObj *gobj;

    for (gobj = isysGObjSearchFromObjKindID_begin(4); gobj != 0;
         gobj = isysGObjSearchFromObjKindID_next(gobj)) {
        GenGeo *gen = &objLayout[gobj->labelId];

        gen->flags |= 0x200000;
    }
}

static inline void initGamesysSceneGObjs(int stage) /* derived name */
{
    GamesysObjInfoFlag *p = (GamesysObjInfoFlag *)gameSysObjInfo;
    int i;

    for (i = 0; i <= 181; i++, p++) {
        if (p->info.no != 0 && (p->flag & 1) == 0 && p->info.stage == stage) {
            initSceneGObj(stage, p->info.no);
        }
    }
}

static void initWayData(int stage)
{
    ExtractWayData(stage);
}

void InitSceneObjects(int stage)
{
    GObj *cam;

    ResetGObjProc();
    boyGObj = girlGObj = 0;
    boyPad = 0;
    girlPad = 0;
    gameover_flag = 0;
    gameover_layout_flag = 0;
    itemWatchOff = 0;

    debug_StdPrintfDummy("[\033[42m scene %d \033[m ]\n", stage);

    gsb_SetZoom(1.0f, 1000.0f);
    brainInit();
    ACTGameView_Init();

    gamesysObjInfoStageInitFlagCls();

    backStageGirlTargetEnemyGop = 0;
    fightSoundProcessRequestStart();

    CreateClipCollisionManagerGObj();

    CreateWaySystemManagerGObj();

    cam = InitCameraGObjs(stage, 0, 1);

    initSceneGObjRange(stage, 2, 6);
    initSceneGObjRange(stage, stageData[stage].labelTop, stageData[stage].labelEnd);
    initGamesysSceneGObjs(stage);

    if (girlGObj != 0) {
        isysGObjMoveAfterGObj(girlGObj, cam);
    }
    if (boyGObj != 0) {
        isysGObjMoveAfterGObj(boyGObj, cam);
    }

    setEnemyGeneratorDispFlag();

    InitCamera();

    initWayData(stage);

    MakeExitAttributeIndex();
}

int HotInitSceneObjects(int a0)
{
    GObj *node = isysGObjGetExist_begin();
    if (node != 0) {
        do {
            int idx = ((GObj *)node)->kind;
            if (idx >= 0) {
                ObjKindEnt *e = &objKindData[idx];
                void (*fn)(GObj *);
                if (e->before != 0) {
                    iosOmSendMail(node, 0x2F, node);
                }
                fn = e->hotInit;
                if (fn != 0) {
                    fn(node);
                }
            }
            node = isysGObjGetExist_next(node);
        } while (node != 0);
    }
    return 1;
}
