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

#include "ee_view.h"
#include "frameDependSequence.h"
#include "matrixDrive.h"

/* PC port: the menus copy a save's preview whole out of and into the card's
   product file (McFileInfo); the host layouts must agree
   (tools/template_audit.py) */
ICO_LAYOUT_AT(struct McPreview, stage, McFileInfo, stage);

ICO_LAYOUT_AT(struct McPreview, cleared, McFileInfo, cleared);

ICO_LAYOUT_AT(struct McPreview, playTime, McFileInfo, playTime);

ICO_LAYOUT_AT(struct McPreview, sofa, McFileInfo, sofa);

ICO_LAYOUT_AT(struct McPreview, word10, McFileInfo, word10);

ICO_LAYOUT_SIZE(struct McPreview, McFileInfo);

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

/* pad idx's trigger bit for the action, with the cursor sound it plays:
   circle (decide), triangle (cancel), left and right */
inline int PSH_POSITIVE(int idx) /* derived name */
{
    if (pad[idx].flags & 0x40) {
        ExecuteSEPackage(0, 91);
        return 1;
    }
    return 0;
}

int lt_ext_back_buttons(void);

/* PC port: the back buttons are the port's (Triangle and its aliases, port/ui/layout_ext.h) */
inline int PSH_NEGATIVE(int idx) /* derived name */
{
    if (pad[idx].flags & lt_ext_back_buttons()) {
        ExecuteSEPackage(0, 92);
        return 1;
    }
    return 0;
}

inline int PSH_LEFT(int idx) /* derived name */
{
    if (pad[idx].flags & 0x8000) {
        ExecuteSEPackage(0, 90);
        return 1;
    }
    return 0;
}

inline int PSH_RIGHT(int idx) /* derived name */
{
    if (pad[idx].flags & 0x2000) {
        ExecuteSEPackage(0, 90);
        return 1;
    }
    return 0;
}

void POSITIVE_SE(void)
{
    soundSeDefPlay(PAL_NTSCAUG6JUN26(412, 400), 0xFFFFFFFE, 0, 0);
}

void NEGATIVE_SE(void)
{
    soundSeDefPlay(PAL_NTSCAUG6JUN26(413, 401), 0xFFFFFFFE, 0, 0);
}

void CUR_SE(void)
{
    soundSeDefPlay(PAL_NTSCAUG6JUN26(411, 399), 0xFFFFFFFE, 0, 0);
}

inline int PSH_POSITIVE_OR_NEGATIVE(int idx)
{
    int v = pad[idx].flags;
    if ((v & PAL_JPNKORCN(0x40, 0x20)) != 0)
        goto one;
    if ((v & PAL_JPNKORCN(0x10, 0x40)) == 0)
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

/* the busy mark progressive_bar draws beside its bar: a line through a
   7-by-7 box, one of four turns a step (its two ends per turn) */
static sceVu0IVECTOR barLine[4][2] = {{{0, 3, 0, 0}, {7, 3, 0, 0}},
                                      {{0, 0, 0, 0}, {7, 7, 0, 0}},
                                      {{3, 0, 0, 0}, {3, 7, 0, 0}},
                                      {{7, 0, 0, 0}, {0, 7, 0, 0}}}; /* derived name */

/* .data: the game-flag ids the load carries across gflagInit.  The list
   holds five ids and the key-config tables follow it; the keep/restore loops
   below walk twenty words. */

/* The keep/restore loops read twenty words from keepFlagNo: the five ids, then
   keyConfigCode and the first seven keyConfigSlot, which follow it in the EE's
   .data (so a load also keeps those game flag ids and the key-config slots).  The host holds the three as one
   array so the walk is in bounds and reads the same words. */
static int keepWords[21] = {388, 384, 383, 385, 382, 16, 128, 32, 64, 8, 2,
                            1,   4,   1,   2,   3,   4,  5,   0,  0,  0};

#define keepFlagNo keepWords
#define keyConfigCode (keepWords + 5)
#define keyConfigSlot (keepWords + 13)

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

/* PC port: the Settings menu (port/ui/settings.h).  Its entry rows are chained after the title's
   rows; the title procs leave Cross/START on them to default_item_select,
   which opens the menu through the row's right link, and mask them with
   their own rows while the memory card check runs. */
int ui_settings_entry_item(int item);
void ui_settings_title_mask(int masked);
/* la_key_config's OK goes back to Settings > Controls (settings.h) */
int ui_settings_key_config_back(void);

#define LA_HOST_NOT_SETTINGS_ROW &&!ui_settings_entry_item(lt_current_property_item())

/* PC port: the bits of the
   procs' Triangle cancels.  With [game] circle_back on (the default) Circle
   is an alias of Triangle there (port/ui/layout_ext.h lt_ext_back_buttons);
   off, 0x10 alone, the PS2's checks.  Not used where Triangle is not a
   cancel (la_adjust_screen: it resets the brightness) or where Circle has
   a meaning of its own (la_key_config: it is one of the buttons assigned). */
int lt_ext_back_buttons(void);

#define LA_BACK lt_ext_back_buttons()

/* PC port: mirror mode, chosen on a port screen after
   the vibration choice (port/ui/settings.h ui_new_game_screen_*) and kept per
   save slot in the port config (port/game/options.h ico_mirror_slot_*). */
int ui_new_game_screen_enter(void);
int ui_new_game_screen_layout(void);
int ico_mirror_slot_saved(int slot, unsigned int sum);
int ico_mirror_slot_loaded(int slot, unsigned int sum);
/* port/game/achievements.h: the run state kept per save slot */
void ico_ach_slot_saved(int slot, unsigned int sum);
int ico_ach_slot_loaded(int slot, unsigned int sum);
void ico_opt_mirror_reset(void);
/* port/game/options.h: the Stereo/Mono row's choice, for [audio] output */
void ico_opt_output_toggled(int mode);
/* PC port: the Hold type row's choice, for [gameplay] hold_type (port/game/options.h) */
void ico_opt_hold_type_toggled(int type);
void la_host_new_game_go(void);
int la_host_mc_slot_pickable(int slot);

/* la_vibe_select's confirm: the New Game screen in place of the start, which
   its confirm runs (la_host_new_game_go, after la_vibe_select) */
static int la_host_new_game_screen(void)
{
    lt_set_item_select_func(0);
    actionStarted = 0;
    return ui_new_game_screen_enter();
}

static int fightSoundStopped = 0; /* derived name */

static SqEntry *layoutVoice = 0; /* derived name */

int startStagePauseDisableTimer = 0;

int layout_boot_flag = 0;

int enable_game_pause = 1;

/* the current port and port 0's card flags, printed; nothing calls it */
void _la_print_port_flags(void) /* derived name */
{
    debug_StdPrintfDummy("current:%d ec:%d  p2:%d  fo:%d  fr:%d  ed:%d\n", curPort,
                         mcPortInfo[0].flags.bit.card, mcPortInfo[0].flags.bit.ps2Card,
                         mcPortInfo[0].flags.bit.formatted, mcPortInfo[0].flags.bit.hasSpace,
                         mcPortInfo[0].flags.bit.hasSave);
}

static int _la_memory_card_check_jun26(McMgr *p, int step)
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
        if (p->free >= 400) {
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
        if (p->dirCount == 10) {
            curPortInfo->flags.w |= 0x20;
            curPortInfo->fileMask = p->mask;
        }
        break;
    }
    return step;
}

