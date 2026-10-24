/*
 * port/data/test/df_index_test.c
 *
 * df_pack.h's index of its own (IcoDfIndex) against the global one, on the
 * user's disc:
 *
 *   df_index_test DISC
 *
 * DISC is mounted twice: the global calls read one mount, an IcoDfIndex
 * the other.  They must agree on the directory (names, sizes, a loose
 * file's bytes), the members (count, names, rows) and a member's bytes;
 * the global index must still answer after the instance was used and
 * closed; ico_df_index_read_pack over a pack must hand the same bytes as
 * one ico_df_read_member per member, in order, and stop when its callback
 * says so.  Exits 77 (skipped) when the image cannot be opened.  Nothing
 * of the disc is printed but names and counts.
 */
#include "df_pack.h"
#include "vfs.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SKIP 77

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

typedef struct PackRun {
    IcoVfs *vfs; /* the global index's mount */
    int calls, stopAfter, nextMember, mismatches;
} PackRun;

/* every member handed is compared with the global read of the same row */
static int onMember(void *user, int member, const char *name, const IcoDfMember *m,
                    const uint8_t *data)
{
    PackRun *r = user;
    IcoDfMember g;
    r->calls++;
    if (member != r->nextMember || ico_df_member(member, &g) != 0 ||
        strcmp(name, ico_df_member_name(member)) != 0 || g.off != m->off || g.size != m->size ||
        g.pack != m->pack) {
        r->mismatches++;
    } else {
        uint8_t *d = malloc(g.size ? g.size : 1);
        if (d == NULL || ico_df_read_member(r->vfs, &g, d) != 0 ||
            (g.size != 0 && memcmp(d, data, g.size) != 0)) {
            r->mismatches++;
        }
        free(d);
    }
    r->nextMember = member + 1;
    return r->stopAfter > 0 && r->calls >= r->stopAfter;
}

