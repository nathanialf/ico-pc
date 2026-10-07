/* texpack_name.h: the names PCSX2 gives textures, so a PCSX2
 * texture-replacement pack (textures/<serial>/replacements/<name>.png|dds)
 * loads unchanged.  CPU only: no device, no game state; the texture
 * cache's hook (Texture.c texHostTexture) fills a TexpackSource from the
 * TIM2 it already holds and asks for the candidate names.
 *
 * The rules, from PCSX2's GSTextureReplacements.cpp and GSTextureCache.cpp
 * (HashCacheKey::Create, HashTextureLevel, PaletteKeyHash,
 * CreateTextureName, ParseReplacementName):
 *
 *   name      TEX0Hash-CLUTHash-bits for the palette formats (PSMT8, PSMT4
 *             and their H variants), TEX0Hash-bits for the direct ones:
 *             printf "%llx-%llx-%08x" / "%llx-%08x", the two hashes in
 *             lower-case hex without zero padding, bits as 8 hex digits,
 *             then ".png" or ".dds".  Extra file levels are
 *             "<name>-mip<N>.<ext>"; region textures (TEX0 region clamp)
 *             "<hash>[-<clut>]-r<W>x<H>-<bits>.<ext>" and, from older
 *             PCSX2 dumps, "<hash>[-<clut>]-r<regionbits hex>-<bits>.<ext>".
 *   bits      PSM | TW << 6 | TH << 10 | TA0 << 15 | AEM << 23 | TA1 << 24
 *             (bit 14, once TCC, is always 0: PCSX2 clears it on parse).
 *             The TEXA fields are 0 for the palette formats and PSMCT32;
 *             only PSMCT24 and PSMCT16/16S carry them, so those names
 *             depend on the TEXA in force (RdTexA: 80/0/80, 7F/1/81,
 *             80/1/80 for TA0/AEM/TA1).
 *   TEX0Hash  XXH3-64 (seed 0, default secret) over one byte stream: the
 *             level the draw binds and, when the name is a mip chain, the
 *             following levels appended in order.  Per level, tw = 2^TW,
 *             th = 2^TH, the PSM's block is CT32/CT24 and the H formats
 *             (which live in CT32 words) 8x8, CT16/CT16S 16x8, PSMT8 16x16,
 *             PSMT4 32x16 texels:
 *               block path    tw >= block width, th >= block height and
 *                             the format fills all 32 bits of its word
 *                             (PSMCT32, PSMT8, PSMT4): the 256-byte GS
 *                             blocks covering tw x th, in block raster
 *                             order, each as GS memory holds it (the
 *                             write swizzle of the TIM2's linear texels,
 *                             texpack_BlockOffset).
 *               expanded path otherwise (a level smaller than a block, or
 *                             PSMCT24, PSMCT16/16S and the H formats): the
 *                             level read out linearly, th rows of
 *                             tw * bpp bytes, no row padding.  Palette
 *                             formats one byte per texel holding the index
 *                             (PSMT4 too); direct formats RGBA32 with TEXA
 *                             applied: CT24 rgb | TA0 << 24 (AEM: alpha 0
 *                             when rgb == 0); CT16 (c & 0x1F) << 3 |
 *                             (c & 0x3E0) << 6 | (c & 0x7C00) << 9 |
 *                             (bit 15 ? TA1 : TA0) << 24 (AEM: alpha 0 when
 *                             the 16-bit value is 0).  Little-endian words.
 *             Texels outside the TIM2's w x h but inside tw x th are
 *             whatever VRAM held on the PS2 and in PCSX2; they hash as
 *             zeros here and the name is marked unstable.
 *   CLUTHash  XXH3-64 over the CLUT as 16 or 256 u32 colours in index
 *             order (entry i is index i's colour).  CT32 CLUTs: the CSM1
 *             swizzle undone (entry i = memory entry rdtex_Csm1Index(i));
 *             a 16-entry CLUT is straight.  CT16/16S CLUTs: the same
 *             de-swizzle, middle-run swap included (WriteCLUT_T16_I8_CSM1
 *             reads the two CT16 blocks a 16x16 upload fills and lands
 *             entry i on memory entry rdtex_Csm1Index(i), as the CT32 path
 *             does; texpack_name_test runs both paths), then each entry
 *             expanded as the CT16 texel above with the TEXA in force (so
 *             a 16-bit CLUT's name depends on TEXA too, through the hash).  24-bit CLUTs are not GS CLUTs
 *             (TIM2 allows them): unsupported.  0 for direct formats.
 *   mips      PCSX2 with hardware mipmapping on hashes the bound level and
 *             the levels after it up to TEX1.MXL into one TEX0Hash; off
 *             (Sad Origami's PAL pack is made so) only the bound level, as
 *             a TEX0 of that level's size.  ICO binds lower levels itself
 *             (TexExt.level), so a bound level lv tries, in this order:
 *             lv alone, the chain lv..levels-1, then the levels k > lv
 *             (each alone, then as a chain) for packs dumped while the
 *             game showed a smaller level.
 *
 * All functions are pure and thread-safe; buffers are the caller's.
 */
