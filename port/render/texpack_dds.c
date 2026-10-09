/* texpack_dds.c: DDS files of PCSX2 texture packs (texpack.h
 * texpack_load_dds).
 *
 * The rules are PCSX2's (GS/Renderers/HW/GSTextureReplacementLoaders.cpp,
 * ParseDDSHeader, ReadDDSMipLevel, DDSLoader), so a file loads here
 * exactly when it loads there and gives the same texels:
 *
 *   header    "DDS ", a 124-byte header whose dwSize is at least 124,
 *             width and height 1..32767, not a volume; with the DX10
 *             fourcc a 20-byte extension that must be a 2D texture of
 *             array size 1.  The data starts after 4 + 124 (+ 20) bytes
 *             whatever dwSize says, and that offset must lie inside the
 *             file.
 *   formats   fourcc DXT1 or DXGI 71: BC1; DXT2, DXT3 or DXGI 74: BC2;
 *             DXT4, DXT5 or DXGI 77: BC3; DXGI 98: BC7 (refused without
 *             BC on the device, as PCSX2 refuses them without the device
 *             feature).  Without a fourcc the pixel format must equal one
 *             of DirectXTex's five (size, flags, bit count and the four
 *             masks): A8R8G8B8 (R and B swapped, alpha kept), X8R8G8B8
 *             (swapped, alpha 0xFF), X8B8G8R8 (alpha 0x80), R8G8B8 (24-bit,
 *             swapped, alpha 0xFF) and A8B8G8R8 (as stored).  These alpha
 *             values are PCSX2's quirks, kept: 0x80 is the GS's opaque, so
 *             X8B8G8R8 is opaque and the other two count double.
 *   levels    the mip count from dwMipMapCount when DDSD_MIPMAPCOUNT is
 *             set (0 there: the full chain), else 1.  Level 0's row pitch
 *             is the header's when both DDSD_PITCH and DDSD_LINEARSIZE
 *             are set (at least one block), else the tight one; the other
 *             levels are always tight, and a level under 4 x 4 still
 *             takes one block per row and column.  Level 0 of a BC file
 *             must be a multiple of 4 both ways (logged).  Level 0 must be
 *             complete; then levels 1 .. mip count are read while the file
 *             holds them (PCSX2's loop runs one past the count and stops
 *             at the end of the file), never more than the full chain.
 *
 * Output: BC levels as the file holds them, rows of blocks repacked to the
 * tight pitch; uncompressed levels as RGBA8 rows of w * 4 bytes.  A header
 * pitch smaller than a tight row is refused (PCSX2 would read rows
 * overlapping each other: a broken file). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/ico_endian.h"
#include "rd_internal.h"
#include "texpack.h"

#define DDS_MAGIC 0x20534444u /* "DDS " */
#define DDS_HEADER_BYTES 124u
#define DDS_DX10_BYTES 20u
#define DDS_MAX_TEXTURE_SIZE 32768u

#define DDS_FOURCC 0x00000004u
#define DDS_RGB 0x00000040u
#define DDS_RGBA 0x00000041u
#define DDS_HEADER_FLAGS_MIPMAP 0x00020000u
#define DDS_HEADER_FLAGS_VOLUME 0x00800000u
#define DDS_HEADER_FLAGS_PITCH 0x00000008u
#define DDS_HEADER_FLAGS_LINEARSIZE 0x00080000u
#define DDS_DIMENSION_TEXTURE2D 3u

#define FOURCC(a, b, c, d)                                                                         \
    ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | ((uint32_t)(uint8_t)(c) << 16) |     \
     ((uint32_t)(uint8_t)(d) << 24))

/* the uncompressed layouts PCSX2 converts (DirectXTex's DDSPF_*) */
typedef enum DdsConv {
    DDS_CONV_NONE = 0, /* a BC format */
    DDS_CONV_A8R8G8B8,
    DDS_CONV_X8R8G8B8,
    DDS_CONV_X8B8G8R8,
    DDS_CONV_R8G8B8,
    DDS_CONV_A8B8G8R8
} DdsConv;

