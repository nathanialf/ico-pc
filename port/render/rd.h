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

/* Mode 3 (code 18) is particle's BEGIN_PARTICLE, RD_PROG_PARTICLE (RENDER_API.md
 * section 8, item 1). */

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
    RD_STENCIL_INCR,         /* shadow strip, sign > 0 */
    RD_STENCIL_DECR,         /* shadow strip, sign < 0 */
    RD_STENCIL_TEST_NONZERO, /* shadow resolve */
    /* Wave 4 (R4b): the resolve's bit passes, RD_STENCIL_RESOLVE_BIT0 + k
     * for count bit k (0..5): stencil EQUAL with read mask 1 << k
     * (RENDER_API.md section 14) */
    RD_STENCIL_RESOLVE_BIT0
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
    /* Wave 4 (R4b): the three blur levels of shadow_Draw.  The count itself
     * (FBP 0x142, scene-sized) is a per-frame target, rd_ShadowCountTarget. */
    RD_TARGET_SHADOW0, /* FBP 0x1C2 / TBP 0x3840: blur level 1, 256 x 256 */
    RD_TARGET_SHADOW1, /* FBP 0x1E2 / TBP 0x3C40: blur level 2, 128 x 128 */
    RD_TARGET_SHADOW2, /* FBP 0x1EA / TBP 0x3D40: blur level 3, 64 x 64 */
    /* Wave 5 (R5a): staticBlur.c's work buffers after FullScreenEffectBefore
     * (workBase 0x2800, 0x2A00, 0x2E00, 0x3000; RENDER_API.md section 17).
     * GifPacket.c's decoder still maps FBP 0x160 / TBP 0x2C00 to WORK1 and
     * FBP 0x180 / TBP 0x3000 to WORK2, from wave 2; staticBlur.c no longer
     * goes through it. */
    RD_TARGET_WORK0,         /* TBP 0x2800 TBW 4: 256 x 128 */
    RD_TARGET_WORK1,         /* TBP 0x2A00 TBW 4: 256 x 256 (256 x 128 is its top half) */
    RD_TARGET_WORK2,         /* TBP 0x2E00 TBW 8: scene-sized flare mask, drawn with SCENE's Z */
    RD_TARGET_WORK3,         /* TBP 0x3000 TBW 4: 256 x 128 eye blur / flare result */
    RD_TARGET_AA0,           /* 256 x 256 anti-alias downsample */
    RD_TARGET_AA1,           /* 128 x 128 */
    RD_TARGET_FEED128,       /* TBP 0x3F00: 128 x 128 aura feedback, persistent across frames */
    RD_TARGET_DATE_SNAPSHOT, /* R8 copy of SCENE alpha MSB, taken when a DATE consumer begins */
    /* wave 5 (R5a), appended so the older ids stay */
    RD_TARGET_AURA_WORK, /* TBP 0x2A00 TBW 8: scene-sized aura buffer (list 8), SCENE's Z */
    RD_TARGET_AURA_TAP,  /* TBP 0x2800 TBW 2: 128 x 128 aura tap buffer */
    RD_TARGET_WORK2_PAD, /* TBP 0x2E00 + W H / 64: fillWork2's 256 x 64 band (never read) */
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
    RD_POST_FOG,            /* fog_DrawFog: depth-indexed LUT (wave 4, R4c: see rd_Post) */
    RD_POST_SHADOW_RESOLVE, /* stencil != 0 -> SHADOW0 */
    RD_POST_BLUR,           /* staticBlur.c / Shadow.c downsample-blur step between two targets */
    RD_POST_COMPOSITE_FIX,  /* textured quad with LERP_FIX blend (motion blur, DoF planes, flare) */
    RD_POST_COPY,           /* texture-to-texture copy standing in for gif_MoveImage / VRAM grabs */
    RD_POST_PRESENT_BLIT,   /* internal: DISPLAY -> backbuffer with aspect, scale, mirror */
    /* Wave 5 (R5a): one GS sprite of staticBlur.c, drawn in the GS integer
     * arithmetic (see rd_Post below); the kind names the effect it belongs
     * to, all six replay alike */
    RD_POST_MOTION_BLUR, /* MotionBlur: DISPLAY as RGB24, H/2 stretched to H, LERP FIX */
    RD_POST_DOF,         /* depthField: SCENE -> WORK1 -> WORK0, six blur passes, four planes */
    RD_POST_FLARE,       /* makeFullScreenFlare* / pasteFullScreenFlare, modes 0 and 1 */
    RD_POST_BLOOM,       /* the same in mode 2 (postEffect 4/5, GLOW: HIGHLIGHT adds Af) */
    RD_POST_AURA,        /* auraInspireBefore/After: AURA_WORK, AURA_TAP, FEED128 feedback */
    RD_POST_EYE_BLUR,    /* eyeBlur: WORK0 -> WORK3, the sun ghosts and tint */
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
    uint8_t interpolate;                /* uncapped presentation (rd_Present, R7b), both presets */
    uint8_t mirror;                     /* mirror mode: final blit flips x, UI pre-flipped */
    uint8_t
        filterUpgrade; /* RdFilterUpgrade: trilinear/anisotropic with generated mips (Enhanced) */
    uint8_t fullHeightScene; /* skip the vertical halving of the reduction pass (Enhanced) */
    uint8_t vsync;
    uint8_t _pad[3];
    /* Wave 7 (R7a): the internal scene resolution in Enhanced, in texels:
     * the scene's texture is sceneWidth x sceneHeight (GS coordinates
     * unchanged, RENDER_API.md section 19); 0 x 0 = the output's: the
     * presentation box in the window. */
    uint32_t sceneWidth, sceneHeight;
    /* > 0: the scene's texture is this factor of the GS size instead
     * (vertically; horizontally times aspect / (4/3)), e.g. 2 */
    float sceneScale;
} RdSettings;

