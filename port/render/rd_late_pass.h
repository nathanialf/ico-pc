/* rd_late_pass.h: fewer late full-size passes on SCENE, the same bytes
 * (port/render/rd_late_pass.c; docs/port/RENDER_POST.md, "The late passes
 * on SCENE").
 *
 * The parts, each its own bit of the switch (rd__late_pass_bits):
 *   RD_LATE_FILL  the crack fill's draw held (rd_crack_fill.c): its colour
 *                 snapshot taken where it is taken without the hold, its
 *                 draw made the first draw of the aura's paste pass in a
 *                 frame whose glow plan draws nothing (DARK), or of the
 *                 glow's paste pass in a frame that still draws the paste
 *                 (LAMPS, WHOLE), so the fill has no pass of its own there
 *   RD_LATE_DEFER the glow's paste (LAMPS, WHOLE) deferred to the aura's
 *                 local-read paste pass (rd_fx_plan.c fxDraw takes it, draws
 *                 it there after a held fill, before the aura's paste), so
 *                 it has no pass of its own
 *   RD_LATE_JOIN  a list 11 screen prim that neither tests nor writes the
 *                 depth and takes no DATE joins the open pass on its target
 *                 that has no depth (the pause menu's draws in the aura's
 *                 paste pass) instead of beginning one with SCENE's depth
 * Every part on by default on a tile-based GPU under Vulkan (MoltenVK too;
 * RD_LATE_DEFAULT); on the other classes and backends JOIN alone, which
 * applies only where the fx plan reads destinations by the local read
 * (rd__late_pass_join).  FILL and DEFER are byte for byte; JOIN is an
 * accepted "looks the same" divergence (docs/port/DIVERGENCES.md VD-31: the
 * no-depth pipeline's sampling of a blended bilinear text sprite lands a
 * level or a few apart on a few hundred texels of list 11's sprites), so it
 * ships wherever it applies.  ICO_RD_LATE_PASS=0|1|all|fill|defer|join (the
 * tests' A/B only, docs/port/TESTING.md; no config key): off, every part
 * (1 and all), one part (forced on any device and backend).  rd__late_pass_force is the
 * tests' switch (-1 the environment's or the profile's). */
#ifndef PORT_RENDER_RD_LATE_PASS_H
#define PORT_RENDER_RD_LATE_PASS_H

#include <stdbool.h>
#include <stdint.h>

#include "rd_internal.h"

#define RD_LATE_FILL 1u
#define RD_LATE_DEFER 2u
#define RD_LATE_JOIN 4u
#define RD_LATE_ALL (RD_LATE_FILL | RD_LATE_DEFER | RD_LATE_JOIN)
/* the tile class's parts and ICO_RD_LATE_PASS=1's: every part */
#define RD_LATE_DEFAULT RD_LATE_ALL
/* rd__late_pass_force only (the tests' frames, which hold no glow chain the
 * light plan reads): the fill held whatever the glow's plan */
#define RD_LATE_ANY_PLAN 0x100u

unsigned rd__late_pass_bits(void);
void rd__late_pass_force(int bits);
/* the switch's words for the bits ("on", "off", "fill", ...) */
const char *rd__late_pass_bits_text(unsigned bits);

/* What the held crack fill does at command c (state st before c's own
 * state), one predicate for the store plan's scan and the walk:
 *   RD_LATE_HOLD       it waits past c: a state command (not the upscale
 *                      point), a NOP, overlay text, a clear or a copy of
 *                      another target than SCENE, a staticBlur sprite into
 *                      another target that writes no depth of SCENE, takes
 *                      no DATE and samples SCENE only in the alpha-only
 *                      form (rd__fx_alpha_only: the glow's mask, which reads
 *                      the alpha the fill does not write), a draw (screen
 *                      prims, a VU draw) into another target that samples
 *                      no SCENE, writes no depth of SCENE (a Z test reads
 *                      it: the lamps' meshes into WORK2), takes no DATE and
 *                      no PRIM.AA1, a sprite on SCENE the glow's light plan
 *                      does not draw (skipped 1), or in the scan, where the
 *                      plan is not known yet (skipped -1), any sprite on
 *                      SCENE that samples no SCENE
 *   RD_LATE_LAND_AURA  the aura's paste into SCENE (pasteFeedBackAreaToFB):
 *                      the fill is the first draw of its pass
 *   RD_LATE_LAND_GLOW  the glow's paste into SCENE (pasteFullScreenFlare:
 *                      WORK3 added through Cs * FIX + Cd or Cs * As + Cd,
 *                      no Z test, COLCLAMP 1, no PABE) that the plan draws:
 *                      the fill is the first draw of its pass (the fx
 *                      plan's premultiplied add, rd_fx_plan.c fxDraw)
 *   RD_LATE_FLUSH      anything else: the fill drawn before c in a pass of
 *                      its own (*why the reason, RD_LATE_WHY_*)
 * c's frame f gives its payload; skipped: 1 the light plan does not draw
 * c, 0 it does, -1 not known (the scan). */