typedef struct DdsPixelFormat {
    uint32_t size, flags, fourcc, bits, r, g, b, a;
} DdsPixelFormat;

static const struct {
    DdsPixelFormat pf;
    DdsConv conv;
} s_layouts[] = {
    {{32, DDS_RGBA, 0, 32, 0x00ff0000u, 0x0000ff00u, 0x000000ffu, 0xff000000u}, DDS_CONV_A8R8G8B8},
    {{32, DDS_RGB, 0, 32, 0x00ff0000u, 0x0000ff00u, 0x000000ffu, 0}, DDS_CONV_X8R8G8B8},
    {{32, DDS_RGB, 0, 32, 0x000000ffu, 0x0000ff00u, 0x00ff0000u, 0}, DDS_CONV_X8B8G8R8},
    {{32, DDS_RGB, 0, 24, 0x00ff0000u, 0x0000ff00u, 0x000000ffu, 0}, DDS_CONV_R8G8B8},
    {{32, DDS_RGBA, 0, 32, 0x000000ffu, 0x0000ff00u, 0x00ff0000u, 0xff000000u}, DDS_CONV_A8B8G8R8},
};

static int refuse(const char *file, const char *why, TexpackImage *out)
{
    memset(out, 0, sizeof(*out));
    if (file != NULL) {
        fprintf(stderr, "textures: %s: %s; the game's texture is kept\n", file, why);
    }
    return -1;
}

static uint32_t blocks(uint32_t extent, uint32_t block)
{
    const uint32_t n = (extent + block - 1) / block;
    return n ? n : 1u;
}

/* one level's texels from the file (src, row pitch srcPitch) to RGBA8 rows
   of w * 4 bytes, as PCSX2's ConvertTexture_* leave them */
static void convertLevel(DdsConv conv, const uint8_t *src, uint32_t srcPitch, uint32_t w,
                         uint32_t h, uint8_t *dst)
{
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *s = src + (size_t)y * srcPitch;
        uint8_t *d = dst + (size_t)y * w * 4;
        for (uint32_t x = 0; x < w; x++, d += 4) {
            switch (conv) {
            case DDS_CONV_A8R8G8B8: /* B G R A in memory */
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                d[3] = s[3];
                s += 4;
                break;
            case DDS_CONV_X8R8G8B8: /* B G R X */
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                d[3] = 0xFF;
                s += 4;
                break;
            case DDS_CONV_X8B8G8R8: /* R G B X */
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = 0x80;
                s += 4;
                break;
            case DDS_CONV_R8G8B8: /* B G R */
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                d[3] = 0xFF;
                s += 3;
                break;
            default: /* A8B8G8R8: R G B A, as stored */
                memcpy(d, s, 4);
                s += 4;
                break;
            }
        }
    }
}

