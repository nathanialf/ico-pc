/* rd_internal.h: what the rd implementation files share, plus the hooks the
 * tests and tools use.  Nothing in ico2/ includes this header.
 *
 * Files
 * -----
 *   rd_core.c      recording: the 13 lists, payload arena, state deltas, targets, the state walk
 *   rd_frame.c     the flip's frame head, per-target Z scale, the VU block, the camera in FrameCB
 *   rd_post.c      rd_post: each post kind as the GS register writes and sprites GsBase.c issues
 *   rd_blur.c      staticBlur.c's sprites and the reduction's; the open border's strips; work scale
 *   rd_blur_border.c  the post sprites' replay; the reduction's border open, its outer columns
 *   rd_shadow.c    the shadow count's recording (rd_replay.c's doShadow* replay it)
 *   rd_water.c     render-to-texture surfaces: target aliases, VRAM block targets, camera scopes
 *   rd_mesh.c      the VU mesh registry, the per-list VU images, the meshes' device arena
 *   rd_tex.c       the texture cache: GS textures decoded into rd textures (rd_tex.h)
 *   rd_pipeline.c  pipeline keys from the state block, the pipeline cache, the reachable set
 *   rd_replay.c    an RdFrame onto the RHI: every command's replay
 *   rd_upload.c    the upload ring and what a replay uploads before its first pass
 *   rd_interp.c    presentation between ticks: the blended frame and rd_present
 *   rd_present.c   DISPLAY to the output in every preset: box blit, overlay, deferred text, capture
 *   rd_crt.c       the CRT filter, in place of the present's box blit
 *   rd_video.c     the FMV picture on the output
 *   rd_perf.c      the per-replay performance records (rd.h RdPerfRecord)
 *   rd_dump.c      frame dump and load
 *   rd_fog_path.c  the fog's depth read (in place, copy, buffer) and its device self-test
 *   rd_dst_read.c  the destination read: the exact GS blend in the fragment shader
 *   rd_screen_planes.c  the screen-prim triangles' planes for the exact viewport
 *   rd_content_hide.c   the content table: stage geometry the native frame hides
 *   rd_content_extend.c the content table's sheets: overlays continued to the frame's edges
 *   rd_content_chain.c  the chains a stage animation lets down, drawn at their size
 *   rd_join.c      the card test's strips joined over a part's meshes
 *   rd_png.c       a minimal PNG writer for the replay tool and tests
 *   texpack.c      texture packs: folders, index, loader thread, RAM cache, replacement budget
 *   texpack_png.c  texture packs: the PNG reader
 *   texpack_dds.c  texture packs: the DDS reader
 *   texpack_image.c  texture packs: the loaded image the PNG and DDS readers share
 *   texpack_name.c texture packs: the names PCSX2 gives textures
 *   modelpack.c    model packs: the folder index, glTF to VU meshes, the replacements, the dump
 *   gltf.c         glTF 2.0 writer and reader for model packs
 *   xxh3.c         XXH3-64, the hash of the pack names and the meshes
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
#include <stdio.h>
#include <string.h>
#include "rd.h"
#include "rhi.h"
#include "rd_camera_blend.h"
#include "../include/ico_frame.h"

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
    RDC_FILTER,      /* b[0] mag, b[1] min (TEX1); u[0] TEX1's mipmap fields (rd_tex1_lod) */
    RDC_WRAP,        /* b[0] s, b[1] t (CLAMP) */
    RDC_TEXTURE,     /* u[0] RdTex id, b[0] RdTexFn, b[1] RdTcc; TME on */
    RDC_TEXTURE_OFF, /* TME off */
    RDC_UVOFFSET,    /* f[0], f[1] */
    RDC_COLORMASK,   /* u[0] FRAME.FBMSK */
    RDC_TARGET,      /* u[0] colour target id, u[1] depth target id, u[2] gsW | gsH << 16,
                      * b[0] useOffset; also resets the scissor to gsW x gsH */
    RDC_SCISSOR,     /* u[0..3] x0 y0 x1 y1, GS pixels, inclusive (SCISSOR_1) */
    RDC_ALPHA,       /* b[0] RdBlend, b[1] FIX; ABE untouched (raw ALPHA writes) */
    RDC_SHADE,       /* b[0] PRIM.IIP: 1 Gouraud, 0 flat */
    RDC_STATE_LAST = RDC_SHADE,
    /* actions */
    RDC_CLEAR,       /* u[0] target, b[0..3] rgba, b[4] clearDepth, u[1] GS z */
    RDC_SCREEN,      /* b[0] RdPrim, b[1] RdSpace, b[2] uvFixed, b[3] RD_SCREEN_TEXT_QUADS,
                      * b[4] RD_SCREEN_FRAME_CAMERA; u[0] payload offset of RdScreenVtx[u[1]] */
    RDC_EXACT_BLEND, /* u[0] src target, u[1] dst target: the GS integer blend of the state
                      * block's ALPHA/FIX/COLCLAMP through blend_int (RdPostParams.exactInt) */
    RDC_COPY,        /* u[0] src target, u[1] dst target, u[2] payload offset of RdCopyRec */
    /* the VU draws, the world prims (recorded only: replay calls
     * rd__not_implemented), the shadow count and the post records */
    RDC_MESH,         /* u[0] mesh, b[0] RdProg, u[1] payload offset, u[2] payload size,
                       * b[4] RD_VU_VIEW_* (rd_mesh.h; 0 in older dumps) */
    RDC_SKINNED,      /* same, plus bones in the payload */
    RDC_GRID,         /* u[1] payload offset, u[2] size; b[5] RdGridSt, f[0], f[1] its sx, sy */
    RDC_PARTICLES,    /* u[1] payload offset, u[2] size */
    RDC_WORLD_PRIMS,  /* b[0] RdPrim, u[0] count, u[1] payload offset, u[2] size */
    RDC_SHADOW_STRIP, /* b[1] RD_SHADOW_FRAME_CAMERA;
                       * b[0] 0: rd_shadow_strip, u[0] count, u[1] payload offset of float[4]
                       * x count, f[0] sign; b[0] RD_SHADOW_TRIS: rd_shadow_tris,
                       * u[1] payload offset of RdScreenVtx[u[0] + u[3]], the triangles that
                       * increment (u[0] vertices) then those that decrement (u[3]) */
    RDC_POST_STUB,    /* b[0] RdPostKind, u[1] payload offset of RdPostRec.  RD_POST_FOG
                       * (doFog) and the rd__is_blur_kind kinds (rd__blur_post_sprite) replay; any
                       * other kind is a stub (skipped, logged once) */
    /* rd_shadow.c: on the state block's colour and depth targets */
    RDC_SHADOW_RESET,   /* the depth target's stencil to 0 */
    RDC_SHADOW_RESOLVE, /* the stencil count into the colour target (rd.h rd_shadow_resolve) */
    /* PRIM.AA1 (dump version 4): a state delta numbered after the actions,
     * so a version 3 dump's command numbers keep their meaning
     * (rd__cmd_is_state) */
    RDC_AA1, /* b[0] PRIM.AA1 */
    /* deferred text (dump version 5): an action the replay draws nothing
     * for; the present collects it (rd_present.c).  b[0] RD_OTEXT_ITEM: u[1]
     * payload offset of an RdTextItem (rd.h), u[2] its size; b[0]
     * RD_OTEXT_OP: a post pass after text (b[1] RdPostKind), u[1] offset of
     * an RdTextOp, u[2] its size.  Keyed */
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

/* RDC_OVERLAY_TEXT's b[0]. */
enum { RD_OTEXT_ITEM = 0, RD_OTEXT_OP = 1 };

/* RDC_SCREEN's b[3]: the draw is the glyph quads of the last
 * RDC_OVERLAY_TEXT item (rd.h rd_deferred_text_quads), skipped by a replay that
 * draws the items deferred; 0 in every other draw and every older dump. */
#define RD_SCREEN_TEXT_QUADS 1

/* RDC_SCREEN's b[4] and RDC_SHADOW_STRIP's b[1]: the draw's vertices were
 * projected on the CPU by the frame's camera (rd.h rd_frame_projected), so
 * rd_interp.c may take them back into the world through the tick's camera
 * and project them with the blended one.  0 in every other draw and in the
 * dumps recorded before the mark (they keep the screen-space blend). */
#define RD_SCREEN_FRAME_CAMERA 1
#define RD_SHADOW_FRAME_CAMERA 1

/* A VU draw's u[3] in a frame rd_interp.c blended between two ticks: its
 * block is the frame camera's (rd_interp.c CAM_FULL), placed
 * through the view (not locked to the camera), re-based onto the blended
 * camera, so RdFrame.tickCams take its positions to the ticks' cameras
 * (rd_replay.c sets ICO_VU_TICKS; vu_common.hlsli vu_region_rule).  0 in
 * every recorded frame and every dump. */
#define RD_VU_TICK_CAMERAS 1u

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

/* Whether a command is a state delta (rd__apply_state) rather than an action. */
static inline int rd__cmd_is_state(uint8_t type)
{
    return type <= RDC_STATE_LAST || type == RDC_AA1;
}

/* RDC_SHADOW_STRIP's b[0]. */
#define RD_SHADOW_TRIS 1
/* The stencil bits the shadow count keeps: n mod 64, as 4 n mod 256 wraps. */
#define RD_SHADOW_STENCIL_MASK 0x3Fu
/* The stencil bit DATE's stencil path keeps the destination alpha's MSB in
 * (rd_replay_tiler.c rd__replay_date_take; RD_STENCIL_DATE_*): the count writes bits
 * 0-5 only (RD_SHADOW_STENCIL_MASK) and its resolve reads no other, so bit
 * 6 is free; bit 7 is unused. */
#define RD_DATE_STENCIL_BIT 0x40u
/* ICO_RD_DATE_SABOTAGE=1 (a test's switch, read once): DATE's setup writes 0
 * into the bit where the alpha's MSB is 1, so the self-test's failure path
 * runs (rd_replay_tiler.c rd__replay_date_take; rd_date_selftest.c). */
bool rd__date_sabotaged(void);
/* rd_shadow_tris tags every vertex with the place (1-based) its triangle
 * had in the call, in RdScreenVtx.rgba (little-endian; the volume draw
 * writes no colour), since the split into increments and decrements loses
 * the order; 0 is untagged (a dump recorded before the tags).  rd_interp.c
 * regroups Shadow.c's triangles into their prisms by it: emitVolumeStrip's
 * ten positions over six vertices kick eight triangles per strip. */
#define RD_SHADOW_PRISM_TRIS 8

static inline uint32_t rd__shadow_tag(const RdScreenVtx *v)
{
    return (uint32_t)v->rgba[0] | (uint32_t)v->rgba[1] << 8 | (uint32_t)v->rgba[2] << 16 |
           (uint32_t)v->rgba[3] << 24;
}

static inline void rd__set_shadow_tag(RdScreenVtx *v, uint32_t tag)
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
    float uvOffset[2];  /* rd_uv_offset: normalised texture units (tex_TransTexture uOfs/vOfs) */
    uint32_t color;     /* RdTarget id (FRAME_1) */
    uint32_t depth;     /* RdTarget id whose depth buffer is bound (ZBUF_1), 0 = none */
    uint32_t gsW, gsH;  /* the size the caller set with the target */
    uint32_t useOffset; /* add the preset's field offset to XYOFFSET */
    int32_t scissor[4]; /* x0, y0, x1, y1 inclusive */
    uint32_t gouraud;   /* PRIM.IIP (rd_gouraud), 1 = Gouraud */
    uint32_t aa1;       /* PRIM.AA1 (rd_aa1); a version 3 dump loads it as 0 */
    /* TEX1's mipmap fields, packed by rd_tex1_lod (rd_state.h): the level
     * the GS samples a texture with GS levels at (RdTexRec.gsLevels).  0 in
     * the dumps before version 9: level 0, as they were drawn */
    uint32_t tex1Lod;
} RdStateBlock;

_Static_assert(sizeof(RdDrawState) == 28, "RdDrawState layout");
_Static_assert(sizeof(RdStateBlock) == 88, "RdStateBlock is dumped as raw bytes");
/* The state block of a version 3 dump: the same fields without aa1. */
#define RD_STATE_BLOCK_V3_SIZE 80u
/* The state block of a version 4 to 8 dump: without tex1Lod. */
#define RD_STATE_BLOCK_V8_SIZE 84u

/* Applies a state command to s.  Returns false (s untouched) for actions. */
bool rd__apply_state(RdStateBlock *s, const RdCmd *c);
/* The state block a fresh GS starts with (rd_init). */
void rd__reset_state_block(RdStateBlock *s);

/* --------------------------------------------------------------- frames */
typedef struct RdCmdList {
    RdCmd *cmds;
    uint32_t count, cap;
} RdCmdList;

#define RD_MAX_TEMP_PER_FRAME 32
/* The frames kept: the one being recorded, the last closed (current) and
 * the one before it (previous), which the interpolation blends while the
 * next is recorded (rd_interp.c). */
#define RD_FRAME_RING 3

/* one of the game's box cull drops (rd_note_cull_drop) */
typedef struct RdCullDrop {
    char model[32];
    int16_t part;
    uint8_t rule, views;
    uint8_t oneWhy; /* with views 1: why (rd.h rd_note_cull_drop) */
} RdCullDrop;

typedef struct RdFrame {
    RdCmdList lists[RD_LIST_COUNT];
    uint8_t *payload;
    uint32_t payloadSize, payloadCap;
    RdStateBlock startState; /* the state the previous frame's replay left */
    RdStateBlock endState;   /* filled when the frame closes */
    RdCamera camera;
    uint32_t hasCamera;
    uint32_t number; /* 1, 2, ... in recording order; 0 = never recorded */
    uint32_t keep;   /* rd_end_frame(keep) */
    uint32_t closed;
    uint32_t gsW, gsH;
    /* the native frame the game built this tick for (rd_frame_head; dump
     * version 10): the display aspect, the visible width in GS pixels (512 k)
     * and the width the cull and the draws used (= gsW).  For a frame
     * without a head 4/3, 0 (unknown: the presenter derives it, a dump
     * writes gsW) and gsW; 4/3, 512 and 512 in the dumps before version 10 */
    float aspect, wExact;
    uint32_t cullW;
    uint32_t tempTargets[RD_MAX_TEMP_PER_FRAME]; /* owned, freed when the frame is reused */
    uint32_t tempCount;
    /* rd_frame.c; not dumped */
    RdVuCommon vu; /* gsb_MakeCommonMatrix's block, the last one recorded */
    uint32_t hasVu;
    /* rd_frame_head's two copies: [0] at the head of list 0, [1] of list 11;
     * command index ranges [start, end) and the index of each RDC_CLEAR and
     * RDC_TARGET; headValid 0 = no head recorded */
    uint32_t headValid;
    uint32_t headStart[2], headEnd[2], headClear[2], headTarget[2];
    /* rd_interp.c; not dumped: rd_camera_cut while the frame
     * was open (rd_end_frame copies it into camera.cut), and the strongest
     * fade rd_post(RD_POST_FADE) recorded, 1 + its alpha (0: none) */
    uint32_t cut;
    uint32_t fade;
    /* rd_free_camera while the frame was open (rd_end_frame copies it into
     * camera.freeCamera) */
    uint32_t freeCamera;
    /* not dumped: RDC_OVERLAY_TEXT items recorded so far (the
     * post passes record their ops only after one) */
    uint32_t textItems;
    /* not dumped: the game's box cull's drops this tick (rd_note_cull_drop),
     * the first RD_CULL_DROPS_MAX of cullDropCount */
    uint32_t cullDropCount;
    RdCullDrop cullDrops[RD_CULL_DROPS_MAX];
    /* rd_interp.c's blended frame; not dumped: hasTickCams 1 when its
     * camera is blended between two ticks' that differ, tickCams the two
     * ticks' cameras as R = S_tick St^-1 (column-major; [0] the tick
     * before's, [1] this tick's; S = proj view, St the blended camera's),
     * for the draws marked RD_VU_TICK_CAMERAS (IcoVuCB.ticks) */
    uint32_t hasTickCams;
    float tickCams[2][16];
} RdFrame;

void rd__frame_reset(RdFrame *f);
void rd__frame_free(RdFrame *f);
/* Appends size bytes (8-aligned) to the payload; returns the offset. */
uint32_t rd__frame_payload(RdFrame *f, const void *data, uint32_t size);

/* The first and last list a frame replays: 0..12, or 11..12 with keep. */
static inline int rd__first_list(int keep)
{
    return keep ? 11 : 0;
}

/* Walks the replayed lists of f in order, applying state commands to *state
 * (which the caller seeds, normally with f->startState) and calling fn for
 * every command after it is applied.  fn may be NULL. */
typedef void (*RdWalkFn)(void *user, int list, uint32_t index, const RdCmd *cmd,
                         const RdStateBlock *state);
void rd__walk(const RdFrame *f, int keep, RdStateBlock *state, RdWalkFn fn, void *user);

/* RdTargetRec.dstUsage's bits (rd__dst_target_usage) */
#define RD_DST_USAGE_STORAGE 1u    /* the exact blend's storage read */
#define RD_DST_USAGE_ATTACHMENT 2u /* the exact blend's attachment read */
#define RD_DST_USAGE_LOCAL_DATE 4u /* DATE's local read of the colour (not the blend) */
/* the fog's local read of the depth (RD_FOG_LOCAL): the depth texture made
 * an input attachment too (rd_core.c rd__target_create_gpu, a scene-class
 * target with depth under RdProfile.fogLocal) */
#define RD_DST_USAGE_LOCAL_DEPTH 8u

/* ------------------------------------------------------------- targets
 * Ids: named target n (RdTargetId) is n + 1; temporary targets are
 * (generation << 16) | (slot + 1) with slot >= RD_TARGET_COUNT. */
#define RD_MAX_TARGETS 96

/* Full pixel: the texels a cropped frame-wide target holds past the
 * frame's exact width at each side (rd_core.c rd__target_scale_of): the
 * reduction's whole-texel sample reads one there */
#define RD_FULL_PIXEL_GUARD 1u

typedef struct RdTargetRec {
    uint32_t gen;
    uint8_t live, named, withDepth, keepAcross;
    uint32_t w, h; /* GS pixels */
    /* The texture's size and its texels per GS pixel
     * (tw = w * sx rounded, th = h * sy); tw == w, th == h, sx == sy == 1
     * in Original and for every target the Enhanced resolution does not
     * scale (rd__target_scale_of) */
    uint32_t tw, th;
    float sx, sy;
    /* Full pixel (rd__apply_display, rd_core.c rd__target_scale_of): a
     * frame-wide scene-class target holds only the frame's exact visible
     * width, the window's own pixels, and ox texels of the GS frame's left
     * (and right) are not in it: GS pixel x of the target is texels
     * x * sx - ox (rd__gs_texel_x); it holds RD_FULL_PIXEL_GUARD texels
     * more at each side, which the present leaves out.  0 for every other
     * target */
    float ox;
    RhiFormat format;
    RhiTexture color, depth;
    RhiState colorState, depthState;
    uint8_t depthSampled; /* depth made sampleable (rd__sampled_depth reads it in place) */
    /* copy used when a draw samples the target it renders to, or reads its
     * destination (rd_replay.c takeSnap: only the area a draw reads is fresh) */
    RhiTexture snap;
    RhiState snapState;
    uint32_t viewTex[3]; /* RdTex ids of rd_target_texture, per RdTexView */
    uint8_t zFormat;     /* RdZFormat of the depth buffer; 0 = PSMZ32 */
    /* a freed temporary target kept with its textures for the
     * next of its size (rd__temp_target_alloc); a taken one is cleared
     * (rd_upload.c rd__clear_new_targets: colour 0, depth 1.0) at the next replay
     * (clearPending) */
    uint8_t parked, clearPending;
    /* the reads the colour texture was made for (rd__dst_target_usage):
     * RD_DST_USAGE_STORAGE and RD_DST_USAGE_ATTACHMENT the exact blend's
     * (only the mode [dev] blend opted into), RD_DST_USAGE_LOCAL_DATE
     * DATE's local read (RdProfile.dateStencil, scene-class targets only),
     * RD_DST_USAGE_LOCAL_DEPTH the fog's of the depth; 0 none */
    uint8_t dstUsage;
    /* the colour texture (and its snapshot) has the RGBA8_UINT view
     * (RHI_TEX_UINT_VIEW: rd_core.c rd__target_create_gpu, under
     * RdProfile.uintViewWork / uintViewScene), which fx_sprite_u_ps reads */
    uint8_t uintView;
    /* a render-to-texture block rd_block_target made (rd_water.c: the
     * reflections of puddle.c and pool.c, the queen barrier's copy of the
     * frame, the dark volume's scene-sized block): one that is not
     * scene-sized takes the scene's scale under Water reflections: Sharp
     * (RdSettings.waterSharp, rd__target_scale_of); 1 also in a dump's
     * keep word (rd_dump.c) */
    uint8_t block;
} RdTargetRec;

/* The texels a screen-prim command can touch on the bound target tc: the
 * bounding box of its vertices (12.4, after XYOFFSET: the native frame's
 * origin 2048 - gsW / 2) padded by two GS pixels (three with PRIM.AA1's
 * edge fringes), through the same mirror and texel scale as the draw,
 * intersected with the scissor bindDraw sets.  mirror is the draw's
 * (rd_replay.c doScreen).  False when it covers nothing.  The DATE
 * snapshot copies only this area (rd_replay.c dateSnapshot). */
bool rd__screen_area(const RdTargetRec *tc, const RdStateBlock *st, const RdScreenVtx *v,
                     uint32_t n, int mirror, int aa1, RhiRect *area);
/* Whether the DATE snapshot must be (re)taken for a draw on target tcId at
 * write serial serial that reads area want (texels; NULL = the whole tw x
 * th target), given the snapshot held (for target heldFor at serial
 * heldSerial, area held).  When it must, *take is the area to copy: want
 * for a new target or serial, the whole target for a second area in the
 * same target and serial (at most one retake more than a whole-target
 * snapshot would need). */
bool rd__date_retake(uint32_t heldFor, uint32_t heldSerial, const RhiRect *held, uint32_t tcId,
                     uint32_t serial, const RhiRect *want, uint32_t tw, uint32_t th, RhiRect *take);
/* Full pixel's clamp of a draw sampling a render target (rd_replay.c
 * drawWrap) against a draw the game wraps on purpose: whether texel
 * coordinates lo .. hi on an axis of size texels lie wholly in one period
 * of a REPEAT other than the first, [k size, (k + 1) size] with k != 0.
 * The GS then samples the texture's inside (puddle.c drawRipples: STs 1.05
 * to 1.95 of the block, the reflection's middle), where a clamp pins every
 * fragment to the edge texel (issue 106: bright flat ring segments).  The
 * screen-space lookups the clamp is for run from about 0 to size and stay
 * clamped. */
bool rd__repeat_kept(float lo, float hi, float size);
/* The axes (bit 0 S, bit 1 T) on which a screen-prim command's texel
 * coordinates on a tw x th texture keep REPEAT by that rule; uvFixed as the
 * command's (12.4 texels, else STQ normalised, divided per vertex) */
uint32_t rd__screen_repeat_kept(const RdScreenVtx *v, uint32_t n, int uvFixed, float tw, float th);

/* GS Z to depth scale of a target id's depth buffer: PSMZ32, the default
 * (also for an unknown id), is 2^-33 on a float depth buffer (gs_math.hlsli
 * GS_ZSCALE_32F: the top Z values mapped apart) and 2^-32 on D24S8. */
float rd__target_z_scale(uint32_t id);
/* 2^24 - 1 when the targets' depth is 24-bit fixed point (the Vulkan
 * D24S8 fallback), 0 when it is float: the fog shader compares depths in
 * the steps the buffer stores (fog_lut.hlsl) */
