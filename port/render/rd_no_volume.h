/* rd_no_volume.h: a shadow window with no volume keeps SCENE's pass open
 * (rd_no_volume.c; docs/port/RENDER_POST.md "A shadow window with no
 * volume").
 *
 * The scan (pure, the CPU test's): the store plan's walk hands every command
 * of the replayed lists to rd_nv_scan_step as an RdNvCmd (its kind, its
 * state's targets, every other target it names, and for a level or a
 * composite the source rd_shadow_box.c's scan named); rd_nv_scan_end says
 * whether the replay holds one shadow window (a reset, then its resolve on
 * the same count target and depth, nothing but state between) with no
 * volume, whose count target and levels nothing but the chain's own levels
 * and composites touches after its resolve. */
#ifndef ICO_RD_NO_VOLUME_H
#define ICO_RD_NO_VOLUME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rd.h"
#include "rhi.h"

struct Replay;
struct RdFrame;
struct RdCmd;
struct RdStateBlock;
struct RdTargetRec;
struct RdPerfRecord;

/* a command's kind for the scan */
enum {
    RD_NV_NONE = 0,  /* a state command, a NOP, the overlay's text: nothing */
    RD_NV_RESET,     /* RDC_SHADOW_RESET */
    RD_NV_STRIP,     /* RDC_SHADOW_STRIP: a volume */
    RD_NV_RESOLVE,   /* RDC_SHADOW_RESOLVE */
    RD_NV_LEVEL,     /* a screen sprite rd_shadow_box.c's scan named a level */
    RD_NV_COMPOSITE, /* one it named a composite */
    RD_NV_DRAW,      /* any other command the scan knows: its targets in color, depth, names */
    RD_NV_UNKNOWN    /* a command the scan does not know */
};

#define RD_NV_NAMES 4  /* the other targets a command names (texture, clear, copy, post record) */
#define RD_NV_CHAIN 8  /* the count target and the levels */
#define RD_NV_SKIPS 16 /* the levels and composites the walk does not draw */
#define RD_NV_CLEARS 8 /* the targets cleared before the resolve */

typedef struct RdNvCmd {
    uint8_t kind;
    uint32_t ord;          /* the store plan's ordinal (g_rdStoreOrd) */
    uint32_t color, depth; /* the state's targets (0 none) */
    uint32_t names[RD_NV_NAMES];
    uint32_t src; /* RD_NV_LEVEL, RD_NV_COMPOSITE: the source the box scan named */
    /* RD_NV_LEVEL: writes no depth (Z write off, or no depth bound);
     * RD_NV_COMPOSITE: its DATE, if on, goes through the stencil bit */
    bool formOk;
    bool clears; /* an RDC_CLEAR of names[0] */
} RdNvCmd;

/* what the scan found */
enum {
    RD_NV_RES_NONE = 0, /* no shadow window */
    RD_NV_RES_VOLUME,   /* a window with a volume (a strip): today's passes */
    RD_NV_RES_EMPTY,    /* a window with no volume the walk may take */
    RD_NV_RES_REFUSED   /* a window the scan does not take: RdNvScan.why */
};

/* why a window was not taken (RdPerfRecord.noVolumeWhy) */
enum {
    RD_NV_WHY_OFF = 0, /* ICO_RD_NO_VOLUME=0 (tests) */
    RD_NV_WHY_CLASS,   /* off by default (RD_NO_VOLUME_DEFAULT 0 in rd_no_volume.c), or not a
                          class it would be on for (PowerVR, MoltenVK, under Vulkan) */
    RD_NV_WHY_DATE,    /* a composite's DATE by the R8 snapshot (or the shader's read) */
    RD_NV_WHY_SETTING, /* MSAA, the tool's pass window, the exact blend, GS state as shader data */
    RD_NV_WHY_FORM,    /* the window's commands: a draw inside it, a second window, a resolve
                          elsewhere, a level writing a depth */
    RD_NV_WHY_CHAIN,   /* the count target or a level touched by another command, or cleared
                          before the resolve */
    RD_NV_WHY_UNKNOWN, /* a command the scan does not know after the resolve */
    RD_NV_WHY_COUNT
};

