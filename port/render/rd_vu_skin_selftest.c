/* rd_vu_skin_selftest.c: the skin check's start-up probe (S1;
 * rd_vu_skin_check.h; docs/port/VU_SKIN_CHECK.md; issue 163).
 *
 * An early warning, not the decider (the in-game check sees the game's own
 * draws): a VuBoneCB of 60 distinct bones (addresses 16..252, each a
 * rotation, a scale, a shear and a move of its own) and a stream of 120
 * vertices whose weight qwords name every address in both lanes (lane x
 * the vertex mod 60, lane z (7 v + 13) mod 60), with mixed weights, are
 * drawn in probe mode (ICO_VU_PROBE: GS X, Y, Z of each vertex) through the
 * device's cluster entries for codes 20, 22 and 24 (vu_skin_vs,
 * vu_skin_spec_vs, vu_skin_debug_vs, or their plain variants where the
 * fallback is plain: the vertex modules the game's skinned draws take),
 * with the block at 64 offsets of a frame's ring, one in each 64-byte phase
 * of a 4 KB page (where the uniform alignment is coarser, its phases),
 * under the device's robust access as it is; each of the 192 probes against
 * vu1_ref's cluster program on the CPU.  A frame of its own (the ring's
 * slot as a replay takes it), read back at once: the start waits for it.
 *
 *   rd: VU skin probe: ok (120 vertices x 64 offsets, codes 20 22 24, plain
 *     set) in T ms
 *   rd: VU skin probe: wrong at offset O (page phase P), code C: N vertices,
 *     address A read as zero (got (x, y, z) want (x, y, z)) in T ms
 *   rd: VU skin check: on (...)  the in-game check's start line
 *
 * Nothing is turned off by a wrong probe: the in-game check decides.  A
 * device that fails in it (lost, its picture not read back) turns the
 * check off ("VU skin check: off (the start-up probe failed: ...)"), and
 * the window writes [dev] vu_skin_check = false (rd.h
 * rd_vu_skin_probe_failed); its notes name it to the start-up retry and
 * the activity file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_replay.h"
#include "rd_vu_skin_check.h"
#include "shader_consts.h"

#define ST_VERTS 120u
#define ST_BONES 60u
#define ST_OFFSETS 64u
#define ST_CODES 3u

static const int kCode[ST_CODES] = {20, 22, 24};
static const uint8_t kVs[ST_CODES] = {RD_VS_VU_SKIN, RD_VS_VU_SKIN_SPEC, RD_VS_VU_SKIN_DEBUG};

static float fbits(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static void qw(float *d, float x, float y, float z, float w)
{
    d[0] = x;
    d[1] = y;
    d[2] = z;
    d[3] = w;
}

/* VU memory 0..35: the region bounds, the fade, the common GIF tag, the
 * cluster's world to GS screen (4..7: x' = 16 x + 2048 z, y' = 16 y + 2048
 * z, z' = 3 * 2^20 (z - 1), w' = z) and its two light matrices (28..35) */
static void block(float (*m)[4])
{
    memset(m, 0, 36 * sizeof(m[0]));
    qw(m[0], 0, 0, 0, 1);
    qw(m[1], 4095, 4095, 0, 16777215);
    qw(m[2], 0, 0, 0, 1);
    qw(m[3], fbits(0x8000u), fbits(0x302EC000u), fbits(0x512u), 0);
    qw(m[4], 16, 0, 0, 0);
    qw(m[5], 0, 16, 0, 0);
    qw(m[6], 2048, 2048, 3145728, 1);
    qw(m[7], 0, 0, -3145728, 0);
    qw(m[28], 0, 1, 0, 0);
    qw(m[29], 0, 0, 1, 0);
    qw(m[30], 1, 0, 0, 0);
    qw(m[31], 0, 0, 0, 1);
    qw(m[32], 0.5f, 0.5f, 0.5f, 0);
    qw(m[33], 0.25f, 0, 0, 0);
    qw(m[34], 0, 0.25f, 0, 0);
    qw(m[35], 0.125f, 0.125f, 0.25f, 0);
}

