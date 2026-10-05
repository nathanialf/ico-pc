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
 * All calls from the game fiber, like the rest of rd.  See
 * docs/port/RENDER_API.md "Textures (wave 2, R2b)".
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
 * w*h*4*4/3 bytes.  Returns the number of levels written after the base. */
uint32_t rdtex_BuildMipChain(const uint8_t *rgba, uint32_t w, uint32_t h, uint8_t *out);
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
/* The Enhanced hook (not in the settings yet): keep a CPU mip chain per
 * entry. */
void rdtex_SetEnhancedMips(int on);
const RdTexCacheStats *rdtex_Stats(void);

#ifdef __cplusplus
}

#endif
#endif /* PORT_RENDER_RD_TEX_H */