float rd__depth_unorm_steps(void);
/* gs_z_to_depth on the CPU (clears): the same formula as gs_math.hlsli. */
float rd__gs_depth(uint32_t z, float scale);

RdTargetRec *rd__target_rec(uint32_t id);
/* Whether t is a work buffer: WORK0, WORK1, WORK3, AA0, AA1, FEED128,
 * AURA_TAP, WORK2_PAD or FEED_HELD (a named target neither scene-class nor
 * a shadow level: rd_core.c rd__target_scale_of).  They keep the PS2's
 * sizes at every scale, and a draw that reads one onto a scaled target
 * reads it with the GS's own filter at that target's finer grid, as the
 * PS2 magnifies it onto its frame, never an above-1x kernel: the glow's,
 * the depth of field's and the aura's pastes into SCENE take
 * fx_sprite_ps's bilinear or nearest at each texel's GS position
 * (rd_replay.c blurAbove1xSteps), the softening's AA0 and AA1 back over
 * SCENE sprite_ps's own sampling (spriteAbove1x).  The game's draws from a
 * work buffer onto the frame all magnify (half a texel a GS pixel or
 * less); draws from SCENE, WORK2 or AURA_WORK into one keep the box */
bool rd__work_class(const RdTargetRec *t);
/* a target's name: SCENE, DISPLAY, ... for a named one, "block" for a
 * render-to-texture block, "temporary" for another temporary, "gone" */
const char *rd__target_name(uint32_t id);

/* The texel position of GS pixel x (from the target's left GS edge) on t:
 * x * sx less the Full pixel crop (RdTargetRec.ox, 0 but on a cropped
 * frame-wide target) */
static inline float rd__gs_texel_x(const RdTargetRec *t, float x)
{
    return x * t->sx - t->ox;
}

/* Creates the GPU textures of a target record (no-op without a device). */
bool rd__target_create_gpu(RdTargetRec *t, const char *name);
void rd__target_destroy_gpu(RdTargetRec *t);
/* Allocates a temp target record (dump loading uses it too). */
uint32_t rd__temp_target_alloc(uint32_t w, uint32_t h, int withDepth, int keepAcross);
/* the same, block != 0 marking a render-to-texture block
 * (RdTargetRec.block: rd_block_target, a dump's block) */
uint32_t rd__temp_target_alloc_ex(uint32_t w, uint32_t h, int withDepth, int keepAcross, int block);
/* rd_temp_target of a block (rd_water.c rd_block_target): freed with the
 * open frame, RdTargetRec.block set */
RdTarget rd__block_temp_target(uint32_t gsW, uint32_t gsH, int withDepth);
void rd__temp_target_free(uint32_t id);
/* Destroys the parked temporary targets' textures (a scale
 * change, shutdown). */
void rd__temp_target_pool_clear(void);
/* A scene-sized allocation failed: asks for a lower scene scale at the next
 * rd_begin_frame, when the scene is above 1x (nothing to give back at 1x). */
void rd__note_scene_pressure(void);
/* The same for a target's own texture (a snapshot): only a scene-sized one
 * counts, a work buffer does not shrink with the scene. */
void rd__note_target_pressure(const RdTargetRec *t);
/* The scene-sized textures kept between frames (fog and effects depth copies,
 * the wrap accumulator), freed so a recreate of the targets starts without
 * them; each is made again at the new size when next needed. */
void rd__scene_caches_free(void);
void rd__effects_depth_free(void);
#define RD_TEMP_PARKED 24 /* parked temporary targets kept at most */

/* ------------------------------------------------------------ textures */
#define RD_MAX_TEXTURES 8192

enum { RD_TEXKIND_IMAGE = 1, RD_TEXKIND_TARGET = 2 };

/* An image texture's texel format (RdTexRec.format; the dump
 * writes it in the image's view word, so 0 is what every older dump holds).
 * RGBA8: rd_create_texture/rd_create_texture_src, 4 bytes a texel.  R8:
 * rd_create_texture_r8, 1 byte (coverage in GS alpha units), drawn with font_ps
 * (rd__plan_screen_draw selects RD_FS_FONT for it) and never given mips.
 * SHEET: rd_create_texture_sheet, 1 byte (coverage
 * 0..255) and RdTexRec.sheet, drawn with font_sheet_ps (RD_FS_FONT_SHEET),
 * never given mips; appended, so the values before it keep their meaning
 * in dumps. */
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
static inline int rd__texel_is_coverage(uint8_t format)
{
    return format == RD_TEXEL_R8 || format == RD_TEXEL_SHEET;
}

static inline int rd__texel_is_block(uint8_t format)
{
    return format >= RD_TEXEL_BC1 && format <= RD_TEXEL_BC7;
}

/* Bytes of one texel of an uncompressed format (RGBA8 4, R8 and SHEET 1).
 * Not meaningful for the BC formats: they use rd__texel_block_bytes. */
static inline uint32_t rd__texel_bytes(uint8_t format)
{
    return rd__texel_is_coverage(format) ? 1u : 4u;
}

/* Texture packs: the side of a format's copy unit in texels (4 for the BC
 * formats' 4x4 blocks, 1 otherwise) and the bytes of one unit (BC1 8,
 * BC2/BC3/BC7 16, else rd__texel_bytes).  A row of a level w texels wide
 * is ceil(w / blockW) * blockBytes bytes and covers blockW texel rows. */
static inline uint32_t rd__texel_block_w(uint8_t format)
{
    return rd__texel_is_block(format) ? 4u : 1u;
}

static inline uint32_t rd__texel_block_bytes(uint8_t format)
{
    if (!rd__texel_is_block(format)) {
        return rd__texel_bytes(format);
    }
    return format == RD_TEXEL_BC1 ? 8u : 16u;
}

/* The RHI format of an image texture of format (RD_TEXEL_*). */
static inline RhiFormat rd__texel_rhi_format(uint8_t format)
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
    uint8_t bakedTexa; /* rd_create_texture's texaMode, kept for dumps and debugging */
    uint32_t w, h;
    uint8_t *pixels; /* w * h texels of format; the CPU copy dumps and re-uploads read */
    RhiTexture rhi;
    RhiState state;
    uint8_t dirty;
    uint8_t format; /* RD_TEXEL_* (images; targets leave it 0) */
    /* with dirty, the texels changed since the last upload: the union
     * of the updates, [dirtyX0, dirtyX1) x [dirtyY0, dirtyY1); the whole
     * texture after a create or rd_update_texture.  rd__upload_dirty_textures copies
     * that rectangle (the whole texture when it makes the RHI texture or
     * a mip chain) */
    uint32_t dirtyX0, dirtyY0, dirtyX1, dirtyY1;
    uint8_t view;      /* RdTexView, RD_TEXKIND_TARGET */
    uint8_t mipLevels; /* levels of rhi (1 unless the Enhanced filter generated mips) */
    uint32_t target;   /* RdTarget id, RD_TEXKIND_TARGET */
    /* the debug name, and the replays of the last uploads (a
     * texture uploaded on many replays in a row is logged once) */
    char name[24];
    uint32_t lastUpload;
    uint16_t uploadStreak;
    uint8_t streakLogged;
    /* Texture packs.  replacement: the texels came from a pack file
     * (rdtex_create_replacement), not from the game's TIM2; the sampler
     * treats it as mipmapped and it has no CPU copy in pixels.  uvW, uvH:
     * the size the draw's UVs are normalised by, the GS size the game's
     * texture had (2^TW x 2^TH), so a 4x replacement samples the same
     * place; 0 = w, h (every texture that is not a replacement).
     * pending: the replacement's levels as the file held them, owned
     * here until rd__upload_dirty_textures copies every level to the RHI texture and
     * frees it (null otherwise).  refused: the graphics card would not
     * create it (uploadReplacement); rdtex_find then misses, so the game's
     * own texture is decoded again in its place. */
    uint8_t replacement;
    uint8_t refused;
    uint32_t uvW, uvH;
    struct TexpackImage *pending;
    /* What the draws of a game texture with Enhanced mips do
     * with its alpha (RD_MIPUSE_*, seen at replay, sticky) and the highest
     * alpha-test reference of its unblended alpha-tested draws (as "a >
     * ref"); mipBuiltBoost and mipBuiltRef: the coverage the uploaded mips
     * keep (uploadMips), a draw that changes it re-uploads the texture. */
    uint8_t mipUse, mipRef;
    uint8_t mipBuiltBoost, mipBuiltRef;
    /* A SHEET texture's style (rd.h RdSheetStyle):
     * the rim's weight (0 off, 1..64 the 64ths of a full rim: rimOn with
     * rimWeight), rimLevel, fillLevel, dither (kept as 0 or 1) */
    uint8_t sheet[4];
    /* Its RdSheetStyle.scale, 1..ICO_SHEET_SCALE_MAX
     * (0 in a texture that is no sheet) */
    uint8_t sheetScale;
    /* The GS mipmap levels of a game texture whose TEX1 asks for mipmapping
     * and whose levels are authored (rdtex_store_levels): gsLevels counts
     * them with the base (0 or 1: none), gsChain holds levels 1.. as RGBA8
     * one after the other, level l max(w >> l, 1) x max(h >> l, 1) texels.
     * These are the TIM2's own pictures, not reductions of the base: some
     * textures keep a dark base and brighter small levels on purpose, so the
     * GS's choice of level by distance fades them (the fog puffs, the light
     * shafts, the mists).  Textures whose levels are reductions keep none
     * and draw from the base alone.  Uploaded
     * as the texture's mips whatever the filter option (uploadMips) and
     * sampled at the GS's level (DF_GS_LOD) */
    uint8_t gsLevels;
    uint8_t *gsChain;
} RdTexRec;

/* RdTexRec.mipUse bits */
enum {
    RD_MIPUSE_BLEND_AS = 1, /* a draw blends by As (PRIM.ABE, C = As): alpha is opacity */
    RD_MIPUSE_TESTED = 2    /* an unblended draw alpha-tests it (AFAIL KEEP or ZB_ONLY) */
};

RdTexRec *rd__tex_rec(uint32_t id);
/* Gives an image texture its GS levels (RdTexRec.gsLevels, gsChain): levels
 * counts the base, chain holds levels 1.. as RGBA8, level l
 * max(w >> l, 1) x max(h >> l, 1) texels (copied).  levels 0 or 1 (or a
 * null chain) takes them away.  The texture is uploaded again when they
 * change. */
void rd__tex_set_levels(RdTex tex, const uint8_t *chain, uint32_t levels);
/* The bytes of levels 1..levels-1 of a w x h image's GS chain. */
size_t rd__gs_chain_bytes(uint32_t w, uint32_t h, uint32_t levels);
/* An image texture of format (RD_TEXEL_*), texels copied from px
 * (w * h * rd__texel_bytes bytes) or zero; rd_create_texture_src and
 * rd_create_texture_r8 are this, and the dump loader. */
RdTex rd__create_texture_fmt(uint32_t w, uint32_t h, const void *px, uint8_t format, RdTexSrc src,
                             const char *debugName);
/* Texture packs: an image texture whose levels are img's (moved into
 * pending, *img left empty), format img->fmt, src RD_TEXSRC_RGBA32,
 * mipLevels img->levels, uvW x uvH the size draws normalise its UVs by.
 * rdtex_create_replacement checks the image first; this only records it. */
RdTex rd__create_texture_replacement(struct TexpackImage *img, uint32_t uvW, uint32_t uvH,
                                     const char *debugName);
/* Frees a replacement's pending levels (texpack_free_image) and the
 * TexpackImage holding them; t->pending is null afterwards. */
void rd__free_pending(RdTexRec *t);

/* --------------------------------------------------------------- meshes
 * rd_mesh.c.  A VU mesh keeps its vertex stream (the
 * batches' vertices without their GIF tags, float4 quadwords as the VIF
 * unpacked them) and its index list (ICO_VU_INDEX(kick, corner)) on the CPU;
 * replay copies them into the frame's upload ring once per replayed frame
 * (rd_replay.c) and binds the stream as the storage buffer t0. */
typedef struct RdVuBatchRec {
    uint32_t firstIndex, indexCount; /* in the mesh's index list */
    uint32_t firstVertex, vertexCount;
    uint32_t srcQw;    /* the batch's GIF tag in the creation stream (rd_update_vu_mesh) */
    uint32_t prim;     /* the tag's PRIM field */
    uint16_t material; /* RdVuBatchDesc.material */
    uint16_t group;
} RdVuBatchRec;

typedef struct RdMeshRec {
    uint32_t gen;
    uint8_t live;
    uint8_t vu;       /* rd_create_vu_mesh */
    uint8_t replaced; /* rd_create_vu_mesh_replacement built it */
    uint8_t stale;    /* rd_vu_mesh_retire: not valid, freed once no kept frame drew it */
    uint32_t vertexCount, stripCount, materialCount;
    /* VU meshes */
    uint32_t qwPerVertex, batchCount, srcQw;
    float (*stream)[4]; /* vertexCount * qwPerVertex quadwords */
    uint32_t *index;    /* the list the device draws, dense (ICO_VU_INDEX) */
    /* The triangles' marks, a byte a kick (shader_consts.h ICO_VU_MARK_*;
     * markCount bytes, NULL when no triangle has one): ICO_VU_MARK_LATER
     * on each triangle that overlaps an earlier triangle of the mesh in
     * its plane (rd_mesh.c markLaterOverlaps), ICO_VU_MARK_HIDE on each
     * triangle of a content table entry (rd_content_hide.c, rd_mesh.c
     * markContentHidden), ICO_VU_MARK_JOINED on each triangle of a joined
     * strip (markJoinedStrips, rd__mesh_join_part).  A draw binds them
     * after the stream (rd__mesh_mark_qw, rd__mesh_write_marks;
     * vu_common.hlsli vu_kick_marks) */
    uint8_t *kickMark;
    uint32_t markCount;
    uint32_t indexCount;
    RdVuBatchRec *batches;
    uint32_t lastUsed;   /* g_rd.frameCounter of the last draw recorded */
    uint32_t replaySeen; /* g_rd.replayCounter of the replay that uploaded it */
    uint64_t ringStream, ringIndex;
    /* the device copy (rd_mesh.c's arena, rd_upload.c
     * rd__upload_meshes): stream at gpuOff, indices at gpuIndexOff of chunk
     * gpuChunk - 1 (0: none), gpuSize bytes reserved; uploaded again when
     * gpuDirty (rd_update_vu_mesh).  transient: rewritten every present (the
     * interpolation's scratch meshes), drawn from the ring instead */
    uint64_t gpuOff, gpuIndexOff, gpuSize;
    uint8_t gpuChunk, gpuDirty, transient;
    char name[24];
    /* The stream's versions for the presenter (rd_update_vu_mesh).
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

    /* rd_mesh.h's mesh identity (rd_vu_mesh_desc_hash); for a
     * replaced mesh the original's; 0 for one built from a raw stream */
    uint64_t hash;
    /* the meshes of the part its strips were last joined over
     * (rd__mesh_join_part's sig; 0: its own strips only) */
    uint64_t partJoinSig;
    /* a sheet of the content table (rd_content_extend.c): the vertex count
     * of the stream it was made from, the tiles appended after it
     * (rd_mesh.c extendSheet; 0: none), its index count (the game's own
     * triangles, all a draw out of the box draws: rd__vu_sheet_end), and
     * the entry's cols and rows, for rd_update_vu_mesh to lay them again */
    uint32_t tiledFrom, tiledIndexFrom;
    uint8_t tileCols, tileRows;
    /* The free-standing strips (rd_mesh.c cardStrips, at creation): the
     * runs of vertices between two strip flags in a batch, three or more,
     * none of whose triangles carried ICO_VU_MARK_JOINED then; cardStrips
     * holds each one's first vertex and one past its last, in vertex order
     * (the card test's strips, rd_draw_vu_mesh).  boneQwUsed: a skinned
     * mesh's VuBoneCB qwords its weights read (the card bits go after them,
     * rd_replay_vu.c doVuDraw) */
    uint32_t *cardStrips;
    uint32_t cardStripCount, boneQwUsed;
} RdMeshRec;

/* The stream mesh m had when frame `frame` was recorded, NULL when no
 * kept version covers it */
const float (*rd__mesh_stream_at(const RdMeshRec *m, uint32_t frame))[4];

#define RD_MAX_MESHES 16384

RdMeshRec *rd__mesh_rec(uint32_t id);
/* The device arena of the meshes' copies (rd_mesh.c): device
 * buffers of RD_MESH_CHUNK bytes (a larger mesh gets one of its own), first
 * fit with coalescing.  rd__mesh_gpu_reserve gives m a range for its stream
 * and indices (false: the arena is full, the mesh is drawn from the ring);
 * meshFree returns it.  A range freed while the GPU may still read it is
 * safe to reuse: every later write to it is a buffer copy recorded after,
 * which waits for earlier reads (rhi.h rhi_cmd_copy_buffer). */
#define RD_MESH_CHUNK (32u << 20)
#define RD_MESH_CHUNKS 32
bool rd__mesh_gpu_reserve(RdMeshRec *m, uint64_t streamBytes, uint64_t indexBytes, uint64_t align);
RhiBuffer rd__mesh_gpu_buffer(uint32_t chunk);
/* Its size in bytes (the VU stream binds it whole). */
uint64_t rd__mesh_gpu_buffer_size(uint32_t chunk);
/* destroys the arena's buffers; every mesh loses its device copy */
void rd__mesh_gpu_shutdown(void);
/* Creates a VU mesh record from a stream and index list already in the
 * tagless layout (dump loading); returns the id, 0 on failure.  Every VU
 * mesh is made here, so its marks are worked out here too. */
uint32_t rd__vu_mesh_create_raw(const float (*stream)[4], uint32_t vertexCount,
                                uint32_t qwPerVertex, const uint32_t *index, uint32_t indexCount,
                                const RdVuBatchRec *batches, uint32_t batchCount, const char *name);

/* The card test's joined strips over the n meshes of
 * one part of a model (rd_replay.c joinParts: the VU draws of a frame with
 * one object and part in their keys), a strip of one mesh joined by a strip
 * of another shape in any of them (rd_mesh.c joinStrips); the marks are
 * added to the meshes' kickMark (gpuDirty when a device copy holds the
 * old list).  sig names the set of meshes: nothing is done when every one
 * was last joined under it.  1 when a mark was added. */
uint64_t rd__mesh_join_part(RdMeshRec *const *ms, uint32_t n, uint64_t sig);

/* rd_join.c: the frame's VU draws grouped by object and part (the key
 * without the packet's ordinal) and joined with rd__mesh_join_part */
void rd__join_parts(const RdFrame *f, int keep);

/* The marks of the triangle of kick k (ICO_VU_MARK_*; 0 past the bytes) */
static inline uint32_t rd__mesh_kick_mark(const RdMeshRec *m, uint32_t k)
{
    return m->kickMark && k < m->markCount ? m->kickMark[k] : 0u;
}

/* The qwords of mark bytes a draw of m binds after its stream (one byte a
 * vertex, rounded up to a qword; 0 when m has no marks) */
static inline uint32_t rd__mesh_mark_qw(const RdMeshRec *m)
{
    return m->kickMark ? (m->vertexCount + 15u) / 16u : 0u; /* ICO_VU_MARKS_PER_QW */
}

/* m's mark bytes as a draw binds them: rd__mesh_mark_qw(m) * 16 bytes at
 * dst, a byte a kick, zero past the marked ones */
void rd__mesh_write_marks(const RdMeshRec *m, uint8_t *dst);

/* dst's marks become a copy of src's (none when src has none): the
 * interpolation's scratch meshes draw their source's (rd_interp.c); false
 * on no memory (dst then has none) */
bool rd__vu_mesh_copy_marks(RdMeshRec *dst, const RdMeshRec *src);

/* The content table (rd_content_hide.c): stage geometry made for the PS2's
 * 4:3 picture, by the stream bits of its black corners, and what the
 * renderer does with it.  RD_HIDE_OFF is never listed (an override's
 * "off"). */
typedef enum RdHideVerb { RD_HIDE_OFF = 0, RD_HIDE = 1 } RdHideVerb;

typedef struct RdContentHide {
    const char *model; /* where it is, for the reader */
    RdHideVerb verb;
    uint32_t count;
    uint32_t corner[8][3]; /* x, y, z float bits */
} RdContentHide;

extern const RdContentHide rd__content_hide[];
extern const int rd__content_hide_count;
#define RD_CONTENT_HIDE_MAX 256 /* the table's size at most (rd_content_hide.c asserts) */

/* rd_mesh.c: the verbs the meshes created from now on are marked with,
 * for tools/content_table_check.py (the environment variable
 * ICO_RD_CONTENT_HIDE, read when the first mesh is marked) and the tests.
 * spec: comma-separated "off", "hide" or "table" for every entry
 * (table: as listed), or "N=verb" for entry N; NULL is "table".  False
 * (nothing changed) on a bad spec. */
bool rd__content_hide_set(const char *spec);
/* the verb entry i is applied with (RD_HIDE_OFF for an index outside) */
RdHideVerb rd__content_hide_verb(int i);

/* The content table's sheets (rd_content_extend.c): stage overlays made to
 * cover the PS2's 4:3 picture, by the bits of their stream, and the tiles
 * rd_mesh.c appends around them.  RD_EXTEND_OFF is never listed (an
 * override's "off"). */
typedef enum RdExtendVerb { RD_EXTEND_OFF = 0, RD_EXTEND_TILES = 1 } RdExtendVerb;

typedef struct RdContentExtend {
    const char *model; /* where it is, for the reader */
    RdExtendVerb verb;
    uint32_t vertexCount, qwPerVertex;
    uint64_t streamHash; /* xxh3_64 of the vertexCount * qwPerVertex quadwords */
    uint8_t cols, rows;  /* tiles appended outward across and down */
} RdContentExtend;

extern const RdContentExtend rd__content_extend[];
extern const int rd__content_extend_count;
#define RD_CONTENT_EXTEND_MAX 64 /* the table's size at most (rd_content_extend.c asserts) */

/* rd_mesh.c: the verbs the meshes created from now on are made with, as
 * rd__content_hide_set (the environment variable ICO_RD_CONTENT_EXTEND,
 * read when the first mesh is made): "off", "tiles" or "table" for every
 * entry, or "N=verb" for entry N; NULL is "table".  False (nothing
 * changed) on a bad spec. */
bool rd__content_extend_set(const char *spec);
/* the verb entry i is applied with (RD_EXTEND_OFF for an index outside) */
RdExtendVerb rd__content_extend_verb(int i);
/* rd_mesh.c: the tiles appended to the sheet in stream (vertexCount
 * vertices of qwPerVertex quadwords, the prelit or lit layout): every
 * 4-vertex strip an axis-aligned tile in one plane (its U a function of x
 * alone, its V of y alone), all of one size; the outer column repeated cols
 * times outward across, then the outer row rows times outward down (away
 * from the model's origin; both ways on an axis the sheet is centred on),
 * the UV continued.  Returns the vertex count of the tiles, 0 when the
 * stream is not such a sheet; writes them at out when out is given and cap
 * (in vertices) holds them. */
uint32_t rd__sheet_tiles(const float (*stream)[4], uint32_t vertexCount, uint32_t qwPerVertex,
                         uint32_t cols, uint32_t rows, float (*out)[4], uint32_t cap);
/* rd_mesh.c: where a draw of m's indices up to `end` (one past its last
 * batch's) stops: out of the box (extended false: the Original preset at
 * 4:3 and 1x, rd_replay.c vuExtended) a sheet of the content table draws
 * only the game's own triangles (m->tiledIndexFrom), so the PS2's picture
 * never holds an appended tile whatever the sheet's placement; in any
 * other state, and for any other mesh, `end` */
