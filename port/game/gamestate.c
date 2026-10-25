/*
 * port/game/gamestate.c
 *
 * The read-only game-state view and the signal queue (port/include/
 * ico_gamestate.h).
 *
 * Built twice: with ICO_GS_LIVE for the program (the sampler reads the
 * game's globals and, when they are safe to follow, the boy's and girl's
 * objects through the game's own accessors), and without it for the tests,
 * which install a synthetic sampler.  Nothing here writes game memory.
 */
#include "ico_gamestate.h"
#include "ico_credits.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "options.h"
#include "variants.h"

#include "../core/region.h" /* ico_region, ico_region_cur: the disc's row of config/regions.txt */

#ifdef ICO_GS_LIVE
#include "typedef.h"

/* The game's globals the snapshot copies (each defined in the file named). */
extern int stage_no;                        /* common/src/main.c */
extern int before_stage_no;                 /* common/src/main.c */
extern int current_stage_no;                /* common/src/main.c */
extern int gameover_flag;                   /* common/src/main.c */
extern int gameover_layout_flag;            /* common/src/main.c */
extern int frame_count;                     /* common/src/main.c */
extern int NonLinearCameraMove;             /* common/src/main.c (PAL only) */
extern int systemStatus[12];                /* common/src/main.c */
extern PadState pad[16];                    /* common/src/main.c, main.h */
extern GObj *boyGObj;                       /* common/src/main.c */
extern GObj *girlGObj;                      /* common/src/main.c */
extern int gamesysTimeCount;                /* common/src/gamesys.c */
extern int gFlagGameClear;                  /* script/src/gflag.c */
extern int gFlagSaveStage;                  /* script/src/gflag.c */
extern int current_layout_id;               /* common/src/layout_texture.c */
extern int IosMcPreviewInfo[];              /* fumi/ios/mcard.c, int[6] */
extern int mpegPlay;                        /* common/src/StageManager.c */
extern int stageManagerFreeResourceFlag;    /* common/src/StageManager.c */
int gflagChk(int bit_idx);                  /* script/src/gflag.c */
int CheckWeaponKind(struct GObj *self);     /* sugipon/src/weapon.c */
unsigned char ACTGame_FLAG_TETSUNAGI(void); /* fumi/src/act-game.c */

#define STAGE_COUNT (ico_region_cur()->stage_count) /* stageData[106], 104 on the Japanese disc */
#define ENEMY_KIND 4     /* GObj.kind of an enemy (isEnemyActive, enemy_act.c) */
#define ACT_CARRIED 0x6F /* the girl's carried action (enemy_act.c:1153) */

void ico_gs_sample_live(IcoGsSnapshot *s)
{
    int i, nflags;

    memset(s, 0, sizeof(*s));
    s->valid = 1;
    s->stage_no = stage_no;
    s->before_stage_no = before_stage_no;
    s->current_stage_no = current_stage_no;
    s->gameover_flag = gameover_flag;
    s->gameover_layout_flag = gameover_layout_flag;
    s->frame_count = frame_count;
    /* the game keeps the language here; the Japanese disc has no such word
       (its ELF defines none, and no JPN code reads one) */
    s->language = ico_region == ICO_REGION_PAL ? NonLinearCameraMove : -1;
    s->time_count = gamesysTimeCount;
    s->game_clear = gFlagGameClear;
    s->save_stage = gFlagSaveStage;
    s->layout = current_layout_id;
    memcpy(s->system_status, systemStatus, sizeof(s->system_status));
    memcpy(s->mc_preview, IosMcPreviewInfo, sizeof(s->mc_preview));
    /* the disc's own flag count: gFlag is 46 bytes on the North American and
       August 2001 discs, 44 on the June 2001 demo (region.h gflag_count), and
       gflagChk past the array would read the next global */
    nflags = ico_region_cur()->gflag_count;
    if (nflags > ICO_GS_GFLAG_BYTES * 8) {
        nflags = ICO_GS_GFLAG_BYTES * 8;
    }
    for (i = 0; i < nflags; i++) {
        if (gflagChk(i)) {
            s->gflags[i >> 3] |= (unsigned char)(1u << (i & 7));
        }
    }
    s->stage_name =
        (stage_no >= 0 && stage_no < STAGE_COUNT) ? stageData[stage_no].name : (const char *)0;
    s->weapon_kind = 0;
    /* The objects are followed only while no stage is being torn down or
       loaded and no movie runs: stop_free_resources (StageManager.c) frees
       the object partitions and then zeroes boyGObj and girlGObj, and the
       movie path zeroes them too. */
    if (stageManagerFreeResourceFlag != 0 || mpegPlay != 0 || boyGObj == 0 ||
        GOBJ_ACT(boyGObj) == 0) {
        return;
    }
    s->actors_valid = 1;
    s->boy_present = 1;
    {
        Act *a = GOBJ_ACT(boyGObj);

        /* scpGameStat_BoyWeaponkind (script.c:1541), with the null checks a
           snapshot needs */
        if (a->weapon != 0 && GOBJ_SUB(a->weapon) != 0 && GOBJ_SUB(a->weapon)->work != 0) {
            s->weapon_kind = CheckWeaponKind(a->weapon);
        }
    }
    if (girlGObj != 0 && GOBJ_ACT(girlGObj) != 0) {
        Act *g = GOBJ_ACT(girlGObj);

        s->girl_present = 1;
        s->hand_held = ACTGame_FLAG_TETSUNAGI() != 0;
        s->yorda_carried_by_enemy =
            g->actMode == ACT_CARRIED && g->carrier != 0 && g->carrier->kind == ENEMY_KIND;
    }
}