/* RdSettings.filterUpgrade (wave 7, R7a) */
typedef enum RdFilterUpgrade {
    RD_FILTER_UPGRADE_OFF = 0, /* per texture as authored (Original) */
    RD_FILTER_UPGRADE_TRILINEAR = 1,
    RD_FILTER_UPGRADE_ANISOTROPIC = 2
} RdFilterUpgrade;

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
/* Where the GPU work the game's calls cause runs (rd_EndFrame's replay and
 * present in the Original preset, rd_BeginFrame's target re-creation, the
 * FMV picture's present; docs/port/PLATFORM.md "Fiber stacks and host
 * calls").  The game calls rd from a fiber with a 256 KB stack; the driver
 * (pipeline creation, present) may need far more.  The window build sets
 * call to ico_sched_call_on_host, which runs fn(arg) on the host stack in
 * the host FP mode and returns; NULL (the default, tests and tools) calls
 * fn directly. */
typedef void (*RdHostCall)(void (*fn)(void *arg), void *arg);
/* Creates every pipeline of the reachable set (rd__EnumerateReachable) now,
 * so the first frames that use them do not stall on the driver's pipeline
 * compile; logs the count and the time.  The window calls it after
 * rd_Init; the tests do not.  Returns the number created. */
uint32_t rd_PrecreatePipelines(void);
/* The longest frame replay (CPU time of recording, pipeline creation,
 * submit and present) in ms since the last reset, and *count the replays
 * in that time; reset != 0 starts a new period.  For the window's 10 s
 * statistics line. */
double rd_ReplayTimeMax(int reset, uint32_t *count);
void rd_SetHostCall(RdHostCall call);
/* Added in wave 2 (R2a).  dl_Clear without dl_Swap (gsb_UpdateGSSystem(1)
 * on the movie path, gsb_UpdateGSSystem before gsSystemReady): the lists
 * recorded since rd_BeginFrame are dropped unreplayed, as the PS2 drops a
 * display list it never kicks.  No-op when no frame is open. */
void rd_DiscardFrame(void);
/* True between rd_BeginFrame and rd_EndFrame / rd_DiscardFrame. */
bool rd_FrameOpen(void);
/* The window's drawable size changed (SDL pixel-size event): resizes the
 * swapchain and the presenter's output box at once.  Host loop only, never
 * from inside a frame's replay. */
void rd_ResizeOutput(uint32_t width, uint32_t height);
/* gsb_SetVSMatrix: the camera for this frame (used by Enhanced projection,
 * interpolation and the WORLD-space 2D conversion).
 * Wave 2 (R2c): called from gsb_MakeCommonMatrix, the point where the view
 * (matrixptr+0x80) and the screen matrix (+0xC0) are both final; view is
 * +0x80, proj43 the GS screen matrix +0xC0 (view space to GS window
 * coordinates and GS Z after the divide by w), zoom vsParam[0], aspect43
 * 4/3, nearZ/farZ vsParam[7]/[8].  Replay puts them in FrameCB: g_view,
 * g_proj = proj43, g_viewProj = proj43 x view, g_cameraPos = the eye from
 * the view's inverse (w = cut), g_clip = near, far, zoom, aspect. */