#ifndef PORT_RENDER_TEXPACK_NAME_H
#define PORT_RENDER_TEXPACK_NAME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GS levels a TIM2 holds (the base and up to 6 mips, TEX1.MXL <= 6) */
#define TEXPACK_MAX_LEVELS 7
/* texpack_Candidates never returns more (every structure times the three
   TEXA modes) */
#define TEXPACK_MAX_CANDIDATES 64
/* the longest name texpack_FormatName writes, with its terminator and
   without an extension: 16 + 1 + 16 + 1 + 8 + 1 */
#define TEXPACK_NAME_MAX 48
/* TexpackName.texa for a name that does not depend on TEXA */
#define TEXPACK_TEXA_ANY 0xFF

/* One GS level as the TIM2 holds it. */
typedef struct TexpackLevel {
    uint8_t tw, th;     /* TEX0.TW / TEX0.TH: the GS size is 2^tw x 2^th */
    uint32_t w, h;      /* the image's texels (w <= 2^tw, h <= 2^th) */
    uint32_t tbw;       /* TEX0.TBW, in 64-texel units (tbw * 64 >= 2^tw in ICO) */
    const void *pixels; /* rows of w texels in the source's psm, linear (host)
                           order, no row padding (rdtex_ImageBytes(psm, w, h)) */
} TexpackLevel;

/* A texture as the cache's hook sees it: every level of the TIM2, lv[0]
   the base, and its CLUT. */
typedef struct TexpackSource {
    uint32_t psm;    /* RDTEX_PSM* (rd_tex.h) of the texels */
    uint32_t levels; /* lv[0 .. levels-1] are valid, 1..TEXPACK_MAX_LEVELS */
    TexpackLevel lv[TEXPACK_MAX_LEVELS];
    uint32_t cpsm;       /* the CLUT's entries: RDTEX_PSMCT32, RDTEX_PSMCT16(S), or
                            RDTEX_PSMCT24 (unsupported); ignored for direct formats */
    uint32_t clutColors; /* 16 or 256 for the palette formats, 0 otherwise: the
                            shape of the CLUT upload (tex_transVramClutTex: 8x2
                            for a 16-entry TIM2 CLUT, 16x16 for any other), so
                            a PSMT4 texture with a 256-entry CLUT passes 256 */
    const void *clut;    /* clutColors entries of cpsm in GS CSM1 memory order, i.e.
                            the uploaded image's texels in rows (rdtex_ClutToCsm1
                            applied when the TIM2 held them in index order), or
                            null for direct formats; a palette reading past them
                            (PSMT8 with 16 entries) hashes zeros there and the
                            name is unstable */
} TexpackSource;

/* A name, computed (texpack_ComputeName) or parsed (texpack_ParseName).
   The pack index's key is (tex0Hash, clutHash, bits); the other fields say
   how a computed name was made. */
typedef struct TexpackName {
    uint64_t tex0Hash;
    uint64_t clutHash; /* 0 when !hasClut */
    uint32_t bits;     /* PSM | TW<<6 | TH<<10 | TA0<<15 | AEM<<23 | TA1<<24 */
    uint8_t hasClut;   /* a palette format: the name has the CLUT part */
    uint8_t startLevel;
    uint8_t texa;     /* the RdTexA the name was computed under, or TEXPACK_TEXA_ANY when
                         neither the texels nor the CLUT depend on TEXA */
    uint8_t mipChain; /* the hash covers startLevel..levels-1, not startLevel alone */
    uint8_t unstable; /* a hashed level's image is smaller than 2^tw x 2^th: the
                         padding hashed as zeros, PCSX2 hashed stale VRAM there */
    /* texpack_ParseName only: the region of a "-rWxH" name (0 x 0 for a
       plain name).  Region names are stored and counted, never matched. */
    uint32_t regionW, regionH;
} TexpackName;

