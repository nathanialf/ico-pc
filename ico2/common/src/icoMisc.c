#include "debug.h"
#include "memory.h"
#include "fieldCollision.h"
#include "Light.h"
#include "StageAnimation.h"
#include "darkVolume.h"
#include "lineManager.h"
#include "matrixDrive.h"
#include "motionManager.h"
#include "motionManager2.h"
#include "particleEffect.h"
#include "spiderGroupManager.h"
#include "stageMultiBgaManager.h"
#include "icoMisc.h"
#include "gamesys.h"
#include "s_init.h"
#include "script.h"
#include <stdio.h>
#include "GsBase.h"
#include "ios.h"
#include "layout_texture.h"
#include "kanban.h"
#include "kanbanBoot.h"
#include "Matrix.h"
#include "cdvd.h"
#include "sceneManager.h"
#include "charFileManager.h"
#include "access.h"
#include "warpGirl.h"
#include "jimaku.h"
#include "streamMotionManager.h"
#include "motionFileManager.h"
#include "way_llf.h"
#include "waterDot.h"
#include "windManager.h"
#include "objact.h"
#include "soundManager.h"
#include "pad.h"
#include "thread.h"
#include "obj_manager.h"
#include "camera-root.h"
#include "act-game.h"
#include "main.h"
#include "windField.h"
#include "debug_exception.h"
#include "fightSound.h"
#include "ebrain.h"
#include "EnemyInit.h"
#include "ZFog.h"
#include "flyManager.h"
#include "staticBlur.h"
#include <assert.h>

static void disp_memory_partition_bar(void);
/* debug.h leaves it out (see there) */
extern int debug_bar_flag;

/* .sdata: the partition bar's backdrop tint; ExecIcoMisc's three state
   words; InitIcoMisc's four; the six debug words dbgC0..dbgC5 (declared in
   icoMisc.h). */
typedef struct { /* field names derived */
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} MiscColor; /* derived name */

static MiscColor partitionBarTint = {255, 255, 255, 128}; /* derived name */

/* .data: the partition bar's two line colours and the wind-field line colour,
   RGBA as Draw2DLine and DrawLineG take them. */
static int partitionFreeColor[4] = {255, 128, 64, 128}; /* derived name */

static int partitionUsedColor[4] = {64, 128, 255, 128}; /* derived name */

/* GifPacket.h's entry points, which this TU does not include: its call
   passes gif_MakeSpriteNoTexture's z as a 32-bit unsigned int, where the
   header takes a long long */
extern void gif_StartPacketPri(int pri);
extern void gif_SetAlpha(long long alpha, long long mode, long long fix);
extern void gif_EndPacket(void);

/* ICO_HOST: GifPacket.c's parameter types, so arguments land where the
   definition reads them on hosts that pass them on the stack
   (layout_texture.c says more) */
#ifdef ICO_HOST

extern void gif_MakeSpriteNoTexture(int x, int y, int w, int h, long long z, MiscColor *col,
                                    int prim);

#else

extern void gif_MakeSpriteNoTexture(int x, int y, int w, int h, unsigned int z, MiscColor *col,
                                    int prim);

#endif

/* .bss: the line buffer the memory report is printed through */
static char printBuf[128]; /* derived name */

inline void ExitIcoMisc(void) {}

/* two-dimensional screen position handed to Draw2DLine */
typedef struct { /* field names derived */
    int x;
    int y;
    int z;
    int w;
} D2Pos; /* derived name */

/* The partition bar's TTY trace, built only when DEBUG is defined; the
   retail build does not define it, so the helper has no body there (the
   trace text derived). */
static __inline__ void partitionBarDebugDisp(void) /* derived name */
{
#ifdef DEBUG
    scePrintf("disp_memory_partition_bar\n");
#endif
}