uint32_t rd__vu_sheet_end(const RdMeshRec *m, uint32_t end, bool extended);
/* Whether the picture is not the PS2's out of the box (ICO_VU_EXTENDED; the
 * region rule, vu_common.hlsli vu_region_rule): the Enhanced preset, a frame
 * wider than 4:3 or a scene scale above 1 (rd_replay.c) */
bool rd__vu_extended(void);
/* A vertex shader's embedded name (RD_VS_*; rd_replay.c s_vsNames) */
const char *rd__vs_name(uint8_t vs);
/* A fragment shader's embedded name (RD_FS_*; rd_replay.c s_fsNames) */
const char *rd__fs_name(uint8_t fs);
/* The card section of a VU draw's payload (rd_mesh.h RdVuCards) of size
 * bytes at payload: its header in *cards and the offset of its words from
 * payload, 0 when the payload has none */
struct RdVuCards;
uint32_t rd__vu_cards_at(const uint8_t *payload, uint32_t size, struct RdVuCards *cards);
/* rd_interp.c's blend of a VU draw: the tick before's verdicts (prev, its
 * payload of prevSize bytes) into the blended copy's first bits (out) */
void rd__vu_cards_merge(uint8_t *out, uint32_t outSize, const uint8_t *prev, uint32_t prevSize);
/* A loaded dump's VU draws without a card section get one (rd_dump.c) */
void rd__vu_cards_fill_frame(RdFrame *f);
/* rd_mesh.c: the per-list VU images (rd_set_vu_common updates all 13). */
void rd__vu_init(void);
void rd__vu_shutdown(void);
void rd__vu_load_common(const RdVuCommon *block);
void rd__mesh_shutdown(void);
/* rd_begin_frame, after the frame number advanced: frees the
 * stale meshes (rd_vu_mesh_retire) no frame from three back on drew */
void rd__vu_mesh_sweep_stale(void);

/* ------------------------------------------------------------ profile
 * The device profile (rd_profile.c; docs/port/RENDER.md): what this device
 * class gets, decided once after rhi_init from RhiLimits, the backend and
 * the platform, read everywhere as g_rd.profile.<flag>, overridden one flag
 * at a time by the [dev] keys and turned off by a self-test that fails
 * (each says so in its line).  One renderer: the flags are the only
 * vendor-dependent thing in it. */
typedef struct RdProfile {
    bool tiler;         /* RhiLimits.tiler: a tile-based GPU */
    bool localRead;     /* RhiLimits.localRead */
    bool fogLocal;      /* the fog reads the depth in the scene pass (RD_FOG_LOCAL first) */
    bool dateStencil;   /* DATE through the stencil bit, not the R8 snapshot */
    bool storePlan;     /* store-op DONT_CARE for depth and targets nothing reads later */
    bool blurFold;      /* the staticBlur pairs folded into one pass each */
    bool uintViewWork;  /* the work-class targets get the RGBA8_UINT view */
    bool uintViewScene; /* SCENE, WORK2 and DISPLAY too (immediate-mode GPUs) */
    bool carefulSet;    /* the start-up set under RdPipelineSetPolicy's careful values */
    /* the pixel diet (docs/port/RENDER.md "The device profile"): alphaPlan
     * the alpha/blend planner (rd_alpha_plan.c rd__plan_screen_draw_alpha),
     * fxPlan the frame-size post sprites' plan (rd_fx_plan.c
     * rd__fx_plan_draw), fxDstLocal its destination read by local read
     * (fx_local_ps), vuPlanesFrac the VU pixel side drops the planes at a
     * fractional scale (rd_pixel_plan.c rd__vu_pixel_fs) */
    bool alphaPlan;
    bool fxPlan;
    bool fxDstLocal;
    bool vuPlanesFrac;
    /* the crack filler above 1x on by default (rd_crack_fill.c): off on a
     * tile-based GPU until measured; [dev] crack_fill over it */
    bool crackFill;
    /* rd_profile.c: the device class's name for the lines; per flag
     * (RD_PROFILE_*) whether a [dev] key set it and whether a self-test
     * turned it off (rd__profile_self_test_off) */
    char cls[96];
    uint16_t devSet, selfTestOff;
    char text[512]; /* the summary's words, as in force */
} RdProfile;

/* RdProfile's flags, for its marks (devSet, selfTestOff) */
enum {
    RD_PROFILE_FOG_LOCAL = 1u << 0,
    RD_PROFILE_DATE_STENCIL = 1u << 1,
    RD_PROFILE_STORE_PLAN = 1u << 2,
    RD_PROFILE_BLUR_FOLD = 1u << 3,
    RD_PROFILE_UINT_WORK = 1u << 4,
    RD_PROFILE_UINT_SCENE = 1u << 5,
    RD_PROFILE_ALPHA_PLAN = 1u << 6,
    RD_PROFILE_FX_PLAN = 1u << 7,
    RD_PROFILE_FX_DST_LOCAL = 1u << 8,
    RD_PROFILE_VU_PLANES = 1u << 9,
};

/* fills g_rd.profile from the device (rd_profile.c); called by rd_init after
 * rd__gpu_init, before the self-tests; logs "rd: profile: ..." */
void rd__profile_init(void);
/* the profile's words for the summary line ("tile-based GPU (Imagination,
 * Vulkan): fog local, DATE stencil, ...") */
const char *rd__profile_text(void);
/* A self-test that found its mechanism wrong on this device turns the
 * flag (one RD_PROFILE_* bit) off: the flag false, "(self-test)" after it
 * in the summary's words, and one line "rd: profile: <flag> off
 * (self-test)". */
void rd__profile_self_test_off(unsigned flag);

/* ------------------------------------------------------------ pipelines */
typedef enum RdVsId {
    RD_VS_SPRITE_UI = 0,
    RD_VS_SPRITE_WORLD,
    RD_VS_BLIT,
    RD_VS_BLEND_INT,
    /* the VU1 program shaders (vu_*.hlsl), group 1 = layoutVu */
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
    RD_VS_FX_RECT, /* fx_rect_vs, the fullscreen triangle at the sprite's Z */
    /* PRIM.AA1 lines and triangles, IcoSpriteAa1Vertex (the sprite vertex
     * and its coverage) */
    RD_VS_SPRITE_AA1_UI,
    RD_VS_SPRITE_AA1_WORLD,
    /* IcoSpriteStqVertex, screen prims with Q != 1 (sprite_stq_*_vs) */
    RD_VS_SPRITE_STQ_UI,
    RD_VS_SPRITE_STQ_WORLD,
    RD_VS_CRT, /* crt_vs (the CRT filter), the fullscreen triangle with 0..1 across the
                  viewport */
    /* IcoSpritePlaneVertex, screen-prim triangles with their planes
     * (rd_screen_planes.c): sprite_plane_*_vs, sprite_stq_plane_*_vs */
    RD_VS_SPRITE_PLANE_UI,
    RD_VS_SPRITE_PLANE_WORLD,
    RD_VS_SPRITE_STQ_PLANE_UI,
    RD_VS_SPRITE_STQ_PLANE_WORLD,
    /* the vertex pre-pass's entries (vu_X_pre_vs), in the order of
     * RD_VS_VU_PRELIT .. RD_VS_VU_GRID_SPEC (rd__vu_pre_vs): one variant,
     * group 1 = layoutVu, drawn with vu_pre_ps into the record's three
     * targets (rd_pipeline.c rd__vu_pre_key) */
    RD_VS_VU_PRE_PRELIT,
    RD_VS_VU_PRE_LIT,
    RD_VS_VU_PRE_LIT_SPEC,
    RD_VS_VU_PRE_REFLECT,
    RD_VS_VU_PRE_SKIN,
    RD_VS_VU_PRE_SKIN_SPEC,
    RD_VS_VU_PRE_SKIN_DEBUG,
    RD_VS_VU_PRE_GRID,
    RD_VS_VU_PRE_GRID_LIT,
    RD_VS_VU_PRE_GRID_SPEC,
    /* (docs/port/RENDER.md, the per-triangle record stage): the ids from
     * RD_VS_OPTIONAL_FIRST on are vertex shaders whose module may be missing
     * from the table; a missing module leaves its id empty and is not an
     * init failure (rd_replay.c rd__gpu_init) */
    RD_VS_VU_MAIN,           /* vu_main_vs: every mesh and grid draw from the records */
    RD_VS_VU_PARTICLE_MAIN,  /* vu_particle_main_vs: the sprites from their records */
    RD_VS_VU_TRI_RULE_PRE,   /* vu_tri_rule_pre_vs: one point a triangle: the verdict, STQ plane */
    RD_VS_VU_TRI_TICK_PRE,   /* vu_tri_tick_pre_vs: one tick camera's test a triangle */
    RD_VS_VU_TRI_PLANES_PRE, /* vu_tri_planes_pre_vs: the colour planes a triangle */
    RD_VS_VU_PARTICLE_PRE,   /* vu_particle_pre_vs: one point a sprite: verdict and planes */
    RD_VS_COUNT
} RdVsId;

/* the VU program entries (the fallback's, docs/port/RENDER.md "The fallback
 * entries"; the ten mesh entries have a plain variant, g_rd.vsFallback) */
#define RD_VS_VU_FIRST RD_VS_VU_PRELIT
#define RD_VS_VU_LAST RD_VS_VU_PARTICLE
#define RD_VS_VU_PRE_FIRST RD_VS_VU_PRE_PRELIT
#define RD_VS_VU_PRE_LAST RD_VS_VU_PRE_GRID_SPEC
#define RD_VS_OPTIONAL_FIRST RD_VS_VU_MAIN

/* a mesh program entry's pre-pass entry (RD_VS_VU_PRELIT ..
 * RD_VS_VU_GRID_SPEC), 0 for any other vertex shader (the particles have
 * no pre-pass) */
static inline uint8_t rd__vu_pre_vs(uint8_t vs)
{
    return vs >= RD_VS_VU_PRELIT && vs <= RD_VS_VU_GRID_SPEC
               ? (uint8_t)(RD_VS_VU_PRE_PRELIT + (vs - RD_VS_VU_PRELIT))
               : 0u;
}

static inline bool rd__vs_is_vu_pre(uint8_t vs)
{
    return vs >= RD_VS_VU_PRE_FIRST && vs <= RD_VS_VU_PRE_LAST;
}

/* the record path's point stages (vu_tri_rule_pre_vs .. vu_particle_pre_vs:
 * points into the record's three RGBA32_UINT targets through vu_pre_ps, as
 * the vertex pre-pass's entries; rd__vu_stage_key) */
static inline bool rd__vs_is_vu_stage(uint8_t vs)
{
    return vs >= RD_VS_VU_TRI_RULE_PRE && vs <= RD_VS_VU_PARTICLE_PRE;
}

/* the record path's two drawing vertex shaders (vu_main_vs,
 * vu_particle_main_vs): group 1 = layoutVu, no vertex input, as the
 * program entries */
static inline bool rd__vs_is_vu_main(uint8_t vs)
{
    return vs == RD_VS_VU_MAIN || vs == RD_VS_VU_PARTICLE_MAIN;
}

typedef enum RdFsId {
    RD_FS_SPRITE = 0,
    RD_FS_BLIT,
    RD_FS_BLEND_INT,
    RD_FS_DATE_SNAP,    /* destination alpha MSB into the R8 DATE snapshot */
    RD_FS_CAMERA_PROBE, /* FrameCB matrices applied to a point, as bytes (tests) */
    RD_FS_VU,           /* vu_ps, the pixel side of every VU draw (VuVSOut) */
    RD_FS_FOG,          /* fog_lut_ps, RD_POST_FOG behind the sprite vertex shaders */
    RD_FS_FX_SPRITE,    /* fx_sprite_ps, staticBlur.c's sprites in GS integers */
    /* COLCLAMP 0 screen prims (rd_replay.c doScreenWrap, raw_wrap.hlsl) */
    RD_FS_WRAP_ACC,     /* wrap_acc_ps: the blend terms into an RGBA16F accumulator */
    RD_FS_WRAP_RESOLVE, /* wrap_resolve_ps: (Cd + acc) mod 256 into the target */
    RD_FS_FONT,         /* font_ps, screen prims sampling an R8 coverage texture */
    /* font_sheet_ps, screen and overlay prims sampling a sheet texture
     * (RD_TEXEL_SHEET); DrawCB.param holds its style */
    RD_FS_FONT_SHEET,
    RD_FS_SPRITE_AA1, /* sprite_aa1_ps, sprite_ps with the coverage as As */
    RD_FS_BOX_REDUCE, /* box_reduce_ps, the shadow count at the GS size */
    RD_FS_SPRITE_STQ, /* sprite_stq_ps, sprite_ps dividing S and T by Q per pixel */
    /* the CRT filter (rd_crt.c): CrtCB in DrawCB's slot */
    RD_FS_CRT_BLOOM, /* crt_bloom_ps: half size, linear, horizontal Gaussian (RGBA16F) */
    RD_FS_CRT_BLUR,  /* crt_blur_ps: the vertical Gaussian (RGBA16F) */
    RD_FS_CRT,       /* crt_ps: the box of the output */
    /* TEXA per texel before the bilinear filter, for a 24- or
     * 16-bit texture under AEM with a linear filter (rd__texa_per_texel) */
    RD_FS_SPRITE_TEXA, /* sprite_texa_ps */
    RD_FS_VU_TEXA,     /* vu_texa_ps */
    /* blit_depth_ps, the present's box blit with the scene's
     * depth into the output-size effects depth (rd_present.c) */
    RD_FS_BLIT_DEPTH,
    /* fog_lut_buffer_ps: fog_lut_ps reading the depth's bits from an
     * R32_UINT texture (RD_FOG_BUFFER, rd_fog_path.c) */
    RD_FS_FOG_BUFFER,
    /* aa_reduce_ps: supersampling, DISPLAY box-averaged to the present box
     * (rd_present.c) */
    RD_FS_AA_REDUCE,
    /* MSAA (rd_replay.c, the MSAA epoch): fog_lut_ms_ps, the fog per sample
     * reading the multisampled depth (created only with sample-rate
     * shading); depth_resolve_ps and depth_upsample_ps, the scene depth
     * between its multisampled and single-sampled textures */
    RD_FS_FOG_MS,
    RD_FS_DEPTH_RESOLVE,
    RD_FS_DEPTH_UPSAMPLE,
    /* vu_pre_ps: the vertex pre-pass's records into its three RGBA32_UINT
     * targets (rd_replay_vu.c rd__vu_prepass) */
    RD_FS_VU_PRE,
    /* From RD_FS_OPTIONAL_FIRST on a module missing from the table leaves
     * its id empty (as the vertex ids from RD_VS_OPTIONAL_FIRST) */
    RD_FS_SPRITE_PLANES,      /* sprite_ps_planes: the screen-prim triangles' plane build */
    RD_FS_SPRITE_TEXA_PLANES, /* sprite_texa_ps_planes */
    RD_FS_SPRITE_STQ_PLANES,  /* sprite_stq_ps_planes */
    RD_FS_FOG_LOCAL,          /* fog_lut_local_ps: the depth as an input attachment (local read) */
    RD_FS_FX_SPRITE_UINT,     /* fx_sprite_u_ps: the work targets through their UINT view */
    RD_FS_FX_BLUR2,           /* fx_blur2_ps: a staticBlur pair folded into one pass */
    RD_FS_DATE_CLEAR,         /* date_clear_ps: DATE's stencil bit cleared over the draw */
    RD_FS_DATE_STENCIL,       /* date_stencil_ps: DATE's test from the attachment into the bit */
    /* the pixel side at a fractional scale (rd_pixel_plan.c rd__vu_pixel_fs):
     * vu_ps and vu_texa_ps without the planes' inputs (ICO_VU_NO_PLANES),
     * each with its *_nodual twin (fsNoDual) and no exact-blend twins */
    RD_FS_VU_NP,      /* vu_np_ps */
    RD_FS_VU_TEXA_NP, /* vu_texa_np_ps */
    /* the frame-size post sprites (rd_fx_plan.c rd__fx_plan_draw), no
     * twins as fx_sprite_ps: fx_frame_ps its constant, alpha-only and
     * premultiplied forms (ICO_FX_FRAME; RD_FXF_ALPHA_ONLY,
     * RD_FXF_OUT_PREMUL), fx_local_ps the destination as an input
     * attachment by local read (ICO_FX_DST_LOCAL; layoutLocal,
     * rd__fs_local_read) */
    RD_FS_FX_FRAME,
    RD_FS_FX_LOCAL,
    /* Motion blur: Smooth (rd_blur_smooth.c), behind blit_vs, each texel
     * by Load: fx_acc_ps the accumulator's step A = S + a' (A - S) into
     * the other RGBA16F texture of the pair, fx_acc_resolve_ps the
     * accumulator rounded into an RGBA8 target the sprite reads; their
     * pipelines a private cache of two (RD_BLUR_SMOOTH_PIPES) */
    RD_FS_FX_ACC,
    RD_FS_FX_ACC_RESOLVE,
    /* the crack filler above 1x (rd_crack_fill.c), behind blit_vs, each
     * texel by Load: crack_fill_ps the pixels no draw covered between two
     * drawn ones in SCENE given the nearer one's colour and depth
     * (SV_Depth), crack_count_ps the pixels it fills counted a 16 x 16
     * block a texel; keys rd__crack_fill_key, in the fog's family */
    RD_FS_CRACK_FILL,
    RD_FS_CRACK_COUNT,
    RD_FS_COUNT
} RdFsId;

#define RD_FS_OPTIONAL_FIRST RD_FS_SPRITE_PLANES

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
    /* RhiDstRead: the exact blend's destination read (rd_dst_read.c); the
     * fragment entry is its _dstrov or _dstatt twin, blending off */
    uint8_t dst;
} RdPipeKeyInt;

/* One hardware draw of a screen-prim command (the AFAIL split makes two). */
typedef struct RdDrawPass {
    RdPipeKeyInt key;
    uint32_t flags; /* DF_* */
    uint32_t modeZ; /* atst | ate << 8 | split << 16 */
    uint32_t aref;
    uint32_t fix; /* FIX as the shader gets it (clamped where the path needs it) */
    /* the exact blend (rd_dst_read.c): FIX as the GS has it (never
     * clamped), and whether the fixed-function blend of this pass is not
     * the GS's (dstWant: a LERP, Cd*FIX + Cs, an Ad form, Cd*As + Cd, PABE
     * on a premultiplied form, COLCLAMP 0) */
    uint32_t fixGs;
    uint8_t dstWant;
    /* a pass without GS blending drawn through a destination-read shader
     * by the storage pass merge (rd__dst_merge): the shader's ALPHA
     * register is Cs (0x00: (Cs - Cs) * As >> 7 + Cs), not the state's */
    uint8_t csOnly;
} RdDrawPass;

RdBlendPath rd__blend_path(uint8_t keyBlend);
uint32_t rd__alpha_register(uint8_t blend);
/* Plans the hardware draws for a screen-prim command under state s.  prim is
 * the topology class (RD_PRIM_TRIANGLES or RD_PRIM_LINES).  Returns 0, 1 or
 * 2 passes. */
int rd__plan_screen_draw(const RdStateBlock *s, uint8_t prim, uint8_t space, RhiFormat colorFmt,
                         RhiFormat depthFmt, RdDrawPass out[2]);
/* The same for a command PRIM.AA1 antialiases (aa1 1: a line or triangle
 * command under s->aa1; rd_pipeline.c says what changes).
 * rd__plan_screen_draw is aa1 0. */
int rd__plan_screen_draw_ex(const RdStateBlock *s, uint8_t prim, int aa1, uint8_t space,
                            RhiFormat colorFmt, RhiFormat depthFmt, RdDrawPass out[2]);

/* ------------------------------------------------ the alpha/blend planner
 * rd_alpha_plan.c (RdProfile.alphaPlan).  RdAlphaRange: the source alpha
 * As a draw's pixels can take before the alpha test, lo..hi inclusive
 * (0..255; [0, 255] decides nothing).  RdAlphaSrc: what the draw's alpha
 * comes from, as its call site has it: kind RD_ALPHA_SRC_VU (rd_replay_vu.c
 * doVuDraw: the command f/c of type cmdType, RDC_MESH, RDC_SKINNED, RDC_GRID
 * or RDC_PARTICLES; its mesh, NULL but for RDC_MESH and RDC_SKINNED; its
 * VuCB with the VU memory; the program RdVuPayload.prog) or
 * RD_ALPHA_SRC_SCREEN (rd_replay.c doScreen: the command f/c and its n
 * vertices v as the replay draws them).  The state s gives the texture
 * (s->tex, s->ds.texEnabled), TEXA, TFX and TCC.
 * rd__alpha_range_of_draw  the range of a draw into *out
 * rd__plan_screen_draw_alpha  passes out[0 .. *n) planned for s (by
 *                          rd__plan_screen_draw / _ex, before the VU
 *                          draw's shader ids) re-planned for the range as:
 *                          true when it changed them (*n may change, to 0,
 *                          1 or 2), false when they stay as they are */
typedef struct RdAlphaRange {
    uint8_t lo, hi;
} RdAlphaRange;

enum { RD_ALPHA_SRC_VU = 0, RD_ALPHA_SRC_SCREEN = 1 };
struct IcoVuCB;

typedef struct RdAlphaSrc {
    uint8_t kind; /* RD_ALPHA_SRC_* */
    uint8_t prog; /* RD_ALPHA_SRC_VU: RdVuPayload.prog */
    uint16_t cmdType;
    const RdFrame *f;
    const RdCmd *c;
    const RdMeshRec *mesh;
    const struct IcoVuCB *vcb;
    const RdScreenVtx *v;
    uint32_t n;
} RdAlphaSrc;

void rd__alpha_range_of_draw(const RdStateBlock *s, const RdAlphaSrc *src, RdAlphaRange *out);
bool rd__plan_screen_draw_alpha(const RdStateBlock *s, RdAlphaRange as, RdDrawPass out[2], int *n);

/* ------------------------------------------------ the VU pixel side
 * rd_pixel_plan.c (RdProfile.vuPlanesFrac).
 * rd__vu_pixel_fs       the fragment entry of a VU draw pass p under state
 *                       s into colour target tc: today (RD_FS_VU or
 *                       RD_FS_VU_TEXA, vuDraw's choice) or its twin
 *                       without the planes (RD_FS_VU_NP, RD_FS_VU_TEXA_NP;
 *                       their *_nodual twins follow key.gs.nodual).  The
 *                       twins have no exact-blend twins: under an exact
 *                       blend (g_rd.dstMode) it returns today
 * rd__vu_pixel_fs_enum  after each VU drawing key k of the start-up set
 *                       (rd_mesh.c enumerateVuDraws): the twin keys the
 *                       current scale can draw, added to out (at most max),
 *                       n the count so far; returns the new count */
RdFsId rd__vu_pixel_fs(RdFsId today, const RdStateBlock *s, const RdTargetRec *tc,
                       const RdDrawPass *p);
/* Whether a draw under state s into target tc drops the attribute planes:
 * RdProfile.vuPlanesFrac, Enhanced, tc's scale not a whole number on both
 * axes, and neither both filters nearest nor a texture with GS levels (the
 * rule rd__vu_pixel_fs and rd__screen_planes_want apply) */
bool rd__planes_dropped(const RdStateBlock *s, const RdTargetRec *tc);
uint32_t rd__vu_pixel_fs_enum(RdPipeKeyInt *out, uint32_t max, uint32_t n, const RdPipeKeyInt *k);
/* An alpha-tested draw with Z write off whose failing fragments write what
 * its passing ones write (AFAIL FB_ONLY, or RGB_ONLY with the alpha channel
 * masked) is one draw without the test, every fragment in the GS's
 * primitive order (rd_pipeline.c rd__plan_screen_draw_ex).  On by default;
 * off plans two draws, the passing fragments' and then the failing ones'
 * (tests: the same frame both ways). */