/* pad[0].now and .flags as the last ExecKeyInput left them, for the movie
   player's "fmv: pad" log line (port/fmv/movie.c) */
void ico_game_pad0(int *now, int *flags)
{
    *now = pad[0].now;
    *flags = pad[0].flags;
}

#endif /* ICO_GS_LIVE */

/* --- signals ------------------------------------------------------------ */

#define SIGNAL_QUEUE 128

typedef struct Signal {
    int event;
    int arg;
} Signal;

static Signal s_pending[SIGNAL_QUEUE];
static int s_pending_n;
static unsigned int s_dropped;

static int s_count[ICO_GS_EV_COUNT];
static int s_arg[ICO_GS_EV_COUNT];

void ico_gs_signal(int event, int arg)
{
    if (event <= ICO_GS_EV_NONE || event >= ICO_GS_EV_COUNT) {
        return;
    }
    if (s_pending_n >= SIGNAL_QUEUE) {
        s_dropped++;
        return;
    }
    s_pending[s_pending_n].event = event;
    s_pending[s_pending_n].arg = arg;
    s_pending_n++;
}

static void raise_now(IcoGsEvent ev, int arg)
{
    s_count[ev]++;
    s_arg[ev] = arg;
}

int ico_gs_signaled(IcoGsEvent event)
{
    return (event > ICO_GS_EV_NONE && event < ICO_GS_EV_COUNT) ? s_count[event] : 0;
}

int ico_gs_signal_arg(IcoGsEvent event)
{
    return (event > ICO_GS_EV_NONE && event < ICO_GS_EV_COUNT) ? s_arg[event] : 0;
}

unsigned int ico_gs_signals_dropped(void)
{
    return s_dropped;
}

/* --- state ---------------------------------------------------------------- */

static IcoGsSampler s_sampler;
static IcoGsSnapshot s_snap, s_prev;
static unsigned int s_ticks;
static int s_stage_entered;

static unsigned int s_enemies, s_game_overs, s_saves;

static struct {
    int fresh;
    unsigned int captures;
    unsigned int game_overs;
    int opening_skipped;
    unsigned int opening_parts;
    int suspended;
    unsigned int saves;   /* saves done in this run (journey), restored with its slot */
    unsigned int enemies; /* enemies defeated in this run, restored with its slot */
    int partial;          /* saves and enemies count only from the slot's load */
} s_run;

void ico_gs_set_sampler(IcoGsSampler fn)
{
    s_sampler = fn;
}

void ico_gs_reset(void)
{
    memset(&s_snap, 0, sizeof(s_snap));
    memset(&s_prev, 0, sizeof(s_prev));
    memset(s_count, 0, sizeof(s_count));
    memset(s_arg, 0, sizeof(s_arg));
    memset(&s_run, 0, sizeof(s_run));
    s_pending_n = 0;
    s_dropped = 0;
    s_ticks = 0;
    s_stage_entered = 0;
    s_enemies = s_game_overs = s_saves = 0;
}

static int flag_in(const IcoGsSnapshot *s, int n)
{
    if (n < 0 || n >= ICO_GS_GFLAG_BYTES * 8) {
        return 0;
    }
    return (s->gflags[n >> 3] >> (n & 7)) & 1;
}

