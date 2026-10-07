/* rd_tex.h: the texture cache (renderer wave 2, package R2b).
 *
 * The game keeps its TIM2 files in memory as Texture.c loaded them: per
 * mipmap level a copy of the image, one copy of the CLUT.  rd_tex decodes
 * those bytes into an RGBA8 rd texture the first time a texture is needed
 * and keeps it until the content changes:
 *
 *   key       (texture id, content generation, TEXA mode).  The id is the
 *             caller's (Texture.c's table index), the generation the
 *             caller's content counter (it bumps it when a CLUT scroll or a
 *             level change alters the texels), the TEXA mode either one of
 *             the three RdTexA values (the texels' alpha baked under it) or
 *             RDTEX_TEXA_REPLAY (the texture keeps the source format's alpha
 *             and the shader applies the TEXA in force at replay).
 *   decode    PSMCT32, PSMCT24, PSMCT16, PSMCT16S, PSMT8, PSMT4, PSMT8H,
 *             PSMT4HL, PSMT4HH and the PSMZ formats (read as the colour
 *             format of the same size), from host-memory (TIM2, linear)
 *             order.  CLUT entries are PSMCT32, PSMCT16/16S or (TIM2 only)
 *             24-bit, in GS CSM1 memory order or in index order.  Output
 *             RGBA8 whose alpha byte is the raw GS alpha (0x80 = 1.0) for
 *             32-bit texels, the A bit (0/1) for 16-bit ones and 0 for
 *             24-bit ones (RdTexSrc, rd.h), so TEXA can still be applied.
 *   size      the image, padded with zero texels to the size the GS
 *             addresses (2^TW x 2^TH), so STQ coordinates and REPEAT wrap
 *             at the same place as on the GS.
 *   update    a new generation for an id whose entry has the same size and
 *             source format re-expands into the same RdTex (rd_UpdateTexture:
 *             every draw of the frame being recorded sees the new texels,
 *             which is also what the PS2 shows, since its DMA chain reads the
 *             CLUT at kick time); otherwise the old RdTex is retired and
 *             destroyed two rdtex_FrameTick calls later, after any frame
 *             that may reference it has been replayed.
 *
 * Mips: the Original preset samples one level per texture, the one the
 * caller decodes (TexExt.level); rdtex_SetEnhancedMips is the hook for the
 * Enhanced filter upgrade (generated mips, trilinear/anisotropic): when on,
 * each entry also keeps a CPU mip chain (rdtex_BuildMipChain) for the RHI to
 * upload once it has mipmapped textures.  Not exposed in settings yet.
 *
 * All calls from the game fiber, like the rest of rd.
 */
#ifndef PORT_RENDER_RD_TEX_H
#define PORT_RENDER_RD_TEX_H

#include <stddef.h>
#include <stdint.h>
#include "rd.h"

#ifdef __cplusplus

