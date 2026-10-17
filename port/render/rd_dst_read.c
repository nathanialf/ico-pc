/* rd_dst_read.c: the exact GS blend, computed in the fragment shader from
 * the destination.
 *
 * The GS blends in integers,
 *     out = ((A - B) * C >> 7) + D,   A, B, D in {Cs, Cd, 0}, C in {As, Ad, FIX}
 * (arithmetic shift: toward minus infinity), 0x80 = 1.0 with no clamp of C,
 * then COLCLAMP (clamp to 0..255, or wrap to 8 bits), PABE (an As with its
 * MSB clear writes Cs unblended), FBA (the stored alpha's MSB set); the
 * alpha channel is never blended (As is stored); FBMSK masks channels.
 * The GPU's fixed-function blender computes Cs * F + Cd * (1 - F) in float
 * and rounds to nearest, one level above the GS on about half the channels
 * of a LERP (the forest's full-screen tint sprite, ALPHA 0x44: 50.0 % of
 * the channels one level up against PCSX2's software renderer, whose
 * pixels all match the floor formula; over a frame the error only adds).
 * The difference depends on Cd, so no change of Cs or F alone fixes it.
 *
 * The destination read: the fragment shader reads its own pixel in
 * rasterization order, computes gs_dst_channel (gs_math.hlsli) and stores
 * the result with blending off, exact for any overlap within a draw, in the
 * draw's own pass.  Two realisations (rhi.h RhiDstRead):
 *   attachment  the colour attachment read as an input attachment in
 *               rasterization order (Vulkan rasterization-order attachment
 *               access over dynamic rendering local read).  Depth,
 *               stencil, discard and write mask stay fixed-function.
 *               Every pass on a readable RGBA8 target is begun readable; a
 *               pending clear is recorded as its own pass before it (the
 *               load op's ordering against input attachment reads in the
 *               same pass is not relied on).
 *   storage     the target as a rasterizer-ordered view (D3D12 ROV; Vulkan
 *               storage image under fragment shader interlock).  The target
 *               is no colour attachment then, so a blended draw runs in a
 *               storage pass with the depth attached and no colour
 *               (rd_replay.c dstPassMode).  The depth and stencil tests run
 *               before the shader ([earlydepthstencil]), so a draw that
 *               writes Z and can discard (alpha test, DATE) is split into
 *               colour without Z write then a depth-only pass when its Z
 *               test is ALWAYS; under GEQUAL/GREATER it stays on the
 *               fixed-function blend (logged once).
 * The preference is attachment, then storage, then the fixed-function
 * blend (logged "fixed-function (rounds up)").  The attachment read is not
 * offered on Qualcomm's own Adreno driver (vk_device.c: its reads return
 * wrong colours, issue 89; RhiLimits.rasterOrderOff says so and the blend
 * line carries it).  rd_blend_selftest (rd_blend_selftest.c) then draws a
 * known frame through each mode at start and keeps the first that is
 * right on the device.
 *
 * What goes through it: the screen prims, the VU meshes and the fog sprite
 * whose fixed-function blend is not the GS's (dstWant, rd_pipeline.c): the
 * LERPs (modes 2, 4, 7), Cd*FIX + Cs (3), the Ad forms (8 to 10), Cd*As +
 * Cd (11), PABE on the premultiplied forms, and COLCLAMP 0, on RGBA8
 * targets, in every preset.  Left on the fixed-function blend: the
 * premultiplied adds and subtracts (0, 1, 5, 6; the adds exact in
 * hardware, the subtracts one level above the GS where Cs*F is not a
 * multiple of 128: the shader truncates Cs*F >> 7 and the blender
 * subtracts it, where the GS shifts the negative (0 - Cs)*F down), the
 * MSAA epoch's passes and the fog drawn while it is open, targets other
 * than RGBA8, the port's presentation overlay.  Under the storage mode the
 * storage pass merge (rd__dst_merge_ok, issue 103) draws the unblended
 * passes and the premultiplied adds of an open storage pass through the
 * read too, the same bytes, so the pass need not end for them.  The staticBlur sprites and
 * blend_int keep their own exact paths; DATE reads its snapshot.  See
 * docs/port/RENDER.md "Destination reads and blend paths".
 */
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "shader_consts.h"

