/*
 * port/game/test/pit_dive_base_test.c
 *
 * Time to rescue Yorda's base ([gameplay] pit_time, port/game/pit_time.h)
 * read off a disc image: the disc's boot ELF fills the motion tables
 * (port/data/tables.c, the first descriptor set whose checksums it
 * matches), then
 *
 *   names    EN1 SINK_A .. SINK_E are found by name, five numbers in a row
 *            from the one the game's hook in that disc's copy of
 *            actEnemyKidnapEnd starts at (ico2/fumi/src/enemy_act.c: 948
 *            on the European, Japanese, Korean and Asian discs, 899 North
 *            American, 901 August 2001, 934 June 2001)
 *   speed    each plays at the game's own speed: playSpeedRatio and
 *            palSpeedRatio 1, no blend row (its frames and speed are its
 *            own), flag bit 26 clear (the play-speed reserve applies)
 *   chain    the motion-orient table: EN1 CARRY_LOOP's row of some kind
 *            leads to SINK_A, and that kind's rows lead SINK_A to B, C, D,
 *            E, each at its motion's last frame (shift frame -1), and
 *            SINK_E to the disc's "no motion" (it holds)
 *   frames   every .mob copy of the five in DATA.DF's packs (the members
 *            numbered by those motions): the first word, the frame count
 *            GetNbMotionFrames reads, the same in every copy
 *   base     (A - 1) + (B - 1) + (C - 1) + (D - 1) + 51 ticks equals
 *            ico_pit_chain_ticks_for(the disc's slug)
 *
 * Nothing starts the game; nothing read is written.  Skipped (77) without
 * a readable disc image, as in a worktree.
 *
 * Usage: pit_dive_base_test <disc image>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "typedef.h"
#include "motionOrientManager.h"

#include "../../core/region.h"
#include "df_pack.h"
#include "pit_time.h"
#include "tables.h"
#include "vfs.h"
#include "../../test/ico_check.h"

/* port/data/gen/table_defs.c's writable definitions */
extern MotionDef motionKind[];
extern MotionOrientEntry motionOrient[];

static const char *const kSink[5] = {"EN1 SINK_A", "EN1 SINK_B", "EN1 SINK_C", "EN1 SINK_D",
                                     "EN1 SINK_E"};

/* the number of the motion named name, or -1 */
static int motionByName(const char *name)
{
    const int n = (int)ico_tables_count(ICO_TID_motionKind);

    for (int i = 0; i < n; i++) {
        if (strncmp(motionKind[i].name, name, sizeof(motionKind[i].name)) == 0) {
            return i;
        }
    }
    return -1;
}

/* the first motion-orient row of motion id and request kind, as the game's
   lookup finds it (motionOrientManager.c GetMotionOrient), or NULL */
static const MotionOrientEntry *orientRow(int id, int kind)
{
    const int n = (int)ico_tables_count(ICO_TID_motionOrient);

    for (int i = 0; i < n; i++) {
        if (motionOrient[i].id == id && motionOrient[i].kind == kind) {
            return &motionOrient[i];
        }
    }
    return NULL;
}

/* the frame counts of the five motions, from their .mob copies */
typedef struct Frames {
    int id[5];
    int frames[5]; /* 0: no copy read yet */
    int copies[5];
    int mismatch;
} Frames;

static int motionMember(const char *name)
{
    const size_t n = name != NULL ? strlen(name) : 0;

    return n > 4 && (strcmp(name + n - 4, ".mob") == 0 || strcmp(name + n - 4, ".MOB") == 0);
}

