/* rd_paste_spec.c: the glow's paste drawn by its own entry, fx_paste_ps
 * (rd_paste_spec.h says what and why; docs/port/RENDER_POST.md, "The
 * glow's paste by its own entry").
 *
 *   rd__paste_spec_why   the gate, pure: the DrawCB words fx_paste_ps holds
 *                        as constants and the key against the start-up
 *                        set's
 *   rd__paste_spec_pipe  rd_fx_plan.c fxDraw's premultiplied add: the
 *                        pipeline to draw with, counted per replay, run and
 *                        10 s block, and in the attempt ledger
 *   rd__paste_spec_selftest  the two modules compared byte for byte on the
 *                        device (the file's end)
 *
 * The module is made here (rd__paste_spec_init, from rd__lamps_init after
 * rd__gpu_init) into g_rd.fs[RD_FS_FX_PASTE], an entry past the name
 * table's count (rd_internal.h RD_FS_SLOTS): rd_replay.c is at its size
 * cap, so neither its name table nor its module loop knows it. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_paste_spec.h"
#include "rd_replay.h"
#include "shader_consts.h"
#include "shaders_gen.h"

static const char *const kWhy[RD_PASTE_SPEC_WHY_COUNT] = {
    [RD_PASTE_SPEC_WHY_NONE] = "drawn by fx_paste_ps",
    [RD_PASTE_SPEC_WHY_OFF] = "off (ICO_RD_PASTE_SPEC=0 or the build's RD_PASTE_SPEC_DEFAULT 0)",
    [RD_PASTE_SPEC_WHY_MODULE] = "no fx_paste_ps module (made under Vulkan alone)",
    [RD_PASTE_SPEC_WHY_TEST] = "its self-test has not passed",
    [RD_PASTE_SPEC_WHY_KEY] = "not the start-up set's key (a depth, another format or blend, "
                              "a multisampled pass)",
    [RD_PASTE_SPEC_WHY_FLAGS] = "a flag fx_paste_ps holds otherwise (above 1x, the alpha alone, "
                                "the glow's mask, DATE, the destination read)",
    [RD_PASTE_SPEC_WHY_TEXFMT] = "the source not RGBA32",
    [RD_PASTE_SPEC_WHY_ATEST] = "an alpha test",
    [RD_PASTE_SPEC_WHY_SOURCE] = "the source scaled or cropped",
    [RD_PASTE_SPEC_WHY_PIPE] = "its pipeline not made",
};

const char *rd__paste_spec_why_text(int why)
{
    return why >= 0 && why < RD_PASTE_SPEC_WHY_COUNT ? kWhy[why] : "?";
}

static struct {
    int env;  /* rd__paste_spec_env, read at init */
    int test; /* 0 not run, 1 passed, 2 failed or not run with a reason */
    char words[240];
    uint32_t replay; /* g_rd.replayCounter of rTaken, rSeen */
    uint32_t rTaken, rSeen;
    uint64_t taken, seen; /* since rd_init */
    int lastWhy;
    bool said[RD_PASTE_SPEC_WHY_COUNT], firstSaid;
    char stateSaid[384];
} s_ps = {.env = -1};

/* the 10 s block's counts (__atomic: the render thread adds, the window's
 * line takes) */
static struct {
    uint64_t taken, seen, why[RD_PASTE_SPEC_WHY_COUNT];
} s_blk;

int rd__paste_spec_env(const char *env)
{
    const char *e = env ? env : getenv("ICO_RD_PASTE_SPEC"); /* the tests only: no config key */
    if (!e || !e[0]) {
        return -1;
    }
    if (strcmp(e, "sabotage") == 0) {
        return 2;
    }
    return e[0] == '0' ? 0 : 1;
}

/* asked for: the switch, else the build's default */
static bool wanted(void)
{
    return s_ps.env >= 1 || (s_ps.env < 0 && RD_PASTE_SPEC_DEFAULT);
}

RdPipeKeyInt rd__paste_spec_general_key(void)
{
    /* rd__fx_enumerate's premultiplied add at Z ALWAYS without the Z write
     * (rd_fx_plan.c: the same state block, built the same way) */
    RdStateBlock s;
    rd__reset_state_block(&s);
    s.ds.colorMask = 0xF;
    s.ds.test = rd_test_from_gs(RD_TEST_Z_ALWAYS);
    s.ds.test.ztst = RD_ZTST_ALWAYS;
    s.ds.zwrite = RD_ZWRITE_OFF;
    RdPipeKeyInt k = rd__blur_key(&s, RHI_FMT_RGBA8_UNORM, RHI_FMT_D32F_S8, NULL, 0);
    k.fs = RD_FS_FX_FRAME;
    k.gs.blend = RD_BLEND_CS_AS_ADD_CD;
    return k;
}

