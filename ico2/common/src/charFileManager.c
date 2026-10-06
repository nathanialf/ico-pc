#include "charFileName.h"
#include "debug.h"
#include "debug_exception.h"
#include "cdvd.h"
#include "memory.h"
#include "shockdriver.h"
#include "adpcm_init.h"
#include "s_init.h"
#include "soundManager.h"
#include "camera-ico2.h"
#include "camera-set-manager.h"
#include "Basic.h"
#include "Light.h"
#include "StageAnimation.h"
#include "motionFileManager.h"
#include "particleEffect.h"
#include "tableSin.h"
#include "DisplayP2O.h"
#include "Shadow.h"
#include "Texture.h"
#include <string.h>
#include "ios.h"
#include <assert.h>
#include "fieldCollision.h"

/* the .cl file's head; its walls are fieldCollision.h's FcWallEnt (0x50
   bytes, the angle at 0x44 and the sine and cosine pair's address at 0x4C) */
typedef FcColl Coll; /* derived name */

typedef struct {        /* field names derived */
    PObjModel *pObj;    /* 0x00 */
    PObjModel *pShadow; /* 0x04 */
    SkelNode *pSkel;    /* 0x08 */
    int skelSum;        /* 0x0C */
    Coll *pColl;        /* 0x10 */
    int state;          /* 0x14 */
} CharFile;             /* derived name */

/* .bss: the character file table, MAX_CHARS entries of 0x18 bytes */
static CharFile charFiles[MAX_CHARS]; /* derived name */

/* .data: the empty entry both initialisers copy over
   every slot of the table. */
static CharFile charFileEmpty = {0, 0, 0, 0, 0, 1}; /* derived name */

/* .sdata: the serial the loaders stamp into each object they build, and the
   semi-common sound header's buffer (defined below). */
static int objSerial = 0; /* derived name */

#include "charFileManager.h"
#include <stdio.h>
#include "main.h"

/* declared here until enemy.c's call stores the pointer without its int
   view; then charFileManager.h declares it */
PObjModel *GetPObjAddress(int id);

inline PObjModel *GetPObjAddress(int id)
{
    return charFiles[id].pObj;
}

void InitCharFileManager(void)
{
    int i;

    objSerial = 0;
    for (i = 0; i < MAX_CHARS; i++) {
        charFiles[i] = charFileEmpty;
    }
    InitPluralCameraSet();
    InitCameraSetManager();
}

void ResetCharFileManager(void)
{
    int i;

    objSerial = 0;
    for (i = 0; i < MAX_CHARS; i++) {
        if (charFiles[i].state == 1) {
            charFiles[i] = charFileEmpty;
        }
    }
    InitPluralCameraSet();
    InitCameraSetManager();
}

/* PObj.c has no header; the definition is (int, int, int) */
extern PObjModel *InitPObj(void *buf, ICO_WORD name, int id);
/* PObj.c (port): frees the strip and morph tables decoded from the model
   image just freed */
void PObj_FreeImageTables(void);

/* "Illegal Model ID number: %d (\"%s\")\n" / "ReadModelFile:Already loaded. (id:%d)%s\n" / "ReadModelFile:loaded::(id:%d)%s(addr:%p/size:%d)\n" / sprintf above belong to ReadModelFile. */
void ReadModelFile(void *h, ICO_WORD name, int size, int id, int kind, int word08, int part)
{
    char buf[256];
    char *p;

    systemStatus[8]++;
    if (size == 0) {
        return;
    }

    if (id >= MAX_CHARS) {
        sprintf(buf, "Illegal Model ID number: %d (\"%s\")\n", id, name);
        /* please raise MAX_CHARS in commmon/include/charFileName.h */
        debug_StdPrintfDummy("commmon/include/charFileName.hのMAX_CHARSを増やしてください\n");
        debug_assertMessage(__FILE__, 130, buf);
        __assert(__FILE__, 130, "e");
    }

    if (charFiles[id].pObj != 0) {
        debug_StdPrintfDummy("ReadModelFile:Already loaded. (id:%d)%s\n", id, name);
        iosCdvdHandlerRead(h, 0, size);
        return;
    }

    if (part == 0) {
        malloc_SetPartition(0);
    } else {
        malloc_SetPartition(1);
    }

    charFiles[id].state = part;
    p = iosMallocDebug(ios_partition_seki, size, __FILE__, 145);
    iosCdvdHandlerRead(h, p, size);
    debug_StdPrintfDummy("ReadModelFile:loaded::(id:%d)%s(addr:%p/size:%d)\n", id, name, p, size);
    charFiles[id].pObj = InitPObj(p, name, id);
    charFiles[id].pObj->serial = objSerial++;
    iosFree(p);
    PObj_FreeImageTables();
}

