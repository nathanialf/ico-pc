/*
 * port/null/gfx_null.c
 *
 * The ICO_HEADLESS graphics seam: what Main's draw phase and the vsync
 * scheduler (common/src/main.c) call into the renderer, as stubs, so the
 * game loop ticks with no renderer built. GsBase.c, which defines the gsb_*
 * functions, is renderer-owned and left out of headless builds
 * (ICO_RENDERER_SOURCES); these take its signatures (seki/include/GsBase.h).
 *
 * Headless differences, all confined to this file and the one call in
 * main.c:
 *   - Main calls ico_null_create_dl() in place of iosOmCreateDL(): the
 *     objects' display-list callbacks do not run (docs/port/PLATFORM.md).
 *   - gsb_SyncGSSystem reports the GS as finished, so every frame is
 *     "presented" and the scheduler never skips one.
 *   - sceGsSyncV keeps the EE's busy wait for the next vsync (libgraph's
 *     sceGsSyncV polls through VSync, sce/libgraph/graph011.c) and returns
 *     the field the host set in GS_CSR.
 */
#include <eeregs.h>

#include "sched.h"

void ico_null_create_dl(void);
void gsb_InitGSSystem(void);
void gsb_Init(void *db);
int gsb_ResetSnap(void);
int gsb_TakeSnap(void);
int gsb_SyncGSSystem(void);
void gsb_UpdateGSSystem(int keep);
int sceGsSyncV(int mode);

void ico_null_create_dl(void) {}

void gsb_InitGSSystem(void) {}

void gsb_Init(void *db)
{
    (void)db;
}

int gsb_ResetSnap(void)
{
    return 0;
}

int gsb_TakeSnap(void)
{
    return 0;
}

int gsb_SyncGSSystem(void)
{
    return 0;
}

void gsb_UpdateGSSystem(int keep)
{
    (void)keep;
}

int sceGsSyncV(int mode)
{
    (void)mode;
    ico_sched_spin_vsync();
    return (int)((*GS_CSR >> 13) & 1);
}
