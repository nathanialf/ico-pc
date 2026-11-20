/* rd_ledger.h: the scene ledger: what each stage contained and what was
 * left out of its pictures, by model name and by reason (issue 221:
 * docs/port/SCENE_LEDGER.md).
 *
 * The log was rich on timing and devices and silent on content: issue
 * 221's report could not say whether the dream's wall model was ever drawn,
 * dropped by a cull, or never built.  The ledger keeps, for the stage in
 * play, one row per model name (the name the game gave the packet's mesh,
 * ico2/seki/src/Packet.c pac_hostBuild, or the box cull's model name,
 * RegistPacket.c regHostCullDrop): the meshes built under it, the ticks it
 * was drawn in with its draws and triangles, and for every reason a draw
 * or a part was left out (RdLedgerReason) the draws or parts left out and
 * the ticks (game side) or pictures (the replay's) it happened in, with
 * the share that happened between ticks.  Beside the models, the effects the
 * game recorded (fog, fades, the blurs, the glow, shadow volumes) by ticks
 * on and their last key value.
 *
 * At the stage's end (the game's stage_no or stage name changes:
 * rd_ledger_scene) the stage is written to the log as "rd: scene ledger:"
 * lines and kept, as text, among the last RD_LEDGER_KEEP stages; the bug
 * report's zip takes them with the stage in play so far
 * (rd_ledger_report_text, scene-ledger.txt) and a capture writes the stage
 * in play so far to the log before its zip takes the log
 * (rd__ledger_log_now).
 *
 * Bounded: RD_LEDGER_SLOTS names a stage, the rest counted as "names past
 * the table"; the text of a stage is at most RD_LEDGER_TEXT bytes, its
 * detailed rows the RD_LEDGER_ROWS most telling (anything left out or never
 * drawn first), the rest one count.  Always on, every platform, no key.
 *
 * The tally, the text and the kept stages are pure (this file and
 * rd_ledger.c, port/render/test/rd_ledger_test.c); rd_ledger_live.c holds
 * the renderer's hooks (rd_internal.h rd__ledger_*). */
#ifndef ICO_RD_LEDGER_H
#define ICO_RD_LEDGER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Why a draw, a part or an object was left out.  A new place that leaves
 * a draw out gets its reason here, its words in rd_ledger.c's table (a
 * compile-time check holds the two to the same count) and a row in
 * tools/check_drop_ledger.py's site table (the drop_ledger_gate test). */
typedef enum RdLedgerReason {
    RD_LEDGER_BOX_OBJECT =
        0,              /* the game's box cull, the whole object (RegistPacket.c rule 2, part -1) */
    RD_LEDGER_BOX_NODE, /* the game's box cull, one node (rule 2 with a node) */
    RD_LEDGER_BOX_PART, /* the game's box cull, one part (rule 3) */
    RD_LEDGER_NO_MESH,  /* its mesh could not be built (rd_create_vu_mesh refused the stream) */
    RD_LEDGER_NO_RECORD,   /* a draw of a mesh with no live record (rd_draw_vu_mesh) */
    RD_LEDGER_BATCHES,     /* a draw whose first batch lies past its mesh */
    RD_LEDGER_FILTER,      /* photo mode's draw filter left it out */
    RD_LEDGER_RECORD_FULL, /* no frame open to record it (rd_mesh.c pushVu) */
    RD_LEDGER_FRUSTUM,     /* the port's visibility test: off the picture or behind the eye */
    RD_LEDGER_OCCLUDED,    /* the port's visibility test: behind what was drawn before it */
    RD_LEDGER_REASONS
} RdLedgerReason;

/* the reason's words in the lines ("the game's box cull (whole object)") */
const char *rd_ledger_reason_words(int reason);
/* 1 for a reason the replay counts by picture (the port's visibility
   test), 0 for one counted by tick (recorded on the game's thread) */
int rd_ledger_reason_by_picture(int reason);

/* the effects the game recorded, by ticks on */
typedef enum RdLedgerFx {
    RD_LEDGER_FX_FOG = 0,
    RD_LEDGER_FX_FADE,
    RD_LEDGER_FX_LETTERBOX,
    RD_LEDGER_FX_BRIGHTNESS,
    RD_LEDGER_FX_FILM_NOISE,
    RD_LEDGER_FX_MOTION_BLUR,
    RD_LEDGER_FX_DOF,
    RD_LEDGER_FX_FLARE,
    RD_LEDGER_FX_GLOW,
    RD_LEDGER_FX_AURA,
    RD_LEDGER_FX_EYE_BLUR,
    RD_LEDGER_FX_AA,
    RD_LEDGER_FX_SHADOWS,
    RD_LEDGER_FX_COUNT
} RdLedgerFx;

const char *rd_ledger_fx_words(int fx);

#define RD_LEDGER_NAME 24 /* RdMeshRec.name's size */
#define RD_LEDGER_SLOTS 256
#define RD_LEDGER_HASH 512 /* a power of two, at least twice the slots */
#define RD_LEDGER_ROWS 48  /* detailed rows a stage's text holds */
#define RD_LEDGER_TEXT 8192
#define RD_LEDGER_KEEP 4 /* the closed stages the report keeps */