RdPipeKeyInt rd__paste_spec_key(void)
{
    RdPipeKeyInt k = rd__paste_spec_general_key();
    k.fs = RD_FS_FX_PASTE;
    return k;
}

int rd__paste_spec_why(const IcoDrawCB *cb, const RdPipeKeyInt *key, const RdPipeKeyInt *start)
{
    RdPipeKeyInt k = *key;
    if (k.fs != RD_FS_FX_FRAME || memcmp(&k, start, sizeof(k)) != 0) {
        return RD_PASTE_SPEC_WHY_KEY;
    }
    const uint32_t fl = cb->mode[0];
    if ((fl & ~(uint32_t)RD_PASTE_SPEC_FREE) != RD_PASTE_SPEC_FIXED) {
        return RD_PASTE_SPEC_WHY_FLAGS;
    }
    if ((cb->mode[1] >> 8) != 0u) {
        return RD_PASTE_SPEC_WHY_TEXFMT; /* gs_math.hlsli TEXFMT_RGBA32 is 0 */
    }
    if (((cb->mode[2] >> 8) & 1u) != 0u) {
        return RD_PASTE_SPEC_WHY_ATEST;
    }
    /* fx_src_scale: g_scale.xy, 1 where not above 0; fx_src_crop16: 0 where
     * g_scale.z is 0 */
    const float sx = cb->scale[0] > 0.0f ? cb->scale[0] : 1.0f;
    const float sy = cb->scale[1] > 0.0f ? cb->scale[1] : 1.0f;
    if (sx != 1.0f || sy != 1.0f || cb->scale[2] != 0.0f) {
        return RD_PASTE_SPEC_WHY_SOURCE;
    }
    return RD_PASTE_SPEC_WHY_NONE;
}

uint32_t rd__paste_spec_keys(RdPipeKeyInt *out, uint32_t max, uint32_t n)
{
    if (!g_rd.fs[RD_FS_FX_PASTE].id || !g_rd.fs[RD_FS_FX_FRAME].id || !wanted()) {
        return n;
    }
    const RdPipeKeyInt k = rd__paste_spec_key();
    return rd__add_pipe_key(out, max, n, &k);
}

bool rd__paste_spec_on(void)
{
    return g_rd.fs[RD_FS_FX_PASTE].id && wanted() && s_ps.test == 1;
}

static void blkAdd(uint64_t *v, uint64_t n)
{
    __atomic_fetch_add(v, n, __ATOMIC_RELAXED);
}

RhiPipeline rd__paste_spec_pipe(const IcoDrawCB *cb, const RdPipeKeyInt *key, RhiPipeline general,
                                RdPipeKeyInt *spec, const RdPipeKeyInt **use)
{
    *use = key;
    if (s_ps.replay != g_rd.replayCounter) {
        s_ps.replay = g_rd.replayCounter;
        s_ps.rTaken = s_ps.rSeen = 0;
    }
    s_ps.rSeen++;
    s_ps.seen++;
    blkAdd(&s_blk.seen, 1);
    int why = RD_PASTE_SPEC_WHY_NONE;
    if (!wanted()) {
        why = RD_PASTE_SPEC_WHY_OFF;
    } else if (!g_rd.fs[RD_FS_FX_PASTE].id) {
        why = RD_PASTE_SPEC_WHY_MODULE;
    } else if (s_ps.test != 1) {
        why = RD_PASTE_SPEC_WHY_TEST;
    } else if (g_rd.passSamples > 1) {
        why = RD_PASTE_SPEC_WHY_KEY; /* rd__get_pipeline would turn it multisampled */
    } else {
        const RdPipeKeyInt start = rd__paste_spec_general_key();
        why = rd__paste_spec_why(cb, key, &start);
    }
    if (why == RD_PASTE_SPEC_WHY_NONE) {
        *spec = *key;
        spec->fs = RD_FS_FX_PASTE;
        const RhiPipeline p = rd__get_pipeline(spec);
        if (p.id) {
            *use = spec;
            s_ps.rTaken++;
            s_ps.taken++;
            blkAdd(&s_blk.taken, 1);
            ico_attempt_fire(ICO_ATT_PASTE_SPEC, 1);
            if (!s_ps.firstSaid) {
                s_ps.firstSaid = true;
                rd__log("paste: the glow's paste drawn by fx_paste_ps, frame %u (a premultiplied "
                        "add into an RGBA8 target; reported once)",
                        g_rd.replayCounter);
            }
            return p;
        }
        why = RD_PASTE_SPEC_WHY_PIPE;
    }
    s_ps.lastWhy = why;
    blkAdd(&s_blk.why[why], 1);
    ico_attempt_fire2(ICO_ATT_PASTE_SPEC, 1);
    if (why != RD_PASTE_SPEC_WHY_OFF && !s_ps.said[why]) {
        s_ps.said[why] = true;
        rd__log("paste: a premultiplied add kept fx_frame_ps: %s (frame %u, flags 0x%X, TEXA word "
                "0x%X; reported once for this reason)",
                kWhy[why], g_rd.replayCounter, cb->mode[0], cb->mode[1]);
    }
    return general;
}

