/*
 * port/save/mc_import.h
 *
 * Imports ICO's save from a PS2 memory card image or a save archive into a
 * card folder (mc_host.c's layout: <saves>/BESCES-50760ico/...).
 * docs/port/SAVES.md, "Importing saves".
 */
#ifndef ICO_SAVE_MC_IMPORT_H
#define ICO_SAVE_MC_IMPORT_H

#include <stddef.h>

/* the game's save directory on the card (fumi/ios/mcard.c's product name) */
#define ICO_MC_SAVE_DIR "BESCES-50760ico"

enum {
    ICO_MC_FMT_UNKNOWN = 0,
    ICO_MC_FMT_RAW, /* .ps2/.bin: a raw card image, with or without ECC spares */
    ICO_MC_FMT_PSU, /* .psu: uLaunchELF/mymc export */
    ICO_MC_FMT_MAX, /* .max: Action Replay MAX (recognised, not supported) */
    ICO_MC_FMT_CBS  /* .cbs: CodeBreaker (recognised, not supported) */
};

/* replace files of the same name already in the card folder */
#define ICO_MC_IMPORT_OVERWRITE 1u

#define ICO_MC_IMPORT_LIST 32

typedef struct IcoMcImport {
    int format;  /* ICO_MC_FMT_* */
    int files;   /* files written into <saves>/BESCES-50760ico */
    int skipped; /* other entries in the source, not imported */
    /* the first ICO_MC_IMPORT_LIST of them, by name */
    char skippedName[ICO_MC_IMPORT_LIST][64]; /* a subdirectory as DIR/NAME */
    char why[256]; /* the reason, when the import failed or found nothing */
} IcoMcImport;

/* The format of the file at path by its contents (ICO_MC_FMT_*). */
int ico_mc_import_detect(const char *path);

/* Reads the image or archive at src and writes ICO's save directory into
   the card folder saves (created if missing). Nothing is written unless the
   whole save was read; existing files of the same name are refused unless
   ICO_MC_IMPORT_OVERWRITE. 0 imported, 1 the source holds no ICO save (the
   other entries are in res), -1 an error (res->why). */
int ico_mc_import(const char *src, const char *saves, unsigned flags, IcoMcImport *res);

#endif /* ICO_SAVE_MC_IMPORT_H */
