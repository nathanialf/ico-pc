/* texpack.h: PCSX2 texture-replacement packs (the files, the index, the
 * loader thread and the replacement budget).  Names come from
 * texpack_name.h; the texture cache (rd_tex.h rdtex_CreateReplacement,
 * rdtex_Replace) puts a loaded image in place of the game's texture.
 *
 *   folders   <user folder>/textures/SCES-50760/replacements, then
 *             <program folder>/textures/SCES-50760/replacements, then the
 *             tolerant layouts (textures/replacements, files directly
 *             under textures/) of both; walked recursively, "png" and
 *             "dds" in any case; the first file of a name wins, later ones
 *             are counted as duplicates; "-mipN" files are not indexed
 *             (read beside their base file); region names are counted,
 *             never matched.
 *   formats   PNG (RGBA8 out, 0x80 alpha for sources without alpha, as
 *             PCSX2) and DDS: BC1, BC2, BC3, BC7 kept as blocks (only when
 *             the device has BC, RhiLimits.bcTextures) and the uncompressed
 *             layouts converted to RGBA8.  RGBA8 alpha is raw GS alpha,
 *             0x80 = 1.0, the renderer's own convention.
 *   loading   texpack_Request queues a file for the loader thread; the
 *             game fiber's texpack_Pump (once a frame, beside
 *             rdtex_FrameTick) installs what finished.  With precache on,
 *             the thread also reads every indexed file into a RAM cache
 *             from startup (requests jump the queue); a request for a
 *             cached file is installed at once, inside the hook, so a
 *             texture shown for one frame (the subtitles) is replaced from
 *             its first.
 *   budget    replacement textures live on the GPU only while their cache
 *             entry does; their bytes count against
 *             video.texture_pack_budget_mb, and a replacement that would
 *             go over is declined (logged once), the game's texture kept.
 *
 * Every function here is called from the game fiber; the loader thread
 * touches only its queues and the files.
 */
#ifndef PORT_RENDER_TEXPACK_H
#define PORT_RENDER_TEXPACK_H

#include <stddef.h>
#include <stdint.h>
#include "rd.h"
#include "texpack_name.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the most levels a pack image carries (a 16384 texture's full chain is
   15; DDS files may hold more, the rest are ignored) */
#define TEXPACK_IMAGE_LEVELS 16

/* One level of a loaded image. */
typedef struct TexpackImageLevel {
    const uint8_t *data; /* inside TexpackImage.blob */
    uint32_t w, h;       /* texels (a BC level under 4 x 4 keeps its real size) */
    uint32_t pitch;      /* bytes from one row to the next: w * 4 for RGBA8, a row
                            of 4x4 blocks (ceil(w/4) * 8 or 16) for BC */
    size_t size;         /* bytes of the level: pitch * h (RGBA8) or
                            pitch * ceil(h/4) (BC) */
} TexpackImageLevel;

/* A pack file as loaded, ready for rdtex_CreateReplacement. */
typedef struct TexpackImage {
    uint8_t fmt;     /* RD_TEXEL_RGBA8 or RD_TEXEL_BC1/BC2/BC3/BC7 (rd_internal.h) */
    uint32_t w, h;   /* level 0 */
    uint32_t levels; /* lv[0 .. levels-1]: the file's own mips (DDS mip count,
                        "-mipN" PNGs); 1 when it has none */
    TexpackImageLevel lv[TEXPACK_IMAGE_LEVELS];
    void *blob;   /* one allocation holding every level; texpack_FreeImage frees it */
    size_t bytes; /* the blob's size: what the image costs in RAM and on the GPU */
} TexpackImage;

/* What a file is (the index's kind). */
typedef enum TexpackKind { TEXPACK_KIND_PNG = 0, TEXPACK_KIND_DDS = 1 } TexpackKind;

/* ---------------------------------------------------------- loaders (CPU)
 * Thread-safe; data/size is the whole file.  0, or -1 with *out zeroed
 * (and one log line naming file, when not null, saying why). */

/* PNG: colour types 0, 2, 3, 4, 6, bit depths 1..16, Adam7, tRNS. */
int texpack_LoadPng(const uint8_t *data, size_t size, const char *file, TexpackImage *out);
/* DDS: BC files are refused when bcSupported is 0, and when level 0 is
   not a multiple of 4 in both directions. */
int texpack_LoadDds(const uint8_t *data, size_t size, int bcSupported, const char *file,
                    TexpackImage *out);
/* Frees img's blob and zeroes it (img may be null or empty). */
void texpack_FreeImage(TexpackImage *img);

/* ------------------------------------------------------------ the pack */

