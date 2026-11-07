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
    RD_STENCIL_RESOLVE_BIT0,
    /* DATE through the stencil (rd_replay_tiler.c rd__replay_date_take; the count
     * leaves bit 6, RD_DATE_STENCIL_BIT, alone): CLEAR writes 0 into bit 6,
     * SET writes 1 where date_stencil_ps keeps the fragment (the
     * destination alpha's MSB), TEST0 and TEST1 pass where bit 6 is 0
     * (DATM 0) or 1 (DATM 1) and write nothing.  A PRIM.AA1 draw under
     * DATE draws each primitive as two draws (rd_replay_aa1.inc
     * aa1DateDraw): TEST0_SET is TEST0 writing 1 into bit 6 where the
     * fragment passes, TEST1_CLR is TEST1 writing 0 there */
    RD_STENCIL_DATE_CLEAR = RD_STENCIL_RESOLVE_BIT0 + 6,
    RD_STENCIL_DATE_SET,
    RD_STENCIL_DATE_TEST0,
    RD_STENCIL_DATE_TEST1,
    RD_STENCIL_DATE_TEST0_SET,
    RD_STENCIL_DATE_TEST1_CLR,
    /* the shadow resolve's A on a disc whose count steps by 8 (rd_shadow.c
     * "The step"): NOTEQUAL 0 under read mask 0x1F, the five bits 8 n mod
     * 256 keeps */
    RD_STENCIL_TEST_NONZERO5
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
    /* host only: WORK3, the glow the tick's first present pasted, kept for
     * the tick's later presents, which paste it again instead of drawing
     * the glow's chain (rd_glow_hold.c); WORK3's size */
    RD_TARGET_WORK3_HELD,
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

/* RdCamera.cut's value for rd_camera_cut_wall's cut */
#define RD_CAMERA_CUT_WALL 2