void rd_SetCamera(const RdCamera *cam);

/* ------------------------------------------ interpolation (wave 7, R7b)
 * docs/port/RENDER_API.md section 20.  The simulation keeps its tick; with
 * RdSettings.interpolate in the Enhanced preset rd_EndFrame only closes the
 * frame, and the host presents as often as it likes with rd_Present(alpha):
 * the last closed frame replayed with every keyed draw blended from the
 * frame before it by alpha in [0, 1) (0 = the previous frame's data, so the
 * picture is one tick late).  Unkeyed draws, CLUT animation, film noise,
 * dissolve and every other state stay at the tick's values.
 *
 * rd_InterpolationActive  RdSettings.interpolate, either preset (as
 *                         applied at the last rd_BeginFrame)
 * rd_Present              replays and presents; false (nothing done) when
 *                         interpolation is off, before the first frame, or
 *                         while an FMV picture is on the output.  Each call
 *                         advances the feedback passes (motion blur) by the
 *                         time since the previous one
 * rd_FrameNumber          the last closed frame's RdFrame number, 0 before
 *                         the first: the host's tick clock
 * rd_CameraCut            the frame being recorded starts a new shot (a hard
 *                         camera cut or a stage change): it is not blended
 *                         from the one before (RdCamera.cut).  Outside an
 *                         open frame it applies to the next one.  Called by
 *                         the window from the game's cut signal
 *                         (port/game/video_options.h ico_video_camera_cut) */
bool rd_InterpolationActive(void);
bool rd_Present(float alpha);
uint32_t rd_FrameNumber(void);
void rd_CameraCut(void);

/* ------------------------------------------- mirror mode (wave 7, R7c)
 * docs/port/RENDER_API.md section 21.  The game's mirror mode (chosen at New
 * Game, port/game/options.h ico_opt_mirror) flips the presented picture
 * horizontally: the presenter's step 2 samples DISPLAY right to left, so
 * every present (Original, Enhanced, interpolated) is mirrored; RD_SPACE_UI
 * screen prims drawn into SCENE or DISPLAY are flipped about the target's
 * centre at replay, so they read normally after the present flip.  Nothing
 * else changes (winding, culling, shadows, DATE, feedback).  rd_SetMirror is
 * the run's value, independent of RdSettings (the window rebuilds those from
 * the display options); the mirror is on when either RdSettings.mirror
 * (tests, the replay tool) or rd_SetMirror's flag is set.  It applies from
 * the next replay or present. */
void rd_SetMirror(int on);
bool rd_MirrorActive(void);

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

/* Added in wave 2 (R2a), for GifPacket.c's GS register decoding (raw
 * gif_SetGsReg writes set one register, never a group):
 *   rd_ABE           PRIM.ABE alone (rd_Blend sets the equation and ABE together)
 *   rd_BlendFunc     the ALPHA register alone: equation and FIX, ABE untouched
 *   rd_Scissor       SCISSOR_1, GS pixels of the bound target, inclusive
 *                    (rd_SetTarget resets it to the target's size)
 *   rd_SamplerFilter TEX1 alone (MMAG, MMIN base filter); wrap untouched
 *   rd_SamplerWrap   CLAMP alone; filters untouched
 *   rd_Gouraud       PRIM.IIP: 1 = Gouraud (the default), 0 = flat, where the
 *                    GS takes the colour of a primitive's last vertex
 *                    (triangles, strips, fans, lines; sprites always use the
 *                    second vertex) */
