/* rd_mesh.h: the mesh draws on the VU1 program shaders (wave 3, R3c). Header
 * only: packages R3a (Packet.c / RegistPacket.c, static and skinned
 * objects) and R3b (Primitive.c grids and particles) implement it in
 * rd_core.c / rd_replay.c.
 *
 * The VU1 programs and what each computes: docs/port/VU1_PROGRAMS.md. The
 * shaders: port/shaders/vu_*.hlsl, binding and constant layouts in
 * docs/port/SHADERS.md ("VU1 programs") and shader_consts.h (IcoVuCB,
 * IcoVuBoneCB). CPU references (test oracle, software fallback):
 * port/render/vu1_ref/vu1_ref.h.
 *
 * Principle: the GPU gets what the VU got. The vertex stream is the VIF
 * UNPACK payload Packet.c / Primitive.c built (float4 quadwords, the GIF tag
 * of each batch included), the per-draw block is the VU1 data memory the
 * program reads (RdVuBlock = VuCB.mem), and the MSCALF code mc_SetMicroCode
 * chose picks the shader entry and the clip mode. Nothing is re-derived from
 * semantic transforms, so no rounding or convention differs from the PS2's
 * before the shader runs.
 *
 * Relation to rd.h: RdProg, RdMesh, RdMaterial and RdKey are rd.h's. rd.h's
 * rd_CreateMesh / rd_DrawMesh / rd_DrawSkinned / rd_DrawGrid /
 * rd_DrawParticles (wave 0, recorded as stubs) carry semantic transforms
 * (RdXform, RdLights) that cannot hold what the programs read: RdXform has no
 * slot for the scissor clip matrix (+0x200 x model, VU memory 20..23), and
 * RdLights keeps three directions and colours where the programs multiply by
 * two full 4 x 4 matrices (the light matrix's fourth column meets n.w).
 * The functions below are the exact path; R3a/R3b call them from the seki
 * sites and either drop the five semantic declarations or keep them for an
 * Enhanced path (a RENDER_API.md note either way). No edit to rd.h is
 * needed for this header. */
#ifndef PORT_RENDER_RD_MESH_H
#define PORT_RENDER_RD_MESH_H

#include <stdint.h>

#include "rd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------ programs
 *
 * resident program  code  RdProg            shader entry      clip mode
 * normal_c (id 1)   32    RD_PROG_PRELIT    vu_prelit_vs      REGION
 *                   34    RD_PROG_PRELIT    vu_prelit_vs      NONE
 *                   36    RD_PROG_PRELIT    vu_prelit_vs      SCISSOR
 * normal_l (id 2)   32    RD_PROG_LIT       vu_lit_vs         REGION
 *                   34    RD_PROG_LIT_SPEC  vu_lit_spec_vs    REGION
 *                   36    RD_PROG_LIT       vu_lit_vs         SCISSOR
 *                   38    RD_PROG_REFLECT   vu_reflect_vs     REGION
 * cluster (id 3)    20    RD_PROG_SKIN      vu_skin_vs        REGION
 *                   22    RD_PROG_SKIN_SPEC vu_skin_spec_vs   REGION
 *                   24    RD_PROG_SKIN_SPEC vu_skin_debug_vs  REGION (debug_specular_flag)
 * mesh (id 4)       20    RD_PROG_GRID      vu_grid_vs        REGION
 *                   22    RD_PROG_GRID_LIT  vu_grid_lit_vs    REGION
 *                   24    RD_PROG_GRID_LIT  vu_grid_spec_vs   REGION (debug_specular_flag)
 * particle (id 5)   18    RD_PROG_PARTICLE  vu_particle_vs    (per particle)
 *
 * Pixel shader: vu_ps for all (sprite_ps's texture function, TEXA, alpha
 * test, DATE, dual-source output, on STQ). The code is what mc_SetMicroCode
 * returns for (mode, light, pass, clip); which program is resident comes
 * from reg_transMicroCode (lightMtx->mode, model->disp), so the pair
 * (program, code) is what selects the row, not the code alone. A code with
 * no row for the resident program (normal_c 38: SET_GSREGISTER's first
 * bundle) never occurs with consistent data; record it as an error. */

typedef enum RdVuClip {
    RD_VU_CLIP_REGION = 0, /* ICO_VU_CLIP_REGION: a vertex outside the GS window (x, y in
                             * 0..4094 exclusive, 0 < w < 16777214; cluster: mem[0]..mem[1])
                             * keeps every triangle that uses it from being drawn */
    RD_VU_CLIP_NONE = 1,   /* ICO_VU_CLIP_NONE: normal_c 34, everything drawn, X/Y wrap */
    RD_VU_CLIP_SCISSOR = 2 /* ICO_VU_CLIP_SCISSOR: code 36, clip in mem[20..23]'s space */
} RdVuClip;