_Static_assert(RD_NV_WHY_COUNT <= RD_NO_VOLUME_WHYS, "rd.h RD_NO_VOLUME_WHYS");

typedef struct RdNvScan {
    uint8_t phase; /* 0 before a window, 1 inside it, 2 after its resolve, 3 decided */
    uint8_t result, why;
    bool strip;
    uint32_t resetOrd, resolveOrd, count, depth;
    uint32_t chain[RD_NV_CHAIN];
    unsigned nChain;
    /* the targets cleared before the resolve (a clear still pending when
     * the zeros are recorded would land after them): a level or count
     * target among them is refused; full: every target counts */
    uint32_t cleared[RD_NV_CLEARS];
    unsigned nCleared;
    bool clearedFull;

    struct {
        uint32_t ord;
        uint8_t kind; /* RD_NV_LEVEL or RD_NV_COMPOSITE */
    } skip[RD_NV_SKIPS];

    unsigned nSkip;
} RdNvScan;

void rd_nv_scan_begin(RdNvScan *s);
void rd_nv_scan_step(RdNvScan *s, const RdNvCmd *c);
/* RD_NV_RES_*: the window, and s->why when refused */
int rd_nv_scan_end(RdNvScan *s);
/* RD_NV_LEVEL or RD_NV_COMPOSITE where the walk leaves ordinal ord undrawn
 * (an RD_NV_RES_EMPTY scan), else RD_NV_NONE */
int rd_nv_skip_kind(const RdNvScan *s, uint32_t ord);
/* the gate before the window (RD_NV_WHY_COUNT: none holds): env the
 * ICO_RD_NO_VOLUME override (-1 unset, 0, 1), classOn the device class's
 * default, the settings in force, dateStencil RdProfile.dateStencil */
int rd_nv_gate(int env, bool classOn, bool msaa, bool passWindow, bool exactBlend, bool uber,
               bool dateStencil);
/* the words of a why (the log, the lines) */
const char *rd_nv_why_words(int why);

/* ------------------------------------------- the scan (rd_store_plan.c) */
void rd__no_volume_scan_begin(const struct RdFrame *f, int keep);
void rd__no_volume_scan_cmd(const struct RdFrame *f, const struct RdCmd *c,
                            const struct RdStateBlock *st, bool state, uint32_t ord);
void rd__no_volume_scan_end(void);
/* rd_shadow_merge.c: the reset at ordinal ord is this replay's window with
 * no volume (its stencil needs no store: nothing reads it); never in the
 * break mode, which keeps the reset and its store */
bool rd__no_volume_owns(uint32_t ord);

/* --------------------------------------------- the walk (rd_replay.c) */
/* doShadowReset: true when the reset is the window's: nothing recorded, the
 * open pass kept (the break mode: the window's resolve is taken, but false:
 * the reset recorded as before) */
bool rd__no_volume_reset(struct Replay *r);
/* doShadowResolve: the window's resolve is to be taken (no pass ended) */
bool rd__no_volume_active(void);
/* the resolve taken: the count's zero owed to red (the count at its GS size
 * above 1x, NULL at scale 1 or when it could not be made) or else to the
 * count target tc; in the break mode the open pass ended first and the
 * shadow window closed, and no zero owed to a target that holds it */
void rd__no_volume_resolve(struct Replay *r, struct RdTargetRec *tc, RhiTexture *red,
                           RhiState *redState);
/* doScreen: true for a level (its target owed a zero) or a composite of the
 * window, or any composite whose level's box is empty (rd_shadow_box.c
 * rd__shadow_box_empty_skip, the empty composites): the walk draws nothing
 * for it, its DATE set-up neither */
bool rd__no_volume_skip(struct Replay *r);
/* endPass, after the pending clears: the owed zeros recorded, each a pass
 * with a CLEAR (0, 0, 0, 0) load and no draw, outside every pass */
void rd__no_volume_flush(void);

/* ------------------------------------------------------------ the lines */
void rd__no_volume_pop(const struct RdPerfRecord *rec);
void rd__no_volume_text(char *out, size_t size, size_t *at);
void rd__no_volume_stats(const struct RdPerfRecord *rec, char *out, size_t size);

#endif