/* layout ids (texLayout procs in the retail tables) */
/* the running disc's (port/core/region.h; 25, 41, 382 and 1 on the
   European disc) */
#define LAYOUT_LOAD_PROCESSING (ico_region_cur()->layout_load) /* la_load_processing */
/* la_save_confirm_complete, la_save_processing's success */
#define LAYOUT_SAVE_COMPLETE (ico_region_cur()->layout_save_complete)
#define GFLAG_NEW_GAME (ico_region_cur()->gflag_new_game) /* gflagOn(382), layout_action.c:738 */
#define STAGE_TITLE (ico_region_cur()->stage_title)

void ico_gs_tick(void)
{
    int i;
    int demo_skipped;
    unsigned int demo_watched;

    s_prev = s_snap;
    memset(&s_snap, 0, sizeof(s_snap));
    if (s_sampler != NULL) {
        s_sampler(&s_snap);
    }
#ifdef ICO_GS_LIVE
    else {
        ico_gs_sample_live(&s_snap);
    }
#endif
    s_ticks++;

    /* this tick's signals: the ones raised since the last tick */
    memset(s_count, 0, sizeof(s_count));
    demo_skipped = 0;
    demo_watched = 0;
    for (i = 0; i < s_pending_n; i++) {
        raise_now((IcoGsEvent)s_pending[i].event, s_pending[i].arg);
        /* DEMO_END: part * 2, plus 1 when START skipped it */
        if (s_pending[i].event == ICO_GS_EV_DEMO_END) {
            int part = s_pending[i].arg >> 1;

            if (s_pending[i].arg & 1) {
                demo_skipped = 1;
            } else if (part >= 1 && part <= ICO_GS_OPENING_PARTS) {
                demo_watched |= 1u << (part - 1);
            }
        }
    }
    s_pending_n = 0;

    if (!s_snap.valid) {
        s_stage_entered = 0;
        return;
    }

    /* polled events */
    s_stage_entered =
        s_count[ICO_GS_EV_STAGE_ENTER] != 0 || (s_prev.valid && s_prev.stage_no != s_snap.stage_no);
    if (s_prev.valid && s_snap.layout == LAYOUT_SAVE_COMPLETE &&
        s_prev.layout != LAYOUT_SAVE_COMPLETE) {
        raise_now(ICO_GS_EV_SAVE_DONE, s_snap.mc_preview[3]);
    }
    if (s_prev.valid && s_snap.layout == LAYOUT_LOAD_PROCESSING &&
        s_prev.layout != LAYOUT_LOAD_PROCESSING) {
        raise_now(ICO_GS_EV_LOAD, 0);
    }
    if (s_prev.valid && flag_in(&s_snap, GFLAG_NEW_GAME) && !flag_in(&s_prev, GFLAG_NEW_GAME)) {
        raise_now(ICO_GS_EV_NEW_GAME, 0);
    }
    if (s_prev.actors_valid && s_snap.actors_valid && s_prev.stage_no == s_snap.stage_no &&
        s_prev.yorda_carried_by_enemy && !s_snap.yorda_carried_by_enemy && s_snap.girl_present &&
        !s_snap.gameover_flag) {
        raise_now(ICO_GS_EV_YORDA_RESCUED, 0);
    }

    /* the run: reset at the title; fresh from a new game; a load ends it */
    if (s_stage_entered && s_snap.stage_no == STAGE_TITLE) {
        memset(&s_run, 0, sizeof(s_run));
    }
    if (s_count[ICO_GS_EV_NEW_GAME]) {
        memset(&s_run, 0, sizeof(s_run));
        s_run.fresh = 1;
    }
    if (s_count[ICO_GS_EV_LOAD]) {
        s_run.fresh = 0;
    }
    if (s_count[ICO_GS_EV_YORDA_GRABBED] != 0) {
        s_run.captures += (unsigned int)s_count[ICO_GS_EV_YORDA_GRABBED];
    } else if (s_snap.yorda_carried_by_enemy && !s_prev.yorda_carried_by_enemy &&
               s_prev.actors_valid) {
        s_run.captures++; /* a capture the hook did not see */
    }
    if (demo_skipped) {
        s_run.opening_skipped = 1;
    }
    s_run.opening_parts |= demo_watched;
    s_run.game_overs += (unsigned int)s_count[ICO_GS_EV_GAME_OVER];
    s_run.saves += (unsigned int)s_count[ICO_GS_EV_SAVE_DONE];
    s_run.enemies += (unsigned int)s_count[ICO_GS_EV_ENEMY_KILLED];
    if (ico_gs_achievements_suspended()) {
        s_run.suspended = 1;
    }

    s_enemies += (unsigned int)s_count[ICO_GS_EV_ENEMY_KILLED];
    s_game_overs += (unsigned int)s_count[ICO_GS_EV_GAME_OVER];
    s_saves += (unsigned int)s_count[ICO_GS_EV_SAVE_DONE];
}