/* XXH3-64 of n bytes at p, seed 0, the default secret (xxHash v0.8.2's
   XXH3_64bits).  p may be null when n is 0. */
uint64_t xxh3_64(const void *p, size_t n);

/* The PSM's block size in texels (CT32/CT24, PSMT8H, PSMT4HL/HH 8x8,
   CT16/CT16S 16x8, PSMT8 16x16, PSMT4 32x16; GSLocalMemory.cpp m_psm[].bs).
   0, or -1 for a psm the names do not cover (the PSMZ formats, which ICO
   never samples, and unknown values). */
int texpack_BlockSize(uint32_t psm, uint32_t *bw, uint32_t *bh);

/* Where texel (x, y) of a block (x < block width, y < block height) sits
   inside its 256-byte GS block, in units of the texel: a u32 index for
   PSMCT32/24, u16 for PSMCT16/16S, a byte for PSMT8, a nibble for PSMT4
   (byte = offset >> 1, an even offset the low nibble).  UINT32_MAX for a
   psm texpack_BlockSize refuses, and for the H formats (always the
   expanded path).  The write swizzle of GSBlock.h/GSTables.cpp,
   e.g. CT32 (2,0) -> 4, PSMT8 (0,2) -> 33, PSMT4 (0,4) -> 192. */
uint32_t texpack_BlockOffset(uint32_t psm, uint32_t x, uint32_t y);

/* The bytes TEX0Hash takes for one level of src under texa (an RdTexA;
   ignored where TEXA does not enter), as the header describes (block or
   expanded path).  Writes them to out when out is not null and cap is
   large enough.  Returns the byte count (the same with out null, to size
   the buffer), 0 for an unsupported psm or level. */
size_t texpack_HashBytes(const TexpackSource *src, uint32_t level, int texa, uint8_t *out,
                         size_t cap);

/* The name of src bound at startLevel (alone, or with the levels after it
   when mipChain) under texa (an RdTexA; ignored and stored as
   TEXPACK_TEXA_ANY when nothing depends on TEXA).  0, or -1 when the name
   cannot be made: a 24-bit CLUT, an unknown psm, startLevel >= levels or
   levels > TEXPACK_MAX_LEVELS, a palette format without a CLUT, a texa
   outside the three RdTexA modes for a name that depends on it. */
int texpack_ComputeName(const TexpackSource *src, uint32_t startLevel, int texa, int mipChain,
                        TexpackName *out);

/* The names to look up for src bound at boundLevel, most likely first:
   boundLevel alone, the chain boundLevel..levels-1 (when there is more
   than one level), then for each k > boundLevel the level k alone and its
   chain.  A name that depends on TEXA comes once per RdTexA mode (RdTexA
   order) at each place; one that does not comes once.  Duplicates (a
   chain of one level) are left out.  Writes at most max names; returns
   how many, or -1 when texpack_ComputeName refuses the source. */
int texpack_Candidates(const TexpackSource *src, uint32_t boundLevel, TexpackName *out, int max);

/* n as a file name without the extension ("%llx-%llx-%08x" with the CLUT
   part, "%llx-%08x" without; never a region or mip suffix).  Returns the
   length written (buf holds at least TEXPACK_NAME_MAX bytes to be safe), or
   -1 when size is too small. */
int texpack_FormatName(const TexpackName *n, char *buf, size_t size);

/* The inverse, as PCSX2 parses: fileName is a base name with its
   extension ("<name>.png"; the extension itself is not checked, a '.'
   must follow the bits).  Fills tex0Hash, clutHash, bits (bit 14
   cleared), hasClut (the CLUT part was present) and regionW/H, zeroing
   the rest.  Returns 0 for a plain name, 1 for a region name ("-rWxH" or
   the old "-r<hex>" form), -1 for anything else, "-mipN" level files
   included (PCSX2 never indexes them either: they are read beside their
   base file). */
int texpack_ParseName(const char *fileName, TexpackName *out);

#ifdef __cplusplus
}
#endif
#endif /* PORT_RENDER_TEXPACK_NAME_H */
