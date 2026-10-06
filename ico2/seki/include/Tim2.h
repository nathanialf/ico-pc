/*
 * ico2/seki/include/Tim2.h
 *
 * The TIM2 file headers Texture.c reads off the disc image in memory
 * (moved here from Texture.c for the PC port's layout asserts; renderer
 * wave 2, package R2b).  Both are disc records: frozen on every host
 * (config/struct_classes.txt, class overlay).  The 64-bit register fields
 * sit on 8-byte boundaries in the file; the host spells the 8-byte
 * alignment out, since a 4-byte-aligned ABI would give a long long member only 4.
 */

#ifndef TIM2_H
#define TIM2_H
#define TIM2_U64 unsigned long long __attribute__((aligned(8)))

/* the TIM2 picture header, the 0x30 bytes after the 16-byte file header.
 * The fields Texture.c reads off it are clutColors at 0x0E, clutType at
 * 0x12 (masked with 0x3F where the compound bits have to go), imageType at
 * 0x13 and the width and height at 0x14 and 0x16. */
typedef struct Tim2Picture {      /* field names derived */
    unsigned int totalSize;       /* 0x00 */
    unsigned int clutSize;        /* 0x04 */
    unsigned int imageSize;       /* 0x08 */
    unsigned short headerSize;    /* 0x0C */
    unsigned short clutColors;    /* 0x0E */
    unsigned char picFormat;      /* 0x10 */
    unsigned char mipMapTextures; /* 0x11 */
    unsigned char clutType;       /* 0x12 */
    unsigned char imageType;      /* 0x13 */
    unsigned short imageWidth;    /* 0x14 */
    unsigned short imageHeight;   /* 0x16 */
    TIM2_U64 GsTex0;              /* 0x18 */
    TIM2_U64 GsTex1;              /* 0x20 */
    unsigned int GsRegs;          /* 0x28 */
    unsigned int GsTexClut;       /* 0x2C */
} Tim2Picture;                    /* derived name */

/* the TIM2 mipmap header that follows the picture header when there is more
 * than one level, two MIPTBP registers and then one image size per level.
 * tex_makeTexturePacket copies 0x30 bytes of picture header into the record
 * and a second 0x30 bytes of mipmap header after it, and steps over a variable
 * number of size words through the mipmap_header_size table before it reaches
 * the ICO block. */
typedef struct Tim2Mipmap { /* field names derived */
    TIM2_U64 GsMiptbp1;     /* 0x00 */
    TIM2_U64 GsMiptbp2;     /* 0x08 */
    unsigned int sizes[8];  /* 0x10 */
} Tim2Mipmap;               /* derived name */

#endif /* TIM2_H */