const char *rd__blend_exact_name(int mode)
{
    switch (mode) {
    case RD_BLEND_EXACT_STORAGE:
        return rhi_backend() == RHI_BACKEND_D3D12 ? "exact (rov)" : "exact (interlock)";
    case RD_BLEND_EXACT_ATTACHMENT:
        return "exact (attachment read)";
    default:
        return "fixed-function (rounds up)";
    }
}

bool rd__fs_has_dst(uint8_t fs)
{
    switch (fs) {
    case RD_FS_SPRITE:
    case RD_FS_SPRITE_TEXA:
    case RD_FS_SPRITE_AA1:
    case RD_FS_SPRITE_STQ:
    case RD_FS_FONT:
    case RD_FS_FONT_SHEET:
    case RD_FS_FOG:
    case RD_FS_FOG_BUFFER:
    case RD_FS_VU:
    case RD_FS_VU_TEXA:
    case RD_FS_VU_LEGACY:
    case RD_FS_VU_LEGACY_TEXA:
        return true;
    default:
        return false;
    }
}

/* the modes whose shaders and layout rd__gpu_init made (every twin) */
static uint8_t madeModes(void)
{
    uint8_t m = 1u << RD_BLEND_EXACT_OFF;
    bool rov = g_rd.layoutDstStorage.id != 0, att = g_rd.layoutDstAttach.id != 0;
    for (int i = 0; i < RD_FS_COUNT; i++) {
        if (rd__fs_has_dst((uint8_t)i)) {
            rov = rov && g_rd.fsDstRov[i].id;
            att = att && g_rd.fsDstAtt[i].id;
        }
    }
    if (rov) {
        m |= 1u << RD_BLEND_EXACT_STORAGE;
    }
    if (att) {
        m |= 1u << RD_BLEND_EXACT_ATTACHMENT;
    }
    return m;
}

/* the mode ICO_RD_BLEND asked for and the device has, -1 when none */
static int s_asked = -1;

int rd__dst_asked(void)
{
    return s_asked;
}

/* the platform default took the fixed-function blend from a device that has
 * an exact mode (MoltenVK, issue 102), not ICO_RD_BLEND */
static bool s_platformFixed;

bool rd__dst_platform_fixed(void)
{
    return s_platformFixed;
}

void rd__dst_init(void)
{
    g_rd.dstModes = g_rd.hasDevice ? madeModes() : (uint8_t)(1u << RD_BLEND_EXACT_OFF);
    s_asked = -1;
    s_platformFixed = false;
    int want = RD_BLEND_EXACT_OFF;
    if (g_rd.dstModes & (1u << RD_BLEND_EXACT_ATTACHMENT)) {
        want = RD_BLEND_EXACT_ATTACHMENT;
    } else if (g_rd.dstModes & (1u << RD_BLEND_EXACT_STORAGE)) {
        want = RD_BLEND_EXACT_STORAGE;
    }
    const char *e = getenv("ICO_RD_BLEND");
    if (e && e[0]) {
        int asked = -1;
        if (strcmp(e, "fixed") == 0 || strcmp(e, "off") == 0 || strcmp(e, "0") == 0) {
            asked = RD_BLEND_EXACT_OFF;
        } else if (strcmp(e, "storage") == 0 || strcmp(e, "interlock") == 0 ||
                   strcmp(e, "rov") == 0) {
            asked = RD_BLEND_EXACT_STORAGE;
        } else if (strcmp(e, "attachment") == 0) {
            asked = RD_BLEND_EXACT_ATTACHMENT;
        }
        if (asked < 0 || !(g_rd.dstModes & (1u << asked))) {
            rd__log("blend: ICO_RD_BLEND=%s %s; %s", e,
                    asked < 0 ? "is not fixed, storage or attachment" : "is not on this device",
                    rd__blend_exact_name(want));
        } else {
            want = asked;
            s_asked = asked;
        }
    }
    /* MoltenVK: the exact blend's pass switching (rd_replay.c dstPassMode)
     * measured 54 ms a replay and 750 passes against 14 ms and 49 with the
     * fixed-function blend (an M1, issue 102; 16.7 ms with MSAA, which
     * draws fixed too).  One rule for macOS and iOS, here; ICO_RD_BLEND
     * ([dev] blend) above has forced any mode */
    const bool mvk = g_rd.hasDevice && rhi_limits()->moltenvk;
    if (mvk && s_asked < 0 && want != RD_BLEND_EXACT_OFF) {
        want = RD_BLEND_EXACT_OFF;
        s_platformFixed = true;
    }
    g_rd.dstMode = (uint8_t)want;
    const char *off = g_rd.hasDevice ? rhi_limits()->rasterOrderOff : NULL;
    if (s_platformFixed) {
        rd__log("blend: %s (MoltenVK: the exact blend's pass switching costs 3 to 4 times the "
                "frame; [dev] blend = storage keeps it)",
                rd__blend_exact_name(want));
    } else {
        rd__log("blend: %s%s%s%s%s", rd__blend_exact_name(want), off ? " (" : "", off ? off : "",
                off ? ")" : "",
                mvk && s_asked >= 0 ? " (forced: MoltenVK's default is fixed-function)" : "");
    }
}

