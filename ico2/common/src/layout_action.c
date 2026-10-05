#include "layout_action.h"
#include "gamesys.h"
#include "op.h"
#include "debug.h"
#include "layout_texture.h"
#include "pad.h"
#include "gobj.h"
#include "adpcm_init.h"
#include "fightSound.h"
#include "gflag.h"
#include "kanbanBoot.h"
#include "script.h"
#include "GsBase.h"
#include <libcdvd.h>
#include <string.h>
#include "StageAnimation.h"
#include "Basic.h"
#include <stdlib.h>
#include "boyact.h"
#include "act-game.h"
#include "StageManager.h"
#include "s_init.h"
#include "typedef.h"
#include "main.h"

static int _la_set_current_port_2(void *p, int first);
static int _la_set_current_port_lock_2(void *p, int first);
static void _la_set_preview_info(void);

/* the custom key map's sixteen pad button codes (iosPadConfCustom.bit),
   the default one bit per button */
typedef struct { /* field names derived */
    int code[16];
} KeyConf; /* derived name */

/* A card port's state as _la_memory_card_check finds it, read whole and
   through a one-bit view: currentPortLockState tests bits 1 and 3..5 of the
   word, and _la_set_current_port_new tests bit 1 and sets bits 2 and 6 of
   both ports through the bits. */
typedef union { /* field names derived */
    unsigned int w;

    struct {
        unsigned int card : 1;      /* a card is in the slot */
        unsigned int ps2Card : 1;   /* it is a PlayStation 2 card */
        unsigned int bothPs2 : 1;   /* both slots hold one */
        unsigned int formatted : 1; /* sceMcGetInfo's format flag */
        unsigned int hasSpace : 1;  /* 360 free clusters or more */
        unsigned int hasSave : 1;   /* the card holds this game's save files */
        unsigned int bothSave : 1;  /* both cards do */
    } bit;
} McPortFlags; /* derived name */

typedef struct {       /* field names derived */
    McPortFlags flags; /* 0x0 */
    int fileMask;      /* 0x4, one bit for each save file on the card */
} McPortInfo;          /* derived name */

/* the save preview the file select shows, the five words of typedef.h's
   McFileInfo */
struct McPreview { /* field names derived */
    int stage;     /* 0x00 */
    int cleared;   /* 0x04 */
    int playTime;  /* 0x08, in frames */
    int sofa;      /* 0x0C */
    int word10;    /* 0x10, no C reader */
}; /* derived name */

/* .sbss, thirteen words: the port-0 lock state _la_set_current_port_2 records and the one
   _la_set_current_port_lock_2 records, the lock results for port 0 and port 1
   of _la_set_current_port_new, the icoMisc lock saved by the logo, the title
   continue and the title new game actions, the progress bar's last step, the
   save select's ready flag, the system save's retry count, the last card
   result, the load (1) or save (0) mode of the file select, and the progress
   bar's total. */
static int portLockState; /* derived name */

static int lock2PortState; /* derived name */

static int port0LockResult; /* derived name */

static int port1LockResult; /* derived name */

static int logoIcoMiscLock; /* derived name */

static int continueIcoMiscLock; /* derived name */

static int newGameIcoMiscLock; /* derived name */

static int barLastStep; /* derived name */

static int saveSelectReady; /* derived name */

static int systemSaveRetry; /* derived name */

static int mcLastResult; /* derived name */

static int mcLoadMode; /* derived name */

static int barTotal; /* derived name */

/* .bss: the two card ports' records, the preview
   record of the save being confirmed, the open request of the layout voice
   and the twenty game flags kept across a load. */
static McPortInfo mcPortInfo[2]; /* derived name */

static struct McPreview previewInfo; /* derived name */

static AdpcmOpenReq voiceOpenReq; /* derived name */

static signed char keepFlags[20]; /* derived name */

void POSITIVE_SE(void)
{
    soundSeDefPlay(412, 0xFFFFFFFE, 0, 0);
}

void NEGATIVE_SE()
{
    soundSeDefPlay(413, 0xFFFFFFFE, 0, 0);
}

void CUR_SE(void)
{
    soundSeDefPlay(411, 0xFFFFFFFE, 0, 0);
}

inline int PSH_POSITIVE_OR_NEGATIVE(int idx)
{
    int v = pad[idx].flags;
    if ((v & 0x40) != 0)
        goto one;
    if ((v & 0x10) == 0)
        goto zero;
one:
    return 1;
zero:
    return 0;
}

void la_TESTFUNCTION(void)
{
    debug_StdPrintfDummy("sync end\n");
}

/* .data: the game-flag ids the load carries across gflagInit.  The list
   holds five ids and the key-config tables follow it; the keep/restore loops
   below walk twenty words. */
#ifdef ICO_HOST

/* The keep/restore loops read twenty words from keepFlagNo: the five ids, then
   keyConfigCode and the first seven keyConfigSlot, which follow it in the EE's
   .data (so a load also keeps those game flag ids and the key-config slots;
   docs/research/compiler-semantics.md s.6).  The host holds the three as one
   array so the walk is in bounds and reads the same words. */
static int keepWords[21] = {388, 384, 383, 385, 382, 16, 128, 32, 64, 8, 2,
                            1,   4,   1,   2,   3,   4,  5,   0,  0,  0};

#define keepFlagNo keepWords
#define keyConfigCode (keepWords + 5)
#define keyConfigSlot (keepWords + 13)
#else

static int keepFlagNo[5] = {388, 384, 383, 385, 382}; /* derived name */

/* the eight pad button
   codes the key-config screen offers, and the six-plus-two slot assignments it
   edits. */
static int keyConfigCode[8] = {16, 128, 32, 64, 8, 2, 1, 4}; /* derived name */

static int keyConfigSlot[8] = {1, 2, 3, 4, 5, 0, 0, 0}; /* derived name */

#endif

/* the memory-card request block the layout actions drive */
McMgr mc = {{0}};

/* mcard.c's request entry points; they return iosMsgSend's result, which
   this TU reads only from iosMcSync: it declares the others void, which its
   calls pin, so it does not include mcard.h */
extern int iosMcSync(McMgr *mp);
extern void iosMcGetInfo(McMgr *mp);
extern void iosMcLoadProductBlock(McMgr *mp);
extern void iosMcGetBlockSaveInfo(McMgr *mp);
extern void iosMcLoadGameBlock(McMgr *mp, void *arg);
extern void iosMcFormat(McMgr *mp);
extern void iosMcSaveIconBlock(McMgr *mp);
extern void iosMcSaveProductBlock(McMgr *mp);
extern void iosMcSaveGameBlock(McMgr *mp, void *arg);
extern void iosMcDelete(McMgr *mp);
/* mcard.c's preview record */
extern int IosMcPreviewInfo[];

/* McMgr is a runtime record: its fields are not at the EE's byte offsets once
   segArg is 8 bytes wide, so the host names them */
#ifdef ICO_HOST
#define MC_PORT(p) (((McMgr *)(p))->port)
#define MC_PATH(p) (((McMgr *)(p))->path)

static int _la_mcard_error_check(void *req)
{
    McMgr *w = (McMgr *)req;

    if (w->result >= 0) {
        return 1;
    }
    switch (w->result) {
    case 0:
        return 1;
    case -2:
        debug_StdPrintfDummy("unformatted %d\n", w->result);
        return -1;
    case -9:
        debug_StdPrintfDummy("not insert memory card %d\n", w->result);
        return -1;
    case -4:
        debug_StdPrintfDummy("%s file not found\n", w->path);
        return -1;
    case -14:
        debug_StdPrintfDummy("%s Directory not found\n", w->dirName);
        return -1;
    case -16:
        debug_StdPrintfDummy("segID %d check sum err rom:%d != load:%d\n", w->segment, w->readSum,
                             w->sum);
        return -1;
    case -15:
        debug_StdPrintfDummy("%s handler func ret err code\n", w->path);
        return -1;
    case -10:
        debug_StdPrintfDummy("memory over\n");
        return -1;
    default:
        debug_StdPrintfDummy("memory card another err %d\n", w->result);
        return -2;
    }
}

#else
#define MC_PORT(p) (*(int *)((char *)(p) + 8))
#define MC_PATH(p) ((char *)(p) + 0x47C)

static int _la_mcard_error_check(void *req)
{
    char *w = (char *)req;

    if (*(int *)(w + 0x10) >= 0) {
        return 1;
    }
    switch (*(int *)(w + 0x10)) {
    case 0:
        return 1;
    case -2:
        debug_StdPrintfDummy("unformatted %d\n", *(int *)(w + 0x10));
        return -1;
    case -9:
        debug_StdPrintfDummy("not insert memory card %d\n", *(int *)(w + 0x10));
        return -1;
    case -4:
        debug_StdPrintfDummy("%s file not found\n", w + 0x47C);
        return -1;
    case -14:
        debug_StdPrintfDummy("%s Directory not found\n", w + 0x454);
        return -1;
    case -16:
        debug_StdPrintfDummy("segID %d check sum err rom:%d != load:%d\n", *(int *)(w + 0x24),
                             *(int *)(w + 0x50), *(int *)(w + 0x4C));
        return -1;
    case -15:
        debug_StdPrintfDummy("%s handler func ret err code\n", w + 0x47C);
        return -1;
    case -10:
        debug_StdPrintfDummy("memory over\n");
        return -1;
    default:
        debug_StdPrintfDummy("memory card another err %d\n", *(int *)(w + 0x10));
        return -2;
    }
}

#endif

/* file-local: nothing outside this TU calls it */
static int _la_memory_card_check(McMgr *p, int step);

/* .sdata: these ten statics and the three globals after them, then each
   function's own statics before it */
static McPortInfo *curPortInfo = &mcPortInfo[0]; /* derived name */

static int lastPort = -1; /* derived name */

static int curPort = 0; /* derived name */

static int curFile = 0; /* derived name */

static int selectFile = -1; /* derived name */

static int nextStage = 1; /* derived name */

static int fileMask = 0; /* derived name */

static int actionStarted = 0; /* derived name */

#ifdef ICO_HOST

/* PC port (Phase 6, 6C): the Settings menu (port/ui/settings.h,
   docs/port/SETTINGS.md).  Its entry rows are chained after the title's
   rows; the title procs leave Cross/START on them to default_item_select,
   which opens the menu through the row's right link, and mask them with
   their own rows while the memory card check runs. */
