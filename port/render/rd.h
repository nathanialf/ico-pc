/* rd.h: the native draw API the game's render layer (ico2/seki) calls.
 *
 * Model
 * -----
 * The PS2 game builds 13 priority-ordered DMA chain lists per frame
 * (seki/src/DisplayList.c) and kicks them in order 0..12 at dl_Swap.  The GS
 * is a single state machine, so register writes made by one list are still
 * in force when the next list starts.  Both facts are observable in the
 * original image, so rd keeps them: a frame is 13 ordered command lists
 * (RD_LIST_COUNT) recorded by the game thread and replayed in order against
 * one persistent RdDrawState (rd_state.h).  Within a list, submission order
 * is draw order; rd never sorts by material.
 *
 * Coordinates
 * -----------
 * Call sites keep passing what they passed to the GS: XYZ in 12.4 fixed
 * point relative to the 2048,2048 GS window centre (XYOFFSET), Z as the GS
 * 24-bit (PSMZ24 scale, 0xFFFFFF = far after the game's own projection) or
 * 32-bit value the call site computed, UVs in texels (12.4) or STQ.  One
 * conversion in rd_core applies the GS sampling rules (pixel centres at
 * integer coordinates, the game's +8/-4 half-texel nudges stay in the data)
 * and the resolution scale of the active preset.
 *
 * Defaults and leakage
 * --------------------
 * gsb_SetGsDefault (GsBase.c) writes TEST/ZBUF/FBA/TEXA at the head of lists
 * 0, 1, 2, 4, 6, 7, 8, 9, 10, 11, 12 only.  Lists 3 and 5 inherit whatever
 * the previous list left.  rd_core reproduces this by applying the same
 * defaults at rd_BeginFrame (through the normal rd_* state calls, recorded
 * into those lists) and nothing else.  The defaults, from GsBase.c:638-688:
 *   normal   (0, 7-12): TEST 0x50000  ZBUF write on  FBA 0 TEXA 80/80
 *   semitr   (1, 2):    TEST 0x5140D  ZBUF write on  FBA 0 TEXA 7F/81 AEM
 *   specular (4):       TEST 0x5C000  ZBUF write off FBA 0 TEXA 80/80
 *   particle (6):       TEST 0x50000  ZBUF write off FBA 0 TEXA 80/80
 *
 * Replay order and keep
 * ---------------------
 * rd_EndFrame(0) replays lists 0..12.  rd_EndFrame(1) ("fbKeep", pause and
 * memory card screens, DisplayList.c:128) replays lists 11 and 12 only, on
 * top of the retained DISPLAY target.
 *
 * Lists
 * -----
 *  0  opaque world and characters
 *  1  semitransparent skinned
 *  2  semitransparent static, points and lines
 *  3  shadow volumes into SHADOW0, then the shadow composite (inherits state)
 *  4  specular and reflection passes, puddle/pool reflection, fog
 *  5  dissolving objects (inherits state)
 *  6  particles
 *  7  shine level 1, then depth of field and motion blur
 *  8  shine level 2
 *  9  shine level 3
 * 10  anti-alias chain
 * 11  UI, subtitles, film noise, brightness, keep, fade, letterbox
 * 12  debug font
 *
 * Threads
 * -------
 * All rd_* calls are made from the game (simulation) fiber.  rd_EndFrame
 * hands the finished RdFrame to the presenter, which may render it several
 * times (interpolation) or once (Original preset).  The game's post passes
 * are appended from scheduler() on the PS2 (main.c:263-272); on the host
 * they are appended by the same code running inside ico_vsync before
 * rd_EndFrame, so there is one well-defined hand-off point.
 *
 * Every function names the seki (or other) entry points that will call it.
 */
#ifndef PORT_RENDER_RD_H
#define PORT_RENDER_RD_H

#include <stdbool.h>
#include <stdint.h>

#include "rd_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RD_LIST_COUNT 13

