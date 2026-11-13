/*
 * port/render/rd_bench.h
 *
 * The rc-only bench, the renderer's side (docs/port/ANDROID_DIAGNOSTICS.md,
 * "The bench"): a frame kept as its dump's bytes (rd_dump_retained_frame_mem)
 * is loaded again into the context (rd__load_frame: its textures, meshes and
 * temporary targets made anew), its commands classified once, and replayed
 * through the renderer's own path (rd__replay_frame_async: the passes,
 * pipelines, waits and present of play) as variants:
 *
 *   knock-outs  commands of a class made NOPs in the bench's own copy of
 *               the lists (the frame's commands restored from a copy before
 *               each variant), as rd_replay_tool's --nop does: no pipeline
 *               or target changes, so a variant needs nothing new
 *   switches    the renderer's own switches the tests use (the no-volume
 *               modes, DATE's shared set-up, the volumes' own pass, the
 *               lamps' samples and size, the filter, the present from
 *               SCENE, the crack fill, the volumes' stencil-only entry, the
 *               glow paste's own entry), set before a variant and every one
 *               restored after it
 *
 * Nothing of the game's: a bench frame is not in the frame ring, the game
 * thread's state is not read or written, and the kept frame's resources
 * are destroyed when the frame is freed.  The classes follow the perf
 * classes' own walk (rd_replay.c: an effect named by a post sprite's kind
 * runs until the next target; the soften and aura effects by their
 * targets), with the glow split by the targets it draws on.  In ico_render
 * (every build): nothing calls it unless the rc build's window
 * (window_bench.c) or rd_replay_tool's --bench does, so a release program
 * does not link it.
 */
#ifndef ICO_RD_BENCH_H
#define ICO_RD_BENCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rd_internal.h"

/* the classes a command can be in (bits of uint32_t) */
enum {
    RD_BENCH_C_ACTION = 0,  /* any action (the lists' knock-outs) */
    RD_BENCH_C_VOLUMES,     /* RDC_SHADOW_STRIP: the shadow volumes' draws */
    RD_BENCH_C_WINDOW,      /* RDC_SHADOW_RESET and RDC_SHADOW_RESOLVE: the window and its count */
    RD_BENCH_C_LEVELS,      /* an action on SHADOW0, 1 or 2: the levels */
    RD_BENCH_C_COMPOSITES,  /* an action on SCENE reading SHADOW0, 1 or 2 */
    RD_BENCH_C_FOG,         /* the fog's effect */
    RD_BENCH_C_LAMPS_SETUP, /* the glow on WORK2 or WORK2_PAD, not a VU draw: clears, depth fill */
    RD_BENCH_C_LAMPS,       /* a VU draw on WORK2 or WORK2_PAD: the lamps' meshes */
    RD_BENCH_C_MASK,        /* the glow on SCENE not reading WORK3: the glow mask */
    RD_BENCH_C_CHAIN,       /* the glow on WORK0, WORK1 or WORK3: the blur chain */
    RD_BENCH_C_PASTE,       /* SCENE reading WORK3 or WORK3_HELD: the glow's paste */
    RD_BENCH_C_GLOW_OTHER,  /* the glow's other draws */
    RD_BENCH_C_AURA,
    RD_BENCH_C_REDUCTION,
    RD_BENCH_C_MOTION_BLUR,
    RD_BENCH_C_DOF,
    RD_BENCH_C_SOFTEN,
    RD_BENCH_C_L0_ALPHA, /* list 0's VU draws with the alpha test or blending on */
    RD_BENCH_C_L0_LARGE, /* list 0's mesh draws whose screen box covers over a quarter */
    RD_BENCH_C_COUNT
};

#define RD_BENCH_BIT(c) (1u << (c))
/* list 0's large draws: the box's area over the target's GS area */
#define RD_BENCH_LARGE_SHARE 0.25

/* the target a texture id is a view of (an RdTarget id), 0 none */
typedef uint32_t (*RdBenchSrcFn)(uint32_t texId, void *user);
/* a list 0 draw's screen box's share of its target's area, < 0 unknown */
typedef double (*RdBenchAreaFn)(const RdFrame *f, const RdCmd *c, const RdStateBlock *st,
                                void *user);

/* Every command of f classified into cls[l][i] (the arrays as long as
   the lists), walking the state from f->startState; src and area NULL:
   the renderer's (the texture records, the culler's box under the draw's
   VU matrix) */
void rd_bench_classify(const RdFrame *f, RdBenchSrcFn src, RdBenchAreaFn area, void *user,
                       uint32_t *const cls[RD_LIST_COUNT]);
/* f's lists made orig's again, then every action of a list in listMask
   (bit l) and every command in a class of classMask made a NOP; the count
   made NOPs */
