/*
 * port/save/state_checkpoint.h
 *
 * Load on a save state that cannot resume where it was saved: the game
 * continues from the checkpoint the state holds.  Every state (format 1
 * from v0.9.6, format 2 from v0.10.0) holds the game's own save image
 * gameSysMainSaveBuff, which gamesysMemorySave wrote at the last checkpoint
 * (StageManager.c CheckPoint: the scripts' checkpoints, the idol doors,
 * every sofa save) and which is what a sofa save writes to the card, plus
 * the words a card load sets beside it.  This part finds them in the
 * state's two game sections (gdata, gbss) without the game; the game's own
 * load runs them (port/game/state_checkpoint_live.c).
 *
 * The globals (ICO_CKPT_G_*), with their sizes in every build:
 *   gameSysMainSaveBuff  25596  the save image (gamesys.c)
 *   systemStatus            48  word 3: a checkpoint was made since the
 *                               journey started (gflagInit clears it, the
 *                               image is kept: a state saved before its
 *                               journey's first checkpoint holds an older
 *                               journey's image)
 *   IosMcPreviewInfo        24  the slot preview (stage, play time, sofa)
 *   optionScreenMode         4  the card block's two option words (the
 *   girlControlMode          4  2001 discs keep Players beside the save)
 *   stage_no                 4  checks a place against the state's header
 *
 * Where they are, in this order (ico_ckpt_locate):
 *   1. the state's own "ckpt" section (states from v0.11.9 on): each
 *      global's range and offset, written from the saving program's own
 *      addresses (ico_ckpt_record_write);
 *   2. the table (state_checkpoint_table.c, tools/state_checkpoint_table.py
 *      from every release asset from v0.9.6 to v0.11.8): the entry of the
 *      state header's commit text and platform;
 *   3. the layout of any table entry of the same platform whose two
 *      ranges have the state's sizes, and this program's own (a release
 *      candidate, or a build of its own, shares a released layout): taken
 *      when the state's own bytes confirm it and every confirming layout
 *      puts the image and systemStatus at one place;
 *   4. the image alone, by its contents: the disc's version stamp (the
 *      image's first 18 bytes, gamesysVersionSave), the save stage after
 *      it (1 .. the disc's last stage) and, but on the 2001 discs (ntsc,
 *      aug6, jun26, whose image has none), the cleared word (0 or 1), the
 *      whole 25596 bytes inside the section.
 *      The game's own copy of the stamp (gamesys.c stamp_str_*, followed by
 *      the next disc's stamp text) never passes: its "stage" is text.  One
 *      image passes, or several byte-identical ones: taken; none or two that
 *      differ: not found.  systemStatus, the preview and the option words
 *      are then unknown (this run's are kept, and the checkpoint is taken
 *      to be the journey's: the one case the image can be an older
 *      journey's).
 * A place "confirms" (1-3) when it lies inside its section, stage_no there
 * equals the header's stage and the image there passes the content test,
 * or, with systemStatus word 3 clear, holds no checkpoint at all (then the
 * state is refused: nothing to continue from).
 *
 * Which refusals take this path (ico_ckpt_route): every refusal of another
 * version's state and of another platform's, and of this version's state
 * when what failed was its own memory (the restore's self-check, a damaged
 * section the checkpoint does not need).  Never: another disc, a Permadeath
 * journey (the state's or the one in force), an empty slot, the conditions
 * of the moment (not paused, a stage change, the card or the disc busy, a
 * movie, another mode), this version's state with other settings (the box
 * names the setting; with it put back the state resumes exactly).
 */
#ifndef ICO_SAVE_STATE_CHECKPOINT_H
#define ICO_SAVE_STATE_CHECKPOINT_H

#include <stddef.h>
#include <stdint.h>

#include "state.h"      /* IcoStateResult */
#include "state_file.h" /* IcoStateHeader */

enum {
    ICO_CKPT_G_IMAGE = 0,
    ICO_CKPT_G_STATUS,
    ICO_CKPT_G_PREVIEW,
    ICO_CKPT_G_SCREEN,
    ICO_CKPT_G_GIRL,
    ICO_CKPT_G_STAGE,
    ICO_CKPT_G_COUNT
};

