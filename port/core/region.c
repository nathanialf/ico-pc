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
    /* memoryPAL (58) and memoryPAL576 (59): the European table's only rows
       the Japanese one lacks (the Korean disc's stageData is the Japanese
       one) */
    if ((ico_region == ICO_REGION_JPN || ico_region == ICO_REGION_KOR) && pal_stage >= 58) {
        return pal_stage - 2;
    }
    return pal_stage;
}
