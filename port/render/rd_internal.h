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

/* RDC_SHADOW_STRIP's b[0] (wave 4, R4b). */
#define RD_SHADOW_TRIS 1
/* The stencil bits the shadow count keeps: n mod 64, as 4 n mod 256 wraps
 * (RENDER_API.md "Shadows"). */
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
} RdStateBlock;

_Static_assert(sizeof(RdDrawState) == 28, "RdDrawState layout");
_Static_assert(sizeof(RdStateBlock) == 80, "RdStateBlock is dumped as raw bytes");

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
     * scale (rd__TargetScaleOf, RENDER_API.md "Presets and display options") */
    uint32_t tw, th;
    float sx, sy;
    uint8_t wide; /* scene-class: draws other than full-screen ones take the wide x scale */
    RhiFormat format;
    RhiTexture color, depth;
    RhiState colorState, depthState;
    RhiTexture snap; /* copy used when a draw samples the target it renders to */
    RhiState snapState;
    uint32_t snapFor;    /* replay counter at which snap was taken */
    uint32_t viewTex[3]; /* RdTex ids of rd_TargetTexture, per RdTexView */
    uint32_t ownerFrame;
    uint8_t zFormat; /* RdZFormat of the depth buffer (R2c); 0 = PSMZ32 */
    /* package P1: a freed temporary target kept with its textures for the
     * next of its size (rd__TempTargetAlloc); a taken one is cleared to 0
     * at the next replay (clearPending) */
    uint8_t parked, clearPending;
} RdTargetRec;

/* GS Z to depth scale of a target id's depth buffer (2^-32 for PSMZ32, the
 * default; 2^-32 also for an unknown id). */
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

typedef struct RdTexRec {
    uint32_t gen;
    uint8_t live, kind;
    uint8_t src;       /* RdTexSrc: TEXFMT_* for the shader's TEXA expansion */
    uint8_t bakedTexa; /* rd_CreateTexture's texaMode, kept for dumps and debugging */
    uint32_t w, h;
    uint8_t *pixels; /* RGBA8, w * h * 4; the CPU copy dumps and re-uploads read */
    RhiTexture rhi;
    RhiState state;
    uint8_t dirty;
    uint8_t view;      /* RdTexView, RD_TEXKIND_TARGET */
    uint8_t mipLevels; /* R7a: levels of rhi (1 unless the Enhanced filter generated mips) */
    uint32_t target;   /* RdTarget id, RD_TEXKIND_TARGET */
    /* package P1: the debug name, and the replays of the last uploads (a
     * texture uploaded on many replays in a row is logged once) */
    char name[24];
    uint32_t lastUpload;
    uint16_t uploadStreak;
    uint8_t streakLogged;
} RdTexRec;

RdTexRec *rd__TexRec(uint32_t id);

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
    uint8_t vu; /* rd_CreateVuMesh */
    uint8_t _pad[2];
    uint32_t vertexCount, stripCount, materialCount;
    /* VU meshes */
    uint32_t qwPerVertex, batchCount, srcQw;
    float (*stream)[4]; /* vertexCount * qwPerVertex quadwords */
    uint32_t *index;
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
/* destroys the arena's buffers; every mesh loses its device copy */
void rd__MeshGpuShutdown(void);
/* Creates a VU mesh record from a stream and index list already in the
 * tagless layout (dump loading); returns the id, 0 on failure. */
uint32_t rd__VuMeshCreateRaw(const float (*stream)[4], uint32_t vertexCount, uint32_t qwPerVertex,
                             const uint32_t *index, uint32_t indexCount,
                             const RdVuBatchRec *batches, uint32_t batchCount, const char *name);
/* rd_mesh.c: the per-list VU images (rd_SetVuCommon updates all 13). */
void rd__VuInit(void);
void rd__VuShutdown(void);
void rd__VuLoadCommon(const RdVuCommon *block);
void rd__MeshShutdown(void);

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
    RD_FS_COUNT
} RdFsId;