int ui_SettingsEntryItem(int item);
void ui_SettingsTitleMask(int masked);

#define LA_HOST_NOT_SETTINGS_ROW &&!ui_SettingsEntryItem(lt_current_property_item())

/* PC port (Q2, docs/port/SETTINGS.md "Circle goes back"): the bits of the
   procs' Triangle cancels.  With [game] circle_back on (the default) Circle
   is an alias of Triangle there (port/ui/layout_ext.h lt_ext_BackButtons);
   off, 0x10 alone, the PS2's checks.  Not used where Triangle is not a
   cancel (la_adjust_screen: it resets the brightness) or where Circle has
   a meaning of its own (la_key_config: it is one of the buttons assigned). */
int lt_ext_BackButtons(void);

#define LA_BACK lt_ext_BackButtons()

/* PC port (renderer wave 7, R7c): mirror mode, chosen on a port screen after
   the vibration choice (port/ui/settings.h ui_MirrorScreen*) and kept per
   save slot in the port config (port/game/options.h ico_mirror_slot_*;
   docs/port/SAVES.md, "Mirror mode"). */
int ui_MirrorScreenEnter(void);
int ui_MirrorScreenLayout(void);
int ico_mirror_slot_saved(int slot, unsigned int sum);
int ico_mirror_slot_loaded(int slot, unsigned int sum);
void ico_opt_mirror_reset(void);
void la_host_new_game_go(void);

/* la_vibe_select's confirm: the mirror screen in place of the start, which
   its confirm runs (la_host_new_game_go, after la_vibe_select) */
static int la_host_mirror_screen(void)
{
    lt_set_item_select_func(0);
    actionStarted = 0;
    return ui_MirrorScreenEnter();
}

#else
#define LA_HOST_NOT_SETTINGS_ROW
#define LA_BACK 0x10
#endif

static int fightSoundStopped = 0; /* derived name */

static SqEntry *layoutVoice = 0; /* derived name */

int startStagePauseDisableTimer = 0;

int layout_boot_flag = 0;

int enable_game_pause = 1;

static int _la_memory_card_check(McMgr *p, int step)
{
    int r;
    int i;

    curPortInfo = &mcPortInfo[p->port];
    switch (step) {
    case 0:
        curPortInfo->fileMask = 0;
        p->slot = 0;
        memset(&mcPortInfo[p->port], 0, 8);
        p->result = 0;
        iosMcGetInfo(p);
        mcLastResult = 0;
        (IosMcProductFile + p->port)->serial = 0;
        (IosMcProductFile + p->port)->fileNo = 0;
        step++;
        break;
    case 10:
        iosMcLoadProductBlock(p);
        step++;
        break;
    case 20:
        strcpy(MC_PATH(p), "game.");
        iosMcGetBlockSaveInfo(p);
        step++;
        break;
    case 1:
    case 11:
    case 21:
        if (iosMcSync(p)) {
            step++;
        }
        break;
    case 12:
    case 22:
        r = _la_mcard_error_check(p);
        if (r > 0) {
            step++;
        }
        switch (p->result) {
        case -16:
        case -15:
        case -4:
            p->result = -14;
            step = 99;
            r = 0;
            break;
        }
        if (r >= 0) {
            return step;
        }
        mcLastResult = p->result;
        if (p->result == -9 || r == -2) {
            curPortInfo->flags.w = (int)curPortInfo->flags.w & ~0x20;
            curPortInfo->flags.w = (int)curPortInfo->flags.w & ~2;
            curPortInfo->flags.w = (int)curPortInfo->flags.w & ~1;
        }
        /* falls through */
    case 2:
        step++;
        break;
    case 3:
        switch (p->type) {
        case 0:
            return 99;
        case 1:
        case 3:
            curPortInfo->flags.w |= 1;
            return 99;
        case 2:
            curPortInfo->flags.w |= 3;
            break;
        }
        switch (p->format) {
        case 0:
            return 99;
        case 1:
            curPortInfo->flags.w |= 8;
            break;
        }
        if (p->free >= 360) {
            curPortInfo->flags.w |= 0x10;
        } else {
            curPortInfo->flags.w = (int)curPortInfo->flags.w & ~0x10;
        }
        step = 10;
        break;
    case 13:
        step = 20;
        break;
    case 23:
        if (p->dirCount >= 11) {
            debug_StdPrintfDummy(
                "debug_mcLoadMainBlock:既に設定された数以上のデータを保存してる\n");
        }
        for (i = 0; i < 10; i++) {
            if ((1 << i) & p->mask) {
                break;
            }
        }
        step = 99;
        if (i == 10) {
            break;
        }
        for (i = 0; i < 10; i++) {
            if (IosMcProductFile[p->port].file[i].stage != 0xFFFFFFFF) {
                break;
            }
        }
        if (i < 10) {
            if (p->dirCount == 10) {
                curPortInfo->flags.w |= 0x20;
                curPortInfo->fileMask = p->mask;
            }
        }
        break;
    }
    return step;
}

/* the current port's lock state: 1 when its record's bit 1 is set and bits
   3..5 are not 1, else -1; inlined three times into _la_set_current_port_2
   and once into _la_set_current_port_lock_2 */
static inline int currentPortLockState(void) /* derived name */
{
    return (((curPortInfo->flags.w >> 1) & 1) && (curPortInfo->flags.w & 0x38) != 8) ? 1 : -1;
}

static int port2Step = 0; /* derived name */

static int port2SubStep = 0; /* derived name */

static int port2Changed = 0; /* derived name */

static int port2Locked = 0; /* derived name */

static int _la_set_current_port_2(void *p, int first)
{
    McPortInfo tmp;
    int r;
    int q = 0;

    if (first != 0) {
        MC_PORT(p) = 0;
        port2Step = 0;
        port2SubStep = 0;
        fileMask = 0;
        return 0;
    }
    curPortInfo = &mcPortInfo[MC_PORT(p)];
    port2Step = _la_memory_card_check(p, port2Step);
    if (port2Step == 99) {
        switch (MC_PORT(p)) {
        case 0:
            portLockState = currentPortLockState();
            port2Changed = (curPortInfo->flags.w >> 5) & 1;
            port2Locked = ((curPortInfo->flags.w >> 1) & 1) && (curPortInfo->flags.w & 0x38) != 8;
            /* the whole 8-byte record is copied to a local and never read
               again */
            tmp = *curPortInfo;
            MC_PORT(p) = 1;
            port2Step = 0;
            break;
        case 1:
            port2Step = 100;
            switch (portLockState) {
            case 1:
                r = currentPortLockState();
                switch (r) {
                case 1:
                    r = curPortInfo->flags.w >> 5;
                    r &= 1;
                    if ((curPortInfo->flags.w >> 1) & 1) {
                        if ((curPortInfo->flags.w & 0x38) != 8) {
                            q = 1;
                        }
                    }
                    if (port2Locked != 0 && q != 0) {
                        mcPortInfo[0].flags.w |= 4;
                        mcPortInfo[1].flags.w |= 4;
                    }
                    if (port2Changed != 0 && r != 0) {
                        mcPortInfo[0].flags.w |= 0x40;
                        mcPortInfo[1].flags.w |= 0x40;
                    }
                    if (lastPort >= 0) {
                        curPort = lastPort;
                    } else if (port2Changed != 0) {
                        curPort = 0;
                    } else if ((curPortInfo->flags.w >> 5) & 1) {
                        curPort = 1;
                    }
                    break;
                case -1:
                    lastPort = r;
                    curPort = 0;
                    curPortInfo = &mcPortInfo[0];
                    curPortInfo->flags.w = (int)curPortInfo->flags.w & ~4;
                    curPortInfo->flags.w = (int)curPortInfo->flags.w & ~0x40;
                    break;
                }
                break;
            case -1:
                r = currentPortLockState();
                switch (r) {
                case 1:
                    curPort = r;
                    curPortInfo = &mcPortInfo[1];
                    curPortInfo->flags.w = (int)curPortInfo->flags.w & ~4;
                    curPortInfo->flags.w = (int)curPortInfo->flags.w & ~0x40;
                    break;
                case -1:
                    curPort = 0;
                    curPortInfo = &mcPortInfo[0];
                    if ((mcPortInfo[0].flags.w & 1) || (mcPortInfo[1].flags.w & 1)) {
                        mcPortInfo[0].flags.w |= 1;
                    }
                    if (((mcPortInfo[0].flags.w >> 1) & 1) || ((mcPortInfo[1].flags.w >> 1) & 1)) {
                        curPortInfo->flags.w |= 2;
                    }
                    curPortInfo->flags.w = (int)curPortInfo->flags.w & ~4;
                    curPortInfo->flags.w = (int)curPortInfo->flags.w & ~0x40;
                    _la_set_current_port_2(p, 1);
                    return -1;
                }
                break;
            }
            curPortInfo = &mcPortInfo[curPort];
            _la_set_current_port_2(p, 1);
            return 1;
        }
    }
    return 0;
}

static int lock2Restart = 1; /* derived name */

static int lock2Step = 0; /* derived name */

static int lock2SubStep = 0; /* derived name */

static int lock2Changed = 0; /* derived name */

static int lock2Locked = 0; /* derived name */

static int _la_set_current_port_lock_2(void *p, int first)
{
    McPortInfo tmp;
    int r;
    int a;
    int q;

    if (first != 0 || lock2Restart != 0) {
        MC_PORT(p) = curPort;
        lock2Step = 0;
        lock2Restart = 0;
        lock2SubStep = 0;
        fileMask = 0;
        return 0;
    }
    curPortInfo = &mcPortInfo[MC_PORT(p)];
    lock2Step = _la_memory_card_check(p, lock2Step);
    if (lock2Step != 99) {
        return 0;
    }
    r = currentPortLockState();
    lock2PortState = r;
    lock2Changed = (curPortInfo->flags.w >> 5) & 1;
    lock2Locked = ((curPortInfo->flags.w >> 1) & 1) && (curPortInfo->flags.w & 0x38) != 8;
    tmp = *curPortInfo;
    switch (lock2PortState) {
    case 1:
        a = curPortInfo->flags.w >> 5;
        a &= 1;
        q = 0;
        if ((curPortInfo->flags.w >> 1) & 1) {
            if ((curPortInfo->flags.w & 0x38) != 8) {
                q = 1;
            }
        }
        if (lock2Locked != 0 && q != 0) {
            mcPortInfo[MC_PORT(p)].flags.w |= 4;
        }
        if (lock2Changed != 0 && a != 0) {
            mcPortInfo[MC_PORT(p)].flags.w |= 0x40;
        }
        _la_set_current_port_lock_2(p, 1);
        return 1;
    case -1:
        curPortInfo = &mcPortInfo[curPort];
        curPortInfo->flags.w = (int)curPortInfo->flags.w & ~4;
        curPortInfo->flags.w = (int)curPortInfo->flags.w & ~0x40;
        return -1;
    }
    return 0;
}

