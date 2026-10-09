/* rd_internal.h: what the rd implementation files share, plus the hooks the
 * tests and tools use.  Nothing in ico2/ includes this header.
 *
 * Files
 * -----
 *   rd_core.c      recording: the 13 lists, payload arena, state deltas,
 *                  named and temporary targets, texture registry, frame
 *                  retention, the state walk that replay and tests share
 *   rd_post.c      rd_Post: the post kinds as the GS register writes and
 *                  sprites the original routines (GsBase.c) issue
 *   rd_pipeline.c  pipeline key derivation from the state block, the cache,
 *                  the enumeration of the reachable pipelines
 *   rd_replay.c    an RdFrame onto the RHI
 *   rd_present.c   DISPLAY to the output (Original preset)
 *   rd_frame.c     the flip's frame head, per-target Z scale, VU block, camera
 *                  into FrameCB (wave 2, R2c)
 *   rd_shadow.c    the shadow count's recording (wave 4, R4b); replayed by
 *                  rd_replay.c (doShadow*)
 *   (fog)          RD_POST_FOG (wave 4, R4c): recorded by rd_post.c as an
 *                  RDC_POST_STUB, replayed by rd_replay.c (doFog) through
 *                  fog_lut_ps
 *   rd_blur.c      staticBlur.c's sprites, RD_POST_MOTION_BLUR ..
 *                  RD_POST_EYE_BLUR (wave 5, R5a): recorded as RDC_POST_STUBs,
 *                  replayed by rd_replay.c (doBlurSprite) through fx_sprite_ps;
 *                  the work-buffer scale rule
 *   rd_dump.c      frame dump and load
 *   rd_png.c       a minimal PNG writer for the replay tool and tests
 *
 * Commands
 * --------
 * A list is an array of fixed-size RdCmd records; variable-length data
 * (vertices, transforms, post parameters) lives in the frame's payload arena
 * and is addressed by byte offset, so the arena can grow and the frame can
 * be written to a file as it is.  Every field has a fixed width and RdCmd
 * has no pointers or implicit padding, so a dump reads the same on the
 * 32-bit and 64-bit builds.
 */
#ifndef PORT_RENDER_RD_INTERNAL_H
#define PORT_RENDER_RD_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rd.h"
#include "rhi.h"

#ifdef __cplusplus

