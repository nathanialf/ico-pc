/*
 * tools/mc_import.c
 *
 * mc_import [--overwrite] --to SAVES FILE...
 *
 * Imports ICO's saves (BESCES-50760ico, the European disc's, and
 * BISCPS-11003ico, the Japanese disc's) from each PS2 memory card image
 * (.ps2, .bin) or .psu archive into the card folder SAVES, the folder
 * [paths] saves (or saves2) names (port/save/mc_import.c). Other games' entries are listed and left alone.
 * Exit 0 when every file gave a save, 1 when one held none, 2 on an error.
 */
#include "mc_import.h"
#include "region.h" /* port/core: each disc's mc_save_dir */
#include <stdio.h>
#include <string.h>

static const char *const fmtName[] = {"unknown", "raw card image", ".psu", ".max", ".cbs"};

static int usage(void)
{
    fprintf(stderr, "usage: mc_import [--overwrite] --to SAVES_FOLDER FILE...\n"
                    "  FILE: a PS2 memory card image (.ps2, .bin) or a .psu save archive\n"
                    "  SAVES_FOLDER: the card folder ([paths] saves or saves2)\n");
    return 2;
}

int main(int argc, char **argv)
{
    const char *to = NULL;
    unsigned flags = 0;
    int worst = 0;
    int nfiles = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--overwrite") == 0) {
            flags |= ICO_MC_IMPORT_OVERWRITE;
        } else if (strcmp(argv[i], "--to") == 0 && i + 1 < argc) {
            to = argv[++i];
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            return usage();
        } else {
            nfiles++;
        }
    }
    if (to == NULL || nfiles == 0) {
        return usage();
    }
    for (i = 1; i < argc; i++) {
        IcoMcImport res;
        int r;
        int k;

        if (strcmp(argv[i], "--to") == 0) {
            i++;
            continue;
        }
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            continue;
        }
        r = ico_mc_import(argv[i], to, flags, &res);
        printf("%s (%s): ", argv[i], fmtName[res.format]);
        if (r == 0) {
            int k;

            printf("imported %d file(s) into %s:", res.files, to);
            for (k = 0; k < ico_region_count(); k++) {
                if (res.regions & (1u << k)) {
                    printf(" %s", ico_region_at(k)->mc_save_dir);
                }
            }
            printf("\n");
        } else {
            printf("%s\n", res.why);
        }
        for (k = 0; k < res.skipped && k < ICO_MC_IMPORT_LIST; k++) {
            printf("  skipped: %s\n", res.skippedName[k]);
        }
        if (res.skipped > ICO_MC_IMPORT_LIST) {
            printf("  skipped: %d more\n", res.skipped - ICO_MC_IMPORT_LIST);
        }
        if (r < 0) {
            worst = 2;
        } else if (r == 1 && worst == 0) {
            worst = 1;
        }
    }
    return worst;
}