/* ----------------------------------------------------------------- types */

/* An RdKey identifies the same logical draw across consecutive frames so
 * the presenter can interpolate between them.  Call sites build it from
 * the object pointer, the part/packet index and a per-object ordinal
 * (RD_KEY()).  Key 0 means "do not interpolate; snap". */
typedef uint64_t RdKey;
#define RD_KEY(objptr, part, ordinal)                                                              \
    ((((uint64_t)(uintptr_t)(objptr)) << 16) ^ (((uint64_t)(part) & 0xFF) << 8) ^                  \
     ((uint64_t)(ordinal) & 0xFF))

typedef struct {
    uint32_t id;
} RdMesh;

typedef struct {
    uint32_t id;
} RdTex;

typedef struct {
    uint32_t id;
} RdTarget;

/* The five VU1 microprograms collapse to these programs; clip/no-clip and
 * scissor entry points merge because the GPU clips.  Codes are the MSCALF
 * values mc_SetMicroCode (MicroCode.c) selects. */
typedef enum RdProg {
    RD_PROG_PRELIT = 0,    /* normal_c: codes 32, 34, 36 */
    RD_PROG_LIT,           /* normal_l: codes 32, 36 */
    RD_PROG_LIT_SPEC,      /* normal_l: code 34 */
    RD_PROG_REFLECT,       /* normal_l NORMAL_REF: code 38 */
    RD_PROG_SKIN,          /* cluster: code 20 */
    RD_PROG_SKIN_SPEC,     /* cluster: code 22 (24 is the debug variant) */
    RD_PROG_GRID,          /* mesh: unlit */
    RD_PROG_GRID_LIT,      /* mesh: lit, specular variant selected by RdLights.specular */
    RD_PROG_PARTICLE,      /* particle */
    RD_PROG_SCREEN,        /* 2D sprites, lines, points in GS window space (gif_* helpers) */
    RD_PROG_WORLD_PRIM,    /* CPU-projected prims with a matrix (darkVolume, lightning, sun) */
    RD_PROG_SHADOW_VOLUME, /* stencil-only strips */
    RD_PROG_POST,          /* fullscreen passes (RdPostKind picks the shader variant) */
    RD_PROG_COUNT
} RdProg;

/* Mode 3 (code 18) is unconfirmed, see RENDER_API.md "Open items". */

typedef enum RdPrim {
    RD_PRIM_POINTS = 0,     /* GS PRIM 0 */
    RD_PRIM_LINES,          /* GS PRIM 1 */
    RD_PRIM_LINE_STRIP,     /* GS PRIM 2 */
    RD_PRIM_TRIANGLES,      /* GS PRIM 3 */
    RD_PRIM_TRIANGLE_STRIP, /* GS PRIM 4 */
    RD_PRIM_TRIANGLE_FAN,   /* GS PRIM 5 */
    RD_PRIM_SPRITES         /* GS PRIM 6: axis-aligned rectangle per two vertices */
} RdPrim;

/* 2D space tags.  UI-tagged draws are pre-flipped in mirror mode so they
 * read correctly after the final present flip, and are anchored to the
 * centred 4:3 box in widescreen.  WORLD-tagged screen prims came from a
 * CPU projection of world positions and follow the wide projection. */
typedef enum RdSpace { RD_SPACE_WORLD = 0, RD_SPACE_UI = 1, RD_SPACE_FULLSCREEN = 2 } RdSpace;

typedef enum RdStencilMode {
    RD_STENCIL_OFF = 0,
    RD_STENCIL_INCR,        /* shadow strip, sign > 0 */
    RD_STENCIL_DECR,        /* shadow strip, sign < 0 */
    RD_STENCIL_TEST_NONZERO /* shadow resolve */
} RdStencilMode;

typedef enum RdTargetFormat {
    RD_TFMT_RGBA8 = 0,
    RD_TFMT_RGBA8_INT,
    RD_TFMT_R8,
    RD_TFMT_COUNT
} RdTargetFormat;

