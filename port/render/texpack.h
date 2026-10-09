/* texpack.h: PCSX2 texture-replacement packs (the files, the index, the
 * loader thread and the replacement budget).  Names come from
 * texpack_name.h; the texture cache (rd_tex.h rdtex_create_replacement,
 * rdtex_replace) puts a loaded image in place of the game's texture.
 *
 *   folders   <user folder>/textures/SCES-50760/replacements, then
 *             <program folder>/textures/SCES-50760/replacements, then the
 *             tolerant layouts (textures/replacements, files directly
 *             under textures/) of both; walked recursively, "png" and
 *             "dds" in any case; the first file of a name wins, later ones
 *             are counted as duplicates; "-mipN" files are counted and
 *             skipped (no loader reads them: an RGBA8 image gets its own
 *             box chain); region names are counted, never matched.
 *             Folder links inside a replacements folder are not followed
 *             (a link back up would make the walk endless).
 *   formats   PNG (RGBA8 out, 0x80 alpha for sources without alpha, as
 *             PCSX2) and DDS: BC1, BC2, BC3, BC7 kept as blocks (only when
 *             the device has BC, RhiLimits.bcTextures) and the uncompressed
 *             layouts converted to RGBA8.  RGBA8 alpha is raw GS alpha,
 *             0x80 = 1.0, the renderer's own convention.
 *   loading   texpack_request queues a file for the loader thread; the
 *             game fiber's texpack_pump (once a frame, beside
 *             rdtex_frame_tick) installs what finished.  With precache on,
 *             the thread also reads every indexed file into a RAM cache
 *             from startup, the PNGs first (the subtitles and menus), then
 *             the DDS files, up to video.texture_pack_cache_mb (0: half
 *             the computer's memory; Android: an eighth, at most 512 MB);
 *             a file that does not fit is skipped
 *             and the next one tried.  Requests jump the queue; a request
 *             for a cached file is installed at once, inside the hook, so
 *             a texture shown for one frame (the subtitles) is replaced
 *             from its first.  The cache keeps each image as the file
 *             holds it (no mip chain): the copy handed to the renderer
 *             gets its levels.
 *   budget    replacement textures live on the GPU only while their cache
 *             entry does; their bytes count against
 *             video.texture_pack_budget_mb (graphics memory, apart from
 *             the RAM cache's limit), and a replacement that would go over
 *             is declined (logged once), the game's texture kept.
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

/* A pack file as loaded, ready for rdtex_create_replacement. */
typedef struct TexpackImage {
    uint8_t fmt;     /* RD_TEXEL_RGBA8 or RD_TEXEL_BC1/BC2/BC3/BC7 (rd_internal.h) */
    uint32_t w, h;   /* level 0 */
    uint32_t levels; /* lv[0 .. levels-1]: the file's own mips (a DDS file's mip
                        count), or the box chain rdtex_replacement_mips adds; 1
                        when it has none */
    TexpackImageLevel lv[TEXPACK_IMAGE_LEVELS];
    void *blob;   /* one allocation holding every level; texpack_free_image frees it */
    size_t bytes; /* the blob's size: what the image costs in RAM and on the GPU */
} TexpackImage;

/* What a file is (the index's kind). */
typedef enum TexpackKind { TEXPACK_KIND_PNG = 0, TEXPACK_KIND_DDS = 1 } TexpackKind;

/* ---------------------------------------------------------- loaders (CPU)
 * Thread-safe; data/size is the whole file.  0, or -1 with *out zeroed
 * (and one log line naming file, when not null, saying why). */

/* PNG: colour types 0, 2, 3, 4, 6, bit depths 1..16, Adam7, tRNS. */
int texpack_load_png(const uint8_t *data, size_t size, const char *file, TexpackImage *out);
/* DDS: BC files are refused when bcSupported is 0, and when level 0 is
   not a multiple of 4 in both directions. */
int texpack_load_dds(const uint8_t *data, size_t size, int bcSupported, const char *file,
                     TexpackImage *out);
/* Frees img's blob and zeroes it (img may be null or empty). */
void texpack_free_image(TexpackImage *img);
/* The largest width or height the loaders accept (the graphics card's
   largest texture, RhiLimits.maxTextureSize; 0: only the formats' own
   limits).  A larger file is refused when its header is read, before any
   memory is taken for its texels.  Set before the loader thread starts
   (texpack_init does). */
void texpack_set_max_side(uint32_t side);
uint32_t texpack_max_side(void);

/* ------------------------------------------------------------ the pack */

typedef struct TexpackConfig {
    const char *userDir;     /* the user folder (ico_pref_dir), or null */
    const char *programDir;  /* the program's folder, or null */
    const char *serial;      /* "SCES-50760" */
    uint32_t budgetMb;       /* video.texture_pack_budget_mb: graphics memory */
    uint32_t cacheMb;        /* video.texture_pack_cache_mb: the RAM cache (0: half the
                               computer's memory; Android: an eighth, at most 512 MB) */
    int precache;            /* video.texture_pack_precache */
    int bcSupported;         /* RhiLimits.bcTextures, so BC files are refused up front */
    uint32_t maxTextureSize; /* RhiLimits.maxTextureSize (0: none), texpack_set_max_side */
    int developer;           /* gameplay.developer_mode: a log line per replacement */
} TexpackConfig;

