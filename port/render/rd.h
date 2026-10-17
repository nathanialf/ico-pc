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
 * 32-bit value the call site computed, UVs in texels (12.4) or STQ.  rd_core
 * records them as they are.  The replay converts them (rd_replay.c convVtx and
 * expand, and the origin rd__frame_group_ex puts in FrameCB): it applies the GS
 * sampling rules (pixel centres at integer coordinates; the game's +8/-4
 * half-texel nudges stay in the data) and the resolution scale in force.
 *
 * Defaults and leakage
 * --------------------
 * gsb_SetGsDefault (GsBase.c) writes TEST/ZBUF/FBA/TEXA at the head of lists
 * 0, 1, 2, 4, 6, 7, 8, 9, 10, 11, 12 only.  Lists 3 and 5 inherit whatever
 * the previous list left.  rd_core reproduces this by applying the same
 * defaults at rd_begin_frame (through the normal rd_* state calls, recorded
 * into those lists) and nothing else.  The defaults, from GsBase.c's
 * gsb_setNormalReg, gsb_setSemitransReg, gsb_setSpecularReg and
 * gsb_setParticleReg:
 *   normal   (0, 7-12): TEST 0x50000  ZBUF write on  FBA 0 TEXA 80/80
 *   semitr   (1, 2):    TEST 0x5140D  ZBUF write on  FBA 0 TEXA 7F/81 AEM
 *   specular (4):       TEST 0x5C000  ZBUF write off FBA 0 TEXA 80/80
 *   particle (6):       TEST 0x50000  ZBUF write off FBA 0 TEXA 80/80
 *
 * Replay order and keep
 * ---------------------
 * rd_end_frame(0) replays lists 0..12.  rd_end_frame(1) ("fbKeep", pause and
 * memory card screens, DisplayList.c dl_Swap) replays lists 11 and 12 only, on
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
 * All rd_* calls are made from the game (simulation) fiber.  rd_end_frame
 * hands the finished RdFrame to the presenter, which renders it once, or
 * several times with interpolation on (rd_present).  The game's post passes
 * are appended from scheduler() on the PS2 (main.c, through
 * gsb_UpdateGSSystem); on the host
 * they are appended by the same code running inside ico_vsync before
 * rd_end_frame, so there is one well-defined hand-off point.
 *
 * Every function names the seki (or other) entry points that call it.
 */
#ifndef PORT_RENDER_RD_H
#define PORT_RENDER_RD_H

#include <stdbool.h>
#include <stddef.h>
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
    RD_PROG_GRID_LIT,      /* mesh: lit, with the specular variant */
    RD_PROG_PARTICLE,      /* particle */
    RD_PROG_SCREEN,        /* 2D sprites, lines, points in GS window space (gif_* helpers) */
    RD_PROG_WORLD_PRIM,    /* CPU-projected prims with a matrix (darkVolume, lightning, sun) */
    RD_PROG_SHADOW_VOLUME, /* stencil-only strips */
    RD_PROG_POST,          /* fullscreen passes (RdPostKind picks the shader variant) */
    RD_PROG_COUNT
} RdProg;

/* Mode 3 (code 18) is particle's BEGIN_PARTICLE, RD_PROG_PARTICLE. */

typedef enum RdPrim {
    RD_PRIM_POINTS = 0,     /* GS PRIM 0 */
    RD_PRIM_LINES,          /* GS PRIM 1 */
    RD_PRIM_LINE_STRIP,     /* GS PRIM 2 */
    RD_PRIM_TRIANGLES,      /* GS PRIM 3 */
    RD_PRIM_TRIANGLE_STRIP, /* GS PRIM 4 */
    RD_PRIM_TRIANGLE_FAN,   /* GS PRIM 5 */
    RD_PRIM_SPRITES         /* GS PRIM 6: axis-aligned rectangle per two vertices */
} RdPrim;

/* 2D space tags; none transforms a position (the native frame: a GS X is
 * drawn where the game put it, at every aspect).  UI-tagged draws are
 * pre-flipped in mirror mode so they read correctly after the final present
 * flip; the 2D layer's 640 grid lands on the frame's 4:3 columns (2048 -+
 * 256).  WORLD-tagged screen prims came from a CPU projection of world
 * positions.  FULLFRAME marks the game's full-frame items (an identity tag,
 * decided by the game on its layout data: GifPacket.h's gif_FullFrameBegin
 * scope draws them at the frame's width; the posts' rects): Full pixel
 * extends the ones that end a pixel short of an edge. */
typedef enum RdSpace { RD_SPACE_WORLD = 0, RD_SPACE_UI = 1, RD_SPACE_FULLFRAME = 2 } RdSpace;

typedef enum RdStencilMode {
    RD_STENCIL_OFF = 0,
    RD_STENCIL_INCR,         /* shadow strip, sign > 0 */
    RD_STENCIL_DECR,         /* shadow strip, sign < 0 */
    RD_STENCIL_TEST_NONZERO, /* shadow resolve */
    /* the resolve's bit passes, RD_STENCIL_RESOLVE_BIT0 + k for count
     * bit k (0..5): stencil EQUAL with read mask 1 << k */
    RD_STENCIL_RESOLVE_BIT0
} RdStencilMode;

typedef enum RdTargetFormat {
    RD_TFMT_RGBA8 = 0,
    RD_TFMT_RGBA8_INT,
    RD_TFMT_R8,
    RD_TFMT_COUNT
} RdTargetFormat;

/* Named targets that stand in for fixed GS VRAM regions.  Sizes are in GS
 * pixels; the preset scales them. */
typedef enum RdTargetId {
    RD_TARGET_SCENE = 0, /* FBP 0x40 / TBP 0x800: 512 x 512 (PAL) or 512 x 448 (NTSC), with
              depth+stencil */
    RD_TARGET_DISPLAY,   /* FBP 0: 512 x H/2 reduced frame; the only displayed buffer;
                          retained across frames */
    /* the three blur levels of shadow_Draw.  The count itself (FBP 0x142,
     * scene-sized) is a per-frame target, rd_shadow_count_target. */
    RD_TARGET_SHADOW0, /* FBP 0x1C2 / TBP 0x3840: blur level 1, 256 x 256 */
    RD_TARGET_SHADOW1, /* FBP 0x1E2 / TBP 0x3C40: blur level 2, 128 x 128 */
    RD_TARGET_SHADOW2, /* FBP 0x1EA / TBP 0x3D40: blur level 3, 64 x 64 */
    /* staticBlur.c's work buffers after FullScreenEffectBefore (workBase
     * 0x2800, 0x2A00, 0x2E00, 0x3000).  GifPacket.c's decoder also maps
     * FBP 0x160 / TBP 0x2C00 to WORK1 and FBP 0x180 / TBP 0x3000 to WORK2;
     * staticBlur.c does not go through the decoder. */
    RD_TARGET_WORK0,         /* TBP 0x2800 TBW 4: 256 x 128 */
    RD_TARGET_WORK1,         /* TBP 0x2A00 TBW 4: 256 x 256 (256 x 128 is its top half) */
    RD_TARGET_WORK2,         /* TBP 0x2E00 TBW 8: scene-sized flare mask, drawn with SCENE's Z */
    RD_TARGET_WORK3,         /* TBP 0x3000 TBW 4: 256 x 128 eye blur / flare result */
    RD_TARGET_AA0,           /* 256 x 256 anti-alias downsample */
    RD_TARGET_AA1,           /* 128 x 128 */
    RD_TARGET_FEED128,       /* TBP 0x3F00: 128 x 128 aura feedback, persistent across frames */
    RD_TARGET_DATE_SNAPSHOT, /* R8 copy of SCENE alpha MSB, taken when a DATE consumer begins */
    /* appended, so the ids above keep their values in dumps */
    RD_TARGET_AURA_WORK, /* TBP 0x2A00 TBW 8: scene-sized aura buffer (list 8), SCENE's Z */
    RD_TARGET_AURA_TAP,  /* TBP 0x2800 TBW 2: 128 x 128 aura tap buffer */
    RD_TARGET_WORK2_PAD, /* TBP 0x2E00 + W H / 64: fillWork2's 256 x 64 band (never read) */
    /* host only, no GS address: FEED128 as a tick's first present found it,
     * put back at the head of the tick's later presents (rd_interp.c
     * feedback) */
    RD_TARGET_FEED_HELD,
    /* host only: DISPLAY as a tick's first present found
     * it, the previous tick's picture, put back at the head of the tick's
     * later presents when the frame reads DISPLAY (the motion blur; rd_interp.c
     * feedback); DISPLAY's size and scale */
    RD_TARGET_DISPLAY_HELD,
    RD_TARGET_COUNT
} RdTargetId;

typedef enum RdTexView { RD_VIEW_RGBA = 0, RD_VIEW_RGB24_TA0 = 1, RD_VIEW_DEPTH = 2 } RdTexView;

/* Transform block a draw carries; copied into the frame, so callers may
 * reuse their storage.  Matrices are column-major float[4][4] exactly as the
 * game's VU0 code lays them out (sceVu0FMATRIX). */
typedef struct RdXform {
    float local[16];     /* model to world */
    float localView[16]; /* model to view, as reg_set*MatrixPacket built it; used verbatim */
    float localProj[16]; /* model to GS clip, 4:3; the renderer substitutes its own projection
                 in Enhanced */
} RdXform;

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

typedef enum RdPostKind {
    RD_POST_REDUCTION = 0, /* SCENE -> DISPLAY, bilinear, tint, border crop; gsb_Reduction */
    RD_POST_KEEP,          /* DISPLAY drawn back at colour 112; gsb_KeepFrameBuffer */
    RD_POST_FADE,          /* gsb_fade */
    RD_POST_LETTERBOX,     /* gsb_scissorOnDemo: 58-line bars */
    RD_POST_BRIGHTNESS,    /* gsb_controlBrightness */
    RD_POST_FILM_NOISE,    /* gsb_filmNoise */
    RD_POST_AA_DOWNSAMPLE, /* gsb_antiAlias chain step */
    RD_POST_AA_COMPOSITE,
    RD_POST_FOG,            /* fog_DrawFog: depth-indexed LUT (see rd_post) */
    RD_POST_SHADOW_RESOLVE, /* reserved (no caller; rd_post refuses it) */
    RD_POST_BLUR,           /* reserved (no caller; rd_post refuses it) */
    RD_POST_COMPOSITE_FIX,  /* textured quad with LERP_FIX blend (motion blur, DoF planes, flare) */
    RD_POST_COPY,           /* texture-to-texture copy standing in for gif_MoveImage / VRAM grabs */
    RD_POST_PRESENT_BLIT,   /* reserved (no caller; rd_post refuses it) */
    /* one GS sprite of staticBlur.c, drawn in the GS integer arithmetic
     * (see rd_post below); the kind names the effect it belongs to, all
     * six replay alike */
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
    float proj[16];     /* the game's screen matrix +0xC0 (gsb_SetVSMatrix): the native
                         * projection, the same at every aspect */
    float zoom, aspect; /* vsParam[0]; the display aspect the frame was built for (GsBase.c
                         * gsFrame.aspect; 4/3 in version 9 and older dumps) */
    float nearZ, farZ;
    uint8_t cut; /* 1 on a camera cut this tick; disables interpolation for the frame */
    /* 1: the port's own free camera drew this tick (photo mode, rd_free_camera):
     * the VU draws keep the triangles with a vertex behind the eye, GPU-clipped
     * (vu_common.hlsli VU_F_BEHIND_EYE); 0 in the game's cameras and in older
     * dumps (the byte was padding, always zero) */
    uint8_t freeCamera;
    /* The camera's picture, the part of its view the frame shows at 4:3,
     * as half-width and half-height in GS pixels about (2048, 2048), for a
     * camera that is not the full screen's: the reflection cameras
     * rd_push_camera records (puddle.c 116, pool.c 128).  0: the full
     * screen's picture, 256 by half the native height (the frame's camera,
     * and every dump from before them: the bytes were padding).  The VU
     * draws recorded under the camera carry it (rd_mesh.c pushVu, RdCmd
     * b[3] and b[6]) into FrameCB (vu_common.hlsli vu_picture_half). */
    uint8_t pictureW, pictureH;
} RdCamera;

typedef enum RdPreset { RD_PRESET_ORIGINAL = 0, RD_PRESET_ENHANCED = 1 } RdPreset;

/* Every display field below applies whatever the preset.  preset is a flag
 * the host derives (RD_PRESET_ORIGINAL when the options are the PS2
 * picture); it decides only the deferred text (rd_present.c), the UI scale
 * (ui_scale_for) and the scene size for sceneScale 0 without a size (the
 * output's box with the Enhanced flag, else the GS size, so a zeroed
 * RdSettings is the PS2 picture). */