/* Named targets that stand in for fixed GS VRAM regions.  Sizes are in GS
 * pixels; the preset scales them (RENDER_API.md "Buffers"). */
typedef enum RdTargetId {
    RD_TARGET_SCENE =
        0, /* FBP 0x40 / TBP 0x800: 512 x 512 (PAL) or 512 x 448 (NTSC), with depth+stencil */
    RD_TARGET_DISPLAY, /* FBP 0: 512 x H/2 reduced frame; the only displayed buffer; retained across frames */
    RD_TARGET_SHADOW0, /* FBP 0x142: shadow count/resolve, 256 x 256 */
    RD_TARGET_SHADOW1, /* 128 x 128 blur level */
    RD_TARGET_SHADOW2, /* 64 x 64 blur level */
    RD_TARGET_WORK0,   /* TBP 0x2800: 256 x 128 */
    RD_TARGET_WORK1,   /* TBP 0x2C00: 256 x 256 */
    RD_TARGET_WORK2,   /* TBP 0x3000 */
    RD_TARGET_WORK3,
    RD_TARGET_AA0,           /* 256 x 256 anti-alias downsample */
    RD_TARGET_AA1,           /* 128 x 128 */
    RD_TARGET_FEED128,       /* TBP 0x3F00: 128 x 128 aura feedback, persistent across frames */
    RD_TARGET_DATE_SNAPSHOT, /* R8 copy of SCENE alpha MSB, taken when a DATE consumer begins */
    RD_TARGET_COUNT
} RdTargetId;

typedef enum RdTexView { RD_VIEW_RGBA = 0, RD_VIEW_RGB24_TA0 = 1, RD_VIEW_DEPTH = 2 } RdTexView;

/* Transform block a draw carries; copied into the frame, so callers may
 * reuse their storage.  Matrices are column-major float[4][4] exactly as the
 * game's VU0 code lays them out (sceVu0FMATRIX). */
typedef struct RdXform {
    float local[16];     /* model to world */
    float localView[16]; /* model to view, as reg_set*MatrixPacket built it; used verbatim */
    float localProj
        [16]; /* model to GS clip, 4:3; the renderer substitutes its own projection in Enhanced */
} RdXform;

typedef struct RdLights {
    float dir[3][4]; /* three directional lights in model space, as the VU block holds them */
    float col[3][4];
    float ambient[4];
    float specular[4]; /* specular colour/power; .w = 0 means no specular */
    float eye[4];      /* eye position in model space for specular/reflection */
    float reflectParams[4];
} RdLights;

typedef struct RdMeshDesc {
    uint32_t vertexCount;
    const float (*pos)[4];       /* required */
    const float (*nrm)[4];       /* optional */
    const float (*uv)[4];        /* s, t, q, 0; optional */
    const uint8_t (*col)[4];     /* optional; GS RGBA (0x80 = 1.0) */
    const uint8_t (*cluster)[4]; /* bone indices for RD_PROG_SKIN; optional */
    const float (*weight)[4];    /* bone weights; optional */
    uint32_t stripCount;
    const uint16_t *stripStart; /* per strip: first vertex */
    const uint16_t *stripLen;   /* per strip: vertex count (>= 3) */
    uint32_t materialCount;
    const uint16_t
        *stripMaterial; /* per strip: material index into the owner's PObjMaterial table */
    const char *debugName;
} RdMeshDesc;

/* Per-material state that the mesh draw applies for each strip range.
 * Built from PObjMaterial.attr by Packet.c and passed at draw time because
 * the dissolve and shadow modes override it per frame. */