static int readFrames(void *user, int member, const char *name, const IcoDfMember *m,
                      const uint8_t *data)
{
    Frames *f = (Frames *)user;

    (void)member;
    if (!motionMember(name) || m->size < 4) {
        return 0;
    }
    for (int k = 0; k < 5; k++) {
        if (m->id != f->id[k]) {
            continue;
        }
        const int frames = (int)((uint32_t)data[0] | (uint32_t)data[1] << 8 |
                                 (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24);
        if (f->copies[k] == 0) {
            printf("pit_dive_base_test:   %4d %-12s %3d frames  %s (%u bytes)\n", m->id, kSink[k],
                   frames, name, (unsigned)m->size);
            f->frames[k] = frames;
        } else if (frames != f->frames[k]) {
            printf("pit_dive_base_test:   %4d %-12s %3d frames in %s, %d in the first copy\n",
                   m->id, kSink[k], frames, name, f->frames[k]);
            f->mismatch++;
        }
        f->copies[k]++;
    }
    return 0;
}

static void readMotionFrames(IcoDfIndex *idx, Frames *f)
{
    const int members = ico_df_index_member_count(idx);
    const int entries = ico_df_index_entries(idx);
    char *packs = (char *)calloc(entries > 0 ? (size_t)entries : 1, 1);

    CHECK(packs != NULL, "out of memory");
    if (packs == NULL) {
        return;
    }
    /* the packs holding one of them, each inflated once */
    for (int i = 0; i < members; i++) {
        IcoDfMember m;
        if (ico_df_index_member_at(idx, i, &m) != 0 || m.pack < 0 || m.pack >= entries ||
            !motionMember(ico_df_index_member_name(idx, i))) {
            continue;
        }
        for (int k = 0; k < 5; k++) {
            if (m.id == f->id[k]) {
                packs[m.pack] = 1;
            }
        }
    }
    for (int p = 0; p < entries; p++) {
        if (packs[p]) {
            CHECK(ico_df_index_read_pack(idx, p, readFrames, f) >= 0, "pack %s did not read",
                  ico_df_index_entry_name(idx, p));
        }
    }
    free(packs);
}

int main(int argc, char **argv)
{
    const char *disc = argc > 1 ? argv[1] : NULL;
    FILE *probe = disc != NULL && disc[0] != '\0' ? fopen(disc, "rb") : NULL;
    char err[256];
    const IcoTableSet *set = NULL;

    if (probe == NULL) {
        printf("pit_dive_base_test: SKIP (no disc image%s%s)\n", disc ? " at " : "",
               disc ? disc : "");
        return 77;
    }
    fclose(probe);
    IcoVfs *vfs = ico_vfs_mount(&ico_vfs_iso9660, disc);
    if (vfs == NULL) {
        printf("pit_dive_base_test: SKIP (%s is not a readable disc image)\n", disc);
        return 77;
    }
    for (int i = 0; i < ICO_TABLE_SET_COUNT && set == NULL; i++) {
        if (ico_tables_load_vfs(vfs, ico_table_sets[i], err, sizeof err) == 0) {
            set = ico_table_sets[i];
        }
    }
    CHECK(set != NULL, "no descriptor set loads this disc's tables (%s)", err);
    if (set == NULL || ico_region_select(set->slug) != 0) {
        ico_vfs_unmount(vfs);
        return ico_check_summary("pit_dive_base_test");
    }
    const char *slug = set->slug;
    /* the copy of actEnemyKidnapEnd this disc runs and its numbers there: SINK_A, the
       motion-def rows without a blend row (blendKind), the "no motion" row's number */
    const int jun26 = strcmp(slug, "jun26") == 0;
    const int ntsc = strcmp(slug, "ntsc") == 0;
    const int aug6 = strcmp(slug, "aug6") == 0;
    const int hookFirst = jun26 ? 934 : aug6 ? 901 : ntsc ? 899 : 948;
    const int plainBlend = jun26 ? 207 : (ntsc || aug6) ? 228 : 320;
    const int noMotion = jun26 ? 1165 : aug6 ? 1086 : ntsc ? 1084 : 1145;
    printf("pit_dive_base_test: %s, the %s tables (%u motion rows, %u motion-orient rows)\n", disc,
           slug, (unsigned)ico_tables_count(ICO_TID_motionKind),
           (unsigned)ico_tables_count(ICO_TID_motionOrient));

    /* names: five numbers in a row, the hook's */
    Frames fr;
    memset(&fr, 0, sizeof fr);
    int named = 1;
    for (int k = 0; k < 5; k++) {
        fr.id[k] = motionByName(kSink[k]);
        CHECK(fr.id[k] >= 0, "%s is not in the %s motion table", kSink[k], slug);
        named &= fr.id[k] >= 0;
    }
    if (!named) {
        ico_vfs_unmount(vfs);
        return ico_check_summary("pit_dive_base_test");
    }
    for (int k = 0; k < 5; k++) {
        const MotionDef *d = &motionKind[fr.id[k]];
        printf("pit_dive_base_test: %4d %s: playSpeedRatio %g palSpeedRatio %g blendKind %d "
               "flags 0x%08X\n",
               fr.id[k], kSink[k], (double)d->playSpeedRatio, (double)d->palSpeedRatio,
               d->blendKind, d->flags.word);
        CHECK(fr.id[k] == hookFirst + k, "%s is %d, the hook's %d", kSink[k], fr.id[k],
              hookFirst + k);
        /* speed: the game's own 1, its own frames and speed, the reserve applies */
        CHECK(d->playSpeedRatio == 1.0f && d->palSpeedRatio == 1.0f,
              "%s plays at %g (50 Hz %g), not 1", kSink[k], (double)d->playSpeedRatio,
              (double)d->palSpeedRatio);
        CHECK(d->blendKind == plainBlend, "%s has blend row %d", kSink[k], d->blendKind);
        CHECK(((d->flags.word >> 26) & 1) == 0, "%s has flag bit 26 (the reserve is ignored)",
              kSink[k]);
    }

    /* chain: the carry's request kind leads into SINK_A, then on to E at each last frame */
    const int carry = motionByName("EN1 CARRY_LOOP");
    const int rows = (int)ico_tables_count(ICO_TID_motionOrient);
    int kind = -1;
    CHECK(carry >= 0, "EN1 CARRY_LOOP is not in the %s motion table", slug);
    for (int i = 0; i < rows && carry >= 0; i++) {
        if (motionOrient[i].id == carry && motionOrient[i].nextId == fr.id[0]) {
            kind = motionOrient[i].kind;
            break;
        }
    }
    CHECK(kind >= 0, "no motion-orient row leads EN1 CARRY_LOOP into SINK_A");
    printf("pit_dive_base_test: the dive's request kind %d (CARRY_LOOP %d -> SINK_A)\n", kind,
           carry);
    for (int k = 0; k < 5 && kind >= 0; k++) {
        const MotionOrientEntry *r = orientRow(fr.id[k], kind);
        const int want = k < 4 ? fr.id[k + 1] : noMotion;
        CHECK(r != NULL, "no row for %s and kind %d", kSink[k], kind);
        if (r == NULL) {
            continue;
        }
        printf("pit_dive_base_test:   %s -> %d at frame %d (mode %d)\n", kSink[k], r->nextId,
               r->shiftFrom, r->shiftMode);
        CHECK(r->nextId == want && r->shiftFrom == -1,
              "%s leads to %d from frame %d, not to %d at its last frame", kSink[k], r->nextId,
              r->shiftFrom, want);
    }

    /* frames: every copy in the packs */
    IcoDfIndex *idx = ico_df_index_open(vfs);
    CHECK(idx != NULL, "DATA.DF could not be read");
    if (idx != NULL) {
        readMotionFrames(idx, &fr);
        ico_df_index_close(idx);
    }
    int read = 1;
    for (int k = 0; k < 5; k++) {
        CHECK(fr.copies[k] > 0, "no .mob of %s (%d) in DATA.DF", kSink[k], fr.id[k]);
        read &= fr.copies[k] > 0;
        printf("pit_dive_base_test: %s: %d frames, %d copies\n", kSink[k], fr.frames[k],
               fr.copies[k]);
    }
    CHECK(fr.mismatch == 0, "%d copies differ in their frame count", fr.mismatch);

    /* base: the ticks from SINK_A's first frame to the take */
    if (read) {
        CHECK(fr.frames[4] > ICO_PIT_TAKE_FRAME + 1, "SINK_E has %d frames, the take is at %d",
              fr.frames[4], ICO_PIT_TAKE_FRAME + 1);
        const int ticks = (fr.frames[0] - 1) + (fr.frames[1] - 1) + (fr.frames[2] - 1) +
                          (fr.frames[3] - 1) + ICO_PIT_TAKE_FRAME + 1;
        printf("pit_dive_base_test: the pull-in: %d + %d + %d + %d + %d = %d ticks (%.2f s), the "
               "option's base %d\n",
               fr.frames[0] - 1, fr.frames[1] - 1, fr.frames[2] - 1, fr.frames[3] - 1,
               ICO_PIT_TAKE_FRAME + 1, ticks, (double)ticks / ICO_PIT_TICK_HZ,
               ico_pit_chain_ticks_for(slug));
        CHECK(ticks == ico_pit_chain_ticks_for(slug),
              "the %s pull-in is %d ticks, pit_time.h says %d", slug, ticks,
              ico_pit_chain_ticks_for(slug));
    }
    ico_vfs_unmount(vfs);
    return ico_check_summary("pit_dive_base_test");
}