static int _la_memory_card_check(McMgr *p, int step)
{
    if (ICO_JUN26) {
        return _la_memory_card_check_jun26(p, step);
    }
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
        if (!ICO_NTSCAUG6JUN26) {
            (IosMcProductFile + p->port)->serial = 0;
            (IosMcProductFile + p->port)->fileNo = 0;
        }
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

static int _la_set_current_port_new_jun26(McMgr *p, int first)
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
        v = ((mcPortInfo[0].flags.w >> 1) & 1) && ((mcPortInfo[1].flags.w >> 1) & 1);
        mcPortInfo[0].flags.bit.bothPs2 = mcPortInfo[1].flags.bit.bothPs2 = v;
        v = ((mcPortInfo[0].flags.w >> 5) & 1) && ((mcPortInfo[1].flags.w >> 5) & 1);
        mcPortInfo[0].flags.bit.bothSave = mcPortInfo[1].flags.bit.bothSave = v;
        if (port0LockResult == 1) {
            curPort = 0;
            r = 1;
        } else if (port1LockResult == 1) {
            curPort = 1;
            r = 1;
        } else {
            if ((mcPortInfo[0].flags.w & 1) == 0 && mcPortInfo[1].flags.bit.card == 1) {
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

static int _la_set_current_port_new(McMgr *p, int first)
{
    if (ICO_JUN26) {
        return _la_set_current_port_new_jun26(p, first);
    }
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
    return PAL_JPNKORCN_NTSCAUG6JUN26(0x37, 0x34, 0x33);
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

static inline int la_boot_confirm_memory_card_jun26(void)
{
    if (PSH_POSITIVE(0)) {
        return lt_link_layout(0);
    }
    return -1;
}

inline int la_boot_confirm_memory_card(void)
{
    if (ICO_JUN26) {
        return la_boot_confirm_memory_card_jun26();
    }
    if (pad[0].flags & PAL_JPNKORCN(0x40, 0x20)) {
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

static int la_vibe_select_ntsc(void)
{
    if (lt_fade_status() == 2 && (pad[0].flags & PAL_JPNKORCN(0x840, 0x820))) {
        soundSeDefPlay(403, 0xFFFFFFFF, 0, 0);
        switch (
            lt_current_property_item() +
            PAL_JPNCN_KOR(0, 30, 36)) { /* JPN property ids 30 lower, KOR property ids 36 lower */
        case 8:
            iosPadActRequestEnable = 1;
            break;
        case 9:
            iosPadActRequestEnable = 0;
            break;
        }
        if (ui_new_game_screen_layout() >= 0) {
            return la_host_new_game_screen(); /* the New Game screen, then the start */
        }
        if (titleAdpcm != 0) {
            titleAdpcm->stream->fadeStep = 0x80;
        }
        titleAdpcm = 0;
        gflagInit();
        keyconfig_reset();
        systemStatus[4] = 0;
        gflagOn(349);
        return -1;
    }
    if (lt_fade_status() != 2) {
        return -1;
    }
    if ((pad[0].flags & PAL_JPNKORCN(LA_BACK, 0x40)) == 0) {
        return -1;
    }
    NEGATIVE_SE();
    lt_set_item_select_func(0);
    actionStarted = 0;
    return 9;
}

inline static int la_vibe_select_jun26(void)
{
    if (lt_fade_status() == 2 && (pad[0].flags & 0x40)) {
        soundSeDefPlay(403, 0xFFFFFFFF, 0, 0);
        switch (lt_current_property_item() +
                0) { /* JPN property ids 30 lower, KOR property ids 36 lower */
        case 8:
            iosPadActRequestEnable = 1;
            break;
        case 9:
            iosPadActRequestEnable = 0;
            break;
        }
        if (ui_new_game_screen_layout() >= 0) {
            return la_host_new_game_screen(); /* the New Game screen, then the start */
        }
        gflagInit();
        systemStatus[4] = 0;
        gflagOn(333);
        return -1;
    }
    if (PSH_NEGATIVE(0)) {
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 9;
    }
    return -1;
}

static int la_vibe_select_aug6(void)
{
    if (lt_fade_status() == 2 && (pad[0].flags & PAL_JPNKORCN(0x840, 0x820))) {
        soundSeDefPlay(403, 0xFFFFFFFF, 0, 0);
        switch (
            lt_current_property_item() +
            PAL_JPNCN_KOR(0, 30, 36)) { /* JPN property ids 30 lower, KOR property ids 36 lower */
        case 8:
            iosPadActRequestEnable = 1;
            break;
        case 9:
            iosPadActRequestEnable = 0;
            break;
        }
        if (ui_new_game_screen_layout() >= 0) {
            return la_host_new_game_screen(); /* the New Game screen, then the start */
        }
        if (titleAdpcm != 0) {
            titleAdpcm->stream->fadeStep = 0x80;
        }
        titleAdpcm = 0;
        gflagInit();
        keyconfig_reset();
        systemStatus[4] = 0;
        gflagOn(348);
        return -1;
    }
    if (lt_fade_status() != 2) {
        return -1;
    }
    if ((pad[0].flags & PAL_JPNKORCN(LA_BACK, 0x40)) == 0) {
        return -1;
    }
    NEGATIVE_SE();
    lt_set_item_select_func(0);
    actionStarted = 0;
    return 9;
}

int la_vibe_select(void)
{
    if (ICO_JUN26) {
        return la_vibe_select_jun26();
    }
    if (ICO_NTSC) {
        return la_vibe_select_ntsc();
    }
    if (ICO_AUG6) {
        return la_vibe_select_aug6();
    }
    if (lt_fade_status() == 2 && (pad[0].flags & PAL_JPNKORCN(0x840, 0x820))) {
        soundSeDefPlay(415, 0xFFFFFFFF, 0, 0);
        switch (
            lt_current_property_item() +
            PAL_JPNCN_KOR(0, 30, 36)) { /* JPN property ids 30 lower, KOR property ids 36 lower */
        case 44:
            iosPadActRequestEnable = 1;
            break;
        case 45:
            iosPadActRequestEnable = 0;
            break;
        }
        if (ui_new_game_screen_layout() >= 0) {
            return la_host_new_game_screen(); /* the New Game screen, then the start */
        }
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
    if ((pad[0].flags & PAL_JPNKORCN(LA_BACK, 0x40)) == 0) {
        return -1;
    }
    NEGATIVE_SE();
    lt_set_item_select_func(0);
    actionStarted = 0;
    return PAL_JPNKORCN(0xC, 0x9);
}

/* PC port: the start la_vibe_select's confirm made,
   run by the New Game screen's confirm (port/ui/settings.c) once the player
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

/* PC port: whether the mouse may pick file slot 0..9 on the memory card
   screen, even when the slot is empty.  The SAVE screen installs no filter
   on the cursor and its Cross saves into an empty slot, so an empty slot's
   picture is as good a target as a used one's; the LOAD screen
   (la_mc_saved_file_select) steps the cursor past empty slots and ignores
   Cross on them, so there the mouse gets no more than the pad */
int la_host_mc_slot_pickable(int slot)
{
    return mcLoadMode == 0 && slot >= 0 && slot < 10;
}

inline int la_scei_logo(int first)
{
    if (first) {
        stgmgrNextStagePreLoadForceStageSet(0);
        logoIcoMiscLock = PAL_AUG6JUN26(lock_execIcoMisc, GlobalTimer);
        systemStatus[5] = 1;
        iosPadEnable();
        isysGObjActiveLink(0, 0);
        gflagOff(PAL_NTSC_AUG6_JUN26(386, 353, 352, 337));
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

static int la_title_continue_or_new_ntsc(int first)
{
    if (first) {
        opTitleLogoMode = 1;
        continueIcoMiscLock = lock_execIcoMisc;
        iosPadEnable();
        isysGObjActiveLink(0, 1);
        systemStatus[5] = 0;
        gflagOff(349);
        ico_opt_mirror_reset(); /* the title belongs to no run */
        if (gflagChk(352) == 0) {
            gflagOn(352);
        }
        continueDecided = 0;
        lt_continue_selected = 0;
    }
    if (continueDecided != 0) {
        lt_item_select_disable = 0;
        lt_mask_property(14, 0);
        lt_mask_property(15, 0);
    } else {
        lt_item_select_disable = 1;
        lt_mask_property(14, 1);
        lt_mask_property(15, 1);
    }
    ui_settings_title_mask(continueDecided == 0);
    /* PC port: not on the Settings row, whose Cross default_item_select
       takes (it opens the menu) */
    if (continueDecided != 0 && (pad[0].flags & PAL_JPNKORCN(0x840, 0x820)) &&
        lt_fade_status() == 2 LA_HOST_NOT_SETTINGS_ROW) {
        lt_continue_selected = 1;
        soundSeDefPlay(402, 0xFFFFFFFF, 0, 0);
        switch (
            lt_current_property_item() +
            PAL_JPNCN_KOR(0, 29, 35)) { /* JPN property ids 29 lower, KOR property ids 35 lower */
        case 14:
            opTitleLogoMode = 2;
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 17;
        case 15:
            opTitleLogoMode = 2;
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 6;
        }
    }
    switch (_la_set_current_port_2(&mc, first)) {
    case 0:
        break;
    case 1:
        if ((curPortInfo->flags.w & 0x22) == 2) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 10;
        }
        continueDecided = 1;
        break;
    case -1:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 10;
    }
    return -1;
}

static int la_title_continue_or_new_jun26(int first)
{
    if (first) {
        continueIcoMiscLock = GlobalTimer;
        iosPadEnable();
        isysGObjActiveLink(0, 1);
        systemStatus[5] = 0;
        gflagOff(333);
        ico_opt_mirror_reset(); /* the title belongs to no run */
        if (gflagChk(336) == 0) {
            gflagOn(336);
        }
        continueDecided = 0;
    }
    if (continueDecided != 0) {
        lt_item_select_disable = 0;
        lt_mask_property(14, 0);
        lt_mask_property(15, 0);
    } else {
        lt_item_select_disable = 1;
        lt_mask_property(14, 1);
        lt_mask_property(15, 1);
    }
    ui_settings_title_mask(continueDecided == 0);
    /* PC port: not on the Settings row, whose Cross default_item_select
       takes (it opens the menu) */
    if (continueDecided != 0 && (pad[0].flags & 0x840) &&
        lt_fade_status() == 2 LA_HOST_NOT_SETTINGS_ROW) {
        soundSeDefPlay(402, 0xFFFFFFFF, 0, 0);
        switch (lt_current_property_item() +
                0) { /* JPN property ids 29 lower, KOR property ids 35 lower */
        case 14:
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 17;
        case 15:
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 6;
        }
    }
    switch (_la_set_current_port_2(&mc, first)) {
    case 0:
        break;
    case 1:
        if ((curPortInfo->flags.w & 0x22) == 2) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 10;
        }
        continueDecided = 1;
        break;
    case -1:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 10;
    }
    return -1;
}

static int la_title_continue_or_new_aug6(int first)
{
    if (first) {
        opTitleLogoMode = 1;
        continueIcoMiscLock = GlobalTimer;
        iosPadEnable();
        isysGObjActiveLink(0, 1);
        systemStatus[5] = 0;
        gflagOff(348);
        ico_opt_mirror_reset(); /* the title belongs to no run */
        if (gflagChk(351) == 0) {
            gflagOn(351);
        }
        continueDecided = 0;
        lt_continue_selected = 0;
    }
    if (continueDecided != 0) {
        lt_item_select_disable = 0;
        lt_mask_property(14, 0);
        lt_mask_property(15, 0);
    } else {
        lt_item_select_disable = 1;
        lt_mask_property(14, 1);
        lt_mask_property(15, 1);
    }
    ui_settings_title_mask(continueDecided == 0);
    /* PC port: not on the Settings row, whose Cross default_item_select
       takes (it opens the menu) */
    if (continueDecided != 0 && (pad[0].flags & PAL_JPNKORCN(0x840, 0x820)) &&
        lt_fade_status() == 2 LA_HOST_NOT_SETTINGS_ROW) {
        lt_continue_selected = 1;
        soundSeDefPlay(402, 0xFFFFFFFF, 0, 0);
        switch (
            lt_current_property_item() +
            PAL_JPNCN_KOR(0, 29, 35)) { /* JPN property ids 29 lower, KOR property ids 35 lower */
        case 14:
            opTitleLogoMode = 2;
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 17;
        case 15:
            opTitleLogoMode = 2;
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 6;
        }
    }
    switch (_la_set_current_port_2(&mc, first)) {
    case 0:
        break;
    case 1:
        if ((curPortInfo->flags.w & 0x22) == 2) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 10;
        }
        continueDecided = 1;
        break;
    case -1:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 10;
    }
    return -1;
}

int la_title_continue_or_new(int first)
{
    if (ICO_JUN26) {
        return la_title_continue_or_new_jun26(first);
    }
    if (ICO_NTSC) {
        return la_title_continue_or_new_ntsc(first);
    }
    if (ICO_AUG6) {
        return la_title_continue_or_new_aug6(first);
    }
    if (first) {
        opTitleLogoMode = 1;
        continueIcoMiscLock = lock_execIcoMisc;
        iosPadEnable();
        isysGObjActiveLink(0, 1);
        systemStatus[5] = 0;
        gflagOff(382);
        ico_opt_mirror_reset(); /* the title belongs to no run */
        if (gflagChk(385) == 0) {
            gflagOn(385);
        }
        continueDecided = 0;
        lt_continue_selected = 0;
    }
    if (continueDecided != 0) {
        lt_item_select_disable = 0;
        lt_mask_property(PAL_JPNCN_KOR(49, 20, 14), 0);
        lt_mask_property(PAL_JPNCN_KOR(50, 21, 15), 0);
    } else {
        lt_item_select_disable = 1;
        lt_mask_property(PAL_JPNCN_KOR(49, 20, 14), 1);
        lt_mask_property(PAL_JPNCN_KOR(50, 21, 15), 1);
    }
    ui_settings_title_mask(continueDecided == 0);
    /* PC port: not on the Settings row, whose Cross default_item_select
       takes (it opens the menu) */
    if (continueDecided != 0 && (pad[0].flags & PAL_JPNKORCN(0x840, 0x820)) &&
        lt_fade_status() == 2 LA_HOST_NOT_SETTINGS_ROW) {
        lt_continue_selected = 1;
        soundSeDefPlay(414, 0xFFFFFFFF, 0, 0);
        switch (
            lt_current_property_item() +
            PAL_JPNCN_KOR(0, 29, 35)) { /* JPN property ids 29 lower, KOR property ids 35 lower */
        case 49:
            opTitleLogoMode = 2;
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN(0x14, 0x11);
        case 50:
            opTitleLogoMode = 2;
            gFlagGameClear = 0;
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN(9, 6);
        }
    }
    switch (_la_set_current_port_2(&mc, first)) {
    case 0:
        break;
    case 1:
        if ((curPortInfo->flags.w & 0x22) == 2) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN(0xD, 0xA);
        }
        continueDecided = 1;
        break;
    case -1:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN(0xD, 0xA);
    }
    return -1;
}

static int newGameDecided = 0; /* derived name */

static int la_title_new_game_only_jun26(int first)
{
    if (first) {
        newGameIcoMiscLock = GlobalTimer;
        iosPadEnable();
        isysGObjActiveLink(0, 1);
        systemStatus[5] = 0;
        gflagOff(333);
        ico_opt_mirror_reset(); /* the title belongs to no run */
        if (gflagChk(336) == 0) {
            gflagOn(336);
        }
        newGameDecided = 0;
        lastPort = -1;
    }
    if (newGameDecided != 0) {
        lt_item_select_disable = 0;
        lt_mask_property(16, 0);
    } else {
        lt_item_select_disable = 1;
        lt_mask_property(16, 1);
    }
    ui_settings_title_mask(newGameDecided == 0);
    /* PC port: not on the Settings row, whose Cross default_item_select
       takes (it opens the menu) */
    if (newGameDecided != 0 && (pad[0].flags & 0x840) &&
        lt_fade_status() == 2 LA_HOST_NOT_SETTINGS_ROW) {
        soundSeDefPlay(PAL_JUN26(414, 402), 0xFFFFFFFF, 0, 0);
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 6;
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
            return 9;
        }
        newGameDecided = 1;
        break;
    }
    return -1;
}

int la_title_new_game_only(int first)
{
    if (ICO_JUN26) {
        return la_title_new_game_only_jun26(first);
    }
    if (first) {
        opTitleLogoMode = 1;
        newGameIcoMiscLock = PAL_AUG6JUN26(lock_execIcoMisc, GlobalTimer);
        iosPadEnable();
        isysGObjActiveLink(0, 1);
        systemStatus[5] = 0;
        gflagOff(PAL_NTSC_AUG6JUN26(382, 349, 348));
        ico_opt_mirror_reset(); /* the title belongs to no run */
        if (gflagChk(PAL_NTSC_AUG6JUN26(385, 352, 351)) == 0) {
            gflagOn(PAL_NTSC_AUG6JUN26(385, 352, 351));
        }
        newGameDecided = 0;
        lt_continue_selected = 0;
        lastPort = -1;
    }
    if (newGameDecided != 0) {
        lt_item_select_disable = 0;
        lt_mask_property(PAL_JPNCN_KORNTSCAUG6JUN26(51, 22, 16), 0);
    } else {
        lt_item_select_disable = 1;
        lt_mask_property(PAL_JPNCN_KORNTSCAUG6JUN26(51, 22, 16), 1);
    }
    ui_settings_title_mask(newGameDecided == 0);
    /* PC port: not on the Settings row, whose Cross default_item_select
       takes (it opens the menu) */
    if (newGameDecided != 0 && (pad[0].flags & PAL_JPNKORCN(0x840, 0x820)) &&
        lt_fade_status() == 2 LA_HOST_NOT_SETTINGS_ROW) {
        soundSeDefPlay(PAL_NTSCAUG6JUN26(414, 402), 0xFFFFFFFF, 0, 0);
        lt_continue_selected = 1;
        opTitleLogoMode = 2;
        if (!ICO_NTSCAUG6JUN26) {
            gFlagGameClear = 0;
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORNTSCAUG6JUN26CN(9, 6);
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
            return PAL_JPNKORNTSCAUG6JUN26CN(0xC, 9);
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

static inline int la_mc_saved_file_select_jun26(int item)
{
    int i = item - 27;

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
    return i + 27;
}

static inline int la_mc_saved_file_select_ntscaug6(int item)
{
    int i = item - 27;
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
    return i + 27;
}

static inline int la_mc_saved_file_select(int item)
{
    if (ICO_JUN26) {
        return la_mc_saved_file_select_jun26(item);
    }
    if (ICO_NTSCAUG6) {
        return la_mc_saved_file_select_ntscaug6(item);
    }
    int i = item - PAL_JPNCN_KOR(62, 33, 27);
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
    return i + PAL_JPNCN_KOR(62, 33, 27);
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

static inline int la_mc_file_select_ntscaug6jun26(int first)
{
    int i;

    curFile = lt_current_property_item() - 27;

    if (actionStarted == 0) {
        lt_item_select_disable = 1;
        return -1;
    }

    for (i = 0; i < 10; i++) {
        if (((fileMask >> i) & 1) && IosMcProductFile[filePort].file[i].stage != 0xFFFFFFFF) {
            lt_mask_property(i + 27, 0);
            lt_mask_property(i + 17, 1);
        } else {
            lt_mask_property(i + 27, 1);
            lt_mask_property(i + 17, 0);
        }
    }

    previewInfo = *(struct McPreview *)&IosMcProductFile[filePort].file[curFile];

    return (pad[0].flags & PAL_JPNKORCN(0x40 | LA_BACK, 0x30)) ? curFile : -1;
}

int la_mc_file_select(int first)
{
    if (ICO_NTSCAUG6JUN26) {
        return la_mc_file_select_ntscaug6jun26(first);
    }
    int i;

    curFile = lt_current_property_item() - PAL_JPNCN_KOR(62, 33, 27);

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
        texLayout[PAL_JPNKORCN(14, 11)].curItem = curFile + PAL_JPNCN_KOR(62, 33, 27);
        fileMoved = 1;
    }

    for (i = 0; i < 10; i++) {
        if (((fileMask >> i) & 1) && IosMcProductFile[filePort].file[i].stage != 0xFFFFFFFF) {
            lt_mask_property(i + PAL_JPNCN_KOR(62, 33, 27), 0);
            lt_mask_property(i + PAL_JPNCN_KOR(52, 23, 17), 1);
        } else {
            lt_mask_property(i + PAL_JPNCN_KOR(62, 33, 27), 1);
            lt_mask_property(i + PAL_JPNCN_KOR(52, 23, 17), 0);
        }
    }

    previewInfo = *(struct McPreview *)&IosMcProductFile[filePort].file[curFile];

    return (pad[0].flags & PAL_JPNKORCN(0x40 | LA_BACK, 0x30)) ? curFile : -1;
}

/* the discs' bodies differ only in the property ids, 29 lower on the
   Japanese disc and 35 lower on the Korean one, as in _la_set_preview_info */
static inline void _la_mask_preview_info_ntscaug6jun26(void)
{
    int i;

    for (i = 0; i < 10; i++) {
        lt_mask_property(i + 41, 1);
        lt_mask_property(i + 51, 1);
        lt_mask_property(i + 61, 1);
        lt_mask_property(i + 71, 1);
        lt_mask_property(i + 81, 1);
        lt_mask_property(i + 91, 1);
    }
    lt_mask_property(39, 1);
    lt_mask_property(40, 1);
    for (i = 0; i < 39; i++) {
        lt_mask_property(i + 101, 1);
    }
}

static void _la_mask_preview_info(void)
{
    if (ICO_NTSCAUG6JUN26) {
        _la_mask_preview_info_ntscaug6jun26();
        return;
    }
    int i;

    for (i = 0; i < 10; i++) {
        lt_mask_property(i + PAL_JPNCN_KOR(76, 47, 41), 1);
        lt_mask_property(i + PAL_JPNCN_KOR(86, 57, 51), 1);
        lt_mask_property(i + PAL_JPNCN_KOR(96, 67, 61), 1);
        lt_mask_property(i + PAL_JPNCN_KOR(106, 77, 71), 1);
        lt_mask_property(i + PAL_JPNCN_KOR(116, 87, 81), 1);
        lt_mask_property(i + PAL_JPNCN_KOR(126, 97, 91), 1);
    }
    lt_mask_property(PAL_JPNCN_KOR(74, 45, 39), 1);
    lt_mask_property(PAL_JPNCN_KOR(75, 46, 40), 1);
    for (i = 0; i < 39; i++) {
        lt_mask_property(i + PAL_JPNCN_KOR(137, 108, 102), 1);
    }
    lt_mask_property(PAL_JPNCN_KOR(136, 107, 101), 1);
}

/* the frames a second, 60 or 50 by the video mode, over the field rate */
#define FRAME_RATE ((60 - systemStatus[0] * 10) / systemStatus[1]) /* derived name */

static inline void playTime_jun26(struct McPreview *p, int *hour, int *min,
                                  int *sec) /* derived name */
{
    *sec = (p->playTime / FRAME_RATE) % 60;
    *min = (p->playTime / (FRAME_RATE * 60)) % 60;
    *hour = (p->playTime / (FRAME_RATE * 3600)) % 100;
}

/* the play time of a save record split into hours, minutes and seconds and
   clamped to 99:59:59, inlined into _la_set_preview_info, la_load_processing
   and la_system_save_processing; the last two leave the results unused */
static inline void playTime_aug6(struct McPreview *p, int *hour, int *min,
                                 int *sec) /* derived name */
{
    int frames = p->playTime;

    *sec = (frames / FRAME_RATE) % 60;
    *min = (frames / (FRAME_RATE * 60)) % 60;
    *hour = frames / (FRAME_RATE * 3600);
    if (*hour >= 100) {
        *hour = 99;
        *min = 59;
        *sec = 59;
    }
}

static inline void playTime(struct McPreview *p, int *hour, int *min, int *sec) /* derived name */
{
    if (ICO_JUN26) {
        playTime_jun26(p, hour, min, sec);
        return;
    }
    if (ICO_AUG6) {
        playTime_aug6(p, hour, min, sec);
        return;
    }
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

static void _la_set_preview_info_jun26(void)
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
    if (hour >= 100) {
        hour = 99;
        min = 59;
        sec = 59;
    }

    hour = ((hour / 10 + 9) % 10) * 10 + (hour % 10 + 9) % 10;
    min = ((min / 10 + 9) % 10) * 10 + (min % 10 + 9) % 10;
    sec = ((sec / 10 + 9) % 10) * 10 + (sec % 10 + 9) % 10;

    lt_mask_property(41 + hour / 10, 0);
    lt_mask_property(51 + hour % 10, 0);
    lt_mask_property(61 + min / 10, 0);
    lt_mask_property(71 + min % 10, 0);
    lt_mask_property(81 + sec / 10, 0);
    lt_mask_property(91 + sec % 10, 0);
    lt_mask_property(39, 0);
    lt_mask_property(40, 0);

    n = previewInfo.stage;
    if (n >= 3 && n < 56) {
        switch (previewInfo.sofa + 42) { /* the demo's ids 42 lower */
        case 329:
            n = 39;
            break;
        case 331:
            n = 40;
            break;
        }
        lt_mask_property(n + 98, 0);
    }
}

static void _la_set_preview_info(void)
{
    if (ICO_JUN26) {
        _la_set_preview_info_jun26();
        return;
    }
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

    if (ICO_NTSCAUG6JUN26) {
        lt_mask_property(41 + hour / 10, 0);
        lt_mask_property(51 + hour % 10, 0);
        lt_mask_property(61 + min / 10, 0);
        lt_mask_property(71 + min % 10, 0);
        lt_mask_property(81 + sec / 10, 0);
        lt_mask_property(91 + sec % 10, 0);
        lt_mask_property(39, 0);
        lt_mask_property(40, 0);
    } else {
        lt_mask_property(PAL_JPNCN_KOR(76, 47, 41) + hour / 10, 0);
        lt_mask_property(PAL_JPNCN_KOR(86, 57, 51) + hour % 10, 0);
        lt_mask_property(PAL_JPNCN_KOR(96, 67, 61) + min / 10, 0);
        lt_mask_property(PAL_JPNCN_KOR(106, 77, 71) + min % 10, 0);
        lt_mask_property(PAL_JPNCN_KOR(116, 87, 81) + sec / 10, 0);
        lt_mask_property(PAL_JPNCN_KOR(126, 97, 91) + sec % 10, 0);
        lt_mask_property(PAL_JPNCN_KOR(74, 45, 39), 0);
        lt_mask_property(PAL_JPNCN_KOR(75, 46, 40), 0);
    }

    n = previewInfo.stage;
    if (!ICO_NTSCAUG6JUN26) {
        if (n == PAL_JPNKORCN(63, 61)) {
            n = 38;
        }
    }
    if (n >= 3 && n < 56) {
        switch (previewInfo.sofa +
                PAL_NTSC_AUG6JUN26(0, 30, 31)) { /* NTSC ids 30 lower, AUG6 ids 31 lower */
        case 329:
            n = 39;
            break;
        case 331:
            n = 40;
            break;
        }
        if (ICO_NTSCAUG6JUN26) {
            lt_mask_property(n + 98, 0);
        } else {
            lt_mask_property(n + PAL_JPNCN_KOR(134, 105, 99), 0);
            if (previewInfo.cleared != 0) {
                lt_mask_property(PAL_JPNCN_KOR(136, 107, 101), 0);
            }
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
    lt_mask_property(PAL_JPNCN_KOR_NTSCAUG6JUN26(176, 147, 141, 140), curPort);
    lt_mask_property(PAL_JPNCN_KOR_NTSCAUG6JUN26(177, 148, 142, 141), curPort ^ 1);
    return -1;
}

/* the load screen's default item; inlined once, into
   la_load_game_memory_card_check */
static inline void setLoadGameStartItem(void) /* derived name */
{
    if (loadSerial == IosMcProductFile[0].serial || saveSerial != IosMcProductFile[1].serial) {
        texLayout[PAL_JPNKORCN(17, 14)].defaultItem = PAL_JPNCN_KOR(186, 158, 152);
    } else {
        texLayout[PAL_JPNKORCN(17, 14)].defaultItem = PAL_JPNCN_KOR(187, 159, 153);
    }
}

int la_load_game_memory_card_check(int first)
{
    _la_mask_preview_info();
    if (ICO_NTSCAUG6JUN26) {
        lt_mask_property(140, 1);
        lt_mask_property(141, 1);
    } else {
        mcLoadMode = 1;
        lt_mask_property(PAL_JPNCN_KOR(176, 147, 141), 1);
        lt_mask_property(PAL_JPNCN_KOR(177, 148, 142), 1);
    }
    switch (_la_set_current_port_2(&mc, first)) {
    case 0:
        if (mcLastResult == 0 || mcLastResult == -14) {
            return -1;
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x16, 0x13, 19);
    case -1:
        if ((curPortInfo->flags.w >> 1) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN_NTSCAUG6JUN26(0x17, 0x14, 20);
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x16, 0x13, 19);
    case 1:
        if ((curPortInfo->flags.w >> 6) & 1) {
            if (!ICO_NTSCAUG6JUN26) {
                setLoadGameStartItem();
            }
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN_NTSCAUG6JUN26(0x11, 0xE, 14);
        }
        if ((curPortInfo->flags.w >> 5) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN_NTSCAUG6JUN26(0x13, 0x10, 16);
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x17, 0x14, 20);
    }
    return -1;
}

static int la_mc_load_current_slot_select_jun26(void)
{
    _la_mask_preview_info();
    if (PSH_POSITIVE(0)) {
        curPort = lt_current_property_item() - 151;
        lastPort = curPort;
        curPortInfo = &mcPortInfo[curPort];
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 16;
    }
    if (PSH_NEGATIVE(0)) {
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 9;
    }
    return -1;
}

static int la_mc_load_current_slot_select_ntscaug6(void)
{
    _la_mask_preview_info();
    if (pad[0].flags & 0x40) {
        curPort = lt_current_property_item() - 151;
        lastPort = curPort;
        curPortInfo = &mcPortInfo[curPort];
        POSITIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 16;
    }
    if (pad[0].flags & PAL_JPNKORCN(LA_BACK, 0x40)) {
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 9;
    }
    return -1;
}

inline int la_mc_load_current_slot_select(void)
{
    if (ICO_JUN26) {
        return la_mc_load_current_slot_select_jun26();
    }
    if (ICO_NTSCAUG6) {
        return la_mc_load_current_slot_select_ntscaug6();
    }
    _la_mask_preview_info();
    if (pad[0].flags & PAL_JPNKORCN(0x40, 0x20)) {
        curPort = lt_current_property_item() - PAL_JPNCN_KOR(186, 158, 152);
        lastPort = curPort;
        curPortInfo = &mcPortInfo[curPort];
        POSITIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN(0x13, 0x10);
    }
    if (pad[0].flags & PAL_JPNKORCN(LA_BACK, 0x40)) {
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN(0xC, 0x9);
    }
    return -1;
}

static int loadCardChanged = 1; /* derived name */

static int loadFileChosen = 0; /* derived name */

static int la_mc_load_file_select_jun26(int first, int item)
{
    int r;

    if (first) {
        fileMask = 0;
        lastPort = filePort = curPort;
        loadFileChosen = 0;
    }

    if (fileMask != 0) {
        _la_set_preview_info();
        lt_mask_property(38, 1);
        lt_mask_property(37, 1);
        lt_mask_property(160, 0);
        lt_mask_property(161, 0);
    } else {
        _la_mask_preview_info();
        lt_mask_property(38, 0);
        lt_mask_property(37, 0);
        lt_mask_property(160, 1);
        lt_mask_property(161, 1);
    }
    lt_set_item_select_func((ICO_WORD_PTR(LtSelectFn))la_mc_saved_file_select);

    if (PSH_NEGATIVE(0)) {
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 9;
    }

    if ((fileMask != 0 || loadFileChosen != 0) && PSH_POSITIVE(0)) {
        if (IosMcProductFile[filePort].file[item].stage != 0xFFFFFFFF) {
            selectFile = item;
            lastPort = filePort;
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 18;
        }
    }

    r = _la_set_current_port_lock_2(&mc, first);
    switch (r) {
    case -1:
        if (((curPortInfo->flags.w >> 1) & 1) == 0) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 19;
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 20;
    case 1:
        if (((curPortInfo->flags.w >> 1) & 1) == 0) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 19;
        }
        if ((curPortInfo->flags.w & 0x22) == 2) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 20;
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
                return 14;
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

int la_mc_load_file_select(int first, int item)
{
    if (ICO_JUN26) {
        return la_mc_load_file_select_jun26(first, item);
    }
    int r;

    if (first) {
        fileMask = 0;
        lastPort = filePort = curPort;
        loadFileChosen = 0;
    }

    if (fileMask != 0) {
        _la_set_preview_info();
        lt_mask_property(PAL_JPNCN_KORNTSCAUG6JUN26(73, 44, 38), 1);
        lt_mask_property(PAL_JPNCN_KORNTSCAUG6JUN26(72, 43, 37), 1);
        lt_mask_property(PAL_JPNCN_KOR_NTSCAUG6JUN26(195, 167, 161, 160), 0);
        lt_mask_property(PAL_JPNCN_KOR_NTSCAUG6JUN26(196, 168, 162, 161), 0);
    } else {
        _la_mask_preview_info();
        lt_mask_property(PAL_JPNCN_KORNTSCAUG6JUN26(73, 44, 38), 0);
        lt_mask_property(PAL_JPNCN_KORNTSCAUG6JUN26(72, 43, 37), 0);
        lt_mask_property(PAL_JPNCN_KOR_NTSCAUG6JUN26(195, 167, 161, 160), 1);
        lt_mask_property(PAL_JPNCN_KOR_NTSCAUG6JUN26(196, 168, 162, 161), 1);
    }
    lt_set_item_select_func((ICO_WORD_PTR(LtSelectFn))la_mc_saved_file_select);

    if (pad[0].flags & PAL_JPNKORCN(LA_BACK, 0x40)) {
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORNTSCAUG6JUN26CN(12, 9);
    }

    if ((fileMask != 0 || loadFileChosen != 0) && (pad[0].flags & PAL_JPNKORCN(0x40, 0x20))) {
        POSITIVE_SE();
        if (IosMcProductFile[filePort].file[item].stage != 0xFFFFFFFF) {
            selectFile = item;
            lastPort = filePort;
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORNTSCAUG6JUN26CN(21, 18);
        }
    }

    r = _la_set_current_port_lock_2(&mc, first);
    switch (r) {
    case -1:
        if (((curPortInfo->flags.w >> 1) & 1) == 0) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORNTSCAUG6JUN26CN(22, 19);
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORNTSCAUG6JUN26CN(23, 20);
    case 1:
        if (((curPortInfo->flags.w >> 1) & 1) == 0) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORNTSCAUG6JUN26CN(22, 19);
        }
        if ((curPortInfo->flags.w & 0x22) == 2) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORNTSCAUG6JUN26CN(23, 20);
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
                return PAL_JPNKORNTSCAUG6JUN26CN(17, 14);
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

static int la_load_confirm_no_memory_card_jun26(int first)
{
    if (first) {
        _la_mask_preview_info();
    }
    if (PSH_POSITIVE(0)) {
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 10;
    }
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        if ((curPortInfo->flags.w >> 1) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 17;
        }
        lock2Restart = 1;
        break;
    case 1:
        if ((curPortInfo->flags.w >> 1) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 17;
        }
        break;
    }
    return -1;
}

int la_load_confirm_no_memory_card(int first)
{
    if (ICO_JUN26) {
        return la_load_confirm_no_memory_card_jun26(first);
    }
    if (first) {
        _la_mask_preview_info();
    }
    if (PSH_POSITIVE_OR_NEGATIVE(0)) {
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0xD, 0xA, 10);
    }
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        if (ICO_NTSC) {
            if ((curPortInfo->flags.w & 0x12) == 2 || (curPortInfo->flags.w & 0x22) == 2) {
                lt_set_item_select_func(0);
                actionStarted = 0;
                return 20;
            }
        } else if (ICO_AUG6JUN26) {
            if ((curPortInfo->flags.w & 0x12) == 2) {
                lt_set_item_select_func(0);
                actionStarted = 0;
                return 20;
            }
        } else {
            if ((curPortInfo->flags.w & 0x32) == 2 || (curPortInfo->flags.w & 0x22) == 2) {
                lt_set_item_select_func(0);
                actionStarted = 0;
                return PAL_JPNKORCN(0x17, 0x14);
            }
        }
        if ((curPortInfo->flags.w >> 1) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN_NTSCAUG6JUN26(0x14, 0x11, 17);
        }
        lock2Restart = 1;
        break;
    case 1:
        if (ICO_NTSC) {
            if ((curPortInfo->flags.w & 0x12) == 2 || (curPortInfo->flags.w & 0x22) == 2) {
                lt_set_item_select_func(0);
                actionStarted = 0;
                return 20;
            }
        } else if (ICO_AUG6JUN26) {
        } else {
            if ((curPortInfo->flags.w & 0x32) == 2 || (curPortInfo->flags.w & 0x22) == 2) {
                lt_set_item_select_func(0);
                actionStarted = 0;
                return PAL_JPNKORCN(0x17, 0x14);
            }
        }
        if ((curPortInfo->flags.w >> 1) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN_NTSCAUG6JUN26(0x14, 0x11, 17);
        }
        break;
    }
    return -1;
}

static int la_load_confirm_no_data_jun26(int first)
{
    if (first) {
        _la_mask_preview_info();
    }
    if (PSH_POSITIVE(0)) {
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 10;
    }
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        if (((curPortInfo->flags.w >> 1) & 1) == 0) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 19;
        }
        lock2Restart = 1;
        break;
    case 1:
        if ((curPortInfo->flags.w >> 5) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 17;
        }
        break;
    }
    return -1;
}

int la_load_confirm_no_data(int first)
{
    if (ICO_JUN26) {
        return la_load_confirm_no_data_jun26(first);
    }
    if (first) {
        _la_mask_preview_info();
    }
    if (PSH_POSITIVE_OR_NEGATIVE(0)) {
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0xD, 0xA, 10);
    }
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        if (((curPortInfo->flags.w >> 1) & 1) == 0) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN_NTSCAUG6JUN26(0x16, 0x13, 19);
        }
        lock2Restart = 1;
        break;
    case 1:
        if ((curPortInfo->flags.w >> 5) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN_NTSCAUG6JUN26(0x14, 0x11, 17);
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
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x2E, 0x2B, 42);
    case -1:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x2E, 0x2B, 42);
    case 1:
        if (filePort != curPort) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN_NTSCAUG6JUN26(0x2E, 0x2B, 42);
        }
        if ((curPortInfo->flags.w & 3) != 3) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN_NTSCAUG6JUN26(0x16, 0x13, 19);
        }
        if ((curPortInfo->fileMask >> selectFile) & 1) {
            fileMask = 0x3FF;
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN_NTSCAUG6JUN26(0x19, 0x16, 22);
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x17, 0x14, 20);
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

static int la_load_processing_jun26(int first)
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
    case 9:
        gflagKeepState();
        gflagInit();
        gamesysMemoryLoad(gameSysMemoryFuncList, gameSysMainSaveBuff, 0);
        gflagRestoreState();
        systemStatus[3] = 1;
        systemStatus[4] = 1;
        /* falls through */
    case 6:
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
        return 42;
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
        debug_StdPrintfDummy("case 10\n");
        loadStep = 0;
        *(struct McPreview *)IosMcPreviewInfo =
            *(struct McPreview *)&IosMcProductFile[mc.port].file[mc.fileNo];
        playTime((struct McPreview *)IosMcPreviewInfo, &hour, &min, &sec);
        ico_mirror_slot_loaded(mc.fileNo, (unsigned int)mc.sum); /* the slot's mirror flag */
        ico_ach_slot_loaded(mc.fileNo, (unsigned int)mc.sum);    /* the slot's achievement run */
        debug_StdPrintfDummy("stage no %d\n", gFlagSaveStage);
        seEnvForceClose = 1;
        soundDataSegAllClose(0, 2);
        stgmgrForceSwitchWithFade(gFlagSaveStage, 0.5f, 4.0f);
        ACTGame_SetActors_Debug(gFlagSaveStage, 0);
        return -1;
    case 20:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 17;
    }
    return -1;
}