typedef struct RdMaterial {
    RdTex tex;     /* id 0 = untextured */
    uint8_t blend; /* RdBlend */
    uint8_t abe;
    uint8_t fba;
    uint8_t wrapS, wrapT; /* RdWrap */
    uint8_t texFn, tcc;
    uint8_t magFilter, minFilter;
    uint8_t mipLevel; /* fixed per texture from TexExt.level */
    uint8_t _pad[2];
    float uvOffset[2]; /* tex_TransTexture's uOfs/vOfs scroll */
} RdMaterial;

typedef struct RdScreenVtx {
    int32_t x, y;    /* GS 12.4 window coordinates */
    uint32_t z;      /* GS Z as the call site wrote it */
    float s, t, q;   /* STQ, or u/v in 12.4 when uvFixed is set on the batch */
    uint8_t rgba[4]; /* GS 0x80 = 1.0 */
} RdScreenVtx;

typedef struct RdWorldVtx {
    float pos[4];
    float st[2];
    uint8_t rgba[4];
} RdWorldVtx;

typedef struct RdParticleBatch {
    uint32_t count;
    const float (*pos)[4];
    const float (*size)[2];
    const uint8_t (*rgba)[4];
    const float (*uvRect)[4];
    RdTex tex;
    const float *viewMtx; /* billboard basis */
    uint8_t blend, abe, zwrite, _pad;
} RdParticleBatch;

/* Mesh3D is the game's procedural grid (cloth, water, flags); declared in
 * ico2/seki/include/Primitive.h.  rd reads vertex arrays through it. */
struct Mesh3D;

typedef enum RdPostKind {
    RD_POST_REDUCTION = 0, /* SCENE -> DISPLAY, bilinear, tint, border crop; gsb_Reduction */
    RD_POST_KEEP,          /* DISPLAY drawn back at colour 112; gsb_KeepFrameBuffer */
    RD_POST_FADE,          /* gsb_fade */
    RD_POST_LETTERBOX,     /* gsb_scissorOnDemo: 58-line bars */
    RD_POST_BRIGHTNESS,    /* gsb_controlBrightness */
    RD_POST_FILM_NOISE,    /* gsb_filmNoise */
    RD_POST_AA_DOWNSAMPLE, /* gsb_antiAlias chain step */
    RD_POST_AA_COMPOSITE,
    RD_POST_FOG,            /* fog_DrawFog: depth-indexed LUT */
    RD_POST_SHADOW_RESOLVE, /* stencil != 0 -> SHADOW0 */
    RD_POST_BLUR,           /* staticBlur.c / Shadow.c downsample-blur step between two targets */
    RD_POST_COMPOSITE_FIX,  /* textured quad with LERP_FIX blend (motion blur, DoF planes, flare) */
    RD_POST_COPY,           /* texture-to-texture copy standing in for gif_MoveImage / VRAM grabs */
    RD_POST_PRESENT_BLIT,   /* internal: DISPLAY -> backbuffer with aspect, scale, mirror */
    RD_POST_COUNT
} RdPostKind;

typedef struct RdPostParams {
    RdTarget src, dst;
    RdTexView srcView;
    uint8_t rgba[4]; /* tint / fade / FIX colour */
    uint8_t fix;     /* blend FIX */
    uint8_t blend;   /* RdBlend */
    uint8_t abe;
    uint8_t exactInt;   /* run the GS integer blend in the shader (feedback passes) */
    uint32_t z;         /* GS Z for depth-tested quads (DoF planes, fog far plane) */
    float rect[4];      /* destination rectangle in GS pixels of dst (x, y, w, h) */
    float uv[4];        /* source rectangle in texels of src */
    const uint8_t *lut; /* 256 x RGBA for RD_POST_FOG */
    float scalar[4];    /* kind-specific: fog strength, grain scale, blur offsets */
    uint32_t lines;     /* RD_POST_LETTERBOX bar height, GS lines */
} RdPostParams;

typedef struct RdCamera {
    float view[16];
    float proj43[16]; /* the game's 4:3 projection (gsb_SetVSMatrix) */
    float zoom,
        aspect43; /* inputs that let the renderer rebuild a wider projection with the same vertical FOV */
    float nearZ, farZ;
    uint8_t cut; /* 1 on a camera cut this tick; disables interpolation for the frame */
    uint8_t _pad[3];
} RdCamera;