void ReadVolumeModelFile(void *h, ICO_WORD name, int size, int id, int kind, int word08, int seg)
{
    char *buf;

    systemStatus[8]++;
    if (size == 0) {
        return;
    }
    if (seg == 0) {
        malloc_SetPartition(0);
    } else {
        malloc_SetPartition(1);
    }
    buf = iosMallocDebug(ios_partition_seki, size, __FILE__, 190);
    if (id >= MAX_CHARS) {
        debug_StdPrintfDummy("Illegal Volume ID number. %d\n", id);
        debug_StdPrintfDummy("commmon/include/charFileName.hのMAX_CHARSを増やしてください\n");
        debug_assert(__FILE__, 195);
        __assert(__FILE__, 195, "0");
    }
    if (charFiles[id].pObj != 0) {
        debug_StdPrintfDummy("ReadVolumeModelFile:Already loaded. (id:%d)%s\n", id, name);
        iosCdvdHandlerRead(h, 0, size);
        return;
    }
    iosCdvdHandlerRead(h, buf, size);
    debug_StdPrintfDummy("ReadVolumeModelFile:loaded::(id:%d)%s(addr:%p/size:%d)\n", id, name, buf,
                         size);
    charFiles[id].pObj = InitPObj(buf, name, id);
    charFiles[id].pObj->serial = objSerial++;
    iosFree(buf);
    PObj_FreeImageTables();
}

/* PObj.c has no header; the definition is (ObjHdr *, char *, int) */
extern PObjModel *AllocPObj(void *buf, ICO_WORD name, int id);

void ReadShadowModelFile(void *h, ICO_WORD name, int size, int id, int kind, int word08, int seg)
{
    char *buf;

    systemStatus[8]++;
    if (size == 0) {
        return;
    }
    if (seg == 0) {
        malloc_SetPartition(0);
    } else {
        malloc_SetPartition(1);
    }
    if (id >= MAX_CHARS) {
        debug_StdPrintfDummy("Illegal Shadow ID number. %d\n", id);
        debug_StdPrintfDummy("commmon/include/charFileName.hのMAX_CHARSを増やしてください\n");
        debug_assert(__FILE__, 235);
        __assert(__FILE__, 235, "0");
    }
    buf = iosMallocDebug(ios_partition_seki, size, __FILE__, 239);
    if (charFiles[id].pShadow != 0) {
        debug_StdPrintfDummy("ReadShadowModelFile:Already loaded. (id:%d)%s\n", id, name);
        iosCdvdHandlerRead(h, 0, size);
        return;
    }
    iosCdvdHandlerRead(h, buf, size);
    debug_StdPrintfDummy("ReadShadowModelFile:loaded::(id:%d)%s(addr:%p/size:%d)\n", id, name, buf,
                         size);
    charFiles[id].pShadow = AllocPObj(buf, name, id);
    charFiles[id].pShadow->serial = objSerial++;
    shadow_MakeObjectData(charFiles[id].pShadow);
    iosFree(buf);
    PObj_FreeImageTables();
}