int la_load_processing(int first)
{
    if (ICO_JUN26) {
        return la_load_processing_jun26(first);
    }
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
        return PAL_JPNKORCN_NTSCAUG6JUN26(46, 43, 42);
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
        if (!ICO_NTSCAUG6JUN26) {
            loadSerial = mcSetFileNo(mc.port, mc.fileNo);
        }
        ico_mirror_slot_loaded(mc.fileNo, (unsigned int)mc.sum); /* the slot's mirror flag */
        ico_ach_slot_loaded(mc.fileNo, (unsigned int)mc.sum);    /* the slot's achievement run */
        debug_StdPrintfDummy("stage no %d\n", gFlagSaveStage);
        seEnvForceClose = 1;
        if (titleAdpcm != 0) {
            titleAdpcm->stream->fadeStep = 0x40;
        }
        titleAdpcm = 0;
        if (!ICO_NTSCAUG6JUN26) {
            if (gflagChk(395)) {
                lt_set_item_select_func(0);
                actionStarted = 0;
                return PAL_JPNKORCN(9, 6);
            }
        }
        stgmgrForceSwitchWithFade(gFlagSaveStage, 0.05f, 4.0f);
        ACTGame_SetActors_Debug(gFlagSaveStage, 0);
        return -1;
    case 20:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORNTSCAUG6JUN26CN(20, 17);
    }
    return -1;
}