/* Quadwords per vertex in the stream (pac_makeNormalStrip,
 * pac_makeClusterStrip, prim_makePacketMesh3D, PrimParticleObj). */
enum {
    RD_VU_QW_PRELIT = 3,   /* pos, ST (s, t, 1, strip flag), colour (r, g, b, 127) floats */
    RD_VU_QW_LIT = 4,      /* pos, normal, ST, colour */
    RD_VU_QW_SKIN = 5,     /* pos, normal, weights (int addr0, w0, int addr1, w1), ST, colour */
    RD_VU_QW_GRID = 2,     /* pos, ST; per batch: GIF tag, colour (two header qwords) */
    RD_VU_QW_GRID_LIT = 3, /* pos, normal, ST */
    RD_VU_QW_PARTICLE = 2  /* (x, y, z, size), (u, v, grey, alpha); header: 6 qwords */
};

/* ---------------------------------------------------- static meshes (R3a)
 *
 * One RdMesh per PObjPart (pac_MakePacket), built once at load from the
 * packet Packet.c already makes: every VIF UNPACK payload (GIF tag, then
 * vertices; the DMA tags, VIF codes and the closing MSCNT left out) back to
 * back in the stream, in packet order. */

typedef struct RdVuBatchDesc {
    uint32_t firstQw;  /* the batch's GIF tag in qw[] (its NLOOP is the vertex count) */
    uint16_t material; /* index into the RdMaterial array of the draw (PacHeader.mat) */
    uint16_t group;    /* the PObjGroup / PacHeader it came from, for the dissolve and
                             specular passes that draw one group again */
} RdVuBatchDesc;

typedef struct RdVuMeshDesc {
    const float (*qw)[4]; /* the stream as Packet.c built it, read at creation */
    uint32_t qwCount;
    uint32_t qwPerVertex; /* RD_VU_QW_PRELIT / _LIT / _SKIN */
    uint32_t batchCount;
    const RdVuBatchDesc *batches;
    uint32_t materialCount;
    const char *debugName;
} RdVuMeshDesc;

/* Builds, once:
 *   - the device vertex buffer (t0, space1): the batches' vertices without
 *     their GIF tags, numbered 0.. across the mesh in batch order (the
 *     shaders read no tag field: the count is in the index list, and each
 *     batch's PRIM bits, TME and ABE, are kept per batch for the material
 *     state); drawn with vu_draw = (0, qwPerVertex, flags, 0), vu_batch = 0;
 *   - the index buffer (uint32): for each batch and each of its vertices k
 *     (k-th in the batch, K in the mesh numbering) with k >= 2 and neither
 *     vertex k nor k-1 carrying the strip flag (ST.w < 1), the three indices
 *     ICO_VU_INDEX(K, 0..2) = K * 4 + corner (vu1ref_StaticKicks is the
 *     rule); per batch its (firstIndex, indexCount).
 * Replay draws a run of batches with one material in one indexed draw
 * (REGION, NONE), or batch by batch, twice, under SCISSOR (see below). */
RdMesh rd_CreateVuMesh(const RdVuMeshDesc *desc);

/* --------------------------------------------------------- per draw */

/* VU1 data memory 0..35 as the program reads it: IcoVuCB.mem, the slot map
 * in vu_common.hlsli. Filled by the seki sites from the packets they build:
 *   0..15  the RdVuCommon block in force at the draw's list position
 *          (rd_GetVuCommon; RENDER_API.md section 12, open question 1)
 *          with qw 2 as SET_UVOFFSET left it (xy: the bound texture's
 *          t->uv scroll or clearUVOffset's zero; it persists in VU memory
 *          until the next SET_UVOFFSET or common block, so a draw without
 *          its own texture packet inherits it) and, for cluster, w = the
 *          fade alpha of reg_setCMatrixPacket
 *   16..27 normal programs: reg_setNMatrixPacket / reg_setMMatrixPacket's 12
 *          qwords (+0x140 x node, +0x200 x node, +0x80 x node). Mesh:
 *          setMatrix's matrix at 16..19. Particle: mtx at 16..19, lmtx
 *          (matrixptr + 0xC0) at 20..23
 *   28..35 the 8 light qwords (normal: reg_set*MatrixPacket_setLight;
 *          cluster: reg_setCMatrixPacket_light; mesh: setLight's lb then la) */
typedef struct RdVuBlock {
    float mem[36][4];
} RdVuBlock;

typedef struct RdVuDraw {
    RdProg prog;
    uint8_t code; /* the MSCALF code */
    uint8_t clip; /* RdVuClip, from the table above */
    uint8_t _pad[2];
    RdVuBlock vu;
    /* skinned: the quadwords SET_CLUSTER_MATRIX copied to VU memory 16..
     * (bone i at 16 + 4 i: nodeMtx x clusterMtx), boneQw = 4 x bones; the
     * draw binds them as VuBoneCB (b3, space1) */
    const float (*bones)[4];
    uint32_t boneQw;
    /* batches to draw (a reg_disp* pass draws a material group or all) */
    uint32_t firstBatch, batchCount;
    const RdMaterial *materials; /* the mesh's materialCount entries */
} RdVuDraw;