bool rd_set_blend_exact(RdBlendExact mode)
{
    if ((unsigned)mode >= RD_BLEND_EXACT_COUNT || !(g_rd.dstModes & (1u << mode))) {
        return false;
    }
    if (g_rd.dstMode != (uint8_t)mode) {
        rd__log("blend: %s", rd__blend_exact_name(mode));
    }
    g_rd.dstMode = (uint8_t)mode;
    return true;
}

RdBlendExact rd_blend_exact(void)
{
    return (RdBlendExact)g_rd.dstMode;
}

uint32_t rd__dst_target_usage(RhiFormat fmt, uint8_t *dstUsage)
{
    uint32_t usage = 0;
    uint8_t u = 0;
    /* every mode the device has, whichever is in force: rd_set_blend_exact
     * switches without remaking the targets */
    if (fmt == RHI_FMT_RGBA8_UNORM) {
        if (g_rd.dstModes & (1u << RD_BLEND_EXACT_STORAGE)) {
            usage |= RHI_TEX_STORAGE;
            u |= 1u;
        }
        if (g_rd.dstModes & (1u << RD_BLEND_EXACT_ATTACHMENT)) {
            usage |= RHI_TEX_INPUT_ATTACHMENT;
            u |= 2u;
        }
    }
    if (dstUsage) {
        *dstUsage = u;
    }
    return usage;
}

/* the fragment may be discarded after the depth test: the alpha test
 * (ate in modeZ), DATE, a VU draw's clip plane (rd_chain.h), the fog's own
 * depth test in the shader */
static bool passDiscards(const RdDrawPass *p)
{
    return (p->modeZ & (1u << 8)) != 0 || (p->flags & (ICO_DF_DATE | ICO_DF_PLANE)) != 0 ||
           p->key.fs == RD_FS_FOG || p->key.fs == RD_FS_FOG_BUFFER;
}

/* the flags that only the fixed-function paths read */
#define BLENDER_FLAGS                                                                              \
    ((uint32_t)ICO_DF_FIX_FACTOR | (uint32_t)ICO_DF_PREMUL | (uint32_t)ICO_DF_C1_DST |             \
     (uint32_t)ICO_DF_NODUAL_FACTOR | (uint32_t)ICO_DF_NODUAL_ALPHA_PASS)

static RdDrawPass dstPass(const RdDrawPass *p, int mode)
{
    RdDrawPass c = *p;
    c.key.dst =
        (uint8_t)(mode == RD_BLEND_EXACT_STORAGE ? RHI_DST_READ_STORAGE : RHI_DST_READ_ATTACHMENT);
    c.key.gs.blend = RD_BLEND_COUNT; /* blending off: the shader blends */
    c.key.gs.nodual = 0;
    c.flags &= ~(BLENDER_FLAGS | (15u << ICO_DF_WMASK_SHIFT));
    c.flags |= (uint32_t)(c.key.gs.colorMask & 15u) << ICO_DF_WMASK_SHIFT;
    c.fix = c.fixGs; /* FIX past 0x80 overshoots on the GS */
    c.dstWant = 0;
    return c;
}

/* the depth-only twin of a pass: its Z test, Z write and discards, no
 * colour */
static RdDrawPass depthOnly(const RdDrawPass *p)
{
    RdDrawPass z = *p;
    z.key.gs.colorMask = 0;
    z.key.gs.blend = RD_BLEND_COUNT;
    z.flags &= ~BLENDER_FLAGS;
    z.dstWant = 0;
    return z;
}

static bool eligible(const RdPipeKeyInt *k)
{
    return k->dst == RHI_DST_READ_NONE && k->colorFmt == RHI_FMT_RGBA8_UNORM &&
           rd__fs_has_dst(k->fs) && (k->gs.colorMask & 7u) != 0 && k->gs.nodual == 0 &&
           k->gs.stencil == RD_STENCIL_OFF && k->gs.msaa <= 1;
}