void rd__set_alpha_merge(bool on);
/* The two-pass blend for a device without dual-source
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
int rd__expand_no_dual(const RdDrawPass *in, int n, RdDrawPass out[4]);
/* Whether ex[i] of an expansion is the alpha pass after a colour pass (the
 * draw counters count the pair once). */
bool rd__no_dual_second(const RdDrawPass *ex, int i);
/* Whether fragment entry fs has a *_nodual twin (it calls gs_dual_out). */
bool rd__fs_has_no_dual(uint8_t fs);

/* ------------------------------------------------ the exact blend
 * rd_dst_read.c (the design is at its top).  rd__dst_init at rd_init
 * sets g_rd.dstMode (RdBlendExact): the fixed-function blend unless
 * ICO_RD_BLEND ([dev] blend) opted into an exact mode the device has, and
 * logs it.  rd__dst_target_usage: the RhiTextureUsage bits a colour
 * target of format fmt is made with (the opted-in destination read, DATE's
 * local read) and *dstUsage its RdTargetRec.dstUsage (RD_DST_USAGE_*).
 * rd__fs_has_dst: whether entry fs
 * has the _dstrov / _dstatt twins (the gs_dual_out entries).
 * rd__expand_dst: the passes in[0..n) as drawn under destination read
 * mode (RdBlendExact; RD_BLEND_EXACT_OFF copies them), into out (at most 2
 * per pass: give out[4] for 2): a pass with dstWant on an RGBA8 target
 * whose entry has the twins and whose mask writes RGB becomes a
 * destination-read pass (key.dst, blending off, FIX unclamped, the write
 * mask in DF bits 19..22); under the storage mode a pass that both writes
 * Z and can discard (alpha test, DATE) is drawn as that colour pass
 * without Z write and then a depth-only pass when its Z test is ALWAYS or
 * NEVER, else left on the fixed-function blend (logged once).  Returns the
 * number of passes written. */
void rd__dst_init(void);
/* The mode ICO_RD_BLEND asked for when the device has it (kept by
 * rd_blend_selftest whatever its result), -1 when none was asked. */
int rd__dst_asked(void);
/* ICO_RD_BLEND ([dev] blend) as read before the modules exist (rd__gpu_init
 * makes only the asked mode's layout and twins): RD_BLEND_EXACT_OFF for
 * fixed (off, 0), RD_BLEND_EXACT_STORAGE for storage (interlock, rov),
 * RD_BLEND_EXACT_ATTACHMENT for attachment, the device's best exact mode for
 * exact (the attachment read, else the storage read: needs rhi_init), -1
 * when unset or another word.  Whether the device has it is rd__dst_init's
 * check. */
int rd__dst_asked_env(void);
/* rd__fog_scene_remade: a new SCENE size runs the blend's self-test again
 * once rd_blend_selftest has run; rd__gpu_shutdown frees its objects. */
void rd__blend_scene_remade(void);
void rd__blend_selftest_shutdown(void);
uint32_t rd__dst_target_usage(RhiFormat fmt, uint8_t *dstUsage);
bool rd__fs_has_dst(uint8_t fs);
int rd__expand_dst(const RdDrawPass *in, int n, int mode, RdDrawPass out[4]);
/* The storage pass merge (issue 103; g_rd.passMerge): inside a storage pass
 * (the storage mode's pass without the colour attached), a pass that
 * rd__expand_dst leaves on the fixed-function blend is drawn through the
 * destination-read shader instead when the shader stores the same bytes,
 * so the pass need not end and begin again with the colour attached: no
 * GS blending (written as Cs, RdDrawPass.csOnly) or the premultiplied add
 * (Cs*FIX + Cd, Cs*As + Cd: the blender adds two whole levels, exact), on
 * an RGBA8 target whose entry has the twins, the mask writing RGB, no
 * stencil, single-sampled, and not a pass that writes Z and can discard
 * (the storage pass tests Z before the shader: rd__expand_dst's split).
 * The premultiplied subtract stays fixed-function: the shader's term rounds
 * Cs*F toward minus infinity (the GS's), the blender path subtracts the
 * truncated Cs*F, one level apart wherever Cs*F is not a multiple of 128.
 * rd__dst_merge_ok says whether p qualifies; rd__dst_merge its image. */
bool rd__dst_merge_ok(const RdDrawPass *p);
RdDrawPass rd__dst_merge(const RdDrawPass *p);
/* The keys the exact blend adds to a reachable set: for each of in[0..n)
 * its rd__expand_dst images under every mode the device has (a pass whose
 * flags and alpha test the key does not keep is taken both ways), added
 * to out (rd__add_pipe_key); returns the new count. */
uint32_t rd__enumerate_dst(const RdPipeKeyInt *in, uint32_t n, RdPipeKeyInt *out, uint32_t max,
                           uint32_t count);
const char *rd__blend_exact_name(int mode);
/* Turns a planned pass of a command whose prims carry Q != 1
 * into the STQ shaders (IcoSpriteStqVertex); 0 when the pass keeps its own. */
int rd__stq_pass(RdDrawPass *dp);

/* The exact viewport's planes, rd_screen_planes.c.  rd__plane_pass: converts a
 * planned screen pass of sprite_ps, sprite_texa_ps or sprite_stq_ps to the
 * vertex shader of its kind that reads the triangles' planes; returns
 * whether it did (the other fragment entries keep theirs).
 * rd__screen_planes: the triangle list vb (n vertices, stride
 * sizeof(IcoSpriteVertex) or sizeof(IcoSpriteStqVertex)) with each
 * triangle's colour and STQ planes per GS pixel, in a buffer of the file's
 * kept until the next call; NULL when it cannot be had. */
int rd__plane_pass(RdDrawPass *dp);
const struct IcoSpritePlaneVertex *rd__screen_planes(const void *vb, uint32_t stride, uint32_t n);
/* Whether doScreen gives a triangle command under state s into target tc,
 * planned as dp, its planes: not where the planes are dropped
 * (rd__planes_dropped; its STQ passes keep them) */
int rd__screen_planes_want(const RdStateBlock *s, const RdTargetRec *tc, const RdDrawPass *dp);
/* Whether a draw under this state samples a PSMCT24 or
 * PSMCT16 texture (or one with a 24- or 16-bit CLUT: RdTexRec.src not
 * RGBA32) under a TEXA with AEM through a linear MAG or MIN filter, the
 * draws whose bilinear edges depend on TEXA coming before the filter; the
 * planners give them sprite_texa_ps / vu_texa_ps. */
int rd__texa_per_texel(const RdStateBlock *s);
/* Whether a draw under this state samples GS levels at the GS's LOD in the
 * Original preset with a linear filter, filtered as the GS filters
 * (rd_pipeline.c): through the TEXA entries' integer filter where
 * rd__texa_per_texel, else the sampler on the GS's grid
 * (ICO_DF_GS_BILINEAR_GRID). */
int rd__gs_filter_int(const RdStateBlock *s);
RdPipeKeyInt rd__post_key(RdVsId vs, RdFsId fs, RhiFormat colorFmt);
/* The present's box blit with the effects depth: blit_vs and
 * blit_depth_ps into colorFmt (the headless output's RGBA8, the swapchain's
 * BGRA8) and an RHI_FMT_D32F depth target, Z written, test ALWAYS. */
RdPipeKeyInt rd__present_depth_key(RhiFormat colorFmt);
/* The pipeline for k, created on first use.  0 without a device. */
RhiPipeline rd__get_pipeline(const RdPipeKeyInt *k);
/* Binds p and sets the dynamic state the key asks for where the device
 * left it dynamic (RhiLimits.dynamicDepth / dynamicBlend: the depth test,
 * write and compare, the blend and the colour mask), from the
 * UN-normalised key k, the one rd__get_pipeline was given (NULL: the bind
 * alone).  Every draw binds through this, never through
 * rhi_cmd_set_pipeline directly (rd_pipeline.c). */
void rd__set_pipeline(RhiCommandList cl, RhiPipeline p, const RdPipeKeyInt *k);
/* The dynamic state's key normalisation (docs/port/RENDER.md "Pipelines"):
 * with g_rd.dynDepth, a key with a depth attachment and no stencil mode
 * gets ztst ALWAYS and zwrite OFF; with g_rd.dynBlend, a key with one
 * colour target and no destination read (not a point stage) gets blend
 * RD_BLEND_COUNT and colour mask 0xF.  Those fields then come from
 * the draw (rd__set_pipeline), so keys that differed only in them share one
 * pipeline.  rd__get_pipeline (drawKey) and rd__enumerate_reachable apply
 * it; rd__key_dynamic_bits says which groups a key leaves dynamic (the
 * same conditions, RHI_DYN_*). */
void rd__key_dynamic_normalise(RdPipeKeyInt *k);
uint32_t rd__key_dynamic_bits(const RdPipeKeyInt *k);
/* the key's words ("program 0 blend 4 vs 5 fs 5 fmt 1/8 msaa 0 dst 0") */
void rd__pipe_key_text(const RdPipeKeyInt *k, char *out, size_t size);

/* The family of a key, for the start-up set's count (rd_pipeline.c
 * rd__pipe_key_family): the first that holds of DATE (a DATE stencil
 * mode), a VU point entry (pre-pass, stage), a VU drawing entry, shadow
 * (the volume's counting stencil or the box reduction), fog (the fog's
 * fragment programs and the depth resolves), CRT, fx (staticBlur's and the
 * post sprites' programs), else 2D (the screen prims, the blits and the
 * water's states, which are screen prims). */
typedef enum RdKeyFamily {
    RD_KF_2D = 0,
    RD_KF_VU,
    RD_KF_VU_POINT,
    RD_KF_SHADOW,
    RD_KF_FOG,
    RD_KF_CRT,
    RD_KF_FX,
    RD_KF_DATE,
    RD_KF_COUNT
} RdKeyFamily;

RdKeyFamily rd__pipe_key_family(const RdPipeKeyInt *k);
const char *rd__pipe_family_name(RdKeyFamily f);
/* counts[family] over the first min(n, max) keys; returns their sum */
uint32_t rd__pipe_family_counts(const RdPipeKeyInt *keys, uint32_t n, uint32_t max,
                                uint32_t counts[RD_KF_COUNT]);
/* The crash report's note (rd_set_activity_fn) around a pipeline made
 * outside rd__get_pipeline: begin sets "creating pipeline <what>" (a
 * graphics driver crash then counts as one while preparing the shaders,
 * diag_host.h), end restores the note rd__get_pipeline would leave. */
void rd__pipeline_note_begin(const char *what);
void rd__pipeline_note_end(void);
void rd__pipeline_cache_clear(void);
/* Gathering (the loss bisect, rd_dump.c rd_loss_bisect): on, rd__get_pipeline
 * creates nothing for a key it does not hold, notes the key (as the pass's
 * sample count made it, each once) and returns 0, so a replay made
 * meanwhile names the pipelines it needs without compiling them on the
 * calling thread.  rd__pipeline_create_gathered turns gathering off and
 * creates the noted keys through rhi_create_pipelines in batches, with the
 * progress function (rd_set_pipeline_progress) as the start-up set does;
 * returns the count noted, *stopped when the progress function stopped it.
 * on clears the notes. */
void rd__pipeline_gather(bool on);
/* whether rd__get_pipeline is only naming keys (rd__pipeline_gather):
 * a key it returns no pipeline for is not missing (rd_replay.c keeps the
 * vertex pre-pass's choices, which would otherwise fall back) */
bool rd__pipeline_gathering(void);
uint32_t rd__pipeline_create_gathered(bool *stopped);
/* the keys the gathering replay has named so far, each once, as
 * rd__get_pipeline made them (copied to out, at most max); returns how many
 * (rd_replay_tool --keys reads them before rd__pipeline_create_gathered
 * clears them) */
uint32_t rd__pipeline_gathered(RdPipeKeyInt *out, uint32_t max);
uint32_t rd__pipeline_count(void);
/* The time the pipelines made at first use took, in all, ms
 * (rd__get_pipeline; the replay's walk breakdown). */
double rd__pipeline_create_ms(void);
/* The draw list the replay is walking (0 .. RD_LIST_COUNT - 1), -1 outside
 * the lists (the vertex pre-pass, the present, outside a replay) (rd_perf.c,
 * from the lists' timestamps and rd__passring_list). */
int rd__replay_list(void);
/* the VU vertex pre-pass is being recorded (rd_replay_vu.c rd__vu_prepass;
 * the first-use line names it) */
bool rd__vu_prepass_running(void);
/* rd_pipelines_lazy was called (the summary's pipelines part) */
bool rd__pipelines_lazy_on(void);
const RdPipeKeyInt *rd__pipeline_key_at(uint32_t i);
/* The pipelines the implemented programs can reach from the game's state
 * set (rd_pipeline.c lists the families and their sources).  Writes up to
 * max keys, returns the total. */
uint32_t rd__enumerate_reachable(RdPipeKeyInt *out, uint32_t max);
/* Adds k to out[0 .. n) unless an equal key is there; returns the new count.
 * Past max the key is not stored but still counted, so the total is the
 * set's size however small out is.  Every rd__enumerate_reachable* family
 * adds its keys through it. */
uint32_t rd__add_pipe_key(RdPipeKeyInt *out, uint32_t max, uint32_t n, const RdPipeKeyInt *k);
/* The screen and post families alone (rd_state_test holds them under 400,
 * and the whole set under RD_PIPELINE_REACHABLE_MAX). */
uint32_t rd__enumerate_reachable_screen(RdPipeKeyInt *out, uint32_t max);
/* The VU program families (rd_mesh.c): with the vertex pre-pass the record
 * path's two drawing shaders under the states the mesh lists draw with and
 * the ten pre-pass and five stage keys; without it the eleven program
 * entries with the legacy pixel shaders.  Appends to out[0..n). */
uint32_t rd__enumerate_reachable_vu(RdPipeKeyInt *out, uint32_t max, uint32_t n);
/* The VU modules (docs/port/RENDER.md, "The fallback entries"),
 * rd_pipeline.c.
 * rd__vu_policy_init  at rd_init, after the shaders: which program entries
 *                     the fallback draws through (g_rd.vsFallback: the
 *                     plain ones on a device of the table,
 *                     RhiLimits.vuFallbackPlain, with ICO_RD_VU_PLAIN=1 or
 *                     after rd_set_vu_plain_only; the extended ones
 *                     elsewhere or with ICO_RD_VU_EXTENDED=1; their modules
 *                     made then), the dynamic state in force (g_rd.dynDepth,
 *                     dynBlend from RhiLimits); logs the device's compiler
 *                     line, the modules and the dynamic state, once per
 *                     rd_init.  false only without a device's limits.
 * rd__shader_words    a module's size in 32-bit words (its SPIR-V), 0 when
 *                     the table has no such module
 * rd__shader_instructions  its SPIR-V instruction count (the header's five
 *                     words left out), 0 when there is none */
bool rd__vu_policy_init(const RhiLimits *lim);
/* The vertex pre-pass (docs/port/RENDER.md, "The vertex pre-pass").
 * rd__vu_prepass_init  at rd_init, after the shaders (rd_replay_vu.c): whether
 *                     it draws (g_rd.vuPrepass: ICO_RD_VU_PREPASS=0, [dev]
 *                     vu_prepass = false, keeps it off, as does a device
 *                     whose colour targets cannot hold the record,
 *                     RhiLimits.colorBytesPerPixel), logged once
 * rd__set_vu_prepass  the tests' switch: on (where the device takes it) or
 *                     off from the next replay, the start-up set's keys
 *                     following it
 * rd__vu_pre_key      the pipeline key of a pre-pass entry (RD_VS_VU_PRE_*;
 *                     rd_pipeline.c): vu_pre_ps into the three
 *                     RGBA32_UINT targets, no depth, points, no blend */
void rd__vu_prepass_init(const RhiLimits *lim);
void rd__set_vu_prepass(bool on);

/* rd_vuprepass_selftest's sabotage (ICO_RD_VU_PREPASS_SABOTAGE), while the
 * mode is not RD_VU_SABOTAGE_OFF: VERTEX the first mesh or grid command's
 * vertex records drawn one texel past where the main shader reads them,
 * TRI its triangles' slot A one slot past, TICKS every tick stage's tick 1
 * tested at tick 0's camera */
enum { RD_VU_SABOTAGE_OFF, RD_VU_SABOTAGE_VERTEX, RD_VU_SABOTAGE_TRI, RD_VU_SABOTAGE_TICKS };

void rd__vu_prepass_sabotage(int mode);
RdPipeKeyInt rd__vu_pre_key(uint8_t preVs);
/* The key of a record stage's point draw (rd_replay_vu.c, pass V's particle
 * stage and pass T; docs/port/RENDER.md "The vertex and triangle
 * pre-passes"): vs one of RD_VS_VU_TRI_RULE_PRE .. RD_VS_VU_PARTICLE_PRE,
 * rd__vu_pre_key's fields (vu_pre_ps into the three RGBA32_UINT targets,
 * points, no depth, no blend) with colour mask colorMask on each target
 * (0xF; the tick stage 0x1 or 0x2, its R or G).  rd__enumerate_reachable_vu
 * holds the five the draws ask for: rule 0xF, tick 0x1, tick 0x2, planes
 * 0xF, particle 0xF. */
RdPipeKeyInt rd__vu_stage_key(uint8_t vs, uint8_t colorMask);
#define RD_HAVE_VU_STAGE_KEY 1
uint32_t rd__shader_words(const char *name);
uint32_t rd__shader_instructions(const char *name);

/* The last replay's DATE snapshots against its MSAA epoch (rd_core.c from
 * rd_replay.c's rd__date_snap, issue 106; the F12 water line): samples the epoch's count (0: no
 * MSAA), sceneSnaps SCENE's snapshots, closing the ones that closed the
 * epoch (their transition resolved it), afterResolve the ones that read
 * the resolved picture (those and the later ones), otherSnaps the
 * snapshots of other targets. */
typedef struct RdDateMsaa {
    uint32_t samples, sceneSnaps, closing, afterResolve, otherSnaps;
} RdDateMsaa;

void rd__date_msaa_last(RdDateMsaa *out);
/* The replay's diagnostic counts (rd_core.c, issues 95 and 106):
 * rd__diag_frame(NULL, 0) as a replay begins, (f, the MSAA epoch's sample
 * count, 1 without) as it ends; rd__span_idx the CPU index list of the VU
 * draw doVuDraw is on (NULL: none); rd__span_range instead the span of a
 * draw whose indices are not read back (a grid's, in the upload ring:
 * values lo..hi); rd__span_draw one indexed VU draw of
 * it (count 0: a second pass of the same draw, not counted), measured on
 * one replay in 64; rd__date_snap a DATE snapshot of target tc as the
 * replay takes it, closing when its transition closes the open MSAA epoch,
 * ended when the epoch was resolved before. */
void rd__diag_frame(const RdFrame *f, uint32_t samples);
void rd__span_idx(const uint32_t *idx);
void rd__span_range(uint32_t lo, uint32_t hi);
void rd__span_draw(uint32_t first, uint32_t count);
void rd__date_snap(const RdTargetRec *tc, bool closing, bool ended);
/* The (program, code) row of rd_mesh.h's table: the RdProg, the vertex
 * shader and the clip mode; false without a row. */
bool rd__vu_row(int program, int code, uint8_t *prog, uint8_t *vs, uint8_t *clip);
bool rd__pipe_key_equal(const RdPipeKeyInt *a, const RdPipeKeyInt *b);
/* The shadow count (rd_shadow.c): the volume pipeline under state s
 * (sprite_world_vs / sprite_ps, colour mask 0, the state's Z test without Z
 * write, stencil INCR_WRAP or, with decr, DECR_WRAP under write mask
 * RD_SHADOW_STENCIL_MASK, on a D32F_S8 depth target), and the resolve's
 * passes (blit_vs / blit_ps): pass k in 0..5 adds 4 << k to RGB where
 * stencil bit k is set, pass 6 writes A where the count is not 0. */
RdPipeKeyInt rd__shadow_volume_key(const RdStateBlock *s, RhiFormat colorFmt, int decr);
RdPipeKeyInt rd__shadow_resolve_key(int pass);
RdPipeKeyInt rd__shadow_reduce_key(void); /* box_reduce_ps: the count at the GS size */
#define RD_SHADOW_RESOLVE_PASSES 7
/* The shadow families above under the states the game draws them with
 * (Shadow.c: TEST 0x50000); appends to out[0..n). */
uint32_t rd__enumerate_reachable_shadow(RdPipeKeyInt *out, uint32_t max, uint32_t n);
/* The depth fog: the draws of an RD_POST_FOG under state s, as
 * rd__plan_screen_draw plans a fullscreen sprite without a depth attachment
 * (fog_lut_ps does the Z test against the depth it reads), with fog_lut_ps
 * as the fragment shader, fog_lut_buffer_ps for path RD_FOG_BUFFER, or
 * fog_lut_local_ps for RD_FOG_LOCAL with the scene depth attached (its test
 * ALWAYS, no write: the shader reads it as the input attachment). */
int rd__fog_plan(const RdStateBlock *s, RhiFormat colorFmt, int path, RdDrawPass out[2]);
/* The fog under ZFog.c's state (TEST 0x50000, ZMSK, ALPHA 0x44 with ABE),
 * with both fragment shaders, and the crack filler's two keys where its
 * shaders are made; appends to out[0..n). */
uint32_t rd__enumerate_reachable_fog(RdPipeKeyInt *out, uint32_t max, uint32_t n);
/* The crack filler's keys (rd_crack_fill.c), blit_vs into RGBA8: count 0
 * crack_fill_ps with the scene depth (D32F_S8) attached, its test ALWAYS
 * with writes (SV_Depth), no blend; count 1 crack_count_ps alone. */
RdPipeKeyInt rd__crack_fill_key(int count);
/* rd_replay.c: frees the fog's LUT texture (rd__gpu_shutdown); the fog samples
 * the depth target itself. */
void rd__fog_shutdown(void);
void rd__shadow_shutdown(void); /* frees the reduced shadow count */
/* staticBlur.c's sprites (rd_blur.c): the pipeline of an
 * RD_POST_MOTION_BLUR .. RD_POST_EYE_BLUR sprite under state s
 * (fx_rect_vs / fx_sprite_ps, no hardware blending, the
 * state's colour mask; with a depth format the state's Z test and Z write,
 * else none), whether it binds the depth target (*useDepth), and the
 * enumeration of the keys staticBlur.c's states reach. */
RdPipeKeyInt rd__blur_key(const RdStateBlock *s, RhiFormat colorFmt, RhiFormat depthFmt,
                          int *useDepth, int uintSrc);
uint32_t rd__enumerate_reachable_blur(RdPipeKeyInt *out, uint32_t max, uint32_t n);
/* rd_fx_plan.c: the frame-size post sprites' keys the profile can draw
 * (fx_frame_ps, fx_local_ps, the fixed-function forms), added after
 * rd__enumerate_reachable_blur's; returns the new count */
uint32_t rd__fx_enumerate(RdPipeKeyInt *out, uint32_t max, uint32_t n);
/* rd_fx_plan.c: fx_local_ps's self-test on SCENE where the profile has
 * fxDstLocal (rd_fog_selftest runs it after DATE's); its flag goes off on
 * a wrong pixel.  Waits for the GPU.  True when it passed or had nothing
 * to check. */
bool rd__fx_local_selftest(void);
/* The CRT filter's pipelines (the two RGBA16F glow passes, the
 * composite on the headless output and the swapchain) */
uint32_t rd__enumerate_reachable_crt(RdPipeKeyInt *out, uint32_t max, uint32_t n);

/* ------------------------------------------------- the CRT filter (rd_crt.c) */
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
bool rd__crt_preset(RdCrtMode mode, RdCrtParams *p);
/* The parameters s asks for: the mode's, with s's overrides. */
bool rd__crt_resolve(const RdSettings *s, RdCrtParams *p);
/* The mask's strength factor for a box boxH output pixels high: 1 from
 * 1080, 0 at 720 and below, linear between. */
