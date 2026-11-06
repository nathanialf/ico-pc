/*
 * port/game/test/weapon_kinds_disc_test.c
 *
 * Gameplay > Combat's weapon rows (port/game/variants.h: Light sword colour,
 * Stick) read off a disc image: the disc's boot ELF fills the data tables
 * (port/data/tables.c, the first descriptor set whose checksums it matches),
 * then, with the rows at Red and Insect net, the kinds the hook gives
 * (ico_variant_weapon_kind, which every disc's copy of weapon.c InitWeaponGeo
 * reads) are checked against the disc's own rows:
 *
 *   rows     weaponKind has 10 rows on the European, Japanese, Korean and
 *            Asian discs, 8 on the North American and August 2001 discs, 7
 *            on the June 2001 demo
 *   net      the stick's kind 1 becomes ico_variant_net_kind(), a row of
 *            this disc's table (7; 6 on the demo, the case WeaponDL_jun26
 *            draws dispInsectNet at): length 120, grip 40, power 0 (a hit
 *            does attackData's power times 0: no harm), weight 1500 (the
 *            stick's), no flags (no unguardable, no sweep), the stick's
 *            blur mode and colour, motion-orient request 0
 *   no swing the motion-orient table has no row of request 0, the kind
 *            commonact.c's ACTGetOrientFromIntrK gives an armed boy's swing
 *            (ACTGame_GetMotOrientFromWeapon): GetMotionOrient finds none and
 *            getMotionOrient answers the table's last row, the "no motion"
 *            row that leads to itself, so the swing never starts; the stick's
 *            request (its row's) has rows (it swings)
 *   red      where the light sword exists, kind 9 becomes 8, and rows 8 and
 *            9 are the same bytes but the trail's colour: 8's (255, 32, 32,
 *            80), 9's (215, 255, 248, 80); elsewhere Red is not offered and
 *            kind 9 is left alone
 *   placed   objLayout's weapon objects (kind 14; the layout's initArg is
 *            the weapon kind, sceneManager.c initSceneGObj's a.obj): the
 *            stick's kind 1 is placed (the rows the net replaces) and the net
 *            kind never is (an unused weapon); on the four discs with the
 *            light sword, kind 9 is placed
 *   others   every other kind comes back from the hook unchanged
 *
 * Nothing starts the game; nothing read is written.  Skipped (77) without a
 * readable disc image, as in a worktree.  The draw and the hold are the CPU
 * test weapon_draw_test's.
 *
 * Usage: weapon_kinds_disc_test <disc image> <work folder>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "typedef.h"
#include "motionOrientManager.h"
#include "weapon.h"

#include "../../core/region.h"
#include "config.h"
#include "tables.h"
#include "variants.h"
#include "vfs.h"
#include "../../test/ico_check.h"

/* port/data/gen/table_defs.c's writable definitions */
extern WeaponDef weaponKind[];
extern MotionOrientEntry motionOrient[];
extern GenGeo objLayout[];

/* the stick, the light sword's two kinds, the weapon object kind */
#define WK_STICK 1
#define WK_LIGHT_RED 8
#define WK_LIGHT 9
#define OBJ_WEAPON 14

static int rowsOfRequest(int kind)
{
    const int n = (int)ico_tables_count(ICO_TID_motionOrient);
    int c = 0;

    for (int i = 0; i < n; i++) {
        c += motionOrient[i].kind == kind;
    }
    return c;
}

/* objLayout's weapon objects of weapon kind k */
static int placed(int k)
{
    const int n = (int)ico_tables_count(ICO_TID_objLayout);
    int c = 0;

    for (int i = 0; i < n; i++) {
        c += objLayout[i].kind == OBJ_WEAPON && (objLayout[i].initArg & 0xFF) == k;
    }
    return c;
}