static inline int la_general_mc_confirm_jun26(void)
{
    if (PSH_POSITIVE(0)) {
        return lt_current_property_item();
    }
    return -1;
}

inline int la_general_mc_confirm(void)
{
    if (ICO_JUN26) {
        return la_general_mc_confirm_jun26();
    }
    if (pad[0].flags & PAL_JPNKORCN(0x40, 0x20)) {
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

static int la_mc_confirm_save_file_jun26(int first, int item)
{
    lt_mask_property(140, 1);
    lt_mask_property(141, 1);
    if (first) {
        systemStatus[5] = 1;
        if (fightSoundProcessRequestStatus() == 0) {
            fightSoundProcessRequestPause();
            fightSoundStopped = 1;
        }
        saveVoice = openLayoutVoice(24);
        CheckPoint();
    }
    _la_mask_preview_info();
    if (saveVoice != 0) {
        layoutVoice = soundDataOpenSync(&voiceOpenReq);
        if (layoutVoice != (SqEntry *)0xFFFFFFFF) {
            saveVoice = 0;
            if (layoutVoice != 0) {
                AdpcmPlay(layoutVoice->stream);
            }
        }
        return -1;
    }
    switch (item) {
    case 183:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 26;
    case 184:
        systemStatus[5] = 0;
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 50;
    }
    return -1;
}

static int la_mc_confirm_save_file_ntscaug6(int first, int item)
{
    int i;

    _la_mask_preview_info();
    lt_mask_property(140, 1);
    lt_mask_property(141, 1);
    if (first) {
        systemStatus[5] = 1;
        if (fightSoundProcessRequestStatus() == 0) {
            fightSoundProcessRequestPause();
            fightSoundStopped = 1;
        }
        CheckPoint();
        saveVoiceOpened = 0;
    }
    if (saveVoiceOpened == 0) {
        if (AdpcmFreeAreaGet() != 0) {
            saveVoice = openLayoutVoice(21);
            saveVoiceOpened = 1;
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
                switch (item + PAL_JPNCN_KOR(0, 24, 30)) { /* JPN ids 24 lower, KOR ids 30 lower */
                case 183:
                    POSITIVE_SE();
                    lt_set_item_select_func(0);
                    actionStarted = 0;
                    return 26;
                case 184:
                    NEGATIVE_SE();
                    systemStatus[5] = 0;
                    lt_set_item_select_func(0);
                    actionStarted = 0;
                    return 50;
                }
            }
            if (lt_fade_status() != 2) {
                return -1;
            }
            if ((pad[0].flags & PAL_JPNKORCN(LA_BACK, 0x40)) == 0) {
                return -1;
            }
            NEGATIVE_SE();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 50;
        }
    }
    return -1;
}

int la_mc_confirm_save_file(int first, int item)
{
    if (ICO_JUN26) {
        return la_mc_confirm_save_file_jun26(first, item);
    }
    if (ICO_NTSCAUG6) {
        return la_mc_confirm_save_file_ntscaug6(first, item);
    }
    int i;

    _la_mask_preview_info();
    lt_mask_property(PAL_JPNCN_KOR(176, 147, 141), 1);
    lt_mask_property(PAL_JPNCN_KOR(177, 148, 142), 1);
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
                switch (item + PAL_JPNCN_KOR(0, 24, 30)) { /* JPN ids 24 lower, KOR ids 30 lower */
                case 214:
                    POSITIVE_SE();
                    lt_set_item_select_func(0);
                    actionStarted = 0;
                    return PAL_JPNKORCN(0x1E, 0x1B);
                case 215:
                    NEGATIVE_SE();
                    systemStatus[5] = 0;
                    lt_set_item_select_func(0);
                    actionStarted = 0;
                    return PAL_JPNKORCN(0x36, 0x33);
                }
            }
            if (lt_fade_status() != 2) {
                return -1;
            }
            if ((pad[0].flags & PAL_JPNKORCN(LA_BACK, 0x40)) == 0) {
                return -1;
            }
            if (stage_no == PAL_JPNKORCN(0x3F, 0x3D)) {
                return -1;
            }
            NEGATIVE_SE();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN(0x36, 0x33);
        }
    }
    return -1;
}