static int portNewStep = 0; /* derived name */

static int portNewRestart = 1; /* derived name */

static int _la_set_current_port_new(McMgr *p, int first)
{
    int r = 0;
    int v;

    if (first) {
        portNewStep = 0;
        curPort = -1;
    }
    switch (portNewStep) {
    case 0:
    case 2:
        curPort++;
        portNewRestart = 1;
        portNewStep++;
        break;
    case 1:
        port0LockResult = _la_set_current_port_lock_2(p, portNewRestart);
        portNewRestart = 0;
        if (port0LockResult == 0) {
            break;
        }
        portNewStep++;
        break;
    case 3:
        port1LockResult = _la_set_current_port_lock_2(p, portNewRestart);
        portNewRestart = 0;
        if (port1LockResult == 0) {
            break;
        }
        portNewStep++;
        break;
    case 4:
        if (((mcPortInfo[0].flags.w >> 1) & 1) && ((mcPortInfo[1].flags.w >> 1) & 1))
            v = 1;
        else
            v = 0;
        mcPortInfo[0].flags.bit.bothPs2 = mcPortInfo[1].flags.bit.bothPs2 = v;
        if (((mcPortInfo[0].flags.w >> 5) & 1) && ((mcPortInfo[1].flags.w >> 5) & 1))
            v = 1;
        else
            v = 0;
        mcPortInfo[0].flags.bit.bothSave = mcPortInfo[1].flags.bit.bothSave = v;
        if (port0LockResult == 1) {
            curPort = 0;
            r = 1;
        } else if (port1LockResult == 1) {
            curPort = 1;
            r = 1;
        } else {
            if (((mcPortInfo[0].flags.w >> 1) & 1) == 0 && mcPortInfo[1].flags.bit.ps2Card == 1) {
                curPort = 1;
            } else {
                curPort = 0;
            }
            r = -1;
        }
        p->port = curPort;
        curPortInfo = &mcPortInfo[curPort];
        break;
    }
    return r;
}

inline int la_boot_memory_card_check(void)
{
    if (kanbanBootEnd == 0) {
        return -1;
    }
    systemStatus[11] = 7;
    layout_boot_flag = 1;
    lt_set_item_select_func(0);
    actionStarted = 0;
    return 0x37;
}

inline int la_boot_no_memory_card(int first, int item)
{
    debug_StdPrintfDummy("no memoca\n");
    return item;
}

inline int la_boot_no_free_area(int first, int item)
{
    debug_StdPrintfDummy("no free\n");
    return item;
}

inline int la_boot_confirm_memory_card(void)
{
    if (pad[0].flags & 0x40) {
        return lt_link_layout(0);
    }
    return -1;
}

inline void keyconfig_reset(void)
{
    KeyConf def = {{0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080, 0x0100, 0x0200,
                    0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000}};

    *(KeyConf *)iosPadConfCustom.bit = def;
}

int la_vibe_select(void)
{
    if (lt_fade_status() == 2 && (pad[0].flags & 0x840)) {
        soundSeDefPlay(415, 0xFFFFFFFF, 0, 0);
        switch (lt_current_property_item()) {
        case 0x2C:
            iosPadActRequestEnable = 1;
            break;
        case 0x2D:
            iosPadActRequestEnable = 0;
            break;
        }
#ifdef ICO_HOST
        if (ui_MirrorScreenLayout() >= 0) {
            return la_host_mirror_screen(); /* R7c: the mirror screen, then the start */
        }
#endif
        if (titleAdpcm != 0) {
            titleAdpcm->stream->fadeStep = 0x80;
        }
        titleAdpcm = 0;
        gflagInit();
        keyconfig_reset();
        systemStatus[4] = 0;
        gflagOn(382);
        return -1;
    }
    if (lt_fade_status() != 2) {
        return -1;
    }
    if ((pad[0].flags & LA_BACK) == 0) {
        return -1;
    }
    NEGATIVE_SE();
    lt_set_item_select_func(0);
    actionStarted = 0;
    return 0xC;
}

#ifdef ICO_HOST

/* PC port (renderer wave 7, R7c): the start la_vibe_select's confirm made,
   run by the mirror screen's confirm (port/ui/settings.c) once the player
   has picked: the title music fades, the game flags and the key config are
   reset and gflag 382 starts the new game, at the same moment as each
   other, as on the PS2 */
void la_host_new_game_go(void)
{
    if (titleAdpcm != 0) {
        titleAdpcm->stream->fadeStep = 0x80;
    }
    titleAdpcm = 0;
    gflagInit();
    keyconfig_reset();
    systemStatus[4] = 0;
    gflagOn(382);
}

#endif

inline int la_scei_logo(int first)
{
    if (first) {
        stgmgrNextStagePreLoadForceStageSet(0);
        logoIcoMiscLock = lock_execIcoMisc;
        systemStatus[5] = 1;
        iosPadEnable();
        isysGObjActiveLink(0, 0);
        gflagOff(386);
        if (layout_boot_flag == 0) {
            layout_boot_flag = 1;
        }
    }
    return -1;
}

int title_demo_mode = 0;

inline int la_title_demo(void)
{
    return -1;
}

static int continueDecided = 0; /* derived name */

int la_title_continue_or_new(int first)
{
    if (first) {
        opTitleLogoMode = 1;
        continueIcoMiscLock = lock_execIcoMisc;
        iosPadEnable();
        isysGObjActiveLink(0, 1);
        systemStatus[5] = 0;
        gflagOff(382);
#ifdef ICO_HOST
        ico_opt_mirror_reset(); /* R7c: the title belongs to no run */
#endif
        if (gflagChk(385) == 0) {
            gflagOn(385);
        }
        continueDecided = 0;
        lt_continue_selected = 0;
    }
    if (continueDecided != 0) {
        lt_item_select_disable = 0;
        lt_mask_property(0x31, 0);
        lt_mask_property(0x32, 0);
    } else {
        lt_item_select_disable = 1;
        lt_mask_property(0x31, 1);
        lt_mask_property(0x32, 1);
    }
#ifdef ICO_HOST
    ui_SettingsTitleMask(continueDecided == 0);
#endif
    /* PC port (6C): not on the Settings row, whose Cross default_item_select
       takes (it opens the menu) */
    if (continueDecided != 0 && (pad[0].flags & 0x840) &&
        lt_fade_status() == 2 LA_HOST_NOT_SETTINGS_ROW) {
        lt_continue_selected = 1;
        soundSeDefPlay(414, 0xFFFFFFFF, 0, 0);
        switch (lt_current_property_item()) {
        case 0x31:
            opTitleLogoMode = 2;
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x14;
        case 0x32:
            opTitleLogoMode = 2;
            gFlagGameClear = 0;
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 9;
        }
    }
    switch (_la_set_current_port_2(&mc, first)) {
    case 0:
        break;
    case 1:
        if ((curPortInfo->flags.w & 0x22) == 2) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0xD;
        }
        continueDecided = 1;
        break;
    case -1:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0xD;
    }
    return -1;
}

static int newGameDecided = 0; /* derived name */

int la_title_new_game_only(int first)
{
    if (first) {
        opTitleLogoMode = 1;
        newGameIcoMiscLock = lock_execIcoMisc;
        iosPadEnable();
        isysGObjActiveLink(0, 1);
        systemStatus[5] = 0;
        gflagOff(382);
#ifdef ICO_HOST
        ico_opt_mirror_reset(); /* R7c: the title belongs to no run */
#endif
        if (gflagChk(385) == 0) {
            gflagOn(385);
        }
        newGameDecided = 0;
        lt_continue_selected = 0;
        lastPort = -1;
    }
    if (newGameDecided != 0) {
        lt_item_select_disable = 0;
        lt_mask_property(51, 0);
    } else {
        lt_item_select_disable = 1;
        lt_mask_property(51, 1);
    }
#ifdef ICO_HOST
    ui_SettingsTitleMask(newGameDecided == 0);
#endif
    /* PC port (6C): not on the Settings row, whose Cross default_item_select
       takes (it opens the menu) */
    if (newGameDecided != 0 && (pad[0].flags & 0x840) &&
        lt_fade_status() == 2 LA_HOST_NOT_SETTINGS_ROW) {
        soundSeDefPlay(414, 0xFFFFFFFF, 0, 0);
        lt_continue_selected = 1;
        opTitleLogoMode = 2;
        gFlagGameClear = 0;
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 9;
    }
    switch (_la_set_current_port_2(&mc, first)) {
    case 0:
        break;
    case -1:
        newGameDecided = 1;
        break;
    case 1:
        if ((curPortInfo->flags.w >> 5) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0xC;
        }
        newGameDecided = 1;
        break;
    }
    return -1;
}

static int filePort = -1; /* derived name */

static int saveSerial = 0; /* derived name */

static int loadSerial = 0; /* derived name */

static int savedFileNo = 0; /* derived name */

static int fileMoved = 0; /* derived name */

static int fileFirst = 1; /* derived name */

static inline int la_mc_saved_file_select(int item)
{
    int i = item - 0x3E;
    int old = i;

    lt_analog2Pad();
    do {
        if (pad[0].flags & 0x1000) {
            i += 5;
        } else if (pad[0].flags & 0x4000) {
            i -= 5;
        } else if (pad[0].flags & 0x8000) {
            i -= 1;
        } else if (pad[0].flags & 0x2000) {
            i += 1;
        } else if (IosMcProductFile[filePort].file[i].stage == 0xFFFFFFFF) {
            i++;
        }
        if (i < 0) {
            i += 10;
        }
        if (i >= 10) {
            i -= 10;
        }
    } while (IosMcProductFile[filePort].file[i].stage == 0xFFFFFFFF);
    if (old != i) {
        CUR_SE();
    }
    return i + 0x3E;
}