float rd__crt_mask_fade(uint32_t boxH);
/* The phosphor mask, per output pixel (crt.hlsl maskOf is the same
 * function).  A source pixel is r output pixels wide (box width / grid
 * width); f is the output pixel's position across it and v across its line
 * (0..1, the pixel's centre), odd whether its source column is odd, gap the
 * mask strength (RdCrtParams.maskStrength).  Returns channel ch's weight:
 * 1 in its own stripe, 1 - gap (the leak) in the other two stripes and in a
 * gap, times 1 - gap on a slot's bridge.  rd__crt_gap_columns is the gap
 * columns a source pixel has room for (1 from r 4, 2 from r 6).
 * rd__crt_triad_gain (crt.hlsl triadGain) is 1 over the mean of channel ch's
 * stripe weight over the output columns whose centres fall in source pixel
 * sx (no curvature; the shader follows the curve), the mean floored at
 * RD_CRT_MEAN_MIN; rd__crt_row_gain is 1 over the slot bridges' mean over a
 * line (1 for the other masks): together the light each triad keeps. */
#define RD_CRT_MEAN_MIN 0.1f
uint32_t rd__crt_gap_columns(float r);
float rd__crt_mask_weight(int mask, float r, float gap, float f, float v, int odd, int ch);
float rd__crt_triad_gain(int mask, float r, float gap, int sx, float v, int ch);
float rd__crt_row_gain(int mask, float gap);
/* The highlights, as crt.hlsl draws them (the CPU side of the same maths,
 * for the tests).  rd__crt_strength_at: the mask's strength under a source
 * pixel p (linear), eased to half as its brightest channel goes from 0.5
 * to 1.  rd__crt_triad_top: channel ch's largest stripe weight over source
 * pixel sx's columns (1, or the leak in a triad short of its stripe).
 * rd__crt_gain_fade: the fraction t of the gains (gain[c], each channel's
 * triad and row gain) a pixel of colour col takes so that no channel's
 * brightest stripe, col (1 + fade (top (1 + t (gain - 1)) - 1)), passes 1;
 * one t for the three channels, so the hue holds.  rd__crt_glow_mix: halation (halo,
 * the wide glow) and bloom (glow, weighted by its luminance) mixed into
 * col in proportion, never added.  rd__crt_shoulder: a colour whose
 * brightest channel passes 1 - 0.1 st scaled as a whole, that channel
 * rolled off toward 1.  rd__crt_beam: a line of colour c (one channel,
 * linear) at d lines from its centre (crt.hlsl beamOf). */
float rd__crt_strength_at(float strength, const float p[3]);
float rd__crt_triad_top(int mask, float r, float gap, int sx, float v, int ch);
float rd__crt_gain_fade(const float col[3], const float gain[3], const float top[3], float fade);
void rd__crt_glow_mix(float col[3], const float halo[3], const float glow[3], float halation,
                      float bloom, float st);
void rd__crt_shoulder(float c[3], float st);
float rd__crt_beam(float c, float d, float beamMin, float beamMax);
/* True when the present draws the CRT filter (a mode, strength > 0). */
bool rd__crt_on(void);
/* ---------------------------------------------------- anti-aliasing
 * RdSettings.antiAlias as it is drawn (rd_core.c): RD_AA_OFF under the
 * Original preset, for an unknown value, and for an MSAA count outside
 * rd_aa_supported_mask(); else the setting. */
int rd__aa_mode(void);
/* 2 or 4 for the supersampling values, else 1 */
uint32_t rd__aa_ssaa_factor(int mode);
/* ", anti-aliasing <name>" for the display log line */
const char *rd__aa_label(int mode);
/* Tests: the fog inside the MSAA epoch reads the resolved depth (the epoch
 * closes) even where it could run per sample (rd_replay.c doFog). */
void rd__msaa_test_fog_resolved(bool on);
/* Frees the MSAA epoch's multisampled scene pair (rd_replay.c; remade at
 * the next replay that wants it). */
void rd__msaa_free(void);
/* The multisampled scene pair's budget on a tile-based GPU, in bytes
 * (rd__apply_display halves the scale until it fits). */
#define RD_MSAA_TILER_BUDGET (384ull << 20)
/* The filter's source grid: the PS2 picture's pixels, vw across and vh
 * lines (DISPLAY's, 512 with full_height); the overlay's layer is vw x gsH
 * (the frame's lines).  vw is the native frame's width less rd__crt_crop
 * columns at each side: the columns of its exact width (rd__frame_wexact)
 * in whole GS pixels, 512 at 4:3.  rd__crt_box: the filter's box in an
 * outW x outH output, of the grid's shape vw / 384 (the 4:3 box at 512),
 * so a source pixel spans as many output pixels as at 4:3 (the present's
 * box, rd_present.c outputBox, differs from it by less than a column). */
void rd__crt_grid(uint32_t *vw, uint32_t *vh);
uint32_t rd__crt_crop(void);
void rd__crt_box(uint32_t outW, uint32_t outH, RhiRect *box);
/* From rd__present_record, in place of the line doubling and
 * the box blit: disp (SHADER_READ), with the overlay drawn into the source
 * grid (rd__overlay_grid_draw), through the glow passes, then the composite
 * (the phosphors per output pixel) into box of out (cleared outside it),
 * out left in RENDER_TARGET.  overlay false: the grid without the overlay
 * (the photo capture's pass).  False when it could not draw
 * (no pipeline): the caller falls back to the box blit and draws the
 * overlay on the output. */
bool rd__crt_record(RhiCommandList cl, const RdTargetRec *disp, RhiTexture out, RhiFormat outFmt,
                    uint32_t outW, uint32_t outH, const RhiRect *box, int mirror, bool overlay);
/* The films under the filter.  rd__crt_film_grid is a film's
 * grid: the game's tube (its 512 triads across the 4:3 box, whatever the
 * frame's width: the films stay 4:3) and
 * the film's field lines in its PS2 display area (dispH / 2, every line
 * with full_height, as the game's).  rd__crt_record_film: pic (pw x ph, the
 * film in its display area, already mirrored, SHADER_READ), box-reduced
 * to the grid vw x vh, then as rd__crt_record from step 1 (no overlay: the
 * films draw none).  rd__crt_last_pass: the composites drawn so far and the
 * last one's grid (the tests). */
void rd__crt_film_grid(uint32_t dispH, uint32_t *vw, uint32_t *vh);
bool rd__crt_record_film(RhiCommandList cl, RhiTexture pic, uint32_t pw, uint32_t ph, uint32_t vw,
                         uint32_t vh, RhiTexture out, RhiFormat outFmt, uint32_t outW,
                         uint32_t outH, const RhiRect *box);
uint32_t rd__crt_last_pass(uint32_t *vw, uint32_t *vh);
/* The textures the filter keeps (the source, the overlay's layer, the glow
 * targets, the films' own) */
void rd__crt_shutdown(void);
/* rd_blur.c: records the sprite (rd_post's staticBlur.c kinds, and the
 * reduction's two sprites from rd_post.c). */
void rd__post_blur(RdPostKind kind, const RdPostParams *p);
/* The same with the sprite's two screen vertices, then the mirrored pair,
 * kept in the payload (RdPostRec.lutOffset) for rd__blur_screen_fallback. */
void rd__post_blur_verts(RdPostKind kind, const RdPostParams *p, const RdScreenVtx v[4]);
/* rd_blur.c: the payload offset of the screen vertices a recorded sprite is
 * drawn with instead of the model (the reduction on a scaled target), or
 * ~0u. */
uint32_t rd__blur_screen_fallback(uint32_t kind, const RdPostRec *p, const RdStateBlock *st);
void rd__blur_snap_verts(const RdStateBlock *st, RdScreenVtx v[2]);
/* rd_blur.c: the UV rectangle the replay draws a recorded sprite with: the
 * recorded one, except the reduction's with the mirror on, which samples
 * the mirror image of the unmirrored sample points. */
void rd__blur_uv_rect(uint32_t kind, const RdPostRec *p, float uv[4]);
/* rd_blur.c: the reduction's textured sprite p drawn again over its two
 * outer GS columns a side (rd_blur_border.c, with the border open at the
 * frame's exact width): out[0] covers GS pixels 0 and 1 and out[1] pixels
 * gsW - 2 and gsW - 1 with p's rows, each with the one U fx_sprite_ps
 * computes at pixel 2 (out[0]) or gsW - 3 (out[1]) from uv (p->uv as
 * recorded: rd__blur_uv_rect's mirror moves both ends of a strip's U as it
 * moves the sprite's), so each strip is that column again, bit for bit, in
 * the GS model.  False (out untouched) for a sprite that is not one over
 * the whole frame width with whole 12.4 corners and UVs, or gsW under 6. */
bool rd__blur_border_strips(const RdPostRec *p, const float uv[4], int32_t gsW, RdPostRec out[2]);
/* The same for the hardware sprite a scaled target draws instead
 * (rd__blur_screen_fallback's four vertices v: the plain pair, then the
 * mirrored one): out[0..3] the left strip's pairs, out[4..7] the right
 * one's, each pair's S constant, the S the pair has at the 12.4 coordinate
 * of pixel 2 or gsW - 3 (rd__blur_snap_verts then moves it as it moves the
 * sprite's) */
void rd__blur_border_verts(const RdScreenVtx v[4], int32_t gsW, RdScreenVtx out[8]);
/* rd_blur_border.c: off (tests) draws the open border without the strips,
 * the picture before v0.8.5; on by default.  The count of strips drawn. */
void rd__set_blur_border_repeat(bool on);
uint64_t rd__blur_border_strip_count(void);

/* rd_blur_border.c, SCENE's outer columns (issue 129, issue 26 at 16:9):
 * where the frame's two outer GS columns a side go (scene: SCENE's record,
 * NULL as no crop): RD_BORDER_CLOSED the GS's own border (Full pixel off at
 * 4:3 or an exact width: the reduction shows black there, the PS2's
 * picture), RD_BORDER_OPEN the border open at the frame's exact width (the
 * screen shows them), RD_BORDER_CROPPED the frame cropped at present
 * (16:9: left out, the glow still reads them), RD_BORDER_WINDOW Full
 * pixel's window (SCENE holds the visible columns only: they are not in
 * it).  A fogged frame's SCENE repeats columns 2 and W - 3 over them where
 * OPEN or CROPPED; rd__set_scene_border_repeat(false) (tests) leaves them
 * as drawn; the count of repeats; rd__scene_border_free the copies' texture
 * (the targets made again, shutdown). */
enum { RD_BORDER_CLOSED = 0, RD_BORDER_OPEN, RD_BORDER_CROPPED, RD_BORDER_WINDOW };

int rd__scene_border_state(const RdTargetRec *scene);
void rd__set_scene_border_repeat(bool on);
uint64_t rd__scene_border_repeat_count(void);
void rd__scene_border_free(void);

/* True for the kinds drawn by the GS sprite model (rd__blur_post_sprite): the
 * staticBlur.c kinds (RD_POST_MOTION_BLUR .. RD_POST_EYE_BLUR) and the
 * reduction. */
static inline bool rd__is_blur_kind(uint32_t kind)
{
    return kind == RD_POST_REDUCTION || (kind >= RD_POST_MOTION_BLUR && kind <= RD_POST_EYE_BLUR);
}

/* --------------------------------------------------------- interpolation
 * rd_interp.c.  rd__interp_frame builds, into a frame it owns, the current
 * frame cur with every keyed draw's data blended from its match in prev by
 * alpha (0 = prev's data, 1 = cur's), and the feedback passes held at the
 * tick: a frame that writes FEED128 copies it into FEED_HELD at its head
 * when firstOfTick, and back from FEED_HELD otherwise, and a
 * frame that reads DISPLAY (the motion blur's old frame) does the same with
 * DISPLAY and DISPLAY_HELD, so every present of a tick starts from the same
 * FEED128 and DISPLAY and draws the recorded FIX.  prev NULL, or a
 * frame-level snap (rd__interp_snap), copies cur's data.  The result is
 * valid until the next call; it owns no temporary targets (cur's are
 * used). */
enum {
    RD_SNAP_NONE = 0, /* interpolated */
    RD_SNAP_NO_PREV,  /* no previous frame, or it is not closed */
    RD_SNAP_GAP,      /* frame numbers not consecutive (a frame was discarded) */
    RD_SNAP_KEEP,     /* either frame is a keep (fbKeep) frame */
    RD_SNAP_CUT,      /* rd_camera_cut during cur (RdCamera.cut) */
    RD_SNAP_CAMERA,   /* the camera turned or moved further than a cut threshold */
    RD_SNAP_FADE,     /* either frame fully faded (the fade edge) */
    RD_SNAP_HISTORY,  /* targets recreated after prev (display options changed) */
    RD_SNAP_SIZE,     /* the scene size differs */
    RD_SNAP_COUNT
};

/* What made a matched keyed draw snap as mismatched */
enum {
    RD_MISMATCH_SIZE = 0, /* payload too short for its header, vertex or primitive count */
    RD_MISMATCH_MESH,     /* the mesh ids are of different layouts (sameMesh) */
    RD_MISMATCH_STATE,    /* program, code or clip mode (b[0..2]) */
    RD_MISMATCH_HEADER,   /* batch range, bones, stream, layout (RdVuPayload) */
    RD_MISMATCH_TOPOLOGY, /* a shadow volume's triangle counts */
    RD_MISMATCH_COUNT
};

/* Why a keyed draw of cur stayed unmatched (RdInterpStats.unmatchedWhy) */
enum {
    RD_UNMATCHED_ABSENT = 0, /* the previous frame has no draw of its key, type and list */
    RD_UNMATCHED_FEWER,      /* the previous frame drew the key fewer times, every one paired */
    RD_UNMATCHED_UNPLACED,   /* left by the pairing by place while a previous draw stayed free */
    RD_UNMATCHED_PAYLOAD,    /* its payload is out of the frame's range */
    RD_UNMATCHED_COUNT
};

/* Why a frame that blended (no snap, alpha below 1) had no blended camera
 * (rd_interp.c camSetup; RdInterpStats.camOffWhy) */
enum {
    RD_CAM_OFF_NONE = 0,    /* the blended camera on, or the frame not blended (a snap) */
    RD_CAM_OFF_ALPHA_RANGE, /* alpha below 0 or NaN (rd__interp_frame passes neither) */
    RD_CAM_OFF_ALPHA1,      /* alpha 1: nothing blends, the tick's own picture */
    RD_CAM_OFF_NO_CAMERA,   /* a frame without a camera (RdFrame.hasCamera) */
    RD_CAM_OFF_LEGACY,      /* ICO_RD_S2_LEGACY=1 (rd__s2_legacy) */
    RD_CAM_OFF_FAILED,      /* a view not invertible, or the blend refused (not affine, a mirror) */
    RD_CAM_OFF_COUNT
};

/* Why a turn over RD_INTERP_TURN_SNAP kept the tick's pose (rd_interp.c
 * rotation continuity; RdInterpStats.turnWhy, the 10 s summary) */
enum {
    RD_TURN_WHY_NONE = 0, /* no memory of the draw: never blended under the snap, or the
                             table full when it was */
    RD_TURN_WHY_OLD,      /* its last memory ended more than RD_INTERP_TURN_GAP ticks before */
    RD_TURN_WHY_POSE,     /* a memory within the gap whose turn does not carry to the pose */
    RD_TURN_WHY_FAR,      /* a memory, the turn further than RD_INTERP_TURN_KEEP from it */
    RD_TURN_WHY_LONG,     /* a memory the long way continues, over RD_INTERP_TURN_LONG */
    RD_TURN_WHY_COUNT
};

typedef struct RdInterpStats {
    uint32_t snap;     /* RD_SNAP_* of the frame */
    uint32_t keyed;    /* keyed draws in cur */
    uint32_t lerped;   /* blended */
    uint32_t missing;  /* no match in prev (snapped) */
    uint32_t mismatch; /* count, topology or mesh differs (snapped) */
    uint32_t jump;     /* moved further than the teleport threshold (snapped) */
    uint32_t morph;    /* mesh draws given a kept or blended vertex stream */
    /* why the mismatched draws snapped (RD_MISMATCH_*), and the mesh
     * draws whose model matrices or bones blended as rotations */
    uint32_t why[RD_MISMATCH_COUNT];
    uint32_t rotated;
    uint32_t turned; /* of them, turning more than 10 degrees in the tick */
    float maxTurn;   /* the largest turn of a model or bone in the tick, degrees */
    /* of them, those with a model or bone turned the long way to continue
     * its turn of the tick before (rotation continuity), and those with one
     * kept at the tick's pose for a turn over RD_INTERP_TURN_SNAP that
     * continued none */
    uint32_t longWay;
    uint32_t turnSnap;
    /* the snapped ones by why (RD_TURN_WHY_*, the draw's first snapped
     * model or bone), and the turns not remembered for want of room */
    uint32_t turnWhy[RD_TURN_WHY_COUNT];
    uint32_t turnFull;
    uint32_t shifted; /* shadow volumes blended with cur's triangles moved by the volume's
                         shift: a topology change, or a prism in one tick only */
    /* VU draws drawn through the rigidly blended camera (matched and
     * not), and of the not-blended ones (unmatched, mismatched, jumped,
     * unkeyed) those re-based */
    uint32_t rebased;
    uint32_t rebasedCur;
    /* lit draws whose previous tick's lights were put in the slots of the
     * current tick's lights they pair with by direction; draws of a key
     * drawn several times paired by their place in the world instead of
     * their order (unpaired new instances included); grids sampling a
     * target whose screen-space STs blended */
    uint32_t lightPaired;
    uint32_t placed;
    uint32_t gridSt;
    /* grids whose STs pool.c's formula (rd.h rd_grid_screen_st) computed
     * through the blended camera instead */
    uint32_t gridStCamera;
    /* the unmatched draws (missing) by RD_UNMATCHED_*; draws of a key drawn
     * several times paired with a free draw of another place (not the
     * nearest of each other) and so drawn as the tick's (counted in jump) */
    uint32_t unmatchedWhy[RD_UNMATCHED_COUNT];
    uint32_t apart;
    /* VU draws of the previous frame alone, their object and part not drawn
     * in the current frame's list and outside its camera's picture: drawn
     * through the blended camera (kept), or not drawn because they could
     * not be placed, their state reached or their block re-based (held) */
    uint32_t prevKept;
    uint32_t prevHeld;
    /* draws projected on the CPU by their tick's camera (RD_SCREEN_FRAME_CAMERA,
     * RD_SHADOW_FRAME_CAMERA) taken through the world onto the blended camera */
    uint32_t reprojected;
    /* The still draws (rd_interp.c stillDraw): blended parts of the world
     * whose object did not move between the ticks while the camera did,
     * each drawn from the tick's block through the blended camera
     * (camCurDraw's block); stillReRounded, of them those whose element-wise
     * blend of camera and model rows (qw 4..27) was not that block in bits
     * (what the blend would have drawn, before the turn).  curWorld: the
     * parts of the world (normal programs, placed through the view) drawn as
     * the tick's through the blended camera (unmatched, snapped or unkeyed)
     * in a frame that blended others.  tickCams: the VU draws marked
     * RD_VU_TICK_CAMERAS */
    uint32_t stillDraws;
    uint32_t stillReRounded;
    uint32_t curWorld;
    uint32_t tickCams;
    /* The mixed picture: in a frame that blended without a blended camera
     * (camOffWhy, RD_CAM_OFF_*), the parts of the world (normal programs'
     * mesh draws placed through the view) that were matched and so drawn
     * from both ticks' blocks blended element by element (at alpha 0 the
     * tick before's block as recorded), worldPrevRaw, and those drawn as
     * the tick's alone (unmatched, snapped or unkeyed: camCurDraw with no
     * camera to re-base them on, the tick's block as recorded),
     * worldCurRaw.  With both above 0 the two kinds stand a tick of camera
     * motion apart in one picture; mixed is then the smaller of the two
     * (the pieces in the minority), 0 otherwise.  Whether such pieces
     * touch is not computed: mixed counts pieces that may open a wedge,
     * not wedges. */
    uint32_t worldPrevRaw;
    uint32_t worldCurRaw;
    uint32_t mixed;
    uint32_t camOffWhy;
    /* VU draws blended whose two payloads differ in size: a card section
     * (rd_mesh.h RdVuCards) at one tick only, the clip mode having changed
     * between REGION and SCISSOR (rd_interp.c blendVu) */
    uint32_t sizeTailBlended;
} RdInterpStats;

/* Rotation-aware blending of an affine 4 x 4 (column-major, w row 0 0 0
 * 1): the 3 x 3 polar-decomposed into a rotation (slerped) and a stretch
 * (lerped); the image of pivot (x, y, z; NULL: the origin) lerped, so the
 * blended matrix turns about it.  False (o untouched) when either matrix
 * is not affine, is singular, or the two have opposite handedness.
 * rd_camera_blend.h rdcb_blend_affine, the turn kept for rd_interp.c. */
bool rd__blend_affine(const double *p, const double *c, double t, const double *pivot, double *o);
/* ICO_RD_S2_LEGACY=1 in the environment (a developer A/B switch, read
 * once) turns off the finer blending: rotation-aware blends, the blended
 * camera, the shadow prisms' alignment and the looser state match (the
 * window also takes alpha from the measured present time).  The VU
 * positions are on the 12.4 grid whatever it says. */
bool rd__s2_legacy(void);
/* ICO_RD_VU_OFFGRID=1 in the environment (a developer switch,
 * read once) brings back unquantised VU X and Y (FrameCB g_z.w) with
 * the Enhanced preset on a scaled target: smoother slow motion, but the
 * seams between meshes and against the GIF path open. */
bool rd__vu_off_grid(void);

int rd__interp_snap(const RdFrame *prev, const RdFrame *cur);
/* The CPU-projected draws' re-projection onto the blended camera (on by
 * default; rd_interp.c): off, they blend in screen space (the
 * tests' and the replay tool's before/after switch). */
void rd__interp_set_reproject(bool on);
/* The present clock's alpha before its clamp to [0, 0.999]
 * (rd_present_clock_alpha's (clock - tickAtMs) / tickMs), as its last call
 * computed it; NAN when that call returned before computing one or
 * rd_present has taken it since (each rd_present takes the value the clock
 * noted for it, so a present the clock did not time, the vsync lock's,
 * finds none).  For the alpha histogram and the tests. */
double rd__present_clock_raw(void);

/* The bins of the alpha histogram (rd_interp.c, the 10 s summary): alpha 0
 * split by the clock's raw value (below 0: clamped from below; anything
 * else, none included: exactly 0), the tenths of 0 < alpha < 0.999, the
 * clock's top 0.999 and 1 (the vsync lock's last slot).  rd__alpha_bin
 * the bin of a present at alpha (already clamped to [0, 1]) whose clock
 * value was raw (NAN: none) */
enum {
    RD_ALPHA_BIN_CLAMPED = 0,
    RD_ALPHA_BIN_ZERO,
    RD_ALPHA_BIN_TENTH0,
    RD_ALPHA_BIN_TOP = RD_ALPHA_BIN_TENTH0 + 10,
    RD_ALPHA_BIN_ONE,
    RD_ALPHA_BIN_COUNT
};

int rd__alpha_bin(float alpha, double raw);
/* One vertex's ST by the RdGridSt formula kind (rd.h; pool.c's statements in
 * double precision): m the grid's model to GS screen (VU qw 16..19), eye the
 * camera's eye in the world (REFLECT), sx and sy the command's f[0] and
 * f[1], pos the vertex and h its wave height (its ST's w).  False (out
 * untouched) for kind NONE or an unknown one, a vertex at w 0 or a result
 * that is not finite. */
bool rd__grid_screen_st(uint8_t kind, const float (*m)[4], const double eye[3], float sx, float sy,
                        const float pos[4], float h, float out[2]);