typedef struct RdCamera {
    float view[16];
    float proj[16];     /* the game's screen matrix +0xC0 (gsb_SetVSMatrix): the native
                         * projection, the same at every aspect */
    float zoom, aspect; /* vsParam[0]; the display aspect the frame was built for (GsBase.c
                         * gsFrame.aspect; 4/3 in version 9 and older dumps) */
    float nearZ, farZ;
    /* 1 on a camera cut this tick (rd_camera_cut), RD_CAMERA_CUT_WALL when the
     * eye's path from the tick before crossed the stage (rd_camera_cut_wall;
     * dumps from before it hold 0 or 1); either disables interpolation for
     * the frame */
    uint8_t cut;
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
     * wrap at the edge (rd_blur_border.c rd__blur_post_sprite); the picture's box, scale
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
    /* Downsampling ([video] downsample = "box", issue 149): the scene drawn
     * at the Resolution row's scale (and the supersampling's, measured
     * against the GS grid) is box-averaged to the game's own size, GS
     * pixels, before the presentation, which then draws it as it draws
     * Resolution 1x (rd_present.c rd__present_record).  In force only with
     * a scale or a fixed size above 1x, never with the CRT filter or the
     * "window" resolution (rd__apply_display: g_rd.downsample).  0 in a
     * zeroed RdSettings: off, nothing changes */
    uint8_t downsample;
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
    /* Motion blur: Smooth ([video] effect_motion_blur_mode = "smooth" with
     * effect_motion_blur on).  The motion blur's sprite (staticBlur.c
     * MotionBlur) reads an accumulator of the scenes of every present
     * instead of the DISPLAY the tick started from, and its FIX is spread
     * over the tick's presents (rd_blur_smooth.c): the same length of
     * trail, moved on at every present.  0 in a zeroed RdSettings: the
     * game's own (Original), as before this option; the replay tool's
     * --motion-blur smooth sets it */
    uint8_t motionBlurSmooth;
    /* Options > Effects > Motion blur strength ([video]
     * effect_motion_blur_strength, 10..100 in tens).  The motion blur's
     * sprite is drawn with FIX x motionBlurStrength / 100 rounded in place of
     * the game's FIX, under Original and Smooth alike (Smooth spreads that
     * FIX over the tick's presents): the trail fades faster below 100
     * (rd_blur_smooth.c rd__blur_strength_fix, rd_blur_border.c
     * rd__blur_post_sprite).  0 in a zeroed RdSettings, and 100 or more: the
     * game's own FIX, as before this option */
    uint8_t motionBlurStrength;
    /* Glow between frames: Smooth ([video] effect_glow_mode = "smooth" with
     * effect_glow on).  Above Frame rate Original the glow's chain
     * (rd_glow_hold.c) is drawn again at every present instead of the
     * tick's first present's glow pasted.  0 in a zeroed RdSettings: the
     * glow held, as before this option.  An explicit ICO_RD_GLOW_REDRAW
     * ([dev] glow_redraw) wins over it. */
    uint8_t glowSmooth;
    /* Options > Picture: Gamma, Picture brightness, Contrast, Saturation
     * ([video] gamma, brightness, contrast, saturation; 1..100).  The
     * colour adjustment of PCSX2's shade boost (port/shaders/tone.hlsli),
     * each value / 50 its factor, applied to the finished picture in the
     * present's box blit, or to the CRT filter's source before the filter
     * (rd_present.c rd__tone_active, rd__tone_params).  50 is the game's
     * picture; 0 (a zeroed RdSettings) reads as 50, so the present is the
     * PS2 picture's byte for byte and its box blit keeps blit_ps while
     * every value is 0 or 50.  Not the movies (they draw their own box). */
    uint8_t toneGamma, toneBrightness, toneContrast, toneSaturation;
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
/* The renderer swapped in the process (port/platform/window_switch.c):
 * rd_suspend is rd_shutdown keeping what the game set up on the renderer
 * and does not set again (the scene size, the GS state between frames, the
 * frame numbers, the mirror mode, the VU lists' resident programs) and
 * forgetting the texture cache's entries; rd_resume is rd_init at the kept
 * scene size with that state put back (false: not started, the state kept
 * for another try).  The game's own textures and meshes are made again on
 * their next draw. */
void rd_suspend(void);
bool rd_resume(const RdSettings *settings, void *sdlWindow);
/* After rd_resume and what the caller runs on the new renderer before the
 * game goes on (the self-tests, the pipeline set): a frame the game had
 * open at rd_suspend is open again, so the rest of the game's tick records
 * into it (its temporary targets freed with it) instead of being dropped
 * outside a frame; its start went with the renderer before, so it is closed
 * without a picture (its rd_end_frame discards it).  Nothing when no frame
 * was open. */
void rd_resume_frame(void);
/* gsb_Init on a 50/60 Hz switch (kanbanBoot's gsResetFunc) recreates the
 * scene-sized targets. */
void rd_reset_scene(uint32_t gsWidth, uint32_t gsHeight);
/* Settings menu: applies at the next rd_begin_frame. */
void rd_set_settings(const RdSettings *settings);
/* Before rd_init: the fallback's program entries take the plain variants
 * (issue 107; docs/port/PIPELINES.md, "The fallback entries"), reason saying
 * why for the log; NULL clears it; ICO_RD_VU_EXTENDED=1 overrides it.  No
 * caller in the tree: the window's went with [dev] vu_plain_only, which is
 * read and ignored (port/platform/host_config.c retired_dev). */
void rd_set_vu_plain_only(const char *reason);
/* Whether the run draws every VU draw with the plain variants (after
 * rd_init), and why (NULL when it does not) */
const char *rd_vu_plain_only(void);
/* Before rd_init: the run draws without the VU vertex pre-pass
 * (docs/port/PIPELINES.md, "The vertex pre-pass"), reason saying why for the
 * log ("for this start: the last start ended while the graphics driver
 * compiled its pipelines"); NULL clears it.  The host calls it after a
 * start that ended inside the pre-pass's compile and in a restart whose
 * step draws without it (window_host.c, startup_retry.h). */
void rd_set_vu_prepass_off(const char *reason);
/* Before rd_init, or after it and before the start-up set (the host reads
 * the device's name and driver from the device rd_init made): the run
 * never makes the sprite planes builds (the
 * screen-prim triangles' exact planes, rd_screen_planes.c), reason saying
 * why for the log ("the driver hung creating them on 2026-10-30, compiler
 * E031.41.03.47"); NULL or "" clears it.  Those triangles draw through the
 * plain sprite builds (the GPU's interpolation of their corners:
 * byte-identical on the dumps measured, a level apart possible at a
 * fractional scale; docs/port/WORK_BUFFERS.md "Without the sprite
 * planes"), which the start-up set holds instead, and none of the
 * family is created on first use.  The host calls it from [dev]
 * pipeline_skip (issue 161, port/platform/pipeline_skip.h);
 * ICO_RD_NO_SPRITE_PLANES=1 does the same for the tests. */
void rd_set_sprite_planes_off(const char *reason);
/* Whether the run draws without the sprite planes builds (after rd_init) */
bool rd_sprite_planes_off(void);

/* How the start-up pipeline set is made (rd_precreate_pipelines;
 * docs/port/PIPELINES.md "The start-up set"): the batch size, the worker
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
/* the careful values (batch 8, one worker thread, a 128 MB budget, the cache
 * after each batch) for a device of RhiLimits.carefulPipelines */
const RdPipelineSetPolicy *rd_pipeline_policy_careful(void);
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
/* The Vulkan pipeline cache file's name for this build's shaders,
 * "pipelines-<16 hex digits>.vkcache" (rd_cache_name.h: every module of
 * the embedded table by name, stage, length and XXH3-64, and
 * RD_PIPELINE_CACHE_FORMAT; not the build), into out (at least 35
 * bytes; 0, or -1 with out "" when it is shorter).  Needs no device: the
 * window names the file before rd_init.  The key is made once a run. */
int rd_pipeline_cache_file(char *out, size_t cap);
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
/* The progress function of a display change (a preset, resolution or
 * aspect change while the game runs): rd_begin_frame makes the pipelines
 * the new state reaches and the start-up set did not hold (a fractional
 * scale's no-planes twins among them) before the frame replays, in batches
 * on the driver's threads, calling fn(ctx, done, total) as
 * rd_set_pipeline_progress's function is called, on the thread that
 * applies the settings (the game's step, on the host stack).  The window
 * shows its "Preparing graphics" screen from it and takes the window's
 * events.  NULL (the default): the batches run without one.  Kept until
 * cleared. */
void rd_set_display_progress(RdPipelineProgressFn fn, void *ctx);
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
/* Whether the system reported low memory (issue 128; diag_host.h
 * ico_diag_low_memory_stop: Android's onTrimMemory "running critical"):
 * fn(why, size) returns nonzero with the level's name in why.  The
 * start-up set (rd_precreate_pipelines, under any policy) asks it after
 * each batch and stops there on a yes, the pipeline cache written as after
 * any batch and the rest made on first use (rd_pipelines_lazy: "no
 * start-up set (memory budget: the system reported low memory (running
 * critical) after 147 of 568 keys; the rest is made as the game needs
 * it)"); a batch in flight is not cut (the careful policy's batches are 8
 * pipelines on one thread).  NULL (the default): never asked. */
typedef int (*RdLowMemoryFn)(char *why, size_t size);
void rd_set_low_memory_fn(RdLowMemoryFn fn);
/* No start-up set (issues 93 and 107; docs/port/PIPELINES.md, "The start-up
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
/* Whether the run makes its pipelines on first use (rd_pipelines_lazy was
 * called: by the window, or by rd_precreate_pipelines when the careful
 * policy's budget stopped the set). */
bool rd_pipelines_lazy_on(void);
/* Once a frame (the window's pump), on the thread that draws: closes the
 * open lazy batch once no pipeline was made for 0.5 s or 2 s after its
 * first.  Nothing outside rd_pipelines_lazy's runs. */
void rd_pipelines_lazy_tick(void);
/* The background finisher (port/render/rd_pipe_finish.h; docs/port/
 * PIPELINES.md "The background finisher"): after rd_precreate_pipelines
 * stopped the set at the free-memory floor (the low-memory function said
 * yes for the floor, not for the system's signal), the window calls it so
 * the set's keys not made yet are compiled in the background instead of
 * on first use: one batch of RD_FINISH_BATCH at a time, on
 * RD_FINISH_THREADS pipeline threads at low priority
 * (rhi_create_pipelines_begin), begun and taken after a picture on the
 * thread that draws (the render thread), the memory function asked before
 * each batch (2: the system's signal, the finisher ends; 1: under the
 * floor, asked again RD_FINISH_FLOOR_MS later), no batch begun while a
 * late step or a slow picture holds it (rd_pipelines_background_hold).
 * A draw that needs a key the finisher has not made yet still makes it on
 * the spot.  Logs "pipelines: the stopped set resumes in the background (N
 * left: ...)", each batch as the set's ("compiled background VU batch
 * 290-321 of 1196 ..."), and its end.  Returns the keys left (0: nothing
 * to finish, or the backend has no background creation: then not started,
 * logged).  The game thread, before the render thread starts. */
typedef int (*RdBackgroundMemoryFn)(char *why, size_t size);
uint32_t rd_pipelines_background_start(RdBackgroundMemoryFn memory);
/* The skinned draws' bone-block check's own background compiles (its c2
 * variant's three pipelines and its box checks', rd_vu_skin_check.c; issue
 * 163) under issue 161's rule as the finisher above: memory asked before
 * each (2: none for the rest of the run; 1: not now), one at a time;
 * noBackground non-NULL (the start-up set stopped for memory on Qualcomm's
 * own driver: window_pipe_finish.c) none at all, its words in the one line
 * that says so.  Until a pipeline exists its part of the check is not drawn.
 * The window, before the render thread starts; without a call (the tests,
 * the tool) every compile may begin. */
void rd_vu_skin_background(RdBackgroundMemoryFn memory, const char *noBackground);
/* A late game step or a slow picture: no background batch begins for
 * RD_FINISH_HOLD_MS (a batch in flight goes on).  Any thread. */
void rd_pipelines_background_hold(void);
/* Whether the finisher has keys left or a batch in flight */
bool rd_pipelines_background_on(void);
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
 * LUT, through one way of reading the depth (inside the scene pass by
 * local read, in place, a copy, a buffer) after another in the platform's
 * order (local first on a tile-based GPU whose profile has it), stopping at
 * the first that
 * passes; the fogged pixels are read back and compared with the GS
 * arithmetic.  That first way is the one the fog takes from then on (an
 * ICO_RD_FOG_PATH or ICO_RD_DEPTH_COPY override is tried first and
 * stays); the ways after it are not tried, unless ICO_RD_FOG_PATH=test (a
 * developer switch) asks for every one.  One log line, "fog: depth path",
 * names it with every result ("not tried" for the rest).
 * The window calls it after rd_precreate_pipelines; after that it runs
 * again whenever SCENE is made at a new size.  Its first run runs
 * rd_date_selftest first, then rd__fx_local_selftest (the post sprites' local
 * read, rd_fx_plan.c).  Waits for the GPU.  Returns whether the path in
 * force passed. */
bool rd_fog_selftest(void);
/* DATE's self-test (rd_date_selftest.c), where the profile asks for DATE
 * through the stencil bit (RdProfile.dateStencil): a frame of its own on
 * SCENE draws a cell of destination alpha 0x80 and one of 0x00, then a
 * DATE sprite with DATM 1 and one with DATM 0 over both, through the
 * stencil path and through the R8 snapshot; four samples each are read back
 * and compared with the GS's outcome.  The stencil path stays only when its
 * four are right ("DATE: stencil path (self-test: stencil ok 4, snapshot ok
 * 4)"); else the profile's flag goes off (self-test) and the snapshot draws
 * DATE ("DATE: snapshot path (self-test: stencil fail 2 at sample 1: ...)").
 * ICO_RD_DATE_PATH=stencil keeps the stencil whatever it finds.  Nothing to
 * do (true) where the profile has the snapshot.  Waits for the GPU. */
bool rd_date_selftest(void);
/* What the renderer decided at its start, for the window's summary line
 * (diag_host.h ico_diag_set_summary): "blend <mode> (self-test: ...);
 * profile <device class>: fog local, DATE stencil, ... (rd_profile.c, the
 * flags as in force); index values dense (marks in the stream); fog <path> (self-test: ...);
 * VU shaders plain only (issue 107)" or "VU shaders plain and extended" (the
 * policy rd_init chose, rd__vu_policy_init), and ", pass split every N" when
 * on, then "; VU pre-pass on (self-test: ok)" or "off (self-test: <reason>)"
 * or "off <why> (self-test not run)" (rd_vuprepass_selftest), then ";
 * pipelines the start-up set" or "; pipelines on first use (no start-up
 * set)" (rd_pipelines_lazy). */
void rd_summary_text(char *out, size_t size);
/* The blend's self-test (rd_blend_selftest.c): a frame of its own
 * (never recorded, never presented) draws on SCENE at its real size and
 * scale, without MSAA, what the blend must get right: the forest's tint
 * sprite over eight cells of known colours (PCSX2's pixels), a textured
 * LERP quad whose clear texels must leave the colour behind them, two
 * overlapping LERP draws (the second must read the first's result) and a
 * glyph through the text sheet's shader.  It runs on every device: the
 * fixed-function blend first (the reference and the default; within 2
 * levels of the GS arithmetic, the two-pass fallback tried and kept when
 * the dual-source blend is not), then, only when ICO_RD_BLEND ([dev]
 * blend) opted into an exact mode the device has, that mode (within 1
 * level of the GS arithmetic and of the reference's glyph), kept whatever
 * its result.  One log line, "blend: <mode> (self-test: ...)", names the
 * blend in force with every result, and a failing entry's worst sample
 * gets a line of its own.  ICO_RD_BLEND_SABOTAGE=attachment,storage,fixed,
 * twopass (tests) makes those entries' read-back white.  The window calls
 * it before rd_precreate_pipelines (the set made is the blend's in force);
 * after that it runs again whenever SCENE is made at a new size.  Waits
 * for the GPU.  Returns whether the blend in force passed. */
bool rd_blend_selftest(void);
/* The VU vertex pre-pass's self-test (rd_vuprepass_selftest.c), after the
 * skinned draws' bone-block check's start-up probe and start line, which
 * run first whatever the pre-pass (rd_vu_skin_check.h rd__vu_skin_selftest;
 * issue 163): a frame of its own (never recorded, never presented) draws three meshes made for it
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
/* Whether the skinned draws' bone-block check's start-up probe
 * (rd_vu_skin_check.h; issue 163), run first by rd_vuprepass_selftest,
 * found the device failing in it (lost, or its picture not read back); why
 * gets the words.  The check is then off for the run; the window writes
 * [dev] vu_skin_check = false before a loss's restart or box, so no later
 * start runs the probe again (window_host.c).  The probe's notes go to the
 * activity function (rd_set_activity_fn): "creating pipeline rd vu skin
 * probe (...)" while its pipelines compile, "the VU skin check's start-up
 * probe (rd vu skin probe): ..." while it draws and reads back
 * (startup_retry.h startup_retry_note_has_skin_probe), so a crash, a freeze
 * or the system's end there names it at the next start. */
bool rd_vu_skin_probe_failed(char *why, size_t size);
/* The skin check's capture on failure (issue 123; rd_vu_skin_check.h): the
 * folder its sidecar is written to (the window's logs folder; NULL or ""
 * none: no sidecar, the frame dump is still asked), kept until changed.
 * rd_vu_skin_capture_due: true once a run, when the check has seen its
 * first failure (twins whose counts differ, or a wrong check) and written
 * its sidecar; path gets the sidecar's path ("" when none was written).
 * The window then takes a frame dump for the next present (window_host.c
 * frame_dump) and puts the sidecar in its zip.  Any thread (an atomic). */
void rd_set_vu_skin_capture_dir(const char *dir);
bool rd_vu_skin_capture_due(char *path, size_t size);

/* Issue 123's ladder (rd_vu_bones.c; docs/port/VU_SKIN_CHECK.md "The
 * ladder"): the skin check's evidence so far this run, which the window's
 * pump (window_bones.c) decides from (bones_ladder.h) before it writes
 * [dev] vu_bones = ring or turnip_debug = nobin for the next start.
 * ring: the skinned entries read their bones through the ring this run;
 * replays: replays whose twins were read; twins read, differ of them with
 * A and B differing (each a twin event); ringClear the evidence that the
 * ring read clears it (an event whose twin C, the ring read in the pass,
 * equalled B, or a wrong check with equal bytes whose column 3 was right),
 * ringFail the opposite; sceneOnly events whose probe of the same command
 * in the same replay drew right (the scene pass's execution), probeWrong
 * events whose probe was wrong; portRace wrong checks whose bytes differed
 * (H2: not the driver's read); addr the last wrong check's address; frame
 * the last event's; started: the renderer has chosen its modules
 * (rd__vu_policy_init ran: the driver and its environment are those of
 * this start); ringUsable: the ring builds could be put in force (the
 * program entries are the plain set, on Vulkan: rd__vu_policy_init).  Any
 * thread (each field an atomic). */
typedef struct RdVuBonesEvidence {
    bool started, ring, ringUsable;
    uint32_t replays, twins, differ;
    uint32_t ringClear, ringFail, sceneOnly, probeWrong, portRace;
    uint32_t addr, frame;
} RdVuBonesEvidence;

void rd_vu_bones_evidence(RdVuBonesEvidence *out);
/* The ladder's step 1 in the run (window_bones.c): mode "ring" or
 * "cbuffer" and its mark, taken by the next rd_init over ICO_RD_VU_BONES
 * and ICO_RD_VU_BONES_BY (the environment is not written while the
 * renderer's, the audio's and the driver's threads may read it).  NULL
 * mode: back to the environment.  The window's thread, before the
 * renderer is made again. */
void rd_set_vu_bones(const char *mode, const char *by);
/* The longest frame replay (CPU time of recording, pipeline creation,
 * submit and present) in ms since the last reset, and *count the replays
 * in that time; reset != 0 starts a new period.  For the window's 10 s
 * statistics line. */
double rd_replay_time_max(int reset, uint32_t *count);
void rd_set_host_call(RdHostCall call);

/* Frame rate Original (interpolation off): asked on the game thread before
 * rd_end_frame replays and presents a frame, with the frame's keep flag and
 * whether the render thread is still busy with the picture before (busy:
 * its job not ended, or its submit and present still on the submit thread;
 * rd_thread.h rd__thread_picture_busy; false when the picture would not be
 * handed over).  RD_GATE_PRESENT replays and presents it; RD_GATE_LATE and
 * RD_GATE_BUSY leave it unreplayed and unpresented (the last picture stays
 * on the screen), so a renderer slower than the game's step does not hold
 * the simulation below its rate (window_host.c's skip at Original,
 * pace_policy.h pace_original_gate).
 *
 * With a gate set and the render thread on, the picture is handed over as
 * the other frame rates' are (rd_thread.h rd__replay_frame_post): the
 * render thread walks it, the submit thread submits and presents it, and
 * the game's step waits for nothing but the hand-over (and, at its next
 * renderer call, the picture's reads: texLock).  A frame the gate presents
 * while the render thread is busy (a keep frame) waits for the picture
 * before to end, its present included, first: an acquire never waits
 * behind a present inside a picture's reads.  Without the render thread,
 * with it parked or in sync, with a capture or blended dump armed, under
 * rd_submit_hold, and under [dev] original_async = false
 * (ICO_RD_ORIGINAL_ASYNC=0) the frame is replayed inside the step, the
 * game thread waiting, as without a gate.
 *
 * NULL (the default, tests and tools) replays every frame inside the step:
 * the caller reads its results after rd_end_frame.  Nothing else of the
 * frame changes: a replayed frame draws the same picture either way. */
enum { RD_GATE_PRESENT = 0, RD_GATE_LATE = 1, RD_GATE_BUSY = 2 };

typedef int (*RdReplayGate)(int keep, bool busy);
void rd_set_replay_gate(RdReplayGate gate);
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
/* One log line now, "rd: frame state (<when>): ..." (the window after the
 * app came back, issue 129): the output's size and the one asked for, the
 * scene's scale, the aspect, the frame's width and exact width, cropped or
 * at its exact width, the reduction's border open or closed and the times
 * its outer columns were drawn as their neighbours.  Host loop; no-op
 * before rd_init. */
void rd_log_frame_state(const char *when);
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
/* rd_camera_cut_wall: the frame being recorded is not blended from the one
 * before because the eye's path between them crossed the stage's collision
 * (port/game/camera_wall.h; GsBase.c gsbHostCullFlip calls it at the flip
 * that closes the frame): RdCamera.cut RD_CAMERA_CUT_WALL, the snap
 * RD_SNAP_WALL.  A frame rd_camera_cut marks too stays a plain cut (1).
 * Outside an open frame it does nothing (the step belongs to no frame). */
void rd_camera_cut_wall(void);
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
 * the frame before (rd_internal.h RD_SNAP_*: 0 blended), whether it was
 * the tick's first present, and the alpha it was drawn at (rd_present's,
 * after its clamp to [0, 1]; 1 for rd_end_frame's present of the tick):
 * the replay tool's --interp with that alpha rebuilds the same picture
 * from the frame and the one before it.  False before the first present. */
typedef struct RdPresentInfo {
    uint32_t frame, keep, fade, snap, firstOfTick;
    float alpha;
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
 * read.  The fixed-function blend is the default on every device: rd_init
 * logs "blend: fixed-function (the default; ...)", and only
 * ICO_RD_BLEND=storage|attachment ([dev] blend; exact: the device's best)
 * opts into "exact (interlock)", "exact (rov)" or "exact (attachment
 * read)" where the device has it (one it lacks is refused and logged;
 * the renderer then makes nothing of the other mode).
 * rd_blend_selftest then checks the blend in force on the device.
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
 *                       survives rd_shutdown / rd_init.  fn runs on the
 *                       thread that records (with the window's render
 *                       thread, before the picture is handed to it;
 *                       rd_thread.h), or on the render thread only inside a
 *                       call the recording thread waits for (rd_end_frame's
 *                       present with interpolation off, the movie's)
 * rd_overlay_prims       valid only inside fn, on fn's thread (ignored
 *                       elsewhere): prims in
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
/* The same, the picture box-downscaled (area average, same aspect) to at
 * most maxW pixels wide first when the output is wider (0: as it is).  The
 * bug-report picture, which goes in a zip a phone shares. */
bool rd_capture_presented_max(const char *png, uint32_t maxW);
int rd_capture_result(char *path, uint32_t pathSize);

/* ------------------------------------------------- a save state's picture
 * The picture a save state keeps (port/game/state_host.c): the game as it
 * was the moment before the pause menu came up, taken without the menu.
 *
 * rd_capture_shot        the next present of a frame begun by the time of
 *                       the call (the frame open now, or an earlier one)
 *                       copies the output as rd_capture_presented does
 *                       (before the overlay: no popup, no HUD), cropped
 *                       to the presentation box (the picture without the
 *                       bars around it), and after the submit hands fn
 *                       that box as RGBA8 (pitch w * 4, alpha 255) in a
 *                       malloc block fn owns and frees.  A present of a
 *                       later frame drops it instead, and so do a failed
 *                       copy and the renderer's shutdown: fn(NULL, 0, 0,
 *                       user).  fn runs once per accepted call, on the
 *                       thread that presents, after the present.  A second
 *                       call before the present replaces the first
 *                       (whose fn is then told NULL).  Its own slot beside
 *                       rd_capture_presented's: one copy serves both when
 *                       both are armed.  false: no device or fn NULL
 * rd_capture_shot_cancel  forgets an armed one (fn is not called)
 * rd_present_box_size    the size of the last present's presentation box in
 *                       output pixels (the picture's own aspect); false
 *                       before the first present */
typedef void (*RdShotFn)(uint8_t *rgba, uint32_t w, uint32_t h, void *user);
bool rd_capture_shot(RdShotFn fn, void *user);
void rd_capture_shot_cancel(void);
/* The same picture as a deferred answer, for a caller on the game thread
 * while the window's render thread presents (rd_thread.h): no fn runs on
 * the presenting thread.  rd_capture_shot_arm arms it as rd_capture_shot
 * does (false: no device); rd_capture_shot_take hands the answer once it
 * came: 1 with *rgba the box (or NULL when the present dropped it, as fn's
 * NULL), the caller owning the block; 0 while none is waiting.  An answer
 * not taken is freed by the next arm and by rd_capture_shot_cancel */
bool rd_capture_shot_arm(void);
int rd_capture_shot_take(uint8_t **rgba, uint32_t *w, uint32_t *h);
bool rd_present_box_size(uint32_t *w, uint32_t *h);

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
 *                        texels and samples on the CPU (the tests' oracle).
 *                        At scale 1 the renderer computes these texels
 *                        itself when it uploads the texture (the changed
 *                        rectangle and the rim's reach round it, the whole
 *                        page after a style change) and the device holds
 *                        them as RGBA8 with an apron
 *                        (ICO_SHEET_BAKE_AX/AY) for the texels outside;
 *                        font_sheet_baked_ps (RD_FS_FONT_SHEET_BAKED)
 *                        blends four of them as font_sheet_ps blends the
 *                        texels it rebuilds, so the bytes are the same at
 *                        a fraction of the cost (issue 173).  A
 *                        scaled strip's page of scale 2 is baked the
 *                        same way (rd_tex.c rd__sheet_bake_scaled_rect:
 *                        its rim's magnification blends by odd 4ths,
 *                        exact in the shader's floats), both halves'
 *                        rows with the apron; 3 and 4 are drawn by
 *                        font_sheet_ps.  ICO_RD_SHEET_SHADER=1 ([dev]
 *                        sheet_shader) draws every scale through
 *                        font_sheet_ps instead (the A/B)
 * rd_set_texture_sheet_style  the style the next replays draw t with (the
 *                        language's rim level, without rasterising
 *                        again); ignored for any other texture.  The style
 *                        is read when a frame is replayed, not when its
 *                        draws are recorded (a baked sheet is baked again
 *                        whole at the next replay)
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
 *                        (the rim made from the coverage, baked).
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
 * write mask 0x3F) and writes the same colour at the resolve.  The North
 * American disc and the 2001 previews use 0x08 and 0xF8, the step 8: a
 * pixel ends at 8 n mod 256 = 8 (n mod 32), the count's low five bits.
 *
 * rd_shadow_count_target  the per-frame stand-in for FBP 0x142: an RGBA8
 *                       target of the scene size gsW x gsH, created by the
 *                       first call in the open frame (rd_temp_target); {0}
 *                       outside a frame
 * rd_shadow_reset        shadow_Reset's clear of FBP 0x142: the stencil of
 *                       the bound depth target to 0 (the colour target is
 *                       written whole by the resolve); the step 4
 * rd_shadow_reset_step   the same with the disc's step, 4 (0x04 / 0xFC) or
 *                       8 (0x08 / 0xF8; anything else is taken as 4): the
 *                       reset and the resolve after it carry it, so a dump
 *                       replays with it on any machine
 * rd_shadow_tris         shadow_RenderVolume's strips as triangles: v holds
 *                       3 x triCount XYZ2 (x, y, z; the rest unused), sign[t]
 *                       > 0 for a triangle whose last vertex had the colour
 *                       0x04 or 0x08 (flat shading), < 0 for 0xFC or
 *                       0xF8.  Depth-tested with the state's TEST against
 *                       the bound depth target, no colour or depth writes
 * rd_shadow_resolve      the count into the bound colour target: RGB =
 *                       step x n mod 256, A = 0x80 where that is not 0, else 0
 *                       (the TEXA expansion shadow_Draw's PSMCT24 read of
 *                       FBP 0x142 applies, TA0 0x80 AEM, baked so the chain
 *                       filters expanded texels as the GS does) */
RdTarget rd_shadow_count_target(uint32_t gsW, uint32_t gsH);
void rd_shadow_reset(void);
void rd_shadow_reset_step(uint32_t step);
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
/* rd_frame_projected_st  while (dsx, dsy) is not (0, 0), the screen prims
 *                    recorded under rd_frame_projected have STQs that follow
 *                    the screen: S / Q grows by dsx and T / Q by dsy per 12.4
 *                    unit of GS X and Y (puddle.c drawRipples: its STs into
 *                    the reflection block are its projected points scaled,
 *                    0.9 / width and 0.9 / height a pixel).  Where the
 *                    presenter re-projects a vertex through the blended
 *                    camera it moves the vertex's S / Q and T / Q by its own
 *                    screen move times these (rd_interp.c), as the block
 *                    moves; the STs are never made from the position (a
 *                    ripple's outer ring's are a rotated point's).  Recorded
 *                    in the draw (RDC_SCREEN b[5], f[0], f[1]); recording
 *                    only; (0, 0) by default */
void rd_frame_projected_st(float dsx, float dsy);

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
/* The tick before the last closed frame (the one the presenter blends
 * from), when it is closed and the tick right before; false (nothing
 * written) otherwise.  F12 zips it beside the frame, so `rd_replay_tool
 * --interp T` rebuilds the pictures between the two. */
bool rd_dump_prev_frame(const char *path);
/* The blended present's frame (F12, port/platform/window_host.c): the next
 * rd_present that draws a picture between two ticks (alpha below 1, not a
 * keep frame) writes the frame it drew (rd_interp.c's blended copy of the
 * tick, its commands, payload, scratch meshes and the ticks' cameras) to
 * path as an ordinary dump, after the present, so rd_replay_tool replays
 * it as drawn without --interp; a present at alpha 1 or of a keep frame,
 * the tick's own picture, writes nothing.  The next rd_present takes the
 * arm either way; NULL or "" disarms.  false: interpolation is off (no
 * rd_present draws), nothing armed.  An rd_present that draws nothing (a
 * movie, interpolation off, no closed frame) expires the arm, as does a
 * result poll with interpolation off or two frames closed since the arm.
 * rd_dump_blended_result  once the arm is taken: 1 written, 0 the present
 *                         was the tick's own picture, -1 the write failed,
 *                         with the present's frame and alpha; -3 the arm
 *                         expired, nothing written (the last closed frame,
 *                         alpha 1); -2 while armed or with nothing armed
 *                         (nothing reported) */
bool rd_dump_blended_arm(const char *path);
int rd_dump_blended_result(uint32_t *frame, float *alpha);
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
 * screenshots and image comparison.  dst NULL: *w and *h get the size a
 * dst must have room for (w * h * 4 bytes; the read may return a narrower
 * picture), nothing read. */
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
 * resolve of the multisampled SCENE, rd_replay.c msaaEnd), CRACK (the crack
 * filler above 1x before the fog: its copies of SCENE's colour and depth,
 * its count and its draw, rd_crack_fill.c; inside the fog's list). */
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
    /* the crack filler above 1x (rd_crack_fill.c rd__crack_fill) */
    RD_PERF_POST_CRACK,
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
 * filter), WAIT the display wait: no pass, the stretch from the end of a
 * presenting picture's scene list (its start stamp, the list's last
 * command) to the start of the present's own list, which alone waits for
 * the window's image (rd_submit.c; RdPerfRecord.gpuDisplayWaitMs; not
 * counted among the passes). */
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
    RD_PERF_PASS_WAIT,
    RD_PERF_PASS_KIND_COUNT
};