void ReadTextureFile(void *h, char *name, int size, int id, int kind, int word08, int seg)
{
    int rv = 0;
    char *buf;
    int flag = 0;

    systemStatus[8]++;
    if (seg == 0) {
        malloc_SetPartition(0);
    } else {
        malloc_SetPartition(1);
    }
    buf = iosMallocDebug(ios_partition_seki, size, __FILE__, 276);
    if (size == 0) {
        debug_StdPrintfDummy("ReadTextureFile:texture size is zero.%s\n", name);
        iosCdvdHandlerRead(h, 0, 0);
        return;
    }
    iosCdvdHandlerRead(h, buf, size);
    if (kind == 55 && seg == 1 && texFile[id].cameraMove != 0) {
        if (texFile[id].cameraMove != NonLinearCameraMove) {
            flag = seg;
        }
    }
    if (flag == 0) {
        rv = tex_InitTexture(name, buf);
    }
    debug_StdPrintfDummy("ReadTextureFile:loaded::(%d)%s(addr:%p/size:%d)\n", rv, name, buf, size);
    iosFree(buf);
}

/* sugipon/include/sugiCommon.h: byte checksum helper, inlined at its call site */
static inline int SumBytes(unsigned char *p, int n) /* derived name */
{
    int sum = 0;
    int i;

    for (i = 0; i < n; i++) {
        sum += *p++;
    }
    return sum;
}

void ReadSkeltonFile(void *h, char *name, int size, int id, int kind, int word08, int seg)
{
    SkelNode *p = 0;
    int sum = 0;
    int i;

    systemStatus[8]++;
    if (size == 0) {
        return;
    }
    if (seg == 0) {
        malloc_SetPartition(0);
    } else {
        malloc_SetPartition(1);
    }
    for (i = 0; i < MAX_CHARS; i++) {
        if (modelData[i].path != 0 && strcmp(modelData[i].path, name) == 0) {
            if (p == 0) {
                if (charFiles[i].pSkel != 0) {
                    debug_StdPrintfDummy("ReadSkeltonFile:Already loaded. %s\n", name);
                    iosCdvdHandlerRead(h, 0, size);
                    p = charFiles[i].pSkel;
                    sum = charFiles[i].skelSum;
                } else {
                    int j;

                    p = mallocseki(size);
                    j = 0;
                    iosCdvdHandlerRead(h, p, size);
                    debug_StdPrintfDummy("ReadSkeltonFile:loaded::%s  (size:%d)\n", name, size);
                    while (p[j].mirror != -1) {
                        j++;
                    }
                    charFiles[i].pSkel = p;
                    sum = SumBytes((unsigned char *)p, j * 64);
                    charFiles[i].skelSum = sum;
                }
            } else {
                charFiles[i].pSkel = p;
                charFiles[i].skelSum = sum;
            }
        }
    }
    if (p == 0) {
        debug_StdPrintfDummy("ReadSkeltonFile:Skelton file is not applied. %s\n", name);
        debug_assert(__FILE__, 362);
        __assert(__FILE__, 362, "FALSE");
    }
}