uint32_t rd_bench_mask(RdFrame *f, RdCmd *const orig[RD_LIST_COUNT],
                       uint32_t *const cls[RD_LIST_COUNT], uint32_t classMask, uint32_t listMask);

/* ------------------------------------------------------------ switches */

/* the structural switches (rd_bench.c: switchSet sets one, rd_bench_switches_restore
   puts every one back, rd_bench_variants lists the frame's).  Another
   package's switch is a value here, a case in each of the two and a row in
   the third: "the glow paste by its specialised entry" when LAMPS4's lands */
enum {
    RD_BENCH_S_NONE = 0,
    RD_BENCH_S_NV_BREAK,   /* the no-volume break mode, the other way */
    RD_BENCH_S_NV_OPEN,    /* the open no-volume mode (SCENE's pass not ended), break off */
    RD_BENCH_S_DATE_ONE,   /* DATE's shared set-up, the other way */
    RD_BENCH_S_OWN_PASS,   /* the shadow volumes in a pass of their own */
    RD_BENCH_S_LAMPS_1X,   /* the lamps' 4-sample pass at 1 sample */
    RD_BENCH_S_LAMPS_FULL, /* the lamps' target at full size, not the GS size */
    RD_BENCH_S_FILTER,     /* the texture filter: trilinear and anisotropic swapped */
    RD_BENCH_S_SCENE,      /* the present from SCENE (no reduction) */
    RD_BENCH_S_NO_CRACK,   /* the crack fill off (a knock-out) */
    RD_BENCH_S_VOLUME_FS,  /* the volumes' stencil-only entry (rd_volume_fs.c), the other way */
    RD_BENCH_S_PASTE_SPEC, /* the glow's paste by its own entry (rd_paste_spec.c), the other way */
    RD_BENCH_S_COUNT
};

/* ------------------------------------------------------------ the frame */

typedef struct RdBenchFrame {
    RdFrame f;
    RdCmd *orig[RD_LIST_COUNT];
    uint32_t *cls[RD_LIST_COUNT];
    uint32_t classCount[RD_BENCH_C_COUNT];
    uint32_t listActions[RD_LIST_COUNT];
    uint32_t commands;
    size_t bytes; /* the dump's */
    bool owned;   /* loaded here: its textures and meshes destroyed at the free */
} RdBenchFrame;

/* kept bytes (a dump of rd_dump_retained_frame_mem) written to path, loaded
   (rd__load_frame), the file removed, the commands copied and classified;
   false with nothing made */
bool rd_bench_frame_load(RdBenchFrame *b, const void *bytes, size_t len, const char *path);
/* rd_replay_tool's loaded frame used as it is (shallow: its lists are the
   tool's, restored at the free; nothing destroyed) */
bool rd_bench_frame_adopt(RdBenchFrame *b, const RdFrame *f);
/* the GPU idle, the frame's lists restored, what the load made destroyed */
void rd_bench_frame_free(RdBenchFrame *b);

/* a variant */
typedef struct RdBenchVariant {
    char name[96];
    uint32_t classes; /* RD_BENCH_BIT mask: knocked out */
    uint32_t lists;   /* bit l: list l's actions knocked out */
    int sw;           /* RD_BENCH_S_*, NONE */
    uint8_t knock;    /* a part knocked out (ranked) */
    uint8_t sum;      /* 1 a list, 2 a part of the parts' sum (bench_plan.h) */
} RdBenchVariant;

/* The variants this frame can run, the baseline first (a class with no
   command, a list with no action and a switch that cannot change here are
   left out); the count */
int rd_bench_variants(const RdBenchFrame *b, RdBenchVariant *out, int max);
/* the variant's commands and switches in force (every switch of the
   variant before restored first); the count of commands made NOPs */
uint32_t rd_bench_apply(RdBenchFrame *b, const RdBenchVariant *v);
/* every switch back to what it was before the bench */
void rd_bench_switches_restore(void);
/* one replay of the frame with its present, posted as play posts one
   (rd__replay_frame_async); false when it could not be posted or the
   device is gone */
bool rd_bench_post(RdBenchFrame *b);
/* the render and submit threads idle (their last picture recorded and
   handed over); before the bench changes the frame's commands */
void rd_bench_settle(void);

/* ---------------------------------------- the switches' own (their files) */
/* rd_no_volume.c: the environment words ICO_RD_NO_VOLUME (open) and
   ICO_RD_NO_VOLUME_BREAK (brk) as if set (0 or 1), -1 each the
   environment's again; the mode in force: 1 break, 2 open, 0 neither */
void rd__no_volume_bench_force(int open, int brk);
int rd__no_volume_bench_mode(void);
/* rd_crack_fill.c: the fill's switch as it stands, and back to it */
uint32_t rd__crack_fill_bench_save(void);
void rd__crack_fill_bench_restore(uint32_t saved);

#endif /* ICO_RD_BENCH_H */