typedef struct RdLedgerEntry {
    char name[RD_LEDGER_NAME];
    uint32_t built;   /* meshes built under the name this stage */
    uint32_t carried; /* built in the stage before and never drawn there */
    uint32_t ticks;   /* ticks with a draw of it recorded */
    uint32_t draws;
    uint64_t tris;
    uint32_t lastTick;
    uint32_t drops[RD_LEDGER_REASONS]; /* draws, parts or objects left out */
    uint32_t pics[RD_LEDGER_REASONS];  /* ticks or pictures with one */
    /* of pics: by the box cull, those tested with the cameras of the
       pictures between the ticks; by the port's test, pictures drawn
       between the ticks */
    uint32_t alt[RD_LEDGER_REASONS];
    uint32_t lastPic[RD_LEDGER_REASONS];
} RdLedgerEntry;

typedef struct RdLedgerTable {
    RdLedgerEntry e[RD_LEDGER_SLOTS];
    uint16_t hash[RD_LEDGER_HASH]; /* 1 + the entry's index, 0 empty */
    uint32_t n;
    uint32_t pastNames; /* events of names that found the table full */
} RdLedgerTable;

typedef struct RdLedgerFxRow {
    uint32_t ticks, lastTick;
    float value; /* the last tick's key value (rd_ledger_fx_value_words) */
} RdLedgerFxRow;

typedef struct RdLedgerStage {
    int stage;     /* the game's stage_no, -1 none */
    char name[48]; /* the game's name of it, "" none */
    uint32_t firstTick, lastTick;
    uint32_t pictures, between; /* pictures replayed, of them between ticks */
    uint32_t lateEvents;        /* the replay's events of a frame of the stage before */
    RdLedgerFxRow fx[RD_LEDGER_FX_COUNT];
    RdLedgerTable models;
} RdLedgerStage;

/* --- the tally (pure) ------------------------------------------------------- */

void rd_ledger_table_reset(RdLedgerTable *t);
/* The row of name (NULL or "": "(no name)"), added when new; -1 when the
   table is full (counted in pastNames).  A name of RD_LEDGER_NAME bytes or
   more is cut. */
int rd_ledger_slot(RdLedgerTable *t, const char *name);
void rd_ledger_note_built(RdLedgerTable *t, int slot);
/* a draw of the row recorded in tick (frame number), tris triangles */
void rd_ledger_note_draw(RdLedgerTable *t, int slot, uint32_t tick, uint32_t tris);
/* n draws, parts or objects of the row left out for reason in picture pic
   (a tick's frame number or the replay's number); alt 1: by the blended
   cameras (box cull) or between ticks (the port's test) */
void rd_ledger_note_drop(RdLedgerTable *t, int slot, int reason, uint32_t pic, int alt, uint32_t n);
/* src's rows added into dst's (the replay's half into the game's) */
void rd_ledger_table_add(RdLedgerTable *dst, const RdLedgerTable *src);

void rd_ledger_stage_begin(RdLedgerStage *s, int stage, const char *name, uint32_t firstTick);
/* an effect recorded in tick, its key value */
void rd_ledger_note_fx(RdLedgerStage *s, int fx, uint32_t tick, float value);
/* The rows of prev built and never drawn and never left out (the stage's
   models loaded before the stage number changed) carried into next with
   carried 1, so a model built in a loading screen and never drawn shows */
void rd_ledger_carry(RdLedgerStage *next, const RdLedgerStage *prev);

/* The stage's text: lines of "scene ledger: ...", each ended by '\n',
   when the stage's state (when: "ended", "so far, at this capture", "so
   far, at this report").  The header (ticks, pictures, models drawn, left
   out, never drawn, the effects), then the detailed rows, then the count of
   rows not detailed.  The length written (at most cap - 1). */
size_t rd_ledger_format(const RdLedgerStage *s, const char *when, char *out, size_t cap);

/* --- the kept stages (pure) ------------------------------------------------- */

typedef struct RdLedgerKept {
    char text[RD_LEDGER_KEEP][RD_LEDGER_TEXT];
    uint32_t n, head; /* head: the next to write */
} RdLedgerKept;

void rd_ledger_keep(RdLedgerKept *k, const char *text);
/* the kept stages, oldest first, then extra (the stage in play; NULL
   none), each after a blank line; the length written */
size_t rd_ledger_kept_text(const RdLedgerKept *k, const char *extra, char *out, size_t cap);

/* --- the renderer's ledger (rd_ledger_live.c) -------------------------------- */

/* Once a game step, on the game's thread (window_perf_rank.c): the stage in
   play.  A change closes the stage before (its lines to the log, its text
   kept) and opens the next.  Also registers rd_ledger_report_text with the
   bug report (report.h ico_report_set_ledger) at its first call. */
void rd_ledger_scene(int stage, const char *name);
/* The bug report's scene-ledger.txt: the kept stages and the stage in play
   so far; the length written.  Static data and snprintf only (a crash
   handler's report calls it). */
size_t rd_ledger_report_text(char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