/* A RD_PERF_PASS_* in plain words ("uploads", "DATE snapshot", ...) */
const char *rd_perf_pass_kind_name(int kind);

struct RdPerfRecord;
/* The GPU time a picture that pacing reads (the present allowance's gate
 * and budget, the step rule, automatic resolution): the record's span
 * (gpuMs) less its display wait (gpuDisplayWaitMs), never below 0; 0 for a
 * record without GPU times.  The present's own list waits for the window's
 * image between the two lists' stamps (rd_submit.c), so under vsync on a
 * driver that waits whole batches the span now holds the wait, which fell
 * before the first stamp while a picture was one list; less the wait,
 * pacing reads the GPU's own work as before.  Where the stamps run ahead of
 * the wait (issue 179's Mali) the wait figure is about 0 and the input is
 * the span, as it was there.  The perf lines print the span and the wait
 * apart. */
double rd_perf_pace_gpu_ms(const struct RdPerfRecord *r);

/* RdPerfPass.target: a named target (RdTargetId: SCENE, WORK0, ...) or one
 * of these */
enum {
    RD_PERF_TARGET_BLOCK = RD_TARGET_COUNT, /* a render-to-texture block */
    RD_PERF_TARGET_TEMP,                    /* another temporary target */
    RD_PERF_TARGET_NONE,                    /* none (the uploads), or gone */
    RD_PERF_TARGET_WINDOW,                  /* the window (the present) */
    RD_PERF_TARGET_RECORD,                  /* the VU pre-pass's record */
    RD_PERF_TARGET_ROWBOX,                  /* the aura's row sums (rd_aura_box.c) */
    RD_PERF_TARGET_SHADOW_COUNT             /* the shadow count at its GS size (rd_shadow_one.c) */
};