typedef struct RdSettings {
    RdPreset preset;
    uint32_t outputWidth, outputHeight; /* window/backbuffer */
    float aspect;                       /* 4/3 .. 20/3 (0, a zeroed RdSettings: 4/3) */
    uint8_t interpolate;                /* uncapped presentation (rd_present), any preset */
    uint8_t mirror;                     /* mirror mode: final blit flips x, UI pre-flipped */
    uint8_t filterUpgrade;   /* RdFilterUpgrade: trilinear/anisotropic with generated mips */
    uint8_t fullHeightScene; /* skip the vertical halving of the reduction pass */
    /* the reduction pass draws the whole frame, with no black border and no
     * wrap at the edge (rd_replay.c doBlurSprite); the picture's box, scale
     * and crop are the same with it off (rd_present.c outputBox,
     * rd__apply_display), which borderMask below masks */
    uint8_t fullPixel;
    uint8_t vsync;
    /* Texture packs ([video] texture_pack, dump_textures): replacements
     * from the pack drawn in place of the game's textures (a true -> false
     * edge set by rd_set_settings puts the originals back when the next
     * frame opens, rdtex_revert_replacements), and each texture the game
     * binds written as a PNG under its PCSX2 name.  Both 0 in a zeroed
     * RdSettings. */
    uint8_t texturePack;
    uint8_t dumpTextures;
    /* [video] effects_depth: the box blit also writes
     * an output-size D32F depth buffer (rd_present.c), the scene's depth at
     * the picture's pixels and 0.0 (far) in the bars, so an effects program
     * hooked into the API (ReShade) finds a depth buffer of the
     * backbuffer's size.  Near 1, far 0 (gs_z_to_depth: the depth grows
     * with GS Z): ReShade's RESHADE_DEPTH_INPUT_IS_REVERSED = 1.  Only
     * with an effects program loaded (rhi_injector_name), never on Android,
     * not under the CRT filter.  0 in a zeroed RdSettings: no depth
     * buffer is written. */
    uint8_t effectsDepth;
    /* Model packs ([video] model_pack, dump_models): replacement models
     * drawn in place of the game's, and each model part saved as glTF.
     * Both 0 in a zeroed RdSettings. */
    uint8_t modelPack;
    uint8_t dumpModels;
    /* The anti-aliasing (RdAntiAlias, [video] anti_aliasing): supersampling
     * multiplies the scene scale and the presenter box-reduces DISPLAY;
     * MSAA draws SCENE's first part of each frame multisampled and resolves
     * it (rd_replay.c, the MSAA epoch).  RD_AA_OFF (0, a zeroed
     * RdSettings) changes nothing anywhere; the host passes OFF under the
     * Original preset, and an MSAA value outside rd_aa_supported_mask()
     * draws as OFF. */
    uint8_t antiAlias;
    /* Full pixel off in the game (the host sets it with fullPixel 0): the
     * picture is Full pixel on's, the reduction drawing the whole frame as
     * above (rd_replay.c), and the presenter draws the game's border over
     * it as a black mask at the picture's own scale (rd_present.c
     * rd__border_mask: 2 GS columns at the sides, the reduction's rows at
     * the top and bottom), as the PS2's reduction scissor hid the picture's
     * edge.  Only on a cropped frame (rd__frame_cropped: 16:9 and the
     * other aspects whose exact width is under gsW, where the crop cuts the
     * game's own border away); at 4:3 and wherever the frame is its exact
     * width (48:9) it changes nothing, the GS's own border shown as on the
     * PS2.  0 in a zeroed RdSettings: the reduction keeps its recorded
     * scissor, DISPLAY's border is the GS's own (the tests' PS2 models,
     * the replay tool without --border-mask); ignored with fullPixel. */
    uint8_t borderMask;
    /* Water reflections: Sharp ([video] effect_water_sharp).  The blocks
     * rd_block_target makes that are not scene-sized (the reflections of
     * puddle.c and pool.c, the queen barrier's copy of the frame) take the
     * scene's scale, so their pictures have the scene's texels per GS pixel
     * (rd_core.c rd__target_scale_of).  0 in a zeroed RdSettings: they are
     * drawn at their own GS size, as on the PS2 (and as before this
     * option; the replay tool's --water-sharp sets it) */
    uint8_t waterSharp;
    /* The internal scene resolution, in texels: the scene's
     * texture is sceneWidth x sceneHeight (GS coordinates unchanged); 0 x 0
     * with sceneScale 0 = the presentation box in the window under the
     * Enhanced flag, else the GS size. */
    uint32_t sceneWidth, sceneHeight;
    /* > 0: the scene's texture is this factor of the GS size instead, both
     * ways (the GS size is the native frame's, as wide as the aspect asks),
     * e.g. 2 */
    float sceneScale;
    /* The CRT filter (rd_crt.c): a present-time pass in either preset that
     * replaces the box blit and never touches SCENE or DISPLAY.  crtMode
     * RD_CRT_OFF (0, a zeroed RdSettings) presents with the plain box blit;
     * crtStrength 0..1 lerps the filtered picture against the plain one (0
     * presents as off).  The five overrides are the config-only crt_* keys,
     * each < 0 for the mode's own value (rd_crt_settings sets them so). */
    uint8_t crtMode;
    uint8_t _crtPad[3];
    float crtStrength;
    float crtScanlines, crtMask, crtHalation, crtBloom, crtCurvature;
} RdSettings;

/* RdSettings.crtMode: what each imitates */
typedef enum RdCrtMode {
    RD_CRT_OFF = 0,
    RD_CRT_SCANLINES = 1, /* scanlines alone: no mask, no glow, flat */
    RD_CRT_CONSUMER = 2,  /* a consumer television: slot mask, glow, curved */
    RD_CRT_TRINITRON = 3, /* an aperture grille set: stripes, cylindrical */
    RD_CRT_PVM = 4,       /* a studio monitor: grille, dark gaps, sharp, flat */
    RD_CRT_SHADOW = 5,    /* a shadow-mask set: dot triads in a delta */
    RD_CRT_MODE_COUNT
} RdCrtMode;

/* Sets s's CRT fields: the mode, the strength (clamped to 0..1) and every
 * override to "the mode's own" (-1). */
void rd_crt_settings(RdSettings *s, RdCrtMode mode, float strength);

/* RdSettings.antiAlias */
typedef enum RdAntiAlias {
    RD_AA_OFF = 0,
    RD_AA_SSAA2 = 1, /* the scene at 2x the Resolution row's scale, box-reduced */
    RD_AA_SSAA4 = 2, /* 4x */
    RD_AA_MSAA2 = 3, /* SCENE multisampled, 2 samples */
    RD_AA_MSAA4 = 4,
    RD_AA_MSAA8 = 5,
    RD_AA_COUNT
} RdAntiAlias;

/* The RdAntiAlias values this device can draw, a bit (1u << value) each:
 * RD_AA_OFF and the supersampling values always; RD_AA_MSAAn when the RHI
 * reports n samples for every SCENE format (RhiLimits.msaaCounts) and can
 * sample depth (RhiLimits.depthSampled); MSAA8 never on a tile-based GPU.
 * The device's capability only: the fog's path never changes it (see
 * rd_aa_msaa_blocked).  Before rd_init (no device): OFF and the
 * supersampling values only. */
uint32_t rd_aa_supported_mask(void);
/* True when an MSAA value is asked for and can be drawn by the device, but
 * no way of reading the multisampled depth passed the fog's self-test on
 * the MSAA scene: the renderer draws without MSAA (rd__aa_mode). */
bool rd_aa_msaa_blocked(void);
/* The sample count of an RdAntiAlias value: 2, 4 or 8 for MSAA, else 1 */
uint32_t rd_aa_samples(int antiAlias);

/* RdSettings.filterUpgrade */
typedef enum RdFilterUpgrade {
    RD_FILTER_UPGRADE_OFF = 0, /* per texture as authored (Original) */
    RD_FILTER_UPGRADE_TRILINEAR = 1,
    RD_FILTER_UPGRADE_ANISOTROPIC = 2
} RdFilterUpgrade;

/* ------------------------------------------------------------ lifecycle */

/* gsb_InitGSSystem / gsb_Init: creates the named targets for the given GS
 * scene size (512x512 PAL, 512x448 NTSC) and loads the shaders.  Returns
 * false if the RHI lacks stencil wrap (a device without dual-source
 * blending is not refused: rd blends in two passes, rd_set_no_dual). */
bool rd_init(uint32_t gsWidth, uint32_t gsHeight, const RdSettings *settings, void *sdlWindow);
void rd_shutdown(void);
/* gsb_Init on a 50/60 Hz switch (kanbanBoot's gsResetFunc) recreates the
 * scene-sized targets. */
void rd_reset_scene(uint32_t gsWidth, uint32_t gsHeight);
/* Settings menu: applies at the next rd_begin_frame. */
void rd_set_settings(const RdSettings *settings);
/* Before rd_init: every VU draw of the run takes the VU vertex shaders'
 * plain variants (issue 107; docs/port/RENDER.md, "The VU shader
 * variants"), reason saying why for the log ("[dev] vu_plain_only: the
 * last start ended while the driver compiled them"); NULL clears it.  The
 * host calls it after a start that ended inside the extended variants'
 * compile (window_host.c); ICO_RD_VU_EXTENDED=1 overrides it. */
void rd_set_vu_plain_only(const char *reason);
/* Whether the run draws every VU draw with the plain variants (after
 * rd_init), and why (NULL when it does not) */
const char *rd_vu_plain_only(void);
/* Before rd_init: the run draws without the VU vertex pre-pass
 * (docs/port/RENDER.md, "The vertex pre-pass"), reason saying why for the
 * log ("for this start: the last start ended while the graphics driver
 * compiled its pipelines"); NULL clears it.  The host calls it after a
 * start that ended inside the pre-pass's compile and in a restart whose
 * step draws without it (window_host.c, startup_retry.h). */
void rd_set_vu_prepass_off(const char *reason);

/* v0.7.10: how the start-up pipeline set is made (rd_precreate_pipelines;
 * docs/port/RENDER.md "The start-up set"): the batch size, the worker
 * threads (0: the backend's choice), the process-memory growth in MB past
 * which the set stops and the rest is made on first use (0: no bound), and
 * whether the pipeline cache is written after every batch.  Zero fields
 * are the defaults; the window sets it from the device profile before
 * rd_precreate_pipelines. */
typedef struct RdPipelineSetPolicy {
    uint32_t batch;
    uint32_t maxThreads;
    uint32_t memoryBudgetMB;
    bool cacheEachBatch;
} RdPipelineSetPolicy;

void rd_set_pipeline_policy(const RdPipelineSetPolicy *p);
const RdPipelineSetPolicy *rd_pipeline_policy(void);
/* Whether the run draws with the VU vertex pre-pass now (after rd_init and
 * rd_vuprepass_selftest) */
bool rd_vu_prepass_on(void);
const RdSettings *rd_get_settings(void);
/* The scene's scale in force (rd_present.c
 * rd__apply_display, from the settings applied at the last rd_begin_frame):
 * the scene-class targets' texels per GS pixel across and down, the same
 * at every aspect (the GS frame is as wide as the aspect asks) and both
 * ways but under the Window resolution; 1 x 1 before rd_init, at 1x and
 * under the CRT filter.  The menus' text strips are rasterised to it
 * (port/ui/menu_font.c). */
void rd_get_scene_scale(float *sx, float *sy);

/* The scene's vertical scale (a whole number, rounded down) when it is below
 * the scale the settings asked for, because the GPU's size limit held it
 * (rd_present.c rd__apply_display) or its memory could not hold the targets
 * and they were made smaller (rd_core.c createNamedTargets, and a later
 * allocation that failed); 0 when they are as asked.  The
 * Resolution row shows it after the asked scale, "16x (8x)". */
int rd_scene_scale_lowered(void);

/* gsb_SetGsDefault / dl_Swap at the start of a tick: clears the 13 lists and
 * records the per-list defaults listed above. */
void rd_begin_frame(void);
/* dl_Swap: closes the frame.  keep != 0 replays lists 11..12 only (fbKeep).
 * Hands the RdFrame to the presenter; returns immediately. */
void rd_end_frame(int keep);
/* Where the GPU work the game's calls cause runs (rd_end_frame's replay and
 * present with interpolation off, rd_begin_frame's target re-creation, the
 * FMV picture's present).  The game calls rd from a fiber with a 256 KB
 * stack; the driver (pipeline creation, present) may need far more.  The
 * window build sets
 * call to ico_sched_call_on_host, which runs fn(arg) on the host stack in
 * the host FP mode and returns; NULL (the default, tests and tools) calls
 * fn directly. */
typedef void (*RdHostCall)(void (*fn)(void *arg), void *arg);
/* Creates every pipeline of the reachable set (rd__enumerate_reachable) now,
 * so the first frames that use them do not stall on the driver's pipeline
 * compile; logs the count, the time, the compiles and the threads.  The keys
 * go to rhi_create_pipelines in batches of up to 32 (the driver compiles a
 * batch on worker threads; a batch holds keys with a destination read or
 * keys without, never both).  The window calls it after rd_init; the tests
 * do not.  Returns the number created. */
uint32_t rd_precreate_pipelines(void);
/* Progress of rd_precreate_pipelines: fn(ctx, done, total) is called with
 * (0, total) before the first pipeline, while a batch compiles (about every
 * 50 ms, with the pipelines finished so far) and between two batches, with
 * done never going down and below total, and (total, total) once at the
 * end; on the calling thread.  fn may present a frame (the window draws its
 * start-up screen from it); that present asks the pipeline cache for its
 * own few pipelines, which are created at once and found again afterwards,
 * so the call is safe from inside the loop.  A nonzero return stops the set
 * after the batch in flight (the game is closing): the rest is created as
 * the frames need it, and the last call is not made.  NULL clears it.
 * Kept across calls until cleared. */
typedef int (*RdPipelineProgressFn)(void *ctx, uint32_t done, uint32_t total);
void rd_set_pipeline_progress(RdPipelineProgressFn fn, void *ctx);
/* The longest single compile of the start-up set so far, in ms, as the
 * worker measured it (updated after each batch; 0 before the first). */
float rd_precreate_longest_ms(void);
/* What the renderer is asking the graphics driver for, as one line, for
 * a crash report (the window passes diag_host.h's activity note): fn(note)
 * before each pipeline creation ("creating pipeline program 3 blend 0 vs 4
 * fs 5 fmt 1/8 msaa 0 dst 0"), for each batch of rd_precreate_pipelines
 * while it compiles ("creating pipelines 321-352 of 702: <first key> to
 * <last key>", the range counting the set's keys the batch went over),
 * between two batches ("creating the start-up pipelines (353 of 702)"),
 * fn(NULL) when the step is over.  NULL (the default): nothing. */
