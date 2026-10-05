/* rd_state.h: the finite GS state the game actually uses.
 *
 * Everything here was enumerated from the sources (grep -a over ico2/, see
 * docs/port/RENDER_API.md section "GS state inventory" for the counts).  The
 * renderer builds pipelines from these enumerations, not from the full GS
 * register space.  Where the game can only reach a state through disc data
 * (the BGA lightning blend mode) the whole table is kept and the value is
 * range-checked at run time.
 *
 * The register encodings in the comments are the raw GS values the game
 * writes, so a reader can grep the call site and the enum in one go.
 */
#ifndef PORT_RENDER_RD_STATE_H
#define PORT_RENDER_RD_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ ALPHA
 * ALPHA = A | B<<2 | C<<4 | D<<6 | FIX<<32, out = ((A - B) * C >> 7) + D.
 * A, B, D: 0 = Cs, 1 = Cd, 2 = 0.  C: 0 = As, 1 = Ad, 2 = FIX.
 *
 * Twelve modes in GifPacket.c alphaTable[12] (gif_SetAlpha's mode
 * argument).  Modes 0..7 appear as literals; 8..11 (the Ad variants) and 3
 * are only reachable through the BGA lightning record (BgaLightningDef.c,
 * seki/src/BgAnimation.c) so every table entry is kept.  Material packets
 * (Packet.c pac_setMaterialPacket) use 0x44, 0x48, 0x42 with FIX 0x80.
 */
typedef enum RdBlend {
    RD_BLEND_CS_FIX_ADD_CD =
        0, /* 0x68: Cs*FIX + Cd        mode 0; shadow accumulate, dissolve in */
    RD_BLEND_CD_SUB_CS_FIX = 1, /* 0x62: Cd - Cs*FIX        mode 1; dissolve out */
    RD_BLEND_LERP_FIX = 2, /* 0x64: (Cs-Cd)*FIX + Cd   mode 2; letterbox, motion blur, AA, fades */
    RD_BLEND_CD_FIX_ADD_CS = 3, /* 0x61: Cd*FIX + Cs        mode 3; disc data only */
    RD_BLEND_LERP_AS = 4,       /* 0x44: (Cs-Cd)*As + Cd    mode 4; default material */
    RD_BLEND_CS_AS_ADD_CD = 5, /* 0x48: Cs*As + Cd         mode 5; additive, specular (with PABE) */
    RD_BLEND_CD_SUB_CS_AS = 6, /* 0x42: Cd - Cs*As         mode 6; subtractive material */
    RD_BLEND_LERP_AS_ALT = 7,  /* 0x44: same as mode 4     mode 7 */
    RD_BLEND_CS_AD_ADD_CD = 8, /* 0x58: Cs*Ad + Cd         disc data only */
    RD_BLEND_CD_SUB_CS_AD = 9, /* 0x52: Cd - Cs*Ad         disc data only */
    RD_BLEND_LERP_AD = 10,     /* 0x54: (Cs-Cd)*Ad + Cd    disc data only */
    RD_BLEND_CD_AS_ADD_CD = 11, /* 0x49: Cd*As + Cd         disc data only */
    RD_BLEND_COUNT = 12
} RdBlend;

/* FIX values seen as literals: 0, 0x10, 0x20, 0x40, 0x60, 0x70 (112), 96,
 * 0x80, 255, plus runtime values (motionBlurAlpha, shadow blend levels,
 * blurCol alpha, debug bar alpha).  FIX is 0..255 with 0x80 = 1.0 and is
 * passed through unchanged; the shader does the >>7. */

/* ------------------------------------------------------------------- TEST
 * TEST = ATE | ATST<<1 | AREF<<4 | AFAIL<<12 | DATE<<14 | DATM<<15 |
 *        ZTE<<16 | ZTST<<17.
 * Fourteen distinct values are written.  They decompose into the fields
 * below; the renderer keys pipelines on the decomposed fields, not the raw
 * value, but RD_TEST_* names the literals so call sites stay greppable.
 */
typedef enum RdAlphaTest {
    RD_ATST_NEVER = 0,
    RD_ATST_ALWAYS = 1,
    RD_ATST_LESS = 2,
    RD_ATST_LEQUAL = 3,
    RD_ATST_EQUAL = 4,
    RD_ATST_GEQUAL = 5,
    RD_ATST_GREATER = 6,
    RD_ATST_NOTEQUAL = 7
} RdAlphaTest;