/* RdPerfPass.target as text: "SCENE", "block", "window", "ROWBOX", ... */
const char *rd_perf_target_name(int code);

/* One piece of a timed replay's GPU work (rd_perf_passes_of): what it is,
 * the list walked and the effect in force when it began, its colour target
 * and size in texels (a copy's source; the present: the window), the draws
 * and pipeline binds the RHI recorded in it, its GPU time (from its
 * timestamp to the next one, so the barriers before it and any work the
 * replay did not name in between count for it; -1 when a stamp is missing)
 * and the CPU time the replay took to record it, in microseconds; where
 * the backend has a busy clock (rhi.h rhi_read_timestamps_busy: OpenGL),
 * busyValid and gpuBusyUs, the part of gpuUs the GPU ran after the work
 * was handed over (the rest it waited for the CPU). */
typedef struct RdPerfPass {
    uint8_t kind;   /* RD_PERF_PASS_* */
    uint8_t target; /* RdTargetId or RD_PERF_TARGET_* */
    int8_t list;    /* 0 .. RD_LIST_COUNT - 1; -1: the uploads, the pre-pass, the present */
    int8_t post;    /* RD_PERF_POST_*, -1 none */
    uint16_t w, h;
    uint32_t draws, pipelineBinds;
    float gpuUs, cpuUs;
    float gpuBusyUs;
    uint8_t busyValid;
    /* OpenGL: the bound groups' entries in it and those skipped as already
     * bound (RhiStats.entryBinds, entryBindsSkipped; 0 elsewhere) */
    uint32_t entryBinds, entrySkipped;
    /* a render pass's area (rd_pass_area.c, RhiRenderPassDesc.area): its
     * offset and size in texels; aw 0 the whole target */
    uint16_t ax, ay, aw, ah;
} RdPerfPass;

/* A piece's label as the window's summary writes it: the effect for a
 * render pass of one, "shadow" for the shadow count's (its target "count":
 * "shadow count 848x448"), "list N" for another render pass, else the kind
 * ("DATE snapshot"); the length written, as snprintf */
int rd_perf_pass_label(const RdPerfPass *p, char *buf, size_t n);

/* Why a code 36 batch's cut and kick draws were kept apart
 * (RdPerfRecord.vuFuseKept; rd_replay_vu.c vuFuseVerdict, in this order):
 * OFF the fuse off ([dev] vu_fuse = false, or the draw merge off); BUILD
 * the program's entry has no merge build (only those mark the kick's
 * pixels); PASSES a plan of two passes or more (the alpha test's split, no
 * dual-source blending); DST a destination read or DATE through the
 * stencil bit; BLEND the cut's blend form cannot give Cs per pixel (the
 * premultiplied adds and subtracts, the Ad forms, Cd*As + Cd); PLANES the
 * cut's pixel shader reads no mark (neither vu_ps / vu_texa_ps, which read
 * VuVSOut.r.w, nor a no-planes twin without a clip plane, which reads the
 * plane distance's sign: none expected); PLANS the two plans differ in more
 * than the blend */
enum {
    RD_VU_FUSE_OFF,
    RD_VU_FUSE_BUILD,
    RD_VU_FUSE_PASSES,
    RD_VU_FUSE_DST,
    RD_VU_FUSE_BLEND,
    RD_VU_FUSE_PLANES,
    RD_VU_FUSE_PLANS,
    RD_VU_FUSE_WHY_COUNT
};

/* Why a mesh has no facing data, so its draws have no side bands on a frame
 * wider than 4:3 (RdMeshRec.facingWhy; RdPerfRecord.vuBesideNoFacing, the 10
 * s line's "side bands:" clause; docs/port/VU1_PROGRAMS.md "The winding
 * mark"): NO_TABLE a prelit mesh built without the EE's table (rebuilt from
 * its packet where no strip records were written and none was kept for its
 * stream in this run: a save state loaded into a fresh run, a morphed
 * stream, the store emptied past 32 MiB, a vertex without a normal; an older
 * dump's prelit mesh), REPLACED a model pack's prelit replacement (no
 * normals, no table), COPY a copy whose index list differs from its
 * source's (rd__vu_mesh_copy_marks), OTHER no memory for the marks or a
 * stream the marks do not fit (a sheet) */
