/* texpack_export.h: every texture on the player's disc as PNG files, named
 * as Dump textures names them (issue 137).
 *
 * The export walks every TIM2 of the disc (texpack_walk.h) and writes each
 * level the game can bind, alone, under every name the runtime dump gives
 * it (texpack.c texpack_dump: the names of texpack_candidates(src, level)
 * whose startLevel is that level and that are no mip chain, one file per
 * TEXA mode when the name depends on TEXA), with the same pixels (the
 * level decoded by rdtex_decode from the RdTexImage Texture.c's
 * texHostTexture builds, padded to 2^TW x 2^TH, the GS alpha raw), into
 * the dump folder <user folder>/textures/<serial>/dumps (texpack_dump_dir).
 * So a pack made from the export is found by the port and by PCSX2 as one
 * made from the runtime dump.  Each name is written once per run (the same
 * texture in several packs is one file), a file already there is kept,
 * and each file is written as <name>.png.tmp and then renamed, so the
 * runtime dump and the pack loader never see half a file.  A texture the
 * runtime names nothing for is refused: a 24-bit CLUT (no GS CLUT) and a
 * CLUT shorter than the format's 16 or 256 entries (Texture.c texHostPack).
 *
 * dumps/manifest.txt lists every item, one row each, its columns separated
 * by tabs (shown here as " | "):
 *
 *   # ICO texture export <serial> <YYYY-MM-DD> complete
 *       (or "stopped after <done> of <total>"; a DRY run adds " dry run")
 *   # items M  tim2 N  jim-records R  written W  already-on-disk S  duplicates D
 *     refused-clut24 X  refused-short-clut Y  bad B  failed F      (one line)
 *   # Not in this export, ... (one line: what no export from the disc can
 *     make, in plain words, and to play with Dump textures on for those)
 *   # member | kind | type | size | WxH | levels | clut | flags | names
 *   <member> | tm2, jim or jim-member | PSMT8 ... ("-" for no TIM2) |
 *       <the TIM2's bytes> | <W>x<H> | <levels> | <colors>/<bytes>B or "-" |
 *       <flags, comma-separated, or "-"> | <names, space-separated, or "-">
 *
 * flags: unstable, scroll, clut24, short-clut, duplicate, on-disk,
 * bad:<not-tim2|header|level-N|unreadable|no-names>, failed.  A name is
 * <name>@<level> after one mark: + written (DRY: would be written),
 * = already on disk, ~ a duplicate of an earlier item's, ! the write
 * failed.  The manifest is written when the run ends, a stopped run's too.
 *
 * PAL: 8630 items (6310 .tm2 members, 2320 .jim records), 11240 level-0
 * names, 6 refused (24-bit CLUT), 841 mipmapped, 156 with a CLUT scroll;
 * deduplicated about 1730 files, about 160 MB (rd_png.c stores the pixels
 * uncompressed).
 *
 * The log (stderr, the game's ico-pc.log):
 *   textures: export started, <N> items, folder <path>
 *   textures: export <done> of <total> items, <W> written     (every 1000 items)
 *   textures: export cannot write <path>: <strerror> (errno <e>, failed write <k>)
 *       (the first 8 failed writes, then every 100th)
 *   textures: export <complete | stopped after D of T | failed>: items M,
 *     tim2 N, jim-records R, written W, already-on-disk S, duplicates D,
 *     refused-clut24 X, refused-short-clut Y, bad B, failed F, manifest <path>
 *   textures: export not started: <reason>                    (begin's -1)
 *   textures: export failed: the disc's DATA.DF could not be read
 * (a DRY run adds " (dry run)" to the start and end lines).
 */
#ifndef PORT_RENDER_TEXPACK_EXPORT_H
#define PORT_RENDER_TEXPACK_EXPORT_H

#include <stddef.h>
#include <stdint.h>

#include "texpack_walk.h"
#include "vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* texpack_export_open / _run flags */
#define TEXPACK_EXPORT_DRY 1u /* the names and the manifest, no PNG */

/* TexpackExportStatus.state */
enum {
    TEXPACK_EXPORT_IDLE = 0,    /* no export since the start */
    TEXPACK_EXPORT_RUNNING = 1, /* the worker is walking the disc */
    TEXPACK_EXPORT_DONE = 2,    /* went through every item */
    TEXPACK_EXPORT_STOPPED = 3, /* texpack_export_cancel (or shutdown) stopped it */
    TEXPACK_EXPORT_FAILED = 4,  /* the disc's index could not be read, the
                                   manifest could not be written, or every
                                   write failed */
};