/* bone b at qwords 4 b .. 4 b + 3, as columns (vu_mat: c0 x + c1 y + c2 z
 * + c3 w): a turn about z, a scale, a shear and a move, none the same */
static void bones(float (*q)[4])
{
    memset(q, 0, 240 * sizeof(q[0]));
    for (uint32_t b = 0; b < ST_BONES; b++) {
        const float c = 1.0f - 0.0012f * (float)(b * b % 41), sn = 0.03f * (float)(b % 11) - 0.15f;
        const float s = 0.9f + 0.004f * (float)b;
        qw(q[4 * b + 0], s * c, s * sn, 0.01f * (float)(b % 5), 0);
        qw(q[4 * b + 1], -s * sn, s * c, 0.015f * (float)(b % 3), 0);
        qw(q[4 * b + 2], 0.02f * (float)(b % 4), -0.01f * (float)(b % 6), 1.0f + 0.003f * (float)b,
           0);
        qw(q[4 * b + 3], 0.3f * (float)(b % 10) - 1.5f, 0.25f * (float)(b % 8) - 1.0f,
           0.05f * (float)(b % 9), 1);
    }
}

/* vertex v: pos, normal, weights (address lane x, w0, lane z, w1), ST,
 * colour (pac_makeClusterStrip's) */
static void stream(float (*q)[4])
{
    for (uint32_t v = 0; v < ST_VERTS; v++) {
        float (*o)[4] = q + v * 5u;
        const float x = -25.0f + 50.0f * (float)(v % 12u) / 11.0f;
        const float y = -20.0f + 40.0f * (float)(v / 12u) / 9.0f;
        qw(o[0], x, y, 2.0f + 0.15f * (float)(v % 7u), 1);
        qw(o[1], 0.3f, 0.5f, 0.8f, 1);
        const uint32_t a0 = 16u + 4u * (v % ST_BONES), a1 = 16u + 4u * ((7u * v + 13u) % ST_BONES);
        const float w0 = 0.2f + 0.15f * (float)(v % 5u);
        qw(o[2], fbits(a0), w0, fbits(a1), 1.0f - w0);
        qw(o[3], 0.1f, 0.2f, 1, 1);
        qw(o[4], 100, 100, 100, 128);
    }
}

static uint64_t put(const void *data, uint64_t size, uint64_t align)
{
    const uint64_t off = rd__ring_alloc(size, align);
    if (off != ~0ull && data) {
        memcpy(g_rd.ringMap[rd__ring_slot()] + off, data, size);
    }
    return off;
}

/* the ring offsets of the 64 blocks: one 4 KB page each, at phase 64 k
 * rounded to the uniform alignment; *phases the distinct phases */
static bool blocks(const void *b, uint64_t ua, uint64_t off[ST_OFFSETS], uint32_t *phases)
{
    const uint64_t base = rd__ring_alloc((uint64_t)ST_OFFSETS * 8192u, 4096u);
    if (base == ~0ull) {
        return false;
    }
    uint64_t seen[ST_OFFSETS];
    *phases = 0;
    for (uint32_t k = 0; k < ST_OFFSETS; k++) {
        const uint64_t phase = (64u * k + ua - 1u) / ua * ua % 4096u;
        off[k] = base + (uint64_t)k * 8192u + phase;
        memcpy(g_rd.ringMap[rd__ring_slot()] + off[k], b, sizeof(IcoVuBoneCB));
        bool dup = false;
        for (uint32_t i = 0; i < *phases && !dup; i++) {
            dup = seen[i] == phase;
        }
        if (!dup) {
            seen[(*phases)++] = phase;
        }
    }
    return true;
}

/* the device's failure in the probe (rd.h rd_vu_skin_probe_failed) */
static const char *s_failed;