typedef enum RdAFail {
    RD_AFAIL_KEEP = 0,    /* discard the fragment */
    RD_AFAIL_FB_ONLY = 1, /* write colour, not depth (two-draw split, see RENDER_API.md) */
    RD_AFAIL_ZB_ONLY = 2, /* not used by the game */
    RD_AFAIL_RGB_ONLY = 3 /* write RGB, not alpha, not depth */
} RdAFail;

typedef enum RdDate {
    RD_DATE_OFF = 0,
    RD_DATE_DEST_ALPHA_0 = 1, /* DATE=1 DATM=0: draw where destination alpha bit is 0 */
    RD_DATE_DEST_ALPHA_1 = 2  /* DATE=1 DATM=1: draw where destination alpha bit is 1 */
} RdDate;

typedef enum RdZTest {
    RD_ZTST_NEVER = 0, /* not used */
    RD_ZTST_ALWAYS = 1,
    RD_ZTST_GEQUAL = 2,
    RD_ZTST_GREATER = 3 /* not used */
} RdZTest;

/* The complete decomposed TEST state. */
typedef struct RdTestState {
    uint8_t ate;   /* 0/1 */
    uint8_t atst;  /* RdAlphaTest */
    uint8_t aref;  /* 0..255, GS alpha scale (0x80 = 1.0) */
    uint8_t afail; /* RdAFail */
    uint8_t date;  /* RdDate */
    uint8_t zte;   /* 0/1; ZTE=0 is documented as prohibited on the GS, treat as ALWAYS */
    uint8_t ztst;  /* RdZTest */
    uint8_t _pad;
} RdTestState;

/* The fourteen literal TEST values written by the game, as raw register
 * words.  rd_TestFromGs() decodes any of them. */
enum {
    RD_TEST_Z_ALWAYS = 0x30000, /* ZTE, ZTST ALWAYS                       (20 sites; 2D, post) */
    RD_TEST_Z_GEQUAL =
        0x50000, /* ZTE, ZTST GEQUAL                       (13; lists 0,6,7-12 default) */
    RD_TEST_AT_GT0_Z_GEQUAL = 0x5000D, /* ATE GREATER aref 0, Z GEQUAL           (10) */
    RD_TEST_AT_GT64_FBONLY =
        0x5140D, /* ATE GREATER aref 0x40 AFAIL FB_ONLY, Z GEQUAL (lists 1,2 default) */
    RD_TEST_DATE1_Z_GEQUAL =
        0x5C000, /* DATE=1 DATM=1, Z GEQUAL                (list 4 default: specular) */
    RD_TEST_AT_GT0_DATE0 =
        0x3400D,             /* ATE GREATER aref 0, DATE=1 DATM=0, Z ALWAYS (shadow composite) */
    RD_TEST_DATE0 = 0x34000, /* DATE=1 DATM=0, Z ALWAYS                (aura) */
    RD_TEST_AT_ALWAYS_DATE0 = 0x34003, /* ATE ALWAYS, DATE=1 DATM=0, Z ALWAYS    (aura) */
    RD_TEST_RGBONLY_DATE1 = 0x3F000,   /* AFAIL RGB_ONLY (ATE off), DATE=1 DATM=1, Z ALWAYS */
    RD_TEST_NEVER_RGBONLY_DATE1 =
        0x3F001, /* ATE NEVER AFAIL RGB_ONLY, DATE=1 DATM=1, Z ALWAYS (RGB mask where dest alpha set) */
    RD_TEST_AT_LT129 = 0x30815,         /* ATE LESS aref 0x81, Z ALWAYS            (flare) */
    RD_TEST_Z_ALWAYS_ATST_GT = 0x3000C, /* ATE off (ATST bits set, no effect), Z ALWAYS */
    RD_TEST_NEVER_RGBONLY =
        0x33001,      /* ATE NEVER AFAIL RGB_ONLY, Z ALWAYS     (darkVolume: RGB-only pass) */
    RD_TEST_OFF = 0x0 /* everything off; one site */
};

static inline RdTestState rd_TestFromGs(uint64_t v)
{
    RdTestState s;
    s.ate = (uint8_t)(v & 1);
    s.atst = (uint8_t)((v >> 1) & 7);
    s.aref = (uint8_t)((v >> 4) & 0xFF);
    s.afail = (uint8_t)((v >> 12) & 3);
    s.date =
        (uint8_t)(((v >> 14) & 1) ? (((v >> 15) & 1) ? RD_DATE_DEST_ALPHA_1 : RD_DATE_DEST_ALPHA_0)
                                  : RD_DATE_OFF);
    s.zte = (uint8_t)((v >> 16) & 1);
    s.ztst = (uint8_t)((v >> 17) & 3);
    s._pad = 0;
    return s;
}

