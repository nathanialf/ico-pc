/*
 * port/render/rd_hold.h
 *
 * The paused scene's hold (issue 123; docs/port/RENDER.md "The paused
 * scene's hold"): while the game is paused and its scene (lists 0-10) is
 * the one the last full replay drew, the replay draws lists 11-12 alone
 * over a copy of SCENE taken at the start of list 11 in that replay.
 *
 * rd_hold_key.c (CPU, pure):
 *   the change counters: rd__hold_mesh_changed (a mesh stream made,
 *     destroyed or rewritten with other bytes, rd_mesh.c) and
 *     rd__hold_tex_changed (a texture made, destroyed or given new texels:
 *     every place a texture is marked for upload, rd_core.c, rd_tex.c,
 *     rd_replay.c), read back by rd__hold_mesh_gen / rd__hold_tex_gen;
 *   rd__hold_scan: the frame's rules: lists 0-10 do not read the picture
 *     before (DISPLAY, DISPLAY_HELD, the display layer's slots: the motion
 *     blur, a keep sprite); the targets lists 0-10 carry from one replay to
 *     the next (FEED128, FEED_HELD, WORK3_HELD), whose bytes the settling
 *     compares; lists 11-12 draw only into SCENE and DISPLAY, read no
 *     target but SCENE's colour, and test no depth, DATE or PRIM.AA1;
 *   rd__hold_lists_key: lists 0-10 hashed with every payload byte they
 *     name (the payload offsets themselves left out, so a menu that records
 *     its payload first does not move the key), the head's scalars, start
 *     state, camera, VU block and tick cameras; not the frame's number;
 *   rd__hold_state_key: what the frame does not carry: the settings, the
 *     filter, the fog path, the blend mode, SCENE's and DISPLAY's textures,
 *     sizes and formats;
 *   rd__hold_why: the decision from those facts (RD_HOLD_*).
 * rd_hold.c (the device): the chain of full replays with one key, the
 *   copy at list 11 (the take), the feedback's readbacks, the held replay's
 *   frame (lists 0-10's actions as RDC_NOP: every ordinal, state command
 *   and scan as in the full replay) and its copy back, the first held
 *   picture's self-test, the lines and the counters.
 */
#ifndef PORT_RENDER_RD_HOLD_H
#define PORT_RENDER_RD_HOLD_H

#include <stdbool.h>
#include <stdint.h>

#include "rd_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Replay;

/* the list whose start the copy is taken at and restored at */
#define RD_HOLD_LIST 11

/* ------------------------------------------------------------- counters */
void rd__hold_mesh_changed(void);
void rd__hold_tex_changed(void);
uint64_t rd__hold_mesh_gen(void);
uint64_t rd__hold_tex_gen(void);

/* ------------------------------------------------------------ the rules */
/* the targets lists 0-10 may carry across replays (FEED128, FEED_HELD,
 * WORK3_HELD): at most one entry each */
#define RD_HOLD_CROSS_MAX 3

enum {
    RD_HOLD_RULE_OK = 0,
    RD_HOLD_RULE_KEEP,    /* a keep frame (lists 11-12 alone already) */
    RD_HOLD_RULE_PICTURE, /* lists 0-10 read or write the picture before (DISPLAY,
                             DISPLAY_HELD, the display layer's slots) */
    RD_HOLD_RULE_PAYLOAD, /* a command names payload bytes outside the payload */
    RD_HOLD_RULE_MENU,    /* lists 11-12 draw elsewhere than SCENE or DISPLAY, read another
                             target, test the depth, DATE or PRIM.AA1, or hold a command the
                             hold does not take (a copy, a fog, a shadow, an upscale point) */
    RD_HOLD_RULE_COUNT
};

typedef struct RdHoldScan {
    uint32_t cross[RD_HOLD_CROSS_MAX]; /* target ids, in the order first touched */
    uint32_t crossN;
    uint32_t menuDraws; /* list 11's actions (not NOP or overlay text) */
    /* RD_HOLD_RULE_MENU: the first list 11-12 command refused (list, index) */
    int8_t badList;
    uint32_t badCmd;
} RdHoldScan;