/* the state's line, when it changes */
static void stateNote(void)
{
    char line[sizeof(s_ps.stateSaid)];
    const char *how = s_ps.env == 0   ? "ICO_RD_PASTE_SPEC=0"
                      : s_ps.env == 1 ? "ICO_RD_PASTE_SPEC=1"
                      : s_ps.env == 2 ? "ICO_RD_PASTE_SPEC=sabotage"
                                      : (RD_PASTE_SPEC_DEFAULT ? "the build's default"
                                                               : "RD_PASTE_SPEC_DEFAULT 0");
    if (!wanted()) {
        snprintf(line, sizeof(line), "off (%s)", how);
    } else if (!g_rd.fs[RD_FS_FX_PASTE].id) {
        snprintf(line, sizeof(line), "off (%s): %s", how,
                 !g_rd.hasDevice ? "no device"
                 : rhi_backend() != RHI_BACKEND_VULKAN
                     ? "fx_paste_ps is made under Vulkan alone; fx_frame_ps draws the paste"
                     : "no fx_paste_ps module in the shader table");
    } else if (s_ps.test == 1) {
        snprintf(line, sizeof(line), "on (%s; self-test %s)", how, s_ps.words);
    } else {
        snprintf(line, sizeof(line), "off until its self-test passes (%s; self-test %s)", how,
                 s_ps.words[0] ? s_ps.words : "not run yet");
    }
    if (strcmp(line, s_ps.stateSaid) != 0) {
        snprintf(s_ps.stateSaid, sizeof(s_ps.stateSaid), "%s", line);
        rd__log("paste: the glow's paste by its own entry (fx_paste_ps, fx_frame_ps's paste "
                "path alone): %s",
                line);
    }
    (void)ico_attempt_decide_paste_spec(
        g_rd.hasDevice && g_rd.fs[RD_FS_FX_PASTE].id != 0, s_ps.env < 0 ? -1 : (s_ps.env ? 1 : 0),
        RD_PASTE_SPEC_DEFAULT, s_ps.test == 1 ? 1 : (s_ps.test == 2 ? 0 : -1));
}

void rd__paste_spec_init(void)
{
    if (g_rd.fs[RD_FS_FX_PASTE].id && g_rd.hasDevice) {
        rhi_destroy_shader(g_rd.fs[RD_FS_FX_PASTE]); /* a renderer made again without a shutdown */
    }
    g_rd.fs[RD_FS_FX_PASTE] = (RhiShader){0};
    memset(&s_ps, 0, sizeof(s_ps));
    s_ps.env = rd__paste_spec_env(NULL);
    /* Vulkan alone (fx_blur4_ps's rule): the other backends keep fx_frame_ps */
    if (g_rd.hasDevice && rhi_backend() == RHI_BACKEND_VULKAN && s_ps.env != 0 &&
        g_rd.fs[RD_FS_FX_FRAME].id && ico_find_shader(RD_PASTE_SPEC_NAME)) {
        g_rd.fs[RD_FS_FX_PASTE] = rd__make_shader(RD_PASTE_SPEC_NAME);
    }
    stateNote();
}

void rd__paste_spec_shutdown(void)
{
    if (g_rd.fs[RD_FS_FX_PASTE].id && g_rd.hasDevice) {
        rhi_destroy_shader(g_rd.fs[RD_FS_FX_PASTE]);
    }
    g_rd.fs[RD_FS_FX_PASTE] = (RhiShader){0};
}

