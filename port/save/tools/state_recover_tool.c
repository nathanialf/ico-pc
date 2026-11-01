/*
 * port/save/tools/state_recover_tool.c
 *
 * state_recover_tool: a memory-card save from an old save state's progress
 * (port/save/state_recover.h), from the side file tools/state_recover.py
 * writes.  No game starts and no disc is read: the save image the state
 * holds goes to the card through the game's own card code.
 *
 *   state_recover_tool SIDE_FILE --card CARD_FOLDER [--slot N] [--overwrite]
 *
 * CARD_FOLDER is the card folder ([paths] saves; memcard/ in the user folder
 * by default; memcard-preview/ for the August 2001 preview).  Without --slot
 * the save goes to the lowest free slot.  Exit status: 0 written, 1 refused
 * (the reason printed), 2 a usage or card error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "state_recover.h"

static int usage(void)
{
    fprintf(stderr, "usage: state_recover_tool SIDE_FILE --card CARD_FOLDER [--slot N] "
                    "[--overwrite]\n");
    return 2;
}

/* frames as h:mm:ss at the disc's field rate (layout_action.c playTime:
   60 - 10 * systemStatus[0] fields a second) */
static void play_time(char *out, size_t n, unsigned long frames, int pal)
{
    const unsigned long fps = pal ? 50ul : 60ul;
    const unsigned long s = frames / fps;

    snprintf(out, n, "%lu:%02lu:%02lu", s / 3600ul, s / 60ul % 60ul, s % 60ul);
}

int main(int argc, char **argv)
{
    const char *side_path = NULL;
    const char *card = NULL;
    int slot = -1;
    int overwrite = 0;
    IcoRecoverSide side;
    IcoRecoverInfo info;
    IcoRecoverResult res;
    char why[256];
    char when[64] = "an unknown time";
    char played[32];
    extern int systemStatus[12];
    int i;
    int rc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--card") == 0 && i + 1 < argc) {
            card = argv[++i];
        } else if (strcmp(argv[i], "--slot") == 0 && i + 1 < argc) {
            char *end;

            slot = (int)strtol(argv[++i], &end, 10);
            if (*end != '\0' || slot < 0 || slot > 9) {
                fprintf(stderr, "state_recover: --slot takes 0 to 9\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--overwrite") == 0) {
            overwrite = 1;
        } else if (argv[i][0] != '-' && side_path == NULL) {
            side_path = argv[i];
        } else {
            return usage();
        }
    }
    if (side_path == NULL || card == NULL) {
        return usage();
    }
    if (ico_recover_side_read(side_path, &side, why, sizeof why) != 0) {
        fprintf(stderr, "state_recover: %s\n", why);
        ico_recover_side_free(&side);
        return 2;
    }
    if (ico_recover_apply(&side, why, sizeof why) != 0) {
        printf("state_recover: refused: %s\n", why);
        ico_recover_side_free(&side);
        return 1;
    }
    ico_recover_info(&info);
    if (side.saved_at > 0) {
        time_t t = (time_t)side.saved_at;
        struct tm *tm = localtime(&t);

        if (tm != NULL) {
            strftime(when, sizeof when, "%Y-%m-%d %H:%M", tm);
        }
    }
    play_time(played, sizeof played, side.play_frames, systemStatus[0] != 0);
    printf("state_recover: %s (format %d, %s, %s disc), saved %s in stage %d, play time %s\n",
           side.source[0] != '\0' ? side.source : side_path, side.format, side.commit, side.disc,
           when, side.state_stage, played);
    printf("state_recover: the game's last checkpoint before it is in stage %d: %d story flags, "
           "%s, version stamp %s; its save image %d bytes of the %d-byte block\n",
           info.stage, info.flags_set,
           info.cleared < 0 ? "no cleared flag on this disc"
           : info.cleared   ? "the game cleared"
                            : "the game not cleared",
           info.stamp ? "the disc's" : "blank (the card it came from was another version's)",
           info.image_bytes, info.block_bytes);

    rc = ico_recover_write_card(card, slot, overwrite, &res);
    if (rc != 0) {
        printf("state_recover: %s: %s\n", rc > 0 ? "refused" : "failed", res.why);
        ico_recover_side_free(&side);
        return rc > 0 ? 1 : 2;
    }
    printf("state_recover: wrote slot %d: %s (%d bytes, sum 0x%08lx); the product file %s, the "
           "slot's preview stage %d, play time %s\n",
           res.slot, res.path, res.file_bytes, (unsigned long)res.sum,
           res.product_existed ? "updated (the other slots kept)" : "made (a first save's)",
           info.stage, played);
    printf("state_recover: loading it from Continue starts stage %d where the game starts any "
           "loaded save in that stage: the boy, and Yorda when the stage has her, at the end of "
           "the first exit into the stage in the game's exit table (layout_action.c "
           "la_load_processing, act-game.c ACTGame_SetActors_Debug)",
           info.stage);
    if (info.sofa != 0) {
        printf(", the boy sitting on the sofa (layout %d) where this checkpoint was a sofa save",
               info.sofa);
    }
    printf("; the story flags, the objects' records, the enemy generators, the hints and "
           "Yorda's state are the checkpoint's, not the moment of the state (stage %d)\n",
           side.state_stage);
    if (info.weapon != 0) {
        printf("state_recover: the boy starts holding the weapon of layout %d, when that object "
               "is in the stage\n",
               info.weapon);
    }
    if (!res.icons) {
        printf("state_recover: the card's directory has no icon files yet: the game writes them "
               "at its next save\n");
    }
    printf("state_recover: config.toml's [mirror] and achievements.toml were not changed: the "
           "slot has no mirror-mode flag and no achievement run of its own\n");
    ico_recover_side_free(&side);
    return 0;
}