int rd__hold_scan(const RdFrame *f, RdHoldScan *s);
const char *rd__hold_rule_text(int rule);

/* ------------------------------------------------------------- the keys */
uint64_t rd__hold_lists_key(const RdFrame *f);
uint64_t rd__hold_state_key(void);

/* ---------------------------------------------------------- the decision */
enum {
    RD_HOLD_HELD = 0,   /* drawn from the held scene */
    RD_HOLD_NOT_PAUSED, /* the game not paused (not counted as replayed) */
    RD_HOLD_OFF,        /* ICO_RD_SCENE_HOLD=0, or its self-test refused it for the run */
    RD_HOLD_DEVICE,     /* the device or the settings (see rd_hold.c deviceWhy) */
    RD_HOLD_PICTURE,    /* the scene reads the picture before (the motion blur) */
    RD_HOLD_MENU,       /* lists 11-12 do what the hold cannot keep */
    RD_HOLD_FIRST,      /* no copy of this scene yet */
    RD_HOLD_LISTS,      /* lists 0-10 differ from the copy's */
    RD_HOLD_SETTINGS,   /* the settings, the targets or the filter differ */
    RD_HOLD_UPLOADS,    /* a mesh or a texture changed, or a texture waits for its upload */
    RD_HOLD_FEEDBACK,   /* the carried targets not settled (equal over two replays) yet */
    RD_HOLD_OWED,       /* the copy was not taken: scene work owed at list 11 */
    RD_HOLD_COUNT
};

typedef struct RdHoldIn {
    bool paused, on, device;
    int rule;  /* RD_HOLD_RULE_* */
    bool take; /* a copy of the chain's scene is held */
    uint64_t lists, refLists;
    uint64_t state, refState;
    uint64_t meshGen, refMeshGen, texGen, refTexGen;
    bool texWaiting; /* a texture marked for upload not uploaded yet */
    bool settled;
} RdHoldIn;

int rd__hold_why(const RdHoldIn *in);
const char *rd__hold_why_text(int why);

/* ------------------------------------------------------------ rd_hold.c */
/* replayFrame (rd_replay.c), after the slot's wait: the frame to walk (f,
 * or the held frame) and this replay's kind */
const RdFrame *rd__hold_frame(const RdFrame *f, int keep, bool present);
/* the walk at the start of list l (rd_replay.c's list loop): the copy
 * (a full replay of a paused scene) or its copy back (a held replay) */
void rd__hold_list(struct Replay *r, int l);
/* rd__replay_frame_here: the first held picture of the run is made twice
 * (rd__hold_selftest_due true once: the held one without a present, then
 * this one in full, rd__hold_selftest_end compares their pictures) */
bool rd__hold_selftest_due(const RdFrame *f, int keep, bool present);
void rd__hold_selftest_mid(void);
void rd__hold_selftest_end(bool ok);
/* rd_perf.c rd__perf_collect_gpu: the feedback's readback of slot */
void rd__hold_collect(uint32_t slot);
/* what the walk leaves owed at list 11, read at the copy: rd_replay.c
 * rd__replay_owed (1 a clear not recorded yet, 2 a shadow reset), rd_crack_fill.c
 * rd__crack_fill_owed (a crack fill held to a paste or to list 11's pass) */
uint32_t rd__replay_owed(void);
bool rd__crack_fill_owed(void);
/* rd_blur_border.c: SCENE's border repeat armed for this replay (a held
 * replay whose take found it still due at list 11: drawn at the same later
 * command as in the full replay) */
void rd__scene_border_rearm(void);

/* the window's stall lines: what replay `replay` was (RD_HOLD_KIND_*) */
enum { RD_HOLD_KIND_NONE = 0, RD_HOLD_KIND_HELD, RD_HOLD_KIND_REPLAYED };

/* rd_shutdown, before the device goes: the held copy and the readbacks
 * destroyed, the chain, the self-test and the device's verdict forgotten
 * (a renderer made again, as rd_replay_tool's --present pass, starts
 * afresh) */
void rd__hold_shutdown(void);
/* the 10 s line's counts (tests) */
uint64_t rd__hold_count(int why);

#ifdef __cplusplus
}
#endif

#endif