typedef struct TexpackConfig {
    const char *userDir;    /* the user folder (ico_pref_dir), or null */
    const char *programDir; /* the program's folder, or null */
    const char *serial;     /* "SCES-50760" */
    uint32_t budgetMb;      /* video.texture_pack_budget_mb */
    int precache;           /* video.texture_pack_precache */
    int bcSupported;        /* RhiLimits.bcTextures, so BC files are refused up front */
    int developer;          /* gameplay.developer_mode: a log line per replacement */
} TexpackConfig;

/* Walks the folders, builds the index, logs what it found ("textures: N
   replacements from <dir>" per folder, or that there is no pack) and,
   with files indexed, starts the loader thread (and the precache).
   Returns the number of replacements indexed (0: nothing to do, every
   other call is then a cheap no-op). */
int texpack_Init(const TexpackConfig *cfg);
/* Replacements indexed (0 before texpack_Init or without a pack: the
   Settings row then says "None installed"). */
int texpack_Count(void);
/* The index entry of a name (key: tex0Hash, clutHash, bits), or -1. */
int texpack_Lookup(const TexpackName *name);
/* The file of an index entry (the path the walk found), or null. */
const char *texpack_EntryPath(int entry);
/* Queue entry (from texpack_Lookup) for the cache entry (texId, gen, texa)
   of rd_tex; uvW, uvH the GS size of the texture it replaces.  The image
   is installed by a later texpack_Pump through rdtex_Replace if the cache
   entry still has that generation then (else it is dropped).  An entry
   already read into the RAM cache (precache) is installed at once instead
   (rdtex_Replace before this returns, so the caller's rdtex_Find gives the
   replacement).  0 queued, 1 already queued or installed, 2 installed now,
   -1 refused (declined by the budget, now or before, the device refused
   it, or the file failed to load before). */
int texpack_Request(int entry, uint32_t texId, uint32_t gen, int texa, uint32_t uvW, uint32_t uvH);
/* Once a frame on the game fiber (Texture.c tex_ResetVram, beside
   rdtex_FrameTick): installs the finished loads (rdtex_CreateReplacement,
   rdtex_Replace), charging the budget. */
void texpack_Pump(void);
/* The renderer restarted (rdtex_Reset): forget pending requests and what
   the budget had charged; the RAM cache stays. */
void texpack_ResetDevice(void);
/* Stops and joins the loader thread, frees the index and the RAM cache. */
void texpack_Shutdown(void);

struct RdTexImage; /* rd_tex.h */

/* [video] dump_textures: writes the bound level of src (im, as the cache's
   hook gave it to rdtex_Store) as PNGs under the names a pack would give
   it at that level (one per TEXA mode when the name depends on TEXA), to
   <user folder>/textures/<serial>/dumps/<name>.png; a file already there
   is kept.  The texels are the decoded, padded RGBA8 with the GS alpha
   raw (0x80 = 1.0), what a PCSX2 dump holds.  Works without a pack (after
   texpack_Init); returns the files written. */
int texpack_Dump(const TexpackSource *src, uint32_t boundLevel, const struct RdTexImage *im);

/* What texpack_Init found and what the loader did since, for the log's
   summary and the tests. */
typedef struct TexpackStats {
    uint32_t files;      /* "png"/"dds" files seen in the folders */
    uint32_t indexed;    /* replacements indexed (texpack_Count) */
    uint32_t duplicates; /* a name already indexed from an earlier file */
    uint32_t regions;    /* region names: stored, never matched */
    uint32_t mipFiles;   /* "-mipN" files: not indexed */
    uint32_t malformed;  /* names that are not texture names */
    uint32_t requested;  /* texpack_Request calls queued */
    uint32_t cached;     /* files in the RAM cache */
    uint64_t cacheBytes; /* their bytes */
    uint32_t loadFailed; /* files that would not load */
    uint32_t installed;  /* replacements put in place */
    uint32_t discarded;  /* loads whose texture had changed or gone by the pump */
    uint32_t declined;   /* refused by the budget */
} TexpackStats;

void texpack_GetStats(TexpackStats *out);

/* ---------------------------------------------------------- the budget
 * GPU bytes of the live replacements, per rd texture.  rd_tex calls
 * texpack_BudgetRelease when it retires a replacement (rdtex_Drop, a new
 * generation, rdtex_RevertReplacements, rdtex_Reset). */

/* The limit in bytes (0: none).  texpack_Init sets it from budgetMb. */
void texpack_BudgetSet(uint64_t bytes);
uint64_t texpack_BudgetLimit(void);
uint64_t texpack_BudgetUsed(void);
/* Charge bytes for the replacement t: 0, or -1 when that would go over the
   limit (nothing charged; the first refusal is logged). */
int texpack_BudgetCharge(RdTex t, uint64_t bytes);
/* Give back what t was charged (nothing when it was not). */
void texpack_BudgetRelease(RdTex t);

#ifdef __cplusplus
}
#endif
#endif /* PORT_RENDER_TEXPACK_H */