unsigned int ico_gs_ticks(void)
{
    return s_ticks;
}

/* --- queries -------------------------------------------------------------- */

int ico_gs_valid(void)
{
    return s_snap.valid;
}

int ico_gs_stage(void)
{
    return s_snap.stage_no;
}

const char *ico_gs_stage_name(void)
{
    return s_snap.stage_name != NULL ? s_snap.stage_name : "";
}

int ico_gs_stage_entered(void)
{
    return s_stage_entered;
}

int ico_gs_flag(int n)
{
    return flag_in(&s_snap, n);
}

int ico_gs_flag_rose(int n)
{
    return s_prev.valid && flag_in(&s_snap, n) && !flag_in(&s_prev, n);
}

int ico_gs_game_clear(void)
{
    return s_snap.game_clear;
}

int ico_gs_tick_hz(void)
{
    int s1 = s_snap.system_status[1];

    if (s1 <= 0) {
        return 25;
    }
    return (60 - s_snap.system_status[0] * 10) / s1;
}

unsigned int ico_gs_play_frames(void)
{
    return s_snap.mc_preview[2] > 0 ? (unsigned int)s_snap.mc_preview[2] : 0u;
}

unsigned int ico_gs_play_seconds(void)
{
    /* playTime (layout_action.c:1021): frames / (((60 - s0 * 10) / s1) * s1) */
    int fps = ico_gs_tick_hz() * (s_snap.system_status[1] > 0 ? s_snap.system_status[1] : 2);

    return fps > 0 ? ico_gs_play_frames() / (unsigned int)fps : 0u;
}

int ico_gs_paused(void)
{
    return s_snap.system_status[5] != 0;
}

int ico_gs_weapon_kind(void)
{
    return s_snap.weapon_kind;
}

int ico_gs_yorda_present(void)
{
    return s_snap.girl_present;
}

int ico_gs_yorda_held(void)
{
    return s_snap.hand_held;
}

int ico_gs_yorda_captured(void)
{
    return s_snap.yorda_carried_by_enemy;
}

unsigned int ico_gs_enemies_killed(void)
{
    return s_enemies;
}

unsigned int ico_gs_game_overs(void)
{
    return s_game_overs;
}

unsigned int ico_gs_saves(void)
{
    return s_saves;
}

int ico_gs_run_fresh(void)
{
    return s_run.fresh;
}

unsigned int ico_gs_run_captures(void)
{
    return s_run.captures;
}

unsigned int ico_gs_run_game_overs(void)
{
    return s_run.game_overs;
}

unsigned int ico_gs_run_saves(void)
{
    return s_run.saves;
}

int ico_gs_run_partial(void)
{
    return s_run.partial;
}

unsigned int ico_gs_run_enemies(void)
{
    return s_run.enemies;
}

int ico_gs_run_opening_skipped(void)
{
    return s_run.opening_skipped;
}

int ico_gs_run_suspended(void)
{
    return s_run.suspended;
}

unsigned int ico_gs_run_opening_parts(void)
{
    return s_run.opening_parts;
}

void ico_gs_run_get(IcoGsRun *out)
{
    out->fresh = s_run.fresh;
    out->captures = s_run.captures;
    out->game_overs = s_run.game_overs;
    out->opening_parts = s_run.opening_parts;
    out->opening_skipped = s_run.opening_skipped;
    out->suspended = s_run.suspended;
    out->saves = s_run.saves;
    out->enemies = s_run.enemies;
    out->partial = s_run.partial;
}

void ico_gs_run_set(const IcoGsRun *in)
{
    s_run.fresh = in->fresh != 0;
    s_run.captures = in->captures;
    s_run.game_overs = in->game_overs;
    s_run.opening_parts = in->opening_parts & ((1u << ICO_GS_OPENING_PARTS) - 1u);
    s_run.opening_skipped = in->opening_skipped != 0;
    s_run.suspended = in->suspended != 0;
    s_run.saves = in->saves;
    s_run.enemies = in->enemies;
    s_run.partial = in->partial != 0;
}