typedef void (*RdActivityFn)(const char *note);
void rd_set_activity_fn(RdActivityFn fn);
/* A start-up batch done (issue 107 for the VU batches, issues 93 and 107
 * for the 2D ones): fn(line) with its compile times, the process's memory
 * and the pipeline cache's size after it ("VU batch 329-360 of 702
 * (extended, compiler E031.41.03.47): 32 pipelines 0.41-2.10 s, mean 1.29
 * s, 12.3 s on 4 threads; memory 185 MB (most 194 MB), pipeline cache 3104
 * KB; vu_prelit_vs 50325 words 9801 instructions: 8 x 1.10-2.10 s mean
 * 1.62 s; ...", a 2D batch "2D batch 193-224 of 527 (compiler ...): ...;
 * slowest 0.21 s program ... (font_sheet_ps ...)"), and a lazy batch's
 * line the same way (rd_pipelines_lazy: "lazy batch 3, pipelines 41-57 of
 * this run ..."), the window passing it to the activity file (diag_host.h
 * ico_diag_startup_activity_batch); the log also gets the line, a set's
 * with each pipeline's time ("rd: pipelines: compiled ...; each (s): 1.10
 * 1.22 ...").  NULL (the default): the log line only. */
void rd_set_batch_fn(RdActivityFn fn);
/* The process's resident memory and its peak in KB for the batches' lines
 * (diag_host.h ico_diag_process_memory: 0 when known).  NULL (the
 * default): "memory unknown". */
typedef int (*RdMemoryFn)(long *rssKb, long *peakKb);
void rd_set_memory_fn(RdMemoryFn fn);
/* No start-up set (issues 93 and 107; docs/port/RENDER.md, "The start-up
 * set"): the window calls it instead of rd_precreate_pipelines, why saying
 * why ("on this device (Adreno 740, issues 93 and 107)", "by the settings
 * ([dev] pipeline_set = \"lazy\")", ...), logged as "rd: pipelines: no
 * start-up set <why>: pipelines are made as the game needs them" with the
 * pipeline cache's size.  Each pipeline is then made when a frame first
 * needs it, and those creations are counted into batches that
 * rd_pipelines_lazy_tick closes: the pipeline cache written to its file
 * (rhi_pipeline_cache_save, its size logged), the batch's line to the log
 * and to rd_set_batch_fn. */
void rd_pipelines_lazy(const char *why);
/* Once a frame (the window's pump), on the thread that draws: closes the
 * open lazy batch once no pipeline was made for 0.5 s or 2 s after its
 * first.  Nothing outside rd_pipelines_lazy's runs. */
void rd_pipelines_lazy_tick(void);
/* The pipelines made so far this run (the start-up set's and the frames'),
 * for the window's low-memory line. */
uint32_t rd_pipeline_count(void);
/* The VU draws' index spans (issue 95; rd_replay.c measures one replay in
 * 64): "VU index span: the widest 14314 (frame 812) over 40 measured
 * frames (one replay in 64), 0 indexed VU draws past 2^24 - 1 of 52000 in
 * 0 frames; the device takes values up to 16777215", for the line the
 * window writes at a device loss. */
void rd_index_span_text(char *out, size_t size);
/* The depth fog's self-test (rd_fog_path.c): a frame of its own (never
 * recorded, never presented, no frame number) draws a grid of cells at
 * known GS Z on SCENE at its real size and scale and fogs it with a known
 * LUT, through one way of reading the depth (in place, a copy, a buffer)
 * after another in the platform's order, stopping at the first that
 * passes; the fogged pixels are read back and compared with the GS
 * arithmetic.  That first way is the one the fog takes from then on (an
 * ICO_RD_FOG_PATH or ICO_RD_DEPTH_COPY override is tried first and
 * stays); the ways after it are not tried, unless ICO_RD_FOG_PATH=test (a
 * developer switch) asks for every one.  One log line, "fog: depth path",
 * names it with every result ("not tried" for the rest).
 * The window calls it after rd_precreate_pipelines; after that it runs
 * again whenever SCENE is made at a new size.  Waits for the GPU.  Returns
 * whether the path in force passed. */
bool rd_fog_selftest(void);
/* What the renderer decided at its start, for the window's summary line
 * (diag_host.h ico_diag_set_summary): "blend <mode> (self-test: ...);
 * index values dense (marks in the stream); fog <path> (self-test: ...);
 * VU shaders plain only (issue 107)" or "VU shaders plain and extended" (the
 * policy rd_init chose, rd__vu_policy_init), and ", pass split every N" when
 * on, then "; VU pre-pass on (self-test: ok)" or "off (self-test: <reason>)"
 * or "off <why> (self-test not run)" (rd_vuprepass_selftest), then ";
 * pipelines the start-up set" or "; pipelines on first use (no start-up
 * set)" (rd_pipelines_lazy). */
void rd_summary_text(char *out, size_t size);
/* The exact blend's self-test (rd_blend_selftest.c): a frame of its own
 * (never recorded, never presented) draws on SCENE at its real size and
 * scale, without MSAA, what the destination read must get right: the
 * forest's tint sprite over eight cells of known colours (PCSX2's
 * pixels), a textured LERP quad whose clear texels must leave the colour
 * behind them, two overlapping LERP draws (the second must read the
 * first's result) and a glyph through the text sheet's shader.  It is
 * drawn with the fixed-function blend first (the reference; within 2
 * levels of the GS arithmetic, the two-pass fallback tried when the
 * dual-source blend is not), then with each exact mode the device has in
 * rd_init's order (attachment read, then interlock or ROV), stopping at
 * the first whose pixels are within 1 level of the GS arithmetic and of
 * the reference's glyph; that mode is the one in force from then on, none
 * passing leaves the fixed-function blend.  An ICO_RD_BLEND attachment or
 * storage is kept whatever its result (logged); ICO_RD_BLEND=fixed and a
 * device without an exact mode run nothing.  One log line, "blend: <mode>
 * (self-test: ...)", names the choice with every result, and a failing
 * mode's worst sample gets a line of its own.  ICO_RD_BLEND_SABOTAGE=
 * attachment,storage,fixed (tests) makes those modes' read-back white.
 * The window calls it before rd_precreate_pipelines (the set made is the
 * chosen mode's); after that it runs again whenever SCENE is made at a new
 * size.  Waits for the GPU.  Returns whether the mode in force passed. */
bool rd_blend_selftest(void);
/* The VU vertex pre-pass's self-test (rd_vuprepass_selftest.c): a frame of
 * its own (never recorded, never presented) draws three meshes made for it
 * (a prelit, a lit and a skinned strip) on SCENE at its real size and
 * scale, without MSAA, once through the programs' own vertex shaders and
 * once through the pre-pass, in the VU shader variant the settings draw
 * with, and compares the two pictures byte for byte.  A difference, a
 * pipeline the pre-pass could not make (its commands drawn the old way) or
 * a device error turns the pre-pass off for the run; one log line, "VU
 * pre-pass: on (self-test: ...)" or "VU pre-pass: off (self-test:
 * <reason>)", says which, and the summary line carries it.  Nothing runs
 * when the pre-pass is already off (the settings, the device, the host).
 * ICO_RD_VU_PREPASS_SABOTAGE=1 (tests) draws the pre-pass's first command's
 * records one texel off.  The window calls it after rd_blend_selftest and
 * before rd_precreate_pipelines (the set made is the pre-pass's or the
 * programs').  Waits for the GPU.  Returns false when it turned the
 * pre-pass off. */
bool rd_vuprepass_selftest(void);
/* The longest frame replay (CPU time of recording, pipeline creation,
 * submit and present) in ms since the last reset, and *count the replays
 * in that time; reset != 0 starts a new period.  For the window's 10 s
 * statistics line. */
double rd_replay_time_max(int reset, uint32_t *count);
void rd_set_host_call(RdHostCall call);
/* dl_Clear without dl_Swap (gsb_UpdateGSSystem(1)
 * on the movie path, gsb_UpdateGSSystem before gsSystemReady): the lists
 * recorded since rd_begin_frame are dropped unreplayed, as the PS2 drops a
 * display list it never kicks.  No-op when no frame is open. */
void rd_discard_frame(void);
/* True between rd_begin_frame and rd_end_frame / rd_discard_frame. */
bool rd_frame_open(void);
/* The window's drawable size changed (SDL pixel-size event): resizes the
 * swapchain and the presenter's output box at once.  Host loop only, never
 * from inside a frame's replay. */
void rd_resize_output(uint32_t width, uint32_t height);
/* The backend rebuilt the swapchain at a size other than the
 * output's (out of date, a lost or recreated surface, an Android rotation
 * or unfold the window's events had not reported yet); the renderer's
 * output followed it at an acquire (as rd_resize_output, without a second
 * rebuild).  True once per such change, with the new size in *w, *h (either
 * may be NULL): the host loop then gives the size to the options that
 * follow the window (aspect auto, resolution window). */
bool rd_output_followed(uint32_t *w, uint32_t *h);
/* gsb_SetVSMatrix: the camera for this frame (used by Enhanced projection,
 * interpolation and the WORLD-space 2D conversion).
 * Called from gsb_MakeCommonMatrix, the point where the view
 * (matrixptr+0x80) and the screen matrix (+0xC0) are both final; view is
 * +0x80, proj the GS screen matrix +0xC0 (view space to GS window
 * coordinates and GS Z after the divide by w), zoom vsParam[0], aspect
 * the frame's display aspect, nearZ/farZ vsParam[7]/[8].  Replay puts them
 * in FrameCB: g_view, g_proj = proj, g_viewProj = proj x view, g_cameraPos = the eye from
 * the view's inverse (w = cut), g_clip = near, far, zoom, aspect. */
void rd_set_camera(const RdCamera *cam);

/* --------------------------------------------------------- interpolation
 * The simulation keeps its tick; with RdSettings.interpolate on (either
 * preset) rd_end_frame only closes the frame, and the host presents as often
 * as it likes with rd_present(alpha): the last closed frame replayed with
 * every keyed draw blended from the frame before it by alpha in [0, 1)
 * (0 = the previous frame's data, so the picture is one tick late).
 * Unkeyed draws, CLUT animation, film noise, dissolve and every other state
 * stay at the tick's values.
 *
 * rd_interpolation_active  RdSettings.interpolate, either preset (as
 *                         applied at the last rd_begin_frame)
 * rd_present              replays and presents; false (nothing done) when
 *                         interpolation is off, before the first frame, or
 *                         while an FMV picture is on the output.  Every call
 *                         of a tick draws its feedback passes (motion blur,
 *                         aura) from what the tick started from, so they
 *                         advance once a tick, as on the PS2
 * rd_frame_number          the last closed frame's RdFrame number, 0 before
 *                         the first: the host's tick clock
 * rd_camera_cut            the frame being recorded starts a new shot (a hard
 *                         camera cut or a stage change): it is not blended
 *                         from the one before (RdCamera.cut).  Outside an
 *                         open frame it applies to the next one.  Called by
 *                         the window from the game's cut signal
 *                         (port/game/video_options.h ico_video_camera_cut) */
bool rd_interpolation_active(void);
bool rd_present(float alpha);
uint32_t rd_frame_number(void);
/* The last closed frame's number, display aspect and native GS width (the
 * frame header, dump version 10); false before the first.  The aspect sweep
 * (window_host.c) waits on it for a frame at the aspect it asked for. */
bool rd_last_frame_native(uint32_t *number, float *aspect, uint32_t *gsW);
void rd_camera_cut(void);
/* rd_free_camera: the frame being recorded is drawn from the port's own free
 * camera (photo mode, port/game/photo_view.c), not a camera the game's
 * artists placed: RdCamera.freeCamera.  Outside an open frame it applies to
 * the next one. */
void rd_free_camera(void);
/* PC port: the game's box cull dropped a model's whole
 * object (rule 2, RegistPacket.c's object box) or one of its parts (rule 3,
 * the part's box) in the frame being recorded; views is how many cameras
 * the cull tested (gsb_HostCullViewsUsed: 1 the tick's alone, 2 or 3 the
 * blend span's, item 5) and, with one, why (oneWhy: GsBase.h
 * GSB_CULL_ONE_*, 1 the Original framerate, 2 a stage's first tick, 3 a
 * cut, 4 the camera standing still, 5 a step past the presenter's limits,
 * 6 a part locked to the camera, 7 no inverse; 0 more than one camera).
 * The frame keeps the first RD_CULL_DROPS_MAX with the count of all; F12
 * (rd_dump_on_demand) writes them to the log as one line, so a report's
 * log names the rule that dropped each part and why one camera did.
 * Nothing outside an open frame. */
#define RD_CULL_DROPS_MAX 48
void rd_note_cull_drop(const char *model, int part, int rule, int views, int oneWhy);

/* What the last present showed (rd_present's, or with interpolation off
 * rd_end_frame's one present of the frame), for the window's start-up log: the
 * frame's number, its keep flag, the strongest fade it recorded (1 + the
 * fade's alpha, GS 0x80 = 1.0; 0 for none), the frame-level snap against
 * the frame before (rd_internal.h RD_SNAP_*: 0 blended) and whether it was
 * the tick's first present.  False before the first present. */
typedef struct RdPresentInfo {
    uint32_t frame, keep, fade, snap, firstOfTick;
} RdPresentInfo;

bool rd_last_present_info(RdPresentInfo *out);

/* DISPLAY's colour (RGBA, as stored) at RD_DISPLAY_PROBE_POINTS points: the
 * centre, then the centres of the top-left, top-right, bottom-left and
 * bottom-right quarters.  Five single-texel copies and a readback that
 * waits for the GPU: for tests (rd_boot_present_test), never per present
 * in a window.  False without a device or DISPLAY. */