enum { RD_LATE_HOLD, RD_LATE_LAND_AURA, RD_LATE_LAND_GLOW, RD_LATE_FLUSH };

/* why a held fill was drawn before the paste (RdPerfRecord.latePassWhy) */
enum {
    RD_LATE_WHY_NONE,
    RD_LATE_WHY_DRAW,    /* a draw (screen prims, a VU draw) or a shadow command */
    RD_LATE_WHY_SCENE,   /* a sprite, clear or copy that writes or reads SCENE */
    RD_LATE_WHY_DEPTH,   /* a sprite that writes SCENE's depth or takes DATE */
    RD_LATE_WHY_POST,    /* the reduction, the fog or another post command */
    RD_LATE_WHY_UPSCALE, /* the upscale point */
    RD_LATE_WHY_PASTE,   /* the paste's pass could not take the fill (its own pass first) */
    RD_LATE_WHY_SNAP,    /* SCENE's snapshot taken or swapped again */
    RD_LATE_WHY_BORDER,  /* SCENE's border repeat with more blocks than the hold keeps */
    RD_LATE_WHY_MSAA,    /* the MSAA epoch */
    RD_LATE_WHY_END,     /* the walk's end */
    RD_LATE_WHY_WORK3,   /* the deferred glow paste's source or a draw that binds SCENE's depth */
    RD_LATE_WHY_COUNT
};

const char *rd__late_pass_why_text(int why);

int rd__late_pass_class(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, int skipped,
                        int *why);

/* The store plan's scan (rd_store_plan.c rd__store_plan_scan): the command
 * before which (or in whose pass) the fill reads SCENE's depth, as the
 * walk places it.  rd__late_pass_scan_begin once a scan;
 * rd__late_pass_scan for each command (ordinal ord, list l, index i) from
 * list 7 on while the fill may be armed: true when the fill's read lies at
 * c (the plan's touch, now).  Where rd__crack_fill_place says TAKE and the
 * fill may be held, it is held: the scan notes that place and the one the
 * held draw lands at (rd__late_pass_class, the scan not knowing the glow's
 * plan: skipped -1), and rd__late_pass_scan_end, after the glow's plan is
 * decided (rd__glow_light_scan_end), gives the place the walk takes: the
 * landing with the plan's skipped sprites waited past under the DARK plan
 * (land), the first command a plan that skips nothing on SCENE does not
 * wait past under LAMPS and WHOLE (first: the glow's paste; the walk's end
 * when none came: end, endL), the snapshot's place under any other (true;
 * false when nothing is held) */
typedef struct RdLateScan {
    bool alpha;                 /* rd__crack_fill_place's */
    bool hold;                  /* the fill may be held this replay */
    bool held;                  /* past the place where the snapshot is taken */
    bool done;                  /* the held draw's landing found (skipped -1) */
    bool first;                 /* the first command not waited past with skipped 0 found */
    bool doneLands, firstLands; /* that command a paste the fill lands in */
    uint32_t takeOrd, takeI, landOrd, landI, firstOrd, firstI;
    int takeL, landL, firstL;
} RdLateScan;

void rd__late_pass_scan_begin(RdLateScan *s);
bool rd__late_pass_scan(RdLateScan *s, const RdFrame *f, const RdCmd *c, const RdStateBlock *st,
                        uint32_t ord, int l, uint32_t i);
bool rd__late_pass_scan_end(const RdLateScan *s, uint32_t end, int endL, uint32_t *ord, int *l,
                            uint32_t *i);
/* this replay's scan found no paste that takes the held fill (the walk does
 * not hold it); the walk's count of such a fill not held */
bool rd__late_pass_no_land(void);
void rd__late_pass_not_held(void);

/* The walk's: whether the crack fill's draw is held this replay (the glow
 * plan's DARK, LAMPS or WHOLE, no MSAA, not under the upscaler's split),
 * and the counters (RdPerfRecord.latePass*, the 10 s line): a held fill
 * drawn in the aura's paste pass or the glow's (glow), one drawn early
 * (why). */
bool rd__late_pass_hold_wanted(void);
void rd__late_pass_held_drawn(const RdTargetRec *scene, bool glow);
void rd__late_pass_flushed(int why);

