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
    /* recorded, not yet replayed (later waves): replay calls rd__NotImplemented */
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
 * (RENDER_API.md section 14). */
#define RD_SHADOW_STENCIL_MASK 0x3Fu

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
    uint32_t w, h; /* GS pixels; the texture size is w*scale, h*scale */
    RhiFormat format;
    RhiTexture color, depth;
    RhiState colorState, depthState;
    RhiTexture snap; /* copy used when a draw samples the target it renders to */
    RhiState snapState;
    uint32_t snapFor;    /* replay counter at which snap was taken */
    uint32_t viewTex[3]; /* RdTex ids of rd_TargetTexture, per RdTexView */
    uint32_t ownerFrame;
    uint8_t zFormat; /* RdZFormat of the depth buffer (R2c); 0 = PSMZ32 */
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
    uint8_t view;    /* RdTexView, RD_TEXKIND_TARGET */
    uint32_t target; /* RdTarget id, RD_TEXKIND_TARGET */
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
    char name[24];
} RdMeshRec;

#define RD_MAX_MESHES 16384

RdMeshRec *rd__MeshRec(uint32_t id);
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
    RD_FS_COUNT
} RdFsId;

/* How an RdBlend equation reaches the hardware (RENDER_API.md section 3). */
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
/* Wave 4 (R4b), the shadow count (rd_shadow.c, RENDER_API.md section 14):
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
/* Wave 4 (R4c), the depth fog (RENDER_API.md section 15): the draws of an
 * RD_POST_FOG under state s, as rd__PlanScreenDraw plans a fullscreen
 * sprite without a depth attachment (fog_lut_ps does the Z test against the
 * depth it reads), with fog_lut_ps as the fragment shader. */
int rd__FogPlan(const RdStateBlock *s, RhiFormat colorFmt, RdDrawPass out[2]);
/* The fog under ZFog.c's state (TEST 0x50000, ZMSK, ALPHA 0x44 with ABE);
 * appends to out[0..n). */
uint32_t rd__EnumerateReachableFog(RdPipeKeyInt *out, uint32_t max, uint32_t n);
/* rd_replay.c: frees the fog's depth copy and LUT textures (rd__GpuShutdown). */
void rd__FogShutdown(void);

/* ------------------------------------------------------------- context */
#define RD_SAMPLER_COUNT 16 /* mag x min x wrapS x wrapT */
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

    RdFrame frames[2];
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
    RhiSampler samplers[RD_SAMPLER_COUNT];
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
} RdContext;

extern RdContext g_rd;

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
    RD_ONCE_FOG,       /* an RD_POST_FOG without a LUT or a depth source */
    RD_ONCE_DEPTH_VIEW /* an ordinary draw sampling a depth view (only the fog reads one) */
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
/* Present pass inside the replay's command list (rd_present.c). */
void rd__PresentRecord(RhiCommandList cl);
bool rd__PresentAcquire(void);
void rd__PresentFinish(void);
void rd__PresentShutdown(void);
/* Moves a texture to a state with a barrier when needed (outside passes). */
void rd__Transition(RhiCommandList cl, RhiTexture t, RhiState *cur, RhiState want);
/* Ring allocation for the frame being replayed. */
uint64_t rd__RingAlloc(uint64_t size, uint64_t align);
/* Per-frame uniform bind groups for the presenter and posts. */
RhiBindGroup rd__FrameGroup(uint32_t targetW, uint32_t targetH, float originX, float originY);
/* The same with the GS Z scale of the bound depth buffer (R2c); rd__FrameGroup
 * uses 2^-24 (no depth bound). */
RhiBindGroup rd__FrameGroupZ(uint32_t targetW, uint32_t targetH, float originX, float originY,
                             float zScale);

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

/* rd_png.c: 8-bit RGBA (or RGB with withAlpha = 0), stored deflate. */
bool rd_WritePng(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t pitch,
                 int withAlpha);

#ifdef __cplusplus
}

#endif
#endif /* PORT_RENDER_RD_INTERNAL_H */