enum {
    RD_VU_NOFACE_NO_TABLE,
    RD_VU_NOFACE_REPLACED,
    RD_VU_NOFACE_COPY,
    RD_VU_NOFACE_OTHER,
    RD_VU_NOFACE_COUNT
};

/* Why a planned replay dropped no depth store (RdPerfRecord.storeWhy;
 * rd_store_plan.c rd__store_plan_end, docs/port/WORK_BUFFERS.md "The store
 * plan"), for the depth the line names (SCENE's where the replay has it):
 * NONE a store was dropped, or no pass had a depth; EFFECTS the present
 * reads SCENE's depth for an effects program ([video] effects_depth with
 * one loaded); KEEP a read before the clear or a readback kept it; STENCIL
 * a shadow window was open on it (rd__stencil_live); ANY a command the plan
 * does not know came later; LAST no pass with the depth bound began after
 * its last touch; FULL more depths than the plan holds */
enum {
    RD_STORE_WHY_NONE,
    RD_STORE_WHY_EFFECTS,
    RD_STORE_WHY_KEEP,
    RD_STORE_WHY_STENCIL,
    RD_STORE_WHY_ANY,
    RD_STORE_WHY_LAST,
    RD_STORE_WHY_FULL,
    RD_STORE_WHY_COUNT
};

/* RdPerfRecord.passAreaWhy: why a render pass on a target with its own
 * depth (SCENE) began whole, the first that holds (rd_pass_area.c
 * rd__pass_area_begin; docs/port/WORK_BUFFERS.md "The pass area"): SCAN no
 * scan entry for the command it began at (the switch off, a pass window,
 * another replay's scan, the target's size changed); CLEAR it takes a clear
 * as its load; MSAA in the MSAA epoch; STORAGE a storage pass; BEGIN begun
 * at a command no area starts at (a clear's pass, a fold's or a flush's
 * draw); VU, FOG, SHADOW, POST, SCREEN the first command of the pass's run
 * whose texels only the GPU knows: a VU draw (mesh, skinned, grid,
 * particles), the fog, a shadow command, a post sprite other than the
 * aura's, a flat draw under DATE or COLCLAMP 0's wrap; EMPTY nothing drawn;
 * LARGE the draws' tiles leave no tile column or row of the target out (a
 * full-frame sprite: the aura's paste, list 11's fades) */
enum {
    RD_AREA_WHY_SCAN,
    RD_AREA_WHY_CLEAR,
    RD_AREA_WHY_MSAA,
    RD_AREA_WHY_STORAGE,
    RD_AREA_WHY_BEGIN,
    RD_AREA_WHY_VU,
    RD_AREA_WHY_FOG,
    RD_AREA_WHY_SHADOW,
    RD_AREA_WHY_POST,
    RD_AREA_WHY_SCREEN,
    RD_AREA_WHY_EMPTY,
    RD_AREA_WHY_LARGE,
    RD_AREA_WHY_COUNT
};

/* RdPerfRecord.lastStoreWhy: why the pass holding a depth's last touch kept
 * its store (rd_pass_cut.c rd__last_store_drop, counted once a depth a
 * replay at rd__store_plan_end: the reason of the last pass begun with the
 * depth bound before its last touch was done; docs/port/WORK_BUFFERS.md
 * "Passes cut"): NOSCAN no scan this replay; SETTING MSAA, the exact blend,
 * GS state as shader data, the pass split or the tool's pass window; PASS
 * the pass was not begun by a draw on its colour and this depth (a post
 * sprite's, a clear's or a fold's pass); LOAD the depth not loaded (a
 * clear's load); SHADOW a shadow window open on the depth; UNKNOWN a
 * command the store plan does not know; and the first command of the pass
 * (its start, or between it and the last touch) the walk may end the pass
 * at: DATE a DATE draw; TARGET a draw sampling its own target, a depth
 * view, or a target not readable when the pass began; WRAP a flat draw
 * under COLCLAMP 0's wrap; POST a post sprite; FOG the fog other than at
 * the pass's start on the local read with no probe and no edge read due;
 * ELSEWHERE a draw on another colour or depth; OTHER a clear, a copy, an
 * exact blend, a shadow command or another action */
enum {
    RD_LAST_WHY_NOSCAN,
    RD_LAST_WHY_SETTING,
    RD_LAST_WHY_PASS,
    RD_LAST_WHY_LOAD,
    RD_LAST_WHY_SHADOW,
    RD_LAST_WHY_UNKNOWN,
    RD_LAST_WHY_DATE,
    RD_LAST_WHY_TARGET,
    RD_LAST_WHY_WRAP,
    RD_LAST_WHY_POST,
    RD_LAST_WHY_FOG,
    RD_LAST_WHY_ELSEWHERE,
    RD_LAST_WHY_OTHER,
    RD_LAST_WHY_COUNT
};

/* RdPerfRecord.pipeFamily's counts: one per pipeline key family
 * (rd_internal.h RdKeyFamily) */
#define RD_PERF_PIPE_FAMILIES 8
/* RdPerfRecord.cullHizOcc's counts: the occlusion test's occluder
 * candidates by reason (rd_vis.h RD_VIS_HZ_OCC_USED .. RD_VIS_HZ_OCC_NOTRI) */
#define RD_PERF_HIZ_OCC 9

enum { RD_PRESENT_ELSEWHERE = 0, RD_PRESENT_ON_SUBMIT = 1, RD_PRESENT_IN_STEP = 2 };