int ico_gs_developer_mode(void)
{
    return ico_opt_developer_mode();
}

int ico_gs_yorda_safe(void)
{
    return ico_opt_yorda_safe();
}

int ico_gs_start_stage_used(void)
{
    /* the value debug_TryToGetStartStage (common/src/debug.c) takes: 2..105
       (2..103 on the Japanese disc) starts somewhere other than the boot */
    const char *v = getenv("ICO_START_STAGE");
    char *end;
    long n;

    if (v == NULL || v[0] == '\0') {
        return 0;
    }
    n = strtol(v, &end, 10);
    return *end == '\0' && n > 1 && n <= ico_region_cur()->start_stage_max;
}

int ico_gs_achievements_suspended(void)
{
    /* the model viewer's stages are not play, nor the Extras credits'
       playback of the ending; One hit per swing, Shadows never dodge,
       Cutscene cameras ignored and the Debug menu (Game variants) are
       cheats like developer mode */
    return ico_gs_developer_mode() || ico_gs_start_stage_used() || ico_mv_active ||
           ico_credits_active() || ico_variants_suspend_achievements();
}

/* --- retail addresses ---------------------------------------------------- */

/* Each entry: the retail address and size on a disc, and where this tick's
   copy sits in the snapshot; one table per row of config/regions.txt, picked
   by ico_region at the read.  PAL sources: config/symbol_addrs.pal.data.txt
   for the main.c and gamesys.c globals; for the rest the retail code's own
   address arithmetic.  JPN sources: config/symbol_addrs.jpn.data.txt (the
   decompilation's link of the Japanese ELF), each address also in its link
   map (build/jpn/ico.jpn.map) but gflags, a static. KOR sources the same:
   config/symbol_addrs.kor.data.txt and build/kor/ico.kor.map. */
typedef struct PeekRow {
    IcoGsPeekEntry e;
    size_t offset;
} PeekRow;

#define ROW(addr, field, name)                                                                     \
    {{(addr), sizeof(((IcoGsSnapshot *)0)->field), (name)}, offsetof(IcoGsSnapshot, field)}

/* a row whose game-side object is smaller than the snapshot's field (the
   served size is the disc's own) */
#define ROWN(addr, field, bytes, name) {{(addr), (bytes), (name)}, offsetof(IcoGsSnapshot, field)}

static const PeekRow s_peek_pal[] = {
    ROW(0x002A50C0u, gflags, "gflags"),                             /* gflagChk 0x00181A48 */
    ROW(0x0028F4C0u, system_status, "systemStatus"),                /* la_game_loading */
    ROW(0x0029B9D0u, mc_preview, "IosMcPreviewInfo"),               /* la_playtime_count */
    ROW(0x00639CE0u, frame_count, "frame_count"),                   /* data.txt:269 */
    ROW(0x00639D10u, stage_no, "stage_no"),                         /* data.txt:272 */
    ROW(0x00639D20u, before_stage_no, "before_stage_no"),           /* data.txt:273 */
    ROW(0x00639D70u, language, "NonLinearCameraMove"),              /* data.txt:278 */
    ROW(0x00639EB4u, gameover_flag, "gameover_flag"),               /* data.txt:301 */
    ROW(0x00639EB8u, gameover_layout_flag, "gameover_layout_flag"), /* data.txt:302 */
    ROW(0x00639ED4u, current_stage_no, "current_stage_no"),         /* data.txt:306 */
    ROW(0x0063AA00u, game_clear, "gFlagGameClear"),                 /* gflagLoad, gp-24816 */
    ROW(0x0063AA04u, save_stage, "gFlagSaveStage"),                 /* gflagLoad, gp-24812 */
    ROW(0x0063B414u, time_count, "gamesysTimeCount"),               /* data.txt:355 */
    ROW(0x0063B60Cu, layout, "current_layout_id"),                  /* lt_switch_layout, gp-21732 */
};

/* The Japanese disc (SCPS-11003): no NonLinearCameraMove (the snapshot's
   language reads -1) */