static void printRow(const char *what, int k)
{
    const WeaponDef *w = &weaponKind[k];
    printf("weapon_kinds_disc_test:   %s kind %d: length %g grip %g power %g weight %d blur %d "
           "colour (%u, %u, %u, %u) request %d flags 0x%X\n",
           what, k, (double)w->length, (double)w->grip, (double)w->power, w->weight, w->blur,
           w->color[0], w->color[1], w->color[2], w->color[3], w->motOrient, w->flags);
}

int main(int argc, char **argv)
{
    const char *disc = argc > 1 ? argv[1] : NULL;
    const char *dir = argc > 2 ? argv[2] : ".";
    FILE *probe = disc != NULL && disc[0] != '\0' ? fopen(disc, "rb") : NULL;
    char err[256], cfg[512];
    const IcoTableSet *set = NULL;

    if (probe == NULL) {
        printf("weapon_kinds_disc_test: SKIP (no disc image%s%s)\n", disc ? " at " : "",
               disc ? disc : "");
        return 77;
    }
    fclose(probe);
    IcoVfs *vfs = ico_vfs_mount(&ico_vfs_iso9660, disc);
    if (vfs == NULL) {
        printf("weapon_kinds_disc_test: SKIP (%s is not a readable disc image)\n", disc);
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
        return ico_check_summary("weapon_kinds_disc_test");
    }
    const char *slug = set->slug;
    const int jun26 = strcmp(slug, "jun26") == 0;
    const int y2001 = jun26 || strcmp(slug, "ntsc") == 0 || strcmp(slug, "aug6") == 0;
    const int rows = (int)ico_tables_count(ICO_TID_weaponKind);
    printf("weapon_kinds_disc_test: %s, the %s tables (%d weapon rows, %u motion-orient rows, "
           "%u layout rows)\n",
           disc, slug, rows, (unsigned)ico_tables_count(ICO_TID_motionOrient),
           (unsigned)ico_tables_count(ICO_TID_objLayout));

    /* the variants with no config file of the user's: a file in the work
       folder, one per disc (ctest runs the discs in parallel) */
    snprintf(cfg, sizeof cfg, "%s/weapon_kinds_disc_%s.toml", dir, slug);
    remove(cfg);
    ico_config_reset(cfg, "/nonexistent/weapon_kinds_disc_test.ini");
    ico_variants_reload();

    /* rows */
    CHECK(rows == (jun26 ? 7 : y2001 ? 8 : 10), "%s: %d weapon rows", slug, rows);

    /* net */
    ico_variant_set(ICO_VARIANT_WEAPON, ICO_WEAPON_NET);
    const int net = ico_variant_weapon_kind(WK_STICK);
    CHECK(net == ico_variant_net_kind() && net == (jun26 ? 6 : 7) && net < rows,
          "%s: the stick is made as kind %d, a row of the table (%d rows)", slug, net, rows);
    if (net >= 0 && net < rows) {
        const WeaponDef *n = &weaponKind[net];
        const WeaponDef *s = &weaponKind[WK_STICK];
        printRow("the insect net", net);
        printRow("the stick", WK_STICK);
        CHECK(n->length == 120.0f && n->grip == 40.0f && n->power == 0.0f,
              "%s: the net's row is length 120, grip 40, power 0", slug);
        CHECK(n->weight == s->weight && n->blur == s->blur &&
                  memcmp(n->color, s->color, sizeof n->color) == 0,
              "%s: the net weighs and trails as the stick", slug);
        CHECK(n->flags == 0 && n->motOrient == 0,
              "%s: the net is guardable, does not sweep, and asks for request 0 (%d)", slug,
              n->motOrient);
        CHECK(s->power > 0.0f && s->motOrient > 0, "%s: the stick hurts and swings", slug);

        /* no swing */
        const int none = rowsOfRequest(n->motOrient);
        const int stick = rowsOfRequest(s->motOrient);
        const int last = (int)ico_tables_count(ICO_TID_motionOrient) - 1;
        printf("weapon_kinds_disc_test:   motion-orient rows of request %d (the net's): %d; of "
               "request %d (the stick's): %d; the last row: id %d request %d next %d\n",
               n->motOrient, none, s->motOrient, stick, motionOrient[last].id,
               motionOrient[last].kind, motionOrient[last].nextId);
        CHECK(none == 0, "%s: %d motion-orient rows answer the net's swing", slug, none);
        CHECK(stick > 0, "%s: the stick's swing has no rows", slug);
        CHECK(last > 0 && motionOrient[last].kind == -1 &&
                  motionOrient[last].nextId == motionOrient[last].id,
              "%s: the table's last row is the \"no motion\" row that leads to itself", slug);
    }
    /* the net's hook leaves every other kind alone */
    for (int k = 0; k < 16; k++) {
        if (k != WK_STICK) {
            CHECK(ico_variant_weapon_kind(k) == k, "%s: net: kind %d moved", slug, k);
        }
    }
    ico_variant_set(ICO_VARIANT_WEAPON, ICO_WEAPON_ORIGINAL);
    CHECK(ico_variant_weapon_kind(WK_STICK) == WK_STICK, "%s: Original: the stick", slug);

    /* red */
    CHECK(ico_variant_offered(ICO_VARIANT_SWORD_COLOUR, ICO_SWORD_RED) == !y2001,
          "%s: Red offered %d", slug, !y2001);
    ico_variant_set(ICO_VARIANT_SWORD_COLOUR, ICO_SWORD_RED);
    if (!y2001) {
        CHECK(ico_variant_weapon_kind(WK_LIGHT) == WK_LIGHT_RED, "%s: kind 9 is made as 8", slug);
        CHECK(ico_variant_weapon_put_kind(WK_LIGHT_RED) == WK_LIGHT,
              "%s: the red sword is set down as kind 9", slug);
        if (rows > WK_LIGHT) {
            WeaponDef a = weaponKind[WK_LIGHT_RED];
            WeaponDef b = weaponKind[WK_LIGHT];
            static const unsigned char kRed[4] = {255, 32, 32, 80};
            static const unsigned char kPale[4] = {215, 255, 248, 80};
            printRow("the red sword", WK_LIGHT_RED);
            printRow("the light sword", WK_LIGHT);
            CHECK(memcmp(a.color, kRed, 4) == 0 && memcmp(b.color, kPale, 4) == 0,
                  "%s: the trails are red and pale", slug);
            memset(a.color, 0, 4);
            memset(b.color, 0, 4);
            CHECK(memcmp(&a, &b, sizeof a) == 0, "%s: rows 8 and 9 differ beyond the colour", slug);
        }
    } else {
        CHECK(ico_variant_get(ICO_VARIANT_SWORD_COLOUR) == ICO_SWORD_ORIGINAL &&
                  ico_variant_weapon_kind(WK_LIGHT) == WK_LIGHT,
              "%s: Red is not offered: kind 9 left alone", slug);
    }
    for (int k = 0; k < 16; k++) {
        if (k != WK_LIGHT) {
            CHECK(ico_variant_weapon_kind(k) == k, "%s: red: kind %d moved", slug, k);
        }
    }
    ico_variant_set(ICO_VARIANT_SWORD_COLOUR, ICO_SWORD_ORIGINAL);

    /* placed */
    printf("weapon_kinds_disc_test:   placed: %d sticks (kind 1), %d of kind %d (the net), %d of "
           "kind 8, %d of kind 9\n",
           placed(WK_STICK), placed(net), net, placed(WK_LIGHT_RED), placed(WK_LIGHT));
    CHECK(placed(WK_STICK) > 0, "%s: no stick is placed", slug);
    CHECK(placed(net) == 0, "%s: the net kind %d is placed %d times", slug, net, placed(net));
    if (!y2001) {
        CHECK(placed(WK_LIGHT) > 0, "%s: the light sword is not placed", slug);
    }

    remove(cfg);
    return ico_check_summary("weapon_kinds_disc_test");
}