/* rd_last_present_info's record for rd_end_frame's present of f
 * (interpolation off; rd_interp.c) */
void rd__note_frame_present(const RdFrame *f);
const RdFrame *rd__interp_frame(const RdFrame *prev, const RdFrame *cur, float alpha,
                                int firstOfTick, RdInterpStats *stats);
void rd__interp_shutdown(void);

/* rd_present.c's capture (rd.h rd_capture_presented): whether one is armed;
 * captureRecord (rd_present.c) copies the output into the capture texture
 * (from rd__present_record, before the overlay); rd__capture_finish reads it
 * back and writes the PNG (after the submit). */
bool rd__capture_armed(void);
void rd__capture_finish(void);
/* The thresholds (world units are the game's centimetres) */
#define RD_INTERP_JUMP_WORLD 300.0f  /* an object's or bone's origin, per tick */
#define RD_INTERP_JUMP_SCREEN 256.0f /* GS pixels: screen prims, shadows, grids, particles */
/* RD_INTERP_CAMERA_MOVE and RD_INTERP_CAMERA_TURN, the camera's step and
 * turn per tick past which the pictures snap: rd_camera_blend.h, which the
 * game's cull shares */
/* A model matrix or bone turning further than this in a tick (3600
 * degrees a second at 30 Hz) is a flip, not a motion: it keeps the tick's,
 * unless it continues the turn the same draw (or bone) made in the tick
 * before (rd_interp.c rotation continuity): a turn within
 * RD_INTERP_TURN_KEEP of that one (the distance between the two turns'
 * rotation vectors) blends the way that continues it, the short way or the
 * long way (a wheel turning 190 degrees a tick turns on forward instead of
 * back 170), up to RD_INTERP_TURN_LONG degrees */
#define RD_INTERP_TURN_SNAP 120.0 /* degrees, per tick */
#define RD_INTERP_TURN_KEEP 60.0  /* degrees */
#define RD_INTERP_TURN_LONG 300.0 /* degrees, per tick */
/* A remembered turn also serves a turn that would snap when it ended up to
 * this many ticks before the pair's first (about a quarter second at 30
 * Hz): a pair not blended between (a dropped present, a cut, a fade, a
 * discarded frame) or a draw not drawn for a few ticks keeps its wheel
 * turning on, when the draw's pose is where the turn carried on that many
 * ticks puts it (within RD_INTERP_TURN_KEEP) */
#define RD_INTERP_TURN_GAP 8u /* ticks */
/* In the alignment of two ticks' shadow prisms, a prism left unmatched
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
    RD_FXF_DST = 4096, /* t2 holds the destination as it was before the sprite */
    /* above 1x: the resampling kernels, the steps in DrawCB.scale[3]
     * (rd_replay.c doBlurSpriteDraw); never set when every target involved
     * is at scale 1 */
    RD_FXF_ABOVE1X = 8192,
    /* fx_frame_ps (rd_fx_plan.c): only the alpha computed, RGB 0 by
     * construction (the glow's mask); the result premultiplied for the
     * fixed-function ONE, ONE blender (RD_BP_PREMUL_ADD) */
    RD_FXF_ALPHA_ONLY = 16384,
    RD_FXF_OUT_PREMUL = 32768
};

/* The steps at which fx_sprite_ps leaves the GS's own filter above 1x, in
 * 1/32 t1 texel per bound texel (common.hlsli GS_RS_BOX_MIN32 and
 * GS_RS_BSPLINE_MAX32): the box above 2 texels, the B-spline (bilinear
 * only) below half a texel; the packed steps (DrawCB.scale[3]) hold up to
 * RD_FX_STEP_MAX each */
#define RD_FX_BOX_MIN32 64
#define RD_FX_BSPLINE_MAX32 16
#define RD_FX_STEP_MAX 4095

/* ------------------------------------------------------------- context */
#define RD_SAMPLER_COUNT 16 /* mag x min x wrapS x wrapT */
/* The Enhanced filter's samplers, the same 16 with linear mips
 * (trilinear) and again with the device's anisotropy; a fourth set with
 * nearest mips for the GS's nearest-level mipmapping (DF_GS_LOD; the GS's
 * linear-between-levels mipmapping takes the trilinear set) */
#define RD_SAMPLER_SETS 4
#define RD_SAMPLER_SET_MIP_LINEAR 1
#define RD_SAMPLER_SET_MIP_NEAREST 3
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
    /* draw passes resampled above 1x since rd_init: the staticBlur sprites
     * with RD_FXF_ABOVE1X (rd_replay.c doBlurSpriteDraw), the sprite passes
     * with ICO_DF_ABOVE1X (fillDrawCB, once per planned pass of a draw) and
     * the fog passes above 1x (doFog): 0 while every target is at scale 1 */
    uint32_t above1xDraws;
    uint32_t onceFlags;

    /* GPU objects (rd_replay.c / rd_pipeline.c) */
    RhiBindGroupLayout layoutFrame, layoutDraw, layoutTex, layoutInt;
    RhiBindGroupLayout layoutVu; /* t0 stream, b1 DrawCB, b2 VuCB, b3 VuBoneCB */
    RhiShader vs[RD_VS_COUNT], fs[RD_FS_COUNT];
    RdProfile profile; /* rd__profile_init */
    /* the fallback's program entries (rd__vu_policy_init; docs/port/RENDER.md
     * "The fallback entries"): vsPlain the ten mesh entries' plain variants
     * (vu_X_plain_vs, made only when the fallback takes them; 0
     * otherwise), vsFallback the module each program entry's key draws
     * through (vsPlain[v] or vs[v]; RD_VS_VU_PRELIT .. RD_VS_VU_PARTICLE),
     * vuFallbackPlain whether they are the plain ones and why ("on this
     * device (Adreno 740, issue 107)") */
    RhiShader vsPlain[RD_VS_COUNT];
    RhiShader vsFallback[RD_VS_COUNT];
    bool vuFallbackPlain;
    char vuPlainWhy[160];
    /* the dynamic state in force (rd__vu_policy_init from
     * RhiLimits.dynamicDepth / dynamicBlend; rd__key_dynamic_normalise,
     * rd__set_pipeline) */
    bool dynDepth, dynBlend;
    /* the VU vertex pre-pass (rd_replay_vu.c rd__vu_prepass, rd__vu_prepass_init;
     * docs/port/RENDER.md, "The vertex pre-pass"): on unless the settings
     * ([dev] vu_prepass = false, ICO_RD_VU_PREPASS=0) or the device
     * (RhiLimits.colorBytesPerPixel under the record's bytes) keep it off,
     * and why not */
    bool vuPrepass;
    char vuPrepassWhy[160];
    /* the *_nodual twins of the gs_dual_out entries (0 for the
     * others), and whether the two-pass blend is in force (no dualSrcBlend,
     * ICO_RD_NO_DUAL or rd_set_no_dual) */
    RhiShader fsNoDual[RD_FS_COUNT];
    bool noDual;
    /* the exact blend (rd_dst_read.c): the mode in force (RdBlendExact),
     * the modes the device has (bit 1 << mode), the _dstrov and _dstatt
     * twins of the gs_dual_out entries, and group 3's layout per mode (the
     * target as a storage texture or an input attachment) */
    uint8_t dstMode;
    uint8_t dstModes;
    RhiShader fsDstRov[RD_FS_COUNT], fsDstAtt[RD_FS_COUNT];
    RhiBindGroupLayout layoutDstStorage, layoutDstAttach;
    /* group 2 of the passes' own reads by local read (RhiLimits.localRead
     * only): slot 1 an input attachment (the fog's depth, DATE's colour),
     * slot 2 a sampled texture (the fog's LUT; DATE's setup the dummy):
     * fog_lut_local_ps, date_stencil_ps and fx_local_ps (rd__fs_local_read) */
    RhiBindGroupLayout layoutLocal;
    /* The scene depth is read through a copy (rd__sampled_depth) and every
     * pass keeps its stencil (loaded and stored) whenever
     * the fog's depth path (fogPath) is not RD_FOG_INPLACE.  Off, the fog
     * and the effects depth sample the depth target itself, and the stencil
     * is kept inside the shadow window only. */
    bool depthCopy;
    /* RdFogPath: how the fog reads the depth (rd_fog_path.c): chosen at
     * rd_init from the platform and the overrides, then by the fog's
     * self-test (rd_fog_selftest) */
    uint8_t fogPath;
    /* MSAA is requested but no way of reading the multisampled depth passed
     * the fog's self-test (rd__fog_aa_reconcile): rd__aa_mode gives Off */
    bool msaaBlocked;
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
    /* deferred text: recording, rd_deferred_text_quads is on (RDC_SCREEN gets
     * RD_SCREEN_TEXT_QUADS); replaying, the present draws the deferred text
     * (rd__overlay_collect) and doScreen skips the quads */
    uint8_t textQuads;
    bool deferText;
    /* the Enhanced display options as applied (rd_present.c
     * rd__apply_display from settings): the scene-class targets' scale (the
     * work buffers and the shadow levels keep their GS sizes), the aspect
     * option (the present box's when no frame says otherwise:
     * rd__frame_wexact), and the texture filter upgrade in force */
    float sceneSx, sceneSy, outAspect;
    /* the last replayed frame's exact width (RdFrame.wExact, from its head:
     * GsBase.c gsFrame.wExact) and its camera's display aspect
     * (RdCamera.aspect, rd__set_replay_camera); 0 before one: the frame's
     * exact width the presenter shows (rd__frame_wexact) */
    float frameWExact, frameAspect;
    /* the scene scale the options asked for (rd__apply_display); sceneSx/Sy
     * are what the targets were made at, lower when createNamedTargets had
     * to fall back (sceneFellBack) */
    float sceneReqSx, sceneReqSy;
    bool sceneFellBack;
    /* Full pixel (rd__apply_display): the exact width in GS pixels that a
     * frame-wide scene-class target holds (the box's aspect's, the
     * window's pixels across it; rd__target_scale_of crops the frame to
     * it, RdTargetRec.ox), 0 when the targets hold the whole frame */
    float frameCropWe;
    /* the sample count of the render pass being recorded (rd_replay.c
     * beginPass): 1, or the MSAA epoch's count; rd__get_pipeline makes the
     * pipelines of that pass with it (RdPipelineKey.msaa) */
    uint8_t passSamples;
    /* ICO_RD_PASS_SPLIT=N ([dev] pass_split; rd_core.c rd_init): a render
     * pass of VU draws is ended and begun again, its targets loaded, after
     * every N of them (rd_replay_vu.c passSplit); 0 off (the default) */
    uint32_t passSplit;
    /* the storage pass merge (rd__dst_merge_ok; [dev] storage_pass_merge,
     * ICO_RD_PASS_MERGE=1 turns it on; rd_core.c rd_init): off by default,
     * used only under the storage mode */
    bool passMerge;
    /* the supersampling factor in force (rd__apply_display): 1, 2 or 4,
     * already in sceneReqSx/Sy; rd_scene_scale_lowered divides it out */
    float ssaa;
    /* a scene-sized texture made after the named targets (a shadow count, a
     * depth copy, a snapshot, the wrap accumulator) could not be allocated
     * while the scene scale was above 1: rd_begin_frame then halves the
     * scale (rd__note_scene_pressure) */
    bool scenePressure;
    uint8_t filterUpgrade, fullHeight;
    int spaceOverride; /* rd_set_space_override + 1; 0 = none */
    uint32_t vsyncApplied;
    /* interpolation (rd_interp.c).  interpFloor: the first
     * frame number recorded after the targets were last recreated (a frame
     * pair interpolates only when both are at or after it); cutPending:
     * rd_camera_cut outside an open frame, for the next one; videoShown: an
     * FMV picture went to the output after the last closed game frame
     * (rd_video.c), so rd_present leaves the output alone */
    uint32_t interpFloor;
    uint8_t cutPending, videoShown;
    uint8_t freeCameraPending; /* rd_free_camera between frames */
    /* rd_set_mirror's flag (the run's mirror mode); the
     * effective mirror is this or settings.mirror (rd__mirror_on) */
    uint8_t mirrorRun;
    /* image textures with dirty set (rd__upload_dirty_textures skips its
     * walk of the table while there are none and the filter is unchanged) */
    uint32_t texDirtyCount;
    int texLevelsFilter; /* filterUpgrade the table was last walked for, -1 = never */
    /* image texture updates that changed texels since rd_init,
     * whole (rd_update_texture) and rectangles (rd_update_texture_rect); the
     * font's tests count them */
    uint32_t texFullUpdates, texRectUpdates;
} RdContext;

extern RdContext g_rd;

/* The mirror mode in force (rd.h rd_set_mirror). */
static inline bool rd__mirror_on(void)
{
    return g_rd.settings.mirror != 0 || g_rd.mirrorRun != 0;
}

/* Once-only diagnostics, by bit. */
enum {
    RD_ONCE_OUTSIDE_FRAME = 0,
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
    RD_ONCE_VU_ROW,       /* a (program, code) pair without a table row */
    RD_ONCE_VU_ENDTAG,    /* the particle end-tag quirk would have hidden a skinned draw */
    RD_ONCE_VU_MATERIALS, /* RdVuDraw.materials given: recorded, not applied */
    RD_ONCE_VU_MESHES,    /* the mesh registry evicted meshes */
    RD_ONCE_VU_CODE,      /* an MSCAL code rd_vu_call does not model (debug font) */
    RD_ONCE_SHADOW,       /* a shadow command without a depth-stencil target of the colour's size */
    RD_ONCE_FOG,          /* an RD_POST_FOG without a LUT or a depth source */
    RD_ONCE_DEPTH_VIEW,   /* an ordinary draw sampling a depth view (only the fog reads one) */
    RD_ONCE_BLUR,         /* a staticBlur sprite without a colour target, or AFAIL with Z write */
    RD_ONCE_WRAP, /* a COLCLAMP 0 draw the wrap path does not model (DATE, PABE, AFAIL split) */
    RD_ONCE_COPY_SCALE,     /* a copy between targets of different resolution scales */
    RD_ONCE_MESH_ARENA,     /* the mesh arena is full: meshes past it drawn from the ring */
    RD_ONCE_AA1_WRAP,       /* PRIM.AA1 under COLCLAMP 0: the wrap path draws without coverage */
    RD_ONCE_PABE,           /* PABE on a premultiplied or Ad blend: As < 0x80 pixels blend anyway */
    RD_ONCE_NODUAL_GREATER, /* the two-pass blend's alpha pass under Z GREATER with Z write */
    RD_ONCE_NODUAL_KEY,     /* a LERP or Cd*FIX + Cs key reached rd__get_pipeline unexpanded */
    RD_ONCE_EFFECTS_DEPTH_CRT, /* RdSettings.effectsDepth under the CRT filter: none */
    RD_ONCE_POST_RESERVED,     /* rd_post with a reserved kind: not recorded */
    RD_ONCE_DST_ZWRITE, /* exact blend, storage: a Z-writing draw that can discard under Z GEQUAL */
    RD_ONCE_DST_FIXED,  /* exact blend: a draw left on the fixed-function blend (MSAA, format) */
    RD_ONCE_COUNT
};

_Static_assert(RD_ONCE_COUNT <= 32, "rd__log_once keeps its flags in 32 bits");

/* a + "/" + b into out (no separator added when a is empty or already ends
 * in '/' or '\\'); -1 when it does not fit, 0 otherwise.  The texture and
 * model packs' folder paths. */
static inline int rd__join_path(char *out, size_t size, const char *a, const char *b)
{
    size_t la = strlen(a);
    const char *sep = la > 0 && a[la - 1] != '/' && a[la - 1] != '\\' ? "/" : "";
    int n = snprintf(out, size, "%s%s%s", a, sep, b);
    return n < 0 || (size_t)n >= size ? -1 : 0;
}

void rd__log(const char *fmt, ...);
void rd__log_once(int bit, const char *fmt, ...);
/* The self-tests' results for the start's summary line (rd_summary_text):
 * "<blend> (self-test: ...)" and "<fog path> (self-test: ...)", written
 * where their log lines are, "" until then. */
extern char g_rdSumBlend[288];
extern char g_rdSumFog[224];
/* ... and the VU vertex pre-pass's (rd_vuprepass_selftest): "on
 * (self-test: ok)", "off (self-test: <reason>)", "off <why> (self-test not
 * run)" */
extern char g_rdSumPrepass[352];
/* A recorded command the replay does not model (RDC_WORLD_PRIMS, or a post
 * kind recorded as a stub): counted, and skipped with one log line for the
 * run.  With rd__set_not_implemented_fatal(true), which the render tests set,
 * it is logged every time and aborts. */
void rd__not_implemented(const char *what);
void rd__set_not_implemented_fatal(bool fatal);
uint32_t rd__not_implemented_count(void);

/* Merging consecutive screen-prim commands into one draw (rd_replay.c
 * doScreen); on by default, off for the tests that compare a merged run
 * with sequential draws. */
void rd__set_screen_merge(bool on);

/* Recording helpers rd_post.c uses for the GS writes that have no public
 * rd_* call of their own. */