int main(int argc, char **argv)
{
    const char *disc = argc > 1 ? argv[1] : NULL;
    FILE *probe = disc != NULL && disc[0] != '\0' ? fopen(disc, "rb") : NULL;
    if (probe == NULL) {
        printf("df_index_test: SKIP (no disc image%s%s)\n", disc ? " at " : "", disc ? disc : "");
        return SKIP;
    }
    fclose(probe);
    IcoVfs *a = ico_vfs_mount(&ico_vfs_iso9660, disc);
    IcoVfs *b = a != NULL ? ico_vfs_mount(&ico_vfs_iso9660, disc) : NULL;
    if (a == NULL || b == NULL) {
        printf("df_index_test: SKIP (%s is not a readable disc image)\n", disc);
        ico_vfs_unmount(a);
        return SKIP;
    }

    /* the global index, built by a look-up */
    IcoDfMember m;
    (void)ico_df_find_member(a, "", &m);
    int members = ico_df_index_members();
    int packs = ico_df_index_packs();
    CHECK(members > 0 && packs > 0);

    IcoDfIndex *idx = ico_df_index_open(b);
    CHECK(idx != NULL);
    if (idx == NULL) {
        ico_vfs_unmount(b);
        ico_vfs_unmount(a);
        return 1;
    }
    CHECK(ico_df_index_member_count(idx) == members);
    CHECK(ico_df_index_pack_count(idx) == packs);

    /* the directory */
    int entries = ico_df_index_entries(idx);
    int loose = -1;
    int smallPack = -1;
    CHECK(entries > 0);
    for (int i = 0; i < entries; i++) {
        const char *g = ico_df_entry_name(a, i);
        const char *n = ico_df_index_entry_name(idx, i);
        CHECK(g != NULL && n != NULL && strcmp(g, n) == 0);
        if (g == NULL || n == NULL) {
            continue;
        }
        CHECK(ico_df_size(a, g) == ico_df_index_entry_size(idx, i));
        CHECK(ico_df_index_entry_find(idx, g) <= i && ico_df_index_entry_find(idx, g) >= 0);
        int first = 0;
        int count = ico_df_index_pack_members(idx, i, &first);
        if (count == 0 && loose < 0 && ico_df_index_entry_size(idx, i) > 0) {
            loose = i;
        }
        if (count >= 3 && count <= 40 && smallPack < 0) {
            smallPack = i;
        }
    }
    CHECK(ico_df_entry_name(a, entries) == NULL && ico_df_index_entry_name(idx, entries) == NULL);
    CHECK(ico_df_index_entry_find(idx, "no such entry") == -1);

    /* a loose file's first bytes, both ways */
    CHECK(loose >= 0);
    if (loose >= 0) {
        uint8_t x[4096];
        uint8_t y[4096];
        memset(x, 0, sizeof(x));
        memset(y, 0, sizeof(y));
        const char *name = ico_df_index_entry_name(idx, loose);
        int64_t nx = ico_df_read(a, name, 0, x, sizeof(x));
        int64_t ny = ico_df_index_read_entry(idx, loose, 0, y, sizeof(y));
        CHECK(nx > 0 && nx == ny && memcmp(x, y, (size_t)nx) == 0);
        CHECK(ico_df_index_read_entry(idx, loose, (uint64_t)ico_df_index_entry_size(idx, loose), y,
                                      sizeof(y)) == 0);
        CHECK(ico_df_index_read_pack(idx, loose, onMember, NULL) == -1);
    }

    /* the members' names and rows */
    int rowsDiffer = 0;
    for (int i = 0; i < members; i++) {
        IcoDfMember g;
        IcoDfMember n;
        const char *gn = ico_df_member_name(i);
        const char *nn = ico_df_index_member_name(idx, i);
        if (gn == NULL || nn == NULL || strcmp(gn, nn) != 0 || ico_df_member(i, &g) != 0 ||
            ico_df_index_member_at(idx, i, &n) != 0 || g.pack != n.pack || g.off != n.off ||
            g.size != n.size || g.id != n.id || g.kind != n.kind) {
            rowsDiffer++;
        }
    }
    CHECK(rowsDiffer == 0);
    CHECK(ico_df_index_member_name(idx, members) == NULL);
    CHECK(ico_df_index_member_at(idx, -1, &m) == -1);

    /* one member's bytes: the last member of the first pack with a few */
    CHECK(smallPack >= 0);
    if (smallPack >= 0) {
        int first = 0;
        int count = ico_df_index_pack_members(idx, smallPack, &first);
        IcoDfMember g;
        IcoDfMember n;
        CHECK(ico_df_member(first + count - 1, &g) == 0);
        CHECK(ico_df_index_find(idx, ico_df_member_name(first + count - 1), &n) == 0);
        uint8_t *x = malloc(g.size ? g.size : 1);
        uint8_t *y = malloc(g.size ? g.size : 1);
        CHECK(x != NULL && y != NULL);
        if (x != NULL && y != NULL) {
            CHECK(ico_df_read_member(a, &g, x) == 0);
            CHECK(ico_df_index_read_member(idx, &g, y) == 0);
            CHECK(memcmp(x, y, g.size) == 0);
        }
        free(x);
        free(y);

        /* the whole pack in one pass, then a stop after two members */
        PackRun r;
        memset(&r, 0, sizeof(r));
        r.vfs = a;
        r.nextMember = first;
        CHECK(ico_df_index_read_pack(idx, smallPack, onMember, &r) == 0);
        CHECK(r.calls == count && r.mismatches == 0);
        memset(&r, 0, sizeof(r));
        r.vfs = a;
        r.nextMember = first;
        r.stopAfter = 2;
        CHECK(ico_df_index_read_pack(idx, smallPack, onMember, &r) == 1);
        CHECK(r.calls == 2 && r.mismatches == 0);
        printf("df_index_test: pack %s, %d members read in one pass\n",
               ico_df_index_entry_name(idx, smallPack), count);
    }
    CHECK(ico_df_index_read_pack(idx, entries, onMember, NULL) == -1);

    /* the global index is untouched by the instance */
    ico_df_index_close(idx);
    CHECK(ico_df_index_members() == members && ico_df_index_packs() == packs);
    CHECK(ico_df_find_member(a, ico_df_member_name(members - 1), &m) == 0);
    CHECK(ico_df_entry_name(a, 0) != NULL);
    ico_df_index_close(NULL);

    printf("df_index_test: %d entries, %d packs, %d members\n", entries, packs, members);
    ico_vfs_unmount(b);
    ico_vfs_unmount(a);
    if (failures) {
        printf("df_index_test: %d failures\n", failures);
        return 1;
    }
    printf("df_index_test: ok\n");
    return 0;
}