typedef struct RdPerfRecord {
    uint32_t replay; /* 1, 2, ... */
    uint32_t frame;  /* the replayed frame's number */
    uint8_t interpolated, keep, presented, gpuValid;
    double totalMs, interpMs, waitMs, acquireMs, uploadMs, walkMs, bindMs, submitMs, presentMs,
        readbackMs;
    double fenceWaitMs; /* the backend's blocked time on GPU completion (inside wait, readback) */
    /* presentMs timed apart (rhi.h rhi_present_split, Vulkan; presentSplit
     * 0 elsewhere, 1 with the driver's call, 2 the lock's wait only, no
     * present call made): the swapchain lock's wait, the driver's present call
     * (vkQueuePresentKHR), the surface poll after it; presentEndMs the
     * present's end on rd's monotonic ms clock (startMs's), 0 when nothing
     * was presented.  The window's lines on a present that blocked (issue
     * 123, window_stall.c) */
    uint8_t presentSplit;
    double presentLockMs, presentCallMs, presentPollMs, presentEndMs;
    /* where the present ran (RD_PRESENT_*): the submit thread (its job's
     * times taken, rd__perf_submit_times), inside the game's step (Frame
     * rate Original's replay with the game thread waiting, rd_end_frame),
     * or elsewhere (the render thread's own job, a present of the window's);
     * the window's line on a present that blocked names the first two */
    uint8_t presentThread;
    uint32_t buffersCreated, buffersDestroyed, texturesCreated, texturesDestroyed;
    uint32_t memoryAllocs, memoryFrees, bindGroups, pipelineBinds, bindGroupBinds;
    /* the binds and state sets the RHI's bind cache found already in force
     * and recorded nothing for (RhiStats.pipelineBindsSkipped,
     * bindGroupBindsSkipped, stateSetsSkipped) */
    uint32_t pipelineBindsSkipped, bindGroupBindsSkipped, stateSetsSkipped;
    uint32_t dynamicSets; /* dynamic depth and blend commands (RhiStats.dynamicSets) */
    /* of those, the blend equations for draws without blending and those
     * that replaced a dual-source one (RhiStats.blendEqOffSets,
     * dualLeftovers) */
    uint32_t blendEqOffSets, dualLeftovers;
    uint32_t draws, renderPasses, barriers, copies, fenceWaits, waitIdles, readbacks;
    /* the queue submits (RhiStats.submits; the submit thread's land in the
     * record closed after them): a presenting picture's two lists, its
     * scene's and its present's (rd_submit.c), on Vulkan an empty one
     * where no list signalled the present's semaphore */
    uint32_t submits;
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
    uint64_t uploadBytes;     /* everything written into the upload ring */
    uint64_t meshUploadBytes; /* of which mesh streams and indices */
    /* Direct3D 11: the bytes its submits wrote from the upload buffers' CPU
     * copies into the GPU's (RhiStats.uploadWriteBytes); 0 elsewhere */
    uint64_t backendWriteBytes;
    double gpuMs;                    /* first timestamp to last */
    double gpuUploadMs;              /* the upload copies at the head */
    double gpuListMs[RD_LIST_COUNT]; /* per command list (0 for a list not replayed) */
    double gpuPresentMs;             /* the present blits */
    /* the display wait: from the scene list's last stamp to the present's
     * own list's first (RD_PERF_PASS_WAIT), inside gpuMs.  Where the GPU
     * writes that stamp only once the list's wait for the window's image
     * is over (a whole-batch wait) it holds the wait; where the wait holds
     * colour output alone (a Mali in the v0.10.4 logs) the stamp comes
     * early and the wait shows in gpuPresentMs instead */
    double gpuDisplayWaitMs;
    /* per picture effect (RD_PERF_POST_*), inside the times above */
    double gpuPostMs[RD_PERF_POST_COUNT];
    double startMs;      /* the replay's start (rd's monotonic ms clock) */
    float alpha;         /* rd_present's alpha; -1 for a replay that is not one */
    uint8_t firstOfTick; /* the first present of its frame */
    /* the replay changed effect more often than there were timestamps:
       the later changes were not timed apart (their time stayed with the
       effect, or the scene, before them) */
    uint8_t gpuPostPartial;
    /* MSAA: where the replay's epoch closed (rd_replay.c msaaEnd): 0 not
     * closed (no epoch), else the list + 1, RD_LIST_COUNT + 1 the replay's
     * end */
    uint8_t msaaCloseAt;
    uint8_t reshown;  /* a between-tick present shown again without a replay (rd_reshow.c) */
    uint8_t glowHeld; /* a between-tick present that pasted the tick's glow (rd_glow_hold.c) */
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
     * rd_fx_plan.c rd__sprite_above1x, rd_replay.c blurAbove1xSteps,
     * doFog), g_rd.above1xDraws counted per replay; each planned pass of a
     * draw counts (fillDrawCB runs per pass: the AFAIL split's two, the
     * two-pass blend's two), so a draw can count twice; 0 at scale 1.  A
     * work buffer drawn onto the frame is never among them (rd_internal.h
     * rd__work_class), nor a shadow composite unless [dev] shadow_bspline
     * (RdProfile.shadowBspline) */
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
    /* The tiler passes (docs/port/RENDER.md, "The replay"): dateStencilDraws
     * the DATE draw passes tested through the stencil bit instead of the R8
     * snapshot (each planned pass, as fillDrawCB counts), dateStencilTakes
     * the setups that wrote the bit (two draws and two barriers each,
     * rd_replay_tiler.c rd__replay_date_take); fogLocalDraws the fog draws that read
     * the depth inside the scene pass (RD_FOG_LOCAL); blurFolds the
     * staticBlur pairs drawn as one pass (fx_blur2_ps, rd_replay.c
     * rd__replay_blur_fold) */
    uint32_t dateStencilDraws, dateStencilTakes, fogLocalDraws, blurFolds;
    /* GS state as shader data (rd_uber.c): the DATE draws whose shader read
     * the alpha from the colour attachment (rd_replay.c prepareDrawEx), no
     * snapshot or stencil bit taken */
    uint32_t dateDstDraws;
    /* The pixel diet (docs/port/RENDER.md, "The device profile"):
     * alphaOnePass the draws the alpha/blend planner drew in one pass where
     * their plan had the alpha test's two, alphaOpaque those it drew
     * without the test and the blending (rd_alpha_plan.c); fxConstant,
     * fxAdditive and fxLocal the staticBlur sprites the frame-size plan drew
     * as a constant fold, through the fixed-function premultiplied add, and
     * reading their destination by local read (rd_fx_plan.c); vuNoPlanes
     * the VU draw passes through vu_np_ps or vu_texa_np_ps (rd_pixel_plan.c);
     * spriteNoPlanes the screen-prim triangle commands drawn through the
     * plain sprite builds because the sprite planes builds are off for the
     * run (rd_set_sprite_planes_off, issue 161; rd_replay.c doScreen);
     * fxLocalDate those of fxLocal under DATE (the shader's test against
     * the alpha it read, no snapshot, no stencil bit) */
    uint32_t alphaOnePass, alphaOpaque, fxConstant, fxAdditive, fxLocal, vuNoPlanes;
    uint32_t spriteNoPlanes, fxLocalDate;
    /* Motion blur: Smooth (rd_blur_smooth.c): blurSmoothPasses the
     * accumulator's passes this replay (the resolve and the accumulate, two
     * a present; one on a restart) */
    uint32_t blurSmoothPasses;
    /* The crack filler above 1x (rd_crack_fill.c): crackFillPasses its
     * fills this replay (0 or 1, before the fog; on the local read drawn in
     * the pass the fog then keeps, not a pass of its own), crackCountPasses the
     * passes that counted what it filled (the tests and the replay tool's
     * --stats only; the count is read back later, rd__crack_fill_counts) */
    uint32_t crackFillPasses, crackCountPasses;
    /* The shadow count's resolves this replay (rd_replay.c doShadowResolve):
     * shadowOne in one draw reading the stencil (shadowCountOne),
     * shadowSeven through the seven stencil-tested draws (and the box
     * reduction above 1x) */
    uint32_t shadowOne, shadowSeven;
    /* of shadowOne, the draws that read the stencil four texels a gather
     * (the rest a texel a load: g_rd.shadowLoad, rd_shadow_selftest.c) */
    uint32_t shadowGathered;
    /* The one draw's pass (rd_shadow_one.c): its colour, written whole,
     * shadowOneW x shadowOneH (no depth attachment), and the stencil it
     * reads through its view, shadowReadW x shadowReadH texels (the last
     * one draw of the replay).  The count target's colour passes the store
     * plan left neither loaded nor stored between the reset and the
     * resolve (shadowColorDropped, also in storeColorDropped; the reset's
     * own pass and the volumes' passes), that colour shadowColorW x
     * shadowColorH texels (rd_store_plan.c rd__store_plan_count_color) */
    uint16_t shadowOneW, shadowOneH, shadowReadW, shadowReadH, shadowColorW, shadowColorH;
    uint32_t shadowColorDropped;
    /* A cleared source read as its colour (rd_const_read.c): constReads the
     * staticBlur sprites drawn untextured in the colour of a target that
     * held only its clear, constKept those reading another target that
     * were not (the source drawn since its clear, or not cleared in this
     * replay) */
    uint32_t constReads, constKept;
    /* The fades fold (rd_fades_fold.c): list 11's last full-frame lerp
     * sprites the reduction applied instead of drawing them into SCENE */
    uint32_t fadesFolded;
    /* The glow's mask folded into its reduction (rd_glow_fold.c): glowFolded
     * 1 when this replay's reduction computed the mask in its box (WORK2
     * neither drawn nor read), glowKept 1 when a glow frame drew the mask
     * into WORK2 instead, glowWhy why (RD_GLOW_FOLD_WHY_*); glowFoldRead 1
     * when the fold read WORK2 as the fill and the sun's fans left it (only
     * the mask's pass gone) */
    uint32_t glowFolded, glowKept, glowWhy, glowFoldRead;
    /* The aura's read of SCENE into FEED128 in two passes (rd_aura_box.c):
     * the reads drawn so this replay (each one pass more, ROWBOX's: 0 or 1
     * a replay; none while the game is paused) */
    uint32_t auraBoxDraws;
    /* The pass area (rd_pass_area.c): the render passes begun this replay
     * with an area smaller than their target (only the tiles the draws
     * touch loaded and stored), the 32 x 32 tiles their areas hold and the
     * tiles their whole targets hold (passAreaTiles / passAreaTargetTiles:
     * the share of those passes' tiles a tile-based GPU loads and stores),
     * and the passes ended early at the end of their area's commands (the
     * walk would have gone on in the same pass: 0 in a usual replay) */
    uint32_t passAreas, passAreaTiles, passAreaTargetTiles, passAreaEnds;
    /* the passes on a target with its own depth begun whole, by why
     * (RD_AREA_WHY_*: the 10 s line's "kept whole" clause) */
    uint16_t passAreaWhy[RD_AREA_WHY_COUNT];
    /* Passes cut (rd_pass_cut.c; docs/port/WORK_BUFFERS.md "Passes cut"):
     * clearsCarried the pending clears carried past a source's barrier
     * that the next pass on their target took as its load op (a pass
     * fewer each; the reduction's DISPLAY clear in a play replay),
     * clearsSkipped the clears not recorded because nothing read their
     * target before it was cleared again (AURA_WORK's under the constant
     * read), storeLastDropped the depth stores dropped at the pass that
     * holds the depth's last touch (counted in storeDepthDropped too) */
    uint32_t clearsCarried, clearsSkipped, storeLastDropped;
    /* The shadow volumes in the scene's pass (rd_shadow_merge.c;
     * docs/port/WORK_BUFFERS.md "The shadow volumes in the scene's pass"):
     * shadowMerged the shadow resets whose volumes were drawn in the
     * scene's open pass (a pass fewer each, and the scene's depth not
     * stored and loaded again), shadowMergeArmed the passes begun with
     * their stencil stored for a later reset (those not taken stored it
     * for nothing) */
    uint32_t shadowMerged, shadowMergeArmed;
    /* the shadow count targets whose new-target clear was not drawn
     * (rd_shadow_merge.c rd__shadow_count_clear: nothing reads them before
     * their resolve; 9.7 MB written fewer each at the phone's size) */
    uint32_t shadowClearSkipped;
    /* the depths whose last touch's pass kept its store, by why
     * (RD_LAST_WHY_*: the "last store kept" clause) */
    uint16_t lastStoreWhy[RD_LAST_WHY_COUNT];
    /* The VU vertex and triangle pre-passes (rd_replay_vu.c rd__vu_prepass;
     * docs/port/PIPELINES.md, "The vertex and triangle pre-passes"):
     * vuPrePasses their render passes (0, 1: the vertices and sprites, or 2:
     * and the triangles), vuPreVertices the vertices recorded, vuPreSprites
     * the sprites, vuTriSlots the triangles' record slots (two a kick, three
     * under the tick cameras), vuPreDraws the draws of both passes (a vertex
     * draw a mesh or grid command, two a particle command, and the record
     * stages' three, five with the tick cameras, a mesh or grid command),
     * vuPreFallbacks the VU draws drawn the old way while they are on (a
     * command past the record, a pipeline missing; 0 in a usual replay),
     * gpuPrepassMs their GPU time (the passes and the copies into the record
     * buffer, between the uploads and list 0; inside gpuMs, not in
     * gpuUploadMs) */
    uint32_t vuPrePasses, vuPreVertices, vuPreDraws, vuPreFallbacks;
    uint32_t vuPreSprites, vuTriSlots;
    /* the VU draws drawn under the ticks' cameras (rd_replay_vu.c doVuDraw:
     * a frame blended between two ticks, a draw marked RD_VU_TICK_CAMERAS,
     * vuSource), once a draw whether the pre-pass recorded it or not */
    uint32_t vuTickDraws;
    /* the VU draws issued (rd_replay_vu.c vuDraw, before any merge: a cut
     * and a kick draw count one each) and the triangles in them (their
     * index lists in threes; a particle's six indices a quad): the work the
     * vertex shader does where the pre-pass is off (9 invocations a
     * triangle; window_scene.h) */
    uint32_t vuDraws, vuTris;
    /* The draw-merge count (rd_replay_vu.c vuMergeNote; counting only, no
     * draw or record changes): per list the VU draws that could join the
     * previous draw's run (vuMergeDraws) and the runs started (vuMergeRuns:
     * a draw that cannot join, or cannot be merged at all, is a run of
     * one); of the runs begun after a draw in the same pass, vuMergeBlocked
     * the first differing key: 0 pipeline, 1 texture group (or stream
     * group), 2 DrawCB bytes, 3 split or DATE stencil or storage pass or
     * skinned or something else drawn between, 4 scissor, FrameCB or
     * target.  prepassBatches the distinct (stage key, FrameCB, stream
     * buffer, bones class) of the pre-pass's draws (vuPreDraws of them) */
    uint32_t vuMergeDraws[RD_LIST_COUNT], vuMergeRuns[RD_LIST_COUNT], vuMergeBlocked[5];
    /* The draw merge as drawn (rd_replay_vu.c vuRunFlush; g_rd.vuMerge):
     * vuMergeIn the VU draws drawn inside a merged GPU draw (a run of two
     * or more), vuMergeGpu those GPU draws, vuMergeAlone the merge builds'
     * draws drawn one a GPU draw (a run of one, or a draw that cannot merge:
     * two passes, DATE through the stencil, a destination read, code 36's
     * pairs); 0 with the merge off */
    uint32_t vuMergeIn, vuMergeGpu, vuMergeAlone;
    /* The fused scissor pairs (rd_replay_vu.c doVuDraw, vuFuseVerdict;
     * g_rd.vuFuse): of code 36's batches (a cut draw and a kick draw each),
     * vuFusePairs those whose kick took its cut's pipeline (PRIM.ABE on),
     * its pixels written unblended (ICO_VU_FUSED), so the draw merge may
     * join them; vuFuseSame those whose two draws had one plan already
     * (the state's ABE on, or the alpha plan's blending off); vuFuseKept
     * the others, by reason (RD_VU_FUSE_*) */
    uint32_t vuFusePairs, vuFuseSame, vuFuseKept[RD_VU_FUSE_WHY_COUNT];
    /* The VU draws that kept the PS2's drops because their mesh has no
     * facing data, by why (RD_VU_NOFACE_*; rd_replay_vu.c vuDropMode):
     * under the camera rule every such draw, under region_rule = ps2 those
     * on a frame wider than 4:3 with the side bands not turned off */
    uint32_t vuBesideNoFacing[RD_VU_NOFACE_COUNT];
    /* The VU draws with side bands that carry the card test's verdicts (a
     * strip of the draw a card at either tick; rd_replay_vu.c vuSource):
     * their cards' triangles keep the drop beside the picture as inside it
     * (vu_common.hlsli vu_rule_beside; issue 189's mist quads) */
    uint32_t vuBesideCards;
    /* The VU draws under the camera rule (ICO_VU_CAMERA; rd_replay_vu.c
     * vuSource; [dev] region_rule = camera): their triangles the PS2's rule
     * drops are drawn clipped when they face the eye, their sprites outside
     * the VU's window drawn whole */
    uint32_t vuCameraDraws;
    /* The visibility test (rd_vis.c rd__vis_frame; [dev] cull): the VU draw
     * commands of the replayed lists (cullDraws), those turned into NOPs
     * (cullSkipped: by the frustum test, cullFrustum, or by occlusion,
     * cullOccluded, under [dev] cull = full), and the test's time on the CPU
     * (cullMs; cullHizMs of it the occlusion test's) */
    uint32_t cullDraws, cullSkipped, cullFrustum, cullOccluded;
    double cullMs, cullHizMs;
    /* The occlusion test's cost by part (rd_vis.h RdVisCounts' hiz
     * fields): cullHizMs is the collect (cullHizCollectMs: of it the alpha
     * proofs, cullHizAlphaMs, and the vertex pass, cullHizVertMs), the
     * occludees' test (cullHizTestMs) and the rasterisation
     * (cullHizRasterMs); the decodes settled before the collect
     * (cullHizSettleMs) are outside it.  The vertices projected
     * (cullHizVerts), the triangles read and kept as occluders before the
     * budget (cullHizTrisRead, cullHizTrisKept), the cells covered, the
     * draws tested, the triangles past the budget, the occluder draws that
     * took their triangles from the collect cache of those that reached
     * the vertex pass (cullHizCacheHits, cullHizCacheDraws), and the
     * occluder candidates by reason (cullHizOcc: used, then refused by
     * state, chain plane, target, bound, program, alpha, small box, no
     * triangle: rd_vis.h RD_VIS_HZ_OCC_USED .. RD_VIS_HZ_OCC_NOTRI) */
    double cullHizSettleMs, cullHizCollectMs, cullHizAlphaMs, cullHizVertMs, cullHizRasterMs;
    double cullHizTestMs;
    uint32_t cullHizVerts, cullHizTrisRead, cullHizTrisKept, cullHizTested, cullHizOverBudget;
    uint32_t cullHizCacheHits, cullHizCacheDraws;
    uint64_t cullHizCells;
    uint32_t cullHizOcc[RD_PERF_HIZ_OCC];
    uint32_t prepassBatches;
    double gpuPrepassMs;
    /* The store plan (rd_replay.c storePlanScan; RdProfile.storePlan): of
     * the render passes the walk began (storePasses; not the passes begun
     * outside it, docs/port/WORK_BUFFERS.md "The store plan"), those with a
     * depth outside the MSAA epoch (storeDepthPasses), the depth stores
     * dropped (storeDepthDropped: the depth and its stencil neither loaded
     * nor stored) and the colour stores dropped (storeColorDropped: the
     * shadow count's colour while it is made, rd__store_plan_count_color;
     * every other colour is kept); storePlanMode 0 every store kept ([dev]
     * store_plan = false, or a pass window), 1 the plan, 2 every depth
     * store dropped (ICO_RD_STORE_PLAN=all, tests); storeKept the
     * replay's depths whose stores the plan keeps from an earlier replay on
     * (a read before the clear, a readback), and the first such depth's
     * target (storeKeptTarget, RD_PERF_TARGET_* codes) and the frame that
     * kept it (storeKeptFrame), where this replay has that depth;
     * storeWhy, in a planned replay that dropped no depth store with a
     * depth bound, why (RD_STORE_WHY_*; NONE otherwise) */
    uint32_t storePasses, storeDepthPasses, storeDepthDropped, storeColorDropped;
    uint8_t storePlanMode;
    uint8_t storeKept, storeKeptTarget, storeWhy;
    uint32_t storeKeptFrame;
    /* GS state as shader data (rd_uber.c): uber 1 while it is in force;
     * pipeFamily the pipeline cache by family at the replay's end, in
     * rd_internal.h RdKeyFamily's order (2D, VU, VU point, shadow, fog,
     * CRT, fx, DATE): the 10 s summary's "pipelines by family"
     * (rd_perf_backend_text) */
    uint8_t uber;
    uint16_t pipeFamily[RD_PERF_PIPE_FAMILIES];
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
    /* The busy clock's share of gpuMs (rhi.h rhi_read_timestamps_busy,
     * OpenGL, in the replays timed pass by pass): gpuBusyValid, and
     * gpuBusyMs the GPU time over the same stamps less the GPU's waits for
     * the CPU to hand the work over (a lower bound of the GPU's own time;
     * gpuMs counts both) */
    uint8_t gpuBusyValid;
    double gpuBusyMs;
    /* gpuGapValid: a timed replay came before this one (within 60 s,
     * rd_perf_gap.h); gpuGapNs this replay's first stamp less that one's
     * last (the same GPU clock): over 0 the GPU idle between them (waiting
     * for this replay's submit), under 0 this replay's first stamp written
     * before the one before ended, so gpuMs counts that end too (the
     * window's 10 s 'GPU between replays' line, pace_policy.h PaceGpuGap;
     * the step rule's GPU time a picture is the median of
     * rd_perf_pace_gpu_ms, gpuMs less the display wait);
     * gpuGapSkipped the replays without timestamps between (their GPU work
     * is inside the gap then) */
    uint8_t gpuGapValid;
    int64_t gpuGapNs;
    uint32_t gpuGapSkipped;
} RdPerfRecord;