/* the selected port's current file number, 0 when the port has no card or
   the file is empty; inlined into la_mc_file_select both directly and
   through mcFileNoOfPort below */
static inline int mcCurrentFileNo(void) /* derived name */
{
    int port = filePort;
    int no = (IosMcProductFile + port)->fileNo;
    if (mcPortInfo[port].fileMask == 0 || IosMcProductFile[port].file[no].stage == 0xFFFFFFFF)
        return 0;
    return no;
}

/* the saved file's number when the port holds the loaded save, else the
   current file's; inlined once, into la_mc_file_select */
static inline int mcFileNoOfPort(void) /* derived name */
{
    int port = filePort;

    if (loadSerial == (IosMcProductFile + port)->serial)
        return savedFileNo;
    return mcCurrentFileNo();
}

int la_mc_file_select(int first)
{
    int i;

    curFile = lt_current_property_item() - 62;

    if (first) {
        fileFirst = 1;
        fileMoved = 0;
    }

    if (actionStarted == 0) {
        lt_item_select_disable = 1;
        return -1;
    }

    if (fileFirst != 0) {
        fileFirst = 0;
        if (mcLoadMode != 0) {
            curFile = mcCurrentFileNo();
        } else {
            curFile = mcFileNoOfPort();
        }
        texLayout[14].curItem = curFile + 62;
        fileMoved = 1;
    }

    for (i = 0; i < 10; i++) {
        if (((fileMask >> i) & 1) && IosMcProductFile[filePort].file[i].stage != 0xFFFFFFFF) {
            lt_mask_property(i + 62, 0);
            lt_mask_property(i + 52, 1);
        } else {
            lt_mask_property(i + 62, 1);
            lt_mask_property(i + 52, 0);
        }
    }

    previewInfo = *(struct McPreview *)&IosMcProductFile[filePort].file[curFile];

    return (pad[0].flags & (0x40 | LA_BACK)) ? curFile : -1;
}

static void _la_mask_preview_info(void)
{
    int i;

    for (i = 0; i < 10; i++) {
        lt_mask_property(i + 76, 1);
        lt_mask_property(i + 86, 1);
        lt_mask_property(i + 96, 1);
        lt_mask_property(i + 106, 1);
        lt_mask_property(i + 116, 1);
        lt_mask_property(i + 126, 1);
    }
    lt_mask_property(74, 1);
    lt_mask_property(75, 1);
    for (i = 0; i < 39; i++) {
        lt_mask_property(i + 137, 1);
    }
    lt_mask_property(136, 1);
}

/* the play time of a save record split into hours, minutes and seconds and
   clamped to 99:59:59, inlined into _la_set_preview_info, la_load_processing
   and la_system_save_processing; the last two leave the results unused */
static inline void playTime(struct McPreview *p, int *hour, int *min, int *sec) /* derived name */
{
    int frames = p->playTime;
    int fps = ((60 - systemStatus[0] * 10) / systemStatus[1]) * systemStatus[1];

    *sec = (frames / fps) % 60;
    *min = (frames / (fps * 60)) % 60;
    *hour = frames / (fps * 3600);
    if (*hour >= 100) {
        *hour = 99;
        *min = 59;
        *sec = 59;
    }
}

static void _la_set_preview_info(void)
{
    int hour;
    int min;
    int sec;
    int n;

    _la_mask_preview_info();

    if (((fileMask >> curFile) & 1) == 0) {
        return;
    }
    if (IosMcProductFile[filePort].file[curFile].stage == 0xFFFFFFFF) {
        return;
    }

    playTime(&previewInfo, &hour, &min, &sec);

    hour = ((hour / 10 + 9) % 10) * 10 + (hour % 10 + 9) % 10;
    min = ((min / 10 + 9) % 10) * 10 + (min % 10 + 9) % 10;
    sec = ((sec / 10 + 9) % 10) * 10 + (sec % 10 + 9) % 10;

    lt_mask_property(76 + hour / 10, 0);
    lt_mask_property(86 + hour % 10, 0);
    lt_mask_property(96 + min / 10, 0);
    lt_mask_property(106 + min % 10, 0);
    lt_mask_property(116 + sec / 10, 0);
    lt_mask_property(126 + sec % 10, 0);
    lt_mask_property(74, 0);
    lt_mask_property(75, 0);

    n = previewInfo.stage;
    if (n == 63) {
        n = 38;
    }
    if (n >= 3 && n < 56) {
        switch (previewInfo.sofa) {
        case 329:
            n = 39;
            break;
        case 331:
            n = 40;
            break;
        }
        lt_mask_property(n + 134, 0);
        if (previewInfo.cleared != 0) {
            lt_mask_property(136, 0);
        }
    }
}

inline int la_mc_preview_info(void)
{
    if (fileMask == 0) {
        if ((1 >> curFile) & 1) {
            return -1;
        }
    }
    _la_set_preview_info();
    return -1;
}

inline int la_mc_current_slot(void)
{
    lt_mask_property(0xB0, curPort);
    lt_mask_property(0xB1, curPort ^ 1);
    return -1;
}

/* the load screen's default item; inlined once, into
   la_load_game_memory_card_check */
static inline void setLoadGameStartItem(void) /* derived name */
{
    if (loadSerial == IosMcProductFile[0].serial || saveSerial != IosMcProductFile[1].serial) {
        texLayout[17].defaultItem = 186;
    } else {
        texLayout[17].defaultItem = 187;
    }
}

int la_load_game_memory_card_check(int first)
{
    _la_mask_preview_info();
    mcLoadMode = 1;
    lt_mask_property(0xB0, 1);
    lt_mask_property(0xB1, 1);
    switch (_la_set_current_port_2(&mc, first)) {
    case 0:
        if (mcLastResult == 0 || mcLastResult == -14) {
            return -1;
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x16;
    case -1:
        if ((curPortInfo->flags.w >> 1) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x17;
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x16;
    case 1:
        if ((curPortInfo->flags.w >> 6) & 1) {
            setLoadGameStartItem();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x11;
        }
        if ((curPortInfo->flags.w >> 5) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x13;
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x17;
    }
    return -1;
}

inline int la_mc_load_current_slot_select(void)
{
    _la_mask_preview_info();
    if (pad[0].flags & 0x40) {
        curPort = lt_current_property_item() - 0xBA;
        lastPort = curPort;
        curPortInfo = &mcPortInfo[curPort];
        POSITIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x13;
    }
    if (pad[0].flags & LA_BACK) {
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0xC;
    }
    return -1;
}

static int loadCardChanged = 1; /* derived name */

static int loadFileChosen = 0; /* derived name */

int la_mc_load_file_select(int first, int item)
{
    int r;

    if (first) {
        fileMask = 0;
        lastPort = filePort = curPort;
        loadFileChosen = 0;
    }

    if (fileMask != 0) {
        _la_set_preview_info();
        lt_mask_property(73, 1);
        lt_mask_property(72, 1);
        lt_mask_property(195, 0);
        lt_mask_property(196, 0);
    } else {
        _la_mask_preview_info();
        lt_mask_property(73, 0);
        lt_mask_property(72, 0);
        lt_mask_property(195, 1);
        lt_mask_property(196, 1);
    }
    lt_set_item_select_func((ICO_WORD_PTR(LtSelectFn))la_mc_saved_file_select);

    if (pad[0].flags & LA_BACK) {
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 12;
    }

    if ((fileMask != 0 || loadFileChosen != 0) && (pad[0].flags & 0x40)) {
        POSITIVE_SE();
        if (IosMcProductFile[filePort].file[item].stage != 0xFFFFFFFF) {
            selectFile = item;
            lastPort = filePort;
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 21;
        }
    }

    r = _la_set_current_port_lock_2(&mc, first);
    switch (r) {
    case -1:
        if (((curPortInfo->flags.w >> 1) & 1) == 0) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 22;
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 23;
    case 1:
        if (((curPortInfo->flags.w >> 1) & 1) == 0) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 22;
        }
        if ((curPortInfo->flags.w & 0x22) == 2) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 23;
        }
        break;
    case 0:
    default:
        return -1;
    }

    if (fileMask != 0) {
        if (loadCardChanged == 0) {
            if (((curPortInfo->flags.w >> 6) & 1) != 0) {
                debug_StdPrintfDummy("1 to 2\n");
                lt_set_item_select_func(0);
                actionStarted = 0;
                return 17;
            }
        } else if (((curPortInfo->flags.w >> 6) & 1) == 0) {
            debug_StdPrintfDummy("2 to 1\n");
            loadCardChanged = (curPortInfo->flags.w >> 6) & 1;
        }
    } else {
        loadCardChanged = (curPortInfo->flags.w >> 6) & 1;
        fileMask = curPortInfo->fileMask;
        actionStarted = r;
        if ((curPortInfo->flags.w & 0xA) == 2) {
            loadFileChosen = r;
        }
    }
    return -1;
}

int la_load_confirm_no_memory_card(int first)
{
    if (first) {
        _la_mask_preview_info();
    }
    if (PSH_POSITIVE_OR_NEGATIVE(0)) {
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0xD;
    }
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        if ((curPortInfo->flags.w & 0x32) == 2 || (curPortInfo->flags.w & 0x22) == 2) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x17;
        }
        if ((curPortInfo->flags.w >> 1) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x14;
        }
        lock2Restart = 1;
        break;
    case 1:
        if ((curPortInfo->flags.w & 0x32) == 2 || (curPortInfo->flags.w & 0x22) == 2) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x17;
        }
        if ((curPortInfo->flags.w >> 1) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x14;
        }
        break;
    }
    return -1;
}

int la_load_confirm_no_data(int first)
{
    if (first) {
        _la_mask_preview_info();
    }
    if (PSH_POSITIVE_OR_NEGATIVE(0)) {
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0xD;
    }
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        if (((curPortInfo->flags.w >> 1) & 1) == 0) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x16;
        }
        lock2Restart = 1;
        break;
    case 1:
        if ((curPortInfo->flags.w >> 5) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x14;
        }
        break;
    }
    return -1;
}

