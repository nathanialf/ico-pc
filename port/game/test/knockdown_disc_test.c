/*
 * port/game/test/knockdown_disc_test.c
 *
 * The knock-down limit's hooks against a disc image's own tables: the disc's
 * boot ELF fills actModeTbl and actIntrList (port/data/tables.c, the first
 * descriptor set whose checksums it matches), then
 *
 *   modes    act mode 0x15 is DOWN and runs actCommonDown for all three
 *            actor kinds (the boy, the girl and the Shadows: the hook counts
 *            only the boy), 0x16 is DEAD (actCommonDie: the mode the watch in
 *            gamestate.c takes as "dead"), 5 FALL, 26 CLIFF_HESITATE
 *   lists    the lists end at the disc's terminator (act.c's
 *            act_check_intr_list: 429, 396 on the North American disc, 392
 *            on the August 2001 preview, 376 on the June 2001 demo)
 *   into     every row whose mode is DOWN, with the lists holding it: the
 *            knock-down mail in the shared list 3 (291 on the European
 *            family, 275, 272, 264 on the three 2001 discs); on the European
 *            family also mail 55 (list 3) and 56 (FALL's list), the landing
 *            of a fall the down act began, which the European body of
 *            actCommonDown passes as a landing; on the 2001 discs no row of
 *            mail 55 or 56 leads to DOWN (their copies pass no landing)
 *   down     DOWN's own list, printed (the get-up mails)
 *
 * Nothing starts the game; nothing read is written.  Skipped (77) without a
 * readable disc image, as in a worktree.
 *
 * Usage: knockdown_disc_test <disc image> <work folder>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define actModeTbl actModeTbl_header_decl
#include "act.h"
#undef actModeTbl

#include "../../core/region.h"
#include "tables.h"
#include "vfs.h"
#include "../../test/ico_check.h"

/* port/data/gen/table_defs.c's writable definitions */
extern ActModeRec actModeTbl[];

/* the EE symbols' registry (port/data/gen/<slug>/ee_symbols.c, stubs in this
   program): the loader stores these addresses for the act words */
void actCommonDown(void);
void actCommonDie(void);

#define M_FALL 5
#define M_DOWN 0x15
#define M_DEAD 0x16
#define M_CLIFF_HESITATE 26
#define MAX_ROWS 1024

typedef void (*ActFn)(void);

static int s_rows, s_modes, s_term;
static unsigned char s_in3[MAX_ROWS];    /* the row is in list 3 (every mode's) */
static unsigned char s_infall[MAX_ROWS]; /* the row is in FALL's list */

static const char *mode_name(int m)
{
    return m >= 0 && m < s_modes ? actModeTbl[m].name : "-";
}

/* rows from start to the terminator, marked */
static void mark(int start, unsigned char *in)
{
    for (int i = start; i >= 0 && i < s_rows && i < MAX_ROWS; i++) {
        in[i] = 1;
        if (actIntrList[i].kind == s_term) {
            break;
        }
    }
}

static void print_list(const char *what, int start)
{
    printf("knockdown_disc_test:   %s (row %d):", what, start);
    for (int i = start; i >= 0 && i < s_rows; i++) {
        if (actIntrList[i].kind == s_term) {
            printf(" end\n");
            return;
        }
        printf(" %d->%d %s", actIntrList[i].kind, actIntrList[i].mode,
               mode_name(actIntrList[i].mode));
    }
    printf(" (no end)\n");
}