int rd__paste_spec_test_state(const char **words)
{
    if (words) {
        *words = s_ps.words[0] ? s_ps.words : "not run";
    }
    return s_ps.test;
}

void rd__paste_spec_counts(uint64_t *taken, uint64_t *seen, int *lastWhy)
{
    *taken = s_ps.taken;
    *seen = s_ps.seen;
    *lastWhy = s_ps.lastWhy;
}

void rd__paste_spec_replay(uint32_t *taken, uint32_t *seen)
{
    const bool now = s_ps.replay == g_rd.replayCounter;
    *taken = now ? s_ps.rTaken : 0u;
    *seen = now ? s_ps.rSeen : 0u;
}

void rd__paste_spec_line(void)
{
    const uint64_t taken = __atomic_exchange_n(&s_blk.taken, 0, __ATOMIC_RELAXED);
    const uint64_t seen = __atomic_exchange_n(&s_blk.seen, 0, __ATOMIC_RELAXED);
    uint64_t w[RD_PASTE_SPEC_WHY_COUNT];
    for (int i = 0; i < RD_PASTE_SPEC_WHY_COUNT; i++) {
        w[i] = __atomic_exchange_n(&s_blk.why[i], 0, __ATOMIC_RELAXED);
    }
    if (seen == 0) {
        return;
    }
    char why[768] = "";
    size_t at = 0;
    for (int i = 1; i < RD_PASTE_SPEC_WHY_COUNT; i++) {
        if (!w[i]) {
            continue;
        }
        const int m = snprintf(why + at, sizeof(why) - at, "%s%llu %s", at ? ", " : " (",
                               (unsigned long long)w[i], kWhy[i]);
        if (m < 0 || (size_t)m >= sizeof(why) - at) {
            break;
        }
        at += (size_t)m;
    }
    fprintf(stderr,
            "window: paste: %llu pastes by fx_paste_ps of %llu, %llu kept on fx_frame_ps%s%s\n",
            (unsigned long long)taken, (unsigned long long)seen, (unsigned long long)(seen - taken),
            why, at ? ")" : "");
}

/* ------------------------------------------------------------ the self-test
 * Pastes drawn both ways (fx_frame_ps and fx_paste_ps, each through its own
 * pipeline of the start-up set's key) from a random RGBA8 source of the
 * glow's shape (half the frame's GS width by 128 rows; 48 x 16 on the small
 * frames: the paste's 2 x 3.5 magnification) over a random destination,
 * read back and compared byte for byte.  Each case draws five bands of
 * rows, each a DrawCB form the gate takes (the paste itself, the As form,
 * nearest with DECAL and FBA, HIGHLIGHT, HIGHLIGHT2 with REPEAT; a TEXA
 * mode word that RGBA32 ignores), so every run-time word fx_paste_ps reads
 * changes between the bands.  The cases:
 *   own        SCENE's own texture size, GS size, scale, origin, Full pixel
 *              crop and jitter: FrameCB as frameRefresh makes it for the
 *              paste (rd__replay_vu_frame under the exact viewport) and
 *              the exact viewport (rd_replay.c setViewport's rule)
 *   1x, 2x, fractional (2.8125 x 2.41071, the phone's), half-line (2x with
 *              the flip's half-line offset): a 96 x 56 GS frame, FrameCB
 *              by rd__frame_group_ex, the plain viewport */
#define PS_BANDS 5u

static uint8_t psLevel(uint32_t i, uint32_t seed)
{
    uint32_t x = i * 2654435761u + seed * 0x9E3779B9u + 0x7F4A7C15u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    return (uint8_t)(x >> 24);
}

static uint32_t pow2At(uint32_t v)
{
    uint32_t p = 1;
    while (p < v && p < 0x80000000u) {
        p <<= 1;
    }
    return p;
}

/* the DrawCB of band b on a GS frame gsW x gsH from a sw x sh source; spec:
 * fx_paste_ps's draw (sabotage: its FIX moved) */