/* The oldest finished record not yet popped (a queue of 64; the oldest are
 * dropped when nobody pops); false when there is none. */
bool rd_perf_pop(RdPerfRecord *out);
/* "; Direct3D 11 submits wrote N KB to the GPU": the mean backendWriteBytes
 * a record of those rd_perf_pop handed out since the last call, "" when
 * they wrote none (every other backend); the count starts again.  The
 * window's uploaded KB line ends with it (docs/port/DISPLAY.md). */
const char *rd_perf_backend_text(void);
/* "N passes kept whole (A <why>, B <why>, ...)" from per-reason counts
 * over `replays` replays (the mean a replay; 1 the replay tool's one):
 * which 0 the pass area's (RD_AREA_WHY_*, RD_AREA_WHY_COUNT counts), 1 the
 * last store's (RD_LAST_WHY_*: "N last stores kept (...)"); the reasons
 * that hold none are left out, "" with none at all.  Into buf (size). */
void rd_perf_why_text(char *buf, size_t size, int which, const uint64_t *counts, double replays);

/* The visibility test's sums over records (rd_perf_backend_text's cull
 * part): rd_perf_cull_add adds record r's cull* fields; rd_perf_cull_text
 * writes the part, the mean a replay over `replays`, into buf (size), ""
 * when no draw was walked: "; cull: N of M scene draws skipped a replay (F
 * frustum, O occluded), T ms (occlusion H) ([dev] cull)", and when the
 * occlusion test ran "; occlusion: collect C ms (settle S, alpha A,
 * vertices V ms for Nv vertices, Nt triangles kept of Na; cache Hc of Dc
 * draws), raster R ms (Nc cells), test X ms (Nd draws tested); occluders
 * used U, refused: state/chain/target/bound/program/alpha/small/no-tri
 * a/b/c/d/e/f/g/h; past the budget B" (docs/port/RENDER.md, "Culling") */
typedef struct RdPerfCullSum {
    uint64_t draws, skipped, frustum, occluded;
    double ms, hizMs, settleMs, collectMs, alphaMs, vertMs, rasterMs, testMs;
    uint64_t verts, trisRead, trisKept, cells, tested, overBudget, cacheHits, cacheDraws;
    uint64_t occ[RD_PERF_HIZ_OCC];
} RdPerfCullSum;

void rd_perf_cull_add(RdPerfCullSum *s, const RdPerfRecord *r);
void rd_perf_cull_text(char *buf, size_t size, const RdPerfCullSum *s, double replays);