#define RD_DISPLAY_PROBE_POINTS 5
bool rd_display_probe(uint8_t rgba[RD_DISPLAY_PROBE_POINTS][4]);

/* The present clock the host derives alpha from.  A present's
 * measured time carries the jitter of the simulation step and the sleeps
 * before it (a few ms: up to a fifth of a tick), which an alpha taken from it
 * directly passes on to every blended draw.  The clock advances by the
 * running mean of the intervals between presents (stepMs) and moves its
 * phase towards the measured time by an eighth of the error per present; an
 * error over half a tick, a pause of more than four steps or time going
 * backwards resets it to the measured time.  alpha = (clock - tickAtMs) /
 * tickMs in [0, 0.999], tickAtMs the simulated time the last frame closed
 * (never a measured close time).  nominalMs: the intended interval (the
 * frame rate cap, half a refresh in mailbox), 0 when unknown. */
typedef struct RdPresentClock {
    double clockMs, stepMs, lastMs;
    uint32_t presents; /* since the last reset; 0 = reset on the next call */
    uint32_t resets;
} RdPresentClock;

float rd_present_clock_alpha(RdPresentClock *c, double nowMs, double tickAtMs, double tickMs,
                             double nominalMs);

/* ----------------------------------------------------------- mirror mode
 * The game's mirror mode (chosen at New Game, port/game/options.h
 * ico_opt_mirror) flips the presented picture
 * horizontally: the presenter's step 2 samples DISPLAY right to left, so
 * every present (Original, Enhanced, interpolated) is mirrored; RD_SPACE_UI
 * screen prims drawn into SCENE or DISPLAY are flipped about the target's
 * centre at replay, so they read normally after the present flip.  Nothing
 * else changes (winding, culling, shadows, DATE, feedback).  rd_set_mirror is
 * the run's value, independent of RdSettings (the window rebuilds those from
 * the display options); the mirror is on when either RdSettings.mirror
 * (tests, the replay tool) or rd_set_mirror's flag is set.  It applies from
 * the next replay or present. */
void rd_set_mirror(int on);
bool rd_mirror_active(void);

/* The two-pass blend fallback for a device without
 * dual-source blending (rd_pipeline.c rd__expand_no_dual).  rd_init turns it
 * on when the device lacks dualSrcBlend or ICO_RD_NO_DUAL=1 is set (and logs
 * "blend: two-pass fallback (no dualSrcBlend)").  rd_set_no_dual switches it
 * for the next draws (tests: the same frame replayed both ways); turning it
 * off on a device without the feature is refused (false). */
bool rd_set_no_dual(bool on);
bool rd_no_dual(void);

/* The exact GS blend (rd_dst_read.c): the blends the GPU's blender rounds
 * (the LERPs, Cd*FIX + Cs, the Ad forms, Cd*As + Cd, PABE on the
 * premultiplied forms) computed in the fragment shader from the
 * destination, as the GS computes them, through the device's destination
 * read.  rd_init picks the mode from the device (RhiLimits) and logs it:
 * "blend: exact (attachment read)", "exact (interlock)", "exact (rov)" or
 * "fixed-function (rounds up)"; ICO_RD_BLEND=fixed|storage|attachment
 * overrides the choice (one the device lacks is refused and logged).
 * rd_blend_selftest then checks the choice on the device and may fall back
 * to the next mode.
 * rd_set_blend_exact switches it for the next draws (tests: the same frame
 * both ways) and returns false for a mode the device or its targets lack. */
typedef enum RdBlendExact {
    RD_BLEND_EXACT_OFF = 0,        /* the fixed-function blend (dual source) */
    RD_BLEND_EXACT_STORAGE = 1,    /* interlock (Vulkan) or ROV (D3D12) */
    RD_BLEND_EXACT_ATTACHMENT = 2, /* rasterization-order attachment read (Vulkan) */
    RD_BLEND_EXACT_COUNT
} RdBlendExact;

bool rd_set_blend_exact(RdBlendExact mode);
RdBlendExact rd_blend_exact(void);

/* -------------------------------------------------- presentation overlay
 * The port's own UI (port/ui's popups) drawn on the output itself, after the
 * presenter's box
 * blit (rd_present.c rd__present_record), at the output's resolution:
 * outside the game's frame, so it is never reduced, never in DISPLAY's
 * history (a keep frame cannot show it twice), never mirrored and never
 * interpolated.
 *
 * rd_set_present_overlay  registers fn (NULL: none; one at a time).  rd calls
 *                       fn(ctx, user) once per present that reaches an
 *                       output (rd_end_frame's with interpolation off, each
 *                       rd_present,
 *                       the replay tool's --present), before the frame's
 *                       replay, so the textures fn creates or updates
 *                       (rd_create_texture_r8, rd_update_texture_rect: the
 *                       font atlases) are uploaded with the frame.  Also at
 *                       the movie picture's presents (rd_video.c), laid out
 *                       in the picture's 4:3 box.  The registration
 *                       survives rd_shutdown / rd_init
 * rd_overlay_prims       valid only inside fn (ignored elsewhere): prims in
 *                       12.4 fixed-point OUTPUT pixels, origin at the
 *                       output's top-left corner, an integer coordinate on
 *                       a pixel's top-left edge (pixel (x, y) is the square
 *                       [x, x + 1) x [y, y + 1); not the GS convention, so a
 *                       texture drawn 1:1 is sampled at its texel centres).
 *                       Up to 4095 pixels each way.  z is ignored (no depth
 *                       test, no Z write), s, t are 12.4 texels of tex
 *                       (uvFixed 1), q unused, rgba a GS colour (0x80 =
 *                       1.0).  tex id 0 draws untextured; otherwise
 *                       MODULATE with TCC RGBA, the texture's alpha in GS
 *                       units (an R8 texture through font_ps, as
 *                       rd_create_texture_r8 says), bilinear, clamped.  blend:
 *                       the GS equation (RD_BLEND_LERP_AS,
 *                       RD_BLEND_CS_AS_ADD_CD; ABE on),
 *                       no alpha test, no DATE, COLCLAMP on.  Drawn in
 *                       call order after the box blit, never flipped (the
 *                       mirror mode flips only the box blit), with no
 *                       scissor but the output
 * rd_read_presented      the last presented output, tightly packed RGBA8
 *                       (outputWidth x outputHeight x 4 bytes, the overlay
 *                       included): the headless output (tests, the replay
 *                       tool).  The window build presents the swapchain
 *                       image and keeps no copy, so it returns false there
 *                       (a capture copy in rd__present_record is the place
 *                       to add one)
 * rd_get_present_overlay  the registered fn (NULL: none) and its user, so a
 *                       caller can put back what it replaced
 * rd_present_blank       a present with no scene: the output
 *                       (the swapchain's next image, or the headless
 *                       output) cleared to 0, the registered overlay drawn
 *                       on it (its ctx as for a frame's present, the box
 *                       the aspect's; never on the CRT filter's grid, no
 *                       deferred text), and presented.  The frame being
 *                       recorded, DISPLAY and the retained frames are not
 *                       touched.  For the host's own screens before the
 *                       game runs (the Android first start's progress,
 *                       window_host.c ico_window_progress).  false with no
 *                       device or when the replay could not run
 * rd_set_present_overlay_top  a second callback, the top layer
 *                       (the touch controls; NULL: none).  Called after
 *                       fn at the same presents; its ctx is always the
 *                       output (outW x outH the output, box the aspect's),
 *                       never the CRT filter's grid, and its prims are
 *                       drawn on the output at its resolution: under fn's
 *                       prims without the filter, over the filtered
 *                       picture with it (fn's prims are inside that
 *                       picture).  rd_overlay_prims as for fn.  The capture
 *                       (rd_capture_presented) leaves both layers out.
 *                       rd_get_present_overlay_top as rd_get_present_overlay */
typedef struct RdRect {
    int32_t x, y;
    uint32_t w, h;
} RdRect;

typedef struct RdOverlayCtx {
    uint32_t outW, outH; /* the output, pixels */
    RdRect box;          /* the presentation box DISPLAY was blitted into */
    float boxScale;      /* box.h / 448: output pixels per 448-line frame line */
    int mirror;          /* the box blit was flipped (the overlay is not) */
} RdOverlayCtx;

typedef void (*RdOverlayFn)(const RdOverlayCtx *ctx, void *user);

/* The 2D layer's box: the game's 640 x 448 grid is a centred 4:3
 * box inside the presentation box, in output pixels.  At 4:3 it is the
 * presentation box; on a wider picture it is the pillar of the box that
 * holds the frame's 4:3 columns (GS 2048 -+ 256); a box narrower than 4:3
 * is kept whole.  rd_ui_box_of gives it for any presentation box (an
 * overlay's ctx->box); rd_ui_box for the box the presenter uses for the
 * output in force (g_rd.settings).  The pointer's mapping into the grid
 * (port/ui/ui_host.c), the deferred text's box and scissor and the movie's
 * box (rd_video.c) all read it. */
RdRect rd_ui_box_of(const RdRect *present);
RdRect rd_ui_box(void);

void rd_set_present_overlay(RdOverlayFn fn, void *user);
RdOverlayFn rd_get_present_overlay(void **user);
bool rd_present_blank(void);
void rd_set_present_overlay_top(RdOverlayFn fn, void *user);
RdOverlayFn rd_get_present_overlay_top(void **user);
void rd_overlay_prims(RdPrim type, const RdScreenVtx *v, uint32_t n, RdTex tex, RdBlend blend);
bool rd_read_presented(void *dst, uint32_t *w, uint32_t *h);

/* -------------------------------------------------- photo mode's picture
 * The paused game draws every tick from the photo camera
 * (port/game/photo_view.c), so its frames are presented like any other; this
 * saves one of them.
 *
 * rd_capture_presented   the next present that reaches an output (either
 *                       build: the headless output or the swapchain image)
 *                       copies the output before the presentation overlay
 *                       (the CRT filter and the deferred text applied, the
 *                       port's popups and photo HUD not) and writes it to
 *                       png (RGB, outputWidth x outputHeight) after the
 *                       submit.  false: no device or no path
 * rd_capture_result      1 (written) or -1 (failed) once for the last
 *                       capture, with its path in path; 0 while it is
 *                       pending or when there is none */
bool rd_capture_presented(const char *png);
int rd_capture_result(char *path, uint32_t pathSize);

/* --------------------------------------------------------- deferred text
 * Text the game shows in lists 11 and 12 (the layout's menu rows, through
 * port/ui) recorded twice:
 * as an item that says what to write, and as the glyph quads that write it
 * into SCENE.  A present of the Enhanced preset with a renderer registered
 * draws the items on the output after the box blit, at the output's
 * resolution (glyphs rasterised at the shown size, a texel a pixel), and
 * skips the quads; every other replay (Original, no present, no renderer)
 * draws the quads and ignores the items, so its pixels are the quads'.
 *
 * rd_deferred_text        records an RDC_OVERLAY_TEXT item into the current
 *                        list, in place (the scissor and the post passes
 *                        that follow it in the lists apply to it at the
 *                        present).  key as for a draw (0: never blended)
 * rd_deferred_text_quads   on != 0: the RDC_SCREEN draws recorded until it is
 *                        called with 0 are the last item's quads, skipped
 *                        when the item is drawn deferred.  The state
 *                        commands between are kept either way
 * rd_set_deferred_text_fn   registers the renderer (NULL: none, the quads are
 *                        drawn).  Called once per deferred item and region
 *                        at a present, before the replay (as the overlay's
 *                        callback), with the item as the present sees it:
 *                        blended between ticks, the colour folded through
 *                        the fades, letterbox and brightness after it.
 *                        Inside it rd_overlay_prims adds prims to the
 *                        deferred layer, clipped to the region (the row's
 *                        scissor, the reduction's border crop, a letterbox
 *                        band), drawn before the overlay's.  The
 *                        registration survives rd_shutdown / rd_init
 * rd_deferred_text_active  whether the replay in progress draws items
 *                        deferred (inside the renderer: true) */
#define RD_TEXT_BYTES 256

typedef struct RdTextItem {
    char utf8[RD_TEXT_BYTES]; /* NUL-terminated */
    float x, y;               /* the anchor, layout grid (port/ui/font.h) */
    float size;               /* the em, grid y units */
    float xf[6];              /* originX, originY, scaleX, scaleY, offsetX, offsetY (UiXform) */
    uint32_t flags;           /* the font's flags (port/ui/font.h UI_*); opaque to rd */
    uint8_t rgba[4];          /* GS colour, 0x80 = 1.0, after the row's fade and dimming */
    uint8_t additive;         /* blend Cs * As + Cd (the glow pass), else the lerp */
    uint8_t hasXf;            /* xf applies */
    uint8_t pad[2];
} RdTextItem;

typedef void (*RdDeferredTextFn)(const RdOverlayCtx *ctx, const RdTextItem *item, void *user);

void rd_deferred_text(const RdTextItem *item, RdKey key);
void rd_deferred_text_quads(int on);
void rd_set_deferred_text_fn(RdDeferredTextFn fn, void *user);
bool rd_deferred_text_active(void);