typedef enum RdPreset { RD_PRESET_ORIGINAL = 0, RD_PRESET_ENHANCED = 1 } RdPreset;

typedef struct RdSettings {
    RdPreset preset;
    uint32_t outputWidth, outputHeight; /* window/backbuffer */
    float aspect;                       /* 4/3 .. 16/9; Original forces 4/3 */
    uint8_t interpolate;                /* uncapped presentation; Original forces 0 */
    uint8_t mirror;                     /* mirror mode: final blit flips x, UI pre-flipped */
    uint8_t filterUpgrade;              /* trilinear/anisotropic with generated mips (Enhanced) */
    uint8_t fullHeightScene; /* skip the vertical halving of the reduction pass (Enhanced) */
    uint8_t vsync;
    uint8_t _pad[3];
} RdSettings;

/* ------------------------------------------------------------ lifecycle */

/* gsb_InitGSSystem / gsb_Init: creates the named targets for the given GS
 * scene size (512x512 PAL, 512x448 NTSC) and loads the shaders.  Returns
 * false if the RHI lacks dual-source blend or stencil wrap. */
bool rd_Init(uint32_t gsWidth, uint32_t gsHeight, const RdSettings *settings, void *sdlWindow);
void rd_Shutdown(void);
/* gsb_Init on a 50/60 Hz switch (kanbanBoot's gsResetFunc) recreates the
 * scene-sized targets. */
void rd_ResetScene(uint32_t gsWidth, uint32_t gsHeight);
/* Settings menu: applies at the next rd_BeginFrame. */
void rd_SetSettings(const RdSettings *settings);
const RdSettings *rd_GetSettings(void);

/* gsb_SetGsDefault / dl_Swap at the start of a tick: clears the 13 lists and
 * records the per-list defaults listed above. */
void rd_BeginFrame(void);
/* dl_Swap: closes the frame.  keep != 0 replays lists 11..12 only (fbKeep).
 * Hands the RdFrame to the presenter; returns immediately. */
void rd_EndFrame(int keep);
/* gsb_SetVSMatrix: the camera for this frame (used by Enhanced projection,
 * interpolation and the WORLD-space 2D conversion). */
void rd_SetCamera(const RdCamera *cam);

/* ------------------------------------------------------------- lists */

/* dl_SetDLPriority(pri): selects the list that subsequent calls record into. */
void rd_SelectList(int list);
int rd_CurrentList(void);

/* --------------------------------------------------------- state deltas
 * Each records a state change into the current list.  Callers:
 *   rd_Blend     gif_SetAlpha, material packets (Packet.c), raw ALPHA writes
 *   rd_Test      gif_SetZTest, gsb_set*Reg, raw TEST writes (rd_TestFromGs)
 *   rd_ZWrite    gif_SetZWrite, raw ZBUF writes
 *   rd_FBA       material packets, raw FBA writes
 *   rd_PABE      specular/reflection passes (RegistPacket.c), raw PABE writes
 *   rd_ColClamp  shadow_Reset (Shadow.c)
 *   rd_TexA      gsb_set*Reg, 2D sprite paths, raw TEXA writes
 *   rd_Sampler   tex_TransTexture (TexExt filters), raw TEX1/CLAMP writes
 *   rd_Texture   tex_TransTexture, raw TEX0 writes (hand-converted sites only)
 *   rd_UVOffset  tex_TransTexture uOfs/vOfs
 *   rd_ColorMask raw FRAME.FBMSK writes (darkVolume.c)
 */
