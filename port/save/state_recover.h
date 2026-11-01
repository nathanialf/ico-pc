/*
 * port/save/state_recover.h
 *
 * A memory-card save from the progress an old save state holds: the engine of
 * state_recover_tool (port/save/tools/state_recover_tool.c), fed by
 * tools/state_recover.py, which reads the state file and the symbol table of
 * the build that wrote it and writes the "side file" below.
 *
 * What a state gives: the game's globals as they were, among them
 * gameSysMainSaveBuff, the save image the game's own gamesysMemorySave wrote
 * at its last checkpoint (StageManager.c CheckPoint, gated by systemStatus[2],
 * which main.c's boot sets; the scripts' checkpoints, the idol doors'
 * scpSekizouCheckPoint and every sofa save run it, layout_action.c
 * la_save_processing step 7) and the block a sofa save writes to the card
 * (the same step).  The game restores that image itself after a game over
 * (icoMisc.c, systemStatus[3] and [4]).  This engine writes that image, with
 * the option words and the slot's preview a sofa save adds, to a card folder
 * through the game's own card code (ico2/fumi/ios/mcard.c on the fiber
 * scheduler over port/save/mc_host.c), into a free slot.  Nothing of the game
 * runs but the card code.
 *
 * The side file: text lines, then blobs, little-endian as the state was:
 *
 *   ICORESCUE 1
 *   disc <slug>            the state's disc (config/regions.txt)
 *   commit <text>          the state's version (its header)
 *   platform <tag>         the state's platform
 *   format <n>             the state file's format version
 *   state_stage <n>        stage_no when the state was saved
 *   play_frames <n>        the play time then, in frames
 *   main_tick <n>          the Main tick then
 *   saved_at <n>           the save's time (seconds since 1970)
 *   source <text>          the state file's name
 *   blob <name> <bytes>    then <bytes> raw bytes and a newline: the global
 *                          <name>'s bytes in the state
 *   end
 *
 * Unknown text lines are ignored; a blob whose name this program does not
 * know is kept and ignored (ico_recover_apply names it).
 */
#ifndef ICO_SAVE_STATE_RECOVER_H
#define ICO_SAVE_STATE_RECOVER_H

#include <stddef.h>
#include <stdint.h>

#define ICO_RECOVER_MAGIC "ICORESCUE 1"
#define ICO_RECOVER_MAX_BLOBS 16
#define ICO_RECOVER_NAME_BYTES 48
#define ICO_RECOVER_BLOB_MAX (1u << 20) /* a blob's largest size */

typedef struct IcoRecoverBlob {
    char name[ICO_RECOVER_NAME_BYTES];
    uint32_t size;
    unsigned char *data; /* malloc'd */
} IcoRecoverBlob;

typedef struct IcoRecoverSide {
    char disc[16];
    char commit[64];
    char platform[24];
    char source[256];
    int format;
    int state_stage;
    uint32_t play_frames;
    uint32_t main_tick;
    int64_t saved_at;
    int n_blobs;
    IcoRecoverBlob blobs[ICO_RECOVER_MAX_BLOBS];
} IcoRecoverSide;

/* Reads a side file: 0, or -1 with the reason in why (n bytes).  out is
   zeroed first; free it with ico_recover_side_free either way. */
int ico_recover_side_read(const char *path, IcoRecoverSide *out, char *why, size_t n);
/* Writes one (the tests'): 0 or -1. */
int ico_recover_side_write(const char *path, const IcoRecoverSide *s);
/* Adds a blob (a copy of data): 0, or -1 when full or too large. */
int ico_recover_side_add(IcoRecoverSide *s, const char *name, const void *data, uint32_t size);
void ico_recover_side_free(IcoRecoverSide *s);
/* The blob of that name, or NULL. */
const IcoRecoverBlob *ico_recover_side_blob(const IcoRecoverSide *s, const char *name);

/* Selects the side file's disc (ico_region_select), then copies each blob
   into this program's global of the same name: gameSysMainSaveBuff,
   systemStatus, IosMcPreviewInfo, optionScreenMode, girlControlMode and
   IosMcProductFile.  0, or -1 with why: an unknown disc, a blob whose size
   is not its global's ("the state's X is N bytes, this program's M"), a
   required one missing (the first three always; the two option words on the
   discs whose game block carries them), a state saved before any checkpoint
   (systemStatus[3] 0: "no checkpoint since the game started"), or a save
   image without this disc's version stamp. */
int ico_recover_apply(const IcoRecoverSide *s, char *why, size_t n);

/* What the applied save image holds, read by the disc's record sizes
   (ico2/common/src/gamesys.c gameSysMemoryFuncList and the save functions
   it names). */
typedef struct IcoRecoverInfo {
    int block_bytes;     /* the game block the card gets (gameblock_write) */
    int image_bytes;     /* the records gamesysMemorySave writes */
    int stamp;           /* 1 the disc's version stamp, 0 zeros (gamesysVersionDiff) */
    int stage;           /* gflagSave's gFlagSaveStage: the stage a load starts */
    int cleared;         /* gFlagGameClear; -1 on the 2001 discs (not in the block) */
    int flags_set;       /* story flags set */
    uint32_t time_count; /* gamesysTimeCount */
    int boy_stage;       /* gameSysObjInfo[0].stage, 0xFFFF none */
    int girl_stage;      /* gameSysObjInfo[1].stage */
    int weapon;          /* the character record's weapon layout id, 0 none */
    int held;            /* the object in the boy's hand (curItem), 0 none */
    int sofa;            /* the sofa's layout id when the image is a sofa save, else 0 */
    int option_words;    /* 1 when the block carries optionScreenMode and girlControlMode */
} IcoRecoverInfo;

void ico_recover_info(IcoRecoverInfo *out);

/* The card save.  card: the card folder (port 0's root, as [paths] saves);
   slot: 0..9, or -1 for the lowest slot with no game.00N.  A slot that holds
   a save is refused unless overwrite.  The game's own steps
   (layout_action.c la_save_processing): the card's product file read when
   there is one, else made as a first save makes it (ten empty previews, a
   serial: the state's own when it has one); the slot's preview set (the
   image's stage, cleared flag and sofa, the state's play time), the record's
   current file set to the slot, the product file written, then the game
   block from gameSysMainSaveBuff with the option words; the file read back
   and its sum checked.  The other slots, the icon files and config.toml's
   [mirror] / achievements.toml keys are left as they are. */
typedef struct IcoRecoverResult {
    int slot;
    uint32_t sum;        /* the game file's sum (the [mirror] keys' slot_N_sum) */
    int file_bytes;      /* game.00N's size */
    int product_existed; /* 1 the card's product file was read, 0 made */
    int icons;           /* 1 the directory holds icon.sys and boy_blk.ico */
    uint32_t used_mask;  /* the slots that held a save before */
    char path[320];      /* <card>/<directory>/game.00N */
    char why[256];       /* a refusal's or failure's reason */
} IcoRecoverResult;

/* 0 written; 1 refused (why: the slot is taken, no free slot, a damaged
   product file); -1 failed (why: a card error). */
int ico_recover_write_card(const char *card, int slot, int overwrite, IcoRecoverResult *res);

/* The read side, for the tests: slot's game block (block_bytes bytes plus
   the option words) and the product record through the game's
   iosMcLoadProductBlock and iosMcLoadGameBlock.  0, or -1 (a card error or
   a bad sum: *result the McMgr's result). */
int ico_recover_read_card(const char *card, int slot, unsigned char *block, size_t block_size,
                          void *product, size_t product_size, int *result);

#endif /* ICO_SAVE_STATE_RECOVER_H */