/* What an export did so far (the counters) and where. */
typedef struct TexpackExportStatus {
    int state;            /* TEXPACK_EXPORT_* */
    int done;             /* items handled */
    int total;            /* items expected (texpack_walk_total; 0 until the
                             worker has read the disc's index; raised to done
                             at the end if the walk handed more) */
    int tim2;             /* items from *.tm2 members */
    int jimRecords;       /* items from .jim records */
    int written;          /* PNG files written (DRY: that would be) */
    int skipped;          /* names whose file was already in the folder */
    int duplicates;       /* names an earlier item of the run already gave */
    int refused;          /* items refused: refusedClut24 + refusedShortClut */
    int refusedClut24;    /* a 24-bit CLUT */
    int refusedShortClut; /* a CLUT shorter than the format's */
    int bad;              /* items that are no readable TIM2 */
    int failed;           /* files that could not be written */
    int scroll;           /* items whose CLUT scrolls (only the first palette exported) */
    char dir[1100];       /* the folder (the dumps folder) */
} TexpackExportStatus;

/* ------------------------------------------------- the core (any thread)
 * Synchronous and disc-free: the tests feed it items of their own. */

typedef struct TexpackExport TexpackExport;

/* A run into the folder dumpDir (it must exist) for the disc serial (the
   manifest's header), flags TEXPACK_EXPORT_*.  NULL when memory runs out
   or dumpDir is empty. */
TexpackExport *texpack_export_open(const char *dumpDir, const char *serial, unsigned flags);
/* One walk item: its names written (or counted) and its manifest row.
   Returns the files written for it (DRY: that would be), or -1 for an item
   refused or bad. */
int texpack_export_item(TexpackExport *x, const TexpackWalkItem *item);
/* The counters so far (state RUNNING). */
void texpack_export_counts(const TexpackExport *x, TexpackExportStatus *out);
/* Writes dumps/manifest.txt (as manifest.txt.tmp, then renamed), logs the
   end line and frees x.  stopped: the run did not reach the end; total:
   the items expected (for "stopped after done of total"; below done it is
   raised to done).  The final counters and state go to *status (when not
   NULL).  Returns the final state. */
int texpack_export_close(TexpackExport *x, int stopped, int total, TexpackExportStatus *status);

/* Called by texpack_export_run before each item (nonzero: stop there, so
   done is the items before it) and after each item with the counters. */
typedef struct TexpackExportHooks {
    int (*cancelled)(void *user);
    void (*progress)(void *user, const TexpackExportStatus *now);
    void *user;
} TexpackExportHooks;

/* The whole export over vfs (a mount the caller owns and nobody else reads
   meanwhile): its own IcoDfIndex, the walk, every item, the manifest; the
   start line and a line per 1000 items in the log.  hooks may be NULL.
   Returns the final state (TEXPACK_EXPORT_DONE / _STOPPED / _FAILED), the
   counters in *status (when not NULL). */
int texpack_export_run(IcoVfs *vfs, const char *dumpDir, const char *serial, unsigned flags,
                       const TexpackExportHooks *hooks, TexpackExportStatus *status);

/* ------------------------------------------------------------ the job
 * One export at a time on a worker thread (SDL, at
 * SDL_THREAD_PRIORITY_LOW), for the Options row (EXPORT-UI).  The worker
 * reads its own mount of the disc (ico_vfs_reopen of ico_vfs_disc()) and
 * its own IcoDfIndex, never the game's, holds at most the largest pack
 * member and two decoded levels, checks the cancel flag between items and
 * sets its final state last, after the manifest.  It shares nothing else
 * with the game but rd_png's CRC table (built by begin, rd_png_prepare),
 * so the game may go on dumping textures meanwhile: the names are the
 * same, a file already there is kept and both write through a .tmp file.
 *
 * Shutdown order: texpack_export_shutdown joins the worker; call it before
 * texpack_shutdown, rd_shutdown and SDL_Quit (main_host.c registers it with
 * at_shutdown after texpack_shutdown, so it runs first; window_host.c calls
 * it before rd_shutdown).  It may be called any number of times. */

/* Starts an export of the selected disc into texpack_dump_dir's folder.
   The game thread.  0 started; -1 (and a "textures: export not started"
   line) when one is running, before texpack_init or without a user folder
   (no dump folder), without a disc, when the disc cannot be mounted again,
   or when the thread cannot start (a build without SDL threads).  A
   finished worker is joined first. */
int texpack_export_begin(void);
/* The job's state and counters (any thread; a copy taken with atomics, so
   the counters may be one item apart).  Returns out->state; out may be
   NULL. */
int texpack_export_poll(TexpackExportStatus *out);
/* Asks a running export to stop before its next item (any thread); it
   then writes its manifest and ends STOPPED.  Nothing when none runs. */
void texpack_export_cancel(void);
/* Cancels a running export and waits for its worker (the game thread).
   The last state stays readable by texpack_export_poll. */
void texpack_export_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif /* PORT_RENDER_TEXPACK_EXPORT_H */