int la_load_start_check(int first)
{
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        fileMask = 0x3FF;
        if (mcLastResult == 0 || mcLastResult == -14) {
            return -1;
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x2E;
    case -1:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x2E;
    case 1:
        if (filePort != curPort) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x2E;
        }
        if ((curPortInfo->flags.w & 3) != 3) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x16;
        }
        if ((curPortInfo->fileMask >> selectFile) & 1) {
            fileMask = 0x3FF;
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x19;
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x17;
    }
    return -1;
}

/* the saved file's serial read from the port's record, with the file number
   kept beside it; inlined into la_load_processing directly and into
   la_save_processing through mcSetSavedFile */
static inline int mcSetFileNo(int port, int no) /* derived name */
{
    int serial = (IosMcProductFile + port)->serial;

    savedFileNo = no;
    return serial;
}

/* the game flags the load carries across gflagInit, parked in keepFlags;
   inlined into la_load_processing */
static inline void gflagKeepState(void) /* derived name */
{
    unsigned int i;

    for (i = 0; i < 20; i++) {
        keepFlags[i] = gflagChk(keepFlagNo[i]);
    }
}

/* the other half of the pair */
static inline void gflagRestoreState(void) /* derived name */
{
    unsigned int i;

    for (i = 0; i < 20; i++) {
        if (keepFlags[i]) {
            gflagOn(keepFlagNo[i]);
        } else {
            gflagOff(keepFlagNo[i]);
        }
    }
}

static int loadStep = 0; /* derived name */

int la_load_processing(int first)
{
    int err;
    int hour;
    int min;
    int sec;

    debug_StdPrintfDummy("load processing\n");
    if (first) {
        loadStep = 0;
    }

    switch (loadStep) {
    case 0:
        strcpy(mc.path, "game.");
        mc.fileNo = selectFile;
        mc.port = curPort;
        mc.slot = 0;
        iosMcGetBlockSaveInfo(&mc);
        loadStep++;
        break;
    case 1:
    case 3:
        if (iosMcSync(&mc) != 0) {
            loadStep++;
        }
        break;
    case 2:
        loadStep++;
        break;
    case 6:
    case 9:
        debug_StdPrintfDummy("case %d\n", loadStep);
        err = _la_mcard_error_check(&mc);
        if (err > 0) {
            loadStep++;
            debug_StdPrintfDummy("McLoad phase:%d  %x\n", loadStep, err);
            debug_StdPrintfDummy("phase++\n");
            return -1;
        }
        debug_StdPrintfDummy("through\n");
        debug_StdPrintfDummy("chk:%d\n", err);
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 46;
    case 4:
        debug_StdPrintfDummy("case 4\n");
        if (((1 << mc.fileNo) & mc.mask) == 0) {
            loadStep = 20;
        } else {
            iosMcLoadProductBlock(&mc);
            loadStep++;
        }
        break;
    case 5:
    case 8:
        debug_StdPrintfDummy("case %d\n", loadStep);
        if (iosMcSync(&mc) != 0) {
            loadStep++;
        }
        break;
    case 7:
        debug_StdPrintfDummy("case %d\n", loadStep);
        debug_StdPrintfDummy("=== LoadGameBlock ===\n");
        iosMcLoadGameBlock(&mc, gameSysMainSaveBuff);
        loadStep++;
        break;
    case 10:
        gflagKeepState();
        gflagInit();
        gamesysMemoryLoad(gameSysMemoryFuncList, gameSysMainSaveBuff, 0);
        gflagRestoreState();
        systemStatus[3] = 1;
        systemStatus[4] = 1;
        debug_StdPrintfDummy("case 10\n");
        loadStep = 0;
        *(struct McPreview *)IosMcPreviewInfo =
            *(struct McPreview *)&IosMcProductFile[mc.port].file[mc.fileNo];
        playTime((struct McPreview *)IosMcPreviewInfo, &hour, &min, &sec);
        loadSerial = mcSetFileNo(mc.port, mc.fileNo);
#ifdef ICO_HOST
        ico_mirror_slot_loaded(mc.fileNo, (unsigned int)mc.sum); /* R7c: the slot's flag */
#endif
        debug_StdPrintfDummy("stage no %d\n", gFlagSaveStage);
        seEnvForceClose = 1;
        if (titleAdpcm != 0) {
            titleAdpcm->stream->fadeStep = 0x40;
        }
        titleAdpcm = 0;
        if (gflagChk(395)) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 9;
        }
        stgmgrForceSwitchWithFade(gFlagSaveStage, 0.05f, 4.0f);
        ACTGame_SetActors_Debug(gFlagSaveStage, 0);
        return -1;
    case 20:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 20;
    }
    return -1;
}

inline int la_general_mc_confirm(void)
{
    if (pad[0].flags & 0x40) {
        return lt_current_property_item();
    }
    return -1;
}

/* opens layout voice number no; inlined into la_game_over_continue and into
   la_mc_confirm_save_file with different numbers */
static inline int openLayoutVoice(int no) /* derived name */
{
    if (layoutVoice != 0) {
        return 0;
    }
    soundDataOpen(&voiceOpenReq, 2, no, 1, 0);
    return 1;
}

/* extra preview pages to re-mask; none in the release build */
#define LA_EXTRA_PREVIEW_PAGES 0

static int saveVoice = 0; /* derived name */

static int saveVoiceOpened = 0; /* derived name */

static int saveVoiceWait = 0; /* derived name */

int la_mc_confirm_save_file(int first, int item)
{
    int i;

    _la_mask_preview_info();
    lt_mask_property(0xB0, 1);
    lt_mask_property(0xB1, 1);
    if (first) {
        systemStatus[5] = 1;
        fightSoundProcessRequestPause();
        fightSoundStopped = 1;
        CheckPoint();
        saveVoiceOpened = 0;
        saveVoiceWait = 0;
    }
    if (saveVoiceOpened == 0) {
        if (AdpcmFreeAreaGet() != 0) {
            saveVoice = openLayoutVoice(0x16);
            saveVoiceOpened = 1;
        } else {
            debug_StdPrintfDummy("ADPCM一杯で開けませんでした。\n");
            if (AdpcmNotUseIopAreaFree() != 0) {
                debug_StdPrintfDummy(
                    "オープンされていないのにも関わらず使われていないIOP領域発見&強制解放\n");
                return -1;
            }
            if (saveVoiceWait-- < 0) {
                AdpcmFadeCloseAll(0x3FFF);
                return -1;
            }
        }
    } else {
        _la_mask_preview_info();
        /* a loop over extra preview pages, built with a count of 0 in the
           release build, so its body never runs */
        for (i = 0; i < LA_EXTRA_PREVIEW_PAGES; i++) {
            _la_mask_preview_info();
        }
        if (saveVoice != 0) {
            layoutVoice = soundDataOpenSync(&voiceOpenReq);
            if (layoutVoice != (SqEntry *)ICO_INVALID_PTR) {
                saveVoice = 0;
                if (layoutVoice != 0) {
                    AdpcmPlay(layoutVoice->stream);
                    return -1;
                }
            }
            return -1;
        }
        {
            if (lt_fade_status() == 2) {
                switch (item) {
                case 214:
                    POSITIVE_SE();
                    lt_set_item_select_func(0);
                    actionStarted = 0;
                    return 0x1E;
                case 215:
                    NEGATIVE_SE();
                    systemStatus[5] = 0;
                    lt_set_item_select_func(0);
                    actionStarted = 0;
                    return 0x36;
                }
            }
            if (lt_fade_status() != 2) {
                return -1;
            }
            if ((pad[0].flags & LA_BACK) == 0) {
                return -1;
            }
            if (stage_no == 0x3F) {
                return -1;
            }
            NEGATIVE_SE();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x36;
        }
    }
    return -1;
}

/* the save side of setLoadGameStartItem, inlined twice into
   la_save_game_memory_card_check */
static inline void setSaveGameStartItem(void) /* derived name */
{
    if (loadSerial == IosMcProductFile[0].serial || loadSerial != IosMcProductFile[1].serial) {
        texLayout[18].defaultItem = 186;
    } else {
        texLayout[18].defaultItem = 187;
    }
}

