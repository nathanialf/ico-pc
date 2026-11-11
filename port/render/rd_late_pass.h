/* rd_late_pass.h: fewer late full-size passes on SCENE, the same bytes
 * (port/render/rd_late_pass.c; docs/port/RENDER_POST.md, "The late passes
 * on SCENE").
 *
 * The parts, each its own bit of the switch (rd__late_pass_bits):
 *   RD_LATE_FILL  the crack fill's draw held (rd_crack_fill.c): its colour
 *                 snapshot taken where it is taken without the hold, its
 *                 draw made the first draw of the aura's paste pass in a
 *                 frame whose glow plan draws nothing (DARK), so the fill
 *                 has no pass of its own there
 * On by default on a tile-based GPU under Vulkan (MoltenVK too); off on the
 * other classes and on OpenGL.  ICO_RD_LATE_PASS=0|1|fill (the tests'
 * A/B only, docs/port/TESTING.md; no config key): off, every part, one
 * part (forced on any device and backend).  rd__late_pass_force is the
 * tests' switch (-1 the environment's or the profile's). */
#ifndef PORT_RENDER_RD_LATE_PASS_H
#define PORT_RENDER_RD_LATE_PASS_H

#include <stdbool.h>
#include <stdint.h>

#include "rd_internal.h"

#define RD_LATE_FILL 1u
#define RD_LATE_ALL RD_LATE_FILL
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
 *                      the alpha the fill does not write), a sprite on SCENE
 *                      the glow's light plan does not draw (skipped 1), or
 *                      in the scan, where the plan is not known yet
 *                      (skipped -1), the glow's paste
 *   RD_LATE_LAND_AURA  the aura's paste into SCENE (pasteFeedBackAreaToFB):
 *                      the fill is the first draw of its pass
 *   RD_LATE_FLUSH      anything else: the fill drawn before c in a pass of
 *                      its own (*why the reason, RD_LATE_WHY_*)
 * c's frame f gives its payload; skipped: 1 the light plan does not draw
 * c, 0 it does, -1 not known (the scan). */
enum { RD_LATE_HOLD, RD_LATE_LAND_AURA, RD_LATE_FLUSH };

/* why a held fill was drawn before the paste (RdPerfRecord.latePassWhy) */
enum {
    RD_LATE_WHY_NONE,
    RD_LATE_WHY_DRAW,    /* a draw (screen prims, a VU draw) or a shadow command */
    RD_LATE_WHY_SCENE,   /* a sprite, clear or copy that writes or reads SCENE */
    RD_LATE_WHY_DEPTH,   /* a sprite that writes SCENE's depth or takes DATE */
    RD_LATE_WHY_POST,    /* the reduction, the fog or another post command */
    RD_LATE_WHY_UPSCALE, /* the upscale point */
    RD_LATE_WHY_PASTE,   /* the aura's paste could not take the fill (its own pass first) */
    RD_LATE_WHY_SNAP,    /* SCENE's snapshot taken or swapped again */
    RD_LATE_WHY_BORDER,  /* SCENE's border repeat with more blocks than the hold keeps */
    RD_LATE_WHY_MSAA,    /* the MSAA epoch */
    RD_LATE_WHY_END,     /* the walk's end */
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
 * landing under the DARK plan (the walk's end when none came: end, endL),
 * the snapshot's place under any other (true; false when nothing is held) */
typedef struct RdLateScan {
    bool alpha; /* rd__crack_fill_place's */
    bool hold;  /* the fill may be held this replay */
    bool held;  /* past the place where the snapshot is taken */
    bool done;  /* the held draw's landing found */
    uint32_t takeOrd, takeI, landOrd, landI;
    int takeL, landL;
} RdLateScan;

void rd__late_pass_scan_begin(RdLateScan *s);
bool rd__late_pass_scan(RdLateScan *s, const RdFrame *f, const RdCmd *c, const RdStateBlock *st,
                        uint32_t ord, int l, uint32_t i);
bool rd__late_pass_scan_end(const RdLateScan *s, uint32_t end, int endL, uint32_t *ord, int *l,
                            uint32_t *i);

/* The walk's: whether the crack fill's draw is held this replay (the glow
 * plan's DARK, no MSAA, not under the upscaler's split), and the
 * counters (RdPerfRecord.latePass*, the 10 s line): a held fill drawn in the
 * aura's paste pass, one drawn early (why). */
bool rd__late_pass_hold_wanted(void);
void rd__late_pass_held_drawn(const RdTargetRec *scene);
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
void rd__crack_fill_flush_held(struct Replay *r, int why);
void rd__crack_fill_snap(struct Replay *r, const RdTargetRec *tc);

#endif