static const PeekRow s_peek_jpn[] = {
    ROW(0x002A2FC0u, gflags, "gflags"),                             /* jpn.data.txt (static) */
    ROW(0x0028D3C0u, system_status, "systemStatus"),                /* jpn.data.txt, map */
    ROW(0x002998C0u, mc_preview, "IosMcPreviewInfo"),               /* jpn.data.txt, map */
    ROW(0x006337E0u, frame_count, "frame_count"),                   /* jpn.data.txt, map */
    ROW(0x00633810u, stage_no, "stage_no"),                         /* jpn.data.txt, map */
    ROW(0x00633820u, before_stage_no, "before_stage_no"),           /* jpn.data.txt, map */
    ROW(0x006339A4u, gameover_flag, "gameover_flag"),               /* jpn.data.txt, map */
    ROW(0x006339A8u, gameover_layout_flag, "gameover_layout_flag"), /* jpn.data.txt, map */
    ROW(0x006339C4u, current_stage_no, "current_stage_no"),         /* jpn.data.txt, map */
    ROW(0x006344C8u, game_clear, "gFlagGameClear"),                 /* jpn.data.txt, map */
    ROW(0x006344CCu, save_stage, "gFlagSaveStage"),                 /* jpn.data.txt, map */
    ROW(0x00634ECCu, time_count, "gamesysTimeCount"),               /* jpn.data.txt, map */
    ROW(0x006350BCu, layout, "current_layout_id"),                  /* jpn.data.txt, map */
};

/* The Korean disc (SCPS-56001): the Japanese program's globals, at the
   Korean link's addresses (no NonLinearCameraMove either) */
static const PeekRow s_peek_kor[] = {
    ROW(0x002A3040u, gflags, "gflags"),                             /* kor.data.txt (static) */
    ROW(0x0028D440u, system_status, "systemStatus"),                /* kor.data.txt, map */
    ROW(0x00299940u, mc_preview, "IosMcPreviewInfo"),               /* kor.data.txt, map */
    ROW(0x006335E0u, frame_count, "frame_count"),                   /* kor.data.txt, map */
    ROW(0x00633610u, stage_no, "stage_no"),                         /* kor.data.txt, map */
    ROW(0x00633620u, before_stage_no, "before_stage_no"),           /* kor.data.txt, map */
    ROW(0x006337A4u, gameover_flag, "gameover_flag"),               /* kor.data.txt, map */
    ROW(0x006337A8u, gameover_layout_flag, "gameover_layout_flag"), /* kor.data.txt, map */
    ROW(0x006337C4u, current_stage_no, "current_stage_no"),         /* kor.data.txt, map */
    ROW(0x006342D8u, game_clear, "gFlagGameClear"),                 /* kor.data.txt, map */
    ROW(0x006342DCu, save_stage, "gFlagSaveStage"),                 /* kor.data.txt, map */
    ROW(0x00634CDCu, time_count, "gamesysTimeCount"),               /* kor.data.txt, map */
    ROW(0x00634ECCu, layout, "current_layout_id"),                  /* kor.data.txt, map */
};

/* The North American disc (SCUS-97113), the August 2001 preview and the June
   2001 demo: the same twelve rows, at each link's addresses (the lists were
   written from each link's symbol table: config/symbol_addrs.<slug>.data.txt).
   Three rows differ from the other discs: the story flags are gFlag (46
   bytes, 44 on the demo; gflag.c names it gFlag, not gflags), the card
   preview record is 16 bytes (IosMcPreviewInfo size:0x10), and there is no
   gFlagGameClear (no New Game+ on these discs: the snapshot's game_clear
   stays 0) and no language word served (NonLinearCameraMove is read for the
   European disc only). */
static const PeekRow s_peek_ntsc[] = {
    ROWN(0x0028A520u, gflags, 46, "gFlag"),                         /* ntsc.data.txt */
    ROW(0x00274EC0u, system_status, "systemStatus"),                /* ntsc.data.txt */
    ROWN(0x00280F78u, mc_preview, 16, "IosMcPreviewInfo"),          /* ntsc.data.txt */
    ROW(0x00631960u, frame_count, "frame_count"),                   /* ntsc.data.txt */
    ROW(0x00631990u, stage_no, "stage_no"),                         /* ntsc.data.txt */
    ROW(0x006319A0u, before_stage_no, "before_stage_no"),           /* ntsc.data.txt */
    ROW(0x00631AF0u, gameover_flag, "gameover_flag"),               /* ntsc.data.txt */
    ROW(0x00631AF4u, gameover_layout_flag, "gameover_layout_flag"), /* ntsc.data.txt */
    ROW(0x00631B14u, current_stage_no, "current_stage_no"),         /* ntsc.data.txt */
    ROW(0x006325B0u, save_stage, "gFlagSaveStage"),                 /* ntsc.data.txt */
    ROW(0x00632F74u, time_count, "gamesysTimeCount"),               /* ntsc.data.txt */
    ROW(0x0063314Cu, layout, "current_layout_id"),                  /* ntsc.data.txt */
};