void ReadCollisionFile(void *h, char *name, int size, int id, int kind, int word08, int seg)
{
    int i;
    int j;
    int k;
    Coll *p;
    float *q;

    systemStatus[8]++;
    if (size == 0) {
        return;
    }
    if (seg == 0) {
        malloc_SetPartition(0);
    } else {
        malloc_SetPartition(1);
    }
    for (i = 0; i < MAX_CHARS; i++) {
        if (strcmp(modelData[i].collPath, name) == 0) {
            if (charFiles[i].pColl != 0) {
                debug_StdPrintfDummy("ReadCollisionFile:Already loaded. %s\n", name);
                iosCdvdHandlerRead(h, 0, size);
            } else {
                debug_StdPrintfDummy("collision size:%d\n", size);
                charFiles[i].pColl = (Coll *)mallocseki(size);
                iosCdvdHandlerRead(h, charFiles[i].pColl, size);
                debug_StdPrintfDummy("ReadCollisionFile:loaded::%s  (size:%d)\n", name, size);
                p = charFiles[i].pColl;
                p->wcl = ICO_EEW(p) + p->wcl;
                p->fcl = ICO_EEW(p) + p->fcl;
                p->wblk = ICO_EEW(p) + p->wblk;
                p->fblk = ICO_EEW(p) + p->fblk;
                p->ofs = ICO_EEW(p) + p->ofs;
                debug_StdPrintfDummy("ch      :%p\n", p);
                debug_StdPrintfDummy("ch->wcl :%p\n", p->wcl);
                debug_StdPrintfDummy("ch->fcl :%p\n", p->fcl);
                debug_StdPrintfDummy("ch->wblk:%p\n", p->wblk);
                debug_StdPrintfDummy("ch->fblk:%p\n", p->fblk);
                debug_StdPrintfDummy("ch->ofs :%p\n", p->ofs);
                for (j = 0; j < 32; j++) {
                    for (k = 0; k < 32; k++) {
                        if (ICO_EEPTR(int *, p->wblk)[j * 32 + k] != 0) {
                            debug_StdPrintfDummy("w %2d %2d :%p\n", j, k,
                                                 ICO_EEPTR(int *, p->wblk)[j * 32 + k]);
                            ICO_EEPTR(int *, p->wblk)
                            [j * 32 + k] = ICO_EEW(p) + ICO_EEPTR(int *, p->wblk)[j * 32 + k];
                        }
                        if (ICO_EEPTR(int *, p->fblk)[j * 32 + k] != 0) {
                            debug_StdPrintfDummy("f %2d %2d :%p\n", j, k,
                                                 ICO_EEPTR(int *, p->fblk)[j * 32 + k]);
                            ICO_EEPTR(int *, p->fblk)
                            [j * 32 + k] = ICO_EEW(p) + ICO_EEPTR(int *, p->fblk)[j * 32 + k];
                        }
                    }
                }
                q = (float *)mallocseki(p->count * 8);
                for (j = 0; j < p->count; j++) {
                    FcWallEnt *w = &ICO_EEPTR(FcWallEnt *, p->wcl)[j];

                    w->normal = ICO_EEW(q + j * 2);
                    FC_WALL_NORMAL(w)[0] = GetTableSin(w->angle);
                    FC_WALL_NORMAL(w)[1] = GetTableCos(w->angle);
                }
            }
            return;
        }
    }
    debug_StdPrintfDummy("ReadCollisionFile:Collision file is not applied. %s\n", name);
    debug_assert(__FILE__, 466);
    __assert(__FILE__, 466, "FALSE");
}

void ReadStageAnimationFile(void *h, char *name, int size, int id, int kind, int word08, int seg)
{
    char *buf;

    systemStatus[8]++;
    if (size == 0) {
        return;
    }
    if (seg == 0) {
        malloc_SetPartition(0);
        debug_assert(__FILE__, 487);
        __assert(__FILE__, 487, "0");
    } else {
        malloc_SetPartition(1);
    }
    buf = mallocseki(size);
    iosCdvdHandlerRead(h, buf, size);
    debug_StdPrintfDummy("ReadStageAnimationFile:loaded::[%d]%s (size:%d)\n", id, name, size);
    stage_ApplyData(name, buf);
}

typedef struct { /* field names derived */
    char pad0[308];
    int area; /* 0x134, the motion memory area: 0 static, 4 dynamic, else swap */
    char pad138[92];
} MotEnt; /* 0x194 */ /* derived name */

/* motionOrientManager.h carries MotionDef and declares no motionKind */
extern MotEnt motionKind[];