static void psCb(IcoDrawCB *cb, uint32_t b, uint32_t gsW, uint32_t gsH, uint32_t sw, uint32_t sh,
                 bool sabotage)
{
    static const struct {
        uint32_t flags;
        uint8_t rgba[4];
        uint8_t blend, fix, texa;
    } kBand[PS_BANDS] = {
        {RD_PASTE_SPEC_FIXED | RD_FXF_TCC | RD_FXF_LINEAR | RD_FXF_CLAMP_S | RD_FXF_CLAMP_T,
         {128, 128, 128, 128},
         RD_BLEND_CS_FIX_ADD_CD,
         128,
         0},
        {RD_PASTE_SPEC_FIXED | RD_FXF_TCC | RD_FXF_LINEAR,
         {150, 90, 200, 77},
         RD_BLEND_CS_AS_ADD_CD,
         0,
         0x2A},
        {RD_PASTE_SPEC_FIXED | (1u << RD_FXF_TFX_SHIFT) | RD_FXF_FBA | RD_FXF_CLAMP_S,
         {64, 64, 64, 200},
         RD_BLEND_CS_FIX_ADD_CD,
         37,
         0x81},
        {RD_PASTE_SPEC_FIXED | (2u << RD_FXF_TFX_SHIFT) | RD_FXF_TCC | RD_FXF_LINEAR |
             RD_FXF_CLAMP_T,
         {100, 180, 40, 90},
         RD_BLEND_CS_FIX_ADD_CD,
         200,
         0},
        {RD_PASTE_SPEC_FIXED | (3u << RD_FXF_TFX_SHIFT) | RD_FXF_TCC,
         {255, 255, 255, 255},
         RD_BLEND_CS_AS_ADD_CD,
         0,
         0x13},
    };

    memset(cb, 0, sizeof(*cb));
    const float ox = (float)(2048u - (gsW >> 1)), oy = (float)(2048u - (gsH >> 1));
    for (int k = 0; k < 4; k++) {
        cb->col[k] = kBand[b].rgba[k];
    }
    cb->mode[0] = kBand[b].flags;
    cb->mode[1] = kBand[b].texa;   /* TEXFMT RGBA32 (0): the TEXA mode word ignored */
    cb->mode[2] = 3u | (1u << 16); /* an ATST and AFAIL word with ATE off: ignored */
    cb->mode[3] = 50u;
    cb->blend[0] = rd__alpha_register(kBand[b].blend);
    cb->blend[1] = (uint32_t)kBand[b].fix + (sabotage && b == 0 ? 1u : 0u);
    cb->blend[2] = 1u; /* COLCLAMP */
    cb->blend[3] = 0u;
    /* pasteFullScreenFlare: the frame's rect, the source's UVs from half a texel */
    cb->uvRect[0] = 8.0f;
    cb->uvRect[1] = 8.0f;
    cb->uvRect[2] = 16.0f * (float)sw;
    cb->uvRect[3] = 16.0f * (float)sh;
    cb->tex[0] = (float)pow2At(sw);
    cb->tex[1] = (float)pow2At(sh);
    cb->tex[2] = (float)sw;
    cb->tex[3] = (float)sh;
    cb->scale[0] = 1.0f;
    cb->scale[1] = 1.0f;
    cb->param[0] = 16.0f * ox;
    cb->param[1] = 16.0f * oy;
    cb->param[2] = 16.0f * (ox + (float)gsW);
    cb->param[3] = 16.0f * (oy + (float)gsH);
    rd__replay_blur_aux1(cb);
}

/* rd_replay.c vpExtent's rule (the exact viewport's size for n texels) */
static uint32_t psExtent(uint32_t n)
{
    const uint32_t lim = rhi_limits()->maxRenderTargetSize;
    uint32_t p = 16384u;
    while (p > lim && p > 1u) {
        p >>= 1;
    }
    return p >= n ? p : n;
}

typedef struct PsCase {
    const char *name;
    uint32_t w, h;     /* the target's texels */
    uint32_t gsW, gsH; /* its GS frame */
    float sx, sy, oyAdd;
    bool own; /* SCENE's own FrameCB under the exact viewport */
} PsCase;

/* one case: false with *why when it could not run; *bad the first texel
 * apart (-1 none) */