int la_save_game_memory_card_check(int first)
{
    _la_mask_preview_info();
    mcLoadMode = 0;
    lt_mask_property(0xB0, 1);
    lt_mask_property(0xB1, 1);
    debug_StdPrintfDummy("save game check port %d\n", curPort);
    switch (_la_set_current_port_new(&mc, first)) {
    case 0:
        break;
    case -1:
        debug_StdPrintfDummy("fail\n");
        if ((curPortInfo->flags.w & 3) == 3) {
            if ((curPortInfo->flags.w >> 2) & 1) {
                setSaveGameStartItem();
                lt_set_item_select_func(0);
                actionStarted = 0;
                return 0x12;
            }
            if (((curPortInfo->flags.w >> 4) & 1) == 0) {
                lt_set_item_select_func(0);
                actionStarted = 0;
                return 0x20;
            }
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x1F;
    case 1:
        debug_StdPrintfDummy("sucess :%d %d %d\n", (curPortInfo->flags.w >> 5) & 1,
                             (curPortInfo->flags.w >> 4) & 1, (curPortInfo->flags.w & 0xA) == 2);
        if ((curPortInfo->flags.w >> 2) & 1) {
            setSaveGameStartItem();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x12;
        }
        if ((curPortInfo->flags.w & 0x30) != 0 || (curPortInfo->flags.w & 0xA) == 2) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x21;
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x20;
    }
    return -1;
}

inline int la_mc_save_current_slot_select(void)
{
    if (pad[0].flags & 0x40) {
        POSITIVE_SE();
        curPort = lt_current_property_item() - 0xBA;
        lastPort = curPort;
        curPortInfo = &mcPortInfo[curPort];
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x21;
    }
    if (pad[0].flags & LA_BACK) {
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x1C;
    }
    return -1;
}

/* the sprite rectangle and colour the gif helpers take, the same pair
   layout_texture.c declares */
typedef struct { /* field names derived */
    int x;
    int y;
    int w;
    int h;
} SprRect; /* derived name */

typedef struct { /* field names derived */
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} SprCol; /* derived name */

/* GifPacket.h's entry points, which this TU does not include; gif_Sprite
   takes z as an unsigned int here, a long long in the header */
extern void gif_StartPacketPri(int pri);
extern void gif_SetZTest(int on);
extern void gif_SetZWrite(int on);
extern void gif_SetAlpha(long long alpha, long long mode, long long fix);

/* ICO_HOST: GifPacket.c's parameter types, so arguments land where the
   definition reads them on hosts that pass them on the stack
   (layout_texture.c says more) */
#ifdef ICO_HOST

extern void gif_Sprite(SprRect *r, long long z, SprRect *uv, SprCol *col, int prim);

#else

extern void gif_Sprite(SprRect *r, unsigned int z, SprRect *uv, SprCol *col, int prim);

#endif

extern void gif_EndPacket(void);

static int barStep = 0; /* derived name */

static int barFrame = 0; /* derived name */

static void progressive_bar(void)
{
    int n;

    if (fbKeep != 0) {
        return;
    }
    if (systemStatus[7] <= 0) {
        return;
    }
    n = systemStatus[8];
    gif_StartPacketPri(12);
    gif_SetZWrite(0);
    gif_SetZTest(0);
    gif_SetAlpha(1, 2, 64);
    {
        SprRect frame = {80, 11, -160, 5};
        SprCol frameCol = {128, 128, 128, 32};

        gif_Sprite(&frame, 0xFFFFFFFF, 0, &frameCol, 1);
    }
    {
        SprRect back = {78, 12, -156, 3};
        SprCol backCol = {0, 0, 0, 32};

        gif_Sprite(&back, 0xFFFFFFFF, 0, &backCol, 1);
    }
    {
        int w = (float)barStep / (float)barTotal * -160.0f;
        SprRect bar = {-80 - w, 13, w, 1};
        SprCol barCol = {64, 255, 64, 32};

        gif_Sprite(&bar, 0xFFFFFFFF, 0, &barCol, 1);
    }
    gif_SetZTest(1);
    gif_SetZWrite(1);
    gif_SetAlpha(1, 4, 128);
    gif_EndPacket();
    if (n != barLastStep) {
        barLastStep = n;
        barFrame++;
    }
}

static int saveCardChanged = 1; /* derived name */

int la_mc_save_file_select(int first, int item)
{
    int r;

    if (first) {
        fileMask = 0;
        lastPort = filePort = curPort;
        saveSelectReady = 0;
        barTotal = 2;
        barStep = 0;
    }

    if (actionStarted != 0) {
        _la_set_preview_info();
        lt_mask_property(73, 1);
        lt_mask_property(72, 1);
        lt_mask_property(239, 0);
        lt_mask_property(240, 0);
    } else {
        lt_mask_property(73, 0);
        lt_mask_property(72, 0);
        lt_mask_property(239, 1);
        lt_mask_property(240, 1);
    }

    if (actionStarted != 0) {
        if (pad[0].flags & LA_BACK) {
            NEGATIVE_SE();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 28;
        }
    }

    if ((fileMask != 0 || saveSelectReady != 0) && (pad[0].flags & 0x40)) {
        POSITIVE_SE();
        selectFile = item;
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 34;
    }

    r = _la_set_current_port_lock_2(&mc, first);
    switch (r) {
    case -1:
        if (((curPortInfo->flags.w >> 1) & 1) == 0) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 31;
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 32;
    case 1:
        if (((mcPortInfo[filePort].flags.w >> 1) & 1) == 0) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 28;
        }
        if (fileMask != 0) {
            if (saveCardChanged == 0) {
                if (((curPortInfo->flags.w >> 2) & 1) != 0) {
                    lt_set_item_select_func(0);
                    actionStarted = 0;
                    return 18;
                }
            } else if (((curPortInfo->flags.w >> 2) & 1) == 0) {
                saveCardChanged = 0;
            }
        } else {
            saveCardChanged = (curPortInfo->flags.w >> 2) & 1;
            if ((curPortInfo->flags.w >> 3) & 1) {
                debug_StdPrintfDummy("format 2\n");
                saveSelectReady = r;
            } else if ((curPortInfo->flags.w & 0xA) == 2) {
                debug_StdPrintfDummy("unformat 2\n");
                if (filePort == curPort) {
                    actionStarted = r;
                }
                saveSelectReady = r;
                return -1;
            }
        }
        if (filePort == curPort) {
            fileMask = curPortInfo->fileMask;
            actionStarted = 1;
        }
        break;
    case 0:
    default:
        return -1;
    }
    return -1;
}

inline int la_save_confirm_no_memory_card(int first)
{
    if (PSH_POSITIVE_OR_NEGATIVE(0)) {
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x1C;
    }
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        if ((curPortInfo->flags.w >> 1) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x1E;
        }
        lock2Restart = 1;
        break;
    case 1:
        if ((curPortInfo->flags.w >> 1) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x1E;
        }
        break;
    }
    return -1;
}

inline int la_save_confirm_no_free_area(int first)
{
    if (PSH_POSITIVE_OR_NEGATIVE(0)) {
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x1C;
    }
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        if (((curPortInfo->flags.w >> 1) & 1) == 0) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x1F;
        }
        lock2Restart = 1;
        break;
    case 1:
        if ((curPortInfo->flags.w >> 4) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x1E;
        }
        break;
    }
    return -1;
}

int la_save_start_check(int first)
{
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        if (((curPortInfo->flags.w >> 1) & 1) == 0) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x1F;
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x1E;
    case 1:
        if (filePort != curPort) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x2C;
        }
        if ((curPortInfo->flags.w & 0xA) == 2) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x24;
        }
        if ((curPortInfo->fileMask >> selectFile) & 1) {
            if (IosMcProductFile[filePort].file[selectFile].stage != 0xFFFFFFFF) {
                lt_set_item_select_func(0);
                actionStarted = 0;
                return 0x23;
            }
            if (curPortInfo->fileMask != 0) {
                debug_StdPrintfDummy("already exist save data\n");
                lt_set_item_select_func(0);
                actionStarted = 0;
                return 0x26;
            }
        }
        debug_StdPrintfDummy("new save. system data making..\n");
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x27;
    }
    return -1;
}

int la_save_confirm_overwrite(int first, int item)
{
    if (first) {
        filePort = curPort;
    }
    fileMask = 0x3FF;
    _la_set_preview_info();
    if (lt_fade_status() == 2 && (pad[0].flags & LA_BACK)) {
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x21;
    }
    switch (item) {
    case 214:
    case 218:
        POSITIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x26;
    case 215:
    case 219:
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x21;
    }
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x1F;
    case 1:
        if (saveCardChanged == 0) {
            if (((curPortInfo->flags.w >> 2) & 1) == 0) {
                break;
            }
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x12;
        }
        if ((curPortInfo->flags.w >> 2) & 1) {
            break;
        }
        saveCardChanged = 0;
        if (filePort == curPort) {
            break;
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x1E;
    }
    return -1;
}

int la_format_confirm(int first, int item)
{
    if (first) {
        filePort = curPort;
    }
    if (lt_fade_status() == 2 && (pad[0].flags & LA_BACK)) {
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x21;
    }
    switch (item) {
    case 214:
    case 218:
        POSITIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x25;
    case 215:
    case 219:
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x21;
    }
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x1F;
    case 1:
        if (saveCardChanged == 0) {
            if (((curPortInfo->flags.w >> 2) & 1) == 0) {
                break;
            }
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x12;
        }
        if ((curPortInfo->flags.w >> 2) & 1) {
            break;
        }
        saveCardChanged = 0;
        if (filePort == curPort) {
            break;
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x1E;
    }
    return -1;
}

static int formatStep = 0; /* derived name */

inline int la_format_processing(int first)
{
    if (first) {
        formatStep = 0;
    }
    switch (formatStep) {
    case 0:
        mc.port = curPort;
        mc.slot = 0;
        iosMcFormat(&mc);
        formatStep++;
        break;
    case 1:
    case 3:
        if (iosMcSync(&mc) == 0) {
            break;
        }
        formatStep++;
        break;
    case 2:
        if (_la_mcard_error_check(&mc) > 0) {
            formatStep++;
            break;
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x2D;
    case 4:
        formatStep++;
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x27;
    }
    return -1;
}

/* the CD real-time clock record sceCdReadClock fills in, declared here as
   seki/src/GsBase.c declares it */
typedef struct sceCdCLOCK {
    unsigned char stat;
    unsigned char second;
    unsigned char minute;
    unsigned char hour;
    unsigned char pad;
    unsigned char day;
    unsigned char month;
    unsigned char year;
} sceCdCLOCK;

/* the save serial, the clock packed into one word or a random number when
   the clock cannot be read; inlined into la_system_save_processing */
static inline int mcMakeSerial(void) /* derived name */
{
    sceCdCLOCK clock;

    sceCdReadClock(&clock);
    if (clock.stat != 0) {
        return rand();
    }
    return ((clock.year & 7) << 26) + (clock.day << 20) + (clock.hour << 14) + (clock.minute << 7) +
           clock.second;
}

static int systemSaveStep = 0; /* derived name */

int la_system_save_processing(int first)
{
    int err;
    int i;
    int hour;
    int min;
    int sec;

    curPortInfo = &mcPortInfo[mc.port];
    if (first) {
        systemSaveStep = 0;
        systemSaveRetry = 0;
        barTotal = 14;
    }

    progressive_bar();

    switch (systemSaveStep) {
    case 0:
        strcpy(mc.path, "game.");
        mc.fileNo = systemSaveRetry;
        mc.port = curPort;
        mc.slot = 0;
        iosMcGetBlockSaveInfo(&mc);
        systemSaveStep++;
        curPortInfo->flags.w = (int)curPortInfo->flags.w & ~0x80;
        barStep++;
        break;
    case 2:
        iosMcSaveIconBlock(&mc);
        systemSaveStep++;
        break;
    case 6:
    case 9:
        err = _la_mcard_error_check(&mc);
        if (err > 0) {
            systemSaveStep++;
            debug_StdPrintfDummy("McSave phase:%d  %x\n", systemSaveStep, err);
            return -1;
        }
        curPortInfo->flags.w |= 0x80;
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 44;
    case 4:
        for (i = 0; i < 10; i++) {
            IosMcProductFile[mc.port].file[i].stage = 0xFFFFFFFF;
        }
        while ((IosMcProductFile[mc.port].serial = mcMakeSerial()) == 0)
            ;
        playTime((struct McPreview *)IosMcPreviewInfo, &hour, &min, &sec);
        iosMcSaveProductBlock(&mc);
        systemSaveStep++;
        barStep++;
        break;
    case 1:
    case 3:
    case 5:
    case 8:
        if (iosMcSync(&mc) != 0) {
            systemSaveStep++;
        }
        break;
    case 7:
        iosMcSaveGameBlock(&mc, gameSysMainSaveBuff);
        systemSaveStep++;
        barStep++;
        break;
    case 10:
        systemSaveStep = 7;
        systemSaveRetry++;
        mc.fileNo = systemSaveRetry;
        if (systemSaveRetry >= 10) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 38;
        }
        break;
    }
    return -1;
}