/* ------------------------------------------------------------------- ZBUF
 * ZBUF = ZBP | PSM<<24 | ZMSK<<32.  The game writes 0x300000C0 (write on)
 * and 0x1300000C0 (write off) at ZBP 0xC0, PSMZ32; temp targets use their
 * own ZBP.  Only the mask matters to the port. */
typedef enum RdZWrite { RD_ZWRITE_ON = 0, RD_ZWRITE_OFF = 1 } RdZWrite;

/* ------------------------------------------------------------------- TEXA
 * TEXA = TA0 | AEM<<15 | TA1<<32.  Three modes seen.  TEXA decides the
 * alpha of PSMCT16/PSMCT24 texels, so the texture cache bakes one variant
 * per (texture, TEXA mode) for those formats; RGBA32 textures ignore it. */
typedef enum RdTexA {
    RD_TEXA_80_80 = 0,     /* 0x8000000080: TA0 0x80, AEM 0, TA1 0x80  (lists 0,4,6,7-12 default) */
    RD_TEXA_7F_81_AEM = 1, /* 0x810000807F: TA0 0x7F, AEM 1, TA1 0x81  (lists 1,2 default) */
    RD_TEXA_80_80_AEM = 2, /* 0x8000008080: TA0 0x80, AEM 1, TA1 0x80  (2D sprites) */
    RD_TEXA_COUNT
} RdTexA;

/* ------------------------------------------------------------------ CLAMP
 * CLAMP = WMS | WMT<<2 (REGION_* fields unused).  Values 0,1,4,5 seen:
 * materials map PObjMatDef.wrap 0..3 to 5,4,1,0 (Packet.c), raw sites
 * write 0, 1, 5. */
typedef enum RdWrap { RD_WRAP_REPEAT = 0, RD_WRAP_CLAMP = 1 } RdWrap;

typedef struct RdSamplerWrap {
    uint8_t s; /* RdWrap: CLAMP bits 0-1 */
    uint8_t t; /* RdWrap: CLAMP bits 2-3 */
} RdSamplerWrap;

static inline RdSamplerWrap rd_WrapFromGs(uint64_t clamp)
{
    RdSamplerWrap w;
    w.s = (uint8_t)((clamp & 3) ? RD_WRAP_CLAMP : RD_WRAP_REPEAT);
    w.t = (uint8_t)(((clamp >> 2) & 3) ? RD_WRAP_CLAMP : RD_WRAP_REPEAT);
    return w;
}

/* ------------------------------------------------------------------- TEX1
 * TEX1 = LCM | MXL<<2 | MMAG<<5 | MMIN<<6 | MTBA<<9 | L<<19 | K<<32.
 * Values seen: 0x60 (MMAG linear, MMIN linear: 28 sites), 0x40 (nearest mag,
 * linear min: 3), 0x20 (linear mag, nearest min: 1), 0 (nearest: via
 * materials/TexExt).  Per-texture filtering comes from the TIM2 ICO block
 * (smpMag/smpMin) in Texture.c and uses the same two filters. */
typedef enum RdFilter { RD_FILTER_NEAREST = 0, RD_FILTER_LINEAR = 1 } RdFilter;

/* ------------------------------------------------------------ TEX0 / TCC
 * Texture function (TFX, TEX0 bits 35-36) and TCC (bit 34).  Only MODULATE
 * and DECAL appear (0x664...800 family = DECAL/RGBA from Texture.c; material
 * TEX0 from TexExt.texFnc).  HIGHLIGHT variants are not used. */
typedef enum RdTexFn { RD_TEXFN_MODULATE = 0, RD_TEXFN_DECAL = 1 } RdTexFn;

typedef enum RdTcc { RD_TCC_RGB = 0, RD_TCC_RGBA = 1 } RdTcc;

/* ------------------------------------------------------ PABE FBA COLCLAMP */
/* PABE: per-pixel alpha blend enable (blend only where As MSB set).  Seen
 * 0 (9 sites) and 1 (1 site: specular/reflection passes). */
/* FBA: force alpha MSB on write.  Materials set it from PObjMatDef.fbaOff;
 * raw sites write 0.  Used by the shadow receiver mask (DATE consumers). */
/* COLCLAMP: 1 everywhere except shadow accumulation (Shadow.c), which sets 0
 * so the additive count wraps; the port implements that pass with stencil. */

/* ----------------------------------------------------------- draw state
 * The full state block a list replays against.  rd_core keeps one of these
 * per frame; lists mutate it in replay order 0..12 and state leaks between
 * lists exactly as it does on the GS (see rd.h, "Lists"). */