static bool psRun(const PsCase *c, uint32_t sceneId, bool sabotage, const char **why, int *bad,
                  uint8_t got[4], uint8_t want[4], uint64_t *texels)
{
    const RhiFormat fmt = RHI_FMT_RGBA8_UNORM;
    const uint32_t usage = RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC |
                           RHI_TEX_COPY_DST | rd__dst_target_usage(fmt, NULL);
    const uint32_t a = rhi_limits()->copyRowPitchAlign ? rhi_limits()->copyRowPitchAlign : 1u;
    const uint32_t sw = c->gsW / 2u > 0u ? c->gsW / 2u : 1u;
    const uint32_t sh = c->own ? 128u : 16u;
    const uint32_t dPitch = (c->w * 4u + a - 1u) / a * a, sPitch = (sw * 4u + a - 1u) / a * a;
    const uint64_t dBytes = (uint64_t)dPitch * c->h, sBytes = (uint64_t)sPitch * sh;
    RhiTexture src = rhi_create_texture(
        &(RhiTextureDesc){sw, sh, 1, fmt, usage, "rd paste spec test source", 0});
    RhiTexture dst[2];
    for (int k = 0; k < 2; k++) {
        dst[k] = rhi_create_texture(
            &(RhiTextureDesc){c->w, c->h, 1, fmt, usage, "rd paste spec test", 0});
    }
    RhiBuffer up = rhi_create_buffer(
        &(RhiBufferDesc){dBytes + sBytes, RHI_BUF_COPY_SRC, RHI_MEM_UPLOAD, "rd paste spec test"});
    uint8_t *m = up.id ? (uint8_t *)rhi_map_buffer(up) : NULL;
    const size_t backSize = (size_t)(c->w * 4u + 1024u) * c->h + 4096u;
    uint8_t *back[2] = {(uint8_t *)malloc(backSize), (uint8_t *)malloc(backSize)};
    *why = NULL;
    *bad = -1;
    const RdPipeKeyInt kg = rd__paste_spec_general_key(), ks = rd__paste_spec_key();
    const RhiPipeline pipe[2] = {rd__get_pipeline(&kg), rd__get_pipeline(&ks)};
    if (!src.id || !dst[0].id || !dst[1].id || !m || !back[0] || !back[1]) {
        *why = "no memory for its textures";
    } else if (!pipe[0].id || !pipe[1].id) {
        *why = "its pipelines missing";
    } else {
        for (uint32_t y = 0; y < c->h; y++) {
            for (uint32_t x = 0; x < c->w * 4u; x++) {
                m[(size_t)y * dPitch + x] = psLevel(y * c->w * 4u + x, 1u);
            }
        }
        for (uint32_t y = 0; y < sh; y++) {
            for (uint32_t x = 0; x < sw * 4u; x++) {
                m[dBytes + (size_t)y * sPitch + x] = psLevel(y * sw * 4u + x, 2u);
            }
        }
        rhi_unmap_buffer(up);
        m = NULL;
        RhiCommandList cl =
            rd__begin_own_frame(64u * 1024u) ? rhi_begin_commands() : (RhiCommandList){0};
        if (!cl.id) {
            *why = "no command list";
        } else {
            rd__upload_textures(cl);
            RhiState ss = RHI_STATE_UNDEFINED, ds[2] = {RHI_STATE_UNDEFINED, RHI_STATE_UNDEFINED};
            rd__transition(cl, src, &ss, RHI_STATE_COPY_DST);
            rhi_cmd_copy_buffer_to_texture(cl, up, dBytes, sPitch, src, 0, (RhiRect){0, 0, sw, sh});
            rd__transition(cl, src, &ss, RHI_STATE_SHADER_READ);
            RdUniform frame;
            RhiViewport vp = {0.0f, 0.0f, (float)c->w, (float)c->h, 0.0f, 1.0f};
            if (c->own) {
                /* frameRefresh's FrameCB for a draw on SCENE (no depth, the
                 * native frame's offset), as the replay records it */
                RdStateBlock st;
                rd__reset_state_block(&st);
                st.color = sceneId;
                st.depth = 0;
                st.gsW = c->gsW;
                st.gsH = c->gsH;
                st.useOffset = 0;
                rd__replay_vp_exact(true);
                frame = rd__replay_vu_frame(&st, 0);
                rd__replay_vp_exact(false);
                const uint32_t pw = psExtent(c->w), ph = psExtent(c->h);
                vp.x = floorf(((float)c->w - (float)pw) * 0.5f);
                vp.y = floorf(((float)c->h - (float)ph) * 0.5f);
                vp.w = (float)pw;
                vp.h = (float)ph;
            } else {
                frame = rd__frame_group_ex(c->gsW, c->gsH, (float)(2048u - (c->gsW >> 1)),
                                           (float)(2048u - (c->gsH >> 1)) + c->oyAdd,
                                           1.0f / 16777216.0f, 1.0f, c->sx, c->sy);
            }
            RhiBinding bd[3];
            memset(bd, 0, sizeof(bd));
            bd[0].slot = 1;
            bd[0].type = RHI_BIND_SAMPLED_TEXTURE;
            bd[0].texture = src;
            bd[0].aspect = RHI_ASPECT_COLOR;
            bd[1].slot = 1;
            bd[1].type = RHI_BIND_SAMPLER;
            bd[1].sampler =
                rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
            bd[2].slot = 2;
            bd[2].type = RHI_BIND_SAMPLED_TEXTURE;
            bd[2].texture = g_rd.dummy;
            bd[2].aspect = RHI_ASPECT_COLOR;
            const RhiBindGroup g = rd__replay_bind_group(g_rd.layoutTex, bd, 3);
            if (!g.id || !frame.group.id) {
                *why = "no bind group";
            }
            for (int k = 0; k < 2; k++) {
                rd__transition(cl, dst[k], &ds[k], RHI_STATE_COPY_DST);
                rhi_cmd_copy_buffer_to_texture(cl, up, 0, dPitch, dst[k], 0,
                                               (RhiRect){0, 0, c->w, c->h});
                rd__transition(cl, dst[k], &ds[k], RHI_STATE_RENDER_TARGET);
                RhiRenderPassDesc p;
                memset(&p, 0, sizeof(p));
                p.color[0].texture = dst[k];
                p.color[0].load = RHI_LOAD_LOAD; /* the random destination */
                p.color[0].store = RHI_STORE_STORE;
                p.colorCount = 1;
                p.width = c->w;
                p.height = c->h;
                rhi_cmd_begin_render_pass(cl, &p);
                rhi_cmd_set_viewport(cl, &vp);
                for (uint32_t b = 0; !*why && b < PS_BANDS; b++) {
                    const uint32_t y0 = c->h * b / PS_BANDS, y1 = c->h * (b + 1u) / PS_BANDS;
                    const RhiRect band = {0, (int32_t)y0, c->w, y1 - y0};
                    rhi_cmd_set_scissor(cl, &band);
                    IcoDrawCB cb;
                    psCb(&cb, b, c->gsW, c->gsH, sw, sh, sabotage && k == 1);
                    const RdPipeKeyInt *key = k ? &ks : &kg;
                    rd__set_pipeline(cl, pipe[k], key);
                    rd__bind_uniform(cl, 0, frame);
                    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
                    rhi_cmd_set_bind_group(cl, 2, g);
                    rhi_cmd_draw(cl, 3, 0, 1);
                }
                rhi_cmd_end_render_pass(cl);
                rd__transition(cl, dst[k], &ds[k], RHI_STATE_COPY_SRC);
            }
            rhi_end_commands(cl);
            rhi_submit(cl);
            rhi_wait_idle();
        }
        uint32_t rp[2] = {0, 0};
        for (int k = 0; k < 2 && !*why; k++) {
            if (!rhi_readback_texture(dst[k], RHI_ASPECT_COLOR, back[k], backSize, &rp[k]) ||
                rp[k] < c->w * 4u || (size_t)rp[k] * c->h > backSize) {
                *why = "its readback failed";
            }
        }
        for (uint32_t i = 0; !*why && i < c->w * c->h && *bad < 0; i++) {
            const uint32_t x = i % c->w, y = i / c->w;
            memcpy(want, back[0] + (size_t)y * rp[0] + x * 4u, 4);
            memcpy(got, back[1] + (size_t)y * rp[1] + x * 4u, 4);
            if (memcmp(got, want, 4) != 0) {
                *bad = (int)i;
            }
        }
        if (!*why) {
            *texels += (uint64_t)c->w * c->h;
        }
    }
    if (m) {
        rhi_unmap_buffer(up);
    }
    rhi_wait_idle();
    if (src.id) {
        rhi_destroy_texture(src);
    }
    for (int k = 0; k < 2; k++) {
        if (dst[k].id) {
            rhi_destroy_texture(dst[k]);
        }
    }
    if (up.id) {
        rhi_destroy_buffer(up);
    }
    free(back[0]);
    free(back[1]);
    return *why == NULL;
}