/* SCENE's border repeat (rd_blur_border.c sceneRepeat): the blocks its last
 * repeat copied, so a held fill drawn after it writes each block's texels
 * from its source column's (rd_crack_fill.c): texel x of block k took SCENE's
 * texel x + b[k].off of its row (the copies' source columns, 2 and W - 3);
 * [x0, x1) the columns it left; serial rd__scene_border_repeat_count() after
 * it, replay and tex the replay and SCENE's colour texture; over: more
 * blocks than b holds (a held fill was drawn before that repeat) */
#define RD_BORDER_BLOCKS 16

typedef struct RdBorderBlock {
    int32_t x, w, off;
} RdBorderBlock;

typedef struct RdBorderBlocks {
    uint64_t serial;
    uint32_t replay;
    uint32_t tex;
    int32_t x0, x1;
    uint32_t n;
    bool over;
    RdBorderBlock b[RD_BORDER_BLOCKS];
} RdBorderBlocks;

const RdBorderBlocks *rd__scene_border_blocks(void);

/* rd_crack_fill.c: a held fill drawn now in a pass of its own (why
 * RD_LATE_WHY_*), as the walk does before a command that cannot wait;
 * nothing when none is held.  rd_blur_border.c's repeat with too many
 * blocks, rd_replay.c's snapshot and swap of SCENE (rd__crack_fill_snap) */
struct Replay;
struct DrawSetup;
void rd__crack_fill_flush_held(struct Replay *r, int why);
void rd__crack_fill_snap(struct Replay *r, const RdTargetRec *tc);

/* RD_LATE_JOIN.  rd__late_pass_cmd (rd_crack_fill.c rd__crack_fill_at,
 * before every command the walk replays): the command walked now.
 * rd__late_pass_join (rd_replay.c prepareDrawEx, after the DATE cut's
 * depth): a screen prim of list 11 under r's state whose draw needs no
 * depth (its Z test off or ALWAYS, its Z write off, no DATE, no PRIM.AA1,
 * no depth view sampled) gets no depth target in ds (tdId 0, depthFmt
 * UNKNOWN) when the open pass on its target has none, so bindDrawEx draws
 * it in that pass; counted (RdPerfRecord.latePassJoined, latePassJoinPasses
 * the passes it joined, each one a pass the draw would have begun) */
void rd__late_pass_cmd(const RdCmd *c);
void rd__late_pass_join(struct Replay *r, struct DrawSetup *ds);

/* RD_LATE_DEFER.  rd__late_pass_defer_wanted (rd_fx_plan.c, the glow's
 * paste about to be drawn by the premultiplied add, command c of frame f
 * under r's state): the switch, LAMPS or WHOLE, the glow's paste form
 * (RD_LATE_LAND_GLOW), a local read on this device, no MSAA, no exact blend,
 * no upscaler split, no pass window.  rd__late_pass_defer_at (from
 * rd__crack_fill_at, after the fill's own step, before every command):
 * while a paste is deferred, it waits past what the fill's class waits past
 * but a draw, a sprite with a Z test of SCENE's depth, a read of SCENE in any
 * form and anything on WORK3 (its source), lands at the aura's paste
 * (rd__late_pass_defer_lands: rd_fx_plan.c draws it in that pass after a
 * held fill) when its pass is the local read's, whole, with no pass on SCENE
 * open before it, else is drawn now (a held fill first).
 * rd__late_pass_defer_end: the walk's end (rd_replay.c).  The counters:
 * rd__late_pass_deferred_taken, rd__late_pass_deferred_drawn (landed in the
 * aura's pass, or drawn early; drawn by fxDraw) */
bool rd__late_pass_defer_wanted(struct Replay *r, const RdFrame *f, const RdCmd *c);
void rd__late_pass_defer_at(struct Replay *r, const RdFrame *f, const RdCmd *c);
bool rd__late_pass_defer_lands(void);
void rd__late_pass_defer_end(struct Replay *r);
void rd__late_pass_deferred_taken(void);
void rd__late_pass_deferred_drawn(bool landed, bool drawn);
/* rd_fx_plan.c: the deferred paste (rd__fx_defer_on: one this replay;
 * rd__fx_defer_draw: drawn now, landed in the aura's pass or early);
 * rd_crack_fill.c rd__crack_fill_unarm_glow: a held fill armed for the
 * glow's paste waits on to the aura's paste with the deferred paste */
bool rd__fx_defer_on(void);
const RdTargetRec *rd__fx_defer_src(void); /* its source target (WORK3) */
void rd__fx_defer_draw(struct Replay *r, bool landed);
void rd__crack_fill_unarm_glow(void);

#endif