typedef struct RdDrawState {
    RdTestState test;
    uint8_t blend;    /* RdBlend */
    uint8_t blendFix; /* 0..255 */
    uint8_t
        abe; /* ALPHA blending enabled for the primitive (PRIM.ABE / gif_SetAlpha's first arg) */
    uint8_t pabe;      /* 0/1 */
    uint8_t fba;       /* 0/1 */
    uint8_t zwrite;    /* RdZWrite */
    uint8_t colclamp;  /* 0/1 */
    uint8_t texa;      /* RdTexA */
    uint8_t texFn;     /* RdTexFn */
    uint8_t tcc;       /* RdTcc */
    uint8_t magFilter; /* RdFilter */
    uint8_t minFilter; /* RdFilter */
    RdSamplerWrap wrap;
    uint8_t colorMask;  /* bits 0..3 = R G B A; derived from AFAIL RGB_ONLY and FRAME.FBMSK */
    uint8_t texEnabled; /* PRIM.TME */
    uint32_t
        fbmsk; /* FRAME.FBMSK as written (0 everywhere except darkVolume's 0x4C target switch) */
} RdDrawState;

/* Pipeline key: what actually selects a GPU pipeline.  Everything in
 * RdDrawState that is not a pipeline-level property (aref, blendFix,
 * sampler settings) goes in a uniform or sampler object instead.  Fewer
 * than 100 distinct keys are expected; rd_core asserts if the cache grows
 * past RD_PIPELINE_CACHE_MAX. */
typedef struct RdPipelineKey {
    uint8_t program; /* RdProg (rd.h) */
    uint8_t blend;   /* RdBlend, or RD_BLEND_COUNT when abe == 0 */
    uint8_t atst;    /* RdAlphaTest, RD_ATST_ALWAYS when ate == 0 */
    uint8_t
        afailSplit; /* 0 = none, 1 = pass A (alpha > ref, Z write), 2 = pass B (alpha <= ref, no Z write) */
    uint8_t date;   /* RdDate */
    uint8_t ztst;   /* RdZTest */
    uint8_t zwrite; /* RdZWrite */
    uint8_t pabe;
    uint8_t fba;
    uint8_t colorMask;
    uint8_t stencil;   /* RdStencilMode (rd.h): shadow volumes only */
    uint8_t targetFmt; /* RdTargetFormat (rd.h) */
    uint8_t prim;      /* RdPrim topology (rd.h) */
    uint8_t _pad[3];
} RdPipelineKey;

#define RD_PIPELINE_CACHE_MAX 256

/* GS register numbers the game writes through gif_SetGsReg, for the
 * transitional rd_gs_shim.c and for assertions. */
enum RdGsReg {
    RD_GS_PRIM = 0x00,
    RD_GS_RGBAQ = 0x01,
    RD_GS_ST = 0x02,
    RD_GS_UV = 0x03,
    RD_GS_XYZF2 = 0x04,
    RD_GS_XYZ2 = 0x05,
    RD_GS_TEX0_1 = 0x06,
    RD_GS_CLAMP_1 = 0x08,
    RD_GS_XYZF3 = 0x0C,
    RD_GS_XYZ3 = 0x0D,
    RD_GS_TEX1_1 = 0x14,
    RD_GS_TEX2_1 = 0x16,
    RD_GS_XYOFFSET_1 = 0x18,
    RD_GS_PRMODECONT = 0x1A,
    RD_GS_PRMODE = 0x1B,
    RD_GS_TEXCLUT = 0x1C,
    RD_GS_SCANMSK = 0x22,
    RD_GS_MIPTBP1_1 = 0x34,
    RD_GS_MIPTBP2_1 = 0x36,
    RD_GS_TEXA = 0x3B,
    RD_GS_FOGCOL = 0x3D,
    RD_GS_TEXFLUSH = 0x3F,
    RD_GS_SCISSOR_1 = 0x40,
    RD_GS_ALPHA_1 = 0x42,
    RD_GS_DIMX = 0x44,
    RD_GS_DTHE = 0x45,
    RD_GS_COLCLAMP = 0x46,
    RD_GS_TEST_1 = 0x47,
    RD_GS_PABE = 0x49,
    RD_GS_FBA_1 = 0x4A,
    RD_GS_FRAME_1 = 0x4C,
    RD_GS_ZBUF_1 = 0x4E,
    RD_GS_BITBLTBUF = 0x50,
    RD_GS_TRXPOS = 0x51,
    RD_GS_TRXREG = 0x52,
    RD_GS_TRXDIR = 0x53
};

#ifdef __cplusplus
}
#endif

#endif /* PORT_RENDER_RD_STATE_H */