/* How an RdBlend equation reaches the hardware (RENDER_API.md "GS to pipeline mapping"). */
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
RdPipeKeyInt rd__PostKey(RdVsId vs, RdFsId fs, RhiFormat colorFmt);
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
/* Wave 4 (R4b), the shadow count (rd_shadow.c, RENDER_API.md "Shadows"):
 * the volume pipeline under state s (sprite_world_vs / sprite_ps, colour
 * mask 0, the state's Z test without Z write, stencil INCR_WRAP or, with
 * decr, DECR_WRAP under write mask RD_SHADOW_STENCIL_MASK, on a D32F_S8
 * depth target), and the resolve's passes (blit_vs / blit_ps): pass k in
 * 0..5 adds 4 << k to RGB where stencil bit k is set, pass 6 writes A where
 * the count is not 0. */
RdPipeKeyInt rd__ShadowVolumeKey(const RdStateBlock *s, RhiFormat colorFmt, int decr);
RdPipeKeyInt rd__ShadowResolveKey(int pass);
#define RD_SHADOW_RESOLVE_PASSES 7
/* The shadow families above under the states the game draws them with
 * (Shadow.c: TEST 0x50000); appends to out[0..n). */
uint32_t rd__EnumerateReachableShadow(RdPipeKeyInt *out, uint32_t max, uint32_t n);
/* Wave 4 (R4c), the depth fog (RENDER_API.md "Depth fog"): the draws of an
 * RD_POST_FOG under state s, as rd__PlanScreenDraw plans a fullscreen
 * sprite without a depth attachment (fog_lut_ps does the Z test against the
 * depth it reads), with fog_lut_ps as the fragment shader. */
int rd__FogPlan(const RdStateBlock *s, RhiFormat colorFmt, RdDrawPass out[2]);
/* The fog under ZFog.c's state (TEST 0x50000, ZMSK, ALPHA 0x44 with ABE);
 * appends to out[0..n). */
uint32_t rd__EnumerateReachableFog(RdPipeKeyInt *out, uint32_t max, uint32_t n);
/* rd_replay.c: frees the fog's depth copy and LUT textures (rd__GpuShutdown). */
void rd__FogShutdown(void);
/* Wave 5 (R5a), staticBlur.c's sprites (rd_blur.c, RENDER_API.md "Full-screen effects and the
 * raw packet builders"): the pipeline of an RD_POST_MOTION_BLUR .. RD_POST_EYE_BLUR sprite
 * under state s (fx_rect_vs / fx_sprite_ps, no hardware blending, the
 * state's colour mask; with a depth format the state's Z test and Z write,
 * else none), whether it binds the depth target (*useDepth), and the
 * enumeration of the keys staticBlur.c's states reach. */
RdPipeKeyInt rd__BlurKey(const RdStateBlock *s, RhiFormat colorFmt, RhiFormat depthFmt,
                         int *useDepth);
uint32_t rd__EnumerateReachableBlur(RdPipeKeyInt *out, uint32_t max, uint32_t n);
/* rd_blur.c: records the sprite (rd_Post's wave-5 kinds). */
void rd__PostBlur(RdPostKind kind, const RdPostParams *p);

/* True for the wave-5 kinds. */
static inline bool rd__IsBlurKind(uint32_t kind)
{
    return kind >= RD_POST_MOTION_BLUR && kind <= RD_POST_EYE_BLUR;
}

/* The interpolation hook of the feedback passes (motion blur, aura): the
 * FIX that gives over dt frames the retention FIX gives over one, for a
 * LERP_FIX (retention (128 - FIX) / 128 per frame) or an additive or
 * subtractive FIX (scaled by dt).  dt = 1 returns fix unchanged, which is
 * all the Original preset ever passes. */
uint8_t rd__BlurFeedbackFix(uint8_t blend, uint8_t fix, float dt);

/* ---------------------------------------------- interpolation (wave 7, R7b)
 * rd_interp.c (RENDER_API.md "Frame rate and interpolation").  rd__InterpFrame builds, into a
 * frame it owns, the current frame cur with every keyed draw's data blended
 * from its match in prev by alpha (0 = prev's data, 1 = cur's), and the
 * feedback passes set up for a present that stands for dt ticks
 * (motion blur's FIX through rd__BlurFeedbackFix; the aura's FEED128
 * writes dropped unless firstOfTick).  prev NULL, or a frame-level snap
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
 * once) turns off the package's picture changes: rotation-aware blends and
 * unquantised VU positions (the window also takes alpha from the measured
 * present time) */
bool rd__S2Legacy(void);

