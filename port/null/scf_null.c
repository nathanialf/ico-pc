/*
 * port/null/scf_null.c
 *
 * libscf's system configuration with fixed answers.  The language is a
 * variable so the config layer can set it from the system locale later
 * (plan, "Boot"); common/src/kanbanBoot.c maps it to the boot language
 * screen's default item.
 */
#include "null_devices.h"

#include <libscf.h>

int ico_scf_language = ICO_SCF_LANGUAGE_ENGLISH;

int sceScfGetLanguage(void)
{
    return ico_scf_language;
}

/* minutes east of GMT */
int sceScfGetTimeZone(void)
{
    return 0;
}

int sceScfGetSummerTime(void)
{
    return 0;
}