/* ------------------------------------------------------- the draw filter
 * The model viewer (port/game/model_viewer.c) shows one object of a loaded
 * stage on its own:
 * every other world draw is left out of the frame where it is recorded.
 *
 * rd_set_draw_filter     on: from now on a world draw (rd_draw_vu_mesh,
 *                      rd_draw_vu_grid, rd_draw_vu_particles, rd_world_prims,
 *                      rd_shadow_strip, rd_shadow_tris, and rd_screen_prims in
 *                      RD_SPACE_WORLD after any space override) is recorded
 *                      only when its key's object (RD_KEY's objptr: the key
 *                      shifted right by 16) is one of the set's, which
 *                      starts as objs[0..n) (at most RD_DRAW_FILTER_MAX).
 *                      UI and full-screen screen prims, post passes, state
 *                      and targets are never filtered.  off (the default):
 *                      every draw is recorded; the set is emptied
 * rd_draw_filter_open    while open (and the filter on), every world draw is
 *                      recorded and a non-zero key's object joins the set,
 *                      so an object whose draws are keyed by several
 *                      display objects (the boy's head, body, cloth) is
 *                      learned from its own display list.  Closed by
 *                      rd_set_draw_filter
 * rd_draw_filter_keeps   whether a world draw with this key is recorded now
 *                      (no learning; tests) */
#define RD_DRAW_FILTER_MAX 64
void rd_set_draw_filter(bool on, const void *const *objs, uint32_t n);
void rd_draw_filter_open(bool open);
bool rd_draw_filter_keeps(RdKey key);

/* ------------------------------------------------------------- lists */

/* dl_SetDLPriority(pri): selects the list that subsequent calls record into. */
void rd_select_list(int list);
int rd_current_list(void);

/* --------------------------------------------------------- state deltas
 * Each records a state change into the current list.  Callers:
 *   rd_blend     gif_SetAlpha, material packets (Packet.c), raw ALPHA writes
 *   rd_test      gif_SetZTest, gsb_set*Reg, raw TEST writes (rd_test_from_gs)
 *   rd_z_write    gif_SetZWrite, raw ZBUF writes
 *   rd_fba       material packets, raw FBA writes
 *   rd_pabe      specular/reflection passes (RegistPacket.c), raw PABE writes
 *   rd_col_clamp  shadow_Reset (Shadow.c)
 *   rd_tex_a      gsb_set*Reg, 2D sprite paths, raw TEXA writes
 *   rd_sampler   tex_TransTexture (TexExt filters), raw TEX1/CLAMP writes
 *   rd_texture   tex_TransTexture, raw TEX0 writes (hand-converted sites only)
 *   rd_uv_offset  tex_TransTexture uOfs/vOfs
 *   rd_color_mask raw FRAME.FBMSK writes (darkVolume.c)
 */
void rd_blend(RdBlend eq, uint8_t fix, int abe);
void rd_test(const RdTestState *test);
void rd_test_gs(uint64_t gsTestWord); /* convenience: rd_test(rd_test_from_gs(v)) */
void rd_z_write(int on);
void rd_fba(int on);
void rd_pabe(int on);
void rd_col_clamp(int on);
void rd_tex_a(RdTexA mode);
void rd_sampler(RdFilter mag, RdFilter min, RdWrap s, RdWrap t);
void rd_texture(RdTex tex, RdTexFn fn, RdTcc tcc);
void rd_texture_off(void);
void rd_uv_offset(float u, float v);
void rd_color_mask(uint32_t fbmsk);

/* For GifPacket.c's GS register decoding (raw
 * gif_SetGsReg writes set one register, never a group):
 *   rd_abe           PRIM.ABE alone (rd_blend sets the equation and ABE together)
 *   rd_blend_func     the ALPHA register alone: equation and FIX, ABE untouched
 *   rd_scissor       SCISSOR_1, GS pixels of the bound target, inclusive
 *                    (rd_set_target resets it to the target's size)
 *   rd_sampler_filter TEX1 alone (MMAG, MMIN base filter); wrap untouched;
 *                    no mipmapping (a texture with GS levels draws level 0)
 *   rd_sampler_tex1   the whole TEX1 word: the filters as rd_sampler_filter
 *                    takes them from it, and its mipmap fields (MXL, MMIN,
 *                    LCM, L, K: rd_tex1_lod), which pick the level a texture
 *                    with GS levels is drawn at (rdtex_store_levels)
 *   rd_sampler_wrap   CLAMP alone; filters untouched
 *   rd_gouraud       PRIM.IIP: 1 = Gouraud (the default), 0 = flat, where the
 *                    GS takes the colour of a primitive's last vertex
 *                    (triangles, strips, fans, lines; sprites always use the
 *                    second vertex) */