/* the save side's readback, the serial kept as both the saved and the
   current one; inlined into la_save_processing */
static inline void mcSetSavedFile(void) /* derived name */
{
    int serial = mcSetFileNo(mc.port, mc.fileNo);

    loadSerial = serial;
    saveSerial = serial;
}

static int saveStep = 0; /* derived name */

int la_save_processing(int first)
{
    int err;
    int hour;
    int min;
    int sec;

    curPortInfo = &mcPortInfo[mc.port];
    if (first) {
        fileMask = 0;
        saveStep = 0;
    }

    progressive_bar();

    switch (saveStep) {
    case 0:
        strcpy(mc.path, "game.");
        mc.fileNo = selectFile;
        mc.port = curPort;
        mc.slot = 0;
        iosMcGetBlockSaveInfo(&mc);
        saveStep++;
        curPortInfo->flags.w = (int)curPortInfo->flags.w & ~0x80;
        break;
    case 1:
        if (iosMcSync(&mc) != 0) {
            saveStep = 4;
        }
        break;
    case 2:
        iosMcSaveIconBlock(&mc);
        saveStep++;
        break;
    case 6:
    case 9:
        err = _la_mcard_error_check(&mc);
        if (err > 0) {
            saveStep++;
            debug_StdPrintfDummy("McSave phase:%d  %x\n", saveStep, err);
            return -1;
        }
        curPortInfo->flags.w |= 0x80;
        debug_StdPrintfDummy("save error? %d\n", saveStep);
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 44;
    case 4:
        IosMcPreviewInfo[0] = stage_no;
        IosMcPreviewInfo[3] = GetSaveSofaLayoutID();
        IosMcPreviewInfo[1] = gFlagGameClear;
        *(struct McPreview *)&IosMcProductFile[mc.port].file[mc.fileNo] =
            *(struct McPreview *)IosMcPreviewInfo;
        playTime((struct McPreview *)IosMcPreviewInfo, &hour, &min, &sec);
        (IosMcProductFile + mc.port)->fileNo = mc.fileNo;
        if ((IosMcProductFile + mc.port)->serial == (IosMcProductFile + (mc.port ^ 1))->serial) {
            do {
                while ((IosMcProductFile[mc.port].serial = mcMakeSerial()) == 0)
                    ;
            } while ((IosMcProductFile + mc.port)->serial ==
                     (IosMcProductFile + (mc.port ^ 1))->serial);
        }
        mcSetSavedFile();
        iosMcSaveProductBlock(&mc);
        saveStep++;
        break;
    case 3:
    case 5:
    case 8:
        if (iosMcSync(&mc) != 0) {
            saveStep++;
            barStep++;
        }
        break;
    case 7:
        CheckPoint();
        iosMcSaveGameBlock(&mc, gameSysMainSaveBuff);
        saveStep++;
        break;
    case 10:
#ifdef ICO_HOST
        ico_mirror_slot_saved(mc.fileNo, (unsigned int)mc.sum); /* R7c: the run's flag */
#endif
        saveStep = 0;
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 41;
    }
    return -1;
}

inline int la_save_confirm_complete(int first, int item)
{
    if (first) {
        previewInfo = *(struct McPreview *)IosMcPreviewInfo;
        fileMask = 0x3FF;
        _la_set_preview_info();
        debug_StdPrintfDummy("save complete %d %d\n", fileMask, curFile);
    }
    if (item != -1) {
        debug_StdPrintfDummy("%d %d %d\n", 0x108, 0x109, item);
    }
    switch (item) {
    case 0x108:
        systemStatus[5] = 0;
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x36;
    case 0x109:
        nextStage = 1;
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x3F;
    }
    return -1;
}

int la_end_confirm(void)
{
    int item;

    if (lt_fade_status() == 2 && (pad[0].flags & LA_BACK)) {
        NEGATIVE_SE();
        item = lt_current_property_item();
        if (item >= 270) {
            if (item < 272) {
                lt_set_item_select_func(0);
                actionStarted = 0;
                return 0x29;
            }
            if (item < 426) {
                if (item >= 424) {
                    lt_set_item_select_func(0);
                    actionStarted = 0;
                    return 0x39;
                }
            }
        }
    }
    if (pad[0].flags & 0x40) {
        debug_StdPrintfDummy("%d\n", lt_current_property_item());
        switch (lt_current_property_item()) {
        case 270:
        case 424:
            POSITIVE_SE();
            optionScreenMode = 0;
            gflagInit();
            fightSoundProcessRequestPause();
            fightSoundClose();
            soundDataSegAllClose(0, 2);
            nextStage = 1;
            stgmgrForceSwitchWithFade(1, 0.025f, 4.0f);
        case 271:
            NEGATIVE_SE();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x29;
        case 425:
            NEGATIVE_SE();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x39;
        }
    }
    return -1;
}

inline int la_save_confirm_yesno(void)
{
    if (pad[0].flags & 0x10) {
        return lt_current_property_item();
    }
    return -1;
}

inline int la_save_confirm_fail(void)
{
    return -1;
}

inline int la_format_confirm_fail(void)
{
    return -1;
}