void rd_ABE(int abe);
void rd_BlendFunc(RdBlend eq, uint8_t fix);
void rd_Scissor(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
void rd_SamplerFilter(RdFilter mag, RdFilter min);
void rd_SamplerWrap(RdWrap s, RdWrap t);
void rd_Gouraud(int iip);

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

/* Wave 3 (R3ab): the mesh path is rd_mesh.h's (the VU1 program shaders on
 * the packets the game builds, docs/port/RENDER_API.md section 13).  The
 * four semantic draw calls below are kept for an Enhanced path and record
 * nothing; rd_CreateMesh keeps a record without geometry. */

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
/* Wave 7 (R7a): while space >= 0, rd_ScreenPrims records that space instead
 * of its argument (the game marks its full-screen 2D items RD_SPACE_FULLSCREEN
 * around the gif helpers that draw them: layout_texture.c's primary sprite).
 * -1 ends it.  Returns the previous value. */
int rd_SetSpaceOverride(int space);
/* darkVolume.c, lightning.c, lineManager.c, sun flare: world-space
 * vertices with the matrix the call site used, transformed on the GPU. */
void rd_WorldPrims(RdPrim type, const RdWorldVtx *v, uint32_t count, const float *mtx, RdKey key);
/* shadow_RenderVolume (Shadow.c): a clipped, extruded strip; sign picks
 * stencil increment or decrement.  Depth-tested against SCENE, no writes
 * to colour or depth. */
void rd_ShadowStrip(const float (*v)[4], uint32_t count, float sign, RdKey key);
/* Wave 4 (R4b): v[i] = (GS 12.4 window X, Y, GS Z, unused) as floats, one
 * triangle strip, every triangle counted with sign; replayed since R4b.
 * Shadow.c uses the exact form below. */

/* ------------------------------------------------- shadows (wave 4, R4b)
 * Shadow.c's count (docs/port/RENDER_API.md section 14).  The PS2 adds each
 * volume face into FBP 0x142 with ALPHA 0x68 FIX 0x80 and COLCLAMP 0, the
 * face colour 0x04 or 0xFC by its facing, so a pixel ends at 4 n mod 256
 * for n the net count of the faces in front of the scene.  rd keeps n mod
 * 64 in the stencil of the bound depth target (INCR_WRAP / DECR_WRAP under
 * write mask 0x3F) and writes the same colour at the resolve.
 *
 * rd_ShadowCountTarget  the per-frame stand-in for FBP 0x142: an RGBA8
 *                       target of the scene size gsW x gsH, created by the
 *                       first call in the open frame (rd_TempTarget); {0}
 *                       outside a frame
 * rd_ShadowReset        shadow_Reset's clear of FBP 0x142: the stencil of
 *                       the bound depth target to 0 (the colour target is
 *                       written whole by the resolve)
 * rd_ShadowTris         shadow_RenderVolume's strips as triangles: v holds
 *                       3 x triCount XYZ2 (x, y, z; the rest unused), sign[t]
 *                       > 0 for a triangle whose last vertex had the colour
 *                       0x04 (flat shading), < 0 for 0xFC.  Depth-tested
 *                       with the state's TEST against the bound depth
 *                       target, no colour or depth writes
 * rd_ShadowResolve      the count into the bound colour target: RGB =
 *                       4 n mod 256, A = 0x80 where that is not 0, else 0
 *                       (the TEXA expansion shadow_Draw's PSMCT24 read of
 *                       FBP 0x142 applies, TA0 0x80 AEM, baked so the chain
 *                       filters expanded texels as the GS does) */
RdTarget rd_ShadowCountTarget(uint32_t gsW, uint32_t gsH);
void rd_ShadowReset(void);
void rd_ShadowTris(const RdScreenVtx *v, const int8_t *sign, uint32_t triCount, RdKey key);
void rd_ShadowResolve(void);

/* ------------------------- render-to-texture surfaces (wave 5, R5b; rd_water.c)
 * puddle.c, pool.c and queen_barrier_disp.c draw into a VRAM block from
 * tex_AllocVramAuto right after tex_ResetVramPri, which is always TBP 0x2800
 * in their lists (4 and 10), and sample it with TEX0 at that block.  The
 * register decoder (GifPacket.c) maps that block to the named AA0 target,
 * which has no depth buffer and is 256 x 256 (the queen barrier's block is
 * 512 x 256).  These calls let the game files bind a per-frame target of the
 * block's own size in its place (docs/port/RENDER_API.md section 16):
 *
 * rd_GsNamedBlock  the named target GifPacket.c's decoder maps a FRAME at
 *                  block tbp (FBP = tbp / 32) of gsW x gsH to (its
 *                  gsTargetOfFbp table: 0, 0x800, 0x2800, 0x2840, 0x2C00,
 *                  0x3000, 0x3F00); {0} when the decoder makes its own
 *                  temporary target for the block (R3ab)
 * rd_BlockTarget   the target standing for VRAM block tbp of gsW x gsH in
 *                  the open frame: created by the first call (rd_TempTarget,
 *                  with a depth buffer when withDepth), the same target for
 *                  the same (tbp, gsW, gsH) for the rest of the frame, as
 *                  the VRAM block is one block whoever draws into it; {0}
 *                  outside a frame
 * rd_AliasTarget   from here on, in the current list and the open frame,
 *                  rd_SetTarget and rd_ClearTarget of `from` and rd_Texture
 *                  of a view of `from` (rd_TargetTexture) record `to`
 *                  instead; rd_SetTarget binds `to`'s depth buffer when it
 *                  was given none or `from`.  to = {0} ends it.  Recording
 *                  only: the commands name `to`, so replay and dumps see an
 *                  ordinary target.  At most 4 aliases a list
 * rd_PushCamera    the camera of the draws recorded from here until the
 *  rd_PopCamera    matching rd_PopCamera in the current list (puddle.c and
 *                  pool.c call gsb_SetVSMatrix mid-frame for their
 *                  reflection views and restore the matrices by copying).
 *                  The frame camera (rd_SetCamera, gsb_MakeCommonMatrix) is
 *                  untouched; the scope is kept with the open frame for the
 *                  Enhanced projection and interpolation (not dumped).  The
 *                  Original preset draws from the matrices the VU packets
 *                  carry and does not read it.  Nesting depth 4, 8 scopes a
 *                  frame */
RdTarget rd_GsNamedBlock(uint32_t tbp, uint32_t gsW, uint32_t gsH);
RdTarget rd_BlockTarget(uint32_t tbp, uint32_t gsW, uint32_t gsH, int withDepth);
void rd_AliasTarget(RdTarget from, RdTarget to);
void rd_PushCamera(const RdCamera *cam);
void rd_PopCamera(void);

/* --------------------------------------------------------------- post */

/* gsb_PostEffect family, staticBlur.c, ZFog.c, Shadow.c composites, and
 * gif_MoveImage: fullscreen or rectangle passes between targets.  Recorded
 * into the current list like any draw. */
void rd_Post(RdPostKind kind, const RdPostParams *params);
/* Wave 4 (R4c), RD_POST_FOG (fog_DrawFog, docs/port/RENDER_API.md section
 * 15): the fog sprite alone, drawn with the state in force (the caller
 * records ZFog.c's register writes as rd state first: the colour target,
 * TEX0 as rd_TargetTexture(SCENE, RD_VIEW_DEPTH) with MODULATE and TCC RGBA,
 * TEX1, ALPHA, TEST, ZBUF mask, ABE).  The texel the sprite reads at UV
 * (u, v) is LUT[(Z >> 16) & 0xFF], Z the 32-bit GS Z of the depth view's
 * target at that texel (the PSMT8H read of the Z copy after ZFog.c's PSMT4
 * byte copy); the Z test compares params->z with that target's depth.
 * Fields:
 *   lut      256 x RGBA in index order (the CLUT as the GS looks it up)
 *   rgba     RGBAQ of the sprite (0x80, 0x80, 0x80, fogStrength)
 *   z        the sprite's GS Z (0xFFFFFF)
 *   rect     the two XYZ2 corners as the GS gets them, 12.4 window
 *            coordinates: x0, y0, x1, y1
 *   uv       the two UVs, 12.4 texels: u0, v0, u1, v1 */
/* Wave 5 (R5a), RD_POST_MOTION_BLUR .. RD_POST_EYE_BLUR (staticBlur.c,
 * docs/port/RENDER_API.md section 17): one GS sprite, PRIM 0x116 or 0x406
 * with ABE as gif_SpriteSensitiveOrg sends it, drawn with the state in force
 * (target, depth, texture, TEX1, CLAMP, TEXA, ALPHA, ABE, PABE, FBA, TEST,
 * COLCLAMP, ZBUF, scissor: the caller records staticBlur.c's register
 * writes as rd state first) in the GS integer arithmetic: GS coverage, the
 * sprite's UV stepped in 12.4, nearest or the GS bilinear on 4-bit
 * fractions with TEXA applied to each texel before filtering, the texture
 * function (MODULATE, DECAL, HIGHLIGHT, HIGHLIGHT2), the alpha test, DATE
 * and the blend on integers against the destination as it was before the
 * sprite.  Fields:
 *   rect     the two XYZ2 corners as the GS gets them (12.4): x0, y0, x1, y1
 *   uv       the two UVs (12.4 texels): u0, v0, u1, v1
 *   rgba     RGBAQ
 *   z        the GS Z of the second vertex
 *   scalar   [0], [1] the TEX0 size 2^TW, 2^TH (CLAMP/REPEAT wrap there);
 *            [2] the frame-time factor of a feedback FIX (1 in Original,
 *            the interpolation hook: rd__BlurFeedbackFix)
 *   lines    TEX0.TFX: 0 MODULATE, 1 DECAL, 2 HIGHLIGHT, 3 HIGHLIGHT2
 *   exactInt 1 */

/* Wave 5 (R5a): the resolution scale of the work buffers (WORK0..3,
 * AURA_*, AA0/1, FEED128, SHADOW0..2): 1 in the Original preset (the
 * literal PS2 sizes); in Enhanced outputHeight / 448, at least 1 and at
 * most 2, so blur radii stay a constant fraction of the screen.  Target
 * allocation reads it; no setting selects Enhanced yet, and the replay of
 * scaled work buffers is not implemented (RENDER_API.md section 17). */
float rd_WorkTargetScale(RdPreset preset, uint32_t outputHeight);

/* ------------------------------------- frame lifecycle and camera (R2c) */

/* Added in wave 2 (R2c).  The flip (gsb_UpdateGSSystem -> sceGsSwapDBuff)
 * sends the scene's draw environment and, with fbClear, its clear packet on
 * GIF path 3 right before dl_Swap kicks the frame's lists, so the PS2 drew
 * every frame (keep frames too) over:
 *   FRAME FBP 0x40, ZBUF 0xC0 PSMZ32 write on, XYOFFSET centred (+0.5 line
 *   when the draw environment carries sceGsSetHalfOffset's half offset),
 *   SCISSOR full, PRMODECONT 1, COLCLAMP 1, DTHE 0, TEST 0x50000; then
 *   TEST 0x30000, PRIM 6 (sprite, no ABE, no TME, flat), RGBAQ = the BG
 *   colour, the full-scene sprite at Z 0, TEST 0x50000.
 * rd_FrameHead records that, as rd state and an rd_ClearTarget, at the head
 * of list 0 and again at the head of list 11 of the open frame (call it right
 * after rd_BeginFrame).  rd_FrameFlip, called before the frame closes, sets
 * what the flip actually sent: the BG colour current at the flip (the PS2
 * consumes gsb_SetBGColor's packet then, one tick after the frame opened) and
 * the half offset of the draw environment that flip sends.  rd_EndFrame keeps
 * the copy in the first replayed list (11 for a keep frame, 0 otherwise) and
 * turns the other into no-ops, so a full frame's list 11 runs in whatever
 * state list 10 left, as on the GS. */
typedef struct RdFrameHead {
    uint8_t rgba[4];   /* RGBAQ of the clear sprite (gsb_SetBGColor, alpha 0x80) */
    uint32_t z;        /* the clear sprite's Z (sceGsSetDefClear: 0) */
    uint32_t gsW, gsH; /* the scene size (ScreenWidth, ScreenHeight) */
    uint8_t halfY;     /* XYOFFSET.y + 8 (sceGsSetHalfOffset with half != 0) */
    uint8_t clear;     /* fbClear: the clear packet is part of the flip */
    uint8_t _pad[2];
} RdFrameHead;

void rd_FrameHead(const RdFrameHead *head);
/* Rewrites the recorded head's clear colour and half offset; no-op without a
 * recorded head in the open frame. */
void rd_FrameFlip(const uint8_t rgba[4], int halfY);

/* rd_SetTarget's useOffset, bit 1 (R2c): XYOFFSET.y + 0.5 GS pixel, the
 * field half offset sceGsSetHalfOffset writes into the flip's draw
 * environment.  Bit 0 keeps its meaning. */
#define RD_TARGET_OFFSET 1
#define RD_TARGET_HALF_Y 2

/* The ZBUF.PSM of a target's depth buffer, which fixes how GS Z maps to
 * depth (FrameCB g_z.x, gs_z_to_depth): every ZBUF the game writes is PSMZ32
 * (ZBUF 0x...000C0 with PSM nibble 0), so that is the default for SCENE and
 * temporary targets; PSMZ24/16 exist for completeness. */
typedef enum RdZFormat { RD_ZFMT_32 = 0, RD_ZFMT_24 = 1, RD_ZFMT_16 = 2 } RdZFormat;

void rd_SetTargetZFormat(RdTarget t, RdZFormat fmt);
/* GS Z to depth for a target's Z format: the scale FrameCB g_z.x carries
 * (2^-32, 2^-24 or 2^-16). */
float rd_TargetZScale(RdTarget t);

/* gsb_MakeCommonMatrix's per-frame VU1 parameter block: the 16 quadwords
 * the packet unpacks (VIF UNPACK V4-32, 16 qw) to VU1 data memory 0..15,
 * referenced from the current position of every one of the 13 lists each
 * time it is built.  Matrices are column-major float[16] as the scratchpad
 * holds them (matrixptr offsets in parentheses).  Nothing consumes it before
 * wave 3; rd keeps the last one recorded in the open frame
 * (docs/port/RENDER_API.md "VU parameter block"). */
typedef struct RdVuCommon {
    float unitW[4];       /* qw 0: 0, 0, 0, 1 */
    float clip[4];        /* qw 1: 4095, 4095, 0, 16777215 */
    float zero[4];        /* qw 2: 0 */
    uint32_t giftag[4];   /* qw 3: 0x8000, 0x302EC000, 0x512, 0 (EOP, PRE PRIM 0x5D, PACKED
                             ST RGBAQ XYZ2) */
    float screenView[16]; /* qw 4..7: world to GS screen, screen (+0xC0) x view (+0x80) = +0x100 */
    float viewport[16];   /* qw 8..11: +0x340 */
    float invView[16];    /* qw 12..15: inverse of the view, +0x380 */
} RdVuCommon;

void rd_SetVuCommon(const RdVuCommon *block);
/* The block of the open frame, else of the last closed frame; NULL before
 * the first one. */
const RdVuCommon *rd_GetVuCommon(void);

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
    uint32_t tempReused; /* package P1: temporary targets that took a pooled texture */
} RdStats;