bool rd__paste_spec_selftest(void)
{
    s_ps.test = 0;
    s_ps.words[0] = '\0';
    if (!g_rd.inited || !g_rd.hasDevice || !g_rd.fs[RD_FS_FX_PASTE].id ||
        !g_rd.fs[RD_FS_FX_FRAME].id || !wanted()) {
        stateNote();
        return true; /* not asked here: nothing to check */
    }
    s_ps.test = 2;
    /* every band's DrawCB passes the gate (the test's own premise) */
    const RdPipeKeyInt kg = rd__paste_spec_general_key();
    for (uint32_t b = 0; b < PS_BANDS; b++) {
        IcoDrawCB cb;
        psCb(&cb, b, 96u, 56u, 48u, 16u, false);
        if (rd__paste_spec_why(&cb, &kg, &kg) != RD_PASTE_SPEC_WHY_NONE) {
            snprintf(s_ps.words, sizeof(s_ps.words), "not run: band %u refused by the gate", b);
            rd__log("paste: self-test not run (band %u refused by the gate): fx_frame_ps draws "
                    "the paste",
                    b);
            stateNote();
            return false;
        }
    }
    PsCase cases[5] = {
        {"own", 0, 0, 0, 0, 1.0f, 1.0f, 0.0f, true},
        {"1x", 96, 56, 96, 56, 1.0f, 1.0f, 0.0f, false},
        {"2x", 192, 112, 96, 56, 2.0f, 2.0f, 0.0f, false},
        {"fractional", 270, 135, 96, 56, 2.8125f, 2.41071f, 0.0f, false},
        {"half-line", 192, 112, 96, 56, 2.0f, 2.0f, 0.5f, false},
    };
    const uint32_t sceneId = rd_target(RD_TARGET_SCENE).id;
    const RdTargetRec *sc = sceneId ? rd__target_rec(sceneId) : NULL;
    const bool own = sc && sc->color.id && sc->tw && sc->th && sc->w && sc->h;
    if (own) {
        cases[0].w = sc->tw;
        cases[0].h = sc->th;
        cases[0].gsW = sc->w;
        cases[0].gsH = sc->h;
        cases[0].sx = sc->sx;
        cases[0].sy = sc->sy;
    }
    const bool sabotage = s_ps.env == 2;
    uint64_t texels = 0;
    const char *why = NULL;
    int bad = -1;
    uint32_t badCase = 0;
    uint8_t got[4] = {0}, want[4] = {0};
    for (uint32_t i = own ? 0u : 1u; i < 5u && !why && bad < 0; i++) {
        badCase = i;
        (void)psRun(&cases[i], sceneId, sabotage, &why, &bad, got, want, &texels);
    }
    if (!why && bad < 0) {
        s_ps.test = 1;
        /* no brackets in the words: the tool's stats line holds them in its own */
        snprintf(s_ps.words, sizeof(s_ps.words),
                 "passed: %llu texels the same at %ux%u, %s, and at 1x, 2x, 2.8125x2.41071 and "
                 "the half-line",
                 (unsigned long long)texels, own ? cases[0].w : 0u, own ? cases[0].h : 0u,
                 own ? (sc->ox > 0.0f ? "SCENE's scale and Full pixel crop" : "SCENE's scale")
                     : "SCENE not made yet");
        rd__log("paste: the glow's paste by its own entry (fx_paste_ps; self-test: %llu texels "
                "the same at %ux%u and four more scales, five DrawCB forms each): the same bytes",
                (unsigned long long)texels, own ? cases[0].w : 0u, own ? cases[0].h : 0u);
    } else if (why) {
        snprintf(s_ps.words, sizeof(s_ps.words), "not run: %s, case %s", why, cases[badCase].name);
        rd__log("paste: self-test not run (%s, case %s): fx_frame_ps draws the paste", why,
                cases[badCase].name);
    } else {
        const PsCase *c = &cases[badCase];
        snprintf(s_ps.words, sizeof(s_ps.words), "failed at texel %d of case %s", bad, c->name);
        rd__log("paste: self-test failed at texel %d (%u, %u) of case %s (%ux%u): "
                "%02X%02X%02X%02X for %02X%02X%02X%02X%s: fx_frame_ps draws the paste",
                bad, (uint32_t)bad % c->w, (uint32_t)bad / c->w, c->name, c->w, c->h, got[0],
                got[1], got[2], got[3], want[0], want[1], want[2], want[3],
                sabotage ? " (ICO_RD_PASTE_SPEC=sabotage)" : "");
    }
    stateNote();
    return s_ps.test == 1;
}