void ReadMotionFile(void *h, char *name, int size, int id, int kind, int word08, int seg)
{
    systemStatus[8]++;
    if (size == 0) {
        return;
    }
    switch (motionKind[id].area) {
    case 0:
        motionTable[id] = iosMallocDebug(ios_partition_smotion, size, __FILE__, 515);
        break;
    case 4:
        motionTable[id] = iosMallocDebug(ios_partition_dmotion, size, __FILE__, 518);
        if (motionTable[id] == 0) {
            /* not enough memory in the dynamic motion area */
            debug_StdPrintfDummy("ダイナミックモーション領域のメモリが足りません。\n");
            debug_assertMessage(__FILE__, 521, "LACK OF DYNAMIC MOTION MEMORY.\n");
            __assert(__FILE__, 521, "e");
        }
        break;
    default:
        motionTable[id] = iosMallocDebug(ios_partition_s2motion, size, __FILE__, 526);
        if (motionTable[id] == 0) {
            /* not enough memory in the motion swap area */
            debug_StdPrintfDummy("モーションスワップ領域のメモリが足りません。\n");
            debug_assertMessage(__FILE__, 529, "LACK OF SWAP MOTION MEMORY.\n");
            __assert(__FILE__, 529, "e");
        }
        break;
    }
    iosCdvdHandlerRead(h, motionTable[id], size);
    InitMotionFile(motionTable[id], name);
    AddMotionMemorySize(size, seg);
    debug_StdPrintfDummy("ReadMotionFile:[%d]%s (size:%d): \033[33m%1.2fMB\033[m\n", id, name, size,
                         (float)GetMotionMemorySize(seg) / 1024.0f / 1024.0f);
}

void ReadParticleEffectFile(void *h, ICO_WORD name, int size, int id)
{
    int *buf = iosMallocDebug(ios_partition_sugipon, size, __FILE__, 552);
    systemStatus[8]++;
    iosCdvdHandlerRead(h, buf, size);
    SetParticleEffectPackage(id, buf, size);
    iosFree(buf);
}

void ReadSoundBdFile(void *h, ICO_WORD name, int size, int id, int kind, int word08, int seg)
{
    char *buf;

    systemStatus[8]++;
    if (size == 0) {
        return;
    }
    buf = iosMallocDebug(ios_partition_seki, size, __FILE__, 578);
    iosCdvdHandlerRead(h, buf, size);
    switch (kind) {
    case 11: {
        int ok = 1;

        if (seFile[id].loaded == 1) {
            seg = 2;
            if (soundSeSemiCommonLoadChk() == 1) {
                ok = 0;
            }
        }
        if (ok != 0) {
            soundBDDataSet(buf, id, 11, 0, seg, size);
        }
    } break;
    case 10:
        if (sndInitBgmCancelFlag == 0) {
            soundBDDataSet(buf, id, 10, 1, seg, size);
        }
        break;
    default:
        debug_assert(__FILE__, 606);
        __assert(__FILE__, 606, "0");
    }
    iosFree(buf);
    debug_StdPrintfDummy("ReadSoundBdFile:loaded::[%d]%s  (size:%d)\n", id, name, size);
}

typedef struct { /* field names derived */
    int mode;
    int bank;
} HdInfo; /* derived name */

/* the semi-common sound bank's header buffer, freed and reallocated on each
   load */
static char *semiCommonHdBuf = 0; /* derived name */

void ReadSoundHdFile(void *h, ICO_WORD name, int size, int id, int kind, int word08, int seg)
{
    /* the sound bank/mode pair the switch fills in and soundHDDataSet reads
       back */
    volatile HdInfo info;
    char *buf;

    systemStatus[8]++;
    if (size == 0) {
        return;
    }
    switch (kind) {
    case 11:
        info.mode = 0;
        info.bank = 0;
        break;
    case 10:
        info.mode = 1;
        info.bank = sndInitBgmCancelFlag;
        break;
    default:
        debug_assert(__FILE__, 642);
        __assert(__FILE__, 642, "0");
    }
    if (info.bank == 0) {
        if (seFile[id].loaded != 1) {
            if (seg == 0) {
                buf = iosMallocDebug(ios_partition_smotion, size, __FILE__, 650);
            } else {
                buf = iosMallocDebug(ios_partition_sound, size, __FILE__, 652);
            }
        } else {
            seg = 2;
            if (soundSeSemiCommonLoadChk() == 1) {
                buf = 0;
            } else {
                if (semiCommonHdBuf != 0) {
                    iosFree(semiCommonHdBuf);
                }
                semiCommonHdBuf = iosMallocDebug(ios_partition_sound_semi, size, __FILE__, 663);
                buf = semiCommonHdBuf;
            }
        }
    } else {
        buf = 0;
    }
    iosCdvdHandlerRead(h, buf, size);
    if (buf != 0) {
        soundHDDataSet(buf, id, kind, info.mode, seg);
    }
    debug_StdPrintfDummy("ReadSoundHdFile:loaded::[%d]%s  (size:%d)\n", id, name, size);
}