static const PeekRow s_peek_aug6[] = {
    ROWN(0x00286890u, gflags, 46, "gFlag"),                         /* aug6.data.txt */
    ROW(0x00271240u, system_status, "systemStatus"),                /* aug6.data.txt */
    ROWN(0x0027D2F8u, mc_preview, 16, "IosMcPreviewInfo"),          /* aug6.data.txt */
    ROW(0x0062BAE0u, frame_count, "frame_count"),                   /* aug6.data.txt */
    ROW(0x0062BB10u, stage_no, "stage_no"),                         /* aug6.data.txt */
    ROW(0x0062BB20u, before_stage_no, "before_stage_no"),           /* aug6.data.txt */
    ROW(0x0062BC70u, gameover_flag, "gameover_flag"),               /* aug6.data.txt */
    ROW(0x0062BC74u, gameover_layout_flag, "gameover_layout_flag"), /* aug6.data.txt */
    ROW(0x0062BC94u, current_stage_no, "current_stage_no"),         /* aug6.data.txt */
    ROW(0x0062C710u, save_stage, "gFlagSaveStage"),                 /* aug6.data.txt */
    ROW(0x0062D0B4u, time_count, "gamesysTimeCount"),               /* aug6.data.txt */
    ROW(0x0062D28Cu, layout, "current_layout_id"),                  /* aug6.data.txt */
};

static const PeekRow s_peek_jun26[] = {
    ROWN(0x00272D30u, gflags, 44, "gFlag"),                         /* jun26.data.txt */
    ROW(0x0025D540u, system_status, "systemStatus"),                /* jun26.data.txt */
    ROWN(0x002695B8u, mc_preview, 16, "IosMcPreviewInfo"),          /* jun26.data.txt */
    ROW(0x006793E0u, frame_count, "frame_count"),                   /* jun26.data.txt */
    ROW(0x00679410u, stage_no, "stage_no"),                         /* jun26.data.txt */
    ROW(0x00679420u, before_stage_no, "before_stage_no"),           /* jun26.data.txt */
    ROW(0x00679570u, gameover_flag, "gameover_flag"),               /* jun26.data.txt */
    ROW(0x00679574u, gameover_layout_flag, "gameover_layout_flag"), /* jun26.data.txt */
    ROW(0x00679594u, current_stage_no, "current_stage_no"),         /* jun26.data.txt */
    ROW(0x0067A000u, save_stage, "gFlagSaveStage"),                 /* jun26.data.txt */
    ROW(0x0067A96Cu, time_count, "gamesysTimeCount"),               /* jun26.data.txt */
    ROW(0x0067AB3Cu, layout, "current_layout_id"),                  /* jun26.data.txt */
};

/* The Asian disc (SCPS-55001): the Korean program's globals at the Asian
   link's addresses (config/symbol_addrs.cn.data.txt; gflags, a static, from
   the link's symbol table), no NonLinearCameraMove either */
static const PeekRow s_peek_cn[] = {
    ROW(0x002A3040u, gflags, "gflags"),                             /* cn.data.txt (static) */
    ROW(0x0028D440u, system_status, "systemStatus"),                /* cn.data.txt */
    ROW(0x00299940u, mc_preview, "IosMcPreviewInfo"),               /* cn.data.txt */
    ROW(0x00633860u, frame_count, "frame_count"),                   /* cn.data.txt */
    ROW(0x00633890u, stage_no, "stage_no"),                         /* cn.data.txt */
    ROW(0x006338A0u, before_stage_no, "before_stage_no"),           /* cn.data.txt */
    ROW(0x00633A24u, gameover_flag, "gameover_flag"),               /* cn.data.txt */
    ROW(0x00633A28u, gameover_layout_flag, "gameover_layout_flag"), /* cn.data.txt */
    ROW(0x00633A44u, current_stage_no, "current_stage_no"),         /* cn.data.txt */
    ROW(0x00634548u, game_clear, "gFlagGameClear"),                 /* cn.data.txt */
    ROW(0x0063454Cu, save_stage, "gFlagSaveStage"),                 /* cn.data.txt */
    ROW(0x00634F4Cu, time_count, "gamesysTimeCount"),               /* cn.data.txt */
    ROW(0x0063513Cu, layout, "current_layout_id"),                  /* cn.data.txt */
};