int rd__InterpSnap(const RdFrame *prev, const RdFrame *cur);
const RdFrame *rd__InterpFrame(const RdFrame *prev, const RdFrame *cur, float alpha, float dt,
                               int firstOfTick, RdInterpStats *stats);
void rd__InterpShutdown(void);
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
    RD_ONCE_MESH_ARENA /* the mesh arena is full: meshes past it drawn from the ring */
};

void rd__Log(const char *fmt, ...);
void rd__LogOnce(int bit, const char *fmt, ...);
/* A recorded command whose replay belongs to a later wave.  Prints and
 * aborts unless rd__SetNotImplementedFatal(false) (tests). */
void rd__NotImplemented(const char *what);
void rd__SetNotImplementedFatal(bool fatal);
uint32_t rd__NotImplementedCount(void);

/* Recording helpers rd_post.c uses for the GS writes that have no public
 * rd_* call of their own. */
void rd__RecScissor(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
void rd__RecABE(int abe);
void rd__RecFilter(RdFilter mag, RdFilter min);
RdCmd *rd__Push(uint8_t type);
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
void rd__PresentFinish(void);
void rd__PresentShutdown(void);
/* Package OV, the presentation overlay (rd.h rd_SetPresentOverlay;
 * rd_present.c).  rd__OverlayCollect runs the registered callback for the
 * present about to be replayed (replayFrame, before the ring is sized and
 * the textures uploaded) and keeps its prims; rd__OverlayRingBytes is what
 * drawing them takes from the upload ring; rd__PresentRecord draws them
 * after the box blit.  rd__OverlayDraw (rd_replay.c) draws one batch into
 * the pass open on an output of format fmt, FrameCB bound by the caller;
 * rd__OverlayState (rd_pipeline.c) is the synthetic state block it plans
 * with, which rd__EnumerateReachableScreen enumerates too. */
void rd__OverlayCollect(void);
uint64_t rd__OverlayRingBytes(void);
void rd__OverlayDraw(RhiCommandList cl, RhiFormat fmt, RhiBindGroup frame, uint8_t prim,
                     const RdScreenVtx *v, uint32_t n, uint32_t tex, uint8_t blend);
void rd__OverlayState(RdStateBlock *s, uint8_t blend);
/* Moves a texture to a state with a barrier when needed (outside passes). */
void rd__Transition(RhiCommandList cl, RhiTexture t, RhiState *cur, RhiState want);
/* rhi_WaitFrame for the renderer's own frames (the replay, the FMV picture,
 * the camera probe): also starts a new epoch of the bind group caches
 * (rd_replay.c, package P1), since bind groups live one frame slot. */
void rd__WaitFrame(void);
/* Ring allocation for the frame being replayed. */
uint64_t rd__RingAlloc(uint64_t size, uint64_t align);
/* Per-frame uniform bind groups for the presenter and posts. */
RhiBindGroup rd__FrameGroup(uint32_t targetW, uint32_t targetH, float originX, float originY);
/* The same with the GS Z scale of the bound depth buffer (R2c); rd__FrameGroup
 * uses 2^-24 (no depth bound). */
RhiBindGroup rd__FrameGroupZ(uint32_t targetW, uint32_t targetH, float originX, float originY,
                             float zScale);
/* Wave 7 (R7a): FrameCB with the wide x scale spaceX in both g_space slots
 * and the target's texels per GS pixel in g_z.yz. */
RhiBindGroup rd__FrameGroupEx(uint32_t targetW, uint32_t targetH, float originX, float originY,
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
RhiBindGroup rd__DrawGroup(const void *drawCB);
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
/* Reads the headless presenter output (RGBA8). */
bool rd__ReadPresent(void *dst, size_t dstSize, uint32_t *w, uint32_t *h);

/* Dumps (rd_dump.c).  Loading creates the frame's textures and temporary
 * targets in the current context and rewrites the ids in the commands. */
#define RD_DUMP_MAGIC "ICORDMP\0"
#define RD_DUMP_VERSION                                                                            \
    3u /* 2: RDC_ALPHA, RDC_SHADE, RdStateBlock.gouraud (wave 2); 3: VU meshes (wave 3) */
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

/* rd_replay.c (wave 5, R5c; RENDER_API.md "Full-screen effects and the raw packet builders"): a
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