/* the save side of setLoadGameStartItem, inlined twice into
   la_save_game_memory_card_check */
static inline void setSaveGameStartItem(void) /* derived name */
{
    if (loadSerial == IosMcProductFile[0].serial || loadSerial != IosMcProductFile[1].serial) {
        texLayout[PAL_JPNKORCN(18, 15)].defaultItem = PAL_JPNCN_KOR(186, 158, 152);
    } else {
        texLayout[PAL_JPNKORCN(18, 15)].defaultItem = PAL_JPNCN_KOR(187, 159, 153);
    }
}

int la_save_game_memory_card_check(int first)
{
    _la_mask_preview_info();
    if (ICO_NTSCAUG6JUN26) {
        lt_mask_property(140, 1);
        lt_mask_property(141, 1);
    } else {
        mcLoadMode = 0;
        lt_mask_property(PAL_JPNCN_KOR(176, 147, 141), 1);
        lt_mask_property(PAL_JPNCN_KOR(177, 148, 142), 1);
    }
    debug_StdPrintfDummy("save game check port %d\n", curPort);
    switch (_la_set_current_port_new(&mc, first)) {
    case 0:
        break;
    case -1:
        debug_StdPrintfDummy("fail\n");
        if ((curPortInfo->flags.w & 3) == 3) {
            if ((curPortInfo->flags.w >> 2) & 1) {
                if (!ICO_NTSCAUG6JUN26) {
                    setSaveGameStartItem();
                }
                lt_set_item_select_func(0);
                actionStarted = 0;
                return PAL_JPNKORCN_NTSCAUG6JUN26(0x12, 0xF, 15);
            }
            if (((curPortInfo->flags.w >> 4) & 1) == 0) {
                lt_set_item_select_func(0);
                actionStarted = 0;
                return PAL_JPNKORCN_NTSCAUG6JUN26(0x20, 0x1D, 28);
            }
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x1F, 0x1C, 27);
    case 1:
        debug_StdPrintfDummy("sucess :%d %d %d\n", (curPortInfo->flags.w >> 5) & 1,
                             (curPortInfo->flags.w >> 4) & 1, (curPortInfo->flags.w & 0xA) == 2);
        if ((curPortInfo->flags.w >> 2) & 1) {
            if (!ICO_NTSCAUG6JUN26) {
                setSaveGameStartItem();
            }
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN_NTSCAUG6JUN26(0x12, 0xF, 15);
        }
        if ((curPortInfo->flags.w & 0x30) != 0 || (curPortInfo->flags.w & 0xA) == 2) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN_NTSCAUG6JUN26(0x21, 0x1E, 29);
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x20, 0x1D, 28);
    }
    return -1;
}

static inline int la_mc_save_current_slot_select_jun26(void)
{
    if (PSH_POSITIVE(0)) {
        curPort = lt_current_property_item() - 151;
        lastPort = curPort;
        curPortInfo = &mcPortInfo[curPort];
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 29;
    }
    if (PSH_NEGATIVE(0)) {
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 25;
    }
    return -1;
}

inline int la_mc_save_current_slot_select(void)
{
    if (ICO_JUN26) {
        return la_mc_save_current_slot_select_jun26();
    }
    if (pad[0].flags & PAL_JPNKORCN(0x40, 0x20)) {
        POSITIVE_SE();
        curPort = lt_current_property_item() - PAL_JPNCN_KOR_NTSCAUG6JUN26(186, 158, 152, 151);
        lastPort = curPort;
        curPortInfo = &mcPortInfo[curPort];
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x21, 0x1E, 29);
    }
    if (pad[0].flags & PAL_JPNKORCN(LA_BACK, 0x40)) {
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x1C, 0x19, 25);
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

extern void gif_Sprite(SprRect *r, long long z, SprRect *uv, SprCol *col, int prim);
extern void gif_EndPacket(void);
/* gif_Line's z words go as 32-bit unsigned ints here too (lui/ori) */
extern void gif_Line(int *v0, int *v1, unsigned int z0, unsigned int z1, SprCol *col, int prim);

static int barStep = 0; /* derived name */

static int barFrame = 0; /* derived name */

static void progressive_bar_jun26(void)
{
    int n;
    sceVu0IVECTOR p0;
    sceVu0IVECTOR p1;

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
        SprRect frame = {80, 6, -160, 6};
        SprCol frameCol = {128, 128, 128, 32};

        gif_Sprite(&frame, 0xFFFFFFFF, 0, &frameCol, 1);
    }
    {
        SprRect back = {78, 7, -156, 4};
        SprCol backCol = {0, 0, 0, 32};

        gif_Sprite(&back, 0xFFFFFFFF, 0, &backCol, 1);
    }
    {
        int w = (float)barStep / (float)barTotal * -160.0f;
        SprRect bar = {-80 - w, 8, w, 2};
        SprCol barCol = {64, 255, 64, 32};

        gif_Sprite(&bar, 0xFFFFFFFF, 0, &barCol, 1);
    }
    {
        SprCol lineCol = {128, 128, 128, 32};

        CopyIVector(p0, barLine[barFrame % 4][0]);
        CopyIVector(p1, barLine[barFrame % 4][1]);
        p0[0] += 252;
        p1[0] += 252;
        p0[1] += 17;
        p1[1] += 17;
        gif_Line(p0, p1, 0xFFFFFFFF, 0xFFFFFFFF, &lineCol, 1);
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

static void progressive_bar(void)
{
    if (ICO_JUN26) {
        progressive_bar_jun26();
        return;
    }
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
        SprRect frame = {80, PAL_JPNKORNTSCAUG6JUN26CN(11, 6), -160,
                         PAL_JPNKORNTSCAUG6JUN26CN(5, 6)};
        SprCol frameCol = {128, 128, 128, 32};

        gif_Sprite(&frame, 0xFFFFFFFF, 0, &frameCol, 1);
    }
    {
        SprRect back = {78, PAL_JPNKORNTSCAUG6JUN26CN(12, 7), -156,
                        PAL_JPNKORNTSCAUG6JUN26CN(3, 4)};
        SprCol backCol = {0, 0, 0, 32};

        gif_Sprite(&back, 0xFFFFFFFF, 0, &backCol, 1);
    }
    {
        int w = (float)barStep / (float)barTotal * -160.0f;
        SprRect bar = {-80 - w, PAL_JPNKORNTSCAUG6JUN26CN(13, 8), w,
                       PAL_JPNKORNTSCAUG6JUN26CN(1, 2)};
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

static int la_mc_save_file_select_jun26(int first, int item)
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
        lt_mask_property(38, 1);
        lt_mask_property(37, 1);
        lt_mask_property(208, 0);
        lt_mask_property(209, 0);
    } else {
        lt_mask_property(38, 0);
        lt_mask_property(37, 0);
        lt_mask_property(208, 1);
        lt_mask_property(209, 1);
    }

    if (actionStarted != 0) {
        if (PSH_NEGATIVE(0)) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 25;
        }
    }

    if ((fileMask != 0 || saveSelectReady != 0) && PSH_POSITIVE(0)) {
        selectFile = item;
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 30;
    }

    r = _la_set_current_port_lock_2(&mc, first);
    switch (r) {
    case -1:
        if (((curPortInfo->flags.w >> 1) & 1) == 0) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 27;
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 28;
    case 1:
        if (((mcPortInfo[filePort].flags.w >> 1) & 1) == 0) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 25;
        }
        if (fileMask != 0) {
            if (saveCardChanged == 0) {
                if (((curPortInfo->flags.w >> 2) & 1) != 0) {
                    lt_set_item_select_func(0);
                    actionStarted = 0;
                    return 15;
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

int la_mc_save_file_select(int first, int item)
{
    if (ICO_JUN26) {
        return la_mc_save_file_select_jun26(first, item);
    }
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
        lt_mask_property(PAL_JPNCN_KORNTSCAUG6JUN26(73, 44, 38), 1);
        lt_mask_property(PAL_JPNCN_KORNTSCAUG6JUN26(72, 43, 37), 1);
        lt_mask_property(PAL_JPNCN_KOR_NTSCAUG6JUN26(239, 216, 210, 208), 0);
        lt_mask_property(PAL_JPNCN_KOR_NTSCAUG6JUN26(240, 217, 211, 209), 0);
    } else {
        lt_mask_property(PAL_JPNCN_KORNTSCAUG6JUN26(73, 44, 38), 0);
        lt_mask_property(PAL_JPNCN_KORNTSCAUG6JUN26(72, 43, 37), 0);
        lt_mask_property(PAL_JPNCN_KOR_NTSCAUG6JUN26(239, 216, 210, 208), 1);
        lt_mask_property(PAL_JPNCN_KOR_NTSCAUG6JUN26(240, 217, 211, 209), 1);
    }

    if (actionStarted != 0) {
        if (pad[0].flags & PAL_JPNKORCN(LA_BACK, 0x40)) {
            NEGATIVE_SE();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORNTSCAUG6JUN26CN(28, 25);
        }
    }

    if ((fileMask != 0 || saveSelectReady != 0) && (pad[0].flags & PAL_JPNKORCN(0x40, 0x20))) {
        POSITIVE_SE();
        selectFile = item;
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(34, 31, 30);
    }

    r = _la_set_current_port_lock_2(&mc, first);
    switch (r) {
    case -1:
        if (((curPortInfo->flags.w >> 1) & 1) == 0) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN_NTSCAUG6JUN26(31, 28, 27);
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(32, 29, 28);
    case 1:
        if (((mcPortInfo[filePort].flags.w >> 1) & 1) == 0) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORNTSCAUG6JUN26CN(28, 25);
        }
        if (fileMask != 0) {
            if (saveCardChanged == 0) {
                if (((curPortInfo->flags.w >> 2) & 1) != 0) {
                    lt_set_item_select_func(0);
                    actionStarted = 0;
                    return PAL_JPNKORNTSCAUG6JUN26CN(18, 15);
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

static inline int la_save_confirm_no_memory_card_jun26(int first)
{
    if (PSH_POSITIVE(0)) {
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 25;
    }
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        if ((curPortInfo->flags.w >> 1) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 26;
        }
        lock2Restart = 1;
        break;
    case 1:
        if ((curPortInfo->flags.w >> 1) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 26;
        }
        break;
    }
    return -1;
}

inline int la_save_confirm_no_memory_card(int first)
{
    if (ICO_JUN26) {
        return la_save_confirm_no_memory_card_jun26(first);
    }
    if (PSH_POSITIVE_OR_NEGATIVE(0)) {
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x1C, 0x19, 25);
    }
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        if ((curPortInfo->flags.w >> 1) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN_NTSCAUG6JUN26(0x1E, 0x1B, 26);
        }
        lock2Restart = 1;
        break;
    case 1:
        if ((curPortInfo->flags.w >> 1) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN_NTSCAUG6JUN26(0x1E, 0x1B, 26);
        }
        break;
    }
    return -1;
}

static inline int la_save_confirm_no_free_area_jun26(int first)
{
    if (PSH_POSITIVE(0)) {
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 25;
    }
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        if (((curPortInfo->flags.w >> 1) & 1) == 0) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 27;
        }
        lock2Restart = 1;
        break;
    case 1:
        if ((curPortInfo->flags.w >> 4) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 26;
        }
        break;
    }
    return -1;
}

inline int la_save_confirm_no_free_area(int first)
{
    if (ICO_JUN26) {
        return la_save_confirm_no_free_area_jun26(first);
    }
    if (PSH_POSITIVE_OR_NEGATIVE(0)) {
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x1C, 0x19, 25);
    }
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        if (((curPortInfo->flags.w >> 1) & 1) == 0) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN_NTSCAUG6JUN26(0x1F, 0x1C, 27);
        }
        lock2Restart = 1;
        break;
    case 1:
        if ((curPortInfo->flags.w >> 4) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN_NTSCAUG6JUN26(0x1E, 0x1B, 26);
        }
        break;
    }
    return -1;
}