void rd_abe(int abe);
void rd_blend_func(RdBlend eq, uint8_t fix);
void rd_scissor(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
void rd_sampler_filter(RdFilter mag, RdFilter min);
void rd_sampler_tex1(uint64_t tex1);
void rd_sampler_wrap(RdWrap s, RdWrap t);
void rd_gouraud(int iip);
/* PRIM.AA1, the GS's edge antialiasing (0 = off, the default).  It acts on
 * lines and triangles only: their edge pixels take the coverage as As and
 * write no Z; points and sprites ignore it.  GifPacket.c sets it from PRIM
 * and returns it to 0 at the end of the packet, so the state never leaks
 * into another list. */
void rd_aa1(int aa1);

/* ------------------------------------------------------------ targets */

/* Named target handle; gsb_SetFrame, gif_SetDrawEnviroment with the fixed
 * TBPs (0, 0x800, 0x2800, 0x2C00, 0x3000, 0x3F00, 0x142 ...) map to these. */
RdTarget rd_target(RdTargetId id);
/* tex_AllocVramAuto / per-priority VRAM bump allocation for render-to-texture
 * (puddle.c, pool.c, queen_barrier_disp.c): a target that lives until
 * rd_begin_frame unless keepAcrossFrames is set. */
RdTarget rd_temp_target(uint32_t gsW, uint32_t gsH, int withDepth, int keepAcrossFrames);
/* gif_SetDrawEnviroment(tbp, psm, w, h, ...) / gsb_SetFrame: direct
 * subsequent draws in the current list to the target.  gsW/gsH are the GS
 * size the caller believes it is drawing into; useOffset applies the
 * XYOFFSET centre (2048 - w/2, 2048 - h/2) the game sets alongside. */
void rd_set_target(RdTarget color, RdTarget depth, uint32_t gsW, uint32_t gsH, int useOffset);
/* Note (from GifPacket.c gif_SetDrawEnviroment): the GS always
 * centres XYOFFSET at (2048 - w/2, 2048 - h/2); useOffset adds the
 * screenOffsetX/Y field offset on top, which the preset supplies (zero in
 * Original; the field parity's half line is RD_TARGET_HALF_Y).  depth names the
 * target whose depth buffer is bound (pass the colour target again for
 * SCENE; id 0 = none).  The call also resets the scissor to gsW x gsH, as
 * gif_SetDrawEnviroment writes SCISSOR_1. */
/* gsb_UpdateGSSystem's clears / gif_Sprite full-screen clears at the frame
 * head: clear colour and optionally depth+stencil of a target. */
void rd_clear_target(RdTarget t, const uint8_t rgba[4], int clearDepth, uint32_t z);
/* Use a target as a texture (puddle/pool reflection, motion blur history
 * reading DISPLAY as RGB24, fog/DoF reading SCENE depth). */
RdTex rd_target_texture(RdTarget t, RdTexView view);

/* ------------------------------------------------------------- textures */

/* tex_loadImage / TIM2 decode in Texture.c: upload an RGBA8 image.  texaMode
 * tells the cache which TEXA variant this expansion was baked with (only
 * meaningful for PSMCT16/24 sources). */
RdTex rd_create_texture(uint32_t w, uint32_t h, const void *rgba8, RdTexA texaMode,
                        const char *debugName);

/* A texture whose alpha byte is still the source
 * format's, expanded by the shader under the TEXA state in force at replay
 * (so TEXA leaks between lists exactly as on the GS) instead of being baked
 * per TEXA mode.  RGB24: the alpha byte is ignored (TA0, AEM).  RGBA16: the
 * alpha byte holds the 1-bit A (0 or 1; TA1 when set, else TA0).  The values
 * equal TEXFMT_* in port/shaders/gs_math.hlsli. */
typedef enum RdTexSrc { RD_TEXSRC_RGBA32 = 0, RD_TEXSRC_RGB24 = 1, RD_TEXSRC_RGBA16 = 2 } RdTexSrc;

RdTex rd_create_texture_src(uint32_t w, uint32_t h, const void *rgba8, RdTexSrc src,
                            const char *debugName);
/* tex_scrollClut: the CLUT changed, re-expanded pixels follow (the whole
 * texture, in its own format: w * h bytes for an R8 one). */
void rd_update_texture(RdTex t, const void *rgba8);
void rd_destroy_texture(RdTex t);

/* A one-channel coverage texture (R8), w * h bytes (null: zero), the
 * port's font atlas pages.  A byte
 * is the coverage in GS alpha units (0x80 = full, as an RGBA8 texture's
 * alpha byte) and stands for a white texel with that alpha: screen prims
 * and overlay prims that sample it are drawn with font_ps, whose texture
 * function, TCC, alpha test and output are sprite_ps's, so the pixels are
 * those of the same texels as RGBA8 (255, 255, 255, cov) at a quarter of
 * the memory.  Never mipmapped, whatever the filter option. */
RdTex rd_create_texture_r8(uint32_t w, uint32_t h, const uint8_t *cov, const char *debugName);
/* Replaces the w x h texels at (x, y) of an image texture with
 * px (tightly packed rows in the texture's format: w * h bytes for R8,
 * w * h * 4 for RGBA8).  The rectangle is clipped to the texture; the
 * next replay uploads the union of the rectangles changed since the last
 * upload, not the whole texture.  An update that changes nothing is not
 * uploaded. */
void rd_update_texture_rect(RdTex t, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                            const void *px);

/* Sheet text.  The menus' words drawn in the look of
 * the game's 4-bit menu sheets (text/menu_PAL_*.tm2: light letters, a
 * darker rim around them, the antialiasing mixed into a few colours) from a
 * plain coverage strip, so a font rasterised on the sheets' own texel grid
 * reads like the sheets did in every preset and scale.
 *
 * rd_create_texture_sheet  a one-channel texture, w * h bytes of coverage
 *                        (null: zero): 0 none to 255 full (a rasteriser's
 *                        8-bit bitmap, not GS units), one byte a sheet
 *                        texel (1 x unit wide, one field line tall); style
 *                        null is {1, 0, 255, 1} (rim on, black rim, white
 *                        fill, dithered).  It is drawn, by screen prims
 *                        and overlay prims alike, with font_sheet_ps
 *                        (RD_FS_FONT_SHEET), which makes
 *                        each texel (x, y) of the sheet from the coverage
 *                        around it (texels outside the texture have
 *                        coverage 0):
 *                          c  the coverage at (x, y);
 *                          r  the largest coverage within ICO_SHEET_RX
 *                             texels across and ICO_SHEET_RY down, each
 *                             scaled by the falloff ICO_SHEET_WX[|dx|] *
 *                             ICO_SHEET_WY[|dy|] (per mille,
 *                             shader_consts.h), rounded: the rim, a dark
 *                             halo that fades out;
 *                          a  rimOn ? max(c, r * w) : c, the texel's
 *                             opacity, w the rim's weight (rimWeight / 64,
 *                             rounded; 1 when rimWeight is 0 or 64 or more);
 *                          t  c / a (0 where a is 0): rim (0) to fill (1);
 *                          a and t quantised to ICO_SHEET_LEVELS levels,
 *                          against a 4x4 Bayer threshold picked by (x, y)
 *                          (the sheets' mottled antialiasing; fixed to the
 *                          texel, so the grain moves with the text) or, with
 *                          dither 0, rounded;
 *                          grey  the rim level lerped to the fill level by t;
 *                          alpha a in GS units (full = 0x80).
 *                        A sampled pixel blends the four sheet texels around
 *                        its position bilinearly, as the GS's bilinear read
 *                        of a 4-bit sheet did, whatever the draw's filter
 *                        and wrap (clamped, never mipmapped).  The texel
 *                        (grey, grey, grey, alpha) then takes sprite_ps's
 *                        path: MODULATE (or DECAL) with the vertex colour,
 *                        TCC, the alpha test, DATE and the blend, so a row's
 *                        colour, fades and the glow's additive draw
 *                        multiply in as for a sheet sprite.
 *                        port/render/test/sheet_ref.c computes the same
 *                        texels and samples on the CPU (the tests' oracle)
 * rd_set_texture_sheet_style  the style the next replays draw t with (the
 *                        language's rim level, without rasterising
 *                        again); ignored for any other texture.  The style
 *                        is read when a frame is replayed, not when its
 *                        draws are recorded
 * rd_update_texture_rect   takes a sheet texture's rectangles as R8's: w * h
 *                        bytes of coverage
 * Scaled strips: with
 *                        style.scale s > 1 the coverage holds s x s texels
 *                        for each sheet texel (a strip rasterised s times
 *                        finer for a scene or an output s times the GS's).
 *                        The letters are those texels' own and the rim is
 *                        the sheet's, magnified as the GS magnified a
 *                        sheet: each sheet texel (X, Y) (texels sX ..
 *                        sX + s - 1 across, sY .. down) has the coverage
 *                        C, the mean of its s x s texels rounded, and the
 *                        rim R, the 1x dilation of C (ICO_SHEET_RX and
 *                        ICO_SHEET_RY sheet texels, the per-mille tables as
 *                        they are); its rim level is
 *                        L = quantise(rimOn ? R * weight : 0) against its
 *                        own Bayer threshold (X & 3, Y & 3).  A texel
 *                        (x, y) blends the L of the four sheet texels
 *                        around its centre, ((x + 0.5) / s - 0.5,
 *                        (y + 0.5) / s - 0.5), bilinearly: Lm on the
 *                        0..255 scale and Am of the GS units
 *                        (L * 128 + 127) / 255.  With c the texel's own
 *                        coverage and th the threshold of the sheet texel
 *                        it lies in (floor(x / s), floor(y / s)):
 *                          alpha  the larger of Am and the fill's
 *                                 quantise(c) in GS units, rounded;
 *                          grey   the rim level lerped to the fill level
 *                                 by t = c / max(c, Lm rounded),
 *                                 quantised;
 *                        so outside the letters a texel is the 1x strip's
 *                        rim magnified s times (its steps, its grain the
 *                        sheet texel's), and the letters' edge is the fine
 *                        texel's.  The bilinear blend of a sample is of
 *                        the texture's own texels.  Such a texture is
 *                        w x h with the coverage in rows 0 .. h/2 - 1 and
 *                        R in rows h/2 .. h - 1, each sheet texel's over
 *                        its s x s texels (the caller fills them with
 *                        rd_sheet_rim); the coverage and the rim are 0
 *                        outside their halves, and a draw addresses the
 *                        top half.  s = 1 draws the unscaled path above
 *                        (the rim made by the shader).
 * rd_sheet_rim            R of the sheet texels (s x s texels each) that
 *                        the rw x rh texels at (x, y) of a w x h coverage
 *                        at scale s touch (rows packed, 0 outside it),
 *                        into rim at those sheet texels' texels (w bytes a
 *                        row), whole sheet texels: a rectangle on the
 *                        sheet texels' corners writes only itself.  The
 *                        rectangle is clipped to the coverage
 * A dump keeps a sheet texture's coverage and style (rd_dump.c). */
typedef struct RdSheetStyle {
    uint8_t rimOn;     /* nonzero: the rim is drawn (light ink: the menus' words) */
    uint8_t rimLevel;  /* grey of the rim, 0..255 (English sheets 0, French ~62) */
    uint8_t fillLevel; /* grey of the letters' fill, 0..255 */
    uint8_t dither;    /* nonzero: the Bayer threshold; 0: rounded to the levels */
    uint8_t rimWeight; /* the rim's strength in 64ths (a faint halo); 0 or 64 and more:
                          full */
    uint8_t scale;     /* coverage texels a sheet texel, across and down
                          (a strip rasterised at the scene's scale); 0 or 1: one,
                          above ICO_SHEET_SCALE_MAX: that */
} RdSheetStyle;

RdTex rd_create_texture_sheet(uint32_t w, uint32_t h, const uint8_t *coverage,
                              const RdSheetStyle *style, const char *name);
void rd_set_texture_sheet_style(RdTex t, const RdSheetStyle *style);
void rd_sheet_rim(const uint8_t *coverage, uint32_t w, uint32_t h, uint32_t scale, int32_t x,
                  int32_t y, int32_t rw, int32_t rh, uint8_t *rim);

/* -------------------------------------------------------------- meshes */

/* The mesh path is rd_mesh.h's: the VU1 program shaders on the packets the
 * game builds. */

/* --------------------------------------------------- immediate prims */

/* gif_Sprite, gif_Draw*Strip*, gif_Line, gif_Point, layout_texture.c,
 * jimaku.c, DisplayFont.c, staticBlur.c quads: primitives in GS window
 * space.  space tags UI vs WORLD for mirror/widescreen. uvFixed: s,t are
 * 12.4 texels (UV register) rather than STQ. */
/* Port UI text: uvFixed = RD_UV_FIXED_CONTINUOUS is uvFixed 1 for
 * sprites that are not PS2 content (port/ui's glyph quads): on a scaled
 * target they rasterise continuously, without the GS-pixel snapping and the
 * UV shift.  At scale 1 it is uvFixed 1. */
#define RD_UV_FIXED_CONTINUOUS 2
void rd_screen_prims(RdPrim type, const RdScreenVtx *v, uint32_t count, RdSpace space, int uvFixed,
                     RdKey key);
/* While space >= 0, rd_screen_prims records that space instead
 * of its argument (a test's: no game code calls it since the game tags its
 * full-frame items with GifPacket.h's gif_FullFrameBegin).  -1 ends it.
 * Returns the previous value. */
int rd_set_space_override(int space);
/* (tests) World-space vertices with a matrix.  No game code calls it and the
 * replay has no model for its command (rd__not_implemented); rd_filter_test
 * records it for the draw filter. */
void rd_world_prims(RdPrim type, const RdWorldVtx *v, uint32_t count, const float *mtx, RdKey key);
/* (tests) A shadow volume as one float strip: v[i] = (GS 12.4 window X, Y,
 * GS Z, unused), every triangle counted with sign (stencil increment or
 * decrement), depth-tested against SCENE, no writes to colour or depth.  No
 * game code calls it (Shadow.c uses rd_shadow_tris below); rd_state_test and
 * rd_filter_test keep the strip path recorded and replayed. */
void rd_shadow_strip(const float (*v)[4], uint32_t count, float sign, RdKey key);

/* --------------------------------------------------------------- shadows
 * Shadow.c's count.  The PS2 adds each
 * volume face into FBP 0x142 with ALPHA 0x68 FIX 0x80 and COLCLAMP 0, the
 * face colour 0x04 or 0xFC by its facing, so a pixel ends at 4 n mod 256
 * for n the net count of the faces in front of the scene.  rd keeps n mod
 * 64 in the stencil of the bound depth target (INCR_WRAP / DECR_WRAP under
 * write mask 0x3F) and writes the same colour at the resolve.
 *
 * rd_shadow_count_target  the per-frame stand-in for FBP 0x142: an RGBA8
 *                       target of the scene size gsW x gsH, created by the
 *                       first call in the open frame (rd_temp_target); {0}
 *                       outside a frame
 * rd_shadow_reset        shadow_Reset's clear of FBP 0x142: the stencil of
 *                       the bound depth target to 0 (the colour target is
 *                       written whole by the resolve)
 * rd_shadow_tris         shadow_RenderVolume's strips as triangles: v holds
 *                       3 x triCount XYZ2 (x, y, z; the rest unused), sign[t]
 *                       > 0 for a triangle whose last vertex had the colour
 *                       0x04 (flat shading), < 0 for 0xFC.  Depth-tested
 *                       with the state's TEST against the bound depth
 *                       target, no colour or depth writes
 * rd_shadow_resolve      the count into the bound colour target: RGB =
 *                       4 n mod 256, A = 0x80 where that is not 0, else 0
 *                       (the TEXA expansion shadow_Draw's PSMCT24 read of
 *                       FBP 0x142 applies, TA0 0x80 AEM, baked so the chain
 *                       filters expanded texels as the GS does) */
RdTarget rd_shadow_count_target(uint32_t gsW, uint32_t gsH);
void rd_shadow_reset(void);
void rd_shadow_tris(const RdScreenVtx *v, const int8_t *sign, uint32_t triCount, RdKey key);
void rd_shadow_resolve(void);

/* ------------------------------------ render-to-texture surfaces (rd_water.c)
 * puddle.c, pool.c and queen_barrier_disp.c draw into a VRAM block from
 * tex_AllocVramAuto right after tex_ResetVramPri, which is always TBP 0x2800
 * in their lists (4 and 10), and sample it with TEX0 at that block.  The
 * register decoder (GifPacket.c) maps that block to the named AA0 target,
 * which has no depth buffer and is 256 x 256 (the queen barrier's block is
 * 512 x 256).  These calls let the game files bind a per-frame target of the
 * block's own size in its place:
 *
 * rd_gs_named_block  the named target GifPacket.c's decoder maps a FRAME at
 *                  block tbp (FBP = tbp / 32) of gsW x gsH to (its
 *                  gsTargetOfFbp table: 0, 0x800, 0x2800, 0x2840, 0x2C00,
 *                  0x3000, 0x3F00); {0} when the decoder makes its own
 *                  temporary target for the block
 * rd_block_target   the target standing for VRAM block tbp of gsW x gsH in
 *                  the open frame: created by the first call (rd_temp_target,
 *                  with a depth buffer when withDepth), the same target for
 *                  the same (tbp, gsW, gsH) for the rest of the frame, as
 *                  the VRAM block is one block whoever draws into it; {0}
 *                  outside a frame.  Any width: the block is drawn at its
 *                  own GS size at every aspect (one the game sizes as the
 *                  native frame, gsFrame.w x ScreenHeight, takes the scene's
 *                  resolution scale like SCENE; rd_core.c
 *                  rd__target_scale_of), or at the scene's scale under
 *                  Water reflections: Sharp (RdSettings.waterSharp)
 * rd_alias_target   from here on, in the current list and the open frame,
 *                  rd_set_target and rd_clear_target of `from` and rd_texture
 *                  of a view of `from` (rd_target_texture) record `to`
 *                  instead; rd_set_target binds `to`'s depth buffer when it
 *                  was given none or `from`.  to = {0} ends it.  Recording
 *                  only: the commands name `to`, so replay and dumps see an
 *                  ordinary target.  At most 4 aliases a list
 * rd_push_camera    the camera of the draws recorded from here until the
 *  rd_pop_camera    matching rd_pop_camera in the current list (puddle.c and
 *                  pool.c call gsb_SetVSMatrix mid-frame for their
 *                  reflection views and restore the matrices by copying).
 *                  The frame camera (rd_set_camera, gsb_MakeCommonMatrix) is
 *                  untouched; the scope is kept with the open frame for the
 *                  Enhanced projection and interpolation (not dumped).  The
 *                  Original preset draws from the matrices the VU packets
 *                  carry and does not read it.  Nesting depth 4, 8 scopes a
 *                  frame
 * rd_grid_screen_st the next grid draw (rd_draw_vu_grid, from
 *                  prim_DispMesh3D) has STs that pool.c computed as a screen
 *                  position of each vertex through the frame's world to
 *                  screen matrix (an RdGridSt formula below), with sx = 1 /
 *                  ScreenWidth and sy = 1 / ScreenHeight as the game had
 *                  them and each vertex's wave height in its ST's w.  While
 *                  pictures are blended and the camera moves, the
 *                  interpolation computes those STs again through the
 *                  blended camera instead of blending the two ticks' STs
 *                  (rd_interp.c).  Recorded in the grid's command (b[5],
 *                  f[0], f[1]), so dumps keep it; it applies to one draw */
RdTarget rd_gs_named_block(uint32_t tbp, uint32_t gsW, uint32_t gsH);
RdTarget rd_block_target(uint32_t tbp, uint32_t gsW, uint32_t gsH, int withDepth);
void rd_alias_target(RdTarget from, RdTarget to);
void rd_push_camera(const RdCamera *cam);
void rd_pop_camera(void);
/* rd_camera_depth     the rd_push_camera scopes open in the frame being
 *                    recorded (0 outside a frame)
 * rd_frame_projected  while on, the screen prims (rd_screen_prims) and
 *                    shadow volumes (rd_shadow_tris) recorded are marked as
 *                    projected on the CPU by the frame camera (rd_set_camera:
 *                    GsBase.c's +0x80 view and +0xC0 screen matrix, whose
 *                    product is +0x100): their GS X, Y and Z are a world
 *                    point seen through that camera, so the presenter may
 *                    see the point through the camera it blends between
 *                    ticks.  Never inside an rd_push_camera scope (a
 *                    reflection's camera).  Recording only; off by default */
int rd_camera_depth(void);
void rd_frame_projected(int on);

/* rd_grid_screen_st's formulas, pool.c's per vertex q (h its wave height in
 * ST.w, M the grid's world to GS screen, (x, y) = (M q).xy / w its GS
 * position, iw = 1 / w):
 *   SURFACE        updatePoolGeo's refracting surface:
 *                  s = (x - 2048) sx + 0.5 + 30 h iw, t the same with y, sy
 *   REFLECT        updatePoolGeo's reflecting surface: the ray from the eye
 *                  to q reflected about the normal (0.1 h, -1, 0.1 h), its y
 *                  negated, is r; d is the GS position of q + r less (x, y);
 *                  s = (x - 2048 + 1000 d.x) 0.8 sx + 0.5, t the same with
 *                  y, d.y, sy
 *   RIPPLE         SetLimitedPoolReflactionMesh: SURFACE with 50 h iw
 *   RIPPLE_CLAMPED SetLayoutedPoolReflactionMesh: RIPPLE, s at least 0 and
 *                  t at most 1 */
typedef enum RdGridSt {
    RD_GRID_ST_NONE = 0,
    RD_GRID_ST_SURFACE = 1,
    RD_GRID_ST_REFLECT = 2,
    RD_GRID_ST_RIPPLE = 3,
    RD_GRID_ST_RIPPLE_CLAMPED = 4,
    RD_GRID_ST_COUNT
} RdGridSt;

void rd_grid_screen_st(RdGridSt kind, float sx, float sy);

/* --------------------------------------------------------------- post */

/* gsb_PostEffect family, staticBlur.c, ZFog.c, Shadow.c composites, and
 * gif_MoveImage: fullscreen or rectangle passes between targets.  Recorded
 * into the current list like any draw. */
void rd_post(RdPostKind kind, const RdPostParams *params);
/* RD_POST_FOG (fog_DrawFog): the fog sprite alone, drawn with the state in
 * force (the caller records ZFog.c's register writes as rd state first: the
 * colour target,
 * TEX0 as rd_target_texture(SCENE, RD_VIEW_DEPTH) with MODULATE and TCC RGBA,
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
/* RD_POST_MOTION_BLUR .. RD_POST_EYE_BLUR (staticBlur.c): one GS sprite,
 * PRIM 0x116 or 0x406 with ABE as gif_SpriteSensitiveOrg sends it, drawn
 * with the state in force
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
 *            [2] 1, not read (every present of a tick draws the tick's FIX
 *            over the tick's old frame, rd_interp.c feedback)
 *   lines    TEX0.TFX: 0 MODULATE, 1 DECAL, 2 HIGHLIGHT, 3 HIGHLIGHT2
 *   exactInt 1 */

/* ------------------------------------------- frame lifecycle and camera */

/* The flip (gsb_UpdateGSSystem -> sceGsSwapDBuff)
 * sends the scene's draw environment and, with fbClear, its clear packet on
 * GIF path 3 right before dl_Swap kicks the frame's lists, so the PS2 drew
 * every frame (keep frames too) over:
 *   FRAME FBP 0x40, ZBUF 0xC0 PSMZ32 write on, XYOFFSET centred (+0.5 line
 *   when the draw environment carries sceGsSetHalfOffset's half offset),
 *   SCISSOR full, PRMODECONT 1, COLCLAMP 1, DTHE 0, TEST 0x50000; then
 *   TEST 0x30000, PRIM 6 (sprite, no ABE, no TME, flat), RGBAQ = the BG
 *   colour, the full-scene sprite at Z 0, TEST 0x50000.
 * rd_frame_head records that, as rd state and an rd_clear_target, at the head
 * of list 0 and again at the head of list 11 of the open frame (call it right
 * after rd_begin_frame).  rd_frame_flip, called before the frame closes, sets
 * what the flip actually sent: the BG colour current at the flip (the PS2
 * consumes gsb_SetBGColor's packet then, one tick after the frame opened) and
 * the half offset of the draw environment that flip sends.  rd_end_frame keeps
 * the copy in the first replayed list (11 for a keep frame, 0 otherwise) and
 * turns the other into no-ops, so a full frame's list 11 runs in whatever
 * state list 10 left, as on the GS. */
typedef struct RdFrameHead {
    uint8_t rgba[4];   /* RGBAQ of the clear sprite (gsb_SetBGColor, alpha 0x80) */
    uint32_t z;        /* the clear sprite's Z (sceGsSetDefClear: 0) */
    uint32_t gsW, gsH; /* the scene size (GsBase.c gsFrame.w, ScreenHeight) */
    float aspect;      /* gsFrame.aspect, wExact: gsFrame.wExact (dump version 10); 0 = 4:3 */
    float wExact;
    uint8_t halfY; /* XYOFFSET.y + 8 (sceGsSetHalfOffset with half != 0) */
    uint8_t clear; /* fbClear: the clear packet is part of the flip */
    uint8_t _pad[2];
} RdFrameHead;

void rd_frame_head(const RdFrameHead *head);
/* Rewrites the recorded head's clear colour and half offset; no-op without a
 * recorded head in the open frame. */
void rd_frame_flip(const uint8_t rgba[4], int halfY);

/* rd_set_target's useOffset, bit 1: XYOFFSET.y + 0.5 GS pixel, the
 * field half offset sceGsSetHalfOffset writes into the flip's draw
 * environment.  Bit 0 keeps its meaning. */
#define RD_TARGET_OFFSET 1
#define RD_TARGET_HALF_Y 2

/* The ZBUF.PSM of a target's depth buffer, which fixes how GS Z maps to
 * depth (FrameCB g_z.x, gs_z_to_depth): every ZBUF the game writes is PSMZ32
 * (ZBUF 0x...000C0 with PSM nibble 0), so that is the default for SCENE and
 * temporary targets; PSMZ24/16 exist for completeness. */
typedef enum RdZFormat { RD_ZFMT_32 = 0, RD_ZFMT_24 = 1, RD_ZFMT_16 = 2 } RdZFormat;

void rd_set_target_z_format(RdTarget t, RdZFormat fmt);
/* GS Z to depth for a target's Z format: the scale FrameCB g_z.x carries
 * (PSMZ32: 2^-33 on a D32S8 depth buffer, 2^-32 on D24S8; PSMZ24 2^-24,
 * PSMZ16 2^-16). */
float rd_target_z_scale(RdTarget t);

/* gsb_MakeCommonMatrix's per-frame VU1 parameter block: the 16 quadwords
 * the packet unpacks (VIF UNPACK V4-32, 16 qw) to VU1 data memory 0..15,
 * referenced from the current position of every one of the 13 lists each
 * time it is built.  Matrices are column-major float[16] as the scratchpad
 * holds them (matrixptr offsets in parentheses).  rd keeps the last one
 * recorded in the open frame. */
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

void rd_set_vu_common(const RdVuCommon *block);
/* The block of the open frame, else of the last closed frame; NULL before
 * the first one. */
const RdVuCommon *rd_get_vu_common(void);

/* ------------------------------------------------------- verification */

/* rd can serialise the current RdFrame (all lists, payload arena, textures
 * referenced by id) so the replay tool (port/render/tools/rd_replay_tool.c)
 * can replay it headless on another backend.  Dumps contain game assets and
 * are never committed. */
bool rd_dump_frame(const char *path);
/* The device-loss dump (port/platform/window_host.c device_lost_quit;
 * docs/port/RENDER.md, "The loss dump"): the frames the renderer still holds
 * as two ordinary dumps, the last closed frame (the one the lost replay
 * drew, or the tick it was blended towards) to lostPath and the one before
 * it, when the ring still holds it, to beforePath (NULL: not written).  CPU
 * data only (the commands, the payload, the image texels and VU streams the
 * renderer keeps), so it works after the device is gone.  The frames'
 * numbers into *lostFrame and *beforeFrame (0: not written; either may be
 * NULL).  True when the lost frame was written. */
bool rd_dump_retained(const char *lostPath, const char *beforePath, uint32_t *lostFrame,
                      uint32_t *beforeFrame);

/* The loss bisect (docs/port/RENDER.md, "The loss bisect"): the frame of
 * the dump at path replayed in this renderer one pass longer each time
 * (the pass window, rd_internal.h rd__replay_set_window: passes 1..k),
 * waiting for the GPU after each (rhi_wait_idle), until the device is lost
 * or the whole frame has run.  Nothing is presented.  The frame's pipelines
 * are made first through rhi_create_pipelines in batches, with the progress
 * function (rd_set_pipeline_progress) as rd_precreate_pipelines runs it; a
 * nonzero return stops the bisect (RD_BISECT_NOT_RUN).  The renderer must
 * have a device; a loss leaves it lost (the caller shuts it down). */
typedef enum RdBisectResult {
    RD_BISECT_NOT_RUN = 0, /* no device, the dump could not be read, or the progress function
                              stopped it: line says why */
    RD_BISECT_LOST,        /* pass `pass` lost the device: line names it */
    RD_BISECT_CLEAN        /* every pass ran without a loss */
} RdBisectResult;

typedef struct RdLossBisect {
    RdBisectResult result;
    uint32_t frame;   /* the dump's frame number */
    uint32_t pass;    /* RD_BISECT_LOST: the pass (1, 2, ...); 0: before any pass drew */
    uint32_t draws;   /* RD_BISECT_LOST: the draws that pass asked for (the second level's N) */
    uint32_t passes;  /* the frame's passes, counted by a replay that draws nothing */
    uint32_t replays; /* the replays it took (the count and passes 1..k; not the
                         replays that only name the frame's pipelines) */
    double ms;        /* the wall time */
    /* RD_BISECT_LOST: the pass that lost the device, in the pass ring's
     * words (rd_passring.h rd_passring_format_bisect); RD_BISECT_NOT_RUN:
     * why; RD_BISECT_CLEAN: empty */
    char line[320];
    /* RD_BISECT_LOST: the pass that began after it (its first command ran
     * up to that pass's start and drew nothing), "" when none */
    char next[320];
} RdLossBisect;

void rd_loss_bisect(const char *path, RdLossBisect *out);

/* The loss bisect's second level (port/render/rd_bisect_plan.h): what one
 * draw of the window was, noted while a draw window is set (no work
 * otherwise).  The draw's place (list, command, its number in the pass),
 * kind (RDC_* type; vu: a VU draw), the mesh, the program and its shaders,
 * the index range drawn (count from first) and the values' span and
 * marked triangles, the batch, the clip mode, the texture bound (id, size,
 * texel format, mip levels) and the pipeline key's words. */
typedef struct RdDrawNote {
    uint8_t valid, vu, type, prog, vs, fs, clip, skinned;
    int8_t list;
    uint8_t texFormat, texMips;
    uint32_t cmd, ordinal, count, first, batch;
    uint32_t tex, texW, texH;
    uint32_t spanLo, spanHi, marked;
    char mesh[25];
    char key[96];
} RdDrawNote;

/* the note in a line ("list 0 command 27, mesh st26a_p1, prog 0 vs 5 fs 5
 * clip 0, ...") */
void rd_draw_note_text(const RdDrawNote *n, char *out, size_t size);

/* One step of the second level: the frame of the dump at path (its
 * pipelines made first, as rd_loss_bisect does) replayed once with passes
 * 1 .. pass - 1 and draws drawFirst .. drawLast of pass `pass` (numbered
 * as the first level counted them) that pass filter (RdBisectFilter, arg
 * its value: 0 every draw, 1 every draw with the VU draws' marks left out,
 * 2 the draws of vertex shader arg, 3 the VU draws of clip mode arg, 4 the
 * skinned draws, 5 the draws binding texture arg), then a wait for the
 * GPU.  result: RD_BISECT_LOST or RD_BISECT_CLEAN, RD_BISECT_NOT_RUN with
 * line saying why; note: the last draw the step recorded. */
typedef struct RdLossStep {
    RdBisectResult result;
    uint32_t passes; /* the frame's passes */
    uint32_t draws;  /* the draws pass `pass` asked for (before the window) */
    uint32_t drawn;  /* the draws the step recorded in it */
    double ms;
    RdDrawNote note;
    char line[320];
} RdLossStep;

void rd_loss_bisect_step(const char *path, uint32_t pass, uint32_t drawFirst, uint32_t drawLast,
                         int filter, uint32_t arg, RdLossStep *out);
/* A dump's frame size and aspect without loading it (the loss bisect sizes
 * its renderer with them); false when path is no dump this version reads
 * or its size is outside 1..4096. */
bool rd_dump_frame_size(const char *path, uint32_t *gsW, uint32_t *gsH, float *aspect);
/* Readback of the DISPLAY target (the reduced frame the presenter scales;
 * the output itself is rd_read_presented), tightly packed RGBA8, for
 * screenshots and image comparison. */
bool rd_read_display(void *dst, uint32_t *w, uint32_t *h);

/* --------------------------------------------------------- statistics */
typedef struct RdStats {
    uint32_t draws, pipelines, pipelineCreates, textureUploads, tempTargets, bytesPayload;
    float gpuMs;
    uint32_t tempReused; /* temporary targets that took a pooled texture */
} RdStats;

const RdStats *rd_get_stats(void);

/* The region rule's card verdicts decided on the CPU since the start
 * (rd_mesh.c, rd_mesh.h RdVuCards): free-standing strips tested, triangles
 * tested (the first passing one ends a strip), and the time it took in ms,
 * all on the simulation's thread as the draws are recorded (the window's
 * summary line gives them per step) */
void rd_vu_card_counts(uint64_t *strips, uint64_t *triangles, double *ms);

/* One record per replay (rd_end_frame's, rd_present's, the replay tool's).
 * CPU phases in ms: interp (rd__interp_frame building the blended copy), wait
 * (rhi_wait_frame:
 * the GPU finishing the frame RHI_FRAMES_IN_FLIGHT replays ago), acquire
 * (the swapchain image), upload (textures, meshes, temporary target clears
 * into the ring and their copies), walk (the command lists: state, geometry
 * expansion, ring writes and command encoding), of which bind (bind group
 * creation), submit, present (the present call), readback (synchronous
 * readbacks: dumps and screenshots only).  The counts are RhiStats deltas
 * since the previous record, so they include what the recording of the
 * frame created (temporary targets, textures).  GPU times come from
 * timestamps when the backend has them (gpuValid), RHI_FRAMES_IN_FLIGHT
 * replays later; rd_perf_pop returns a record once they are in. */

/* The picture effects RdPerfRecord.gpuPostMs times apart.  Each is part of
 * the list (or, for the CRT filter, the present) it runs in, so their times
 * are also inside gpuListMs and gpuPresentMs.  The replay tells them by the
 * effect a post sprite names and by the target a pass draws into:
 * REDUCTION the scene shrunk into the shown picture (gsb_Reduction), FOG,
 * MOTION_BLUR (the motion blur), AURA (the feedback blur of the aura: its
 * own targets AURA_WORK, AURA_TAP, FEED128 and its sprites), GLOW (flare,
 * bloom and the sun's eye blur), DOF (depth of field), SOFTEN (the screen
 * softening: the AA0/AA1 passes and the pass into SCENE right after them),
 * CRT (the CRT filter's passes in the present), MSAA (the anti-aliasing's
 * resolve of the multisampled SCENE, rd_replay.c msaaEnd). */
enum {
    RD_PERF_POST_REDUCTION = 0,
    RD_PERF_POST_FOG,
    RD_PERF_POST_MOTION_BLUR,
    RD_PERF_POST_AURA,
    RD_PERF_POST_GLOW,
    RD_PERF_POST_DOF,
    RD_PERF_POST_SOFTEN,
    RD_PERF_POST_CRT,
    /* the MSAA epoch's resolves of SCENE (rd_replay.c msaaEnd) */
    RD_PERF_POST_MSAA,
    RD_PERF_POST_COUNT
};

/* A RD_PERF_POST_* in plain words ("reduction", "depth of field", ...) */
const char *rd_perf_post_name(int post);

/* The pieces of GPU work the per-pass timing tells apart (RdPerfPass.kind,
 * RdPerfRecord.passKinds; rd_perf.c rd__perf_pass, docs/port/RENDER.md,
 * "Performance records"), each begun outside any render pass: UPLOAD the
 * uploads at the replay's head (textures, meshes, the new targets' clears),
 * PREPASS the VU vertex pre-pass and its copies into the record buffer,
 * RENDER a render pass of the lists (rd_replay.c beginPass: a draw's, a
 * clear's own, the COLCLAMP 0 wrap's, the shadow count's reduction), COPY a
 * target copied into another (RDC_COPY), SNAP a target copied into its
 * snapshot (a draw that reads the target it draws into), DATE the DATE
 * snapshot's pass, FOG_COPY the fog's depth read made ready (the copy of
 * the depth on the copy path, the buffer path's two transfers, the fog
 * table's upload), EXACT the exact blend's copies and pass, MSAA the MSAA
 * epoch's resolves and spreads, STENCIL a shadow reset's stencil clear as a
 * pass of its own, PRESENT the present (DISPLAY onto the window, the CRT
 * filter). */
enum {
    RD_PERF_PASS_UPLOAD = 0,
    RD_PERF_PASS_PREPASS,
    RD_PERF_PASS_RENDER,
    RD_PERF_PASS_COPY,
    RD_PERF_PASS_SNAP,
    RD_PERF_PASS_DATE,
    RD_PERF_PASS_FOG_COPY,
    RD_PERF_PASS_EXACT,
    RD_PERF_PASS_MSAA,
    RD_PERF_PASS_STENCIL,
    RD_PERF_PASS_PRESENT,
    RD_PERF_PASS_KIND_COUNT
};

/* A RD_PERF_PASS_* in plain words ("uploads", "DATE snapshot", ...) */
const char *rd_perf_pass_kind_name(int kind);

/* RdPerfPass.target: a named target (RdTargetId: SCENE, WORK0, ...) or one
 * of these */
enum {
    RD_PERF_TARGET_BLOCK = RD_TARGET_COUNT, /* a render-to-texture block */
    RD_PERF_TARGET_TEMP,                    /* another temporary target */
    RD_PERF_TARGET_NONE,                    /* none (the uploads), or gone */
    RD_PERF_TARGET_WINDOW,                  /* the window (the present) */
    RD_PERF_TARGET_RECORD                   /* the VU pre-pass's record */
};

/* RdPerfPass.target as text: "SCENE", "block", "window", ... */
const char *rd_perf_target_name(int code);

/* One piece of a timed replay's GPU work (rd_perf_passes_of): what it is,
 * the list walked and the effect in force when it began, its colour target
 * and size in texels (a copy's source; the present: the window), the draws
 * and pipeline binds the RHI recorded in it, its GPU time (from its
 * timestamp to the next one, so the barriers before it and any work the
 * replay did not name in between count for it; -1 when a stamp is missing)
 * and the CPU time the replay took to record it, in microseconds. */
typedef struct RdPerfPass {
    uint8_t kind;   /* RD_PERF_PASS_* */
    uint8_t target; /* RdTargetId or RD_PERF_TARGET_* */
    int8_t list;    /* 0 .. RD_LIST_COUNT - 1; -1: the uploads, the pre-pass, the present */
    int8_t post;    /* RD_PERF_POST_*, -1 none */
    uint16_t w, h;
    uint32_t draws, pipelineBinds;
    float gpuUs, cpuUs;
} RdPerfPass;

/* A piece's label as the window's summary writes it: the effect for a
 * render pass of one, "list N" for another render pass, else the kind
 * ("DATE snapshot"); the length written, as snprintf */
int rd_perf_pass_label(const RdPerfPass *p, char *buf, size_t n);

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
    /* draws that sampled a render-to-texture block (RdTargetRec.block: the
     * puddles', the pools', the queen barrier's) drawn at a scale other than
     * SCENE's (rd_core.c rd__target_scale_of, the other-size rule) */
    uint32_t blockOtherScaleReads;
    /* Of bindGroups, rd's own by kind.  uniformGroups: the
     * frame, draw and VU layouts' groups, whose uniforms take dynamic
     * offsets, so one group per layout and buffer serves the replay (the VU
     * layout's stream buffer is the mesh arena chunk or the ring);
     * textureGroups: one per (texture, sampler, DATE snapshot) set. */
    uint32_t uniformGroups, textureGroups;
    /* Screen-prim draws.  screenCmds: the draws the screen-prim
     * commands make one by one (a command's passes); screenDraws: the draws
     * recorded after consecutive commands under the same state are merged */
    uint32_t screenCmds, screenDraws;
    uint64_t uploadBytes;            /* everything written into the upload ring */
    uint64_t meshUploadBytes;        /* of which mesh streams and indices */
    double gpuMs;                    /* first timestamp to last */
    double gpuUploadMs;              /* the upload copies at the head */
    double gpuListMs[RD_LIST_COUNT]; /* per command list (0 for a list not replayed) */
    double gpuPresentMs;             /* the present blits */
    /* per picture effect (RD_PERF_POST_*), inside the times above */
    double gpuPostMs[RD_PERF_POST_COUNT];
    double startMs;      /* the replay's start (rd's monotonic ms clock) */
    float alpha;         /* rd_present's alpha; -1 for a replay that is not one */
    uint8_t firstOfTick; /* the first present of its frame */
    /* the replay changed effect more often than there were timestamps:
       the later changes were not timed apart (their time stayed with the
       effect, or the scene, before them) */
    uint8_t gpuPostPartial;
    uint8_t _pad2[2];
    /* MSAA (RdSettings.antiAlias): the replay's sample count (0 without)
     * and the epoch's resolves of SCENE (0 or 1 a replay) */
    uint32_t msaaSamples, msaaResolves;
    /* the slots of the replay's bind groups left without a binding (each
     * slot of rd's layouts gets one; 0 in every replay) */
    uint32_t unboundSlots;
    /* The exact blend's cost (rd_dst_read.c, issue 103): dstDraws the
     * draws recorded through a destination-read shader (either
     * realisation), storagePasses the passes begun without the colour
     * attached for the storage read, dstSwitches the passes ended and begun
     * again on the same targets to change between a storage pass and one
     * with the colour attached (rd_replay.c dstPassMode).  0 under the
     * fixed-function blend; the attachment read never switches. */
    uint32_t dstDraws, storagePasses, dstSwitches;
    /* glowPasses: the render passes begun while the replay drew the glow
     * (RD_PERF_POST_GLOW: flare, bloom, the sun's eye blur).  snapshots:
     * copies of a target into its snapshot texture (a draw reading the
     * target it draws into, a staticBlur sprite reading its destination);
     * snapSwaps: staticBlur sprites that cover their whole target and drew
     * into the snapshot texture, swapped with the target's, instead of a
     * copy (rd_replay.c doBlurSpriteDraw, blurSwapTarget) */
    uint32_t glowPasses, snapshots, snapSwaps;
    /* a1xDraws: the draw passes that took the above-1x kernels (the area
     * box, the B-spline, the fog's table read between its entries:
     * rd_replay.c spriteAbove1x, blurAbove1xSteps, doFog), g_rd.above1xDraws
     * counted per replay; each planned pass of a draw counts (fillDrawCB
     * runs per pass: the AFAIL split's two, the two-pass blend's two), so a
     * draw can count twice; 0 at scale 1.  A work buffer drawn onto the
     * frame is never among them (rd_internal.h rd__work_class) */
    uint32_t a1xDraws;
    /* texaDraws: the draw passes through the per-texel TEXA filter
     * (sprite_texa_ps, vu_texa_ps, sprite_stq_ps's four taps: a 24- or
     * 16-bit texture under a TEXA with AEM and a linear filter,
     * rd_pipeline.c rd__texa_per_texel); gridDraws: those with GS levels in
     * the Original preset drawn through the sampler on the GS's grid
     * instead (ICO_DF_GS_BILINEAR_GRID: RGBA32 and TEXA 80/80 textures,
     * rd__gs_filter_int).  Counted per planned pass as a1xDraws (fillDrawCB).
     * uintDraws: the staticBlur sprites drawn through fx_sprite_u_ps (the
     * UINT views, rd_replay.c doBlurSpriteDraw) */
    uint32_t texaDraws, gridDraws, uintDraws;
    /* The VU vertex pre-pass (rd_replay_vu.c rd__vu_prepass; docs/port/RENDER.md,
     * "The vertex pre-pass"): vuPrePasses its render passes (0 or 1),
     * vuPreVertices the vertices it recorded, vuPreDraws its draws (a VU
     * command each), vuPreFallbacks the mesh and grid draws drawn the old
     * way while it is on (a command past the record, a pipeline missing; 0
     * in a usual replay), gpuPrepassMs its GPU time (the pass and the copies
     * into the record buffer, between the uploads and list 0; inside gpuMs,
     * not in gpuUploadMs) */
    uint32_t vuPrePasses, vuPreVertices, vuPreDraws, vuPreFallbacks;
    double gpuPrepassMs;
    /* The per-pass timing (rd_perf_passes_of; docs/port/RENDER.md,
     * "Performance records").  passKinds: the pieces of GPU work begun, by
     * RD_PERF_PASS_* (every replay); passEffects: of the render passes, those
     * begun while a picture effect was in force, by RD_PERF_POST_*.
     * passTimed: the replay was timed piece by piece (every replay with
     * [dev] perf_passes, else one every 250 ms), passCount its pieces,
     * passPartial: it began more than the stamps allow (223) and the later
     * ones were timed with the last. */
    uint16_t passKinds[RD_PERF_PASS_KIND_COUNT];
    uint16_t passEffects[RD_PERF_POST_COUNT];
    uint8_t passTimed, passPartial;
    uint16_t passCount;
    /* The walk's CPU time by list, ms (from the lists' start to each list's
     * end: the binds made in it included), 0 for a list not walked */
    double walkListMs[RD_LIST_COUNT];
    /* Of uploadBytes: the textures (their levels, the white texel),
     * the uniform blocks (DrawCB, VuCB, VuBoneCB, FrameCB: rd_replay.c
     * uniformGroup, rd_replay_vu.c cbCopy), the interpolation's scratch
     * meshes (of meshUploadBytes: the transient meshes rd_interp.c blends
     * for each present), the VU streams without a mesh and the grids' index
     * lists; the rest is the screen prims' vertices, the fog's table and the
     * alignment between them */
    uint64_t uploadTextureBytes, uploadUniformBytes, uploadInterpBytes, uploadStreamBytes;
} RdPerfRecord;