int texpack_load_dds(const uint8_t *data, size_t size, int bcSupported, const char *file,
                     TexpackImage *out)
{
    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (data == NULL || size < 4u + DDS_HEADER_BYTES || ico_le32(data) != DDS_MAGIC) {
        return refuse(file, "not a DDS file", out);
    }
    const uint8_t *hd = data + 4;
    const uint32_t hSize = ico_le32(hd + 0), flags = ico_le32(hd + 4);
    const uint32_t height = ico_le32(hd + 8), width = ico_le32(hd + 12);
    const uint32_t pitchOrLinear = ico_le32(hd + 16), mipCount = ico_le32(hd + 24);
    const DdsPixelFormat pf = {ico_le32(hd + 72), ico_le32(hd + 76), ico_le32(hd + 80),
                               ico_le32(hd + 84), ico_le32(hd + 88), ico_le32(hd + 92),
                               ico_le32(hd + 96), ico_le32(hd + 100)};
    if (hSize < DDS_HEADER_BYTES) {
        return refuse(file, "a damaged DDS header", out);
    }
    if (width == 0 || width >= DDS_MAX_TEXTURE_SIZE || height == 0 ||
        height >= DDS_MAX_TEXTURE_SIZE) {
        return refuse(file, "the DDS size is out of range", out);
    }
    if (texpack_max_side() && (width > texpack_max_side() || height > texpack_max_side())) {
        return refuse(file, "the picture is larger than this graphics card can show", out);
    }
    if (flags & DDS_HEADER_FLAGS_VOLUME) {
        return refuse(file, "a 3D DDS texture", out);
    }
    /* the full chain: PCSX2's CalcMipmapLevelsForReplacement */
    uint32_t full = 1;
    for (uint32_t m = width > height ? width : height; m > 1; m >>= 1) {
        full++;
    }
    uint32_t mips = 1;
    if (flags & DDS_HEADER_FLAGS_MIPMAP) {
        mips = mipCount != 0 ? mipCount : full;
    }

    size_t offset = 4u + DDS_HEADER_BYTES;
    uint8_t fmt = RD_TEXEL_RGBA8;
    DdsConv conv = DDS_CONV_NONE;
    uint32_t blockW = 1, blockBytes = 4;
    if (pf.flags & DDS_FOURCC) {
        uint32_t dxgi = 0;
        if (pf.fourcc == FOURCC('D', 'X', '1', '0')) {
            if (size < offset + DDS_DX10_BYTES) {
                return refuse(file, "a damaged DDS header", out);
            }
            const uint8_t *x = data + offset;
            if (ico_le32(x + 4) != DDS_DIMENSION_TEXTURE2D || ico_le32(x + 12) != 1) {
                return refuse(file, "not a single 2D DDS texture", out);
            }
            offset += DDS_DX10_BYTES;
            dxgi = ico_le32(x);
        }
        if (pf.fourcc == FOURCC('D', 'X', 'T', '1') || dxgi == 71) {
            fmt = RD_TEXEL_BC1;
        } else if (pf.fourcc == FOURCC('D', 'X', 'T', '2') ||
                   pf.fourcc == FOURCC('D', 'X', 'T', '3') || dxgi == 74) {
            fmt = RD_TEXEL_BC2;
        } else if (pf.fourcc == FOURCC('D', 'X', 'T', '4') ||
                   pf.fourcc == FOURCC('D', 'X', 'T', '5') || dxgi == 77) {
            fmt = RD_TEXEL_BC3;
        } else if (dxgi == 98) {
            fmt = RD_TEXEL_BC7;
        } else {
            return refuse(file,
                          "a DDS format packs do not use (BC1, BC2, BC3, BC7 and "
                          "uncompressed RGB/RGBA load)",
                          out);
        }
        if (!bcSupported) {
            return refuse(file, "this graphics card cannot draw compressed (BC) textures", out);
        }
        blockW = 4;
        blockBytes = rd__texel_block_bytes(fmt);
    } else {
        for (size_t i = 0; i < sizeof(s_layouts) / sizeof(s_layouts[0]); i++) {
            const DdsPixelFormat *l = &s_layouts[i].pf;
            if (pf.size == l->size && pf.flags == l->flags && pf.fourcc == l->fourcc &&
                pf.bits == l->bits && pf.r == l->r && pf.g == l->g && pf.b == l->b &&
                pf.a == l->a) {
                conv = s_layouts[i].conv;
                break;
            }
        }
        if (conv == DDS_CONV_NONE) {
            return refuse(file, "an uncompressed DDS layout packs do not use", out);
        }
        blockBytes = pf.bits / 8;
    }

    /* level 0's pitch: the header's (both flags set), else tight */
    const uint32_t bw0 = blocks(width, blockW), bh0 = blocks(height, blockW);
    const uint64_t tight0 = (uint64_t)bw0 * blockBytes;
    uint64_t pitch0 = tight0;
    if ((flags & DDS_HEADER_FLAGS_PITCH) && (flags & DDS_HEADER_FLAGS_LINEARSIZE)) {
        if (pitchOrLinear < blockBytes) {
            return refuse(file, "a damaged DDS header", out);
        }
        pitch0 = pitchOrLinear;
        if (pitch0 < tight0) {
            return refuse(file, "a damaged DDS header (row pitch)", out);
        }
    }
    if (offset >= size) {
        return refuse(file, "the DDS file holds no image", out);
    }
    if (blockW > 1 && (width % blockW != 0 || height % blockW != 0)) {
        /* PCSX2: "the width/height of the first mip level must be a
           multiple of 4" (D3D11 cannot make such a texture) */
        return refuse(file, "a compressed DDS texture must be a multiple of 4 wide and high", out);
    }

    /* the levels the file holds: level 0 whole, then 1 .. mips while
       complete, at most the full chain and TEXPACK_IMAGE_LEVELS */
    size_t srcOff[TEXPACK_IMAGE_LEVELS];
    uint32_t lw[TEXPACK_IMAGE_LEVELS], lh[TEXPACK_IMAGE_LEVELS];
    uint64_t srcPitch[TEXPACK_IMAGE_LEVELS], outSize[TEXPACK_IMAGE_LEVELS];
    uint32_t levels = 0;
    size_t pos = offset;
    uint64_t total = 0;
    for (uint32_t l = 0; l <= mips && l < full && l < TEXPACK_IMAGE_LEVELS; l++) {
        const uint32_t w = width >> l ? width >> l : 1u, h = height >> l ? height >> l : 1u;
        const uint32_t bh = l == 0 ? bh0 : blocks(h, blockW);
        const uint64_t sp = l == 0 ? pitch0 : (uint64_t)blocks(w, blockW) * blockBytes;
        const uint64_t need = sp * bh;
        if (need > size - pos) {
            if (l == 0) {
                return refuse(file, "the DDS file is cut short", out);
            }
            break; /* the file ends: the levels so far */
        }
        srcOff[l] = pos;
        srcPitch[l] = sp;
        lw[l] = w;
        lh[l] = h;
        outSize[l] = conv == DDS_CONV_NONE ? (uint64_t)blocks(w, blockW) * blockBytes * bh
                                           : (uint64_t)w * h * 4;
        total += outSize[l];
        pos += (size_t)need;
        levels++;
    }
    if (total > (uint64_t)SIZE_MAX) {
        return refuse(file, "the DDS image is too large", out);
    }
    uint8_t *blob = malloc((size_t)total);
    if (blob == NULL) {
        return refuse(file, "not enough memory for the DDS image", out);
    }
    uint8_t *dst = blob;
    for (uint32_t l = 0; l < levels; l++) {
        TexpackImageLevel *lv = &out->lv[l];
        const uint8_t *src = data + srcOff[l];
        lv->data = dst;
        lv->w = lw[l];
        lv->h = lh[l];
        lv->size = (size_t)outSize[l];
        if (conv == DDS_CONV_NONE) {
            const uint32_t bw = blocks(lw[l], blockW), bh = blocks(lh[l], blockW);
            lv->pitch = bw * blockBytes;
            for (uint32_t y = 0; y < bh; y++) {
                memcpy(dst + (size_t)y * lv->pitch, src + (size_t)(y * srcPitch[l]), lv->pitch);
            }
        } else {
            lv->pitch = lw[l] * 4;
            convertLevel(conv, src, (uint32_t)srcPitch[l], lw[l], lh[l], dst);
        }
        dst += lv->size;
    }
    out->fmt = fmt;
    out->w = width;
    out->h = height;
    out->levels = levels;
    out->blob = blob;
    out->bytes = (size_t)total;
    return 0;
}