extern "C" {
#endif

/* GS pixel storage modes (TEX0.PSM / BITBLTBUF.DPSM) */
enum {
    RDTEX_PSMCT32 = 0,
    RDTEX_PSMCT24 = 1,
    RDTEX_PSMCT16 = 2,
    RDTEX_PSMCT16S = 10,
    RDTEX_PSMT8 = 19,
    RDTEX_PSMT4 = 20,
    RDTEX_PSMT8H = 27,
    RDTEX_PSMT4HL = 36,
    RDTEX_PSMT4HH = 44,
    RDTEX_PSMZ32 = 48,
    RDTEX_PSMZ24 = 49,
    RDTEX_PSMZ16 = 50,
    RDTEX_PSMZ16S = 58
};

/* The texels' alpha left in the source format, TEXA applied at replay. */
#define RDTEX_TEXA_REPLAY 0xFF

/* One image as it sits in host memory. */
typedef struct RdTexImage {
    uint32_t w, h;       /* texels */
    uint32_t padW, padH; /* the texture's size (>= w, h); 0 = w, h */
    uint32_t psm;        /* RDTEX_PSM* of the texels */
    uint32_t cpsm;       /* CLUT entries: RDTEX_PSMCT32, RDTEX_PSMCT16(S), or RDTEX_PSMCT24
                            (3-byte entries; TIM2 allows them, the GS does not) */
    uint32_t clutColors; /* 16 or 256 for the indexed formats */
    int clutLinear;      /* 1: entries in index order; 0: GS CSM1 memory order */
    const void *pixels;  /* rows of w texels, no padding */
    const void *clut;
} RdTexImage;

/* Per-texture sampler state the caller derives from its TEX1/CLAMP words
 * (RdFilter, RdWrap). */
typedef struct RdTexSampler {
    uint8_t mag, min, wrapS, wrapT;
} RdTexSampler;

typedef struct RdTexCacheStats {
    uint32_t entries;  /* live entries */
    uint32_t decodes;  /* images decoded (creates + updates) */
    uint32_t creates;  /* rd textures created */
    uint32_t updates;  /* re-expansions into an existing rd texture */
    uint32_t hits;     /* rdtex_Find hits */
    uint32_t misses;   /* rdtex_Find misses */
    uint32_t retired;  /* rd textures queued for destruction */
    uint32_t failures; /* images that could not be decoded */
    uint32_t replaced; /* texture packs: entries given a replacement (rdtex_Replace) */
} RdTexCacheStats;

/* ------------------------------------------------------------ decoding */

/* The position of CLUT index i in GS CSM1 memory order (a 256-entry CLUT
 * swaps the middle two 8-entry runs of every 32; a 16-entry CLUT is held
 * straight). */
static inline uint32_t rdtex_Csm1Index(uint32_t i, uint32_t colors)
{
    if (colors != 256) {
        return i;
    }
    switch (i & 0x18) {
    case 0x08:
        return i + 8;
    case 0x10:
        return i - 8;
    default:
        return i;
    }
}

/* Rearrange a CLUT held in index order into CSM1 memory order, in place
 * (Texture.c tex_convertClutCSM2ToCSM1, for any entry size; a 16-entry CLUT
 * is unchanged). */
void rdtex_ClutToCsm1(void *clut, uint32_t colors, uint32_t entryBytes);

/* Bytes of one image of w x h texels in psm (0 for an unknown psm). */
size_t rdtex_ImageBytes(uint32_t psm, uint32_t w, uint32_t h);

/* Decode im into out (padW x padH RGBA8, zero outside w x h).  *src gets
 * the source format the alpha byte holds.  Returns 0, or -1 for a format
 * it does not know. */
int rdtex_Decode(const RdTexImage *im, uint8_t *out, RdTexSrc *src);

/* The GS TEXA expansion on the CPU: rewrites the alpha byte of n texels of
 * source format src under mode (gs_texa_alpha in gs_math.hlsli).  RGBA32
 * texels are left alone. */
void rdtex_ApplyTexa(uint8_t *rgba, size_t n, RdTexSrc src, RdTexA mode);

/* The Enhanced mip chain: successive 2x2 box levels of a w x h RGBA8 image
 * (both powers of two) written one after the other to out, which holds
 * rdtex_MipChainBytes(w, h) bytes.  Returns the number of levels written
 * after the base. */
uint32_t rdtex_BuildMipChain(const uint8_t *rgba, uint32_t w, uint32_t h, uint8_t *out);
/* The bytes rdtex_BuildMipChain writes for a w x h base: the sum of
 * max(w>>k,1) * max(h>>k,1) * 4 over the levels k >= 1.  w*h*4/3 is only
 * right for square images: a 128x4 chain is 764 bytes (its 1-high levels
 * keep a full row each), 512x2 is 2044. */
size_t rdtex_MipChainBytes(uint32_t w, uint32_t h);
/* Wave 7 (R7a): alpha-coverage preservation for the Enhanced filter's mips.
 * base is level 0 (w x h), chain the levels rdtex_BuildMipChain wrote
 * (levels of them); each level whose share of texels with alpha > ref fell
 * below level 0's has its alpha scaled up (at most 4x, never past level 0's
 * largest alpha) until it is back, so alpha-tested foliage and fences do
 * not thin out in the distance.  Textures without such texels or without
 * any are left alone. */
void rdtex_KeepAlphaCoverage(const uint8_t *base, uint32_t w, uint32_t h, uint8_t *chain,
                             uint32_t levels, uint8_t ref);

/* --------------------------------------------------------------- cache */

/* The entry for (id, gen, texa), or {0}. */
RdTex rdtex_Find(uint32_t id, uint32_t gen, int texa);
/* Decode im for (id, gen, texa) and return its texture (see "update"
 * above).  smp may be null.  {0} when rd is not initialised or the format
 * is unknown. */
RdTex rdtex_Store(uint32_t id, uint32_t gen, int texa, const RdTexImage *im,
                  const RdTexSampler *smp, const char *debugName);
/* The sampler state stored with the entry for (id, texa), or null. */
const RdTexSampler *rdtex_Sampler(uint32_t id, int texa);
/* The texture id is gone (tex_FreeTexture): retire its entries. */
void rdtex_Drop(uint32_t id);
/* Once per game frame: destroys the textures retired two calls ago. */
void rdtex_FrameTick(void);
/* rd was shut down (and maybe started again): forget every entry without
 * destroying anything (rd_Shutdown destroyed them). */
void rdtex_Reset(void);
/* ------------------------------------------------------- texture packs
 *
 * A pack replacement (texpack.h) takes the place of a cache entry's
 * texture: every later bind of (id, gen, texa) samples it.  It lasts as
 * long as the entry: a new generation (a CLUT scroll re-store), rdtex_Drop
 * or rdtex_Reset retire it with the entry. */

struct TexpackImage; /* texpack.h */

/* A replacement texture from a pack image: its levels (RGBA8 with raw GS
 * alpha, or BC blocks as the file holds them) move into the texture's
 * pending upload and *img is left empty; the next replay uploads every
 * level and frees them (rd_DestroyTexture and rd_Shutdown free them when
 * that never came).  uvW, uvH: the GS size of the texture it replaces
 * (2^TW x 2^TH, the original entry's padded size), which the draws' UVs
 * keep being normalised by.  The texture samples as RD_TEXSRC_RGBA32 (the
 * pack's alpha is raw GS alpha, no TEXA) and as mipmapped: min filter
 * linear, trilinear between its levels under the Original filter and
 * anisotropic when the option says so.  An RGBA8 image without its own
 * mips gets the box chain here (rdtex_ReplacementMips, unless the caller
 * already did it); a BC image without mips stays one level (logged once).
 * {0} (img untouched) when rd is not initialised, the format is BC and
 * the device has no BC (RhiLimits.bcTextures), the image is larger than
 * the device takes, or it is empty. */
RdTex rdtex_CreateReplacement(struct TexpackImage *img, uint32_t uvW, uint32_t uvH,
                              const char *debugName);
/* The 2x2 box chain (rdtex_BuildMipChain, alpha coverage kept as the
 * Enhanced filter's mips) appended to a one-level RGBA8 image: img's blob
 * is replaced by one holding every level and img->levels set.  CPU only,
 * callable from any thread (the pack's loader thread may do it so the game
 * fiber does not).  0, or -1 (img unchanged: not a one-level RGBA8 image
 * with rows of w * 4 bytes, or no memory). */
int rdtex_ReplacementMips(struct TexpackImage *img);
/* Install rep as the texture of the entry (id, gen, texa): the entry's
 * current texture is retired, rep takes its place and the entry is marked
 * replaced (a later store of that entry, a CLUT scroll's new generation,
 * makes a new texture of the game's own and retires rep).  0, or -1 when
 * there is no such entry (freed, or a newer generation stored since the
 * request) or rep is not a live texture: the caller keeps rep and
 * destroys it. */
int rdtex_Replace(uint32_t id, uint32_t gen, int texa, RdTex rep);
/* The pack was switched off (rd_SetSettings on a texturePack true -> false
 * edge): retire every replaced entry's texture and forget the entry, so
 * the next bind decodes the game's original again. */
void rdtex_RevertReplacements(void);
/* Called with each replacement texture the cache gives up: the entry was
 * dropped (rdtex_Drop), stored again (a new generation), reverted
 * (rdtex_RevertReplacements), replaced once more, or forgotten
 * (rdtex_Reset).  The texture itself is destroyed two frame ticks later
 * (or already was, after rd_Shutdown); the hook only settles accounts.
 * texpack.c sets texpack_BudgetRelease here at texpack_Init and null at
 * texpack_Shutdown; null (the default) calls nothing. */
typedef void (*RdTexReleaseFn)(RdTex t);
void rdtex_SetReleaseHook(RdTexReleaseFn fn);

/* The Enhanced hook (not in the settings yet): keep a CPU mip chain per
 * entry. */
void rdtex_SetEnhancedMips(int on);
const RdTexCacheStats *rdtex_Stats(void);

#ifdef __cplusplus
}

#endif
#endif /* PORT_RENDER_RD_TEX_H */