static int la_save_start_check_jun26(int first)
{
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 26;
    case 1:
        if (filePort != curPort) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 40;
        }
        if ((curPortInfo->flags.w & 0xA) == 2) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 32;
        }
        if ((curPortInfo->fileMask >> selectFile) & 1) {
            if (IosMcProductFile[filePort].file[selectFile].stage != 0xFFFFFFFF) {
                lt_set_item_select_func(0);
                actionStarted = 0;
                return 31;
            }
            if (curPortInfo->fileMask != 0) {
                debug_StdPrintfDummy("already exist save data\n");
                lt_set_item_select_func(0);
                actionStarted = 0;
                return 34;
            }
        }
        debug_StdPrintfDummy("new save. system data making..\n");
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 35;
    }
    return -1;
}

int la_save_start_check(int first)
{
    if (ICO_JUN26) {
        return la_save_start_check_jun26(first);
    }
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        if (((curPortInfo->flags.w >> 1) & 1) == 0) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN_NTSCAUG6JUN26(0x1F, 0x1C, 27);
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x1E, 0x1B, 26);
    case 1:
        if (filePort != curPort) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN_NTSCAUG6JUN26(0x2C, 0x29, 40);
        }
        if ((curPortInfo->flags.w & 0xA) == 2) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN_NTSCAUG6JUN26(0x24, 0x21, 32);
        }
        if ((curPortInfo->fileMask >> selectFile) & 1) {
            if (IosMcProductFile[filePort].file[selectFile].stage != 0xFFFFFFFF) {
                lt_set_item_select_func(0);
                actionStarted = 0;
                return PAL_JPNKORCN_NTSCAUG6JUN26(0x23, 0x20, 31);
            }
            if (curPortInfo->fileMask != 0) {
                debug_StdPrintfDummy("already exist save data\n");
                lt_set_item_select_func(0);
                actionStarted = 0;
                return PAL_JPNKORCN_NTSCAUG6JUN26(0x26, 0x23, 34);
            }
        }
        debug_StdPrintfDummy("new save. system data making..\n");
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x27, 0x24, 35);
    }
    return -1;
}

static int la_save_confirm_overwrite_jun26(int first, int item)
{
    if (first) {
        filePort = curPort;
    }
    fileMask = 0x3FF;
    _la_set_preview_info();
    switch (item) {
    case 183:
    case 187:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 34;
    case 184:
    case 188:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 29;
    }
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 27;
    case 1:
        if (saveCardChanged == 0) {
            if (((curPortInfo->flags.w >> 2) & 1) == 0) {
                break;
            }
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 15;
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
        return 26;
    }
    return -1;
}

static int la_save_confirm_overwrite_ntscaug6(int first, int item)
{
    if (first) {
        filePort = curPort;
    }
    fileMask = 0x3FF;
    _la_set_preview_info();
    if (lt_fade_status() == 2 && (pad[0].flags & PAL_JPNKORCN(LA_BACK, 0x40))) {
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 29;
    }
    switch (item + PAL_JPNCN_KOR(0, 24, 30)) { /* JPN ids 24 lower, KOR ids 30 lower */
    case 183:
    case 187:
        POSITIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 34;
    case 184:
    case 188:
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 29;
    }
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 27;
    case 1:
        if (saveCardChanged == 0) {
            if (((curPortInfo->flags.w >> 2) & 1) == 0) {
                break;
            }
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 15;
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
        return 26;
    }
    return -1;
}

int la_save_confirm_overwrite(int first, int item)
{
    if (ICO_JUN26) {
        return la_save_confirm_overwrite_jun26(first, item);
    }
    if (ICO_NTSCAUG6) {
        return la_save_confirm_overwrite_ntscaug6(first, item);
    }
    if (first) {
        filePort = curPort;
    }
    fileMask = 0x3FF;
    _la_set_preview_info();
    if (lt_fade_status() == 2 && (pad[0].flags & PAL_JPNKORCN(LA_BACK, 0x40))) {
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN(0x21, 0x1E);
    }
    switch (item + PAL_JPNCN_KOR(0, 24, 30)) { /* JPN ids 24 lower, KOR ids 30 lower */
    case 214:
    case 218:
        POSITIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN(0x26, 0x23);
    case 215:
    case 219:
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN(0x21, 0x1E);
    }
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN(0x1F, 0x1C);
    case 1:
        if (saveCardChanged == 0) {
            if (((curPortInfo->flags.w >> 2) & 1) == 0) {
                break;
            }
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN(0x12, 0xF);
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
        return PAL_JPNKORCN(0x1E, 0x1B);
    }
    return -1;
}

static int la_format_confirm_jun26(int first, int item)
{
    if (first) {
        filePort = curPort;
    }
    switch (item) {
    case 183:
    case 187:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 33;
    case 184:
    case 188:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 29;
    }
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 27;
    case 1:
        if (saveCardChanged == 0) {
            if (((curPortInfo->flags.w >> 2) & 1) == 0) {
                break;
            }
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 15;
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
        return 26;
    }
    return -1;
}

static int la_format_confirm_ntscaug6(int first, int item)
{
    if (first) {
        filePort = curPort;
    }
    if (lt_fade_status() == 2 && (pad[0].flags & PAL_JPNKORCN(LA_BACK, 0x40))) {
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 29;
    }
    switch (item + PAL_JPNCN_KOR(0, 24, 30)) { /* JPN ids 24 lower, KOR ids 30 lower */
    case 183:
    case 187:
        POSITIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 33;
    case 184:
    case 188:
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 29;
    }
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 27;
    case 1:
        if (saveCardChanged == 0) {
            if (((curPortInfo->flags.w >> 2) & 1) == 0) {
                break;
            }
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 15;
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
        return 26;
    }
    return -1;
}

int la_format_confirm(int first, int item)
{
    if (ICO_JUN26) {
        return la_format_confirm_jun26(first, item);
    }
    if (ICO_NTSCAUG6) {
        return la_format_confirm_ntscaug6(first, item);
    }
    if (first) {
        filePort = curPort;
    }
    if (lt_fade_status() == 2 && (pad[0].flags & PAL_JPNKORCN(LA_BACK, 0x40))) {
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN(0x21, 0x1E);
    }
    switch (item + PAL_JPNCN_KOR(0, 24, 30)) { /* JPN ids 24 lower, KOR ids 30 lower */
    case 214:
    case 218:
        POSITIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN(0x25, 0x22);
    case 215:
    case 219:
        NEGATIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN(0x21, 0x1E);
    }
    switch (_la_set_current_port_lock_2(&mc, first)) {
    case 0:
        break;
    case -1:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN(0x1F, 0x1C);
    case 1:
        if (saveCardChanged == 0) {
            if (((curPortInfo->flags.w >> 2) & 1) == 0) {
                break;
            }
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN(0x12, 0xF);
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
        return PAL_JPNKORCN(0x1E, 0x1B);
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
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x2D, 0x2A, 41);
    case 4:
        formatStep++;
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x27, 0x24, 35);
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
        return PAL_JPNKORCN_NTSCAUG6JUN26(44, 41, 40);
    case 4:
        for (i = 0; i < 10; i++) {
            IosMcProductFile[mc.port].file[i].stage = 0xFFFFFFFF;
        }
        if (!ICO_NTSCAUG6JUN26) {
            while ((IosMcProductFile[mc.port].serial = mcMakeSerial()) == 0)
                ;
        }
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
            return PAL_JPNKORCN_NTSCAUG6JUN26(38, 35, 34);
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
        return PAL_JPNKORCN_NTSCAUG6JUN26(44, 41, 40);
    case 4:
        IosMcPreviewInfo[0] = stage_no;
        if (ICO_NTSCAUG6JUN26) {
            IosMcPreviewInfo[2] = GetSaveSofaLayoutID();
        } else {
            IosMcPreviewInfo[3] = GetSaveSofaLayoutID();
            IosMcPreviewInfo[1] = gFlagGameClear;
        }
        *(struct McPreview *)&IosMcProductFile[mc.port].file[mc.fileNo] =
            *(struct McPreview *)IosMcPreviewInfo;
        playTime((struct McPreview *)IosMcPreviewInfo, &hour, &min, &sec);
        if (!ICO_NTSCAUG6JUN26) {
            (IosMcProductFile + mc.port)->fileNo = mc.fileNo;
            if ((IosMcProductFile + mc.port)->serial ==
                (IosMcProductFile + (mc.port ^ 1))->serial) {
                do {
                    while ((IosMcProductFile[mc.port].serial = mcMakeSerial()) == 0)
                        ;
                } while ((IosMcProductFile + mc.port)->serial ==
                         (IosMcProductFile + (mc.port ^ 1))->serial);
            }
            mcSetSavedFile();
        }
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
        ico_mirror_slot_saved(mc.fileNo, (unsigned int)mc.sum); /* the run's mirror flag */
        ico_ach_slot_saved(mc.fileNo, (unsigned int)mc.sum);    /* the achievement run */
        saveStep = 0;
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(41, 38, 37);
    }
    return -1;
}

static inline int la_save_confirm_complete_ntscaug6jun26(int first, int item)
{
    if (first) {
        previewInfo = *(struct McPreview *)IosMcPreviewInfo;
        fileMask = 0x3FF;
        _la_set_preview_info();
        debug_StdPrintfDummy("save complete %d %d\n", fileMask, curFile);
    }
    if (item != -1) {
        debug_StdPrintfDummy("%d %d %d\n", 233, 234, item);
    }
    switch (item + PAL_JPNCN_KOR(0, 23, 29)) { /* JPN ids 23 lower, KOR ids 29 lower */
    case 233:
        systemStatus[5] = 0;
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 50;
    case 234:
        nextStage = 1;
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 59;
    }
    return -1;
}

inline int la_save_confirm_complete(int first, int item)
{
    if (ICO_NTSCAUG6JUN26) {
        return la_save_confirm_complete_ntscaug6jun26(first, item);
    }
    if (first) {
        previewInfo = *(struct McPreview *)IosMcPreviewInfo;
        fileMask = 0x3FF;
        _la_set_preview_info();
        debug_StdPrintfDummy("save complete %d %d\n", fileMask, curFile);
    }
    if (item != -1) {
        debug_StdPrintfDummy("%d %d %d\n", PAL_JPNCN_KOR(264, 241, 235),
                             PAL_JPNCN_KOR(265, 242, 236), item);
    }
    switch (item + PAL_JPNCN_KOR(0, 23, 29)) { /* JPN ids 23 lower, KOR ids 29 lower */
    case 264:
        systemStatus[5] = 0;
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN(0x36, 0x33);
    case 265:
        nextStage = 1;
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN(0x3F, 0x3C);
    }
    return -1;
}

static int la_end_confirm_ntsc(void)
{
    int item;

    if (lt_fade_status() == 2 && (pad[0].flags & PAL_JPNKORCN(LA_BACK, 0x40))) {
        NEGATIVE_SE();
        item = lt_current_property_item();
        if (item >= 239) {
            if (item < 241) {
                lt_set_item_select_func(0);
                actionStarted = 0;
                return 37;
            }
            if (item < 375) {
                if (item >= 373) {
                    lt_set_item_select_func(0);
                    actionStarted = 0;
                    return 53;
                }
            }
        }
    }
    if (pad[0].flags & PAL_JPNKORCN(0x40, 0x20)) {
        debug_StdPrintfDummy("%d\n", lt_current_property_item());
        switch (
            lt_current_property_item() +
            PAL_JPNCN_KOR(0, 23, 29)) { /* JPN property ids 23 lower, KOR property ids 29 lower */
        case 239:
        case 373:
            POSITIVE_SE();
            gflagInit();
            fightSoundProcessRequestPause();
            fightSoundClose();
            soundDataSegAllClose(0, 2);
            nextStage = 1;
            stgmgrForceSwitchWithFade(1, 0.025f, 4.0f);
        case 240:
            NEGATIVE_SE();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 37;
        case 374:
            NEGATIVE_SE();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 53;
        }
    }
    return -1;
}

inline static int la_end_confirm_jun26(void)
{
    if (PSH_POSITIVE(0)) {
        debug_StdPrintfDummy("%d\n", lt_current_property_item());
        switch (lt_current_property_item() +
                0) { /* JPN property ids 23 lower, KOR property ids 29 lower */
        case 239:
        case 373:
            gflagInit();
            sampleModeDemoEndTimeOut();
        case 240:
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 37;
        }
    }
    return -1;
}

static int la_end_confirm_aug6(void)
{
    int item;

    if (lt_fade_status() == 2 && (pad[0].flags & PAL_JPNKORCN(LA_BACK, 0x40))) {
        NEGATIVE_SE();
        item = lt_current_property_item();
        if (item >= 239) {
            if (item < 241) {
                lt_set_item_select_func(0);
                actionStarted = 0;
                return 37;
            }
            if (item < 375) {
                if (item >= 373) {
                    lt_set_item_select_func(0);
                    actionStarted = 0;
                    return 53;
                }
            }
        }
    }
    if (pad[0].flags & PAL_JPNKORCN(0x40, 0x20)) {
        debug_StdPrintfDummy("%d\n", lt_current_property_item());
        switch (
            lt_current_property_item() +
            PAL_JPNCN_KOR(0, 23, 29)) { /* JPN property ids 23 lower, KOR property ids 29 lower */
        case 239:
        case 373:
            POSITIVE_SE();
            gflagInit();
            nextStage = 1;
            stgmgrForceSwitchWithFade(1, 0.025f, 4.0f);
        case 240:
            NEGATIVE_SE();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 37;
        case 374:
            NEGATIVE_SE();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 53;
        }
    }
    return -1;
}