/* Walks the folders, builds the index, logs what it found ("textures: N
   replacements from <dir>" per folder, or that there is no pack) and,
   with files indexed, starts the loader thread (and the precache).
   Returns the number of replacements indexed (0: nothing to do, every
   other call is then a cheap no-op). */
int texpack_init(const TexpackConfig *cfg);
/* Replacements indexed (0 before texpack_init or without a pack: the
   Settings row then says "None installed"). */
int texpack_count(void);
/* The index entry of a name (key: tex0Hash, clutHash, bits), or -1. */
int texpack_lookup(const TexpackName *name);
/* The file of an index entry (the path the walk found), or null. */
const char *texpack_entry_path(int entry);
/* Queue entry (from texpack_lookup) for the cache entry (texId, gen, texa)
   of rd_tex; uvW, uvH the GS size of the texture it replaces.  The image
   is installed by a later texpack_pump through rdtex_replace if the cache
   entry still has that generation then (else it is dropped).  An entry
   already read into the RAM cache (precache) is installed at once instead
   (rdtex_replace before this returns, so the caller's rdtex_find gives the
   replacement).  0 queued, 1 already queued or installed, 2 installed now,
   -1 refused (declined by the budget, now or before, the graphics card
   refused it before, or the file failed to load before). */
int texpack_request(int entry, uint32_t texId, uint32_t gen, int texa, uint32_t uvW, uint32_t uvH);
/* Once a frame on the game fiber (Texture.c tex_ResetVram, beside
   rdtex_frame_tick): installs the finished loads (rdtex_create_replacement,
   rdtex_replace), charging the budget.  While the pack is switched off
   (rd_get_settings()->texturePack 0) the finished loads are dropped
   (counted as discarded) and the queued requests forgotten, so nothing
   goes in after the switch. */
void texpack_pump(void);
/* Stops and joins the loader thread, frees the index and the RAM cache. */
void texpack_shutdown(void);
/* 1 when the entry's image is in the RAM cache (the precache's coverage,
   for the tests), else 0. */
int texpack_entry_cached(int entry);

struct RdTexImage; /* rd_tex.h */

/* [video] dump_textures: writes the bound level of src (im, as the cache's
   hook gave it to rdtex_store) as PNGs under the names a pack would give
   it at that level (one per TEXA mode when the name depends on TEXA), to
   <user folder>/textures/<serial>/dumps/<name>.png; a file already there
   is kept.  The texels are the decoded, padded RGBA8 with the GS alpha
   raw (0x80 = 1.0), what a PCSX2 dump holds.  Works without a pack (after
   texpack_init); returns the files written. */
int texpack_dump(const TexpackSource *src, uint32_t boundLevel, const struct RdTexImage *im);

/* What texpack_init found and what the loader did since, for the log's
   summary and the tests. */
typedef struct TexpackStats {
    uint32_t files;        /* "png"/"dds" files seen in the folders */
    uint32_t indexed;      /* replacements indexed (texpack_count) */
    uint32_t duplicates;   /* a name already indexed from an earlier file */
    uint32_t regions;      /* region names: stored, never matched */
    uint32_t mipFiles;     /* "-mipN" files: not indexed */
    uint32_t malformed;    /* names that are not texture names */
    uint32_t requested;    /* texpack_request calls queued */
    uint32_t cached;       /* files in the RAM cache */
    uint64_t cacheBytes;   /* their bytes */
    uint64_t cacheLimit;   /* the RAM cache's limit in bytes */
    uint32_t skipped;      /* files the precache left out: they did not fit */
    uint32_t precacheDone; /* 1 once the precache has been through the index */
    uint32_t loadFailed;   /* files that would not load */
    uint32_t installed;    /* replacements put in place */
    uint32_t discarded;    /* loads whose texture had changed or gone by the pump */
    uint32_t declined;     /* refused by the budget */
    uint32_t refused;      /* refused by the graphics card when uploaded: not tried again */
} TexpackStats;

void texpack_get_stats(TexpackStats *out);

/* The system low on memory (Android's LOW_MEMORY): the RAM
   cache lets go of every image no copy is being made of at the moment,
   and its limit drops to what it still holds, so the precache reads
   nothing more ahead and a loaded file is no longer kept (requests still
   load, from the file, and install); a line for the log.  Returns the
   bytes the cache holds.  Any thread; nothing before texpack_init. */
uint64_t texpack_low_memory(void);

/* ---------------------------------------------------------- the budget
 * GPU bytes of the live replacements, per rd texture.  rd_tex calls
 * texpack_budget_release when it retires a replacement (rdtex_drop, a new
 * generation, rdtex_revert_replacements, a replacement the graphics card
 * refused, rdtex_reset). */

/* The limit in bytes (0: none).  texpack_init sets it from budgetMb. */
void texpack_budget_set(uint64_t bytes);
uint64_t texpack_budget_limit(void);
uint64_t texpack_budget_used(void);
/* Charge bytes for the replacement t: 0, or -1 when that would go over the
   limit (nothing charged; the first refusal is logged). */
int texpack_budget_charge(RdTex t, uint64_t bytes);
/* Give back what t was charged (nothing when it was not).  When the
   graphics card refused t (rdtex_replacement_refused), its file is not
   offered again this run. */
void texpack_budget_release(RdTex t);

#ifdef __cplusplus
}
#endif
#endif /* PORT_RENDER_TEXPACK_H */
