/* texpack_walk.h: every TIM2 texture on the disc, as the game would load it.
 *
 * The offline half of the texture names (texpack_name.h): the walk reads
 * DATA.DF through an IcoDfIndex of its own (df_pack.h) and hands each TIM2
 * to a callback with the TexpackSource the runtime's hook builds for it
 * (Texture.c texHostPack: every level, the CLUT turned into CSM1 order when
 * the file holds it in index order), so the names texpack_candidates gives
 * it are the names the game looks up and Dump textures writes.  Its users:
 * texpack_disc_test (every name into texpack_names.txt) and the texture
 * export (texpack_export.h, the PNGs).
 *
 * The order, which texpack_names.txt keeps:
 *   1. every *.tm2 member of every pack, in the index's order (DATA.DF's
 *      directory, then each pack's), duplicates of a name included;
 *   2. every loose *.jim file of DATA.DF's directory: the subtitles, one
 *      TIM2 per 0x8800-byte record (jimaku.c reads them record by record
 *      into the buffer it hands to tex_InitTexture), found by scanning the
 *      file at every 16 bytes for the "TIM2" tag;
 *   3. every *.jim member of the packs, scanned the same way.
 * Each pack is inflated once per pass that wants a member of it
 * (ico_df_index_read_pack).
 *
 * PAL (texpack_names.txt): 8630 TIM2s, 6310 .tm2 members and 2320 .jim
 * records; 11240 level-0 names; 6 refused (a 24-bit CLUT); 841 mipmapped;
 * 156 with a CLUT scroll.
 *
 * No render device, no game state: links ico_texpack_name and ico_port_data
 * only; any thread, one walk per IcoDfIndex at a time.
 */
#ifndef PORT_RENDER_TEXPACK_WALK_H
#define PORT_RENDER_TEXPACK_WALK_H

#include <stddef.h>
#include <stdint.h>

#include "df_pack.h"
#include "texpack_name.h"
#include "vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the spacing of the subtitle files' records */
#define TEXPACK_WALK_RECORD 0x8800u
/* an item's member name with its terminator: a pack member's (0x214) and a
   record's "@0x<offset>" */
#define TEXPACK_WALK_NAME_MAX 600

/* where an item came from */
enum {
    TEXPACK_WALK_TM2 = 0,        /* a *.tm2 member of a pack */
    TEXPACK_WALK_JIM_LOOSE = 1,  /* a record of a loose *.jim file */
    TEXPACK_WALK_JIM_MEMBER = 2, /* a record of a *.jim member of a pack */
};

/* why an item has no source (TexpackWalkItem.bad) */
enum {
    TEXPACK_WALK_OK = 0,
    TEXPACK_WALK_NOT_TIM2 = 1,   /* shorter than the headers or no "TIM2" tag */
    TEXPACK_WALK_BAD_HEADER = 2, /* type, levels or size out of range, or the
                                    image and CLUT run past the file (type,
                                    levels, w, h are filled) */
    TEXPACK_WALK_BAD_LEVEL = 3,  /* level badLevel holds badBytes, fewer than
                                    its texels need or past the image (the
                                    header fields and src.lv[0..badLevel] are
                                    filled) */
    TEXPACK_WALK_UNREADABLE = 4, /* the member or file would not read or
                                    inflate: member names it, data is null */
};

/* One TIM2 as the walk hands it (valid until the callback returns). */
typedef struct TexpackWalkItem {
    char member[TEXPACK_WALK_NAME_MAX]; /* "object/.../x.tm2", "data_EG01.jim@0x8800" */
    int kind;                           /* TEXPACK_WALK_TM2 / _JIM_LOOSE / _JIM_MEMBER */
    const uint8_t *data;                /* the TIM2 (a record: to the end of its file) */
    size_t size;
    int bad; /* TEXPACK_WALK_OK or the reason */
    uint32_t badLevel;
    size_t badBytes;
    /* the TIM2's picture header */
    uint32_t type;       /* imageType: 1 CT16, 2 CT24, 3 CT32, 4 T4, 5 T8 */
    uint32_t clutType;   /* as the file holds it (bit 7: index order) */
    uint32_t clutColors; /* as the file holds it */
    uint32_t clutSize;   /* the CLUT's bytes in the file */
    uint32_t imageSize;
    size_t bytes; /* the TIM2's own: 16 + headerSize + imageSize + clutSize (0
                     for TEXPACK_WALK_NOT_TIM2 and _BAD_HEADER) */
    uint32_t w, h, levels;
    int nonPow2; /* w or h is no power of two */
    int scroll;  /* the ICO block scrolls the CLUT (tex_textureAnimation): the
                    names of the frames after the first are not on the disc */
    int clut24;  /* a 24-bit CLUT: no GS CLUT, texpack_candidates refuses it */
    /* a palette format whose CLUT is shorter than the format's 16 or 256
       entries: the file's clutColors below it (not 0), or fewer CLUT bytes
       than the format's entries take.  The runtime names no such texture
       (Texture.c texHostPack: the GS reads the full CLUT, the rest is stale
       VRAM); src still holds it padded with zeros, as texpack_disc_test
       names it */
    int shortClut;
    /* the hook's source: lv[] point into data, clut at the item's own
       copy (clut[]) padded with zeros to 256 entries.  clutColors is 16 for
       a file CLUT of 16 entries, else 256 (the upload's shape) */
    TexpackSource src;
    uint8_t clut[256 * 4];
} TexpackWalkItem;

/* The walk's callback: 0 goes on, anything else stops the walk. */
typedef int (*TexpackWalkFn)(void *user, const TexpackWalkItem *item);

/* What a walk handed (texpack_walk_disc fills it). */
typedef struct TexpackWalkStats {
    int items;      /* callbacks: TIM2s, bad ones and unreadable members included */
    int tm2;        /* items of *.tm2 members */
    int jimFiles;   /* *.jim files and members read */
    int jimRecords; /* items of their records */
    int unreadable; /* TEXPACK_WALK_UNREADABLE items */
    int stopped;    /* the callback stopped the walk */
} TexpackWalkStats;

/* The TIM2 type's name ("PSMT8"; "NONE" for one out of range). */
const char *texpack_walk_type_name(uint32_t type);

/* Parses the TIM2 at d (size bytes) into *out (member, kind as given):
   returns out->bad. */
int texpack_walk_tim2(const char *member, int kind, const uint8_t *d, size_t size,
                      TexpackWalkItem *out);

/* Every TIM2 of a file of records (a .jim), named "<file>@0x<offset>": the
   file is scanned at every 16 bytes for the "TIM2" tag.  The items handed;
   *stopped (when not NULL) set when fn stopped it. */
int texpack_walk_records(const char *file, int kind, const uint8_t *d, size_t size,
                         TexpackWalkFn fn, void *user, int *stopped);

/* The items a walk of idx will hand, before anything is read: the *.tm2
   members plus, per *.jim (loose and member), its size in records rounded
   up.  An estimate for a progress bar where a .jim holds other than one
   TIM2 per record (the PAL disc's do not). */
int texpack_walk_total(const IcoDfIndex *idx);

/* The walk, in the order above.  idx is the caller's index over vfs (NULL:
   the walk opens one of its own over vfs and closes it).  0 when it went to
   the end, 1 when fn stopped it, -1 when no index could be opened.  stats
   may be NULL. */
int texpack_walk_disc(IcoVfs *vfs, IcoDfIndex *idx, TexpackWalkFn fn, void *user,
                      TexpackWalkStats *stats);

#ifdef __cplusplus
}
#endif
#endif /* PORT_RENDER_TEXPACK_WALK_H */
