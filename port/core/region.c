/*
 * port/core/region.c
 *
 * The selected region (region.h): the game's index ico_region and the
 * look-ups over the table of every row (region_table.c, generated). Plain
 * data: no log (the program's select_disc logs the disc it plays).
 */
#include "region.h"

#include <stdio.h>
#include <string.h>

int ico_region = 0;

/* ico_region_info[] is region_table.c's, generated from config/regions.txt
   (tools/region_blocks.py gen-table; region_select checks the order). */

const IcoRegionInfo *ico_region_cur(void)
{
    return ico_region_info[ico_region];
}

int ico_region_new_game_flag(const char **disc_id)
{
    const IcoRegionInfo *r = ico_region_cur();
    if (disc_id != NULL) {
        *disc_id = r->disc_id;
    }
    return r->gflag_new_game;
}

int ico_region_count(void)
{
    return ICO_REGION_COUNT;
}

const IcoRegionInfo *ico_region_at(int i)
{
    return i >= 0 && i < ICO_REGION_COUNT ? ico_region_info[i] : NULL;
}

static int slug_row(const char *slug)
{
    int i;

    for (i = 0; slug != NULL && i < ICO_REGION_COUNT; i++) {
        if (strcmp(ico_region_info[i]->slug, slug) == 0) {
            return i;
        }
    }
    return -1;
}

const IcoRegionInfo *ico_region_by_slug(const char *slug)
{
    return ico_region_at(slug_row(slug));
}

const IcoRegionInfo *ico_region_by_disc_id(const char *disc_id)
{
    int i;

    for (i = 0; disc_id != NULL && i < ICO_REGION_COUNT; i++) {
        if (strcmp(ico_region_info[i]->disc_id, disc_id) == 0) {
            return ico_region_info[i];
        }
    }
    return NULL;
}

int ico_region_select(const char *slug)
{
    int i = slug_row(slug);

    if (i < 0) {
        return -1;
    }
    ico_region = i;
    return 0;
}

int ico_region_key_prefixed(const char *section, const char *base, char *out, int size)
{
    int n = snprintf(out, (size_t)size, "%s%s%s%s", section != NULL ? section : "",
                     section != NULL && section[0] != '\0' ? "." : "", ico_region_cur()->key_prefix,
                     base);

    return n >= 0 && n < size ? 0 : -1;
}

int ico_region_key_suffixed(const char *base, char *out, int size)
{
    int n = snprintf(out, (size_t)size, "%s%s", base, ico_region_cur()->key_suffix);

    return n >= 0 && n < size ? 0 : -1;
}

int ico_region_stage(int pal_stage)
{
    if (ico_region == ICO_REGION_PAL || pal_stage < 0) {
        return pal_stage;
    }
    if (ico_region == ICO_REGION_JUN26) {
        /* the June 2001 demo's own order (its stageData, 103 rows): rows
           0..49 (03t4demo) as PAL; then its 04a4demo (50), which PAL has
           not, so 04b4demo .. memory (PAL 50..57) are one higher; no
           memoryPAL, memoryPAL576 (58, 59) or staff3 (62); staff1, staff2
           (PAL 60, 61) at 59, 60, end (63) at 61, temp .. itou_boss (64..86)
           two lower; no itou_st03t (87); boss_test .. the E3 rows (88..105)
           three lower */
        if (pal_stage <= 49) {
            return pal_stage;
        }
        if (pal_stage <= 57) {
            return pal_stage + 1;
        }
        if (pal_stage == 60 || pal_stage == 61) {
            return pal_stage - 1;
        }
        if (pal_stage == 63) {
            return 61;
        }
        if (pal_stage >= 64 && pal_stage <= 86) {
            return pal_stage - 2;
        }
        if (pal_stage >= 88 && pal_stage <= 105) {
            return pal_stage - 3;
        }
        return -1; /* 58, 59, 62, 87: no row on the demo */
    }
    /* the Japanese, Korean and Asian discs, the North American disc and the
       August 2001 preview: the 104-row stageData without memoryPAL (58) and
       memoryPAL576 (59), the European table's only rows they lack, so the
       rows from staff1 on are two lower */
    if (pal_stage == 58 || pal_stage == 59) {
        return -1;
    }
    return pal_stage >= 60 ? pal_stage - 2 : pal_stage;
}