int main(int argc, char **argv)
{
    const char *disc = argc > 1 ? argv[1] : NULL;
    FILE *probe = disc != NULL && disc[0] != '\0' ? fopen(disc, "rb") : NULL;
    char err[256];
    const IcoTableSet *set = NULL;

    if (probe == NULL) {
        printf("knockdown_disc_test: SKIP (no disc image%s%s)\n", disc ? " at " : "",
               disc ? disc : "");
        return 77;
    }
    fclose(probe);
    IcoVfs *vfs = ico_vfs_mount(&ico_vfs_iso9660, disc);
    if (vfs == NULL) {
        printf("knockdown_disc_test: SKIP (%s is not a readable disc image)\n", disc);
        return 77;
    }
    for (int i = 0; i < ICO_TABLE_SET_COUNT && set == NULL; i++) {
        if (ico_tables_load_vfs(vfs, ico_table_sets[i], err, sizeof err) == 0) {
            set = ico_table_sets[i];
        }
    }
    ico_vfs_unmount(vfs);
    CHECK(set != NULL, "no descriptor set loads this disc's tables (%s)", err);
    if (set == NULL || ico_region_select(set->slug) != 0) {
        return ico_check_summary("knockdown_disc_test");
    }
    const char *slug = set->slug;
    const int jun26 = strcmp(slug, "jun26") == 0;
    const int ntsc = strcmp(slug, "ntsc") == 0;
    const int aug6 = strcmp(slug, "aug6") == 0;
    const int europe = !jun26 && !ntsc && !aug6; /* pal, jpn, kor, cn: the European body */
    s_modes = (int)ico_tables_count(ICO_TID_actModeTbl);
    s_rows = (int)ico_tables_count(ICO_TID_actIntrList);
    printf("knockdown_disc_test: %s, the %s tables (%d act modes, %d interrupt rows)\n", disc, slug,
           s_modes, s_rows);
    CHECK(s_modes > M_DEAD && s_rows > 3 && s_rows <= MAX_ROWS, "%s: %d modes, %d rows", slug,
          s_modes, s_rows);
    if (s_modes <= M_DEAD || s_rows <= 3 || s_rows > MAX_ROWS) {
        return ico_check_summary("knockdown_disc_test");
    }

    /* modes */
    for (int k = 0; k < 3; k++) {
        CHECK((ActFn)actModeTbl[M_DOWN].ent[k].act == actCommonDown,
              "%s: mode 0x15's act for actor kind %d is not actCommonDown", slug, k);
        CHECK((ActFn)actModeTbl[M_DEAD].ent[k].act == actCommonDie,
              "%s: mode 0x16's act for actor kind %d is not actCommonDie", slug, k);
    }
    CHECK(strcmp(actModeTbl[M_DOWN].name, "DOWN") == 0, "%s: mode 0x15 is %s", slug,
          actModeTbl[M_DOWN].name);
    CHECK(strcmp(actModeTbl[M_DEAD].name, "DEAD") == 0, "%s: mode 0x16 is %s", slug,
          actModeTbl[M_DEAD].name);
    CHECK(strcmp(actModeTbl[M_FALL].name, "FALL") == 0, "%s: mode 5 is %s", slug,
          actModeTbl[M_FALL].name);
    CHECK(strcmp(actModeTbl[M_CLIFF_HESITATE].name, "CLIFF_HESITATE") == 0, "%s: mode 26 is %s",
          slug, actModeTbl[M_CLIFF_HESITATE].name);
    printf("knockdown_disc_test:   DOWN: actCommonDown for the three actor kinds, its list at "
           "row %d, its mail %d, skip-marked %d\n",
           actModeTbl[M_DOWN].intrList, actModeTbl[M_DOWN].mail, actModeTbl[M_DOWN].skipMarked);

    /* lists */
    s_term = actIntrList[s_rows - 1].kind;
    {
        const int want = jun26 ? 376 : ntsc ? 396 : aug6 ? 392 : 429;

        CHECK(s_term == want, "%s: the lists end at kind %d, want %d", slug, s_term, want);
    }
    memset(s_in3, 0, sizeof(s_in3));
    memset(s_infall, 0, sizeof(s_infall));
    mark(3, s_in3);
    mark(actModeTbl[M_FALL].intrList, s_infall);
    print_list("list 0", 0);
    print_list("DOWN's list", actModeTbl[M_DOWN].intrList);

    /* into DOWN */
    {
        const int knock = jun26 ? 264 : aug6 ? 272 : ntsc ? 275 : 291;
        int seen_knock = 0, seen55 = 0, seen56 = 0, other = 0;

        for (int i = 0; i < s_rows; i++) {
            const int kind = actIntrList[i].kind;

            if (kind == 55 || kind == 56) {
                printf("knockdown_disc_test:   row %d: mail %d -> mode %d %s%s%s\n", i, kind,
                       actIntrList[i].mode, mode_name(actIntrList[i].mode),
                       s_in3[i] ? " (list 3)" : "", s_infall[i] ? " (FALL's list)" : "");
            }
            if (actIntrList[i].mode != M_DOWN) {
                continue;
            }
            printf("knockdown_disc_test:   row %d: mail %d -> DOWN%s%s\n", i, kind,
                   s_in3[i] ? " (list 3)" : "", s_infall[i] ? " (FALL's list)" : "");
            if (kind == knock && s_in3[i]) {
                seen_knock++;
            } else if (europe && kind == 55 && s_in3[i]) {
                seen55++;
            } else if (europe && kind == 56 && s_infall[i]) {
                seen56++;
            } else {
                other++;
                CHECK(0, "%s: row %d: mail %d leads to DOWN (not the knock-down mail %d%s)", slug,
                      i, kind, knock, europe ? " nor the landing's 55 and 56" : "");
            }
        }
        CHECK(seen_knock == 1, "%s: the knock-down mail %d leads to DOWN in list 3 %d times", slug,
              knock, seen_knock);
        if (europe) {
            CHECK(seen55 == 1 && seen56 == 1,
                  "%s: the landing's mails into DOWN: 55 in list 3 %d, 56 in FALL's list %d", slug,
                  seen55, seen56);
        }
        printf("knockdown_disc_test:   into DOWN: the knock-down mail %d, landings %d, others "
               "%d\n",
               knock, seen55 + seen56, other);
    }
    return ico_check_summary("knockdown_disc_test");
}