#define ICO_CKPT_IMAGE_BYTES 25596u
#define ICO_CKPT_STAMP_BYTES 18u
#define ICO_CKPT_RANGE_NONE (-1)

/* the global's name and size (the sizes above) */
const char *ico_ckpt_global_name(int g);
uint32_t ico_ckpt_global_size(int g);

/* A global's place: range 0 (gdata) or 1 (gbss), its offset in the
   state's section; range ICO_CKPT_RANGE_NONE when the program has none. */
typedef struct IcoCkptPlace {
    int range;
    uint32_t off;
    uint32_t size;
} IcoCkptPlace;

/* A table entry (state_checkpoint_table.c). */
typedef struct IcoCkptLayout {
    const char *tag;      /* the release ("v0.11.5") */
    const char *commit;   /* the commit text its states' headers carry ("" not found) */
    const char *platform; /* the platform tag (state_file.h ico_state_platform_tag) */
    uint32_t gdata_bytes, gbss_bytes;
    IcoCkptPlace g[ICO_CKPT_G_COUNT];
    const char *asset;  /* the release asset it was read from */
    const char *sha256; /* that asset's */
} IcoCkptLayout;

extern const IcoCkptLayout ico_ckpt_table[];
extern const int ico_ckpt_table_count;

/* The entry of exactly this commit text and platform, or NULL. */
const IcoCkptLayout *ico_ckpt_table_find(const IcoCkptLayout *tab, int n, const char *commit,
                                         const char *platform);

/* --- the "ckpt" section a state carries from v0.11.9 on ---------------------------
 * Little-endian: "CKPT", version 1, the count, 0; then per global 32 bytes:
 * its name (20 bytes, NUL-padded), range (int32), offset, size (uint32). */
#define ICO_CKPT_RECORD_MAGIC 0x54504B43u /* "CKPT" */
#define ICO_CKPT_RECORD_BYTES (16u + 32u * ICO_CKPT_G_COUNT)

/* The record of places g (ICO_CKPT_G_COUNT of them) into out: its size, or
   0 when cap is too small. */
size_t ico_ckpt_record_write(const IcoCkptPlace *g, void *out, size_t cap);
/* g from a record: 0, or -1 (g all NONE) when it is damaged, of another
   kind, or names a global at another size.  An unknown name is skipped, a
   global it lacks stays NONE. */
int ico_ckpt_record_read(const void *p, size_t n, IcoCkptPlace *g);

/* --- the disc's image -------------------------------------------------------------- */

/* The disc's stamp (18 bytes with the NUL; the Asian disc is the Korean
   build's), or NULL for a slug this does not know. */
const char *ico_ckpt_stamp(const char *disc);
/* 1 on the 2001 discs (ntsc, aug6, jun26): no cleared word after the stage. */
int ico_ckpt_disc_2001(const char *disc);
/* the disc's last stage (region.c's stage counts) */
int ico_ckpt_disc_last_stage(const char *disc);

/* 1 when the avail bytes at p start a save image of this disc (stamp,
   stage, cleared word, the whole image inside avail); *stage its stage */
int ico_ckpt_image_valid(const unsigned char *p, uint64_t avail, const char *disc, int *stage);

/* The two sections. */
typedef struct IcoCkptSections {
    const unsigned char *p[2];
    uint64_t n[2];
} IcoCkptSections;

enum { ICO_CKPT_SEARCH_ONE = 0, ICO_CKPT_SEARCH_NONE, ICO_CKPT_SEARCH_SEVERAL };

/* The content search (4 above): ICO_CKPT_SEARCH_ONE with *at (range and
   offset), or NONE / SEVERAL; *passed the images that passed (identical
   copies counted), *rejected the stamps whose bytes after failed. */
int ico_ckpt_search(const IcoCkptSections *s, const char *disc, IcoCkptPlace *at, int *passed,
                    int *rejected);

/* --- the whole decision -------------------------------------------------------------- */

enum {
    ICO_CKPT_BY_NONE = 0,
    ICO_CKPT_BY_RECORD, /* the state's own record */
    ICO_CKPT_BY_TABLE,  /* the table's entry of its build */
    ICO_CKPT_BY_LAYOUT, /* a released (or this program's) layout its bytes confirm */
    ICO_CKPT_BY_SEARCH  /* the image found by its contents */
};