static void disp_memory_partition_bar(void)
{
    IosMemPart *parts[5] = {ios_partition_isys, ios_partition_s2motion, ios_partition_smotion,
                            ios_partition_common, 0};
    D2Pos st;
    D2Pos ed;
    char buf[1024];
    IosMemPart *p;
    IosMemNode *e;
    int i = 0;
    int j;
    int k;
    int size;
    int total;
    int used;
    int max;
    int x;

    if (fbKeep != 0) {
        return;
    }
    max = 0;
    gif_StartPacketPri(12);
    for (; parts[i] != 0; i++) {
        p = parts[i];
        size = p->end - p->start + 0x10;
        if (max < size) {
            max = size;
        }
    }
    st.z = ed.z = 0;
    st.w = ed.w = 0;
    st.y = (ScreenHeight / 2 + 1898) << 4;
    ed.y = st.y + 1280;
    if (debug_bar_flag == 2) {
        gif_SetAlpha(1, 2, 32);
        gif_MakeSpriteNoTexture((-(ScreenWidth >> 1) + 2178) << 4, (ScreenHeight / 2 + 1898) << 4,
                                (ScreenWidth - 200) << 4, 768, 0xFFFFFFFF, &partitionBarTint, 1);
    }
    gif_SetAlpha(1, 2, 112);
    for (j = 0, k = 0; j < max; j += 0x100000) {
        st.x = ed.x = (int)(((float)(-(ScreenWidth >> 1) + 130) + 2048.0f) * 16.0f +
                            (float)j * (float)(ScreenWidth - 200) / (float)max * 16.0f);
        if (k % 10) {
            Draw2DLine((int *)&st, (int *)&ed, partitionUsedColor, -1);
        } else {
            Draw2DLine((int *)&st, (int *)&ed, partitionFreeColor, -1);
        }
        k++;
    }
    for (i = 0; parts[i] != 0; i++) {
        used = 0;
        p = parts[i];
        e = p->head;
        total = p->end - p->start + 0x10;
        if (e != 0) {
            do {
                used += e->size << 4;
                e = e->free_next;
#ifdef ICO_HOST
                if (e != 0 && !ico_arena_contains(e, 0)) {
#else
                if ((unsigned int)e > 0x1FEFFF0) {
#endif
                    sprintf(
                        buf,
                        "DISP_MEMORY_PARTITION_BAR():\n\tINVALID MEMORY FREE AREA INDICATED IN PARTITION \"%s\"\n\tMALLOCED MEMORY'S NEXT_FREE: %p\n",
                        p->name, e);
                    debug_assertMessage(__FILE__, 609, buf);
                    __assert(__FILE__, 609, "e");
                }
            } while (e != 0);
        }
        gif_SetAlpha(0, 2, 112);
        gif_SetAlpha(1, 2, 112);
        st.y = ed.y = (ScreenHeight / 2 + i * 18 + 1907) << 4;
        st.x = (int)(((float)(-(ScreenWidth >> 1) + 130) + 2048.0f) * 16.0f);
        ed.x = (int)((float)st.x +
                     (float)(total - used) * (float)(ScreenWidth - 200) / (float)max * 16.0f);
        Draw2DLine((int *)&st, (int *)&ed, partitionFreeColor, -1);
        x = (int)(((float)(-(ScreenWidth >> 1) + 130) + 2048.0f) * 16.0f);
        st.x = (int)((float)x + (float)total * (float)(ScreenWidth - 200) / (float)max * 16.0f);
        ed.x = (int)((float)x +
                     (float)(total - used) * (float)(ScreenWidth - 200) / (float)max * 16.0f);
        Draw2DLine((int *)&st, (int *)&ed, partitionUsedColor, -1);
    }
    gif_SetAlpha(1, 4, 128);
    partitionBarDebugDisp();
    /* a draw in white, compiled out of the retail build (the call derived) */
    if (0) {
        Draw2DLine((int *)&st, (int *)&ed, (sceVu0IVECTOR){255, 255, 255, 128}, -1);
    }
    gif_EndPacket();
    for (i = 0; parts[i] != 0; i++) {
        p = parts[i];
        debug_PrintfDummy(ScreenWidth / 2 - (ScreenWidth >> 1) + 30,
                          (ScreenHeight / 2 - 150 + ScreenHeight / 2 + i * 18) / 2, 0xFFFFFF80,
                          "%10s", p->name);
    }
}

static void disp_memory_partition(void)
{
    IosMemPart *p;
    int y = 0x70;
    debug_PrintfDummy(24, 100, 0xFFFFFF00, "partition             total free/all      max free");
    iosMallocCheckLeak(ios_partition_root);
    p = ios_partition_root->child;
    if (p != 0) {
        do {
            unsigned int sum = 0;
            unsigned int max = 0;
            IosMemNode *e;
            int diff;
            iosMallocCheckLeak(p);
            e = p->head;
            if (e != 0) {
                do {
                    unsigned int v = e->size << 4;
                    if (max < v) {
                        max = v;
                    }
                    sum += v;
                    e = e->free_next;
                } while (e != 0);
            }
            diff = p->end - p->start + 0x10;
            sprintf(printBuf, "%8s%8x: %8x/%8x %x", p->name, p, sum, diff, max);
            debug_PrintfDummy(100, y, 0xFFFFFF00, printBuf);
            y += 8;
            p = p->next;
        } while (p != 0);
    }
}

static void *prevCameraPos = 0; /* derived name */

static int seEnvMute = 0; /* derived name */

static int diskErrorBlink = 0; /* derived name */

void ExecIcoMisc(void)
{
    int total;
    int used;
    void *cam;

    if (debug_mem_partition_flag == 1) {
        disp_memory_partition();
    }
    if (debug_seslotdisp_flag == 1) {
        debug_SESlotDisp();
    }
    if (iosCdvdDiskStatusGet() != 0) {
        if (diskErrorBlink++ < 15) {
            debug_PrintfDummy(250, 100, 0xFF000000, "DISK ERROR");
        } else if (diskErrorBlink >= 31) {
            diskErrorBlink = 0;
        }
    }
    if (iopBuffOver != 0) {
        debug_PrintfDummy(250, 100, 0xFF000000, "IOP BUFF OVER -%d bytes", iopBuffOver);
    }
    iosOmGetGObjStatus(&total, &used);
    if (debug_font_flag2 != 0 || (debug_font_flag & 1) != 0) {
        debug_Printf(470, 10, (used * 100 / total > 90) ? 0xFF300080 : 0xC0FF80, "GObj %d/%d", used,
                     total);
    }
    if (debug_memory_bar != 0) {
        disp_memory_partition_bar();
    }
    if (graphics_ready == 0) {
        if (iosCdvdDiskStatusGet() == 0) {
            exec_layout_texture();
        }
    }
    if (kanbanCommonRead != 0) {
        if (stage_no == 1) {
            kanbanBootMain();
        }
    }
    kanbanExec();
    if (graphics_ready != 0) {
        return;
    }
    if (systemStatus[5] == 0) {
        ExecParticleEffects();
        ExecStreamMotionManager();
        ExecWindManager();
        soundSeEnvMasterVolRate = scpSeEnvMasterVolRate;
        ExecSpiderGroupManager();
        ExecGameOverEffect();
        seEnvMute = 0;
    } else {
        if (current_layout_id == 28) {
            seEnvMute = 1;
        }
        if (seEnvMute != 0) {
            soundSeEnvMasterVolRate = 0.0f;
        } else if (scpSeEnvMasterVolRate > 0.5f) {
            soundSeEnvMasterVolRate = 0.5f;
        }
    }
    if (systemStatus[6] == 0) {
        gamesysBackStageProcess();
        if (debug_wallcheck_flag == 1) {
            warpGirlOutStage(stage_no, 1);
        }
        cam = GetCameraPos();
        if (prevCameraPos == 0) {
            if (cam != 0) {
                soundSeEnvPlay();
            }
        }
        prevCameraPos = cam;
        if (cam != 0) {
            soundReqTickProc();
            scpGirlHintVoiceTickProc(cam);
        }
        fightSoundProcess();
    }
    eBrainProcess();
    if (fall_death_active != 0) {
        if (boyGObj != 0) {
            if (GOBJ_SUB(boyGObj)->ctrl.fallHeight > 1000.0f) {
                lt_switch_layout(62);
            }
        }
        if (girlGObj != 0) {
            if (GOBJ_SUB(girlGObj)->ctrl.fallHeight > 1000.0f) {
                lt_switch_layout(62);
            }
        }
    }
    jimakuDisp(&jimaku_msg);
}

/* per-scene preset: twelve (time, effect id) pairs then the scene label at 0xC0 */
typedef struct { /* field names derived */

    struct {
        float t;
        int id;
    } ent[12];

    char pad60[0xC0 - 0x60];
    char name[0x194 - 0xC0];
} ScnPre; /* derived name */

/* particle-effect entry: package id at 0x18, attribute word at 0x20 whose
   bit 0 marks the entry as already handled */
typedef struct { /* field names derived */
    char pad0[24];
    int pkg; /* 0x18 */
    char pad1C[4];
    unsigned int done : 1; /* 0x20 */
    unsigned int pad21 : 31;
} EffEnt; /* derived name */

/* motionOrientManager.h carries MotionDef and declares no motionKind */
extern const ScnPre motionKind[];
/* the effect table, read-only here and read as EffEnt;
   frameDependSequence.h declares EffEntry */
extern const EffEnt motionEffKind[];

static unsigned char setActorsDebugPending = 1; /* derived name */

static int charFileManagerReady = 0; /* derived name */

static int commonPackLoaded = 0; /* derived name */

static int loadedMotionSeg = -1; /* derived name */

/* .sbss: the frame stamp the load-time report below prints */
static int load_time; /* derived name */

void InitIcoMisc(int *arg)
{
    int stage = *arg;
    int i;
    int j;
    int id;
    int found;
    int pack;
    char *fname;

    debugMoveMode = 0;
    motionFrameUpdate = 1;

    InitializeStaticBlur();
    InitStreamMotionManager();
    InitSpiderGroupManager();
    InitFlyManager();

    if (systemStatus[3] != 0 && systemStatus[4] != 0) {
        gamesysMemoryLoad(gameSysMemoryFuncList, gameSysMainSaveBuff, 0);
        scpBoyControlReadDisable = 0;
        systemStatus[4] = 0;
    }
    if (thisIsYourStartStage != 1 && setActorsDebugPending != 0) {
        ACTGame_SetActors_Debug(stage, 0);
        setActorsDebugPending = 0;
    }
    debug_StdPrintfDummy("Init Object Light\n");
    light_InitLight();
    InitStageLight(stage);
    enemy_Initialize();
    debug_StdPrintfDummy("Init Packing Data\n");
    load_time = frame_count;
    if (charFileManagerReady == 0) {
        InitCharFileManager();
    } else {
        ResetCharFileManager();
    }
    charFileManagerReady = 1;
    debug_StdPrintfDummy("InitCharFIleManager out\n");
    pack = LoadFileType;
    if (commonPackLoaded == 0) {
        debugCdvdLoadInfoSegInit(0);
        iosCdvdLoadPackFile(pack, GetDataFileName(-1, pack), 0);
        kanbanInit(0);
        kanbanBootInit();
        commonPackLoaded = 1;
    }
    if (stage_no == 1) {
        kanbanBootStart();
    }
    if (loadedMotionSeg != stageData[stage].mot) {
        /* case 1 shares the default arm */
        switch (stageData[stage].mot) {
        case 1:
        default:
            fname = GetDataFileName2("MOTION1", pack);
            break;
        case 2:
            fname = GetDataFileName2("MOTION2", pack);
            break;
        case 3:
            fname = GetDataFileName2("MOTION3", pack);
            break;
        }
        if (loadedMotionSeg != -1) {
            /* "this stage uses a different motion segment from the previous one" */
            debug_StdPrintfDummy(
                "\033[33mこのステージは前のステージと異なるモーションセグメントを使用します。\033[m\n");
            ResetStatic2MotionManager(loadedMotionSeg);
        }
        iosMallocResetPartition(ios_partition_s2motion);
        iosCdvdLoadPackFile(pack, fname, 0);
        loadedMotionSeg = stageData[stage].mot;
    }
    debug_StdPrintfDummy("iosCdvdLoadPackFile\n");
    debugCdvdLoadInfoSegInit(1);
    iosCdvdLoadPackFile(pack, GetDataFileName(stage, pack), 1);
    load_time = frame_count - load_time;
    debug_StdPrintfDummy("InitWayPointSystem\n");
    InitWayPointSystem();
    debug_StdPrintfDummy("MakeFogClut\n");
    fog_MakeFogClut();
    debug_StdPrintfDummy("InitParticleEffects\n");
    InitParticleEffects();
    InitStageMultiBgaManager();
    InitializeWaterDot();
    debug_StdPrintfDummy("InitSceneObjects( %d )\n", stage);
    InitSceneObjects(stage);
    InitWindManager(stage);
    debug_StdPrintfDummy("%s\n", stageData[stage_no].name);
    init_layout_texture(stage);

    found = 0;
    for (i = 0; i < 1145; i++) {
        for (j = 0; j < 12; j++) {
            id = motionKind[i].ent[j].id;
            if (id > 0xFFFF) {
                continue;
            }
            if (motionEffKind[id].done) {
                continue;
            }
            if (id == 24) {
                continue;
            }
            if (GetParticleEffectPackage(motionEffKind[id].pkg)[1] != 1) {
                continue;
            }
            /* "the particle %s that %s calls is emitted forever" */
            debug_StdPrintfDummy(
                "\"\033[33m%s\033[m\"が呼ぶパーティクル\"\033[33m%s\033[m\"は永久発生です\n",
                motionKind[i].name, particleEffectFile[motionEffKind[id].pkg].name);
            found = 1;
        }
    }
    if (found != 0) {
        /* "it runs, but left going it eats up memory: change the data to one
           that does not loop" */
        debug_StdPrintfDummy(
            "\033[36m動作はさせますが続けているとメモリの\n資源を食いつくします\nループではないものにデータを修正してください\033[m\n");
    }
    debug_StdPrintfDummy("Init Stage Animation\n");
    gsb_ResetFilmNoise();
    stage_Init();
    debug_StdPrintfDummy("Init Object Action\n");
    ObjAction_Init();
    InitStageChange();
    graphics_ready = 0;
    debug_StdPrintfDummy("load time %f sec\n", load_time / 60.0f);
    sndInit(stage);
    iosPadActInit();
    if (stageData[stage_no].initproc != 0) {
        stageData[stage_no].initproc();
    }
    MakeCollisionDependGObjList();
    gamesysObjInfoStageInitPosSaveUnlock();
    systemStatus[5] = 0;
    systemStatus[6] = 0;
    iosThreadDestroy(0);
}

int dbgC0 = 0;

int dbgC1 = 0;

int dbgC2 = 0;

int dbgC3 = 0;

int dbgC4 = 0;

int dbgC5 = 0;

static int windLineColor[4] = {0, 128, 255, 128}; /* derived name */

void DispIcoMisc(void)
{
    int i = 0;
    int j;

    stage_DispAnimation();
    DispStageMultiBgaManager();
    DispParticleEffects();
    DispGameOverEffect();
    SetSkeltonDispSwitch(debug_skel_flag);
    SetHitCollisionDisplay(debug_wallcheck_flag, debug_wallcheck_flag);
    if (debug_fieldcollision_flag > 0) {
        DrawCollision(debug_fieldcollision_flag == 1 ? 0 : -10);
    }
    if (debug_brain_bar_flag != 0) {
        DispAllSpiderGroups();
    }
    if (debug_skel_flag != 0) {
        MatrixDrive_PushMatrix();
        _UnitMatrix(MatrixDrive_GetMatrix());
        gif_StartPacketPri(11);
        gif_SetAlpha(1, 5, 0x80);
        for (j = -10; j < 20; j++) {
            for (i = -10; i < 10; i++) {
                float pos[4] = {j * 100.0f, -10.0f, i * 100.0f, 1.0f};
                float tip[4];

                _ScaleVectorXYZ(tip, GetWindVector(0, pos), 10.0f);
                _AddVectorXYZ(tip, pos, tip);
                DrawLineG(tip, windLineColor, pos, windLineColor, 0);
            }
        }
        gif_EndPacket();
        MatrixDrive_PopMatrix();
    }
    light_DispVolume();
}