/* The oldest finished record not yet popped (a queue of 64; the oldest are
 * dropped when nobody pops); false when there is none. */
bool rd_perf_pop(RdPerfRecord *out);

/* The CPU time of the replay finished last (its RdPerfRecord.totalMs,
 * popped or not): with interpolation off the window's pacing reads the
 * frame's one replay, rd_end_frame's present, after the step. */
double rd_last_replay_ms(void);

/* The pieces of timed replay `replay` (RdPerfRecord.passTimed): their
 * count, *out pointing at them until the next replay; 0 when that replay was
 * not timed or its pieces were dropped (the queue holds 8 replays).  Pieces
 * of earlier replays still queued are dropped.  Call it after rd_perf_pop
 * handed out the record. */
uint32_t rd_perf_passes_of(uint32_t replay, const RdPerfPass **out);

/* [dev] perf_passes (ICO_RD_PERF_PASSES=1): every replay is timed piece by
 * piece (the window writes ico-pc-passes.csv) */
bool rd_perf_passes_mode(void);

/* The CSV of the pieces (the window's ico-pc-passes.csv): the header line
 * and one piece's line, each with its newline; the length written, as
 * snprintf */
int rd_perf_pass_csv_header(char *buf, size_t n);
int rd_perf_pass_csv_line(uint32_t replay, uint32_t frame, uint32_t index, const RdPerfPass *p,
                          char *buf, size_t n);