const RdStats *rd_GetStats(void);

/* Package P1: one record per replay (rd_EndFrame's, rd_Present's, the
 * replay tool's), docs/port/RENDER_API.md section 22.  CPU phases in ms:
 * interp (rd__InterpFrame building the blended copy), wait (rhi_WaitFrame:
 * the GPU finishing the frame RHI_FRAMES_IN_FLIGHT replays ago), acquire
 * (the swapchain image), upload (textures, meshes, temporary target clears
 * into the ring and their copies), walk (the command lists: state, geometry
 * expansion, ring writes and command encoding), of which bind (bind group
 * creation), submit, present (the present call), readback (synchronous
 * readbacks: dumps and screenshots only).  The counts are RhiStats deltas
 * since the previous record, so they include what the recording of the
 * frame created (temporary targets, textures).  GPU times come from
 * timestamps when the backend has them (gpuValid), RHI_FRAMES_IN_FLIGHT
 * replays later; rd_PerfPop returns a record once they are in. */
typedef struct RdPerfRecord {
    uint32_t replay; /* 1, 2, ... */
    uint32_t frame;  /* the replayed frame's number */
    uint8_t interpolated, keep, presented, gpuValid;
    double totalMs, interpMs, waitMs, acquireMs, uploadMs, walkMs, bindMs, submitMs, presentMs,
        readbackMs;
    double fenceWaitMs; /* the backend's blocked time on GPU completion (inside wait, readback) */
    uint32_t buffersCreated, buffersDestroyed, texturesCreated, texturesDestroyed;
    uint32_t memoryAllocs, memoryFrees, bindGroups, pipelineBinds, bindGroupBinds;
    uint32_t draws, renderPasses, barriers, copies, fenceWaits, waitIdles, readbacks;
    uint32_t textureUploads, meshUploads, tempClears, dateSnapshots, exactBlends;
    uint32_t pipelineCreates; /* pipelines created (a key the start-up set missed) */
    uint64_t uploadBytes;     /* everything written into the upload ring */
    uint64_t meshUploadBytes; /* of which mesh streams and indices */
    double gpuMs;             /* first timestamp to last */
    double gpuUploadMs;       /* the upload copies at the head */
    double gpuListMs[13];     /* per command list (0 for a list not replayed) */
    double gpuPresentMs;      /* the present blits */
} RdPerfRecord;

/* The oldest finished record not yet popped (a queue of 64; the oldest are
 * dropped when nobody pops); false when there is none. */
bool rd_PerfPop(RdPerfRecord *out);

#ifdef __cplusplus
}

#endif
#endif /* PORT_RENDER_RD_H */