/* RdPerfRecord.glowWhy (rd_glow_fold.c): why a glow frame drew its mask into
 * WORK2 (docs/port/RENDER_POST.md, "The glow's mask in its reduction"):
 * FANS not given (the sun's fans between the fill and the mask fold the
 * mask alone, RdPerfRecord.glowFoldRead),
 * LAMPS a draw into WORK2 between the mask and the reduction (the lamps'
 * meshes), SCENE a draw into SCENE between them, OTHER WORK2 drawn or read
 * elsewhere in the frame, FORM the fill,
 * the mask or the reduction not in the fold's form, MSAA the MSAA epoch,
 * PLAN the fx plan off or no fx_frame_ps, UNDONE a reduction that could not
 * take a planned fold (the fill and the mask drawn after all).
 * rd_glow_fold_why_text gives the words the 10 s line and --stats print. */
enum {
    RD_GLOW_FOLD_WHY_NONE,
    RD_GLOW_FOLD_WHY_FANS,
    RD_GLOW_FOLD_WHY_LAMPS,
    RD_GLOW_FOLD_WHY_OTHER,
    RD_GLOW_FOLD_WHY_FORM,
    RD_GLOW_FOLD_WHY_MSAA,
    RD_GLOW_FOLD_WHY_PLAN,
    RD_GLOW_FOLD_WHY_UNDONE,
    RD_GLOW_FOLD_WHY_SCENE,
    RD_GLOW_FOLD_WHY_COUNT
};

const char *rd_glow_fold_why_text(uint32_t why);

/* The CPU time of the replay finished last (its RdPerfRecord.totalMs,
 * popped or not; a picture handed over is still running when the step
 * ends, so the window's pacing reads rd_frame_post_last_take instead). */
double rd_last_replay_ms(void);

/* The game thread's own cost of closing a frame since the last take:
 * rd_end_frame's walk of the frame for its end state and the rest of its
 * bookkeeping, from after its wait for a picture's reads (the window's read
 * waits) to before the replay it makes with interpolation off (that is the
 * picture's); the record of the frame's draws is inside the game's step.
 * The closes counted, their mean and the longest in ms; the sums start
 * again.  The window's 10 s line gives it (render_thread.c). */
void rd_frame_close_take(unsigned *closes, double *meanMs, double *maxMs);

/* Frame rate Original's pictures as the game thread made them in
 * rd_end_frame (rd_set_replay_gate), since the last take: posts, the
 * pictures handed over to the render thread (postMs their hand-over's time
 * on the game thread in all, postMaxMs the longest); postWaits of them,
 * those that waited for the picture before to end first (a keep frame
 * while the render thread was busy, or no gate), postWaitMs that wait in
 * all; inlines, the pictures replayed and presented inside the step, the
 * game thread waiting (no render thread, parked, sync, a capture, [dev]
 * original_async = false), inlineMs their time in all; busySkips, the
 * frames the gate left out because the render thread was busy (RD_GATE_BUSY),
 * lateSkips those it left out late.  The sums start again.  The window's 10
 * s game-thread line (render_thread.c, rd_frame_post_text). */
typedef struct RdFramePost {
    unsigned posts, postWaits, inlines, busySkips, lateSkips;
    double postMs, postMaxMs, postWaitMs, inlineMs;
} RdFramePost;

void rd_frame_post_take(RdFramePost *out);
/* The clause of the 10 s line from p: "; its picture handed over in 0.6 ms
 * (longest 14.2 ms; 3 waited for the picture before, 31.0 ms in all; 12
 * skipped: render thread busy); presents waited inside the step: 0 (0.0
 * ms)"; empty when p counts nothing (the other frame rates); out always
 * terminated */
void rd_frame_post_text(const RdFramePost *p, char *out, size_t cap);

/* The last Original picture rd_end_frame made since the last take (false:
 * none): how (RD_POST_HANDED, RD_POST_INLINE, RD_POST_SKIP_LATE,
 * RD_POST_SKIP_BUSY), its frame, ms the game thread spent on it (the
 * hand-over, its wait included, or the replay inside the step) and waitMs
 * the hand-over's wait for the picture before.  The window takes it every
 * pace: the slow-step line and the first present's pause on a new
 * swapchain (window_present.c). */
enum { RD_POST_HANDED = 1, RD_POST_INLINE, RD_POST_SKIP_LATE, RD_POST_SKIP_BUSY };

typedef struct RdFramePostLast {
    uint32_t frame;
    int how;
    double ms, waitMs;
} RdFramePostLast;

bool rd_frame_post_last_take(RdFramePostLast *out);
/* The slow-step line's words for p: ", the picture handed over 0.5 ms (0.0
 * ms waiting for the one before)", ", the picture replayed inside the step
 * 165.6 ms", ", the picture not presented (the render thread busy)" or ",
 * the picture not presented (the step late)"; out always terminated */
void rd_frame_post_last_text(const RdFramePostLast *p, char *out, size_t cap);

/* The pieces of timed replay `replay` (RdPerfRecord.passTimed): their
 * count, *out pointing at them until the next replay; 0 when that replay was
 * not timed or its pieces were dropped (the queue holds 8 replays).  Pieces
 * of earlier replays still queued are dropped.  Call it after rd_perf_pop
 * handed out the record. */
uint32_t rd_perf_passes_of(uint32_t replay, const RdPerfPass **out);

/* [dev] perf_passes (ICO_RD_PERF_PASSES=1): every replay is timed piece by
 * piece (the window writes ico-pc-passes.csv) */
bool rd_perf_passes_mode(void);

/* Issue 123: every replay timed piece by piece from now on (on: the
 * window's first present that blocked, so the next stall's replay is
 * timed; ~47 more bottom-of-pipe stamps a replay, no CSV), or back to one
 * every 250 ms (off).  True when that changed something: timestamps exist,
 * the backend is one rd_perf_passes_arm_backend allows (on only) and it
 * was not so already (never on iOS, where pass timing is off).  Any
 * thread; the render thread reads it at its next replay. */
bool rd_perf_passes_arm(bool on);
/* Whether a present that blocked arms rd_perf_passes_arm: only when its
 * record carries the present split (presentSplit, the Vulkan backend's)
 * on Vulkan (MoltenVK included; kind an rhi.h RhiBackendKind); never on
 * OpenGL, where each armed stamp's issue-time read may hold the CPU, nor
 * on Direct3D (pure) */
bool rd_perf_passes_arm_backend(int kind, bool presentSplit);
/* "every replay timed pass by pass from here on Vulkan (a present blocked
 * 359 ms at replay 2941): the next stalled replay's passes are compared
 * with the block's" (the window's line, once; backend: the armed
 * backend's name, rhi_backend_kind_name) */
int rd_perf_passes_arm_text(uint32_t replay, double presentMs, const char *backend, char *buf,
                            size_t n);

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
    double busyMs;  /* of gpuMs, the GPU busy time (RdPerfPass.gpuBusyUs) */
    uint64_t busyN; /* the pieces that had one */
} RdPerfPassAggEntry;

typedef struct RdPerfPassAgg {
    uint32_t replays;      /* timed replays added */
    uint32_t count;        /* entries */
    uint64_t passes;       /* their pieces */
    uint64_t small;        /* of them, under 0.05 ms on the GPU */
    uint64_t dropped;      /* pieces whose entry found no room (in passes only) */
    double gpuMs, smallMs; /* the pieces' GPU time, and the small ones' */
    /* the pieces with a busy time (OpenGL): their busy time and their GPU
       time (the span between the same stamps) */
    double busyMs, busySpanMs;
    uint64_t entryBinds, entrySkipped; /* the pieces' (OpenGL) */
    RdPerfPassAggEntry e[RD_PERF_PASS_AGG_MAX];
} RdPerfPassAgg;

void rd_perf_pass_agg_reset(RdPerfPassAgg *a);
void rd_perf_pass_agg_add(RdPerfPassAgg *a, const RdPerfPass *p, uint32_t n);
/* "list 0 SCENE 848x448 420 draws 4.12 ms, glow WORK1 256x128 10.0 passes
 * 10 draws 1.21 ms, ... (5 of 47.0 passes a replay, 16 timed replays;
 * passes under 0.05 ms: 31.0 a replay, 0.62 ms)": the top entries by GPU
 * time, per timed replay ("N passes" only where an entry is not one pass a
 * replay; "4.12 ms (busy 1.30)" where its pieces had a busy time, OpenGL,
 * and the trailer then ends "; GPU busy 9.10 ms, span 30.20 ms a timed
 * replay", after "; group entries 2100 a timed replay, 1800 already bound"
 * where the pieces bound some); the length of the text in buf (cut to fit
 * n) */
int rd_perf_pass_agg_text(const RdPerfPassAgg *a, unsigned top, char *buf, size_t n);
/* The block's mean GPU time of one piece like p (same kind, target, list,
 * effect and size: rd_perf_pass_agg_add's entry), ms a piece; false when
 * the block has none like it */
bool rd_perf_pass_agg_mean(const RdPerfPassAgg *a, const RdPerfPass *p, double *ms);
/* Issue 123: a timed replay whose present blocked against the block's
 * timed replays before it (a, without it), the window's line:
 *   "stalled replay 2941 (present 352 ms, GPU 71.3 ms against the block's
 *   27.0): the first pass 52.1 ms (the block's 1.2: it waits for the
 *   display's image), the other 46 timed passes 27.0 ms (the block's 25.8);
 *   list 0 SCENE 3048x2032 52.1 ms (the block's 1.2), ...; 31 of 47
 *   passes over 2x their block mean, the rest within 1.3x (16 timed
 *   replays in the block so far)"
 * first its first render pass apart (the GPU's wait for the display's
 * image lands inside it: grown alone, the display held the image) and the
 * other timed pieces summed against the sum of their block means; then
 * its `top` most expensive pieces, each with the block's mean of one like
 * it ("(new to the block)" for one the block has none like), then of its
 * pieces the block has a mean of 0.05 ms or more for, how many took over
 * twice that mean and the largest ratio of the rest ("; 12 under 0.05 ms
 * left out", "; 2 new to the block", "; 1 not timed" for the others).  No
 * timed replay in the block before it (a empty): the pieces without the
 * block's means and "no timed replay in the block before it to compare".
 * gpuValid 0: "GPU not timed"; blockGpuMs under 0 (no GPU span in the
 * block before it): "GPU 71.3 ms, the block's first". The length of the
 * text in buf (cut to fit n) */
int rd_perf_pass_grow_text(const RdPerfPassAgg *a, const RdPerfPass *p, uint32_t count,
                           uint32_t replay, double presentMs, int gpuValid, double gpuMs,
                           double blockGpuMs, unsigned top, char *buf, size_t n);
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