int rd__expand_dst(const RdDrawPass *in, int n, int mode, RdDrawPass out[4])
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        const RdDrawPass *p = &in[i];
        if (mode == RD_BLEND_EXACT_OFF || !p->dstWant || !eligible(&p->key)) {
            out[m++] = *p;
            continue;
        }
        if (mode == RD_BLEND_EXACT_STORAGE && p->key.gs.zwrite == RD_ZWRITE_ON &&
            p->key.depthFmt != RHI_FMT_UNKNOWN && passDiscards(p)) {
            if (p->key.gs.ztst == RD_ZTST_ALWAYS || p->key.gs.ztst == RD_ZTST_NEVER) {
                /* Z ALWAYS: the colour pass (early tests, no Z write) and
                 * the depth pass admit the same fragments, and the depth
                 * pass leaves the last one's Z, as the GS */
                out[m] = dstPass(p, mode);
                out[m].key.gs.zwrite = RD_ZWRITE_OFF;
                m++;
                out[m++] = depthOnly(p);
            } else {
                rd__log_once(RD_ONCE_DST_ZWRITE,
                             "blend: exact (storage): a draw that writes Z under Z GEQUAL or "
                             "GREATER and can discard stays on the fixed-function blend");
                out[m++] = *p;
            }
            continue;
        }
        out[m++] = dstPass(p, mode);
    }
    return m;
}

bool rd__dst_merge_ok(const RdDrawPass *p)
{
    if (p->dstWant || !eligible(&p->key)) {
        return false;
    }
    const RdBlendPath bp = rd__blend_path(p->key.gs.blend);
    if (bp != RD_BP_NONE && bp != RD_BP_PREMUL_ADD) {
        return false;
    }
    return !(p->key.gs.zwrite == RD_ZWRITE_ON && p->key.depthFmt != RHI_FMT_UNKNOWN &&
             passDiscards(p));
}

RdDrawPass rd__dst_merge(const RdDrawPass *p)
{
    RdDrawPass c = dstPass(p, RD_BLEND_EXACT_STORAGE);
    c.csOnly = (uint8_t)(rd__blend_path(p->key.gs.blend) == RD_BP_NONE);
    return c;
}

uint32_t rd__enumerate_dst(const RdPipeKeyInt *in, uint32_t n, RdPipeKeyInt *out, uint32_t max,
                           uint32_t count)
{
    const int mode = g_rd.dstMode;
    if (mode == RD_BLEND_EXACT_OFF) {
        return count;
    }
    for (uint32_t i = 0; i < n; i++) {
        if (!eligible(&in[i])) {
            continue;
        }
        RdDrawPass p;
        memset(&p, 0, sizeof(p));
        p.key = in[i];
        const RdBlendPath bp = rd__blend_path(in[i].gs.blend);
        if (bp == RD_BP_NONE || bp == RD_BP_PREMUL_ADD) {
            /* the storage pass merge's image (rd__dst_merge), the same key
             * as a blended pass's: the start-up set has it when the device
             * has the storage mode and the merge is on */
            if (mode == RD_BLEND_EXACT_STORAGE && g_rd.passMerge) {
                const RdDrawPass m = rd__dst_merge(&p);
                count = rd__add_pipe_key(out, max, count, &m.key);
            }
            if (bp == RD_BP_NONE) {
                continue;
            }
        }
        p.dstWant = 1;
        /* a key keeps neither the alpha test nor DATE: the pass as it is,
         * and under the storage mode with Z write also split */
        RdDrawPass ex[4];
        int ne = rd__expand_dst(&p, 1, mode, ex);
        if (mode == RD_BLEND_EXACT_STORAGE && p.key.gs.zwrite == RD_ZWRITE_ON &&
            p.key.depthFmt != RHI_FMT_UNKNOWN) {
            ex[ne] = dstPass(&p, mode);
            ex[ne].key.gs.zwrite = RD_ZWRITE_OFF;
            ne++;
            ex[ne++] = depthOnly(&p);
        }
        for (int j = 0; j < ne; j++) {
            /* what the two-pass fallback makes of a non-dst image (the
             * depth-only pass under ICO_RD_NO_DUAL) */
            RdDrawPass nd[4];
            const int nn = rd__expand_no_dual(&ex[j], 1, nd);
            for (int k = 0; k < nn; k++) {
                count = rd__add_pipe_key(out, max, count, &nd[k].key);
            }
        }
    }
    return count;
}
