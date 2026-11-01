/*
 * port/game/test/yorda_trip_disc_test.c
 *
 * Yorda trips' motions on the disc image the build names (ICO_DISC_IMAGE):
 * the disc's boot ELF fills the motion and stage tables (port/data/tables.c,
 * the first descriptor set whose checksums it matches), then
 *
 *   names    the run loop, the fall's three motions and her per-stage
 *            HETARI motions are found by name (yorda_trip_source.c
 *            ico_yorda_trip_find_ids), each printed with its number, and
 *            the motions the fall leads on to (its motion-orient rows)
 *   gates    each stage named "st04a ..." (the main gate's): its pack
 *            (stageData[].dataFile + ".DF") and the members of it whose ids
 *            are those motions, with their sizes
 *   source   the pack the game's side reads them from (the first holding
 *            the fall as a motion file) and each copy's size; on a disc
 *            where a pack holds them, every motion of the fall is read and
 *            none is empty
 *
 * Nothing starts the game; nothing read is written.  Skipped (77) without
 * a readable disc image, as in a worktree.
 *
 * Usage: yorda_trip_disc_test <disc image>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define stageData stageData_header_decl
#include "typedef.h"
#include "motionOrientManager.h"
#undef stageData

#include "../../core/region.h"
#include "df_pack.h"
#include "tables.h"
#include "vfs.h"
#include "yorda_trip.h"
#include "../../test/ico_check.h"

/* port/data/gen/table_defs.c's writable definitions */
extern StgPre stageData[];
extern MotionDef motionKind[];

static int isWanted(const IcoYtIds *ids, int id)
{
    for (int k = 0; k < ids->n; k++) {
        if (ids->id[k] == id) {
            return 1;
        }
    }
    return 0;
}

static void listGates(IcoDfIndex *idx, const IcoYtIds *ids)
{
    const int rows = ico_region_cur()->stage_count;

    for (int s = 0; s < rows; s++) {
        if (strncmp(stageData[s].name, "st04a ", 6) != 0) {
            continue;
        }
        char pack[48];
        int first = 0;
        snprintf(pack, sizeof pack, "%.32s.DF", stageData[s].dataFile);
        const int e = ico_df_index_entry_find(idx, pack);
        const int n = e >= 0 ? ico_df_index_pack_members(idx, e, &first) : 0;
        int found = 0;
        unsigned bytes = 0;
        printf("yorda_trip_disc_test: stage %d %s: pack %s (%s, %d members)\n", s,
               stageData[s].name, pack, e >= 0 ? "on the disc" : "not on the disc", n);
        for (int i = first; i < first + n; i++) {
            IcoDfMember m;
            if (ico_df_index_member_at(idx, i, &m) == 0 && isWanted(ids, m.id)) {
                printf("yorda_trip_disc_test:   %4d %-34s %7u bytes  %s\n", m.id,
                       motionKind[m.id].name, (unsigned)m.size, ico_df_index_member_name(idx, i));
                found++;
                bytes += m.size;
            }
        }
        printf("yorda_trip_disc_test:   %d of her %d motions, %u bytes\n", found, ids->n, bytes);
    }
}

int main(int argc, char **argv)
{
    const char *disc = argc > 1 ? argv[1] : NULL;
    FILE *probe = disc != NULL && disc[0] != '\0' ? fopen(disc, "rb") : NULL;
    char err[256];
    const IcoTableSet *set = NULL;

    if (probe == NULL) {
        printf("yorda_trip_disc_test: SKIP (no disc image%s%s)\n", disc ? " at " : "",
               disc ? disc : "");
        return 77;
    }
    fclose(probe);
    IcoVfs *vfs = ico_vfs_mount(&ico_vfs_iso9660, disc);
    if (vfs == NULL) {
        printf("yorda_trip_disc_test: SKIP (%s is not a readable disc image)\n", disc);
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
        return ico_check_summary("yorda_trip_disc_test");
    }
    printf("yorda_trip_disc_test: %s, the %s tables (%u motion rows)\n", disc, set->slug,
           (unsigned)ico_tables_count(ICO_TID_motionKind));

    IcoYtIds ids;
    const int named = ico_yorda_trip_find_ids(&ids, ico_region_cur()->mv_motion_kinds);
    CHECK(named == 0, "the run loop or the fall is not named in the %s table", set->slug);
    printf("yorda_trip_disc_test: run loop %d, the fall %d %d %d, %d per-stage HETARI motions:\n",
           ids.run_loop, ids.sit, ids.sit_loop, ids.sit_end, ids.n);
    for (int k = 0; k < ids.n; k++) {
        printf("yorda_trip_disc_test:   %4d %s\n", ids.id[k], motionKind[ids.id[k]].name);
    }
    printf("yorda_trip_disc_test: the fall leads on to %d motions:\n", ids.n_exit);
    for (int k = 0; k < ids.n_exit; k++) {
        printf("yorda_trip_disc_test:   %4d %s (area %d)\n", ids.exit[k],
               motionKind[ids.exit[k]].name, motionKind[ids.exit[k]].area);
    }
    CHECK(ids.n_exit > 0, "the fall's motion-orient rows lead nowhere");

    IcoDfIndex *idx = ico_df_index_open(vfs);
    CHECK(idx != NULL, "DATA.DF could not be read");
    if (idx != NULL && named == 0) {
        listGates(idx, &ids);

        IcoYtSource src;
        char why[160];
        const int rc = ico_yorda_trip_source_read(idx, &ids, &src, why, sizeof why);
        printf("yorda_trip_disc_test: source: %s\n", why);
        CHECK(rc >= 0, "the read failed: %s", why);
        if (rc == 0) {
            int sit = 0, loop = 0, end = 0;
            for (int k = 0; k < src.n; k++) {
                printf("yorda_trip_disc_test:   %4d %-34s %7u bytes  %s\n", src.m[k].id,
                       motionKind[src.m[k].id].name, (unsigned)src.m[k].size, src.m[k].name);
                CHECK(src.m[k].size > 0 && src.m[k].data != NULL, "motion %d is empty",
                      src.m[k].id);
                sit |= src.m[k].id == ids.sit;
                loop |= src.m[k].id == ids.sit_loop;
                end |= src.m[k].id == ids.sit_end;
            }
            CHECK(sit && loop && end, "%s lacks part of the fall (%d %d %d)", src.pack, sit, loop,
                  end);
            printf("yorda_trip_disc_test: %d motions, %u bytes (%u KB) from %s\n", src.n,
                   (unsigned)src.bytes, (unsigned)(src.bytes + 1023) / 1024, src.pack);
            ico_yorda_trip_source_free(&src);
        }
    }
    ico_df_index_close(idx);
    ico_vfs_unmount(vfs);
    return ico_check_summary("yorda_trip_disc_test");
}