inline int la_delete_start_check(int first)
{
    switch (_la_set_current_port_2(&mc, first)) {
    case 0:
        break;
    case -1:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x1E;
    case 1:
        if ((curPortInfo->fileMask >> selectFile) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x31;
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x21;
    }
    return -1;
}

inline int la_delete_confirm(int first, int item)
{
    switch (item) {
    case 0xD6:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x32;
    case 0xD7:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x1E;
    }
    return -1;
}

static int deleteStep = 0; /* derived name */

int la_delete_processing(int first)
{
    if (first) {
        mc.fileNo = selectFile;
        deleteStep = 2;
    }
    switch (deleteStep) {
    case 2:
        strcpy(mc.path, mc.dir[mc.fileNo].EntryName);
        iosMcDelete(&mc);
        deleteStep++;
        break;
    case 3:
        if (iosMcSync(&mc) == 0) {
            break;
        }
        deleteStep++;
        break;
    case 4:
        if (_la_mcard_error_check(&mc) == 0) {
            break;
        }
        deleteStep++;
        break;
    case 5:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x33;
    }
    return -1;
}

inline int la_delete_confirm_complete(void)
{
    int ret;
    if ((pad[0].flags & LA_BACK) == 0)
        goto fail;
    lt_set_item_select_func(0);
    actionStarted = 0;
    ret = 0x1E;
    goto out;
fail:
    ret = -1;
out:
    return ret;
}

inline int la_delete_confirm_fail(void)
{
    return -1;
}

inline int la_game_loading(int first)
{
    if (first != 0) {
        systemStatus[6] = 1;
    }
    return -1;
}

inline void la_playtime_count(void)
{
    if (systemStatus[5] == 0) {
        IosMcPreviewInfo[2]++;
    }
}

/* inlined into la_game_loop and into la_game_over_continue */
static inline void releaseGameLoopCursor(void) /* derived name */
{
    if (layoutVoice != 0 && layoutVoice->stream != 0) {
        layoutVoice->stream->fadeStep = 0x100;
    }
    layoutVoice = 0;
}

int laoutActionPauseRequest = 0;

int la_game_loop(int first)
{
    if (first) {
        if (fightSoundStopped != 0) {
            fightSoundProcessRequestStart();
            fightSoundStopped = 0;
        }
        releaseGameLoopCursor();
        systemStatus[2] = 1;
        systemStatus[5] = 0;
        iosPadEnable();
        isysGObjActiveLink(0, 1);
        layoutActPushStartNew = 0;
    }
    if (layoutActPushStartNew != 0) {
        laoutActionPauseRequest++;
    } else {
        laoutActionPauseRequest = 0;
    }
    if ((startStagePauseDisableTimer >= 11 && (pad[0].flags & 0x800)) ||
        (float)laoutActionPauseRequest >
            (float)((60 - systemStatus[0] * 10) / systemStatus[1]) * 0.5f) {
        if (enable_game_pause == 0) {
            return -1;
        }
        if (gflagChk(338) != 0) {
            return -1;
        }
        systemStatus[5] = 1;
        layoutActPushStartNew = 0;
        adpcmPauseRequest(1);
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x39;
    }
    return -1;
}

inline int la_game_demo_pause(int first)
{
    if (first) {
        systemStatus[5] = 1;
    }
    if ((pad[0].flags & 0x800) == 0) {
        return -1;
    }
    lt_set_item_select_func(0);
    actionStarted = 0;
    return 0x37;
}

inline int la_game_demo(int first)
{
    if (first) {
        if (stage_no == 1) {
            iosPadDisable();
        }
    }
    if ((pad[0].flags & 0x800) && gflagChk(388)) {
        debug_StdPrintfDummy("push start\n");
        gflagOff(388);
        title_demo_mode ^= 1;
        nextStage = stage_after_skipping_demo;
        stgmgrForceSwitchWithFade(nextStage, 8.0f, 4.0f);
        if (stage_after_skipping_demo == 0xFFFFFFFF) {
            stage_after_skipping_demo = 1;
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x3F;
    }
    return -1;
}

inline int la_game_pause(int first)
{
    if (first) {
        systemStatus[5] = 1;
        iosPadActStopAll();
        texLayout[58].defaultItem = 308;
    }
    if (lt_fade_status() != 2) {
        return -1;
    }
    if (((pad[0].flags & 0x40) && lt_current_property_item() == 0x127) ||
        (pad[0].flags & (0x800 | LA_BACK))) {
        NEGATIVE_SE(0);
        adpcmPauseRequest(0);
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x36;
    }
    return -1;
}

static int gameOverVoice = 0; /* derived name */

static int gameOverVoiceOpened = 0; /* derived name */

int la_game_over_continue(int first)
{
    if (first) {
        if (fadeStatus != 0) {
            scpFadeIn(3.0f);
        }
        enable_game_pause = 1;
        iosPadEnable();
        scpBoyControlReadDisable = 0;
        systemStatus[5] = 1;
        AdpcmFadeCloseAll(0x200);
        AdpcmNotUseIopAreaFree();
        soundSePlayModeStop(1);
        soundSePlayModeStop(0);
        iosPadActStopAll();
        gameOverVoiceOpened = 0;
    } else if (gameOverVoiceOpened == 0) {
        if (AdpcmFreeAreaGet() != 0) {
            gameOverVoice = openLayoutVoice(0x34);
            gameOverVoiceOpened = 1;
        }
    } else if (gameOverVoice != 0) {
        layoutVoice = soundDataOpenSync(&voiceOpenReq);
        if (layoutVoice != (SqEntry *)ICO_INVALID_PTR) {
            gameOverVoice = 0;
            if (layoutVoice != 0) {
                AdpcmPlay(layoutVoice->stream);
                return -1;
            }
        }
    } else {
        switch (lt_current_property_item()) {
        case 0x1AD:
            if ((pad[0].flags & 0x40) == 0 || lt_fade_status() != 2) {
                return -1;
            }
            POSITIVE_SE();
            systemStatus[4] = 1;
            nextStage = gFlagSaveStage;
            releaseGameLoopCursor();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x3F;
        case 0x1AE:
            if ((pad[0].flags & 0x40) == 0 || lt_fade_status() != 2) {
                return -1;
            }
            POSITIVE_SE();
            optionScreenMode = 0;
            gflagInit();
            nextStage = 1;
            releaseGameLoopCursor();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 0x3F;
        }
    }
    return -1;
}

inline int la_switching_stage(void)
{
    if (fightSoundPlayChk() == 0) {
        stgmgrForceSwitchWithFade(nextStage, 0.4f, 4.0f);
    }
    return -1;
}

/* the key-config property sweep, inlined into la_key_config twice */
static inline void keyconfigMaskAll(void) /* derived name */
{
    int i;

    for (i = 0; i < 48; i++) {
        lt_mask_property(i + 342, 1);
        lt_default_mask_property(i + 342, 1);
    }
}

/* the lowest set bit of v, -1 for none */
static inline int keyBitIndex(int v) /* derived name */
{
    int i;

    for (i = 0; i < 16; i++) {
        if ((v >> i) & 1) {
            return i;
        }
    }
    return -1;
}

/* the index of pad code v in keyConfigCode, -1 for none */
static inline int keyCodeIndex(int v) /* derived name */
{
    int i;

    for (i = 0; i < 8; i++) {
        if (v == keyConfigCode[i]) {
            return i;
        }
    }
    return -1;
}

/* the slot assigned pad code v, -1 for none */
static inline int keyAssignIndex(int v) /* derived name */
{
    int i;

    for (i = 0; i < 8; i++) {
        if (v == keyConfigCode[keyConfigSlot[i]]) {
            return i;
        }
    }
    return -1;
}

static int keyConfigMask = 0xFF; /* derived name */

int la_key_config(int first)
{
    int i;
    int sel;
    int m;
    int k;
    int n;

    sel = lt_current_property_item() - 336;
    if (first) {
        texLayout[58].defaultItem = 323;
        for (i = 0; i < 16; i++) {
            if ((keyConfigMask >> i) & 1) {
                keyConfigSlot[keyCodeIndex(iosPadConfCustom.bit[i])] = keyCodeIndex(1 << i);
            }
        }
    }
    if (sel >= 0 && sel < 6) {
        m = pad[0].flags & keyConfigMask;
        if (m != 0 && m == pad[0].flags) {
            k = keyCodeIndex(m);
            POSITIVE_SE();
            if (k != -1) {
                n = keyAssignIndex(m);
                if (n != -1) {
                    keyConfigSlot[n] = keyConfigSlot[sel];
                }
                keyConfigSlot[sel] = k;
            }
        }
    }
    keyconfigMaskAll();
    for (i = 0; i < 6; i++) {
        lt_mask_property(i * 8 + 342 + keyConfigSlot[i], 0);
        lt_default_mask_property(i * 8 + 342 + keyConfigSlot[i], 0);
    }
    if (pad[0].flags & 0x40) {
        if (lt_current_property_item() == 391) {
            for (i = 7; i >= 0; i--) {
                keyConfigSlot[i] = i;
            }
            NEGATIVE_SE();
        }
        if (lt_current_property_item() == 390) {
            POSITIVE_SE();
            for (i = 0; i < 16; i++) {
                if ((keyConfigMask >> i) & 1) {
                    iosPadConfCustom.bit[i] = 0;
                } else {
                    iosPadConfCustom.bit[i] = 1 << i;
                }
            }
            for (i = 0; i < 8; i++) {
                iosPadConfCustom.bit[keyBitIndex(keyConfigCode[keyConfigSlot[i]])] =
                    keyConfigCode[i];
            }
            keyconfigMaskAll();
            for (i = 0; i < 6; i++) {
                lt_default_mask_property(i * 8 + 342 + keyConfigSlot[i], 0);
                lt_mask_property(i * 8 + 342 + keyConfigSlot[i], 0);
            }
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 58;
        }
    }
    return -1;
}

/* the option screen's layout items: the five screen modes, the stage
   animation each mode plays (-1 for none), and the two choices of the control
   type (item 318) followed by the two of the setting item 325 toggles, in one
   table; the code reaches the second pair through an offset pointer */
static const int screenModeItem[5] = {303, 304, 305, 306, 307}; /* derived name */

static const int screenModeAnim[5] = {-1, 67, 68, 69, 70}; /* derived name */

static const int choiceItem[4] = {321, 322, 328, 329}; /* derived name */

int la_game_option(void)
{
    int mode;
    int cur;
    int sel;
    int item;

    mode = soundOutputModeGet();
    lt_analog2Pad();
    if ((pad[0].now & 0xA000) != 0) {
        cur = optionControlType;
        sel = optionScreenMode;
        switch (lt_current_property_item()) {
        case 300:
            if ((pad[0].flags & 0x8000) != 0) {
                sel--;
                if (sel < 0) {
                    sel = 4;
                }
                CUR_SE();
            } else if ((pad[0].flags & 0x2000) != 0) {
                sel++;
                if (sel >= 5) {
                    sel = 0;
                }
                CUR_SE();
            }
            break;
        case 318:
            if ((pad[0].flags & 0xA000) != 0) {
                cur = cur == 0;
                CUR_SE();
            }
            break;
        case 313:
            if ((pad[0].flags & 0xA000) != 0) {
                iosPadActRequestEnable = iosPadActRequestEnable == 0;
                CUR_SE();
            }
            break;
        case 308:
            if ((pad[0].flags & 0xA000) != 0) {
                if (mode == 1) {
                    mode = 0;
                } else {
                    mode = 1;
                }
                soundOutputModeSet(mode);
                CUR_SE();
            }
            break;
        case 325:
            if ((pad[0].flags & 0xA000) != 0) {
                girlControlMode = girlControlMode == 0;
                CUR_SE();
            }
            break;
        }
        if (sel != optionScreenMode) {
            if (screenModeAnim[optionScreenMode] != -1) {
                stage_SetLoopFlag(screenModeAnim[optionScreenMode], 0);
                stage_SetAnimation(screenModeAnim[optionScreenMode], -1, -2);
            }
            item = screenModeAnim[sel];
            if (item != -1) {
                stage_SetLoopFlag(item, 1);
                stage_SetAnimation(item, 1, 0);
            }
            optionScreenMode = sel;
        }
        optionControlType = cur;
    }
    for (sel = 0; sel < 5; sel++) {
        lt_default_mask_property(screenModeItem[sel], 1);
    }
    lt_default_mask_property(screenModeItem[optionScreenMode], 0);
    for (sel = 0; sel < 2; sel++) {
        lt_default_mask_property(choiceItem[sel], 1);
    }
    lt_default_mask_property(choiceItem[optionControlType], 0);
    if ((pad[0].flags & 0x40) != 0) {
        if (lt_current_property_item() == 330) {
            NEGATIVE_SE();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 57;
        }
    }
    if (mode == 0) {
        lt_default_mask_property(311, 0);
        lt_default_mask_property(312, 1);
    } else {
        lt_default_mask_property(311, 1);
        lt_default_mask_property(312, 0);
    }
    if (iosPadActRequestEnable != 0) {
        lt_default_mask_property(316, 0);
        lt_default_mask_property(317, 1);
    } else {
        lt_default_mask_property(316, 1);
        lt_default_mask_property(317, 0);
    }
    for (sel = 0; sel < 2; sel++) {
        lt_default_mask_property((choiceItem + 2)[sel], 1);
    }
    lt_default_mask_property((choiceItem + 2)[girlControlMode], 0);
    return -1;
}

/* inlined once, into la_adjust_screen */
static inline void clearAdjustScreenMarks(void) /* derived name */
{
    int i;

    for (i = 0; i < 15; i++) {
        lt_default_mask_property(i + 395, 1);
    }
}

int la_adjust_screen(void)
{
    int v;

    texLayout[58].defaultItem = 324;
    lt_analog2Pad();
    if (pad[0].flags & 0x8000) {
        v = systemStatus[11];
        if (v > 0) {
            CUR_SE();
            systemStatus[11] = v - 1;
        }
    } else if (pad[0].flags & 0x2000) {
        v = systemStatus[11];
        if (v < 14) {
            CUR_SE();
            systemStatus[11] = v + 1;
        }
    }
    if (pad[0].flags & 0x10) {
        NEGATIVE_SE();
        systemStatus[11] = 7;
    }
    clearAdjustScreenMarks();
    lt_default_mask_property(systemStatus[11] + 395, 0);
    if (pad[0].flags & 0x40) {
        POSITIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 0x3A;
    }
    return -1;
}

#ifdef ICO_HOST

/* PC port (6C): what every proc does before it returns the layout it
   switches to, for the port's Settings procs (actionStarted is this file's) */
void la_host_leave(void)
{
    lt_set_item_select_func(0);
    actionStarted = 0;
}

#endif

unsigned int stage_after_skipping_demo = 0;

int layoutActPushStartNew = 0;