int la_end_confirm(void)
{
    if (ICO_JUN26) {
        return la_end_confirm_jun26();
    }
    if (ICO_NTSC) {
        return la_end_confirm_ntsc();
    }
    if (ICO_AUG6) {
        return la_end_confirm_aug6();
    }
    int item;

    if (lt_fade_status() == 2 && (pad[0].flags & PAL_JPNKORCN(LA_BACK, 0x40))) {
        NEGATIVE_SE();
        item = lt_current_property_item();
        if (item >= PAL_JPNCN_KOR(270, 247, 241)) {
            if (item < PAL_JPNCN_KOR(272, 249, 243)) {
                lt_set_item_select_func(0);
                actionStarted = 0;
                return PAL_JPNKORCN(0x29, 0x26);
            }
            if (item < PAL_JPNCN_KOR(426, 403, 397)) {
                if (item >= PAL_JPNCN_KOR(424, 401, 395)) {
                    lt_set_item_select_func(0);
                    actionStarted = 0;
                    return PAL_JPNKORCN(0x39, 0x36);
                }
            }
        }
    }
    if (pad[0].flags & PAL_JPNKORCN(0x40, 0x20)) {
        debug_StdPrintfDummy("%d\n", lt_current_property_item());
        switch (
            lt_current_property_item() +
            PAL_JPNCN_KOR(0, 23, 29)) { /* JPN property ids 23 lower, KOR property ids 29 lower */
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
            return PAL_JPNKORCN(0x29, 0x26);
        case 425:
            NEGATIVE_SE();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN(0x39, 0x36);
        }
    }
    return -1;
}

static inline int la_save_confirm_yesno_jun26(void)
{
    if (PSH_NEGATIVE(0)) {
        return lt_current_property_item();
    }
    return -1;
}

inline int la_save_confirm_yesno(void)
{
    if (ICO_JUN26) {
        return la_save_confirm_yesno_jun26();
    }
    if (pad[0].flags & PAL_JPNKORCN(0x10, 0x40)) {
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
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x1E, 0x1B, 26);
    case 1:
        if ((curPortInfo->fileMask >> selectFile) & 1) {
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN_NTSCAUG6JUN26(0x31, 0x2E, 45);
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x21, 0x1E, 29);
    }
    return -1;
}

inline int la_delete_confirm(int first, int item)
{
    switch (item +
            PAL_JPNCN_KOR_NTSCAUG6JUN26(
                0, 24, 30, 31)) { /* JPNCN ids 24 lower, KOR ids 30 lower, NTSCAUG6 ids 31 lower */
    case 214:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x32, 0x2F, 46);
    case 215:
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x1E, 0x1B, 26);
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
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x33, 0x30, 47);
    }
    return -1;
}

static inline int la_delete_confirm_complete_jun26(void)
{
    if (PSH_NEGATIVE(0)) {
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 26;
    }
    return -1;
}