void rd__rec_scissor(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
void rd__rec_abe(int abe);
void rd__rec_filter(RdFilter mag, RdFilter min);
RdCmd *rd__push(uint8_t type);
/* The draw filter's test for one world draw (rd.h
   rd_set_draw_filter); learns the key's object while the filter is open */
bool rd__draw_filter_pass(RdKey key);
RdFrame *rd__rec_frame(void);

/* ---------------------------------------------------- replay / present */
bool rd__gpu_init(void *sdlWindow);
void rd__gpu_shutdown(void);
/* Replays f (lists 0..12, or 11..12 with keep) from f->startState and, with
 * present, runs the presenter.  The replay state block ends as f->endState. */
bool rd__replay_frame(const RdFrame *f, int keep, bool present);
/* The pass window (the loss bisect, rd_dump.c rd_loss_bisect;
 * docs/port/RENDER.md, "The loss bisect"): every replay numbers its passes
 * 1, 2, ... in the order they begin (the VU vertex pre-pass first when it
 * runs, rd_replay_vu.c rd__vu_prepass, then render passes, copies,
 * snapshots and exact blends: the pass ring's entries other than the
 * uploads and the present).  With a window [first, last] the draws and the
 * target and snapshot copies of the passes outside it are not recorded
 * (those passes still begin, so their clears and layout changes run; the
 * pre-pass's record copies are left out with its draws, its buffer keeping
 * an earlier replay's record; the uploads and the fog's depth copy,
 * rd_fog_path.c, are not windowed), and the walk stops after the command
 * that began pass last + 1, which therefore draws nothing.  So [1, k] is
 * the frame up to pass k, [k, k] pass k alone (on the targets as the
 * passes before it left them in an earlier replay), [UINT32_MAX, 0] no
 * draw at all with every pass counted.  first 0: no window (the default).
 * Set until changed: the caller clears it. */
void rd__replay_set_window(uint32_t first, uint32_t last);

/* The second level's draw window (the loss bisect, rd_dump.c
 * rd_loss_bisect_step): the pass window first .. last, and in pass `last`
 * only the draws drawFirst .. drawLast (numbered 1, 2, ... as the pass asks
 * for them, before the filter) that pass filter (rd_bisect_plan.h
 * RdBisectFilter, arg its value); filter MASK_MARKS draws every VU draw of
 * the window with its marks left out (vu_mark zero).  drawLast 0: the pass
 * window alone.  The note of the last draw recorded:
 * rd__replay_window_note (RdDrawNote: only filled while a draw window is
 * set). */
void rd__replay_set_window_draws(uint32_t first, uint32_t last, uint32_t drawFirst,
                                 uint32_t drawLast, int filter, uint32_t arg);
void rd__replay_window_note(RdDrawNote *out);

/* The window's state (rd_window.c), read by the replay (rd_replay.c
 * passPush, drawCmd, copyCmd, vuDraw, replayFrame) */
typedef struct RdReplayWindow {
    uint32_t first, last; /* first 0: no window */
    uint32_t passes;      /* begun in this replay */
    uint32_t lastDraws;   /* asked for in pass `last` */
    uint32_t drawn;       /* recorded in pass `last` */
    uint64_t lastSeq;     /* pass `last`'s pass ring entry, 0: not begun */
    uint64_t nextSeq;     /* pass last + 1's, 0: not begun */
    bool stop;
    bool noCount; /* the pass split's own pass (passSplit): not a pass of the window */
    /* the second level's draw window (rd__replay_set_window_draws): in pass
     * `last` the draws drawFirst .. drawLast (0: all) that pass filter */
    uint32_t drawFirst, drawLast;
    int filter;
    uint32_t arg;
    /* the command being replayed and its texture (the note of a draw that
     * is not a VU draw) */
    int8_t list;
    uint32_t cmd, tex;
    RdDrawNote note; /* the last draw recorded */
} RdReplayWindow;

extern RdReplayWindow g_rdWin;

/* the VU draw about to be recorded (rd__win_vu_note, while a draw window is
 * set): the window's filter and note read it; a draw without it is no VU
 * draw */
typedef struct RdDrawCtx {
    bool set;
    RdDrawNote note;
} RdDrawCtx;

extern RdDrawCtx g_rdDrawCtx;

/* the VU command being replayed, for the draw note (rd_replay_vu.c doVuDraw,
 * while a draw window is set) */
typedef struct RdVuCur {
    uint8_t type, prog, clip;
    uint32_t batch;
    const uint32_t *index; /* the index list the draws' first and count are in */
    const uint8_t *marks;  /* the mesh's mark bytes (RdMeshRec.kickMark), markCount of them */
    uint32_t markCount;
    const char *mesh;
} RdVuCur;

extern RdVuCur g_rdVuCur;

/* the window leaves this pass's draws and copies out */
static inline bool rd__win_muted(void)
{
    return g_rdWin.first &&
           (g_rdWin.passes < g_rdWin.first || (g_rdWin.last && g_rdWin.passes > g_rdWin.last));
}

/* (with a window set) whether the draw about to be recorded is (false: left
 * out); counts pass `last`'s draws and notes the last one recorded */
bool rd__win_take(void);

/* the note of a VU draw about to be recorded (draws first .. count of
 * g_rdVuCur's list with key k, texture tex; not indexed: the vertices first
 * .. count).  Its vs is classVs, the program's own entry: a draw through
 * vu_main_vs and the pre-passes' draws are noted as their program's,
 * so the second level's VS_ONLY separates the programs (the key text names
 * the pipeline drawn with) */
struct IcoVuCB;
void rd__win_vu_note(uint32_t tex, const RdPipeKeyInt *k, const struct IcoVuCB *vcb,
                     uint8_t classVs, int indexed, uint32_t first, uint32_t count);

typedef struct RdReplayWindowStats {
    uint32_t passes;    /* the last replay's passes begun */
    uint32_t lastDraws; /* the draws pass `last` asked for (all recorded without a draw window) */
    uint32_t drawn;     /* the draws recorded in pass `last` */
    uint64_t lastSeq;   /* pass `last`'s pass ring sequence number, 0: never begun */
    uint64_t nextSeq;   /* pass last + 1's (the walk stopped there), 0: none */
    bool stopped;       /* the walk stopped at the window's end */
} RdReplayWindowStats;

void rd__replay_window_stats(RdReplayWindowStats *out);

/* A staticBlur sprite with ABE and ALPHA LERP FIX at FIX 128 writes
 * ((Cs - Cd) * 128 >> 7) + Cd, which is Cs: rd_replay.c draws it without
 * ABE and so without the destination snapshot (DATE and AFAIL RGB_ONLY
 * still take theirs). On by default; off draws it with the blend (tests:
 * the same frame replayed both ways). */
void rd__set_blur_fix128_skip(bool on);
/* The snapshots taken so far, copies and swaps alike (rd_replay.c takeSnap,
   blurSwapTarget): the fix128_skip test counts what a sprite skipped */
uint64_t rd__blur_snapshot_count(void);
/* The reduction's fallback sprite (rd_replay.c doBlurSpriteDraw) whose steps
 * keep the GS's filter on both axes (blurAbove1xSteps 0: full height at
 * scale 1, one SCENE row a DISPLAY row) is drawn without DF_ABOVE1X, as
 * sprite_above1x would take the GS's path at each pixel.  On by default;
 * off flags it as any other scaled pair (tests: the same frame both ways,
 * byte for byte). */
void rd__set_a1x_gs_keep(bool on);

/* rd_perf.c (rd.h RdPerfRecord): the record of the replay
 * being made.  rd__perf_begin starts it (the CPU phases are added to it by
 * the replay), rd__perf_end closes it with the RhiStats deltas and queues it
 * for its GPU times, which rd__perf_collect_gpu attaches right after the
 * rhi_wait_frame RHI_FRAMES_IN_FLIGHT replays later.  rd__perf_stamp writes
 * timestamp i (RD_PERF_TS_*) into the replay's command list.
 *
 * rd__perf_post(cl, post) says that the GPU work recorded from now on
 * belongs to the picture effect post (RD_PERF_POST_*; -1: none, the scene
 * or the present's own blits): when that changes, a timestamp from the
 * indices above RD_PERF_TS_COUNT ends the time of the one before.  Every
 * RD_PERF_TS_* stamp ends it too and starts the next stretch with none.
 * When those indices run out the effect is not changed (the record says
 * so: RdPerfRecord.gpuPostPartial). */
enum {
    RD_PERF_TS_BEGIN = 0, /* after rhi_wait_frame, before the uploads */
    RD_PERF_TS_LISTS = 1, /* the uploads and the VU vertex pre-pass done, list 0 starts */
    RD_PERF_TS_LIST0 = 2, /* + l: list l done */
    RD_PERF_TS_PRESENT = RD_PERF_TS_LIST0 + RD_LIST_COUNT, /* the present blits done */
    RD_PERF_TS_PREPASS, /* the uploads done, the VU vertex pre-pass starts */
    RD_PERF_TS_COUNT
};

/* The effects' indices run from RD_PERF_TS_COUNT to RD_PERF_TS_PASS0; a
 * timed replay's pieces (rd__perf_pass) take RD_PERF_TS_PASS0 + their index
 * and the stamp after the last one the next index, up to
 * RHI_MAX_TIMESTAMPS - 1 (223 pieces). */
#define RD_PERF_TS_PASS0 32u
#define RD_PERF_PASS_MAX (RHI_MAX_TIMESTAMPS - RD_PERF_TS_PASS0 - 1u)
/* without [dev] perf_passes a replay is timed piece by piece when the last
 * timed one started at least this long before (rd_perf.c rd__perf_begin) */
#define RD_PERF_PASS_EVERY_MS 250.0

/* rd__perf_pass's target for the pieces without a target id: the VU
 * pre-pass's record, the window */
#define RD_PERF_ID_RECORD 0xFFFFFFF0u
#define RD_PERF_ID_WINDOW 0xFFFFFFF1u

extern RdPerfRecord g_rdPerf;
void rd__perf_reset(void); /* a new device: its counters start at 0 */
void rd__perf_begin(const RdFrame *f, int keep, bool present);
void rd__perf_collect_gpu(void);
void rd__perf_end(void);
void rd__perf_stamp(RhiCommandList cl, uint32_t index);
/* the walk's end (the vertex pre-pass and the lists recorded, the present
 * recorded): the slow walk line counts the pipelines and creations from
 * RD_PERF_TS_PREPASS to here */
void rd__perf_walk_end(void);
void rd__perf_post(RhiCommandList cl, int post);
/* the effect an RDC_POST_STUB of kind RdPostKind belongs to (RD_PERF_POST_*),
   -1 for a kind that is none of them */
int rd__perf_post_of_kind(uint32_t kind);
/* rd_present's interpolation time, charged to the replay that follows */
void rd__perf_interp_ms(double ms);
void rd__perf_alpha(float alpha, int firstOfTick); /* the next replay is a present at alpha */
/* a synchronous readback's time (rd_replay.c readTexture), charged likewise */
void rd__perf_readback_ms(double ms);
/* A piece of GPU work begins (rd.h RD_PERF_PASS_*; called outside any
 * render pass, before the piece records anything): counted by kind, and in
 * a timed replay its timestamp written and its record opened (the colour
 * target id or RD_PERF_ID_*, its size in texels, the effect in force).
 * rd__perf_pass_end closes the last piece with one more stamp, before the
 * replay's list ends (rd_replay.c replayFrame).  docs/port/RENDER.md,
 * "Performance records". */
void rd__perf_pass(RhiCommandList cl, int kind, uint32_t target, uint32_t w, uint32_t h, int post);
void rd__perf_pass_end(RhiCommandList cl);
/* [dev] perf_passes from ICO_RD_PERF_PASSES, logged once (rd_replay.c's
 * device start); rd__set_perf_passes sets it (tests, the replay tool) */
void rd__perf_passes_init(void);
void rd__set_perf_passes(bool on);
/* [dev] perf_list_stamps (ICO_RD_PERF_LIST_STAMPS=0 turns them off, read by
 * rd__perf_passes_init): the list and effect timestamps, which are often
 * written inside an open render pass; off, only the stamps outside every
 * pass are written (rd_perf.c stamp).  rd__set_perf_list_stamps for the
 * tests */
void rd__set_perf_list_stamps(bool on);
/* RdPerfPass.target for target id `id` (rd_core.c) */
uint8_t rd__target_code(uint32_t id);
/* rd_set_host_call's hook, or fn(arg) directly (rd_core.c) */
void rd__on_host(void (*fn)(void *arg), void *arg);
/* a monotonic clock in ms (rd_core.c), for the replay and start-up timings */
double rd__now_ms(void);
/* Present pass inside the replay's command list (rd_present.c). */
void rd__present_record(RhiCommandList cl);
/* The presentation box of aspect (4:3: the Original integer
 * box) centred in outW x outH; rd_video.c's movie box is the 4:3 one. */
void rd__present_box(uint32_t outW, uint32_t outH, float aspect, RhiRect *box);
/* the 2D layer's 4:3 box (rd.h rd_ui_box_of) inside the presentation box of
 * an outW x outH output: the movie's box */
void rd__ui_box(uint32_t outW, uint32_t outH, RhiRect *box);
/* The reduction pass's border crop: columns at each side and rows of
 * DISPLAY's gsH / 2 at the top and bottom (GsBase.c) that it leaves black.
 * Shared with the presenter's deferred text, which keeps off that border
 * unless full pixel is on (the reduction then draws the whole frame), and
 * with the border mask (rd__border_mask).  The rows are 0 on the Japanese
 * disc, whose gsb_Reduction_jpn keeps every line (region.h
 * reduction_crops_rows). */
uint32_t rd__reduction_crop(uint32_t gsH);
/* The frame is cropped: gsW is over 512 and its exact width
 * (rd__frame_wexact) under gsW, so step 2 leaves (gsW - wExact) / 2
 * columns out at each side (16:9: 688 over 682.67) and the game's own
 * border with them.  False at 512 (4:3) and where the frame is its exact
 * width (48:9 at 2048).  The border mask and the replay's open border
 * (rd_replay.c borderOpen) apply only then. */
bool rd__frame_cropped(void);
/* The border mask (RdSettings.borderMask with Full pixel off, on a cropped
 * frame alone: rd__frame_cropped; elsewhere the GS's own border): the part of
 * box, a picture cols GS columns across of a frame gsH lines high, that
 * the game's border leaves visible.  The border is the reduction's
 * (rd_post.c postReduction scissor) at the picture's own scale: 2 GS
 * columns of boxW / cols output pixels at each side, rounded, and
 * crop / (gsH / 2) of the box's height at the top and bottom
 * (rd__reduction_crop: 2 of 224 lines, 8 of 256 at gsH 512), rounded.  The
 * presenter's box blit, its effects depth blit and the CRT composite take
 * *inner as their scissor (the viewport stays the box, the cleared black
 * stays in the bands) and the deferred text is clipped to it.  False, with
 * *inner = *box, without the mask. */
bool rd__border_mask(const RhiRect *box, float cols, uint32_t gsH, RhiRect *inner);
/* The widest aspect the presenter shows is the native frame's,
 * ICO_FRAME_ASPECT_MAX (port/include/ico_frame.h, included above). */
/* The frame's exact width in GS pixels, the columns the presenter shows
 * with Full pixel on and off (ico_frame.h IcoFrame.wExact): exactly 512
 * when the scene is 512 wide (the Original integer box, no crop); else the
 * last replayed frame's own (RdFrame.wExact), or 384 times the display
 * aspect (its camera's, else the aspect option) for a frame without one;
 * at most gsW.  The present box has the aspect wExact / 384, and the
 * source rectangle drops (gsW - wExact) / 2 GS pixels at each side
 * (rd_present.c); the UI box, the deferred text and the CRT grid (rd_crt.c
 * rd__crt_crop) take the same width. */
float rd__frame_wexact(void);
/* g_rd.settings -> g_rd.sceneSx/Sy,
 * outAspect, filterUpgrade, fullHeight (and the swapchain's vsync).  True
 * when a target scale changed (the caller recreates the named targets). */
bool rd__apply_display(void);
/* tw, th, sx, sy, wide of a target record from its GS size;
 * named = its RdTargetId, -1 for a temporary target (rd_core.c). */
void rd__target_scale_of(RdTargetRec *t, int named);
bool rd__present_acquire(void);
/* After a swapchain image is acquired (rd__present_acquire, the
 * movie's acquireOut): the output size becomes the swapchain's when they
 * differ (logged once per change; rd_output_followed reports it) */
void rd__output_follow_swapchain(void);
/* For the tests: the output size and the box the last present
 * drew the picture in (rd__present_record, the movie's frame), noted as it
 * is recorded; false before the first */
void rd__note_present_box(uint32_t outW, uint32_t outH, const RhiRect *box);
bool rd__last_present_box(uint32_t *outW, uint32_t *outH, RhiRect *box);
void rd__present_finish(void);
void rd__present_shutdown(void);

/* A uniform block in the ring and the group that reads it: the
 * group is its layout's one group of the replay (a dynamic uniform slot),
 * offset the block's ring offset, passed when the group is bound. */
typedef struct RdUniform {
    RhiBindGroup group;
    uint32_t offset;
} RdUniform;

/* The presentation overlay (rd.h rd_set_present_overlay;
 * rd_present.c).  rd__overlay_collect runs the registered callback for the
 * present about to be replayed (replayFrame, before the ring is sized and
 * the textures uploaded) and keeps its prims; rd__overlay_ring_bytes is what
 * drawing them takes from the upload ring; rd__present_record draws them
 * after the box blit.  rd__overlay_draw (rd_replay.c) draws one batch into
 * the pass open on an output of format fmt, FrameCB bound by the caller;
 * rd__overlay_state (rd_pipeline.c) is the synthetic state block it plans
 * with, which rd__enumerate_reachable_screen enumerates too.
 * Deferred text: rd__overlay_collect(f, keep) first collects the frame's
 * deferred text (rd.h rd_deferred_text) when the present draws it deferred
 * (the Enhanced preset, a renderer registered, an output), which sets
 * g_rd.deferText for the replay (doScreen then skips the items' quads); the
 * items' prims are drawn before the overlay's, each batch in its region. */
void rd__overlay_collect(const RdFrame *f, int keep);
/* The same for a film's present (rd_video.c): the film's box on the output
 * (box43) is the picture's box, no CRT grid, no deferred text.
 * rd__overlay_film_record draws what it collected in a pass of its own on out
 * (RENDER_TARGET, fmt, w x h) after the picture, the touch controls over the
 * HUD and the popups; rd__overlay_ring_bytes is the ring it takes. */
void rd__overlay_collect_film(const RhiRect *box);
void rd__overlay_film_record(RhiCommandList cl, RhiTexture out, RhiFormat fmt, uint32_t w,
                             uint32_t h);
/* The dirty textures the next upload walk takes from the ring (the tail of
 * the replay's estimate) and the walk itself, into cl, for the films'
 * overlay: the touch controls' and the popups' textures. */
uint64_t rd__texture_ring_bytes(void);
void rd__upload_textures(RhiCommandList cl);
/* With the CRT filter on, rd__overlay_collect gives the overlay a context
 * of the filter's source grid (rd__crt_grid's vw x the frame's lines, the
 * box the whole of it, the frame's 1x scale) and collects no deferred text;
 * rd__crt_record then draws the prims into that layer (t, of fmt, w x h,
 * RENDER_TARGET) with rd__overlay_grid_draw, and the present draws no overlay
 * above the filter (unless the filter could not draw: then the grid's prims
 * are scaled into the box on the output).  rd__overlay_grid_pending: there
 * are prims for the layer.  The layer holds only the main overlay's prims
 * (rd_set_present_overlay); the top layer's (rd_set_present_overlay_top) are
 * laid out on the output and drawn on it after the filter. */
bool rd__overlay_grid_pending(void);
void rd__overlay_grid_draw(RhiCommandList cl, RhiTexture t, RhiFormat fmt, uint32_t w, uint32_t h);
/* rd_present.c's blit: src (sw x sh) into box of dst, filter, x flipped,
 * crop texels of src left out at each side (0: all of it) */
void rd__present_blit(RhiCommandList cl, RhiTexture src, uint32_t sw, uint32_t sh, RhiTexture dst,
                      RhiFormat dstFmt, uint32_t dw, uint32_t dh, RhiLoadOp load,
                      const RhiRect *box, RdFilter filter, int mirror, float crop);
/* rd_core.c, from rd_post: records the RD_OTEXT_OP of a post
 * pass of kind (FADE, LETTERBOX, BRIGHTNESS, KEEP, REDUCTION) when the frame being
 * recorded has deferred text already; nothing otherwise */
void rd__deferred_text_op(uint8_t kind, const RdTextOp *op, RdKey key);
uint64_t rd__overlay_ring_bytes(void);
void rd__overlay_draw(RhiCommandList cl, RhiFormat fmt, RdUniform frame, uint8_t prim,
                      const RdScreenVtx *v, uint32_t n, uint32_t tex, uint8_t blend);
void rd__overlay_state(RdStateBlock *s, uint8_t blend);
/* The RHI shader for a compiled entry point of the shader table
 * (shaders_gen.h), DXIL or SPIR-V as the backend takes; id 0 (logged) when
 * the table has no such entry.  The replay's shaders and the films'. */
RhiShader rd__make_shader(const char *name);
/* Moves a texture to a state with a barrier when needed (outside passes).
 * A clear or shadow reset the replay has not recorded yet (RdPendingClear,
 * RdStencilWindow) on t is recorded first, into the replay's list: only when
 * cl is that list.  Any other list (a readback, the present's own) must find
 * nothing pending on t, which is asserted; the replay's last endPass records
 * whatever is pending before its list ends. */
void rd__transition(RhiCommandList cl, RhiTexture t, RhiState *cur, RhiState want);

/* A copy of a target's depth for a pass to sample (g_rd.depthCopy) */
typedef struct RdDepthCopy {
    RhiTexture tex;
    RhiState state;
    uint32_t w, h;
} RdDepthCopy;

/* The texture a pass samples t's depth from (RHI_ASPECT_DEPTH), left in a
 * sampled state, outside any pass: t's depth itself in
 * RHI_STATE_DEPTH_READ, or with g_rd.depthCopy a whole copy of it in *copy
 * (made or resized here) in RHI_STATE_SHADER_READ.  Id 0 when the copy
 * cannot be made. */
RhiTexture rd__sampled_depth(RhiCommandList cl, RdTargetRec *t, RdDepthCopy *copy,
                             const char *name);
/* rd__sampled_depth's copy whatever g_rd.depthCopy says: t's whole depth
 * (both aspects) copied into *copy (made or resized here), left in
 * RHI_STATE_SHADER_READ outside any pass; id 0 when it cannot be made,
 * with no word to the scale (rd__sampled_depth notes the scene's pressure
 * then; the crack filler, rd_crack_fill.c, which writes t's depth while it
 * reads the copy, does without). */
RhiTexture rd__depth_copy_take(RhiCommandList cl, RdTargetRec *t, RdDepthCopy *copy,
                               const char *name);
void rd__depth_copy_free(RdDepthCopy *copy);
/* Turns g_rd.depthCopy on (the fog's path RD_FOG_COPY; tests: the copy
 * against the depth read in place, in one run). */
void rd__force_depth_copy(void);

/* ------------------------------------------------------- the fog's depth
 * rd_fog_path.c.  The three ways the fog can read the scene depth: the
 * depth target sampled in place, a whole copy of it sampled (vkCmdCopyImage
 * into a second depth-stencil texture), or its depth aspect copied into a
 * buffer and from there into an R32_UINT colour texture the fog reads as
 * words (Vulkan only).  Every target depth is made with the sampled and the
 * copy-source uses the device allows, so a switch needs no new targets. */
typedef enum RdFogPath {
    RD_FOG_INPLACE = 0,
    RD_FOG_COPY = 1,
    RD_FOG_BUFFER = 2,
    /* the depth read inside the scene pass, the pass's own depth attachment
     * as an input attachment (local read: fog_lut_local_ps, rd_replay.c
     * doFog), where the fog's depth is SCENE's own at its size; otherwise
     * (and on D3D12) the copy.  Only where RdProfile.fogLocal */
    RD_FOG_LOCAL = 3,
    RD_FOG_PATH_COUNT
} RdFogPath;

/* "inplace", "copy", "buffer", "local" */
const char *rd__fog_path_name(int path);
/* The device can take the path (in place: a sampleable depth; buffer:
 * the Vulkan backend; local: rd__fog_local_offered). */
bool rd__fog_path_supported(int path);
/* The local read is offered: the profile's fogLocal, the device's local
 * read on Vulkan, fog_lut_local_ps and g_rd.layoutLocal made */
bool rd__fog_local_offered(void);
/* Sets g_rd.fogPath, and g_rd.depthCopy with it (on unless in place). */
void rd__set_fog_path(int path);
/* rd_init, before the targets are made: the platform's path (in place on
 * a desktop GPU; the copy on a tile-based GPU and on D24S8), or the one
 * ICO_RD_FOG_PATH (inplace, copy, buffer) or ICO_RD_DEPTH_COPY (1 copy,
 * 0 in place) names (ICO_RD_FOG_PATH=test names none: the self-test then
 * tries every path, not only up to the first that passes); ICO_RD_FOG_SABOTAGE (a comma list of paths) makes
 * those paths read a depth of 0, as a device that returns nothing would
 * (tests).  Arms the probe. */
void rd__fog_path_init(void);

/* The texture the fog reads t's depth from, by g_rd.fogPath, left in a
 * sampled state outside any pass; *state tracks it (the probe copies from
 * it).  aspect is RHI_ASPECT_DEPTH, or RHI_ASPECT_COLOR for the buffer
 * path's R32_UINT texture (buffer set).  False when it cannot be made. */
typedef struct RdFogSource {
    RhiTexture tex;
    RhiState *state;
    RhiViewAspect aspect;
    uint8_t buffer;
    uint8_t sabotaged; /* ICO_RD_FOG_SABOTAGE names the path: the shader reads 0 */
} RdFogSource;

bool rd__fog_source(RhiCommandList cl, RdTargetRec *t, RdDepthCopy *copy, RdFogSource *out);
/* The local read's source (RD_FOG_LOCAL): t's depth itself, read by its own
 * pass as an input attachment (no transition, no copy); sabotaged as
 * ICO_RD_FOG_SABOTAGE names "local" */
void rd__fog_source_local(RdTargetRec *t, RdFogSource *out);
/* The buffer path's buffer and texture (SCENE's caches: freed with them). */
void rd__fog_buffer_free(void);

/* The probe: on the first fogged replay after start and after the scene
 * targets are made again at a new size, doFog asks rd__fog_probe_wanted
 * and, after the fog's draw, rd__fog_probe_record copies five texels
 * (centre, and the corners 8 pixels in) of the depth the fog read and of
 * the fogged colour target into small textures; rd__fog_probe_finish (the
 * next rd_begin_frame) reads them back and logs one line, "fog: probe".
 * zid: the depth's target id (its GS Z scale); lut: the fog's table (256
 * RGBA8 entries; NULL: none), whose entry 0 the line names as the colour of
 * a pixel nothing drew. */
bool rd__fog_probe_wanted(void);
void rd__fog_probe_record(RhiCommandList cl, const RdTargetRec *tz, uint32_t zid,
                          const RdFogSource *src, RdTargetRec *tc, const uint8_t *lut);
void rd__fog_probe_finish(void);
/* The self-test's replays (rd_fog_selftest) and the probe's fogged replay:
 * doFog reads the table at the exact entry of the depth's index (Z >> 16),
 * not between entries above 1x, so both measure the depth read alone. */
bool rd__fog_exact(void);
/* The self-tests' frames (rd_fog_path.c, rd_blend_selftest.c), built here,
 * never recorded: rd__st_push appends a zeroed command of type to list
 * (NULL when out of memory), rd__st_push1 one with b[0] = v, rd__st_test
 * the TEST state of a GS TEST word, rd__st_target the list's colour and
 * depth target scene at w x h GS pixels with the XYOFFSET. */
RdCmd *rd__st_push(RdFrame *f, int list, uint8_t type);
bool rd__st_push1(RdFrame *f, int list, uint8_t type, uint8_t v);
bool rd__st_test(RdFrame *f, int list, uint64_t word);
bool rd__st_target(RdFrame *f, int list, uint32_t scene, uint32_t w, uint32_t h);
/* Copies recorded and not read back yet (rd_begin_frame then calls
 * rd__fog_probe_finish on the host). */
bool rd__fog_probe_pending(void);

/* The edge probe (rd_fog_path.c, issues 129 and 26): every fogged replay
 * doFog asks rd__fog_edge_due (want: the fog not the multisampled read; it
 * also counts the copies of a replay the GPU is past and logs them) and, when it
 * says so, ends its pass and calls rd__fog_edge_record, which copies the
 * fog's depth (src, tz's size) at texel columns 0, 1, tw - 2 and tw - 1
 * into a readback buffer; lut: the fog's table (its entry 0 named), NULL.
 * rd__fog_edge_column counts the rows of column index (0 or 1) of a pair
 * of words per row, pitch bytes apart, at GS Z 0; rd__fog_edge_line writes
 * the "fog: probe: outer columns:" line for four of them; rd__fog_edge_last
 * is the last such line logged ("" before one, tests). */
typedef struct RdFogEdgeColumn {
    uint32_t column;      /* the texel column */
    uint32_t rows;        /* the rows looked at */
    uint32_t zero;        /* of them at GS Z 0 */
    uint32_t first, last; /* the first and last of those (zero 0: none) */
} RdFogEdgeColumn;

bool rd__fog_edge_due(bool want, const RdFogSource *src, const RdTargetRec *tz);
void rd__fog_edge_record(RhiCommandList cl, const RdTargetRec *tz, uint32_t zid,
                         const RdFogSource *src, const uint8_t *lut);
void rd__fog_edge_column(const uint8_t *data, uint32_t pitch, uint32_t rows, uint32_t index,
                         bool d24, float zScale, uint32_t column, RdFogEdgeColumn *out);
size_t rd__fog_edge_line(char *out, size_t size, const RdFogEdgeColumn c[4], uint32_t gsW,
                         int state, const uint8_t *lut0);
const char *rd__fog_edge_last(void);
/* rd_core.c, after the named targets were made again: a new SCENE size arms
 * the probe and, once rd_fog_selftest has run, runs the self-test again. */
void rd__fog_scene_remade(void);
/* rd_begin_frame, after the settings applied: when the sample count asked
 * for differs from the one the self-test last ran under, runs it again on
 * the MSAA scene (sampled paths first); sets or clears g_rd.msaaBlocked. */
void rd__fog_aa_reconcile(void);
/* The anti-aliasing the settings ask for and the device can draw, whatever
 * the fog path (rd__aa_mode without g_rd.msaaBlocked). */
int rd__aa_requested(void);
/* rd__gpu_shutdown: the probe's and the buffer path's objects. */
void rd__fog_path_shutdown(void);

/* A target clear the replay has not recorded yet
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
bool rd__take_pending_clear(const RdPendingClear *p, uint32_t cid, uint32_t did,
                            RhiLoadOp *colorLoad, RhiLoadOp *depthLoad);

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
 * window a frame, on SCENE's depth (one rd_shadow_reset and one
 * rd_shadow_resolve). */
#define RD_STENCIL_LIVE_MAX 4

typedef struct RdStencilWindow {
    uint32_t live[RD_STENCIL_LIVE_MAX];
    uint32_t clearFor;
    uint8_t spill;
} RdStencilWindow;

/* Whether depth (not 0) is inside its window. */
bool rd__stencil_live(const RdStencilWindow *w, uint32_t depth);
/* The reset of depth opens its window (nothing when it is open already). */
void rd__stencil_open(RdStencilWindow *w, uint32_t depth);
/* The end of depth's resolve closes its window. */
void rd__stencil_close(RdStencilWindow *w, uint32_t depth);

/* The stencil ops of a pass on depth (not 0) whose depth loads depthLoad;
 * whole: the pass's render area is the whole depth.  A pending reset of
 * this depth is taken when whole: the stencil loads CLEAR (to 0), clearFor
 * is emptied and true is returned.  Else, on a live depth, the stencil
 * loads as the depth does; on any other, a CLEAR stays CLEAR and anything
 * else is DONT_CARE.  The stencil is stored on a live depth only. */
bool rd__stencil_ops(RdStencilWindow *w, uint32_t depth, bool whole, RhiLoadOp depthLoad,
                     RhiLoadOp *stencilLoad, RhiStoreOp *stencilStore);
/* rhi_wait_frame for the renderer's own frames (the replay, the FMV picture,
 * the camera probe): also starts a new epoch of the bind group caches
 * (rd_replay.c), since bind groups live one frame slot. */
void rd__wait_frame(void);
/* A frame of the renderer's own outside a replay (the films,
 * rd_video.c): rd__wait_frame, then the next slot of the upload ring (at
 * least ringBytes) as a replay takes it, so the ring's slot keeps pace
 * with the RHI's frames and rd__frame_group, rd__crt_group and the like
 * work.  False when the ring cannot be had. */
bool rd__begin_own_frame(uint64_t ringBytes);
/* Tests only: a ring larger than this many bytes (0: none) cannot be made,
 * as on a device whose upload memory is full (rd_upload.c rd__ensure_ring). */
extern uint64_t g_rdRingFailOver;
/* Ring allocation for the frame being replayed. */
uint64_t rd__ring_alloc(uint64_t size, uint64_t align);
/* rd_replay.c's replay state for rd_upload.c: the command list being
 * recorded, the frame slot whose ring it uses, and the stencil's shadow
 * window. */
RhiCommandList rd__replay_cl(void);
uint32_t rd__ring_slot(void);
RdStencilWindow *rd__replay_stencil(void);
/* rd_replay.c helpers rd_upload.c shares: the alpha coverage a texture's
 * Enhanced mips keep, whether a target's passes begin readable, and
 * rd__stencil_ops with the depth copy's stencil load and store. */
int rd__mip_boost(const RdTexRec *t, uint8_t *ref);
bool rd__pass_wants_readable(const RdTargetRec *c);
/* The local read's fragment entries (fog_lut_local_ps, date_stencil_ps,
 * fx_local_ps): group 2 is g_rd.layoutLocal (rd_pipeline.c pipelineDesc) */
bool rd__fs_local_read(uint8_t fs);
/* The fog's table, uploaded at the replay's upload phase (replayFrame,
 * after the textures): the first RD_POST_FOG record of the lists the
 * replay draws, copied into the LUT texture once, outside any pass, so
 * doFog draws without a copy (and inside the scene pass on the local
 * path); a fog whose table is another is copied in doFog as before */
void rd__fog_lut_upload(const RdFrame *f, int keep);
/* DATE's stencil path (rd_replay.c; RdProfile.dateStencil): the setup's
 * keys, date_clear_ps (mode RD_STENCIL_DATE_CLEAR) and date_stencil_ps
 * (RD_STENCIL_DATE_SET), through blit_vs into colorFmt with the scene depth,
 * colour mask 0 (rd_pipeline.c), and the DATE draws' twins of an enumerated
 * key (rd__enumerate_reachable_date) */
RdPipeKeyInt rd__date_setup_key(uint8_t mode, RhiFormat colorFmt);
uint32_t rd__enumerate_reachable_date(RdPipeKeyInt *out, uint32_t max, uint32_t n);
/* DATE's twin of a planned draw pass: the stencil bit's test
 * (RD_STENCIL_DATE_TEST0 / TEST1 by DF_DATM), the shader's DF_DATE and
 * DF_DATM off (the planner keeps DATE in the flags, the key's date off).
 * False (unchanged) for a pass without DF_DATE, with a stencil mode of its
 * own or without a depth attachment */
bool rd__date_stencil_pass(RdDrawPass *p);
/* the fold's key of a staticBlur pair (fx_blur2_ps over colorFmt, no depth;
 * rd_replay_tiler.c rd__replay_blur_fold) */
RdPipeKeyInt rd__blur_fold_key(const RdStateBlock *s, RhiFormat colorFmt);
bool rd__stencil_ops_packed(RdStencilWindow *w, uint32_t depth, bool whole, RhiLoadOp depthLoad,
                            RhiLoadOp *stencilLoad, RhiStoreOp *stencilStore);
/* rd_upload.c, for the replay: the ring a frame needs (its commands, plus
 * rd__texture_ring_bytes), the slot's ring grown to at least `need` (false,
 * keeping the old ring, when it cannot be had), the dirty textures' upload
 * into the replay's list, the static meshes' upload into the device arena,
 * and the first clear of the targets with a new texture. */
uint64_t rd__estimate_ring(const RdFrame *f, int keep);
bool rd__ensure_ring(uint64_t need);
void rd__upload_dirty_textures(void);
void rd__upload_meshes(const RdFrame *f, int keep);
void rd__clear_new_targets(void);
/* Binds u at group index `group` with its offset (rhi_cmd_set_bind_group_offsets). */
void rd__bind_uniform(RhiCommandList cl, uint32_t group, RdUniform u);
/* FrameCB for the presenter and posts (group 0). */
RdUniform rd__frame_group(uint32_t targetW, uint32_t targetH, float originX, float originY);
/* The same with the GS Z scale of the bound depth buffer; rd__frame_group
 * uses 2^-24 (no depth bound). */
RdUniform rd__frame_group_z(uint32_t targetW, uint32_t targetH, float originX, float originY,
                            float zScale);
/* FrameCB with the native frame's region scale (g_frame.x; 1 for none:
 * rd__region_scale gives a target's) and the target's texels per GS pixel
 * in g_z.yz. */
RdUniform rd__frame_group_ex(uint32_t targetW, uint32_t targetH, float originX, float originY,
                             float zScale, float regionScale, float scaleX, float scaleY);
/* The region scale of a draw into tc: 512 / W on a target of the native
 * frame's GS size W x H (g_rd.gsW x gsH: SCENE, WORK2, AURA_WORK, the shadow
 * count, a block the game sizes as the frame) when W is not 512 (FrameCB
 * g_frame.x carries it, which no shader reads: the VU region test compares
 * the position as computed); exactly 1 otherwise (every target at 4:3). */
float rd__region_scale(const RdTargetRec *tc);

/* ------------------------------------------------- frame head and camera
 * rd_frame.c. */
/* rd_end_frame: keep the head copy in the first replayed list, no-op the
 * other (rd.h, rd_frame_head). */
void rd__frame_head_resolve(RdFrame *f, int keep);
/* Fills FrameCB's camera fields (view, proj, viewProj, cameraPos, clip)
 * from cam, or identity with cam NULL.  cb is an IcoFrameCB. */
void rd__fill_camera_cb(void *cb, const RdCamera *cam);
/* The camera FrameCB carries during the replay of a frame (rd_replay.c). */
void rd__set_replay_camera(const RdCamera *cam);
/* Test hook: FrameCB from cam, then camera_probe_ps evaluates
 * mul(g_view, p), mul(g_proj, mul(g_view, p)) and mul(g_viewProj, p) on the
 * GPU and returns them in out[0..2] (bit patterns read back).  False without
 * a device. */
bool rd__camera_probe(const RdCamera *cam, const float p[4], float out[3][4]);
/* DrawCB (group 1). */
RdUniform rd__draw_group(const void *drawCB);
/* crt.hlsl's CrtCB (IcoCrtCB) in DrawCB's dynamic slot and
 * layout; the layout's dynamic group is cached per block size, so the two
 * sizes need not match (rd_replay.c dynamicGroup). */
RdUniform rd__crt_group(const void *crtCB);
RhiBindGroup rd__tex_group(RhiTexture t, RhiSampler s);
/* The same with t2 (sprite_ps's DATE snapshot) bound to date; rd__tex_group binds
 * the 1x1 dummy there (sprite_ps reads t2 only under DF_DATE). */
RhiBindGroup rd__tex_group_date(RhiTexture t, RhiSampler s, RhiTexture date);
RhiSampler rd__sampler(RdFilter mag, RdFilter min, RdWrap s, RdWrap t);

/* ------------------------------------------------------ tests and tools */
/* Recording without a device: rd_* calls work, rd_end_frame closes frames
 * but replays nothing. */
bool rd__init_record_only(uint32_t gsW, uint32_t gsH);
const RdFrame *rd__last_frame(void);
/* F12's cull line for the frame f (rd_dump.c, rd_note_cull_drop), without
 * the log's prefix; the length written */
size_t rd__cull_drop_line(const RdFrame *f, char *line, size_t cap);
const RdFrame *rd__prev_frame(void);
/* Reads a target's colour (RGBA8, tightly packed, w * h * 4 bytes). */
bool rd__read_target(RdTarget t, void *dst, size_t dstSize, uint32_t *w, uint32_t *h);
/* The depth buffer of a target with one, as floats (w * h,
 * tests only). */
bool rd__read_target_depth(RdTarget t, float *dst, size_t dstSize, uint32_t *w, uint32_t *h);
/* Reads the headless presenter output (RGBA8). */
bool rd__read_present(void *dst, size_t dstSize, uint32_t *w, uint32_t *h);
/* The effects depth of the last present (RdSettings.effectsDepth), outW x
 * outH floats; false when no present has drawn it (tests). */
bool rd__read_present_depth(float *dst, size_t dstSize, uint32_t *w, uint32_t *h);
/* The effects depth pass runs only with an effects program
 * loaded (rhi_injector_name); true runs it whenever RdSettings.effectsDepth
 * is on, as with a program (tests: lavapipe has no layer). */
void rd__force_effects_depth(bool force);
/* The photo capture's synchronous readback of an RHI texture (RGBA8 or BGRA8,
 * w x h, tightly packed), leaving it in COPY_SRC (*state follows) */
bool rd__read_rhi_texture(RhiTexture t, RhiState *state, uint32_t w, uint32_t h, void *dst,
                          size_t dstSize);
/* Reads an image texture's level 0 back from the GPU as its
 * format holds it (tightly packed, w * h * rd__texel_bytes bytes); false
 * before its first upload. */
bool rd__read_texture(RdTex t, void *dst, size_t dstSize, uint32_t *w, uint32_t *h);

/* Dumps (rd_dump.c).  Loading creates the frame's textures and temporary
 * targets in the current context and rewrites the ids in the commands. */
#define RD_DUMP_MAGIC "ICORDMP\0"
/* The dump format's versions: 2 adds RDC_ALPHA, RDC_SHADE and
 * RdStateBlock.gouraud; 3 the VU meshes; 4 RDC_AA1 and RdStateBlock.aa1;
 * 5 RDC_OVERLAY_TEXT and RDC_SCREEN's RD_SCREEN_TEXT_QUADS; 6
 * RD_TARGET_FEED_HELD, a 17th fixed target; 7 RD_TEXEL_SHEET images, the
 * style in the view word; 8 RD_TARGET_DISPLAY_HELD, an 18th fixed target
 * (the motion blur); 9 RdStateBlock.tex1Lod, RDC_FILTER's u[0] and an image's GS
 * levels (RdTexRec.gsLevels) after its texels; 10 the native frame after
 * gsH: f32 aspect, f32 wExact, u32 cullW (RdFrame.aspect, .wExact, .cullW);
 * 11 a sheet image's bakedTexa word with bits 16..23 clear, its image as in
 * 10 (those bits set: a sheet w x h at every scale with a halo made on the
 * CPU in its bottom half, which loads as the image made again from the
 * coverage of its top half, rd_dump.c sheetFromHalo); 12 the disc's region
 * slug after cullW (char[RD_DUMP_REGION_BYTES], NUL-padded: "pal", "jpn";
 * an older dump is "pal", RdDumpInfo.region).
 * rd__dump_frame writes this version; rd__load_frame reads 3 to it, and a
 * dump before 10 loads as the 4:3 frame (aspect 4/3, wExact 512, cullW 512).
 * A dump holds the picture as drawn at its width: a replay can crop it to a
 * narrower aspect, never widen it (rd_replay_tool). */
#define RD_DUMP_VERSION 12u
#define RD_DUMP_REGION_BYTES 8
bool rd__dump_frame(const RdFrame *f, const char *path);
bool rd__load_frame(const char *path, RdFrame *out);

/* A dump's header, read without loading it (rd_replay_tool sizes the
 * renderer and chooses the replay's aspect from it). */
typedef struct RdDumpInfo {
    uint32_t version;
    uint32_t gsW, gsH;
    float aspect;   /* 4/3 before version 10 */
    float wExact;   /* 512 before version 10 */
    uint32_t cullW; /* 512 before version 10 */
    /* the slug of the disc the frame was drawn on (a row of
       config/regions.txt; "pal" before version 12): rd_replay_tool selects
       it (ico_region_select) so the reduction's crop and border are that
       disc's */
    char region[RD_DUMP_REGION_BYTES];
} RdDumpInfo;

bool rd__dump_peek(const char *path, RdDumpInfo *info);

/* rd_water.c: render-to-texture surfaces (rd.h rd_alias_target,
 * rd_block_target, rd_push_camera).  The per-frame records live in rd_water.c,
 * one per frame slot, and are cleared by rd__frame_reset.
 *   rd__alias_of        the target rd_alias_target put in place of target id in
 *                      the current list of the open frame, 0 for none
 *   rd__camera_at       the camera of command index in list of f: the innermost
 *                      rd_push_camera scope that holds it, else the frame camera
 *                      (NULL when the frame has none)
 *   rd__camera_scopes   the number of scopes recorded in f
 *   rd__frame_projected rd_frame_projected is on and no rd_push_camera scope
 *                      is open: the draw recorded now gets
 *                      RD_SCREEN_FRAME_CAMERA / RD_SHADOW_FRAME_CAMERA */
typedef struct RdCameraScope {
    int32_t list;
    uint32_t start, end; /* command indices [start, end) of the list; end is
                            UINT32_MAX while open */
    RdCamera cam;
} RdCameraScope;

void rd__water_frame_reset(const RdFrame *f);
/* The F12 log line's summary of f's render-to-texture blocks (issue 106):
 * the Water reflections option, SCENE's scale, each block a draw of f wrote
 * or read (GS size, texels and scale, depth, the draws into it, the draws
 * reading it with their DATE, AFAIL and blend), the DATE draws by target,
 * and the draws sampling a texture-pack replacement (blank in a dump).  At
 * most cap - 1 characters; the length written.  Walks f once: for a dump,
 * never per frame */
size_t rd__water_dump_line(const RdFrame *f, char *line, size_t cap);
/* a draw samples src (rd_replay.c resolveTexture): counts it in
 * g_rdPerf.blockOtherScaleReads when src is a block not at SCENE's scale */
void rd__block_read(const RdTargetRec *src);
uint32_t rd__alias_of(uint32_t id);
/* rd_grid_screen_st's formula and scales for the grid draw being recorded,
 * and none for the draws after it (rd_mesh.c rd_draw_vu_grid) */
void rd__grid_screen_st_take(uint8_t *kind, float *sx, float *sy);
const RdCamera *rd__camera_at(const RdFrame *f, int list, uint32_t index);
uint32_t rd__camera_scopes(const RdFrame *f, const RdCameraScope **scopes);
bool rd__frame_projected(void);
/* the pipelines of these files' screen-prim states that the screen families
 * leave out (rd__enumerate_reachable adds them) */
uint32_t rd__enumerate_reachable_water(RdPipeKeyInt *out, uint32_t max, uint32_t n);

/* rd_replay.c: a screen-prim command under COLCLAMP 0 with an additive or
 * subtractive equation (ALPHA modes 0, 1, 5, 6, ABE on) wraps modulo 256
 * per channel as on the GS instead of clamping (doScreenWrap: wrap_acc_ps
 * into an RGBA16F accumulator, then wrap_resolve_ps).  True when state s
 * takes that path.  rd__wrap_shutdown frees the accumulator and the path's
 * pipelines (rd__gpu_shutdown); the pipelines are a private cache outside
 * rd__get_pipeline's, at most RD_WRAP_PIPES. */
bool rd__wrap_applies(const RdStateBlock *s);
void rd__wrap_shutdown(void);
uint32_t rd__wrap_pipeline_count(void);
#define RD_WRAP_PIPES 16

/* rd_blur_smooth.c: Options > Effects > Motion blur: Smooth
 * (RdSettings.motionBlurSmooth; docs/port/RENDER.md, "The motion blur
 * under Smooth").  The motion blur's sprite reads, instead of the DISPLAY
 * the tick started from, an RGBA16F accumulator of the scenes of every
 * present, A = S + a' (A - S), resolved into an RGBA8 scene-class temporary
 * target, with the sprite's FIX spread over the tick's presents:
 * FIX' = clamp(round(128 (FIX / 128)^(1 / k)), FIX, 127).
 * rd__blur_smooth_arm      the presenters, before a replay that presents
 *                          (rd_interp.c rd_present, rd_core.c replayOnHost;
 *                          the replay tool before its own): a tick's first
 *                          present, and k the presents a tick (1..64); a
 *                          replay not armed (a self-test, a dump's, --time)
 *                          draws the sprite as Original and leaves the
 *                          accumulator alone
 * rd__blur_smooth_replay_end  rd__replay_frame after every replay: an armed
 *                          replay that is not a keep frame's and drew no
 *                          motion blur sprite (the game's alpha 0, the
 *                          option off) ends the trail: the next sprite
 *                          starts it again from its own scene (FIX' 0)
 * rd__blur_smooth_active   Smooth is in force (the setting, and the
 *                          accumulator not refused this run):
 *                          rd_interp.c feedback leaves DISPLAY_HELD alone
 * rd__blur_smooth_free     the accumulator, its target and the restart
 *                          (rd_core.c: the targets recreated, Smooth to
 *                          Original); why is logged when it held one
 * rd__blur_smooth_shutdown the above and the two pipelines (rd__gpu_shutdown)
 * rd__blur_smooth_fix      FIX' of FIX over k presents (pure)
 * rd__blur_smooth_block    the 10 s summary's line (rd_interp.c presentLog),
 *                          logged when the block drew a Smooth sprite or
 *                          restarted, then the block's counts start again
 * rd__blur_smooth_sizes    the pair's and the target's texture sizes (0 when
 *                          none: tests)
 * rd__blur_smooth_counts   since start: the sprites drawn under Smooth, the
 *                          restarts, the sprites drawn as Original while
 *                          Smooth was set (tests)
 * k (rd_interp.c rd_present): the present clock's tick over its step
 * (rd_present_clock_alpha) on the timer path, the presents the tick before
 * counted under the vsync lock, 1 for the first tick */
void rd__blur_smooth_arm(int firstOfTick, uint32_t presentsPerTick);
void rd__blur_smooth_replay_end(int keep);
bool rd__blur_smooth_active(void);
void rd__blur_smooth_free(const char *why);
void rd__blur_smooth_shutdown(void);
uint8_t rd__blur_smooth_fix(uint32_t fix, uint32_t presentsPerTick);
void rd__blur_smooth_block(void);
void rd__blur_smooth_sizes(uint32_t acc[2], uint32_t res[2]);
void rd__blur_smooth_counts(uint64_t *drawn, uint64_t *restarts, uint64_t *original);
#define RD_BLUR_SMOOTH_PIPES 2
#define RD_BLUR_SMOOTH_K_MAX 64u

/* rd_crack_fill.c: the crack filler above 1x (issue 26; docs/port/RENDER.md,
 * "Crack filling above 1x").  Before the fog of SCENE (rd_replay.c doFog,
 * rd_replay.h rd__crack_fill), under Enhanced with SCENE's scale above 1 on
 * an axis, a texel no draw covered (depth 0, the fog's table entry 0), or
 * a drawn one behind both by a step in depth (a farther surface seen
 * through the crack), between texels in front of it within the reach takes
 * the nearer one's colour and depth.
 * rd__crack_fill_init      rd_init: the switch (ICO_RD_CRACK_FILL=0, [dev]
 *                          crack_fill = false: off; =1, true: on; unset:
 *                          RdProfile.crackFill, on but on a tile-based GPU)
 *                          and the counters from zero
 * rd__crack_fill_scene_made  rd_core.c after the named targets were made:
 *                          the state line when it changed ("crack fill: on
 *                          above 1x (scale ..., reach ...)", "off", "not
 *                          drawn ..."), a count in flight dropped
 * rd__crack_fill_set_enabled  the replay tool's --no-crack-fill, tests: the
 *                          switch for the run, over the environment
 * rd__crack_fill_count_all  every filled replay counted (tests, the tool's
 *                          --stats); the game counts none
 * rd__crack_fill_wait      waits for the GPU and reads the count in flight
 * rd__crack_fill_counts    since rd_init: replays filled, replays counted,
 *                          the pixels those filled, the last and the
 *                          largest count, the fogged replays above 1x not
 *                          filled, the last fill's scale and reach
 * rd__crack_fill_block     the 10 s summary's lines (rd_interp.c presentLog)
 * rd__crack_fill_reach     the reach on an axis at scale s: ceil(s / 2)
 *                          above 1, at most RD_CRACK_REACH_MAX, else 0
 * rd__crack_fill_shutdown  rd_shutdown: the count's target and buffer */
typedef struct RdCrackCounts {
    uint64_t filled, counted, pixels, skipped;
    uint32_t last, max;
    float sx, sy;
    uint32_t rx, ry;
    bool enabled;
} RdCrackCounts;

void rd__crack_fill_init(void);
void rd__crack_fill_scene_made(void);
void rd__crack_fill_set_enabled(bool on);
void rd__crack_fill_count_all(bool on);
void rd__crack_fill_wait(void);
void rd__crack_fill_counts(RdCrackCounts *out);
void rd__crack_fill_block(void);
uint32_t rd__crack_fill_reach(float scale);
void rd__crack_fill_shutdown(void);
#define RD_CRACK_REACH_MAX 3u
/* a texel is in front of a crack's texel when nearer by more than
 * 1 / RD_CRACK_BEHIND of its own depth (blit.hlsl crack_front) */
#define RD_CRACK_BEHIND 64u

/* rd_png.c: 8-bit RGBA (or RGB with withAlpha = 0), stored deflate.  The
 * source is RGBA8 either way, rows pitch (>= w * 4) bytes apart. */
bool rd_write_png(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t pitch,
                  int withAlpha);

#ifdef __cplusplus
}

#endif
#endif /* PORT_RENDER_RD_INTERNAL_H */