/* The window's 10 s summary of the timed replays' pieces: the pieces summed
 * by what they are (kind, list, effect, target and size), the most
 * expensive first.  Zero-initialise or rd_perf_pass_agg_reset. */
#define RD_PERF_PASS_AGG_MAX 128

typedef struct RdPerfPassAggEntry {
    uint8_t kind, target;
    int8_t list, post;
    uint16_t w, h;
    uint64_t passes, draws;
    double gpuMs, cpuMs;
} RdPerfPassAggEntry;

typedef struct RdPerfPassAgg {
    uint32_t replays;      /* timed replays added */
    uint32_t count;        /* entries */
    uint64_t passes;       /* their pieces */
    uint64_t small;        /* of them, under 0.05 ms on the GPU */
    uint64_t dropped;      /* pieces whose entry found no room (in passes only) */
    double gpuMs, smallMs; /* the pieces' GPU time, and the small ones' */
    RdPerfPassAggEntry e[RD_PERF_PASS_AGG_MAX];
} RdPerfPassAgg;

void rd_perf_pass_agg_reset(RdPerfPassAgg *a);
void rd_perf_pass_agg_add(RdPerfPassAgg *a, const RdPerfPass *p, uint32_t n);
/* "list 0 SCENE 848x448 420 draws 4.12 ms, glow WORK1 256x128 10.0 passes
 * 10 draws 1.21 ms, ... (5 of 47.0 passes a replay, 16 timed replays;
 * passes under 0.05 ms: 31.0 a replay, 0.62 ms)": the top entries by GPU
 * time, per timed replay ("N passes" only where an entry is not one pass a
 * replay); the length of the text in buf (cut to fit n) */
int rd_perf_pass_agg_text(const RdPerfPassAgg *a, unsigned top, char *buf, size_t n);
/* "47.0: render passes 32.0 (lists 9.0, fog 1.0, glow 17.0, ...), DATE
 * snapshot 1.5, ...": RdPerfRecord.passKinds and passEffects summed over
 * `replays` replays, as means (kinds and effects never begun left out); the
 * length of the text in buf (cut to fit n) */
int rd_perf_pass_kinds_text(const double kinds[RD_PERF_PASS_KIND_COUNT],
                            const double effects[RD_PERF_POST_COUNT], double replays, char *buf,
                            size_t n);

#ifdef __cplusplus
}

#endif
#endif /* PORT_RENDER_RD_H */