bool rd_vu_skin_probe_failed(char *why, size_t size)
{
    if (why && size) {
        snprintf(why, size, "%s", s_failed ? s_failed : "");
    }
    return s_failed != NULL;
}

bool rd__vu_skin_selftest(void)
{
    if (!g_rd.inited || !g_rd.hasDevice) {
        return true;
    }
    if (!rd__vu_skin_check_on()) {
        if (rhi_backend() == RHI_BACKEND_VULKAN && g_rd.windowed) {
            rd__log("VU skin check: off (%s)",
                    rhi_device_lost() ? "the device was lost" : rd__vu_skin_check_why());
        }
        return true;
    }
    const double t0 = rd__now_ms();
    const char *set = g_rd.vuFallbackPlain ? "plain set" : "extended set";
    /* the crash report's and the activity file's note (rd.h
     * rd_vu_skin_probe_failed): a crash, a freeze or the system's end
     * inside the probe names it at the next start, which then runs
     * without the check ([dev] vu_skin_check = false, window_host.c) */
    rd__pipeline_note_begin("rd vu skin probe (the VU skin check's start-up probe: its three "
                            "pipelines)");
    static float mem[36][4], bb[240][4], st[ST_VERTS * 5u][4];
    block(mem);
    bones(bb);
    stream(st);
    /* the CPU's positions, one batch of 120 */
    static RdSkinPos ref[ST_CODES][ST_VERTS];
    const RdSkinBatch batch = {0, ST_VERTS};
    RdSkinCase kc[ST_CODES];
    bool made = true;
    for (uint32_t c = 0; c < ST_CODES; c++) {
        kc[c] = (RdSkinCase){kCode[c],
                             (const float (*)[4])mem,
                             (const float (*)[4])bb,
                             (const float (*)[4])st,
                             5u,
                             &batch,
                             1u,
                             ST_VERTS};
        made = made && rd__skin_reference(&kc[c], 0, ref[c]);
        made = made && rd__vu_skin_probe_pipeline(kVs[c], false).id != 0;
    }
    const RhiBuffer idx = rd__vu_skin_index_buffer();
    /* the box check's occlusion queries made now, where the start already
     * waits, for its 80 indices: the game's frames neither stall at the
     * first box check nor reset and read the whole pool (rhi.h
     * rhi_occlusion_prepare) */
    if (rhi_occlusion_supported()) {
        (void)rhi_occlusion_prepare(rd__vu_skin_queries()); /* the twins' too where they run */
    }
    const uint32_t w = ICO_VU_PROBE_FIELDS * ST_OFFSETS, h = ST_VERTS * ST_CODES;
    RhiTexture tex = {0};
    RhiState texState = RHI_STATE_UNDEFINED;
    uint8_t *px = malloc((size_t)w * h * 4u);
    const char *why = NULL;
    rd__activity_note("the VU skin check's start-up probe (rd vu skin probe): drawing 192 probes "
                      "and reading them back");
    uint32_t phases = 0;
    uint64_t boff[ST_OFFSETS];
    if (!made || !idx.id || !px) {
        why = "its pipelines or buffers could not be made";
    } else if (!rd__begin_own_frame(2u << 20)) {
        why = "no ring";
    } else {
        tex = rhi_create_texture(&(RhiTextureDesc){w, h, 1, RHI_FMT_RGBA8_UINT,
                                                   RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC,
                                                   "rd vu skin probe self-test", 0});
        const uint64_t ua = rhi_limits()->uniformAlign ? rhi_limits()->uniformAlign : 256u;
        const uint64_t sOff = put(st, sizeof(st), 256u);
        IcoDrawCB dcb;
        memset(&dcb, 0, sizeof(dcb));
        const uint64_t dOff = put(&dcb, sizeof(dcb), ua);
        uint64_t vOff[ST_CODES];
        bool ok = tex.id && sOff != ~0ull && dOff != ~0ull && blocks(bb, ua, boff, &phases);
        for (uint32_t c = 0; c < ST_CODES && ok; c++) {
            IcoVuCB v;
            memset(&v, 0, sizeof(v));
            memcpy(v.mem, mem, sizeof(v.mem));
            v.draw[0] = 0; /* the stream from t0's first qword */
            v.draw[1] = 5;
            v.draw[2] = ICO_VU_CLIP_REGION | ICO_VU_PROBE | ICO_VU_NO_BESIDE;
            v.strip[1] = 1;
            vOff[c] = put(&v, sizeof(v), ua);
            ok = vOff[c] != ~0ull;
        }
        /* group 1 as vuGroupMake binds it: the stream at t0, the ring for
         * the three dynamic blocks, t4 and t5 the ring (read by no cluster
         * entry) */
        RhiBinding b[6];
        memset(b, 0, sizeof(b));
        const RhiBuffer ring = g_rd.ring[rd__ring_slot()];
        b[0] = (RhiBinding){.slot = 0,
                            .type = RHI_BIND_STORAGE_BUFFER,
                            .buffer = ring,
                            .offset = sOff,
                            .size = sizeof(st)};
        static const uint32_t sizes[3] = {sizeof(IcoDrawCB), sizeof(IcoVuCB), sizeof(IcoVuBoneCB)};
        for (int i = 0; i < 3; i++) {
            b[1 + i] = (RhiBinding){.slot = (uint32_t)(1 + i),
                                    .type = RHI_BIND_UNIFORM_BUFFER_DYNAMIC,
                                    .buffer = ring,
                                    .offset = 0,
                                    .size = sizes[i]};
        }
        b[4] = (RhiBinding){
            .slot = 4, .type = RHI_BIND_STORAGE_BUFFER, .buffer = ring, .offset = 0, .size = 4096};
        b[5] = (RhiBinding){
            .slot = 5, .type = RHI_BIND_STORAGE_BUFFER, .buffer = ring, .offset = 0, .size = 4096};
        const RhiBindGroup g1 = ok ? rd__replay_bind_group(g_rd.layoutVu, b, 6) : (RhiBindGroup){0};
        RhiBinding fb = {.slot = 0,
                         .type = RHI_BIND_UNIFORM_BUFFER_DYNAMIC,
                         .buffer = ring,
                         .offset = 0,
                         .size = sizeof(IcoFrameCB)};
        const RhiBindGroup g0 =
            ok ? rd__replay_bind_group(g_rd.layoutFrame, &fb, 1) : (RhiBindGroup){0};
        const RhiBindGroup g2 =
            ok ? rd__tex_group(g_rd.dummy, rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST,
                                                       RD_WRAP_CLAMP, RD_WRAP_CLAMP))
               : (RhiBindGroup){0};
        RhiCommandList cl =
            ok && g0.id && g1.id && g2.id ? rhi_begin_commands() : (RhiCommandList){0};
        if (!cl.id) {
            why = "its frame could not be recorded";
        } else {
            rd__transition(cl, tex, &texState, RHI_STATE_RENDER_TARGET);
            RhiRenderPassDesc rp;
            memset(&rp, 0, sizeof(rp));
            rp.color[0] =
                (RhiColorAttachment){tex, RHI_LOAD_CLEAR, {0, 0, 0, 0}, RHI_STORE_STORE, false};
            rp.colorCount = 1;
            rp.width = w;
            rp.height = h;
            rhi_cmd_begin_render_pass(cl, &rp);
            rhi_cmd_set_index_buffer(cl, idx, 0, true);
            for (uint32_t c = 0; c < ST_CODES; c++) {
                rd__set_pipeline(cl, rd__vu_skin_probe_pipeline(kVs[c], false), NULL);
                for (uint32_t k = 0; k < ST_OFFSETS; k++) {
                    /* tile (k, c): FrameCB's exact-viewport form of
                     * vu_probe_out (rd_vu_skin_check.c rd__vu_skin_end) */
                    IcoFrameCB fc;
                    memset(&fc, 0, sizeof(fc));
                    fc.target[0] = (float)w;
                    fc.target[1] = (float)h;
                    fc.target[2] = 1.0f / (float)w;
                    fc.target[3] = 1.0f / (float)h;
                    fc.frame[0] = 1.0f;
                    fc.vp[0] = 2.0f / (float)w;
                    fc.vp[1] = 2.0f / (float)h;
                    fc.misc[2] = 1.0f;
                    fc.misc[3] = 1.0f;
                    fc.vpo[0] = (float)(ICO_VU_PROBE_FIELDS * k);
                    fc.vpo[1] = (float)(ST_VERTS * c);
                    const uint64_t fOff = put(&fc, sizeof(fc), ua);
                    if (fOff == ~0ull) {
                        continue;
                    }
                    const uint32_t f0 = (uint32_t)fOff;
                    const uint32_t offs[3] = {(uint32_t)dOff, (uint32_t)vOff[c], (uint32_t)boff[k]};
                    rhi_cmd_set_bind_group_offsets(cl, 0, g0, &f0, 1);
                    rhi_cmd_set_bind_group_offsets(cl, 1, g1, offs, 3);
                    rhi_cmd_set_bind_group(cl, 2, g2);
                    rhi_cmd_draw_indexed(cl, ST_VERTS * 9u, 0, 0, 1);
                }
            }
            rhi_cmd_end_render_pass(cl);
            rd__transition(cl, tex, &texState, RHI_STATE_COPY_SRC);
            rhi_end_commands(cl);
            rhi_submit(cl);
            if (!rd__read_rhi_texture(tex, &texState, w, h, px, (size_t)w * h * 4u) ||
                rhi_device_lost()) {
                why = "the device failed in it";
                s_failed = rhi_device_lost() ? "the device was lost in it"
                                             : "its picture could not be read back";
            }
        }
    }
    /* the 192 probes against the CPU */
    uint32_t bad = 0, badK = 0, badC = 0;
    RdSkinVerdict bv;
    memset(&bv, 0, sizeof(bv));
    if (!why) {
        static int32_t got[ST_VERTS][3];
        for (uint32_t c = 0; c < ST_CODES && !bad; c++) {
            for (uint32_t k = 0; k < ST_OFFSETS && !bad; k++) {
                for (uint32_t v = 0; v < ST_VERTS; v++) {
                    const uint8_t *row = px + ((size_t)(ST_VERTS * c + v) * w) * 4u;
                    for (uint32_t f = 0; f < 3u; f++) {
                        memcpy(&got[v][f], row + ((size_t)ICO_VU_PROBE_FIELDS * k + f) * 4u, 4);
                    }
                }
                RdSkinVerdict v;
                rd__skin_judge(&kc[c], ref[c], (const int32_t (*)[3])got, &v);
                if (v.wrong || v.compared != ST_VERTS) {
                    bad = v.wrong ? v.wrong : ST_VERTS - v.compared;
                    badK = k;
                    badC = c;
                    bv = v;
                }
            }
        }
    }
    if (tex.id) {
        rhi_destroy_texture(tex);
    }
    free(px);
    rd__pipeline_note_end();
    const double ms = rd__now_ms() - t0;
    if (why) {
        rd__log("VU skin probe: not run (%s) in %.1f ms", why, ms);
    } else if (!bad) {
        rd__log("VU skin probe: ok (%u vertices x %u offsets in %u page phases, codes 20 22 24, "
                "%s) in %.1f ms",
                ST_VERTS, ST_OFFSETS, phases, set, ms);
    } else {
        const uint64_t phase = boff[badK] & 4095u;
        if (bv.addr) {
            rd__log("VU skin probe: wrong at offset %llu (page phase %llu), code %d, %s: %u "
                    "vertices, address %u read as zero%s (got (%.1f, %.1f, %d) want (%.1f, %.1f, "
                    "%d)) in %.1f ms",
                    (unsigned long long)boff[badK], (unsigned long long)phase, kCode[badC], set,
                    bad, bv.addr, bv.explains ? "" : " (not the first wrong vertex's)",
                    bv.got[0] / 16.0, bv.got[1] / 16.0, bv.got[2], bv.want.x / 16.0,
                    bv.want.y / 16.0, bv.want.z, ms);
        } else if (bv.nanAddr) {
            rd__log("VU skin probe: wrong at offset %llu (page phase %llu), code %d, %s: %u "
                    "vertices, address %u read as non-finite (%u dropped) (got (%.1f, %.1f, %d) "
                    "want (%.1f, %.1f, %d)) in %.1f ms",
                    (unsigned long long)boff[badK], (unsigned long long)phase, kCode[badC], set,
                    bad, bv.nanAddr, bv.dropped, bv.got[0] / 16.0, bv.got[1] / 16.0, bv.got[2],
                    bv.want.x / 16.0, bv.want.y / 16.0, bv.want.z, ms);
        } else {
            rd__log("VU skin probe: wrong at offset %llu (page phase %llu), code %d, %s: %u "
                    "vertices, no zero matrix explains them (got (%.1f, %.1f, %d) want (%.1f, "
                    "%.1f, %d)) in %.1f ms",
                    (unsigned long long)boff[badK], (unsigned long long)phase, kCode[badC], set,
                    bad, bv.got[0] / 16.0, bv.got[1] / 16.0, bv.got[2], bv.want.x / 16.0,
                    bv.want.y / 16.0, bv.want.z, ms);
        }
    }
    if (s_failed) {
        /* the device cannot run it: off for the run, and the window
         * writes the setting (rd_vu_skin_probe_failed) */
        rd__vu_skin_check_set(0);
        rd__log("VU skin check: off (the start-up probe failed: %s)", s_failed);
        return true;
    }
    const RhiLimits *lim = rhi_limits();
    if (rd__vu_skin_twins_on()) {
        rd__log("VU skin check: on (every skinned draw twinned in its own pass through its own "
                "pipeline (A the draw's bone offset, B the same bytes at a fresh offset; counted "
                "by occlusion query%s); one probe %s, targeted after a twin event, "
                "drawn again after the picture and compared with the CPU, its bone block's "
                "bytes with the GPU's copy; variants: a fresh ring offset, uniform robustness "
                "off %s; the box check %s; the first failure captured (a sidecar and a frame "
                "dump); %s)",
                lim->occlusionPrecise ? ", precise" : ", zero or not only",
                rd__driver_is_turnip(lim) ? "a present" : "every 100 ms",
                lim->uniformRobustOff ? "(VK_EXT_pipeline_robustness)"
                : lim->robustText && strncmp(lim->robustText, "on", 2) == 0
                    ? "not offered (no VK_EXT_pipeline_robustness)"
                    : "not needed (robust access off)",
                rhi_occlusion_supported() ? "by occlusion query"
                                          : "not offered (no occlusion queries)",
                rd__vu_skin_check_why());
        return why != NULL || bad == 0;
    }
    rd__log("VU skin check: on (one skinned draw every %d ms, in turn, drawn again after the "
            "picture as a probe of its own and compared with the CPU, its bone block's bytes "
            "with the GPU's copy; variants: a fresh ring offset, uniform robustness off %s; the "
            "box check %s; %s)",
            100,
            lim->uniformRobustOff ? "(VK_EXT_pipeline_robustness)"
            : lim->robustText && strncmp(lim->robustText, "on", 2) == 0
                ? "not offered (no VK_EXT_pipeline_robustness)"
                : "not needed (robust access off)",
            rhi_occlusion_supported() ? "by occlusion query" : "not offered (no occlusion queries)",
            rd__vu_skin_check_why());
    return why != NULL || bad == 0;
}