typedef struct PeekTable {
    const PeekRow *rows;
    int count;
} PeekTable;

/* ico_gs_peek_table's copy holds PAL's rows, the most */
_Static_assert(sizeof(s_peek_jpn) / sizeof(s_peek_jpn[0]) <=
                   sizeof(s_peek_pal) / sizeof(s_peek_pal[0]),
               "peek rows");
_Static_assert(sizeof(s_peek_kor) / sizeof(s_peek_kor[0]) <=
                   sizeof(s_peek_pal) / sizeof(s_peek_pal[0]),
               "peek rows");
_Static_assert(sizeof(s_peek_ntsc) / sizeof(s_peek_ntsc[0]) <=
                   sizeof(s_peek_pal) / sizeof(s_peek_pal[0]),
               "peek rows");
_Static_assert(sizeof(s_peek_aug6) / sizeof(s_peek_aug6[0]) <=
                   sizeof(s_peek_pal) / sizeof(s_peek_pal[0]),
               "peek rows");
_Static_assert(sizeof(s_peek_jun26) / sizeof(s_peek_jun26[0]) <=
                   sizeof(s_peek_pal) / sizeof(s_peek_pal[0]),
               "peek rows");
_Static_assert(sizeof(s_peek_cn) / sizeof(s_peek_cn[0]) <=
                   sizeof(s_peek_pal) / sizeof(s_peek_pal[0]),
               "peek rows");

/* config/regions.txt's order */
static const PeekTable s_peek_tables[ICO_REGION_COUNT] = {
    {s_peek_pal, (int)(sizeof(s_peek_pal) / sizeof(s_peek_pal[0]))},
    {s_peek_jpn, (int)(sizeof(s_peek_jpn) / sizeof(s_peek_jpn[0]))},
    {s_peek_kor, (int)(sizeof(s_peek_kor) / sizeof(s_peek_kor[0]))},
    {s_peek_ntsc, (int)(sizeof(s_peek_ntsc) / sizeof(s_peek_ntsc[0]))},
    {s_peek_aug6, (int)(sizeof(s_peek_aug6) / sizeof(s_peek_aug6[0]))},
    {s_peek_jun26, (int)(sizeof(s_peek_jun26) / sizeof(s_peek_jun26[0]))},
    {s_peek_cn, (int)(sizeof(s_peek_cn) / sizeof(s_peek_cn[0]))},
};
_Static_assert(sizeof(s_peek_tables) / sizeof(s_peek_tables[0]) == 7 && ICO_REGION_COUNT == 7,
               "a peek table for each row of config/regions.txt");

static const PeekTable *peek_table(void)
{
    int n = (int)(sizeof(s_peek_tables) / sizeof(s_peek_tables[0]));

    return &s_peek_tables[ico_region >= 0 && ico_region < n ? ico_region : 0];
}

int ico_gs_peek(unsigned int addr, unsigned int size, unsigned int *out)
{
    const PeekTable *t = peek_table();
    int i;

    if (out == NULL || (size != 1 && size != 2 && size != 4)) {
        return -1;
    }
    for (i = 0; i < t->count; i++) {
        const PeekRow *r = &t->rows[i];

        if (addr >= r->e.addr && addr - r->e.addr + size <= r->e.size) {
            const unsigned char *p =
                (const unsigned char *)&s_snap + r->offset + (addr - r->e.addr);
            unsigned int v = 0;
            unsigned int b;

            /* the host's int fields are little endian like the EE's (every
               target the port builds for is); read byte-wise, any alignment */
            for (b = 0; b < size; b++) {
                v |= (unsigned int)p[b] << (8 * b);
            }
            *out = v;
            return 0;
        }
    }
    return -1;
}

int ico_gs_peek_table(const IcoGsPeekEntry **out)
{
    static IcoGsPeekEntry table[sizeof(s_peek_pal) / sizeof(s_peek_pal[0])];
    const PeekTable *t = peek_table();
    int i;

    for (i = 0; i < t->count; i++) {
        table[i] = t->rows[i].e;
    }
    if (out != NULL) {
        *out = table;
    }
    return t->count;
}
