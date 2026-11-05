/*
 * port/game/game_state_keep.h
 *
 * The game's state kept across a port excursion from the title (Extras >
 * Credits, credits_live.c; Extras > Unused content's test rooms,
 * unused_live.c) and put back as the title's stage is entered again
 * (game_state_keep.c).  Game side: built into the program with the game's
 * include path, and into credits_test and unused_content_test over stubs.
 */
#ifndef PORT_GAME_GAME_STATE_KEEP_H
#define PORT_GAME_GAME_STATE_KEEP_H

#ifdef __cplusplus
extern "C" {
#endif

/* the story flags kept: gflag.c's bitmap at its largest (400 flags on the
   European, Japanese, Korean and Asian discs, 368 on the North American
   disc and the August 2001 preview, 352 on the June 2001 demo: region.h
   gflag_count), walked to the running disc's count */
#define ICO_KEEP_GFLAG_MAX 400
/* gamesys.c's checkpoint image (gamesys.h gameSysMainSaveBuff; the .c
   checks the size against the declaration) */
#define ICO_KEEP_SAVE_IMAGE 25596

typedef struct IcoGameKeep {
    unsigned char flags[ICO_KEEP_GFLAG_MAX];
    int gameClear;
    char world[ICO_KEEP_SAVE_IMAGE];
    char checkpoint[ICO_KEEP_SAVE_IMAGE];
    int status[3]; /* systemStatus[2..4] */
} IcoGameKeep;

/* everything a save holds, taken with the game's own gamesysMemorySave
   into k (the flags, the object records that place the boy and Yorda in a
   stage, the generators, hints, the character record, the back stage,
   itou's flags), the checkpoint image gameSysMainSaveBuff, gFlagGameClear
   and systemStatus[2..4] */
void ico_game_keep_take(IcoGameKeep *k);
/* the state k holds put back: gamesysMemoryLoad as the title's Load does,
   the checkpoint image, systemStatus[2..4], then the flags bit by bit;
   returns how many flags differed after the load (ACTGame_SetActors_Debug
   sets the disc's 394, 361, 360 or 343) */
int ico_game_keep_put_back(IcoGameKeep *k);

#ifdef __cplusplus
}
#endif

#endif /* PORT_GAME_GAME_STATE_KEEP_H */