extern "C" {
#endif

/* ------------------------------------------------------------- commands */
typedef enum RdCmdType {
    RDC_NOP = 0,
    /* state deltas: change the replay state block, draw nothing */
    RDC_TEST,        /* b[0..6] ate atst aref afail date zte ztst */
    RDC_BLEND,       /* b[0] RdBlend, b[1] FIX, b[2] abe */
    RDC_ABE,         /* b[0] abe only (PRIM.ABE of a post sprite; ALPHA left as is) */
    RDC_ZWRITE,      /* b[0] RdZWrite */
    RDC_FBA,         /* b[0] */
    RDC_PABE,        /* b[0] */
    RDC_COLCLAMP,    /* b[0] */
    RDC_TEXA,        /* b[0] RdTexA */
    RDC_FILTER,      /* b[0] mag, b[1] min (TEX1) */
    RDC_WRAP,        /* b[0] s, b[1] t (CLAMP) */
    RDC_TEXTURE,     /* u[0] RdTex id, b[0] RdTexFn, b[1] RdTcc; TME on */
    RDC_TEXTURE_OFF, /* TME off */
    RDC_UVOFFSET,    /* f[0], f[1] */
    RDC_COLORMASK,   /* u[0] FRAME.FBMSK */
    RDC_TARGET,      /* u[0] colour target id, u[1] depth target id, u[2] gsW | gsH << 16,
                      * b[0] useOffset; also resets the scissor to gsW x gsH */
    RDC_SCISSOR,     /* u[0..3] x0 y0 x1 y1, GS pixels, inclusive (SCISSOR_1) */
    RDC_ALPHA,       /* b[0] RdBlend, b[1] FIX; ABE untouched (wave 2: raw ALPHA writes) */
    RDC_SHADE,       /* b[0] PRIM.IIP: 1 Gouraud, 0 flat (wave 2) */
    RDC_STATE_LAST = RDC_SHADE,
    /* actions */
    RDC_CLEAR,       /* u[0] target, b[0..3] rgba, b[4] clearDepth, u[1] GS z */
    RDC_SCREEN,      /* b[0] RdPrim, b[1] RdSpace, b[2] uvFixed; u[0] payload offset of
                      * RdScreenVtx[u[1]] */
    RDC_EXACT_BLEND, /* u[0] src target, u[1] dst target: the GS integer blend of the state
                      * block's ALPHA/FIX/COLCLAMP through blend_int (RdPostParams.exactInt) */
    RDC_COPY,        /* u[0] src target, u[1] dst target, u[2] payload offset of RdCopyRec */
    /* the VU draws (wave 3), the world prims (recorded only: replay calls
     * rd__NotImplemented), the shadow count (wave 4) and the post records */
    RDC_MESH,         /* u[0] mesh, b[0] RdProg, u[1] payload offset, u[2] payload size */
    RDC_SKINNED,      /* same, plus bones in the payload */
    RDC_GRID,         /* u[1] payload offset, u[2] size */
    RDC_PARTICLES,    /* u[1] payload offset, u[2] size */
    RDC_WORLD_PRIMS,  /* b[0] RdPrim, u[0] count, u[1] payload offset, u[2] size */
    RDC_SHADOW_STRIP, /* b[0] 0: rd_ShadowStrip, u[0] count, u[1] payload offset of float[4]
                       * x count, f[0] sign; b[0] RD_SHADOW_TRIS (wave 4, R4b): rd_ShadowTris,
                       * u[1] payload offset of RdScreenVtx[u[0] + u[3]], the triangles that
                       * increment (u[0] vertices) then those that decrement (u[3]) */
    RDC_POST_STUB,    /* b[0] RdPostKind, u[1] payload offset of RdPostRec; since wave 4
                       * (R4c) RD_POST_FOG is replayed (doFog), the other kinds are not */
    /* wave 4 (R4b), rd_shadow.c: on the state block's colour and depth targets */
    RDC_SHADOW_RESET,   /* the depth target's stencil to 0 */
    RDC_SHADOW_RESOLVE, /* the stencil count into the colour target (rd.h rd_ShadowResolve) */
    /* package AA1 (dump version 4): a state delta numbered after the actions,
     * so a version 3 dump's command numbers keep their meaning
     * (rd__CmdIsState) */
    RDC_AA1, /* b[0] PRIM.AA1 */
    /* package DEF (dump version 5): an action replay draws nothing for; the
     * present collects it (rd_present.c).  b[0] RD_OTEXT_ITEM: u[1] payload offset of an RdTextItem
     * (rd.h), u[2] its size; b[0] RD_OTEXT_OP: a post pass after text (b[1]
     * RdPostKind), u[1] offset of an RdTextOp, u[2] its size.  Keyed */
    RDC_OVERLAY_TEXT,
    RDC_COUNT
} RdCmdType;

typedef struct RdCmd {
    uint8_t type; /* RdCmdType */
    uint8_t b[7];
    uint32_t keyLo, keyHi; /* RdKey of a draw, 0 for state */
    uint32_t u[4];
    float f[2];
} RdCmd;

_Static_assert(sizeof(RdCmd) == 40, "RdCmd is dumped as raw bytes");

/* RDC_OVERLAY_TEXT's b[0] (package DEF). */
enum { RD_OTEXT_ITEM = 0, RD_OTEXT_OP = 1 };

/* RDC_SCREEN's b[3] (package DEF): the draw is the glyph quads of the last
 * RDC_OVERLAY_TEXT item (rd.h rd_DeferredTextQuads), skipped by a replay that
 * draws the items deferred; 0 in every other draw and every older dump. */
#define RD_SCREEN_TEXT_QUADS 1

/* A post pass recorded after deferred text (rd_post.c), as the present folds
 * it into the items before it: FADE and BRIGHTNESS lerp the whole frame to
 * rgb by rgba[3] / 128, LETTERBOX lerps two bands of `lines` lines to black
 * by fix / 128, KEEP replaces the scene (the items before it are gone),
 * REDUCTION scales it by rgba / 128 (the tint). */
typedef struct RdTextOp {
    uint8_t rgba[4];
    uint8_t fix, pad[3];
    uint32_t lines;
} RdTextOp;

_Static_assert(sizeof(RdTextItem) == 304, "RdTextItem is dumped as raw bytes");
_Static_assert(sizeof(RdTextOp) == 12, "RdTextOp is dumped as raw bytes");

/* Whether a command is a state delta (rd__ApplyState) rather than an action. */
static inline int rd__CmdIsState(uint8_t type)
{
    return type <= RDC_STATE_LAST || type == RDC_AA1;
}

/* RDC_SHADOW_STRIP's b[0] (wave 4, R4b). */
#define RD_SHADOW_TRIS 1
/* The stencil bits the shadow count keeps: n mod 64, as 4 n mod 256 wraps. */
#define RD_SHADOW_STENCIL_MASK 0x3Fu
/* Package V3: rd_ShadowTris tags every vertex with the place (1-based) its
 * triangle had in the call, in RdScreenVtx.rgba (little-endian; the volume
 * draw writes no colour), since the split into increments and decrements
 * loses the order; 0 is untagged (a dump recorded before V3).  rd_interp.c
 * regroups Shadow.c's triangles into their prisms by it: emitVolumeStrip's
 * ten positions over six vertices kick eight triangles per strip. */
#define RD_SHADOW_PRISM_TRIS 8

static inline uint32_t rd__ShadowTag(const RdScreenVtx *v)
{
    return (uint32_t)v->rgba[0] | (uint32_t)v->rgba[1] << 8 | (uint32_t)v->rgba[2] << 16 |
           (uint32_t)v->rgba[3] << 24;
}

static inline void rd__SetShadowTag(RdScreenVtx *v, uint32_t tag)
{
    v->rgba[0] = (uint8_t)tag;
    v->rgba[1] = (uint8_t)(tag >> 8);
    v->rgba[2] = (uint8_t)(tag >> 16);
    v->rgba[3] = (uint8_t)(tag >> 24);
}

/* RD_POST_COPY's rectangle. */
typedef struct RdCopyRec {
    int32_t srcX, srcY, dstX, dstY;
    uint32_t w, h;
} RdCopyRec;

/* RdPostParams without the pointer, as a stubbed post kind records it. */
typedef struct RdPostRec {
    uint32_t src, dst, srcView;
    uint8_t rgba[4];
    uint8_t fix, blend, abe, exactInt;
    uint32_t z;
    float rect[4];
    float uv[4];
    float scalar[4];
    uint32_t lines;
    uint32_t lutOffset; /* payload offset of 256 x RGBA, or ~0u */
} RdPostRec;

_Static_assert(sizeof(RdPostRec) == 80, "RdPostRec is dumped as raw bytes");

/* ---------------------------------------------------------- state block
 * The persistent GS state the lists replay against.  Fixed-width fields
 * only: it is part of the frame dump. */
typedef struct RdStateBlock {
    RdDrawState ds;     /* rd_state.h: TEST, ALPHA, ZBUF mask, FBA, PABE, ... */
    uint32_t tex;       /* RdTex id of the last RDC_TEXTURE */
    float uvOffset[2];  /* rd_UVOffset: normalised texture units (tex_TransTexture uOfs/vOfs) */
    uint32_t color;     /* RdTarget id (FRAME_1) */
    uint32_t depth;     /* RdTarget id whose depth buffer is bound (ZBUF_1), 0 = none */
    uint32_t gsW, gsH;  /* the size the caller set with the target */
    uint32_t useOffset; /* add the preset's field offset to XYOFFSET */
    int32_t scissor[4]; /* x0, y0, x1, y1 inclusive */
    uint32_t gouraud;   /* PRIM.IIP (rd_Gouraud), 1 = Gouraud */
    uint32_t aa1;       /* PRIM.AA1 (rd_AA1), package AA1; a version 3 dump loads it as 0 */
} RdStateBlock;

_Static_assert(sizeof(RdDrawState) == 28, "RdDrawState layout");
_Static_assert(sizeof(RdStateBlock) == 84, "RdStateBlock is dumped as raw bytes");
/* The state block of a version 3 dump: the same fields without aa1. */
#define RD_STATE_BLOCK_V3_SIZE 80u

/* Applies a state command to s.  Returns false (s untouched) for actions. */
bool rd__ApplyState(RdStateBlock *s, const RdCmd *c);
/* The state block a fresh GS starts with (rd_Init). */
void rd__ResetStateBlock(RdStateBlock *s);

/* --------------------------------------------------------------- frames */
typedef struct RdCmdList {
    RdCmd *cmds;
    uint32_t count, cap;
} RdCmdList;

#define RD_MAX_TEMP_PER_FRAME 32
/* Wave 7 (R7b): the frames kept: the one being recorded, the last closed
 * (current) and the one before it (previous), which the interpolation
 * blends while the next is recorded (rd_interp.c).  Wave 1 kept two: the
 * recording reused the previous frame's slot. */
#define RD_FRAME_RING 3

typedef struct RdFrame {
    RdCmdList lists[RD_LIST_COUNT];
    uint8_t *payload;
    uint32_t payloadSize, payloadCap;
    RdStateBlock startState; /* the state the previous frame's replay left */
    RdStateBlock endState;   /* filled when the frame closes */
    RdCamera camera;
    uint32_t hasCamera;
    uint32_t number; /* 1, 2, ... in recording order; 0 = never recorded */
    uint32_t keep;   /* rd_EndFrame(keep) */
    uint32_t closed;
    uint32_t gsW, gsH;
    uint32_t tempTargets[RD_MAX_TEMP_PER_FRAME]; /* owned, freed when the frame is reused */
    uint32_t tempCount;
    /* wave 2 (R2c), rd_frame.c; not dumped */
    RdVuCommon vu; /* gsb_MakeCommonMatrix's block, the last one recorded */
    uint32_t hasVu;
    /* rd_FrameHead's two copies: [0] at the head of list 0, [1] of list 11;
     * command index ranges [start, end) and the index of each RDC_CLEAR and
     * RDC_TARGET; headValid 0 = no head recorded */
    uint32_t headValid;
    uint32_t headStart[2], headEnd[2], headClear[2], headTarget[2];
    /* wave 7 (R7b), rd_interp.c; not dumped: rd_CameraCut while the frame
     * was open (rd_EndFrame copies it into camera.cut), and the strongest
     * fade rd_Post(RD_POST_FADE) recorded, 1 + its alpha (0: none) */
    uint32_t cut;
    uint32_t fade;
    /* package DEF; not dumped: RDC_OVERLAY_TEXT items recorded so far (the
     * post passes record their ops only after one) */
    uint32_t textItems;
} RdFrame;

void rd__FrameReset(RdFrame *f);
void rd__FrameFree(RdFrame *f);
/* Appends size bytes (8-aligned) to the payload; returns the offset. */
uint32_t rd__FramePayload(RdFrame *f, const void *data, uint32_t size);

/* The first and last list a frame replays: 0..12, or 11..12 with keep. */
static inline int rd__FirstList(int keep)
{
    return keep ? 11 : 0;
}

/* Walks the replayed lists of f in order, applying state commands to *state
 * (which the caller seeds, normally with f->startState) and calling fn for
 * every command after it is applied.  fn may be NULL. */
typedef void (*RdWalkFn)(void *user, int list, uint32_t index, const RdCmd *cmd,
                         const RdStateBlock *state);
void rd__Walk(const RdFrame *f, int keep, RdStateBlock *state, RdWalkFn fn, void *user);

/* ------------------------------------------------------------- targets
 * Ids: named target n (RdTargetId) is n + 1; temporary targets are
 * (generation << 16) | (slot + 1) with slot >= RD_TARGET_COUNT. */
#define RD_MAX_TARGETS 96

typedef struct RdTargetRec {
    uint32_t gen;
    uint8_t live, named, withDepth, keepAcross;
    uint32_t w, h; /* GS pixels */
    /* Wave 7 (R7a): the texture's size and its texels per GS pixel
     * (tw = w * sx rounded, th = h * sy); tw == w, th == h, sx == sy == 1
     * in Original and for every target the Enhanced resolution does not
     * scale (rd__TargetScaleOf) */
    uint32_t tw, th;
    float sx, sy;
    uint8_t wide; /* scene-class: draws other than full-screen ones take the wide x scale */
    /* Widescreen reflections: a render-to-texture block with its own depth
     * buffer (rd_BlockTarget, the decoder's blocks) widened by the display
     * aspect: tw = w / f texels for f = (4/3) / aspect, wide set, so its 3D
     * view shows what the wide scene shows; the draws that sample it scale
     * their x addressing by f (fillDrawCB, DrawCB.g_scale.zw).  0 at 4:3
     * and in Original */
    uint8_t wideBlock;
    RhiFormat format;
    RhiTexture color, depth;
    RhiState colorState, depthState;
    RhiTexture snap; /* copy used when a draw samples the target it renders to */
    RhiState snapState;
    /* the texels of snap copied from the target by the latest copy (the
     * rest holds older content): a draw reads snap only inside it */
    RhiRect snapRect;
    uint32_t snapFor;    /* replay counter at which snap was taken */
    uint32_t viewTex[3]; /* RdTex ids of rd_TargetTexture, per RdTexView */
    uint32_t ownerFrame;
    uint8_t zFormat; /* RdZFormat of the depth buffer (R2c); 0 = PSMZ32 */
    /* package P1: a freed temporary target kept with its textures for the
     * next of its size (rd__TempTargetAlloc); a taken one is cleared
     * (rd_replay.c clearNewTargets: colour 0, depth 1.0) at the next replay
     * (clearPending) */
    uint8_t parked, clearPending;
} RdTargetRec;

/* The texels a screen-prim command can touch on the bound target tc: the
 * bounding box of its vertices (12.4, after XYOFFSET) padded by two GS
 * pixels (three with PRIM.AA1's edge fringes), through the same wide x
 * scale, mirror and texel scale as the draw, intersected with the scissor
 * bindDraw sets.  stretch, uiPrim and mirror are the draw's (rd_replay.c
 * doScreen).  False when it covers nothing.  The DATE snapshot copies only
 * this area (rd_replay.c dateSnapshot). */
bool rd__ScreenArea(const RdTargetRec *tc, const RdStateBlock *st, const RdScreenVtx *v, uint32_t n,
                    int stretch, int uiPrim, int mirror, int aa1, RhiRect *area);
/* Whether the DATE snapshot must be (re)taken for a draw on target tcId at
 * write serial serial that reads area want (texels; NULL = the whole tw x
 * th target), given the snapshot held (for target heldFor at serial
 * heldSerial, area held).  When it must, *take is the area to copy: want
 * for a new target or serial, the whole target for a second area in the
 * same target and serial (at most one retake more than a whole-target
 * snapshot would need). */
bool rd__DateRetake(uint32_t heldFor, uint32_t heldSerial, const RhiRect *held, uint32_t tcId,
                    uint32_t serial, const RhiRect *want, uint32_t tw, uint32_t th, RhiRect *take);

/* GS Z to depth scale of a target id's depth buffer: PSMZ32, the default
 * (also for an unknown id), is 2^-33 on a float depth buffer (gs_math.hlsli
 * GS_ZSCALE_32F: the top Z values mapped apart) and 2^-32 on D24S8. */
float rd__TargetZScale(uint32_t id);
/* gs_z_to_depth on the CPU (clears): the same formula as gs_math.hlsli. */
float rd__GsDepth(uint32_t z, float scale);

RdTargetRec *rd__TargetRec(uint32_t id);
/* Creates the GPU textures of a target record (no-op without a device). */
bool rd__TargetCreateGpu(RdTargetRec *t, const char *name);
void rd__TargetDestroyGpu(RdTargetRec *t);
/* Allocates a temp target record (dump loading uses it too). */
uint32_t rd__TempTargetAlloc(uint32_t w, uint32_t h, int withDepth, int keepAcross);
void rd__TempTargetFree(uint32_t id);
/* Package P1: destroys the parked temporary targets' textures (a scale
 * change, shutdown). */
void rd__TempTargetPoolClear(void);
#define RD_TEMP_PARKED 24 /* parked temporary targets kept at most */

/* ------------------------------------------------------------ textures */
#define RD_MAX_TEXTURES 8192

enum { RD_TEXKIND_IMAGE = 1, RD_TEXKIND_TARGET = 2 };

/* Package R8: an image texture's texel format (RdTexRec.format; the dump
 * writes it in the image's view word, so 0 is what every older dump holds).
 * RGBA8: rd_CreateTexture/rd_CreateTextureSrc, 4 bytes a texel.  R8:
 * rd_CreateTextureR8, 1 byte (coverage in GS alpha units), drawn with font_ps
 * (rd__PlanScreenDraw selects RD_FS_FONT for it) and never given mips.
 * SHEET (v0.4.2, package F-A): rd_CreateTextureSheet, 1 byte (coverage
 * 0..255) and RdTexRec.sheet, drawn with font_sheet_ps (RD_FS_FONT_SHEET),
 * never given mips; appended, so the older values keep their meaning in
 * dumps. */
enum {
    RD_TEXEL_RGBA8 = 0,
    RD_TEXEL_R8 = 1,
    /* Texture packs: a replacement's block-compressed texels as the DDS
     * file holds them (4x4 blocks, RHI_FMT_BC*), uploaded from
     * RdTexRec.pending and never kept in pixels, never dumped */
    RD_TEXEL_BC1 = 2,
    RD_TEXEL_BC2 = 3,
    RD_TEXEL_BC3 = 4,
    RD_TEXEL_BC7 = 5,
    RD_TEXEL_SHEET = 6,
    RD_TEXEL_COUNT
};

/* The one-byte coverage formats (R8 and SHEET): no mips, no TEXA. */
static inline int rd__TexelIsCoverage(uint8_t format)
{
    return format == RD_TEXEL_R8 || format == RD_TEXEL_SHEET;
}

static inline int rd__TexelIsBlock(uint8_t format)
{
    return format >= RD_TEXEL_BC1 && format <= RD_TEXEL_BC7;
}

/* Bytes of one texel of an uncompressed format (RGBA8 4, R8 and SHEET 1).
 * Not meaningful for the BC formats: they use rd__TexelBlockBytes. */
static inline uint32_t rd__TexelBytes(uint8_t format)
{
    return rd__TexelIsCoverage(format) ? 1u : 4u;
}

/* Texture packs: the side of a format's copy unit in texels (4 for the BC
 * formats' 4x4 blocks, 1 otherwise) and the bytes of one unit (BC1 8,
 * BC2/BC3/BC7 16, else rd__TexelBytes).  A row of a level w texels wide
 * is ceil(w / blockW) * blockBytes bytes and covers blockW texel rows. */
static inline uint32_t rd__TexelBlockW(uint8_t format)
{
    return rd__TexelIsBlock(format) ? 4u : 1u;
}

static inline uint32_t rd__TexelBlockBytes(uint8_t format)
{
    if (!rd__TexelIsBlock(format)) {
        return rd__TexelBytes(format);
    }
    return format == RD_TEXEL_BC1 ? 8u : 16u;
}

/* The RHI format of an image texture of format (RD_TEXEL_*). */
static inline RhiFormat rd__TexelRhiFormat(uint8_t format)
{
    switch (format) {
    case RD_TEXEL_R8:
    case RD_TEXEL_SHEET:
        return RHI_FMT_R8_UNORM;
    case RD_TEXEL_BC1:
        return RHI_FMT_BC1_UNORM;
    case RD_TEXEL_BC2:
        return RHI_FMT_BC2_UNORM;
    case RD_TEXEL_BC3:
        return RHI_FMT_BC3_UNORM;
    case RD_TEXEL_BC7:
        return RHI_FMT_BC7_UNORM;
    default:
        return RHI_FMT_RGBA8_UNORM;
    }
}

struct TexpackImage; /* texpack.h */

typedef struct RdTexRec {
    uint32_t gen;
    uint8_t live, kind;
    uint8_t src;       /* RdTexSrc: TEXFMT_* for the shader's TEXA expansion */
    uint8_t bakedTexa; /* rd_CreateTexture's texaMode, kept for dumps and debugging */
    uint32_t w, h;
    uint8_t *pixels; /* w * h texels of format; the CPU copy dumps and re-uploads read */
    RhiTexture rhi;
    RhiState state;
    uint8_t dirty;
    uint8_t format; /* R8: RD_TEXEL_* (images; targets leave it 0) */
    /* R8: with dirty, the texels changed since the last upload: the union
     * of the updates, [dirtyX0, dirtyX1) x [dirtyY0, dirtyY1); the whole
     * texture after a create or rd_UpdateTexture.  uploadTextures copies
     * that rectangle (the whole texture when it makes the RHI texture or
     * a mip chain) */
    uint32_t dirtyX0, dirtyY0, dirtyX1, dirtyY1;
    uint8_t view;      /* RdTexView, RD_TEXKIND_TARGET */
    uint8_t mipLevels; /* R7a: levels of rhi (1 unless the Enhanced filter generated mips) */
    uint32_t target;   /* RdTarget id, RD_TEXKIND_TARGET */
    /* package P1: the debug name, and the replays of the last uploads (a
     * texture uploaded on many replays in a row is logged once) */
    char name[24];
    uint32_t lastUpload;
    uint16_t uploadStreak;
    uint8_t streakLogged;
    /* Texture packs.  replacement: the texels came from a pack file
     * (rdtex_CreateReplacement), not from the game's TIM2; the sampler
     * treats it as mipmapped and it has no CPU copy in pixels.  uvW, uvH:
     * the size the draw's UVs are normalised by, the GS size the game's
     * texture had (2^TW x 2^TH), so a 4x replacement samples the same
     * place; 0 = w, h (every texture that is not a replacement).
     * pending: the replacement's levels as the file held them, owned
     * here until uploadTextures copies every level to the RHI texture and
     * frees it (null otherwise).  refused: the graphics card would not
     * create it (uploadReplacement); rdtex_Find then misses, so the game's
     * own texture is decoded again in its place. */
    uint8_t replacement;
    uint8_t refused;
    uint32_t uvW, uvH;
    struct TexpackImage *pending;
    /* Package P8: what the draws of a game texture with Enhanced mips do
     * with its alpha (RD_MIPUSE_*, seen at replay, sticky) and the highest
     * alpha-test reference of its unblended alpha-tested draws (as "a >
     * ref"); mipBuiltBoost and mipBuiltRef: the coverage the uploaded mips
     * keep (uploadMips), a draw that changes it re-uploads the texture. */
    uint8_t mipUse, mipRef;
    uint8_t mipBuiltBoost, mipBuiltRef;
    /* v0.4.2 (package F-A): a SHEET texture's style (rd.h RdSheetStyle):
     * the rim's weight (0 off, 1..64 the 64ths of a full rim: rimOn with
     * rimWeight), rimLevel, fillLevel, dither (kept as 0 or 1) */
    uint8_t sheet[4];
    /* v0.4.2 (package F-G): its RdSheetStyle.scale, 1..ICO_SHEET_SCALE_MAX
     * (0 in a texture that is no sheet) */
    uint8_t sheetScale;
} RdTexRec;

/* Package P8: RdTexRec.mipUse bits */
enum {
    RD_MIPUSE_BLEND_AS = 1, /* a draw blends by As (PRIM.ABE, C = As): alpha is opacity */
    RD_MIPUSE_TESTED = 2    /* an unblended draw alpha-tests it (AFAIL KEEP or ZB_ONLY) */
};

RdTexRec *rd__TexRec(uint32_t id);
/* R8: an image texture of format (RD_TEXEL_*), texels copied from px
 * (w * h * rd__TexelBytes bytes) or zero; rd_CreateTextureSrc and
 * rd_CreateTextureR8 are this, and the dump loader. */
RdTex rd__CreateTextureFmt(uint32_t w, uint32_t h, const void *px, uint8_t format, RdTexSrc src,
                           const char *debugName);
/* Texture packs: an image texture whose levels are img's (moved into
 * pending, *img left empty), format img->fmt, src RD_TEXSRC_RGBA32,
 * mipLevels img->levels, uvW x uvH the size draws normalise its UVs by.
 * rdtex_CreateReplacement checks the image first; this only records it. */
RdTex rd__CreateTextureReplacement(struct TexpackImage *img, uint32_t uvW, uint32_t uvH,
                                   const char *debugName);
/* Frees a replacement's pending levels (texpack_FreeImage) and the
 * TexpackImage holding them; t->pending is null afterwards. */
void rd__FreePending(RdTexRec *t);

/* --------------------------------------------------------------- meshes
 * rd_mesh.c (wave 3, R3ab).  A VU mesh keeps its vertex stream (the
 * batches' vertices without their GIF tags, float4 quadwords as the VIF
 * unpacked them) and its index list (ICO_VU_INDEX(kick, corner)) on the CPU;
 * replay copies them into the frame's upload ring once per replayed frame
 * (rd_replay.c) and binds the stream as the storage buffer t0. */
typedef struct RdVuBatchRec {
    uint32_t firstIndex, indexCount; /* in the mesh's index list */
    uint32_t firstVertex, vertexCount;
    uint32_t srcQw;    /* the batch's GIF tag in the creation stream (rd_UpdateVuMesh) */
    uint32_t prim;     /* the tag's PRIM field */
    uint16_t material; /* RdVuBatchDesc.material */
    uint16_t group;
} RdVuBatchRec;

typedef struct RdMeshRec {
    uint32_t gen;
    uint8_t live;
    uint8_t vu;       /* rd_CreateVuMesh */
    uint8_t replaced; /* v0.4.1 (M0): rd_CreateVuMeshReplacement built it */
    uint8_t stale;    /* rd_VuMeshRetire: not valid, freed once no kept frame drew it */
    uint32_t vertexCount, stripCount, materialCount;
    /* VU meshes */
    uint32_t qwPerVertex, batchCount, srcQw;
    float (*stream)[4]; /* vertexCount * qwPerVertex quadwords */
    uint32_t *index;
    /* issue 25: the index list the device draws, index with
     * ICO_VU_INDEX_LATER on each triangle that overlaps an earlier
     * triangle of the mesh in its plane (rd_mesh.c markLaterOverlaps);
     * NULL when none does (rd__MeshDrawIndex) */
    uint32_t *drawIndex;
    uint32_t indexCount;
    RdVuBatchRec *batches;
    uint32_t lastUsed;   /* g_rd.frameCounter of the last draw recorded */
    uint32_t replaySeen; /* g_rd.replayCounter of the replay that uploaded it */
    uint64_t ringStream, ringIndex;
    /* package P1: the device copy (rd_mesh.c's arena, rd_replay.c
     * uploadMeshes): stream at gpuOff, indices at gpuIndexOff of chunk
     * gpuChunk - 1 (0: none), gpuSize bytes reserved; uploaded again when
     * gpuDirty (rd_UpdateVuMesh).  transient: rewritten every present (the
     * interpolation's scratch meshes), drawn from the ring instead */
    uint64_t gpuOff, gpuIndexOff, gpuSize;
    uint8_t gpuChunk, gpuDirty, transient;
    char name[24];
    /* R7d: the stream's versions for the presenter (rd_UpdateVuMesh).
     * verFrame is the frame that was recording when the stream was last
     * written (0: as created); frames from it on draw the live stream.
     * While interpolating, a rewrite of a stream that a retained frame drew
     * keeps the old one in hist (the older entry replaced): what frames
     * from .. to - 1 drew. */
    uint32_t verFrame;

    struct {
        float (*stream)[4];
        uint32_t from, to;
    } hist[2];

    /* v0.4.1 (M0): rd_mesh.h's mesh identity (rd_VuMeshDescHash); for a
     * replaced mesh the original's; 0 for one built from a raw stream */
    uint64_t hash;
} RdMeshRec;

/* R7d: the stream mesh m had when frame `frame` was recorded, NULL when no
 * kept version covers it */
const float (*rd__MeshStreamAt(const RdMeshRec *m, uint32_t frame))[4];

#define RD_MAX_MESHES 16384

RdMeshRec *rd__MeshRec(uint32_t id);
/* Package P1: the device arena of the meshes' copies (rd_mesh.c): device
 * buffers of RD_MESH_CHUNK bytes (a larger mesh gets one of its own), first
 * fit with coalescing.  rd__MeshGpuReserve gives m a range for its stream
 * and indices (false: the arena is full, the mesh is drawn from the ring);
 * meshFree returns it.  A range freed while the GPU may still read it is
 * safe to reuse: every later write to it is a buffer copy recorded after,
 * which waits for earlier reads (rhi.h rhi_CmdCopyBuffer). */
#define RD_MESH_CHUNK (32u << 20)
#define RD_MESH_CHUNKS 32
bool rd__MeshGpuReserve(RdMeshRec *m, uint64_t streamBytes, uint64_t indexBytes, uint64_t align);
RhiBuffer rd__MeshGpuBuffer(uint32_t chunk);
/* Its size in bytes (package PA: the VU stream binds it whole). */
uint64_t rd__MeshGpuBufferSize(uint32_t chunk);
/* destroys the arena's buffers; every mesh loses its device copy */
void rd__MeshGpuShutdown(void);
/* Creates a VU mesh record from a stream and index list already in the
 * tagless layout (dump loading); returns the id, 0 on failure.  Every VU
 * mesh is made here, so its drawIndex (issue 25) is worked out here too. */
uint32_t rd__VuMeshCreateRaw(const float (*stream)[4], uint32_t vertexCount, uint32_t qwPerVertex,
                             const uint32_t *index, uint32_t indexCount,
                             const RdVuBatchRec *batches, uint32_t batchCount, const char *name);

/* Issue 25: the index list a draw of m uploads (RdMeshRec.drawIndex) */
static inline const uint32_t *rd__MeshDrawIndex(const RdMeshRec *m)
{
    return m->drawIndex ? m->drawIndex : m->index;
}

/* dst's drawIndex becomes a copy of src's (none when src has none): the
 * interpolation's scratch meshes draw their source's (rd_interp.c);
 * false on no memory (dst then has none) */
bool rd__VuMeshCopyDrawIndex(RdMeshRec *dst, const RdMeshRec *src);
/* rd_mesh.c: the per-list VU images (rd_SetVuCommon updates all 13). */
void rd__VuInit(void);
void rd__VuShutdown(void);
void rd__VuLoadCommon(const RdVuCommon *block);
void rd__MeshShutdown(void);
/* v0.4.1 (M0): rd_BeginFrame, after the frame number advanced: frees the
 * stale meshes (rd_VuMeshRetire) no frame from three back on drew */
void rd__VuMeshSweepStale(void);

/* ------------------------------------------------------------ pipelines */
typedef enum RdVsId {
    RD_VS_SPRITE_UI = 0,
    RD_VS_SPRITE_WORLD,
    RD_VS_BLIT,
    RD_VS_BLEND_INT,
    /* wave 3 (R3ab): the VU1 program shaders (vu_*.hlsl), group 1 = layoutVu */
    RD_VS_VU_PRELIT,
    RD_VS_VU_LIT,
    RD_VS_VU_LIT_SPEC,
    RD_VS_VU_REFLECT,
    RD_VS_VU_SKIN,
    RD_VS_VU_SKIN_SPEC,
    RD_VS_VU_SKIN_DEBUG,
    RD_VS_VU_GRID,
    RD_VS_VU_GRID_LIT,
    RD_VS_VU_GRID_SPEC,
    RD_VS_VU_PARTICLE,
    RD_VS_FX_RECT, /* wave 5 (R5a): fx_rect_vs, the fullscreen triangle at the sprite's Z */
    /* package AA1: PRIM.AA1 lines and triangles, IcoSpriteAa1Vertex (the
     * sprite vertex and its coverage) */
    RD_VS_SPRITE_AA1_UI,
    RD_VS_SPRITE_AA1_WORLD,
    /* package RSMALL: IcoSpriteStqVertex, screen prims with Q != 1 (sprite_stq_*_vs) */
    RD_VS_SPRITE_STQ_UI,
    RD_VS_SPRITE_STQ_WORLD,
    RD_VS_CRT, /* package CRT: crt_vs, the fullscreen triangle with 0..1 across the viewport */
    RD_VS_COUNT
} RdVsId;

#define RD_VS_VU_FIRST RD_VS_VU_PRELIT
#define RD_VS_VU_LAST RD_VS_VU_PARTICLE

typedef enum RdFsId {
    RD_FS_SPRITE = 0,
    RD_FS_BLIT,
    RD_FS_BLEND_INT,
    RD_FS_DATE_SNAP,    /* wave 2: destination alpha MSB into the R8 DATE snapshot */
    RD_FS_CAMERA_PROBE, /* wave 2 (R2c): FrameCB matrices applied to a point, as bytes (tests) */
    RD_FS_VU,           /* wave 3 (R3ab): vu_ps, the pixel side of every VU program */
    RD_FS_FOG,          /* wave 4 (R4c): fog_lut_ps, RD_POST_FOG behind the sprite vertex shaders */
    RD_FS_FX_SPRITE,    /* wave 5 (R5a): fx_sprite_ps, staticBlur.c's sprites in GS integers */
    /* wave 5 (R5c): COLCLAMP 0 screen prims (rd_replay.c doScreenWrap, raw_wrap.hlsl) */
    RD_FS_WRAP_ACC,     /* wrap_acc_ps: the blend terms into an RGBA16F accumulator */
    RD_FS_WRAP_RESOLVE, /* wrap_resolve_ps: (Cd + acc) mod 256 into the target */
    RD_FS_FONT,         /* package R8: font_ps, screen prims sampling an R8 coverage texture */
    /* v0.4.2 (package F-A): font_sheet_ps, screen and overlay prims sampling a
     * sheet texture (RD_TEXEL_SHEET); DrawCB.param holds its style */
    RD_FS_FONT_SHEET,
    RD_FS_SPRITE_AA1, /* package AA1: sprite_aa1_ps, sprite_ps with the coverage as As */
    RD_FS_BOX_REDUCE, /* package RSMALL: box_reduce_ps, the shadow count at the GS size */
    RD_FS_SPRITE_STQ, /* package RSMALL: sprite_stq_ps, sprite_ps dividing S and T by Q per pixel */
    /* package CRT (rd_crt.c): CrtCB in DrawCB's slot */
    RD_FS_CRT_BLOOM, /* crt_bloom_ps: half size, linear, horizontal Gaussian (RGBA16F) */
    RD_FS_CRT_BLUR,  /* crt_blur_ps: the vertical Gaussian (RGBA16F) */
    RD_FS_CRT,       /* crt_ps: the box of the output */
    /* package TEXA: TEXA per texel before the bilinear filter, for a 24- or
     * 16-bit texture under AEM with a linear filter (rd__TexaPerTexel) */
    RD_FS_SPRITE_TEXA, /* sprite_texa_ps */
    RD_FS_VU_TEXA,     /* vu_texa_ps */
    /* v0.4.1 (R1): blit_depth_ps, the present's box blit with the scene's
     * depth into the output-size effects depth (rd_present.c) */
    RD_FS_BLIT_DEPTH,
    RD_FS_COUNT
} RdFsId;

/* How an RdBlend equation reaches the hardware. */
typedef enum RdBlendPath {
    RD_BP_NONE = 0,      /* ABE off */
    RD_BP_LERP,          /* modes 2, 4, 7: SRC1 / 1-SRC1, factor As/128 or FIX/128 (<= 1) */
    RD_BP_PREMUL_ADD,    /* modes 0, 5: shader Cs*F>>7, ONE + ONE (exact for F up to 255) */
    RD_BP_PREMUL_REVSUB, /* modes 1, 6: shader Cs*F>>7, Cd - Cs' (exact) */
    RD_BP_DST_FIX,       /* mode 3: Cs + Cd * FIX/128 (FIX clamped to 0x80) */
    RD_BP_AD_ADD,        /* mode 8: Cs * Ad + Cd, DST_ALPHA (Ad/255, not /128) */
    RD_BP_AD_REVSUB,     /* mode 9 */
    RD_BP_AD_LERP,       /* mode 10 */
    RD_BP_CD_KEEP,       /* mode 11 Cd*As + Cd: not representable, leaves Cd (logged) */
    RD_BP_COUNT
} RdBlendPath;

/* The cache key: the normalised GS key plus what the backend needs. */
typedef struct RdPipeKeyInt {
    RdPipelineKey gs;
    uint8_t vs, fs;
    uint8_t colorFmt; /* RhiFormat */
    uint8_t depthFmt; /* RhiFormat, RHI_FMT_UNKNOWN = no depth attachment */
} RdPipeKeyInt;

/* One hardware draw of a screen-prim command (the AFAIL split makes two). */
typedef struct RdDrawPass {
    RdPipeKeyInt key;
    uint32_t flags; /* DF_* */
    uint32_t modeZ; /* atst | ate << 8 | split << 16 */
    uint32_t aref;
    uint32_t fix; /* FIX as the shader gets it (clamped where the path needs it) */
} RdDrawPass;

RdBlendPath rd__BlendPath(uint8_t keyBlend);
uint32_t rd__AlphaRegister(uint8_t blend);
/* Plans the hardware draws for a screen-prim command under state s.  prim is
 * the topology class (RD_PRIM_TRIANGLES or RD_PRIM_LINES).  Returns 0, 1 or
 * 2 passes. */
int rd__PlanScreenDraw(const RdStateBlock *s, uint8_t prim, uint8_t space, RhiFormat colorFmt,
                       RhiFormat depthFmt, RdDrawPass out[2]);
/* The same for a command PRIM.AA1 antialiases (aa1 1: a line or triangle
 * command under s->aa1; rd_pipeline.c says what changes).
 * rd__PlanScreenDraw is aa1 0. */
int rd__PlanScreenDrawEx(const RdStateBlock *s, uint8_t prim, int aa1, uint8_t space,
                         RhiFormat colorFmt, RhiFormat depthFmt, RdDrawPass out[2]);
/* Package AN-E, the two-pass blend for a device without dual-source
 * blending (g_rd.noDual): the planned passes in[0..n) as the device draws
 * them, in order, into out (at most 2 per pass: give out[4] for a plan of
 * 2).  With dual-source blending, or for a pass whose fragment entry has no
 * second output, a pass is copied as it is (a gs_dual_out entry gets its
 * *_nodual twin: key.gs.nodual).  A LERP or Cd*FIX + Cs pass becomes a
 * colour pass (ICO_DF_NODUAL_FACTOR, the factor in c0.a, mask RGB) and, when
 * its mask has A, an alpha pass after it (ICO_DF_NODUAL_ALPHA_PASS, no
 * blending, mask A, no Z write, the Z test admitting the Z the colour pass
 * wrote).  rd_pipeline.c says why the result is the one-pass result.
 * Returns the number of passes written. */
int rd__ExpandNoDual(const RdDrawPass *in, int n, RdDrawPass out[4]);
/* Whether ex[i] of an expansion is the alpha pass after a colour pass (the
 * draw counters count the pair once). */
bool rd__NoDualSecond(const RdDrawPass *ex, int i);
/* Whether fragment entry fs has a *_nodual twin (it calls gs_dual_out). */
bool rd__FsHasNoDual(uint8_t fs);
/* Package RSMALL: turns a planned pass of a command whose prims carry Q != 1
 * into the STQ shaders (IcoSpriteStqVertex); 0 when the pass keeps its own. */
int rd__StqPass(RdDrawPass *dp);
/* Package TEXA: whether a draw under this state samples a PSMCT24 or
 * PSMCT16 texture (or one with a 24- or 16-bit CLUT: RdTexRec.src not
 * RGBA32) under a TEXA with AEM through a linear MAG or MIN filter, the
 * draws whose bilinear edges depend on TEXA coming before the filter; the
 * planners give them sprite_texa_ps / vu_texa_ps. */
int rd__TexaPerTexel(const RdStateBlock *s);
RdPipeKeyInt rd__PostKey(RdVsId vs, RdFsId fs, RhiFormat colorFmt);
/* v0.4.1 (R1): the present's box blit with the effects depth: blit_vs and
 * blit_depth_ps into colorFmt (the headless output's RGBA8, the swapchain's
 * BGRA8) and an RHI_FMT_D32F depth target, Z written, test ALWAYS. */
RdPipeKeyInt rd__PresentDepthKey(RhiFormat colorFmt);
/* The pipeline for k, created on first use.  0 without a device. */
RhiPipeline rd__GetPipeline(const RdPipeKeyInt *k);
void rd__PipelineCacheClear(void);
uint32_t rd__PipelineCount(void);
const RdPipeKeyInt *rd__PipelineKeyAt(uint32_t i);
/* The pipelines the implemented programs can reach from the game's state
 * set (rd_pipeline.c lists the families and their sources).  Writes up to
 * max keys, returns the total. */
uint32_t rd__EnumerateReachable(RdPipeKeyInt *out, uint32_t max);
/* The screen and post families alone (wave 2's set, asserted under 100). */
uint32_t rd__EnumerateReachableScreen(RdPipeKeyInt *out, uint32_t max);
/* Wave 3 (R3ab): the VU program families (rd_mesh.c): each VU vertex shader
 * under the states the mesh lists draw with.  Appends to out[0..n). */
uint32_t rd__EnumerateReachableVu(RdPipeKeyInt *out, uint32_t max, uint32_t n);
/* The (program, code) row of rd_mesh.h's table: the RdProg, the vertex
 * shader and the clip mode; false without a row. */
bool rd__VuRow(int program, int code, uint8_t *prog, uint8_t *vs, uint8_t *clip);
bool rd__PipeKeyEqual(const RdPipeKeyInt *a, const RdPipeKeyInt *b);
/* Wave 4 (R4b), the shadow count (rd_shadow.c):
 * the volume pipeline under state s (sprite_world_vs / sprite_ps, colour
 * mask 0, the state's Z test without Z write, stencil INCR_WRAP or, with
 * decr, DECR_WRAP under write mask RD_SHADOW_STENCIL_MASK, on a D32F_S8
 * depth target), and the resolve's passes (blit_vs / blit_ps): pass k in
 * 0..5 adds 4 << k to RGB where stencil bit k is set, pass 6 writes A where
 * the count is not 0. */
RdPipeKeyInt rd__ShadowVolumeKey(const RdStateBlock *s, RhiFormat colorFmt, int decr);
RdPipeKeyInt rd__ShadowResolveKey(int pass);
RdPipeKeyInt rd__ShadowReduceKey(void); /* package RSMALL */
#define RD_SHADOW_RESOLVE_PASSES 7
/* The shadow families above under the states the game draws them with
 * (Shadow.c: TEST 0x50000); appends to out[0..n). */
uint32_t rd__EnumerateReachableShadow(RdPipeKeyInt *out, uint32_t max, uint32_t n);
/* Wave 4 (R4c), the depth fog: the draws of an
 * RD_POST_FOG under state s, as rd__PlanScreenDraw plans a fullscreen
 * sprite without a depth attachment (fog_lut_ps does the Z test against the
 * depth it reads), with fog_lut_ps as the fragment shader. */
int rd__FogPlan(const RdStateBlock *s, RhiFormat colorFmt, RdDrawPass out[2]);
/* The fog under ZFog.c's state (TEST 0x50000, ZMSK, ALPHA 0x44 with ABE);
 * appends to out[0..n). */
uint32_t rd__EnumerateReachableFog(RdPipeKeyInt *out, uint32_t max, uint32_t n);
/* rd_replay.c: frees the fog's LUT texture (rd__GpuShutdown); the fog samples
 * the depth target itself. */
void rd__FogShutdown(void);
void rd__WideScissor(int32_t *x0, int32_t *x1, int32_t w, float f); /* package RSMALL */
void rd__ShadowShutdown(void); /* package RSMALL: the reduced shadow count */
/* Wave 5 (R5a), staticBlur.c's sprites (rd_blur.c): the pipeline of an RD_POST_MOTION_BLUR .. RD_POST_EYE_BLUR sprite
 * under state s (fx_rect_vs / fx_sprite_ps, no hardware blending, the
 * state's colour mask; with a depth format the state's Z test and Z write,
 * else none), whether it binds the depth target (*useDepth), and the
 * enumeration of the keys staticBlur.c's states reach. */
RdPipeKeyInt rd__BlurKey(const RdStateBlock *s, RhiFormat colorFmt, RhiFormat depthFmt,
                         int *useDepth);
uint32_t rd__EnumerateReachableBlur(RdPipeKeyInt *out, uint32_t max, uint32_t n);
/* Package CRT: the CRT filter's pipelines (the two RGBA16F glow passes, the
 * composite on the headless output and the swapchain) */
uint32_t rd__EnumerateReachableCrt(RdPipeKeyInt *out, uint32_t max, uint32_t n);

/* ------------------------------------------------- the CRT filter (rd_crt.c)
 * Package CRT. */
typedef enum RdCrtMask {
    RD_CRT_MASK_NONE = 0,
    RD_CRT_MASK_GRILLE = 1, /* aperture grille: R, G, B stripes down the block */
    RD_CRT_MASK_SLOT = 2, /* slot mask: the stripes in slots, alternate blocks half a block lower */
    RD_CRT_MASK_DOTS = 3  /* shadow mask: two rows of R, G, B dots half a triad apart */
} RdCrtMask;

/* A mode's look (the presets' table in rd_crt.c) */
typedef struct RdCrtParams {
    float scanline;         /* 0..1: the beam profile against the plain lines */
    float beamMin, beamMax; /* the beam's full width at half maximum, dark to bright, in lines */
    int mask;               /* RdCrtMask */
    float maskStrength;     /* 0..1: 1 - the stripes' leak, the gaps' darkness */
    float halation, bloom;  /* 0..1 */
    float curvX, curvY;     /* the barrel warp per axis */
    float corner;           /* the corners' radius, of the box height */
    float vignette;         /* the vignette's exponent */
    float gammaIn, gammaOut;
} RdCrtParams;

/* The mode's own parameters; false for RD_CRT_OFF or an unknown mode. */
bool rd__CrtPreset(RdCrtMode mode, RdCrtParams *p);
/* The parameters s asks for: the mode's, with s's overrides. */
bool rd__CrtResolve(const RdSettings *s, RdCrtParams *p);
/* The mask's strength factor for a box boxH output pixels high: 1 from
 * 1080, 0 at 720 and below, linear between. */
float rd__CrtMaskFade(uint32_t boxH);
/* The phosphor mask, per output pixel (crt.hlsl maskOf is the same
 * function).  A source pixel is r output pixels wide (box width / grid
 * width); f is the output pixel's position across it and v across its line
 * (0..1, the pixel's centre), odd whether its source column is odd, gap the
 * mask strength (RdCrtParams.maskStrength).  Returns channel ch's weight:
 * 1 in its own stripe, 1 - gap (the leak) in the other two stripes and in a
 * gap, times 1 - gap on a slot's bridge.  rd__CrtGapColumns is the gap
 * columns a source pixel has room for (1 from r 4, 2 from r 6).
 * rd__CrtTriadGain (crt.hlsl triadGain) is 1 over the mean of channel ch's
 * stripe weight over the output columns whose centres fall in source pixel
 * sx (no curvature; the shader follows the curve), the mean floored at
 * RD_CRT_MEAN_MIN; rd__CrtRowGain is 1 over the slot bridges' mean over a
 * line (1 for the other masks): together the light each triad keeps. */
#define RD_CRT_MEAN_MIN 0.1f
uint32_t rd__CrtGapColumns(float r);
float rd__CrtMaskWeight(int mask, float r, float gap, float f, float v, int odd, int ch);
float rd__CrtTriadGain(int mask, float r, float gap, int sx, float v, int ch);
float rd__CrtRowGain(int mask, float gap);
/* The highlights, as crt.hlsl draws them (the CPU side of the same maths,
 * for the tests).  rd__CrtStrengthAt: the mask's strength under a source
 * pixel p (linear), eased to half as its brightest channel goes from 0.5
 * to 1.  rd__CrtTriadTop: channel ch's largest stripe weight over source
 * pixel sx's columns (1, or the leak in a triad short of its stripe).
 * rd__CrtGainFade: the fraction t of the gains (gain[c], each channel's
 * triad and row gain) a pixel of colour col takes so that no channel's
 * brightest stripe, col (1 + fade (top (1 + t (gain - 1)) - 1)), passes 1;
 * one t for the three channels, so the hue holds.  rd__CrtGlowMix: halation (halo,
 * the wide glow) and bloom (glow, weighted by its luminance) mixed into
 * col in proportion, never added.  rd__CrtShoulder: a colour whose
 * brightest channel passes 1 - 0.1 st scaled as a whole, that channel
 * rolled off toward 1.  rd__CrtBeam: a line of colour c (one channel,
 * linear) at d lines from its centre (crt.hlsl beamOf). */
float rd__CrtStrengthAt(float strength, const float p[3]);
float rd__CrtTriadTop(int mask, float r, float gap, int sx, float v, int ch);
float rd__CrtGainFade(const float col[3], const float gain[3], const float top[3], float fade);
void rd__CrtGlowMix(float col[3], const float halo[3], const float glow[3], float halation,
                    float bloom, float st);
void rd__CrtShoulder(float c[3], float st);
float rd__CrtBeam(float c, float d, float beamMin, float beamMax);
/* True when the present draws the CRT filter (a mode, strength > 0). */
bool rd__CrtOn(void);
/* The filter's source grid: the PS2 picture's pixels, vw across (512 at
 * 4:3, wider with the aspect) and vh lines (DISPLAY's, 512 with
 * full_height); the overlay's layer is vw x gsH (the frame's lines). */
void rd__CrtGrid(uint32_t *vw, uint32_t *vh);
/* Package CRT, from rd__PresentRecord in place of the line doubling and
 * the box blit: disp (SHADER_READ), with the overlay drawn into the source
 * grid (rd__OverlayGridDraw), through the glow passes, then the composite
 * (the phosphors per output pixel) into box of out (cleared outside it),
 * out left in RENDER_TARGET.  overlay false: the grid without the overlay
 * (the photo capture's pass, package PHOTO).  False when it could not draw
 * (no pipeline): the caller falls back to the box blit and draws the
 * overlay on the output. */
bool rd__CrtRecord(RhiCommandList cl, const RdTargetRec *disp, RhiTexture out, RhiFormat outFmt,
                   uint32_t outW, uint32_t outH, const RhiRect *box, int mirror, bool overlay);
/* Package C1: the films under the filter.  rd__CrtFilmGrid is a film's
 * grid: the game's tube (its 512 triads across the 4:3 box: GS width) and
 * the film's field lines in its PS2 display area (dispH / 2, every line
 * with full_height, as the game's).  rd__CrtRecordFilm: pic (pw x ph, the
 * film in its display area, already mirrored, SHADER_READ), box-reduced
 * to the grid vw x vh, then as rd__CrtRecord from step 1 (no overlay: the
 * films draw none).  rd__CrtLastPass: the composites drawn so far and the
 * last one's grid (the tests). */
void rd__CrtFilmGrid(uint32_t dispH, uint32_t *vw, uint32_t *vh);
bool rd__CrtRecordFilm(RhiCommandList cl, RhiTexture pic, uint32_t pw, uint32_t ph, uint32_t vw,
                       uint32_t vh, RhiTexture out, RhiFormat outFmt, uint32_t outW, uint32_t outH,
                       const RhiRect *box);
uint32_t rd__CrtLastPass(uint32_t *vw, uint32_t *vh);
/* The textures the filter keeps (the source, the overlay's layer, the glow
 * targets, the films' own) */
void rd__CrtShutdown(void);
/* rd_blur.c: records the sprite (rd_Post's wave-5 kinds, and since R-POST
 * the reduction's two sprites, rd_post.c). */
void rd__PostBlur(RdPostKind kind, const RdPostParams *p);
/* The same with the sprite's two screen vertices, then the mirrored pair,
 * kept in the payload (RdPostRec.lutOffset) for rd__BlurScreenFallback. */
void rd__PostBlurVerts(RdPostKind kind, const RdPostParams *p, const RdScreenVtx v[4]);
/* rd_blur.c: the payload offset of the screen vertices a recorded sprite is
 * drawn with instead of the model (the reduction on a scaled target), or
 * ~0u (R-POST). */
uint32_t rd__BlurScreenFallback(uint32_t kind, const RdPostRec *p, const RdStateBlock *st);
/* rd_blur.c: the UV rectangle the replay draws a recorded sprite with: the
 * recorded one, except the reduction's with the mirror on, which samples
 * the mirror image of the unmirrored sample points (R-POST). */
void rd__BlurUvRect(uint32_t kind, const RdPostRec *p, float uv[4]);

/* True for the kinds drawn by the GS sprite model (doBlurSprite): the
 * wave-5 kinds and the reduction (R-POST). */
static inline bool rd__IsBlurKind(uint32_t kind)
{
    return kind == RD_POST_REDUCTION || (kind >= RD_POST_MOTION_BLUR && kind <= RD_POST_EYE_BLUR);
}

/* ---------------------------------------------- interpolation (wave 7, R7b)
 * rd_interp.c.  rd__InterpFrame builds, into a
 * frame it owns, the current frame cur with every keyed draw's data blended
 * from its match in prev by alpha (0 = prev's data, 1 = cur's), and the
 * feedback passes held at the tick: a frame that writes FEED128 copies it
 * into FEED_HELD at its head when firstOfTick, and back from FEED_HELD
 * otherwise, and (v0.4.3, issue 28) a frame that reads DISPLAY (the motion
 * blur's old frame) does the same with DISPLAY and DISPLAY_HELD, so every
 * present of a tick starts from the same FEED128 and DISPLAY and draws the
 * recorded FIX.  prev NULL, or a frame-level snap
 * (rd__InterpSnap), copies cur's data.  The result is valid until the next
 * call; it owns no temporary targets (cur's are used). */
enum {
    RD_SNAP_NONE = 0, /* interpolated */
    RD_SNAP_NO_PREV,  /* no previous frame, or it is not closed */
    RD_SNAP_GAP,      /* frame numbers not consecutive (a frame was discarded) */
    RD_SNAP_KEEP,     /* either frame is a keep (fbKeep) frame */
    RD_SNAP_CUT,      /* rd_CameraCut during cur (RdCamera.cut) */
    RD_SNAP_CAMERA,   /* the camera turned or moved further than a cut threshold */
    RD_SNAP_FADE,     /* either frame fully faded (the fade edge) */
    RD_SNAP_HISTORY,  /* targets recreated after prev (display options changed) */
    RD_SNAP_SIZE,     /* the scene size differs */
    RD_SNAP_COUNT
};

typedef struct RdInterpStats {
    uint32_t snap;     /* RD_SNAP_* of the frame */
    uint32_t keyed;    /* keyed draws in cur */
    uint32_t lerped;   /* blended */
    uint32_t missing;  /* no match in prev (snapped) */
    uint32_t mismatch; /* count, topology or mesh differs (snapped) */
    uint32_t jump;     /* moved further than the teleport threshold (snapped) */
    uint32_t morph;    /* R7d: mesh draws given a kept or blended vertex stream */
    /* S2: why the mismatched draws snapped (RD_MISMATCH_*), and the mesh
     * draws whose model matrices or bones blended as rotations */
    uint32_t why[5];
    uint32_t rotated;
    uint32_t turned;  /* of them, turning more than 10 degrees in the tick */
    float maxTurn;    /* the largest turn of a model or bone in the tick, degrees */
    uint32_t shifted; /* shadow volumes whose topology changed: S2 moved cur's by the volume's
                         shift; since V3 those with a prism of one tick only */
    /* S6: VU draws drawn through the rigidly blended camera (matched and
     * not), and of the not-blended ones (unmatched, mismatched, jumped,
     * unkeyed) those re-based */
    uint32_t rebased;
    uint32_t rebasedCur;
} RdInterpStats;

/* S2: what made a matched keyed draw snap as mismatched */
enum {
    RD_MISMATCH_SIZE = 0, /* payload size, vertex or primitive count */
    RD_MISMATCH_MESH,     /* the mesh ids are of different layouts (sameMesh) */
    RD_MISMATCH_STATE,    /* program, code or clip mode (b[0..2]) */
    RD_MISMATCH_HEADER,   /* batch range, bones, stream, layout (RdVuPayload) */
    RD_MISMATCH_TOPOLOGY  /* a shadow volume's triangle counts */
};

/* S2: rotation-aware blending of an affine 4 x 4 (column-major, w row 0 0 0
 * 1): the 3 x 3 polar-decomposed into a rotation (slerped) and a stretch
 * (lerped); the image of pivot (x, y, z; NULL: the origin) lerped, so the
 * blended matrix turns about it.  False (o untouched) when either matrix
 * is not affine, is singular, or the two have opposite handedness. */
bool rd__BlendAffine(const double *p, const double *c, double t, const double *pivot, double *o);
/* S2: ICO_RD_S2_LEGACY=1 in the environment (a developer A/B switch, read
 * once) turns off the package's picture changes: rotation-aware blends (the
 * window also takes alpha from the measured present time).  The VU
 * positions are on the 12.4 grid whatever it says (issue 26). */
bool rd__S2Legacy(void);
/* Issue 26: ICO_RD_VU_OFFGRID=1 in the environment (a developer switch,
 * read once) brings back S2's unquantised VU X and Y (FrameCB g_z.w) with
 * the Enhanced preset on a scaled target: smoother slow motion, but the
 * seams between meshes and against the GIF path open. */
bool rd__VuOffGrid(void);

int rd__InterpSnap(const RdFrame *prev, const RdFrame *cur);
const RdFrame *rd__InterpFrame(const RdFrame *prev, const RdFrame *cur, float alpha,
                               int firstOfTick, RdInterpStats *stats);
void rd__InterpShutdown(void);

/* rd_present.c's capture (rd.h rd_CapturePresented): whether one is armed;
 * rd__CaptureRecord copies the output into the capture texture (from
 * rd__PresentRecord, before the overlay); rd__CaptureFinish reads it back
 * and writes the PNG (after the submit). */
bool rd__CaptureArmed(void);
void rd__CaptureFinish(void);
/* The thresholds (world units are the game's centimetres) */
#define RD_INTERP_JUMP_WORLD 300.0f  /* an object's or bone's origin, per tick */
#define RD_INTERP_JUMP_SCREEN 256.0f /* GS pixels: screen prims, shadows, grids, particles */
#define RD_INTERP_CAMERA_MOVE 300.0f /* the eye, per tick */
#define RD_INTERP_CAMERA_TURN 30.0f  /* degrees, per tick */
/* S2: a model matrix or bone turning further than this in a tick (3600
 * degrees a second at 30 Hz) is a flip, not a motion: it keeps the tick's */
#define RD_INTERP_TURN_SNAP 120.0 /* degrees, per tick */
/* V3: in the alignment of two ticks' shadow prisms, a prism left unmatched
 * costs as much as a pair whose top caps lie this far apart (rms, GS pixels)
 * once the volume's shift is taken out; a pair further apart than sqrt(2)
 * times this is cheaper as two unmatched prisms */
#define RD_INTERP_PRISM_GAP 32.0

/* fx_sprite_ps's DrawCB.mode[0] flags (FXF_* in port/shaders/fx_sprite.hlsl) */
enum {
    RD_FXF_TEXTURED = 1,
    RD_FXF_TFX_SHIFT = 1, /* bits 1..2: TEX0.TFX */
    RD_FXF_TCC = 8,
    RD_FXF_LINEAR = 16, /* TEX1.MMAG (the sprites' LOD is K = 0) */
    RD_FXF_CLAMP_S = 32,
    RD_FXF_CLAMP_T = 64,
    RD_FXF_ABE = 128,
    RD_FXF_PABE = 256,
    RD_FXF_FBA = 512,
    RD_FXF_DATE = 1024,
    RD_FXF_DATM = 2048,
    RD_FXF_DST = 4096 /* t2 holds the destination as it was before the sprite */
};

/* ------------------------------------------------------------- context */
#define RD_SAMPLER_COUNT 16 /* mag x min x wrapS x wrapT */
/* Wave 7 (R7a): the Enhanced filter's samplers, the same 16 with linear mips
 * (trilinear) and again with the device's anisotropy */
#define RD_SAMPLER_SETS 3
#define RD_SCRATCH_COUNT 6

typedef struct RdScratch {
    RhiTexture tex;
    RhiState state;
    uint32_t w, h;
} RdScratch;

typedef struct RdContext {
    bool inited, hasDevice;
    uint32_t gsW, gsH;
    RdSettings settings, pendingSettings;
    bool settingsPending;

    RdFrame frames[RD_FRAME_RING];
    int recIndex;  /* frame being recorded, -1 between frames */
    int lastIndex; /* last closed frame, -1 before the first */
    uint32_t frameCounter;
    int list;
    RdStateBlock persistent; /* the state the last replayed frame left */

    RdTargetRec targets[RD_MAX_TARGETS];
    RdTexRec *textures; /* RD_MAX_TEXTURES */
    RdMeshRec *meshes;  /* RD_MAX_MESHES */
    RdStats stats;
    uint32_t onceFlags;

    /* GPU objects (rd_replay.c / rd_pipeline.c) */
    RhiBindGroupLayout layoutFrame, layoutDraw, layoutTex, layoutInt;
    RhiBindGroupLayout layoutVu; /* wave 3: t0 stream, b1 DrawCB, b2 VuCB, b3 VuBoneCB */
    RhiShader vs[RD_VS_COUNT], fs[RD_FS_COUNT];
    /* package AN-E: the *_nodual twins of the gs_dual_out entries (0 for the
     * others), and whether the two-pass blend is in force (no dualSrcBlend,
     * ICO_RD_NO_DUAL or rd_SetNoDual) */
    RhiShader fsNoDual[RD_FS_COUNT];
    bool noDual;
    RhiSampler samplers[RD_SAMPLER_COUNT * RD_SAMPLER_SETS]; /* [set * 16 + index] */
    RhiTexture dummy;
    RhiState dummyState;
    RhiBuffer ring[RHI_FRAMES_IN_FLIGHT];
    uint8_t *ringMap[RHI_FRAMES_IN_FLIGHT];
    uint64_t ringCap[RHI_FRAMES_IN_FLIGHT];
    uint32_t replayCounter;
    RdScratch scratch[RD_SCRATCH_COUNT];
    /* rd_present.c */
    RhiTexture presentLines, presentOut;
    RhiState presentLinesState, presentOutState;
    uint32_t presentLinesW, presentLinesH, presentOutW, presentOutH;
    /* package DEF: recording, rd_DeferredTextQuads is on (RDC_SCREEN gets
     * RD_SCREEN_TEXT_QUADS); replaying, the present draws the deferred text
     * (rd__OverlayCollect) and doScreen skips the quads */
    uint8_t textQuads;
    bool deferText;
    /* wave 7 (R7a): the Enhanced display options as applied (rd_present.c
     * rd__ApplyDisplay from settings): the scene-class targets' scale, the
     * fixed work buffers' scale, the wide x factor (4/3) / aspect for draws
     * into scene-class targets (1 in Original), the output aspect, and the
     * texture filter upgrade in force */
    float sceneSx, sceneSy, workScale, wideX, outAspect;
    uint8_t filterUpgrade, fullHeight;
    int spaceOverride; /* rd_SetSpaceOverride + 1; 0 = none */
    uint32_t vsyncApplied;
    /* wave 7 (R7b): interpolation (rd_interp.c).  interpFloor: the first
     * frame number recorded after the targets were last recreated (a frame
     * pair interpolates only when both are at or after it); cutPending:
     * rd_CameraCut outside an open frame, for the next one; videoShown: an
     * FMV picture went to the output after the last closed game frame
     * (rd_video.c), so rd_Present leaves the output alone */
    uint32_t interpFloor;
    uint8_t cutPending, videoShown;
    /* wave 7 (R7c): rd_SetMirror's flag (the run's mirror mode); the
     * effective mirror is this or settings.mirror (rd__MirrorOn) */
    uint8_t mirrorRun;
    /* package P1: image textures with dirty set (uploadTextures skips its
     * walk of the table while there are none and the filter is unchanged) */
    uint32_t texDirtyCount;
    int texLevelsFilter; /* filterUpgrade the table was last walked for, -1 = never */
    /* package R8: image texture updates that changed texels since rd_Init,
     * whole (rd_UpdateTexture) and rectangles (rd_UpdateTextureRect); the
     * font's tests count them */
    uint32_t texFullUpdates, texRectUpdates;
} RdContext;

extern RdContext g_rd;

/* Wave 7 (R7c): the mirror mode in force (rd.h rd_SetMirror). */
static inline bool rd__MirrorOn(void)
{
    return g_rd.settings.mirror != 0 || g_rd.mirrorRun != 0;
}

/* Once-only diagnostics, by bit. */
enum {
    RD_ONCE_OUTSIDE_FRAME = 0,
    RD_ONCE_DATE,
    RD_ONCE_COLCLAMP,
    RD_ONCE_BLEND_RANGE,
    RD_ONCE_CD_KEEP,
    RD_ONCE_AD,
    RD_ONCE_FBMSK,
    RD_ONCE_BAD_TEX,
    RD_ONCE_EXACT_SIZE,
    RD_ONCE_STQ,
    RD_ONCE_LIST_RANGE,
    RD_ONCE_TEMP_FULL,
    RD_ONCE_DATE_SIZE,
    /* wave 3 (R3ab) */
    RD_ONCE_VU_ROW,        /* a (program, code) pair without a table row */
    RD_ONCE_VU_ENDTAG,     /* the particle end-tag quirk would have hidden a skinned draw */
    RD_ONCE_VU_MATERIALS,  /* RdVuDraw.materials given: recorded, not applied */
    RD_ONCE_VU_MESHES,     /* the mesh registry evicted meshes */
    RD_ONCE_VU_CODE,       /* an MSCAL code rd_VuCall does not model (debug font) */
    RD_ONCE_SEMANTIC_MESH, /* the wave-0 semantic mesh calls: not recorded */
    /* wave 4 (R4b) */
    RD_ONCE_SHADOW, /* a shadow command without a depth-stencil target of the colour's size */
    /* wave 4 (R4c) */
    RD_ONCE_FOG,        /* an RD_POST_FOG without a LUT or a depth source */
    RD_ONCE_DEPTH_VIEW, /* an ordinary draw sampling a depth view (only the fog reads one) */
    /* wave 5 (R5a) */
    RD_ONCE_BLUR, /* a staticBlur sprite without a colour target, or AFAIL with Z write */
    /* wave 5 (R5c) */
    RD_ONCE_WRAP, /* a COLCLAMP 0 draw the wrap path does not model (DATE, PABE, AFAIL split) */
    /* wave 7 (R7a) */
    RD_ONCE_COPY_SCALE, /* a copy between targets of different resolution scales */
    /* package P1 */
    RD_ONCE_MESH_ARENA, /* the mesh arena is full: meshes past it drawn from the ring */
    /* package AA1 */
    RD_ONCE_AA1_WRAP, /* PRIM.AA1 under COLCLAMP 0: the wrap path draws without coverage */
    /* package P8 */
    RD_ONCE_PABE, /* PABE on a premultiplied or Ad blend: As < 0x80 pixels blend anyway */
    /* package AN-E */
    RD_ONCE_NODUAL_GREATER, /* the two-pass blend's alpha pass under Z GREATER with Z write */
    RD_ONCE_NODUAL_KEY,     /* a LERP or Cd*FIX + Cs key reached rd__GetPipeline unexpanded */
    /* v0.4.1 (R1) */
    RD_ONCE_EFFECTS_DEPTH_CRT /* RdSettings.effectsDepth under the CRT filter: none */
};

void rd__Log(const char *fmt, ...);
void rd__LogOnce(int bit, const char *fmt, ...);
/* A recorded command whose replay belongs to a later wave.  Prints and
 * aborts unless rd__SetNotImplementedFatal(false) (tests). */
void rd__NotImplemented(const char *what);
void rd__SetNotImplementedFatal(bool fatal);

/* Package PC: merging consecutive screen-prim commands into one draw
 * (rd_replay.c doScreen); on by default, off
 * for the tests that compare a merged run with sequential draws. */
void rd__SetScreenMerge(bool on);
uint32_t rd__NotImplementedCount(void);

/* Recording helpers rd_post.c uses for the GS writes that have no public
 * rd_* call of their own. */
void rd__RecScissor(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
void rd__RecABE(int abe);
void rd__RecFilter(RdFilter mag, RdFilter min);
RdCmd *rd__Push(uint8_t type);
/* package MV: the draw filter's test for one world draw (rd.h
   rd_SetDrawFilter); learns the key's object while the filter is open */
bool rd__DrawFilterPass(RdKey key);
RdFrame *rd__RecFrame(void);

/* ---------------------------------------------------- replay / present */
bool rd__GpuInit(void *sdlWindow);
void rd__GpuShutdown(void);
/* Replays f (lists 0..12, or 11..12 with keep) from f->startState and, with
 * present, runs the presenter.  The replay state block ends as f->endState. */
bool rd__ReplayFrame(const RdFrame *f, int keep, bool present);

/* Package P1 (rd_perf.c, rd.h RdPerfRecord): the record of the replay
 * being made.  rd__PerfBegin starts it (the CPU phases are added to it by
 * the replay), rd__PerfEnd closes it with the RhiStats deltas and queues it
 * for its GPU times, which rd__PerfCollectGpu attaches right after the
 * rhi_WaitFrame RHI_FRAMES_IN_FLIGHT replays later.  rd__PerfStamp writes
 * timestamp i (RD_PERF_TS_*) into the replay's command list. */
enum {
    RD_PERF_TS_BEGIN = 0, /* after rhi_WaitFrame, before the uploads */
    RD_PERF_TS_LISTS = 1, /* the uploads done, list 0 starts */
    RD_PERF_TS_LIST0 = 2, /* + l: list l done */
    RD_PERF_TS_PRESENT = RD_PERF_TS_LIST0 + RD_LIST_COUNT, /* the present blits done */
    RD_PERF_TS_COUNT
};

extern RdPerfRecord g_rdPerf;
void rd__PerfReset(void); /* a new device: its counters start at 0 */
void rd__PerfBegin(const RdFrame *f, int keep, bool present);
void rd__PerfCollectGpu(void);
void rd__PerfEnd(void);
void rd__PerfStamp(RhiCommandList cl, uint32_t index);
/* rd_Present's interpolation time, charged to the replay that follows */
void rd__PerfInterpMs(double ms);
void rd__PerfAlpha(float alpha, int firstOfTick); /* S2: the next replay is a present at alpha */
/* a synchronous readback's time (rd_replay.c readTexture), charged likewise */
void rd__PerfReadbackMs(double ms);
/* rd_SetHostCall's hook, or fn(arg) directly (rd_core.c) */
void rd__OnHost(void (*fn)(void *arg), void *arg);
/* a monotonic clock in ms (rd_core.c), for the replay and start-up timings */
double rd__NowMs(void);
/* Present pass inside the replay's command list (rd_present.c). */
void rd__PresentRecord(RhiCommandList cl);
/* Wave 7 (R7a): the presentation box of aspect (4:3: the Original integer
 * box) centred in outW x outH; rd_video.c's movie box is the 4:3 one. */
void rd__PresentBox(uint32_t outW, uint32_t outH, float aspect, RhiRect *box);
/* Wave 7 (R7a): g_rd.settings -> g_rd.sceneSx/Sy, workScale, wideX,
 * outAspect, filterUpgrade, fullHeight (and the swapchain's vsync).  True
 * when a target scale changed (the caller recreates the named targets). */
bool rd__ApplyDisplay(void);
/* Wave 7 (R7a): tw, th, sx, sy, wide of a target record from its GS size;
 * named = its RdTargetId, -1 for a temporary target (rd_core.c). */
void rd__TargetScaleOf(RdTargetRec *t, int named);
bool rd__PresentAcquire(void);
/* v0.4.2 N1: after a swapchain image is acquired (rd__PresentAcquire, the
 * movie's acquireOut): the output size becomes the swapchain's when they
 * differ (logged once per change; rd_OutputFollowed reports it) */
void rd__OutputFollowSwapchain(void);
/* v0.4.2 N4, for the tests: the output size and the box the last present
 * drew the picture in (rd__PresentRecord, the movie's frame), noted as it
 * is recorded; false before the first */
void rd__NotePresentBox(uint32_t outW, uint32_t outH, const RhiRect *box);
bool rd__LastPresentBox(uint32_t *outW, uint32_t *outH, RhiRect *box);
void rd__PresentFinish(void);
void rd__PresentShutdown(void);

/* Package PA: a uniform block in the ring and the group that reads it: the
 * group is its layout's one group of the replay (a dynamic uniform slot),
 * offset the block's ring offset, passed when the group is bound. */
typedef struct RdUniform {
    RhiBindGroup group;
    uint32_t offset;
} RdUniform;

/* Package OV, the presentation overlay (rd.h rd_SetPresentOverlay;
 * rd_present.c).  rd__OverlayCollect runs the registered callback for the
 * present about to be replayed (replayFrame, before the ring is sized and
 * the textures uploaded) and keeps its prims; rd__OverlayRingBytes is what
 * drawing them takes from the upload ring; rd__PresentRecord draws them
 * after the box blit.  rd__OverlayDraw (rd_replay.c) draws one batch into
 * the pass open on an output of format fmt, FrameCB bound by the caller;
 * rd__OverlayState (rd_pipeline.c) is the synthetic state block it plans
 * with, which rd__EnumerateReachableScreen enumerates too.
 * Package DEF: rd__OverlayCollect(f, keep) first collects the frame's
 * deferred text (rd.h rd_DeferredText) when the present draws it deferred
 * (the Enhanced preset, a renderer registered, an output), which sets
 * g_rd.deferText for the replay (doScreen then skips the items' quads); the
 * items' prims are drawn before the overlay's, each batch in its region. */
void rd__OverlayCollect(const RdFrame *f, int keep);
/* Package CRT2: with the CRT filter on, rd__OverlayCollect gives the
 * overlay a context of the filter's source grid (rd__CrtGrid's vw x the
 * frame's lines, the box the whole of it, the frame's 1x scale) and
 * collects no deferred text; rd__CrtRecord then draws the prims into that
 * layer (t, of fmt, w x h, RENDER_TARGET) with rd__OverlayGridDraw, and the
 * present draws no overlay above the filter (unless the filter could not
 * draw: then the grid's prims are scaled into the box on the output).  rd__OverlayGridPending: there
 * are prims for the layer.  Package AN-T: only the main layer's
 * (rd_SetPresentOverlay); the top layer's (rd_SetPresentOverlayTop) are
 * laid out on the output and drawn on it after the filter. */
bool rd__OverlayGridPending(void);
void rd__OverlayGridDraw(RhiCommandList cl, RhiTexture t, RhiFormat fmt, uint32_t w, uint32_t h);
/* rd_present.c's blit: src (sw x sh) into box of dst, filter, x flipped */
void rd__PresentBlit(RhiCommandList cl, RhiTexture src, uint32_t sw, uint32_t sh, RhiTexture dst,
                     RhiFormat dstFmt, uint32_t dw, uint32_t dh, RhiLoadOp load, const RhiRect *box,
                     RdFilter filter, int mirror);
/* rd_core.c, from rd_Post (package DEF): records the RD_OTEXT_OP of a post
 * pass of kind (FADE, LETTERBOX, BRIGHTNESS, KEEP, REDUCTION) when the frame being
 * recorded has deferred text already; nothing otherwise */
void rd__DeferredTextOp(uint8_t kind, const RdTextOp *op, RdKey key);
uint64_t rd__OverlayRingBytes(void);
void rd__OverlayDraw(RhiCommandList cl, RhiFormat fmt, RdUniform frame, uint8_t prim,
                     const RdScreenVtx *v, uint32_t n, uint32_t tex, uint8_t blend);
void rd__OverlayState(RdStateBlock *s, uint8_t blend);
/* Moves a texture to a state with a barrier when needed (outside passes). */
void rd__Transition(RhiCommandList cl, RhiTexture t, RhiState *cur, RhiState want);

/* v0.4.2 (N2): a target clear the replay has not recorded yet
 * (rd_replay.c doClear): the next pass on the target takes it as its load op
 * (a tile-based GPU then neither writes the cleared target out nor reads it
 * back), else it is recorded as its own pass before anything else touches
 * the target.  target 0: none. */
typedef struct RdPendingClear {
    uint32_t target;
    uint8_t depth; /* the target's depth and stencil cleared too */
    float color[4];
    float clearDepth;
} RdPendingClear;

/* A pass about to open on colour target cid (depth target did, 0 none) with
 * these loads takes the pending clear p: the colour load LOAD on p's target,
 * and when p clears depth, the pass's depth p's target's loaded too.  Then
 * the loads become CLEAR (the depth's only when p clears it) and true is
 * returned; else they are left and false is returned. */
bool rd__TakePendingClear(const RdPendingClear *p, uint32_t cid, uint32_t did, RhiLoadOp *colorLoad,
                          RhiLoadOp *depthLoad);

/* The stencil of a depth target holds something only for the shadow count:
 * its reset clears it (RDC_SHADOW_RESET), the volumes count into it
 * (doShadowStrip) and the resolve reads it (doShadowResolve).  Every other
 * pipeline has the stencil test off (rd_pipeline.c shadowStencil sets it for
 * the volume and resolve keys alone), so outside that window no pass needs
 * the stencil loaded or stored.
 *   live      the depths whose stencil holds a count, each from its reset to
 *             the end of its resolve's pass (0: a free slot).  One per depth,
 *             so a reset on a second depth before the first depth's resolve
 *             leaves the first one's count loaded and stored.
 *   clearFor  the depth whose reset is not recorded yet: the next pass on it
 *             clears the stencil as its load op (0: none)
 *   spill     set when more depths were live at once than live[] holds: from
 *             then on every depth counts as live (loaded and stored, the safe
 *             side) until the window is emptied for the next replay
 * A depth is named by its texture id in rd_replay.c.  The game opens one
 * window a frame, on SCENE's depth (one rd_ShadowReset and one
 * rd_ShadowResolve). */
#define RD_STENCIL_LIVE_MAX 4

typedef struct RdStencilWindow {
    uint32_t live[RD_STENCIL_LIVE_MAX];
    uint32_t clearFor;
    uint8_t spill;
} RdStencilWindow;

/* Whether depth (not 0) is inside its window. */
bool rd__StencilLive(const RdStencilWindow *w, uint32_t depth);
/* The reset of depth opens its window (nothing when it is open already). */
void rd__StencilOpen(RdStencilWindow *w, uint32_t depth);
/* The end of depth's resolve closes its window. */
void rd__StencilClose(RdStencilWindow *w, uint32_t depth);

/* The stencil ops of a pass on depth (not 0) whose depth loads depthLoad;
 * whole: the pass's render area is the whole depth.  A pending reset of
 * this depth is taken when whole: the stencil loads CLEAR (to 0), clearFor
 * is emptied and true is returned.  Else, on a live depth, the stencil
 * loads as the depth does; on any other, a CLEAR stays CLEAR and anything
 * else is DONT_CARE.  The stencil is stored on a live depth only. */
bool rd__StencilOps(RdStencilWindow *w, uint32_t depth, bool whole, RhiLoadOp depthLoad,
                    RhiLoadOp *stencilLoad, RhiStoreOp *stencilStore);
/* rhi_WaitFrame for the renderer's own frames (the replay, the FMV picture,
 * the camera probe): also starts a new epoch of the bind group caches
 * (rd_replay.c, package P1), since bind groups live one frame slot. */
void rd__WaitFrame(void);
/* Package C1: a frame of the renderer's own outside a replay (the films,
 * rd_video.c): rd__WaitFrame, then the next slot of the upload ring (at
 * least ringBytes) as a replay takes it, so the ring's slot keeps pace
 * with the RHI's frames and rd__FrameGroup, rd__CrtGroup and the like
 * work.  False when the ring cannot be had. */
bool rd__BeginOwnFrame(uint64_t ringBytes);
/* Ring allocation for the frame being replayed. */
uint64_t rd__RingAlloc(uint64_t size, uint64_t align);
/* Binds u at group index `group` with its offset (rhi_CmdSetBindGroupOffsets). */
void rd__BindUniform(RhiCommandList cl, uint32_t group, RdUniform u);
/* FrameCB for the presenter and posts (group 0). */
RdUniform rd__FrameGroup(uint32_t targetW, uint32_t targetH, float originX, float originY);
/* The same with the GS Z scale of the bound depth buffer (R2c); rd__FrameGroup
 * uses 2^-24 (no depth bound). */
RdUniform rd__FrameGroupZ(uint32_t targetW, uint32_t targetH, float originX, float originY,
                          float zScale);
/* Wave 7 (R7a): FrameCB with the wide x scale spaceX in both g_space slots
 * and the target's texels per GS pixel in g_z.yz. */
RdUniform rd__FrameGroupEx(uint32_t targetW, uint32_t targetH, float originX, float originY,
                           float zScale, float spaceX, float scaleX, float scaleY);

/* ------------------------------------------------- frame head and camera
 * rd_frame.c (wave 2, R2c). */
/* rd_EndFrame: keep the head copy in the first replayed list, no-op the
 * other (rd.h, rd_FrameHead). */
void rd__FrameHeadResolve(RdFrame *f, int keep);
/* Fills FrameCB's camera fields (view, proj, viewProj, cameraPos, clip)
 * from cam, or identity with cam NULL.  cb is an IcoFrameCB. */
void rd__FillCameraCB(void *cb, const RdCamera *cam);
/* The camera FrameCB carries during the replay of a frame (rd_replay.c). */
void rd__SetReplayCamera(const RdCamera *cam);
/* Test hook: FrameCB from cam, then camera_probe_ps evaluates
 * mul(g_view, p), mul(g_proj, mul(g_view, p)) and mul(g_viewProj, p) on the
 * GPU and returns them in out[0..2] (bit patterns read back).  False without
 * a device. */
bool rd__CameraProbe(const RdCamera *cam, const float p[4], float out[3][4]);
/* DrawCB (group 1). */
RdUniform rd__DrawGroup(const void *drawCB);
/* Package CRT: crt.hlsl's CrtCB (IcoCrtCB) in DrawCB's dynamic slot and
 * layout; the layout's dynamic group is cached per block size, so the two
 * sizes need not match (rd_replay.c dynamicGroup). */
RdUniform rd__CrtGroup(const void *crtCB);
RhiBindGroup rd__TexGroup(RhiTexture t, RhiSampler s);
/* The same with t2 (sprite_ps's DATE snapshot) bound to date; rd__TexGroup binds
 * the 1x1 dummy there (sprite_ps reads t2 only under DF_DATE). */
RhiBindGroup rd__TexGroupDate(RhiTexture t, RhiSampler s, RhiTexture date);
RhiSampler rd__Sampler(RdFilter mag, RdFilter min, RdWrap s, RdWrap t);

/* ------------------------------------------------------ tests and tools */
/* Recording without a device: rd_* calls work, rd_EndFrame closes frames
 * but replays nothing. */
bool rd__InitRecordOnly(uint32_t gsW, uint32_t gsH);
const RdFrame *rd__LastFrame(void);
const RdFrame *rd__PrevFrame(void);
/* Reads a target's colour (RGBA8, tightly packed, w * h * 4 bytes). */
bool rd__ReadTarget(RdTarget t, void *dst, size_t dstSize, uint32_t *w, uint32_t *h);
/* Package P8: the depth buffer of a target with one, as floats (w * h,
 * tests only). */
bool rd__ReadTargetDepth(RdTarget t, float *dst, size_t dstSize, uint32_t *w, uint32_t *h);
/* Reads the headless presenter output (RGBA8). */
bool rd__ReadPresent(void *dst, size_t dstSize, uint32_t *w, uint32_t *h);
/* v0.4.1 (R1): the effects depth of the last present (RdSettings.effectsDepth),
 * outW x outH floats; false when no present has drawn it (tests). */
bool rd__ReadPresentDepth(float *dst, size_t dstSize, uint32_t *w, uint32_t *h);
/* v0.4.1 (R1): the effects depth pass runs only with an effects program
 * loaded (rhi_InjectorName); true runs it whenever RdSettings.effectsDepth
 * is on, as with a program (tests: lavapipe has no layer). */
void rd__ForceEffectsDepth(bool force);
/* package PHOTO: a synchronous readback of an RHI texture (RGBA8 or BGRA8,
 * w x h, tightly packed), leaving it in COPY_SRC (*state follows) */
bool rd__ReadRhiTexture(RhiTexture t, RhiState *state, uint32_t w, uint32_t h, void *dst,
                        size_t dstSize);
/* Package R8: reads an image texture's level 0 back from the GPU as its
 * format holds it (tightly packed, w * h * rd__TexelBytes bytes); false
 * before its first upload. */
bool rd__ReadTexture(RdTex t, void *dst, size_t dstSize, uint32_t *w, uint32_t *h);

/* Dumps (rd_dump.c).  Loading creates the frame's textures and temporary
 * targets in the current context and rewrites the ids in the commands. */
#define RD_DUMP_MAGIC "ICORDMP\0"
#define RD_DUMP_VERSION                                                                            \
    8u /* 2: RDC_ALPHA, RDC_SHADE, RdStateBlock.gouraud (wave 2); 3: VU meshes (wave 3); 4:     \
          RDC_AA1, RdStateBlock.aa1 (package AA1); 5: RDC_OVERLAY_TEXT and RDC_SCREEN's       \
          RD_SCREEN_TEXT_QUADS (package DEF); 6: RD_TARGET_FEED_HELD, a 17th fixed target;    \
          7: RD_TEXEL_SHEET images, the style in the view word (v0.4.2, F-A); 8:              \
          RD_TARGET_DISPLAY_HELD, an 18th fixed target (v0.4.3, issue 28).                    \
          rd__LoadFrame reads 3 to 8 */
bool rd__DumpFrame(const RdFrame *f, const char *path);
bool rd__LoadFrame(const char *path, RdFrame *out);

/* rd_water.c (wave 5, R5b): render-to-texture surfaces (rd.h rd_AliasTarget,
 * rd_BlockTarget, rd_PushCamera).  The per-frame records live in rd_water.c,
 * one per frame slot, and are cleared by rd__FrameReset.
 *   rd__AliasOf        the target rd_AliasTarget put in place of target id in
 *                      the current list of the open frame, 0 for none
 *   rd__CameraAt       the camera of command index in list of f: the innermost
 *                      rd_PushCamera scope that holds it, else the frame camera
 *                      (NULL when the frame has none)
 *   rd__CameraScopes   the number of scopes recorded in f */
typedef struct RdCameraScope {
    int32_t list;
    uint32_t start, end; /* command indices [start, end) of the list; end is
                            UINT32_MAX while open */
    RdCamera cam;
} RdCameraScope;

void rd__WaterFrameReset(const RdFrame *f);
uint32_t rd__AliasOf(uint32_t id);
const RdCamera *rd__CameraAt(const RdFrame *f, int list, uint32_t index);
uint32_t rd__CameraScopes(const RdFrame *f, const RdCameraScope **scopes);
/* the pipelines of these files' screen-prim states that the screen families
 * leave out (rd__EnumerateReachable adds them) */
uint32_t rd__EnumerateReachableWater(RdPipeKeyInt *out, uint32_t max, uint32_t n);

/* rd_replay.c (wave 5, R5c): a
 * screen-prim command
 * under COLCLAMP 0 with an additive or subtractive equation (ALPHA modes 0,
 * 1, 5, 6, ABE on) wraps modulo 256 per channel as on the GS instead of
 * clamping (doScreenWrap: wrap_acc_ps into an RGBA16F accumulator, then
 * wrap_resolve_ps).  True when state s takes that path.  rd__WrapShutdown
 * frees the accumulator and the path's pipelines (rd__GpuShutdown); the
 * pipelines are a private cache outside rd__GetPipeline's, at most
 * RD_WRAP_PIPES. */
bool rd__WrapApplies(const RdStateBlock *s);
void rd__WrapShutdown(void);
uint32_t rd__WrapPipelineCount(void);
#define RD_WRAP_PIPES 16

/* rd_png.c: 8-bit RGBA (or RGB with withAlpha = 0), stored deflate.  The
 * source is RGBA8 either way, rows pitch (>= w * 4) bytes apart. */
bool rd_WritePng(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t pitch,
                 int withAlpha);

#ifdef __cplusplus
}

#endif
#endif /* PORT_RENDER_RD_INTERNAL_H */