typedef struct { /* field names derived */
    int mode;
    int bank;
} SqInfo; /* derived name */

/* ReadSoundSqFile and ReadSoundAdpcmFile, between ReadSoundHdFile and
   ReadShockFile; they are plain `inline`, so their out-of-line bodies come
   out at the end of the object. */

inline void ReadSoundSqFile(void *h, ICO_WORD name, int size, int id, int kind, int word08, int seg)
{
    /* the sound bank/mode pair the switch fills in and soundSQDataSet reads
       back */
    volatile SqInfo info;
    char *buf;

    systemStatus[8]++;
    if (size == 0) {
        return;
    }
    switch (kind) {
    case 11:
        info.mode = 0;
        info.bank = 0;
        break;
    case 10:
        info.mode = 1;
        info.bank = sndInitBgmCancelFlag;
        break;
    default:
        debug_assert(__FILE__, 709);
        __assert(__FILE__, 709, "0");
    }
    if (info.bank == 0) {
        if (seg == 0) {
            buf = iosMallocDebug(ios_partition_smotion, size, __FILE__, 715);
        } else {
            buf = iosMallocDebug(ios_partition_sound, size, __FILE__, 717);
        }
    } else {
        buf = 0;
    }
    iosCdvdHandlerRead(h, buf, size);
    if (buf != 0) {
        soundSQDataSet(buf, id, kind, info.mode, seg);
    }
    debug_StdPrintfDummy("ReadSoundSqFile:loaded::[%d]%s  (size:%d)\n", id, name, size);
}

inline void ReadSoundAdpcmFile(void *h, ICO_WORD name, int size, int id, int kind, int word08,
                               int seg)
{
    int key;
    int hi;
    char *p;
    SqEntry *q;

    systemStatus[8]++;
    if (size > 0x5C000)
        size = 0x5C000;
    hi = kind << 16;
    key = (id & 0xFFFF) | hi;
    if (soundDataAreaSearch(&key) == 0) {
        p = iosMallocDebug(ios_partition_smotion, size, __FILE__, 757);
        iosCdvdHandlerRead(h, p, size);
        debug_StdPrintfDummy("ReadAdpcmFile:loaded::[%d]%s  (size:%d)\n", id, name, size);
        q = adpcmDataSet(p, id, kind, seg, size, AdpcmIopBuffAlloc(), 0);
        iosFree(p);
        AdpcmPlay(q->stream);
    } else {
        iosCdvdHandlerRead(h, 0, size);
    }
}

void ReadShockFile(void *h, ICO_WORD name, int size, int id, int kind, int word08, int seg)
{
    ShockVoiceFile *p;

    systemStatus[8]++;
    if (seg == 0) {
        malloc_SetPartition(0);
        if (size == 0) {
            p = 0;
        } else {
            p = iosMallocDebug(ios_partition_shock, size + sizeof(ShockVoiceFile), __FILE__, 788);
            iosCdvdHandlerRead(h, p->image, size);
            Init_ShockVoiceSet(&p->set, p->image);
        }
        ShockVoiceSetCommon = &p->set;
    } else {
        malloc_SetPartition(1);
        if (size == 0) {
            p = 0;
        } else {
            p = mallocseki(size + sizeof(ShockVoiceFile));
            iosCdvdHandlerRead(h, p->image, size);
            Init_ShockVoiceSet(&p->set, p->image);
        }
        ShockVoiceSetStage = &p->set;
    }
    debug_StdPrintfDummy("ReadShockData:loaded::[%d]%s  (size:%d)\n", id, name, size);
}

void ReadCamerasetFile(void *h, ICO_WORD name, int size, int id)
{
    char *buf;

    buf = iosMallocDebug(ios_partition_oomori, size, __FILE__, 820);
    if (buf == 0) {
        /* not enough memory to load the camera data */
        debug_StdPrintfDummy("カメラデータをロードするためのメモリが足りません\n");
        debug_assert(__FILE__, 825);
        __assert(__FILE__, 825, "0");
    }
    systemStatus[8]++;
    iosCdvdHandlerRead(h, buf, size);
    AddPluralCameraSet(id, buf);
    iosFree(buf);
}