void rd_Blend(RdBlend eq, uint8_t fix, int abe);
void rd_Test(const RdTestState *test);
void rd_TestGs(uint64_t gsTestWord); /* convenience: rd_Test(rd_TestFromGs(v)) */
void rd_ZWrite(int on);
void rd_FBA(int on);
void rd_PABE(int on);
void rd_ColClamp(int on);
void rd_TexA(RdTexA mode);
void rd_Sampler(RdFilter mag, RdFilter min, RdWrap s, RdWrap t);
void rd_Texture(RdTex tex, RdTexFn fn, RdTcc tcc);
void rd_TextureOff(void);
void rd_UVOffset(float u, float v);
void rd_ColorMask(uint32_t fbmsk);

/* ------------------------------------------------------------ targets */

/* Named target handle; gsb_SetFrame, gif_SetDrawEnviroment with the fixed
 * TBPs (0, 0x800, 0x2800, 0x2C00, 0x3000, 0x3F00, 0x142 ...) map to these. */
RdTarget rd_Target(RdTargetId id);
/* tex_AllocVramAuto / per-priority VRAM bump allocation for render-to-texture
 * (puddle.c, pool.c, queen_barrier_disp.c): a target that lives until
 * rd_BeginFrame unless keepAcrossFrames is set. */
RdTarget rd_TempTarget(uint32_t gsW, uint32_t gsH, int withDepth, int keepAcrossFrames);
/* gif_SetDrawEnviroment(tbp, psm, w, h, ...) / gsb_SetFrame: direct
 * subsequent draws in the current list to the target.  gsW/gsH are the GS
 * size the caller believes it is drawing into; useOffset applies the
 * XYOFFSET centre (2048 - w/2, 2048 - h/2) the game sets alongside. */
void rd_SetTarget(RdTarget color, RdTarget depth, uint32_t gsW, uint32_t gsH, int useOffset);
/* Note (wave 1, from GifPacket.c gif_SetDrawEnviroment): the GS always
 * centres XYOFFSET at (2048 - w/2, 2048 - h/2); useOffset adds the
 * screenOffsetX/Y field offset on top, which the preset supplies (zero in
 * Original until RENDER_API.md open item 4 is settled).  depth names the
 * target whose depth buffer is bound (pass the colour target again for
 * SCENE; id 0 = none).  The call also resets the scissor to gsW x gsH, as
 * gif_SetDrawEnviroment writes SCISSOR_1. */
/* gsb_UpdateGSSystem's clears / gif_Sprite full-screen clears at the frame
 * head: clear colour and optionally depth+stencil of a target. */
void rd_ClearTarget(RdTarget t, const uint8_t rgba[4], int clearDepth, uint32_t z);
/* Use a target as a texture (puddle/pool reflection, motion blur history
 * reading DISPLAY as RGB24, fog/DoF reading SCENE depth). */
RdTex rd_TargetTexture(RdTarget t, RdTexView view);

/* ------------------------------------------------------------- textures */

/* tex_loadImage / TIM2 decode in Texture.c: upload an RGBA8 image.  texaMode
 * tells the cache which TEXA variant this expansion was baked with (only
 * meaningful for PSMCT16/24 sources). */
RdTex rd_CreateTexture(uint32_t w, uint32_t h, const void *rgba8, RdTexA texaMode,
                       const char *debugName);
/* Added in wave 1 (R1b): a texture whose alpha byte is still the source
 * format's, expanded by the shader under the TEXA state in force at replay
 * (so TEXA leaks between lists exactly as on the GS) instead of being baked
 * per TEXA mode.  RGB24: the alpha byte is ignored (TA0, AEM).  RGBA16: the
 * alpha byte holds the 1-bit A (0 or 1; TA1 when set, else TA0).  The values
 * equal TEXFMT_* in port/shaders/gs_math.hlsli. */
typedef enum RdTexSrc { RD_TEXSRC_RGBA32 = 0, RD_TEXSRC_RGB24 = 1, RD_TEXSRC_RGBA16 = 2 } RdTexSrc;
RdTex rd_CreateTextureSrc(uint32_t w, uint32_t h, const void *rgba8, RdTexSrc src,
                          const char *debugName);
