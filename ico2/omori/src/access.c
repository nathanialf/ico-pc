#include "typedef.h"
#include <stdio.h>
#include "access.h"

/* the path sprintf builds and this file hands back (the European size; the Japanese
   disc's is 512) */
static char accessPath_pal[528]; /* derived name */

static char accessPath_jun26[512]; /* derived name */

#define accessPath PAL_JUN26(accessPath_pal, accessPath_jun26)

/* Callers pass the stage and the DF flag (StageManager.c, icoMisc.c), which
   goes straight through to GetDataFileName2.  The DEBUG build reads the data
   from the host instead and builds that path in buf; retail keeps only the
   declaration. */
char *GetDataFileName(int no, int isDF)
{
    char buf[256];
    const char *name;

    if (no == -1) {
        name = "COMMON";
    } else {
        name = stageData[no].dataFile;
    }
#ifndef DEBUG
    return GetDataFileName2(name, isDF);
#else
    sprintf(buf, "host0:%s", GetDataFileName2(name, isDF));
    sprintf(accessPath, "%s", buf);
    return accessPath;
#endif
}

char *GetDataFileName2(const char *name, int isDF)
{
    char dir[128];
    char ext[128];

    if (isDF) {
        sprintf(dir, "DFDATAS");
        sprintf(ext, "DF");
    } else {
        sprintf(dir, "DATAS");
        sprintf(ext, "BIN");
    }

    sprintf(accessPath, "%s/%s.%s", dir, name, ext);

    return accessPath;
}