/* reg_dispNObj / reg_dispLObj / reg_dispCObj and the specular, reflection
 * and dissolve passes: records an RDC_MESH (or RDC_SKINNED when bones is
 * set) with the payload below. */
void rd_DrawVuMesh(RdMesh m, const RdVuDraw *d, RdKey key);

/* ------------------------------------------------- grids (R3b, mesh.vsm)
 * prim_DispMesh3D: the Mesh3D packet buffer of the frame (m->bufs[buffer_ID],
 * Mesh3D.qwc quadwords) copied into the frame as it is. Per strip it holds
 * the DpkHead's VIF qword, the GIF tag, the colour, stripLen vertices and
 * the MSCNT qword; every strip has stripLen vertices, so one draw covers the
 * grid: vu_draw = (0, qwPerVertex, flags, stripLen), vu_batch = (3, 1): the
 * shader skips the VIF qword with the header and the MSCNT with the
 * trailer, and reads the colour as the header's last qword. Indices: per
 * strip b, k = 2 .. stripLen - 1 (no strip flag in mesh.vsm). */
typedef struct RdVuGridDraw {
    const float (*qw)[4]; /* Mesh3D.bufs[buffer_ID], Mesh3D.qwc quadwords */
    uint32_t strips;      /* Mesh3D.strips */
    uint32_t stripLen;    /* Mesh3D.stripLen: vertices per batch */
    uint32_t lit;         /* Mesh3D.lit: RD_VU_QW_GRID_LIT layout */
    uint8_t code;         /* 20, 22 or 24 */
    uint8_t _pad[3];
    RdVuBlock vu; /* mem[16..19] = setMatrix, mem[28..35] = setLight (lb, la) */
    RdMaterial material;
} RdVuGridDraw;

void rd_DrawVuGrid(const RdVuGridDraw *d, RdKey key);

/* ------------------------------------------ particles (R3b, particle.vsm)
 * prim_DispParticle: the PrimParticleObj of the frame from num on (count,
 * the two GIF tags, clip window, (size scale, du, dv), two qwords per
 * particle), 6 + 2 * count quadwords, copied. Drawn non-indexed, six
 * vertices per particle; a skipped particle (alpha 0, a corner outside the
 * window) gives an empty quad. */
typedef struct RdVuParticleDraw {
    const float (*qw)[4];
    uint32_t count; /* PrimParticle.num (at most 80) */
    uint32_t _pad;
    RdVuBlock vu;        /* mem[16..19] = mtx, mem[20..23] = lmtx (SET_PARTICLE_MATRIX) */
    RdMaterial material; /* the particle texture; PRIM 0xD6: TME, ABE (AA1 not reproduced) */
} RdVuParticleDraw;

void rd_DrawVuParticles(const RdVuParticleDraw *d, RdKey key);

/* ------------------------------------------------- the recorded payload
 *
 * RDC_MESH / RDC_SKINNED / RDC_GRID / RDC_PARTICLES keep their rd_internal.h
 * meaning (u[0] mesh, b[0] RdProg, u[1] payload offset, u[2] payload size)
 * with this payload in the frame arena, 16-byte aligned:
 *
 *   RdVuPayload                  the fixed part
 *   RdVuBlock                    VuCB.mem for the draw
 *   bones[boneQw][4]             RDC_SKINNED only
 *   stream[streamQw][4]          RDC_GRID / RDC_PARTICLES only (the frame's copy)
 *   RdMaterial[materialCount]
 *
 * Replay per batch in order (the PS2 draws batch after batch, material
 * packets between them): bind the batch's material state, then
 *   REGION / NONE: one indexed draw of the batch's index range;
 *   SCISSOR: two, vu_draw.z | ICO_VU_CUT_ONLY then | ICO_VU_KICK_ONLY:
 *     SCISSOR_COMMON kicks its fans during the loop, before the batch's own
 *     packet, so the clipped triangles of a batch come first;
 * merging batches with the same material and clip mode into one draw is
 * correct except under SCISSOR (the cut-first order is per batch). */
typedef struct RdVuPayload {
    uint8_t code, clip, prog, _pad;
    uint32_t firstBatch, batchCount;
    uint32_t boneQw;        /* RDC_SKINNED */
    uint32_t streamQw;      /* RDC_GRID / RDC_PARTICLES: the copied stream */
    uint32_t vertsPerBatch; /* RDC_GRID: stripLen; RDC_PARTICLES: count */
    uint32_t qwPerVertex;
    uint32_t materialCount;
} RdVuPayload;

#ifdef __cplusplus
}
#endif

#endif