void ReadEndCheckFile(void *h, ICO_WORD name, int size)
{
    char *buf = iosMallocDebug(ios_partition_oomori, size, __FILE__, 854);
    systemStatus[8]++;
    iosCdvdHandlerRead(h, buf, size);
    iosFree(buf);
}

void ReadStageSettingFile(void *h, ICO_WORD name, int size)
{
    char *buf;

    systemStatus[8]++;
    buf = iosMallocDebug(ios_partition_seki, size, __FILE__, 905);
    iosCdvdHandlerRead(h, buf, size);
    /* port: the copy has no bound; every .ssb on the disc fits (R4 s.2) */
    if (size > (int)sizeof(GlobalStageSetting)) {
        __builtin_trap();
    }
    memcpy(&GlobalStageSetting, buf, size);
    light_AddLight(0, 0, 0);
    tex_RemakeRegistersSampleMin(0);
}

void CSVSYSTEM_ReadCharFiles(Sub15C *rec, int id)
{
    int n = 0;
    int sum;

    if (id >= MAX_CHARS) {
        debug_StdPrintfDummy("Illegal Char ID Number. %d\n");
        debug_StdPrintfDummy("commmon/include/charFileName.hのMAX_CHARSを増やしてください\n");
        debug_assert(__FILE__, 927);
        __assert(__FILE__, 927, "0");
    }
    rec->modelId = id;
    debug_StdPrintfDummy("Link polygon & skelton & collision -> DObj. %d\n", id);
    rec->model = charFiles[id].pObj;
    debug_StdPrintfDummy("polygon %p.\n", rec->model);
    if (rec->model != 0) {
        debug_StdPrintfDummy("object name %s.\n", rec->model);
    }
    rec->shadow = charFiles[id].pShadow;
    debug_StdPrintfDummy("shadow %p.\n", rec->shadow);
    if (rec->shadow != 0) {
        debug_StdPrintfDummy("shadow object name %s.\n", rec->shadow);
    }
    rec->skel = charFiles[id].pSkel;
    debug_StdPrintfDummy("skelton %p.\n", rec->skel);
    rec->colData = (ICO_WORD)charFiles[id].pColl;
    debug_StdPrintfDummy("collision %p.\n", rec->colData);
    if (rec->skel != 0) {
        while (rec->skel[n].mirror != -1) {
            n++;
        }
        rec->skelNodeNum = n;
        rec->morphNum = rec->skel[n].kind;
        if (rec->morphNum < 0) {
            /* the shape data count information is old */
            debug_StdPrintfDummy("\033[36m シェイプデータの数情報が古いです。%d\033[m\n",
                                 rec->morphNum);
            rec->morphNum = 0;
        }
        sum = SumBytes((unsigned char *)rec->skel, rec->skelNodeNum * 64);
        if (sum == charFiles[id].skelSum) {
            /* the skelton of "%s" is sound */
            debug_StdPrintfDummy("\033[36m\"%s\"のスケルトンは正常(%x)\033[m\n", rec->model, sum);
        } else {
            debug_StdPrintfDummy(
                "\033[33m --- W - A - R - N - I - N - G ------------------------\033[m\n");
            /* the skelton of "%s" is damaged */
            debug_StdPrintfDummy("\033[33m\"%s\"のスケルトンが破損しています(%x(NOW)!=%x)\033[m\n",
                                 rec->model, sum, charFiles[id].skelSum);
            /* it was broken between the load and the stage placement */
            debug_StdPrintfDummy("\033[33mロード直後からステージ配置の間に壊されました\033[m\n");
            debug_StdPrintfDummy(
                "\033[33m ------------------------------------------------------\033[m\n");
        }
    } else {
        rec->skelNodeNum = 0;
        rec->morphNum = 0;
    }
}