inline int la_delete_confirm_complete(void)
{
    if (ICO_JUN26) {
        return la_delete_confirm_complete_jun26();
    }
    int ret;
    if ((pad[0].flags & PAL_JPNKORCN(LA_BACK, 0x40)) == 0)
        goto fail;
    lt_set_item_select_func(0);
    actionStarted = 0;
    ret = PAL_JPNKORCN_NTSCAUG6JUN26(0x1E, 0x1B, 26);
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

static inline void la_playtime_count_ntsc(void)
{
    if (systemStatus[5] == 0) {
        IosMcPreviewInfo[1]++;
    }
}

inline void la_playtime_count(void)
{
    if (ICO_NTSC) {
        la_playtime_count_ntsc();
        return;
    }
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

static inline int la_game_loop_jun26(int first)
{
    IosMcPreviewInfo[1]++; /* the August preview counts the play time here */
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
    if ((pad[0].flags & 0x800) || layoutActPushStartNew != 0) {
        if (enable_game_pause == 0) {
            return -1;
        }
        systemStatus[5] = 1;
        layoutActPushStartNew = 0;
        adpcmPauseRequest(1);
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 53;
    }
    return -1;
}

int la_game_loop(int first)
{
    if (ICO_JUN26) {
        return la_game_loop_jun26(first);
    }
    if (ICO_AUG6JUN26) {
        IosMcPreviewInfo[1]++; /* the August preview counts the play time here */
    }
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
        if (gflagChk(PAL_NTSC_AUG6JUN26(338, 307, 306)) != 0) {
            return -1;
        }
        systemStatus[5] = 1;
        layoutActPushStartNew = 0;
        adpcmPauseRequest(1);
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x39, 0x36, 53);
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
    return PAL_JPNKORCN_NTSCAUG6JUN26(0x37, 0x34, 51);
}

static inline int la_game_demo_jun26(int first)
{
    if (first) {
        iosPadDisable();
    }
    if ((pad[0].flags & 0x800) && gflagChk(339)) {
        debug_StdPrintfDummy("push start\n");
        gflagOff(339);
        title_demo_mode ^= 1;
        nextStage = stage_after_skipping_demo;
        stgmgrForceSwitchWithFade(nextStage, 8.0f, 4.0f);
        if (stage_after_skipping_demo == 0xFFFFFFFF) {
            stage_after_skipping_demo = 1;
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 59;
    }
    return -1;
}

inline int la_game_demo(int first)
{
    if (ICO_JUN26) {
        return la_game_demo_jun26(first);
    }
    if (first) {
        if (stage_no == 1) {
            iosPadDisable();
        }
    }
    if ((pad[0].flags & 0x800) && gflagChk(PAL_NTSC_AUG6JUN26(388, 355, 354))) {
        debug_StdPrintfDummy("push start\n");
        gflagOff(PAL_NTSC_AUG6JUN26(388, 355, 354));
        title_demo_mode ^= 1;
        nextStage = stage_after_skipping_demo;
        stgmgrForceSwitchWithFade(nextStage, 8.0f, 4.0f);
        if (stage_after_skipping_demo == 0xFFFFFFFF) {
            stage_after_skipping_demo = 1;
        }
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x3F, 0x3C, 59);
    }
    return -1;
}

static inline int la_game_pause_ntsc(int first)
{
    if (first) {
        systemStatus[5] = 1;
        iosPadActStopAll();
    }
    if (lt_fade_status() != 2) {
        return -1;
    }
    if (((pad[0].flags & 0x40) && lt_current_property_item() == 264) ||
        (pad[0].flags & (0x800 | LA_BACK))) {
        NEGATIVE_SE();
        adpcmPauseRequest(0);
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 50;
    }
    return -1;
}

static inline int la_game_pause_jun26(int first)
{
    if (first) {
        systemStatus[5] = 1;
    }
    if ((pad[0].flags & 0x800) || ((pad[0].flags & 0x40) && lt_current_property_item() == 264)) {
        adpcmPauseRequest(0);
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 50;
    }
    return -1;
}

static inline int la_game_pause_aug6(int first)
{
    if (first) {
        systemStatus[5] = 1;
    }
    if (lt_fade_status() != 2) {
        return -1;
    }
    if (((pad[0].flags & 0x40) && lt_current_property_item() == 264) ||
        (pad[0].flags & (0x800 | LA_BACK))) {
        NEGATIVE_SE();
        adpcmPauseRequest(0);
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 50;
    }
    return -1;
}

inline int la_game_pause(int first)
{
    if (ICO_JUN26) {
        return la_game_pause_jun26(first);
    }
    if (ICO_NTSC) {
        return la_game_pause_ntsc(first);
    }
    if (ICO_AUG6) {
        return la_game_pause_aug6(first);
    }
    if (first) {
        systemStatus[5] = 1;
        iosPadActStopAll();
        texLayout[PAL_JPNKORCN(58, 55)].defaultItem = PAL_JPNCN_KOR(308, 285, 279);
    }
    if (lt_fade_status() != 2) {
        return -1;
    }
    if (((pad[0].flags & PAL_JPNKORCN(0x40, 0x20)) &&
         lt_current_property_item() == PAL_JPNCN_KOR(295, 272, 266)) ||
        (pad[0].flags & PAL_JPNKORCN(0x800 | LA_BACK, 0x840))) {
        NEGATIVE_SE();
        adpcmPauseRequest(0);
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN(0x36, 0x33);
    }
    return -1;
}

static int gameOverVoice = 0; /* derived name */

static int gameOverVoiceOpened = 0; /* derived name */

static int la_game_over_continue_jun26(int first)
{
    if (first) {
        iosPadEnable();
        systemStatus[5] = 1;
        soundSePlayModeStop(1);
        soundSePlayModeStop(0);
        gameOverVoice = openLayoutVoice(48);
    }
    if (gameOverVoice != 0) {
        layoutVoice = soundDataOpenSync(&voiceOpenReq);
        if (layoutVoice != (SqEntry *)ICO_INVALID_PTR) {
            gameOverVoice = 0;
            if (layoutVoice != 0) {
                AdpcmPlay(layoutVoice->stream);
                return -1;
            }
        }
    } else {
        switch (lt_current_property_item() +
                0) { /* JPN property ids 23 lower, KOR property ids 29 lower */
        case 378:
            if (PSH_POSITIVE(0)) {
                systemStatus[4] = 1;
                nextStage = gFlagSaveStage;
                releaseGameLoopCursor();
                lt_set_item_select_func(0);
                actionStarted = 0;
                return 59;
            }
            break;
        case 379:
            if (PSH_POSITIVE(0)) {
                gflagInit();
                sampleModeDemoEndTimeOut();
            }
            break;
        }
    }
    return -1;
}

static int la_game_over_continue_ntscaug6(int first)
{
    if (first) {
        enable_game_pause = 1;
        iosPadEnable();
        scpBoyControlReadDisable = 0;
        systemStatus[5] = 1;
        AdpcmFadeCloseAll(0x200);
        soundSePlayModeStop(1);
        soundSePlayModeStop(0);
        iosPadActStopAll();
        gameOverVoiceOpened = 0;
    } else if (gameOverVoiceOpened == 0) {
        if (AdpcmFreeAreaGet() != 0) {
            gameOverVoice = openLayoutVoice(50);
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
        switch (
            lt_current_property_item() +
            PAL_JPNCN_KOR(0, 23, 29)) { /* JPN property ids 23 lower, KOR property ids 29 lower */
        case 378:
            if ((pad[0].flags & PAL_JPNKORCN(0x40, 0x20)) == 0) {
                return -1;
            }
            POSITIVE_SE();
            systemStatus[4] = 1;
            nextStage = gFlagSaveStage;
            releaseGameLoopCursor();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 59;
        case 379:
            if ((pad[0].flags & 0x40) == 0) {
                return -1;
            }
            POSITIVE_SE();
            gflagInit();
            nextStage = 1;
            releaseGameLoopCursor();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 59;
        }
    }
    return -1;
}

int la_game_over_continue(int first)
{
    if (ICO_JUN26) {
        return la_game_over_continue_jun26(first);
    }
    if (ICO_NTSCAUG6) {
        return la_game_over_continue_ntscaug6(first);
    }
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
        switch (
            lt_current_property_item() +
            PAL_JPNCN_KOR(0, 23, 29)) { /* JPN property ids 23 lower, KOR property ids 29 lower */
        case 429:
            if ((pad[0].flags & PAL_JPNKORCN(0x40, 0x20)) == 0 || lt_fade_status() != 2) {
                return -1;
            }
            POSITIVE_SE();
            systemStatus[4] = 1;
            nextStage = gFlagSaveStage;
            releaseGameLoopCursor();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN(0x3F, 0x3C);
        case 430:
            if ((pad[0].flags & PAL_JPNKORCN(0x40, 0x20)) == 0 || lt_fade_status() != 2) {
                return -1;
            }
            POSITIVE_SE();
            optionScreenMode = 0;
            gflagInit();
            nextStage = 1;
            releaseGameLoopCursor();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN(0x3F, 0x3C);
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
        lt_mask_property(i + PAL_JPNCN_KOR_NTSCAUG6JUN26(342, 319, 313, 291), 1);
        lt_default_mask_property(i + PAL_JPNCN_KOR_NTSCAUG6JUN26(342, 319, 313, 291), 1);
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

static int la_key_config_jun26(int first)
{
    int i;
    int sel;
    int m;
    int k;
    int n;

    sel = lt_current_property_item() - 285;
    if (first) {
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
        lt_mask_property(i * 8 + 291 + keyConfigSlot[i], 0);
    }
    if (PSH_POSITIVE(0)) {
        if (lt_current_property_item() == 340) {
            for (i = 7; i >= 0; i--) {
                keyConfigSlot[i] = i;
            }
        }
        if (lt_current_property_item() == 339) {
            for (i = 0; i < 16; i++) {
                iosPadConfCustom.bit[i] = 0;
            }
            for (i = 0; i < 8; i++) {
                iosPadConfCustom.bit[keyBitIndex(keyConfigCode[keyConfigSlot[i]])] =
                    keyConfigCode[i];
            }
            keyconfigMaskAll();
            for (i = 0; i < 6; i++) {
                lt_default_mask_property(i * 8 + 291 + keyConfigSlot[i], 0);
                lt_mask_property(i * 8 + 291 + keyConfigSlot[i], 0);
            }
            lt_set_item_select_func(0);
            actionStarted = 0;
            /* PC port: back to Settings > Controls, which opens this
               screen now that the Options screen (58) is not reached */
            return ui_settings_key_config_back();
        }
    }
    if (pad[0].flags & 0x800) {
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 50;
    }
    return -1;
}

static int la_key_config_ntscaug6(int first)
{
    int i;
    int sel;
    int m;
    int k;
    int n;

    sel = lt_current_property_item() - 285;
    if (first) {
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
        lt_mask_property(i * 8 + 291 + keyConfigSlot[i], 0);
    }
    if (pad[0].flags & 0x40) {
        if (lt_current_property_item() == 340) {
            for (i = 7; i >= 0; i--) {
                keyConfigSlot[i] = i;
            }
            NEGATIVE_SE();
        }
        if (lt_current_property_item() == 339) {
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
                lt_default_mask_property(i * 8 + 291 + keyConfigSlot[i], 0);
                lt_mask_property(i * 8 + 291 + keyConfigSlot[i], 0);
            }
            lt_set_item_select_func(0);
            actionStarted = 0;
            /* PC port: back to Settings > Controls, which opens this
               screen now that the Options screen (58) is not reached */
            return ui_settings_key_config_back();
        }
    }
    if (pad[0].flags & 0x800) {
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 50;
    }
    return -1;
}

int la_key_config(int first)
{
    if (ICO_JUN26) {
        return la_key_config_jun26(first);
    }
    if (ICO_NTSCAUG6) {
        return la_key_config_ntscaug6(first);
    }
    int i;
    int sel;
    int m;
    int k;
    int n;

    sel = lt_current_property_item() - PAL_JPNCN_KOR(336, 313, 307);
    if (first) {
        texLayout[PAL_JPNKORCN(58, 55)].defaultItem = PAL_JPNCN_KOR(323, 300, 294);
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
        lt_mask_property(i * 8 + PAL_JPNCN_KOR(342, 319, 313) + keyConfigSlot[i], 0);
        lt_default_mask_property(i * 8 + PAL_JPNCN_KOR(342, 319, 313) + keyConfigSlot[i], 0);
    }
    if (pad[0].flags & PAL_JPNKORCN(0x40, 0x20)) {
        if (lt_current_property_item() == PAL_JPNCN_KOR(391, 368, 362)) {
            for (i = 7; i >= 0; i--) {
                keyConfigSlot[i] = i;
            }
            NEGATIVE_SE();
        }
        if (lt_current_property_item() == PAL_JPNCN_KOR(390, 367, 361)) {
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
                lt_default_mask_property(i * 8 + PAL_JPNCN_KOR(342, 319, 313) + keyConfigSlot[i],
                                         0);
                lt_mask_property(i * 8 + PAL_JPNCN_KOR(342, 319, 313) + keyConfigSlot[i], 0);
            }
            lt_set_item_select_func(0);
            actionStarted = 0;
            /* PC port: back to Settings > Controls, which opens this
               screen now that the Options screen (58) is not reached */
            return ui_settings_key_config_back();
        }
    }
    return -1;
}

/* the option screen's layout items: the five screen modes, the stage
   animation each mode plays (-1 for none), and the two choices of the control
   type (item 318, JPN 295, KOR 289) followed by the two of the setting item 325 (JPN 302, KOR
   296) toggles, in one
   table; the code reaches the second pair through an offset pointer */

static const int screenModeItem_kor[5] = {274, 275, 276, 277, 278}; /* derived name */

static const int screenModeItem_jpn[5] = {280, 281, 282, 283, 284}; /* derived name */

static const int screenModeItem_pal[5] = {303, 304, 305, 306, 307}; /* derived name */

static const int screenModeAnim_jpn[5] = {-1, 63, 64, 65, 66}; /* derived name */

static const int screenModeAnim_pal[5] = {-1, 67, 68, 69, 70}; /* derived name */

static const int choiceItem_kor[4] = {292, 293, 299, 300}; /* derived name */

static const int choiceItem_jpn[4] = {298, 299, 305, 306}; /* derived name */

static const int choiceItem_pal[4] = {321, 322, 328, 329}; /* derived name */

#define screenModeItem PAL_JPNCN_KOR(screenModeItem_pal, screenModeItem_jpn, screenModeItem_kor)
#define screenModeAnim PAL_JPNKORCN(screenModeAnim_pal, screenModeAnim_jpn)
#define choiceItem PAL_JPNCN_KOR(choiceItem_pal, choiceItem_jpn, choiceItem_kor)

/* not reached on the port on any disc (the pause menu's Options opens Settings), so the USA
   disc's and the August 2001 preview's own Options screen is not carried */
/* PC port: the USA disc's and the August 2001 preview's option screen, their own text (two
   rows: the sound output and the vibration, and the back item) */
static int la_game_option_ntscaug6(void)
{
    int mode;

    mode = soundOutputModeGet();
    if ((pad[0].flags & 0x40) != 0) {
        switch (lt_current_property_item()) {
        case 269:
            POSITIVE_SE();
            if (mode == 0) {
                mode = 1;
            } else {
                mode = 0;
            }
            soundOutputModeSet(mode);
            ico_opt_output_toggled(mode); /* PC port: the output mode is kept in config.toml */
            break;
        case 274:
            POSITIVE_SE();
            iosPadActRequestEnable ^= 1;
            break;
        case 281:
            NEGATIVE_SE();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return 53;
        }
    }
    if (mode == 0) {
        lt_default_mask_property(272, 0);
        lt_default_mask_property(273, 1);
    } else {
        lt_default_mask_property(272, 1);
        lt_default_mask_property(273, 0);
    }
    if (iosPadActRequestEnable != 0) {
        lt_default_mask_property(277, 0);
        lt_default_mask_property(278, 1);
    } else {
        lt_default_mask_property(277, 1);
        lt_default_mask_property(278, 0);
    }
    return -1;
}

/* PC port: the June 2001 demo's option screen (upstream's JUN26 arm): the sound output and the
   vibration rows, no back item */
static int la_game_option_jun26(void)
{
    int mode;

    mode = soundOutputModeGet();
    if (PSH_POSITIVE(0)) {
        switch (lt_current_property_item()) {
        case 269:
            if (mode == 0) {
                mode = 1;
            } else {
                mode = 0;
            }
            soundOutputModeSet(mode);
            ico_opt_output_toggled(mode); /* PC port: the output mode is kept in config.toml */
            break;
        case 274:
            iosPadActRequestEnable ^= 1;
            break;
        }
    }
    if (mode == 0) {
        lt_default_mask_property(272, 0);
        lt_default_mask_property(273, 1);
    } else {
        lt_default_mask_property(272, 1);
        lt_default_mask_property(273, 0);
    }
    if (iosPadActRequestEnable != 0) {
        lt_default_mask_property(277, 0);
        lt_default_mask_property(278, 1);
    } else {
        lt_default_mask_property(277, 1);
        lt_default_mask_property(278, 0);
    }
    return -1;
}

int la_game_option(void)
{
    if (ICO_NTSCAUG6) {
        return la_game_option_ntscaug6();
    }
    if (ICO_JUN26) {
        return la_game_option_jun26();
    }
    int mode;
    int cur;
    int sel;
    int item;

    mode = soundOutputModeGet();
    lt_analog2Pad();
    if ((pad[0].now & 0xA000) != 0) {
        cur = optionControlType;
        sel = optionScreenMode;
        switch (
            lt_current_property_item() +
            PAL_JPNCN_KOR(0, 23, 29)) { /* JPN property ids 23 lower, KOR property ids 29 lower */
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
                ico_opt_hold_type_toggled(cur); /* PC port: the type is kept in config.toml */
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
                ico_opt_output_toggled(mode);
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
    if ((pad[0].flags & PAL_JPNKORCN(0x40, 0x20)) != 0) {
        if (lt_current_property_item() == PAL_JPNCN_KOR(330, 307, 301)) {
            NEGATIVE_SE();
            lt_set_item_select_func(0);
            actionStarted = 0;
            return PAL_JPNKORCN(57, 54);
        }
    }
    if (mode == 0) {
        lt_default_mask_property(PAL_JPNCN_KOR(311, 288, 282), 0);
        lt_default_mask_property(PAL_JPNCN_KOR(312, 289, 283), 1);
    } else {
        lt_default_mask_property(PAL_JPNCN_KOR(311, 288, 282), 1);
        lt_default_mask_property(PAL_JPNCN_KOR(312, 289, 283), 0);
    }
    if (iosPadActRequestEnable != 0) {
        lt_default_mask_property(PAL_JPNCN_KOR(316, 293, 287), 0);
        lt_default_mask_property(PAL_JPNCN_KOR(317, 294, 288), 1);
    } else {
        lt_default_mask_property(PAL_JPNCN_KOR(316, 293, 287), 1);
        lt_default_mask_property(PAL_JPNCN_KOR(317, 294, 288), 0);
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
        lt_default_mask_property(i + PAL_JPNCN_KOR_NTSCAUG6JUN26(395, 372, 366, 344), 1);
    }
}

static int la_adjust_screen_jun26(void)
{
    if (PSH_LEFT(0)) {
        if (systemStatus[11] > 0) {
            systemStatus[11]--;
        }
    } else if (PSH_RIGHT(0)) {
        if (systemStatus[11] < 14) {
            systemStatus[11]++;
        }
    }
    if (PSH_NEGATIVE(0)) {
        systemStatus[11] = 7;
    }
    clearAdjustScreenMarks();
    lt_default_mask_property(systemStatus[11] + 344, 0);
    if (PSH_POSITIVE(0)) {
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 54;
    }
    if (pad[0].flags & 0x800) {
        lt_set_item_select_func(0);
        actionStarted = 0;
        return 50;
    }
    return -1;
}

int la_adjust_screen(void)
{
    if (ICO_JUN26) {
        return la_adjust_screen_jun26();
    }
    int v;

    if (ICO_KOR) {
        texLayout[55].defaultItem = 295;
    } else if (ICO_JPNCN) {
        texLayout[55].defaultItem = 301;
    } else if (ICO_NTSCAUG6JUN26) {
    } else {
        texLayout[58].defaultItem = 324;
    }
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
    if (pad[0].flags & PAL_JPNKORCN(0x10, 0x40)) {
        NEGATIVE_SE();
        systemStatus[11] = 7;
    }
    clearAdjustScreenMarks();
    lt_default_mask_property(systemStatus[11] + PAL_JPNCN_KOR_NTSCAUG6JUN26(395, 372, 366, 344), 0);
    if (pad[0].flags & PAL_JPNKORCN(0x40, 0x20)) {
        POSITIVE_SE();
        lt_set_item_select_func(0);
        actionStarted = 0;
        return PAL_JPNKORCN_NTSCAUG6JUN26(0x3A, 0x37, 54);
    }
    return -1;
}

/* PC port: what every proc does before it returns the layout it
   switches to, for the port's Settings procs (actionStarted is this file's) */
void la_host_leave(void)
{
    lt_set_item_select_func(0);
    actionStarted = 0;
}

/* PC port: what the pause menu's Back does before it returns la_game_loop
   (la_game_pause and its twins), for the port's Settings procs that close
   the menu into the game (Game variants > Debug menu); the caller returns
   the region's la_game_loop */
void la_host_resume_play(void)
{
    adpcmPauseRequest(0);
    la_host_leave();
}

/* PC port: Settings > Gameplay > Film effect, what la_game_option does
   when row 300 changes the mode: the old mode's stage animation stopped, the
   new one's started */
void la_host_film_effect(int mode)
{
    int item;

    if (mode < 0 || mode >= 5 || mode == optionScreenMode) {
        return;
    }
    if (optionScreenMode >= 0 && optionScreenMode < 5 && screenModeAnim[optionScreenMode] != -1) {
        stage_SetLoopFlag(screenModeAnim[optionScreenMode], 0);
        stage_SetAnimation(screenModeAnim[optionScreenMode], -1, -2);
    }
    item = screenModeAnim[mode];
    if (item != -1) {
        stage_SetLoopFlag(item, 1);
        stage_SetAnimation(item, 1, 0);
    }
    optionScreenMode = mode;
}

/* PC port: the model viewer leaves the title as a new game
   does, the title music fading (la_host_new_game_go), and comes back to it
   the way la_end_confirm's Yes ends a game */
void la_host_title_music_fade(void)
{
    if (titleAdpcm != 0) {
        titleAdpcm->stream->fadeStep = 0x80;
    }
    titleAdpcm = 0;
}

void la_host_end_game(void)
{
    optionScreenMode = 0;
    gflagInit();
    fightSoundProcessRequestPause();
    fightSoundClose();
    soundDataSegAllClose(0, 2);
    nextStage = 1;
    stgmgrForceSwitchWithFade(1, 0.025f, 4.0f);
}

unsigned int stage_after_skipping_demo = 0;

int layoutActPushStartNew = 0;
