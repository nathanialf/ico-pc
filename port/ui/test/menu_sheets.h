/*
 * port/ui/test/menu_sheets.h
 *
 * The game's menu sheets, decoded, for the menu_look test (menu_sheets.c).
 */
#ifndef PORT_UI_TEST_MENU_SHEETS_H
#define PORT_UI_TEST_MENU_SHEETS_H

#include <stddef.h>
#include <stdint.h>

#include "vfs.h"

typedef struct MsSheet {
    char name[64];
    uint8_t *rgba; /* w x h texels: grey (r = g = b) and alpha 0..255 (the GS's 0x80 is 255) */
    int w, h;
} MsSheet;

/* The sheet `name` (a member of a pack on the disc, "text/menu_PAL_EG/menu_PAL_01.tm2"),
   decoded once and kept; NULL when it is not there or does not decode. */
const MsSheet *ms_SheetFor(IcoVfs *vfs, const char *name);
/* Frees every sheet. */
void ms_SheetsFree(void);
/* The sheet of language lang (UiLang order: EN FR DE IT ES) for a texFile
   path: its menu_PAL_xx folder replaced; other paths unchanged. */
void ms_SheetName(const char *path, int lang, char *out, size_t size);
/* texFile's rows (the table descriptor's count). */
int ms_TexFileCount(void);

#endif /* PORT_UI_TEST_MENU_SHEETS_H */