typedef struct IcoCkptInput {
    const char *commit, *platform, *disc; /* the state's header */
    int stage_no;                         /* the header's */
    IcoCkptSections sec;
    const void *record; /* the "ckpt" section, or NULL */
    size_t record_n;
    const IcoCkptLayout *table;
    int table_n;
    /* this program's own layout (platform, sizes, places), or NULL */
    const IcoCkptLayout *self;
} IcoCkptInput;

typedef struct IcoCkptPlan {
    int by;         /* ICO_CKPT_BY_* */
    int checkpoint; /* systemStatus word 3: 1, 0 (none since the journey started), -1 unknown */
    /* each global's bytes in the sections, NULL when not known */
    const unsigned char *g[ICO_CKPT_G_COUNT];
    IcoCkptPlace place[ICO_CKPT_G_COUNT];
    int image_stage; /* the stage the image's journey continues in */
    int layouts;     /* BY_LAYOUT: the layouts that confirmed */
    int passed;      /* BY_SEARCH: the images that passed, the stamps that failed */
    int rejected;
    char source[128]; /* what found it, for the log */
    char why[200];    /* when not found (or no checkpoint) */
} IcoCkptPlan;

/* 0 found (plan->checkpoint may still be 0: nothing to continue from), -1
   not found (why). */
int ico_ckpt_locate(const IcoCkptInput *in, IcoCkptPlan *plan);

/* --- which refusals take it ------------------------------------------------------------ */

typedef enum IcoCkptRoute {
    ICO_CKPT_ROUTE_EXACT = 0,  /* it resumed where it was saved */
    ICO_CKPT_ROUTE_CHECKPOINT, /* the checkpoint path is tried */
    ICO_CKPT_ROUTE_REFUSED     /* refused as before */
} IcoCkptRoute;

/* r: the exact path's result for the state; other_version: the state's
   build is not this one (its header's build id or format differs). */
IcoCkptRoute ico_ckpt_route(IcoStateResult r, int other_version);
const char *ico_ckpt_route_text(IcoCkptRoute k);

/* --- the syms section's block list, read without the symbol table ------------------------
 * (state_xbuild.h's record: the header's counts, then the units, data,
 * points and blocks records): the named block's offset in its packed
 * section and its size and flags.  1 found, 0 not, -1 the record is
 * damaged.  *layout (may be NULL) the record's game data layout hash. */
int ico_ckpt_syms_block(const void *syms, size_t n, const char *name, uint64_t *off, uint64_t *size,
                        unsigned *flags, uint64_t *layout);

/* --- a state file's part of it ------------------------------------------------------------ */

enum {
    ICO_CKPT_S_GDATA = 0,
    ICO_CKPT_S_GBSS,
    ICO_CKPT_S_REG,
    ICO_CKPT_S_SYMS,
    ICO_CKPT_S_CKPT,
    ICO_CKPT_S_COUNT
};

#define ICO_CKPT_SECTION_MAX (256ull << 20) /* a section read at most (gbss is 6 MB) */

typedef struct IcoCkptState {
    IcoStateHeader h;
    unsigned char *p[ICO_CKPT_S_COUNT]; /* malloc'd; NULL when the state has none */
    uint64_t n[ICO_CKPT_S_COUNT];
    char why[160];
} IcoCkptState;

/* Reads the header and the sections "gdata", "gbss", "reg", "syms" and
   "ckpt" (any format, any build: no size is checked against this program).
   0, or -1 with st->why (no file, a damaged header or section, no gdata
   section, out of memory).  st is zeroed first; free it either way. */
int ico_ckpt_state_read(const char *path, IcoCkptState *st);
void ico_ckpt_state_free(IcoCkptState *st);
/* in from st (the table and self given) */
void ico_ckpt_state_input(const IcoCkptState *st, const IcoCkptLayout *self, IcoCkptInput *in);
/* The registered block `name` in the state's reg section by its syms
   section's block list: 1 with *out (size bytes, this program's size), 0
   when the state has no such block (or no list, a format 1 state), -1 when
   it is of another size, an audio block or outside the section (why). */
int ico_ckpt_state_block(const IcoCkptState *st, const char *name, uint64_t size,
                         const unsigned char **out, char *why, size_t n);

#endif /* ICO_SAVE_STATE_CHECKPOINT_H */