/* tex_scrollClut: the CLUT changed, re-expanded pixels follow. */
void rd_UpdateTexture(RdTex t, const void *rgba8);
void rd_DestroyTexture(RdTex t);

/* -------------------------------------------------------------- meshes */

/* pac_MakePacket (Packet.c): one RdMesh per PObjPart, built once at load. */
RdMesh rd_CreateMesh(const RdMeshDesc *desc);
void rd_DestroyMesh(RdMesh m);
/* reg_dispNObj / reg_dispLObj / reg_dispCObj paths (RegistPacket.c): the
 * static or lit mesh draw.  materials has desc->materialCount entries. */
void rd_DrawMesh(RdMesh m, RdProg prog, const RdXform *xf, const RdLights *lights,
                 const RdMaterial *materials, RdKey key);
/* reg_setCMatrixPacket path: skinned draw with bone matrices (model space,
 * as the cluster microprogram received them). */
void rd_DrawSkinned(RdMesh m, RdProg prog, const RdXform *xf, const float (*bones)[16],
                    uint32_t boneCount, const RdLights *lights, const RdMaterial *materials,
                    RdKey key);
/* prim_DispMesh3D (Primitive.c): procedural grid; vertex arrays are read
 * from the Mesh3D at call time and copied into the frame. */
void rd_DrawGrid(const struct Mesh3D *grid, const RdXform *xf, const RdLights *lights,
                 const RdMaterial *mat, RdKey key);
/* prim_DispParticle (Primitive.c) and particleEffect.c after conversion. */
void rd_DrawParticles(const RdParticleBatch *batch, RdKey key);

/* --------------------------------------------------- immediate prims */

/* gif_Sprite, gif_Draw*Strip*, gif_Line, gif_Point, layout_texture.c,
 * jimaku.c, DisplayFont.c, staticBlur.c quads: primitives in GS window
 * space.  space tags UI vs WORLD for mirror/widescreen. uvFixed: s,t are
 * 12.4 texels (UV register) rather than STQ. */
void rd_ScreenPrims(RdPrim type, const RdScreenVtx *v, uint32_t count, RdSpace space, int uvFixed,
                    RdKey key);
/* darkVolume.c, lightning.c, lineManager.c, sun flare: world-space
 * vertices with the matrix the call site used, transformed on the GPU. */
void rd_WorldPrims(RdPrim type, const RdWorldVtx *v, uint32_t count, const float *mtx, RdKey key);
/* shadow_RenderVolume (Shadow.c): a clipped, extruded strip; sign picks
 * stencil increment or decrement.  Depth-tested against SCENE, no writes
 * to colour or depth. */
void rd_ShadowStrip(const float (*v)[4], uint32_t count, float sign, RdKey key);

/* --------------------------------------------------------------- post */

/* gsb_PostEffect family, staticBlur.c, ZFog.c, Shadow.c composites, and
 * gif_MoveImage: fullscreen or rectangle passes between targets.  Recorded
 * into the current list like any draw. */
void rd_Post(RdPostKind kind, const RdPostParams *params);

/* ------------------------------------------------------- verification */

/* rd can serialise the current RdFrame (all lists, payload arena, textures
 * referenced by id) so tools/verify can replay it headless on another
 * backend.  Dumps contain game assets and are never committed. */
bool rd_DumpFrame(const char *path);
/* Readback of the DISPLAY target or backbuffer after present, tightly
 * packed RGBA8, for screenshots and image comparison. */
bool rd_ReadDisplay(void *dst, uint32_t *w, uint32_t *h);

/* --------------------------------------------------------- statistics */
typedef struct RdStats {
    uint32_t draws, pipelines, pipelineCreates, textureUploads, tempTargets, bytesPayload;
    float gpuMs;
} RdStats;

const RdStats *rd_GetStats(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RENDER_RD_H */
