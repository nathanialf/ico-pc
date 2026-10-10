/* rd_fog_path.c: how the depth fog (rd_replay.c doFog) reads the scene
 * depth, and the two checks that say what that read gives on the device.
 *
 * The paths (RdFogPath):
 *   inplace  the depth target sampled where it is (RHI_STATE_DEPTH_READ);
 *   copy     a whole copy of it (vkCmdCopyImage into a second depth-stencil
 *            texture) sampled instead (rd_core.c rd__sampled_depth);
 *   buffer   its depth aspect copied into a device buffer and from there
 *            into an R32_UINT colour texture of the same size, read as words
 *            by fog_lut_buffer_ps (a float's bits on D32S8; on D24S8 the
 *            24-bit step in the low bits, the top byte undefined and masked
 *            off).  Two whole-picture transfers a fogged frame: a fallback
 *            only.  Vulkan only (D3D12 copies a depth only as a whole
 *            subresource into a placed footprint; not done there).
 * Every target depth is made with every use the device allows (sampled
 * and copy source), so the path can change between two replays.
 *
 * The probe: on the first fogged replay after start and after SCENE is made
 * at a new size, five texels (centre, the corners 8 pixels in) of the depth
 * the fog read and of the colour it drew are copied out after the fog's
 * draw and logged at the next rd_begin_frame as GS Z, fog index and RGBA:
 *   fog: probe <path> z=<5 x GS Z> idx=<5 x index> out=<5 x RGBA>
 * so a report's log says what the device returned.
 *
 * The self-test (rd_fog_selftest): a frame built here, never recorded (no
 * frame number, no LUT of the game's, no held targets touched), is replayed
 * on SCENE at its real size and scale: a 16 x 16 grid of cells at GS Z
 * (index << 16) | 0x8000, index 0..255 one per cell, then RD_POST_FOG with
 * a LUT whose every entry differs, as ZFog.c fogHostDraw records it.  The
 * cell centres and the probe's five points are read back and compared with
 * the GS arithmetic (index = Z >> 16, LUT, MODULATE by 0x80, the LERP with
 * As, alpha As; rd_fog_test's model) for each path in the platform's order
 * (desktop: inplace, copy, buffer; tile-based or D24S8: copy, inplace,
 * buffer).  The first that passes is the path from then on; none passing,
 * buffer is kept (if the device has it) and the log says so loudly:
 *   fog: depth path <name> (self-test: inplace ok 0, copy ok 0, buffer ok 0)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"

static const char *const s_pathNames[RD_FOG_PATH_COUNT] = {"inplace", "copy", "buffer"};

/* the order the self-test tries the paths in */
static const uint8_t kOrderDesktop[RD_FOG_PATH_COUNT] = {RD_FOG_INPLACE, RD_FOG_COPY,
                                                         RD_FOG_BUFFER};
static const uint8_t kOrderTiler[RD_FOG_PATH_COUNT] = {RD_FOG_COPY, RD_FOG_INPLACE, RD_FOG_BUFFER};

/* the environment, read at rd__fog_path_init */
static int s_override = -1; /* the path ICO_RD_FOG_PATH or ICO_RD_DEPTH_COPY names */
static const char *s_overrideWhy;
static bool s_testAll;     /* ICO_RD_FOG_PATH=test: the self-test tries every path */
static uint8_t s_sabotage; /* ICO_RD_FOG_SABOTAGE: a bit per path */

/* the buffer path's objects, at the size of the depth last read */
static struct {
    RhiBuffer buf;
    RhiTexture tex;
    RhiState state;
    uint32_t w, h;
} s_buf;

#define PROBE_POINTS 5

static struct {
    bool armed;   /* the next fogged replay records */
    bool pending; /* recorded, read back at the next rd_begin_frame */
    bool haveZ;   /* the depth texels were copied (not on D3D12's depth) */
    bool d24;
    uint8_t path;
    float zScale;
    RhiBuffer buf; /* the depth texels, one per copyOffsetAlign */
    RhiTexture zTex, cTex;
    RhiState zState, cState;
    RhiFormat cFmt;
} s_probe;

static struct {
    bool armed;   /* rd_fog_selftest ran: a new SCENE size runs it again */
    bool running; /* its replays: no probe */
    uint32_t w, h, tw, th;
} s_st;

/* ------------------------------------------------------------ the paths */

const char *rd__fog_path_name(int path)
{
    return path >= 0 && path < RD_FOG_PATH_COUNT ? s_pathNames[path] : "?";
}

bool rd__fog_path_supported(int path)
{
    if (!g_rd.hasDevice) {
        return path == RD_FOG_INPLACE || path == RD_FOG_COPY;
    }
    switch (path) {
    case RD_FOG_INPLACE:
    case RD_FOG_COPY: /* the copy is the same format, sampled */
        return rhi_limits()->depthSampled;
    case RD_FOG_BUFFER:
        return rhi_backend() == RHI_BACKEND_VULKAN;
    default:
        return false;
    }
}

void rd__set_fog_path(int path)
{
    if (path < 0 || path >= RD_FOG_PATH_COUNT) {
        return;
    }
    g_rd.fogPath = (uint8_t)path;
    g_rd.depthCopy = path != RD_FOG_INPLACE;
}

static bool depthIs24(void)
{
    const char *ds = g_rd.hasDevice ? rhi_limits()->depthStencilFormatName : NULL;
    return ds && strcmp(ds, "D24S8") == 0;
}

static const uint8_t *platformOrder(void)
{
    const bool tiler = g_rd.hasDevice && rhi_limits()->tiler;
    return tiler || depthIs24() ? kOrderTiler : kOrderDesktop;
}

static int pathOf(const char *s, size_t n)
{
    for (int p = 0; p < RD_FOG_PATH_COUNT; p++) {
        if (strlen(s_pathNames[p]) == n && strncmp(s, s_pathNames[p], n) == 0) {
            return p;
        }
    }
    return -1;
}

void rd__fog_path_init(void)
{
    s_override = -1;
    s_overrideWhy = NULL;
    s_testAll = false;
    s_sabotage = 0;
    const char *fp = getenv("ICO_RD_FOG_PATH");
    const char *dc = getenv("ICO_RD_DEPTH_COPY");
    const char *sb = getenv("ICO_RD_FOG_SABOTAGE");
    if (fp && strcmp(fp, "test") == 0) {
        s_testAll = true;
        rd__log("fog: ICO_RD_FOG_PATH=test: the self-test tries every path");
    } else if (fp && fp[0]) {
        s_override = pathOf(fp, strlen(fp));
        if (s_override < 0) {
            rd__log("fog: ICO_RD_FOG_PATH=%s is not inplace, copy, buffer or test: ignored", fp);
        } else {
            s_overrideWhy = "ICO_RD_FOG_PATH";
        }
    }
    if (s_override < 0 && dc && dc[0]) {
        s_override = dc[0] != '0' ? RD_FOG_COPY : RD_FOG_INPLACE;
        s_overrideWhy = "ICO_RD_DEPTH_COPY";
    }
    while (sb && *sb) {
        const char *e = strchr(sb, ',');
        const size_t n = e ? (size_t)(e - sb) : strlen(sb);
        const int p = pathOf(sb, n);
        if (p >= 0) {
            s_sabotage |= (uint8_t)(1u << p);
        }
        sb = e ? e + 1 : NULL;
    }
    const uint8_t *order = platformOrder();
    int path = s_override >= 0 ? s_override : order[0];
    const char *why = s_override >= 0                         ? s_overrideWhy
                      : g_rd.hasDevice && rhi_limits()->tiler ? "tile-based GPU"
                      : depthIs24()                           ? "D24S8"
                                                              : "desktop GPU";
    if (!rd__fog_path_supported(path)) {
        const int want = path;
        for (int i = 0; i < RD_FOG_PATH_COUNT; i++) {
            if (rd__fog_path_supported(order[i])) {
                path = order[i];
                break;
            }
        }
        rd__log("fog: the %s path is not available on this device: %s", rd__fog_path_name(want),
                rd__fog_path_name(path));
    }
    rd__set_fog_path(path);
    rd__log("fog: depth path %s at start (%s)", rd__fog_path_name(path), why);
    if (s_sabotage) {
        rd__log("fog: ICO_RD_FOG_SABOTAGE: the depth reads of%s%s%s give 0",
                (s_sabotage & 1) ? " inplace" : "", (s_sabotage & 2) ? " copy" : "",
                (s_sabotage & 4) ? " buffer" : "");
    }
    memset(&s_st, 0, sizeof(s_st));
    s_probe.armed = true;
    s_probe.pending = false;
}

/* ----------------------------------------------------- the depth source */

void rd__fog_buffer_free(void)
{
    if (s_buf.buf.id) {
        rhi_destroy_buffer(s_buf.buf);
    }
    if (s_buf.tex.id) {
        rhi_destroy_texture(s_buf.tex);
    }
    memset(&s_buf, 0, sizeof(s_buf));
}

/* the row pitch of w words, as the backend's buffer copies want it */
static uint32_t wordPitch(uint32_t w)
{
    const uint32_t a = rhi_limits()->copyRowPitchAlign ? rhi_limits()->copyRowPitchAlign : 1u;
    return (w * 4u + a - 1u) / a * a;
}

bool rd__fog_source(RhiCommandList cl, RdTargetRec *t, RdDepthCopy *copy, RdFogSource *out)
{
    memset(out, 0, sizeof(*out));
    out->sabotaged = (uint8_t)((s_sabotage >> g_rd.fogPath) & 1u);
    if (g_rd.fogPath != RD_FOG_BUFFER) {
        const RhiTexture z = rd__sampled_depth(cl, t, copy, "rd fog depth");
        if (!z.id) {
            return false;
        }
        out->tex = z;
        out->state = z.id == t->depth.id ? &t->depthState : &copy->state;
        out->aspect = RHI_ASPECT_DEPTH;
        return true;
    }
    const uint32_t w = t->tw, h = t->th, pitch = wordPitch(t->tw);
    if (!s_buf.tex.id || s_buf.w != w || s_buf.h != h) {
        rd__fog_buffer_free();
        /* device-local: the copies stay on the GPU */
        s_buf.buf = rhi_create_buffer(&(RhiBufferDesc){(uint64_t)pitch * h,
                                                       RHI_BUF_COPY_SRC | RHI_BUF_COPY_DST,
                                                       RHI_MEM_DEVICE, "rd fog depth words"});
        s_buf.tex = rhi_create_texture(&(RhiTextureDesc){
            w, h, 1, RHI_FMT_R32_UINT, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST | RHI_TEX_COPY_SRC,
            "rd fog depth words"});
        s_buf.state = RHI_STATE_UNDEFINED;
        s_buf.w = w;
        s_buf.h = h;
        if (!s_buf.buf.id || !s_buf.tex.id) {
            rd__fog_buffer_free();
            rd__note_scene_pressure();
            return false;
        }
    }
    const RhiRect all = {0, 0, w, h};
    rd__transition(cl, t->depth, &t->depthState, RHI_STATE_COPY_SRC);
    rhi_cmd_copy_texture_to_buffer(cl, t->depth, RHI_ASPECT_DEPTH, all, s_buf.buf, 0, pitch);
    rd__transition(cl, s_buf.tex, &s_buf.state, RHI_STATE_COPY_DST);
    rhi_cmd_copy_buffer_to_texture(cl, s_buf.buf, 0, pitch, s_buf.tex, 0, all);
    rd__transition(cl, s_buf.tex, &s_buf.state, RHI_STATE_SHADER_READ);
    out->tex = s_buf.tex;
    out->state = &s_buf.state;
    out->aspect = RHI_ASPECT_COLOR;
    out->buffer = 1;
    return true;
}

/* ------------------------------------------------------------ the probe */

/* centre, then the corners 8 pixels in (top left, top right, bottom left,
 * bottom right) of a w x h texture */
static void probePoints(uint32_t w, uint32_t h, uint32_t xs[PROBE_POINTS],
                        uint32_t ys[PROBE_POINTS])
{
    const uint32_t x0 = w > 16 ? 8 : 0, x1 = w > 16 ? w - 9 : (w ? w - 1 : 0);
    const uint32_t y0 = h > 16 ? 8 : 0, y1 = h > 16 ? h - 9 : (h ? h - 1 : 0);
    const uint32_t px[PROBE_POINTS] = {w / 2, x0, x1, x0, x1};
    const uint32_t py[PROBE_POINTS] = {h / 2, y0, y0, y1, y1};
    memcpy(xs, px, sizeof(px));
    memcpy(ys, py, sizeof(py));
}

/* The GS Z a depth word stands for, as fog_lut.hlsl fog_gs_z computes it */
static uint32_t gsZOf(uint32_t word, bool d24, float scale)
{
    float d;
    if (d24) {
        d = (float)(word & 0xFFFFFFu) / 16777215.0f;
    } else {
        memcpy(&d, &word, sizeof(d));
    }
    if (!(d == d)) {
        d = 0.0f; /* NaN: what the device returned is no depth */
    }
    if (scale < 1.5e-10f && d >= 1.0f - 1.0f / 256.0f) {
        const float s = (d - (1.0f - 1.0f / 256.0f)) * 16777216.0f;
        return 0xFFFF0000u + (s > 65535.0f ? 65535u : (uint32_t)s);
    }
    float zf = scale > 0.0f ? d / scale : 0.0f;
    zf = zf < 0.0f ? 0.0f : (zf > 4294967040.0f ? 4294967040.0f : zf);
    return (uint32_t)zf;
}

bool rd__fog_probe_wanted(void)
{
    return g_rd.hasDevice && s_probe.armed && !s_probe.pending && !s_st.running;
}

bool rd__fog_probe_pending(void)
{
    return s_probe.pending;
}

static void probeFree(void)
{
    if (s_probe.buf.id) {
        rhi_destroy_buffer(s_probe.buf);
    }
    if (s_probe.zTex.id) {
        rhi_destroy_texture(s_probe.zTex);
    }
    if (s_probe.cTex.id) {
        rhi_destroy_texture(s_probe.cTex);
    }
    s_probe.buf = (RhiBuffer){0};
    s_probe.zTex = s_probe.cTex = (RhiTexture){0};
}

static uint32_t probeStride(void)
{
    const uint32_t a = rhi_limits()->copyOffsetAlign;
    return a > 4 ? a : 4;
}

void rd__fog_probe_record(RhiCommandList cl, const RdTargetRec *tz, uint32_t zid,
                          const RdFogSource *src, RdTargetRec *tc)
{
    s_probe.armed = false; /* one try for each arming */
    if (s_probe.cTex.id && s_probe.cFmt != tc->format) {
        rhi_destroy_texture(s_probe.cTex);
        s_probe.cTex = (RhiTexture){0};
    }
    const uint32_t stride = probeStride(), pitch = wordPitch(1);
    if (!s_probe.buf.id) {
        s_probe.buf = rhi_create_buffer(&(RhiBufferDesc){(uint64_t)stride * PROBE_POINTS,
                                                         RHI_BUF_COPY_SRC | RHI_BUF_COPY_DST,
                                                         RHI_MEM_DEVICE, "rd fog probe"});
    }
    if (!s_probe.zTex.id) {
        s_probe.zTex = rhi_create_texture(&(RhiTextureDesc){PROBE_POINTS, 1, 1, RHI_FMT_R32_UINT,
                                                            RHI_TEX_COPY_DST | RHI_TEX_COPY_SRC,
                                                            "rd fog probe depth"});
        s_probe.zState = RHI_STATE_UNDEFINED;
    }
    if (!s_probe.cTex.id) {
        s_probe.cTex = rhi_create_texture(&(RhiTextureDesc){
            PROBE_POINTS, 1, 1, tc->format, RHI_TEX_COPY_DST | RHI_TEX_COPY_SRC, "rd fog probe"});
        s_probe.cState = RHI_STATE_UNDEFINED;
        s_probe.cFmt = tc->format;
    }
    if (!s_probe.buf.id || !s_probe.zTex.id || !s_probe.cTex.id) {
        rd__log("fog: probe: no texture or buffer for it");
        return;
    }
    uint32_t xs[PROBE_POINTS], ys[PROBE_POINTS];
    /* the depth: D3D12 copies a depth texture to a buffer only whole (the
     * buffer path's words are a colour texture: any backend) */
    s_probe.haveZ = rhi_backend() == RHI_BACKEND_VULKAN || src->aspect == RHI_ASPECT_COLOR;
    if (s_probe.haveZ) {
        probePoints(tz->tw, tz->th, xs, ys);
        rd__transition(cl, src->tex, src->state, RHI_STATE_COPY_SRC);
        for (uint32_t k = 0; k < PROBE_POINTS; k++) {
            rhi_cmd_copy_texture_to_buffer(cl, src->tex, src->aspect,
                                           (RhiRect){(int32_t)xs[k], (int32_t)ys[k], 1, 1},
                                           s_probe.buf, (uint64_t)k * stride, pitch);
        }
        rd__transition(cl, s_probe.zTex, &s_probe.zState, RHI_STATE_COPY_DST);
        for (uint32_t k = 0; k < PROBE_POINTS; k++) {
            rhi_cmd_copy_buffer_to_texture(cl, s_probe.buf, (uint64_t)k * stride, pitch,
                                           s_probe.zTex, 0, (RhiRect){(int32_t)k, 0, 1, 1});
        }
        rd__transition(cl, s_probe.zTex, &s_probe.zState, RHI_STATE_COPY_SRC);
    }
    /* the colour the fog drew */
    probePoints(tc->tw, tc->th, xs, ys);
    rd__transition(cl, tc->color, &tc->colorState, RHI_STATE_COPY_SRC);
    rd__transition(cl, s_probe.cTex, &s_probe.cState, RHI_STATE_COPY_DST);
    for (uint32_t k = 0; k < PROBE_POINTS; k++) {
        rhi_cmd_copy_texture(cl, tc->color, (RhiRect){(int32_t)xs[k], (int32_t)ys[k], 1, 1},
                             s_probe.cTex, (int32_t)k, 0);
    }
    rd__transition(cl, s_probe.cTex, &s_probe.cState, RHI_STATE_COPY_SRC);
    s_probe.path = g_rd.fogPath;
    s_probe.d24 = rd__depth_unorm_steps() > 0.0f;
    s_probe.zScale = rd__target_z_scale(zid);
    s_probe.pending = true;
}

void rd__fog_probe_finish(void)
{
    if (!s_probe.pending) {
        return;
    }
    s_probe.pending = false;
    uint8_t px[PROBE_POINTS * 4];
    uint32_t words[PROBE_POINTS];
    const bool okC =
        rd__read_rhi_texture(s_probe.cTex, &s_probe.cState, PROBE_POINTS, 1, px, sizeof(px));
    const bool okZ = s_probe.haveZ && rd__read_rhi_texture(s_probe.zTex, &s_probe.zState,
                                                           PROBE_POINTS, 1, words, sizeof(words));
    char z[PROBE_POINTS * 12], idx[PROBE_POINTS * 5], out[PROBE_POINTS * 12];
    size_t nz = 0, ni = 0, no = 0;
    z[0] = idx[0] = out[0] = '\0';
    for (int k = 0; k < PROBE_POINTS; k++) {
        const char *sep = k ? "," : "";
        if (okZ) {
            const uint32_t g = gsZOf(words[k], s_probe.d24, s_probe.zScale);
            nz += (size_t)snprintf(z + nz, sizeof(z) - nz, "%s%08X", sep, g);
            /* above the fog sprite's 0xFFFFFF the pixel is not fogged */
            if (g > 0xFFFFFFu) {
                ni += (size_t)snprintf(idx + ni, sizeof(idx) - ni, "%s-", sep);
            } else {
                ni += (size_t)snprintf(idx + ni, sizeof(idx) - ni, "%s%u", sep, (g >> 16) & 0xFFu);
            }
        }
        if (okC) {
            const uint8_t *p = &px[k * 4];
            no += (size_t)snprintf(out + no, sizeof(out) - no, "%s%02X%02X%02X%02X", sep, p[0],
                                   p[1], p[2], p[3]);
        }
    }
    rd__log("fog: probe %s z=%s idx=%s out=%s (centre, then the corners 8 pixels in; %s)",
            rd__fog_path_name(s_probe.path), okZ ? z : "n/a", okZ ? idx : "n/a", okC ? out : "n/a",
            s_probe.d24 ? "D24S8" : "D32S8");
}

/* ------------------------------------------------------------ self-test */

#define ST_CELLS 16
#define ST_SAMPLES (PROBE_POINTS + ST_CELLS * ST_CELLS)
#define ST_TOLERANCE 2

static const uint8_t kStClear[4] = {30, 60, 90, 0x80};

static void stLut(int i, uint8_t out[4])
{
    out[0] = (uint8_t)i;
    out[1] = (uint8_t)(i * 97 + 13);
    out[2] = (uint8_t)(255 - i);
    out[3] = (uint8_t)(0x10 + i * 0x70 / 255); /* 0x10 .. 0x80: As within 1.0 */
}

static void stCellColor(int c, uint8_t out[4])
{
    out[0] = (uint8_t)(c * 53 + 17);
    out[1] = (uint8_t)(c * 101 + 71);
    out[2] = (uint8_t)(c * 29 + 200);
    out[3] = 0x40;
}

static RdCmd *stPush(RdFrame *f, int list, uint8_t type)
{
    RdCmdList *cl = &f->lists[list];
    if (cl->count == cl->cap) {
        const uint32_t cap = cl->cap ? cl->cap * 2 : 32;
        RdCmd *p = realloc(cl->cmds, (size_t)cap * sizeof(RdCmd));
        if (!p) {
            return NULL;
        }
        cl->cmds = p;
        cl->cap = cap;
    }
    RdCmd *c = &cl->cmds[cl->count++];
    memset(c, 0, sizeof(*c));
    c->type = type;
    return c;
}

static bool stPush1(RdFrame *f, int list, uint8_t type, uint8_t v)
{
    RdCmd *c = stPush(f, list, type);
    if (c) {
        c->b[0] = v;
    }
    return c != NULL;
}

static bool stTest(RdFrame *f, int list, uint64_t word)
{
    const RdTestState t = rd_test_from_gs(word);
    RdCmd *c = stPush(f, list, RDC_TEST);
    if (c) {
        c->b[0] = t.ate;
        c->b[1] = t.atst;
        c->b[2] = t.aref;
        c->b[3] = t.afail;
        c->b[4] = t.date;
        c->b[5] = t.zte;
        c->b[6] = t.ztst;
    }
    return c != NULL;
}

static bool stTarget(RdFrame *f, int list, uint32_t scene, uint32_t w, uint32_t h)
{
    RdCmd *c = stPush(f, list, RDC_TARGET);
    if (c) {
        c->u[0] = scene;
        c->u[1] = scene;
        c->u[2] = (w & 0xFFFF) | ((h & 0xFFFF) << 16);
        c->b[0] = RD_TARGET_OFFSET;
    }
    return c != NULL;
}

/* the cell (cx, cy) of the GS pixel column x, row y */
static int stCellAt(uint32_t w, uint32_t h, uint32_t x, uint32_t y)
{
    int cx = 0, cy = 0;
    while (cx + 1 < ST_CELLS && x >= (uint32_t)(cx + 1) * w / ST_CELLS) {
        cx++;
    }
    while (cy + 1 < ST_CELLS && y >= (uint32_t)(cy + 1) * h / ST_CELLS) {
        cy++;
    }
    return cy * ST_CELLS + cx;
}

/* What rd_fog_test case p and ZFog.c record, built directly: list 0 the
 * cells (sprites in full-screen space, so they span the whole texture as
 * the fog's sprite does, wide or not), list 4 fog_DrawFog's state and the
 * RD_POST_FOG record. */
static bool stBuildFrame(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h)
{
    memset(f, 0, sizeof(*f));
    rd__reset_state_block(&f->startState);
    f->gsW = w;
    f->gsH = h;
    bool ok = stTarget(f, 0, scene, w, h);
    RdCmd *c = stPush(f, 0, RDC_CLEAR);
    if (c) {
        c->u[0] = scene;
        memcpy(c->b, kStClear, 4);
        c->b[4] = 1;
        c->u[1] = 0;
    }
    ok = ok && c && stPush(f, 0, RDC_TEXTURE_OFF) && stPush1(f, 0, RDC_ABE, 0) &&
         stTest(f, 0, RD_TEST_Z_ALWAYS) && stPush1(f, 0, RDC_ZWRITE, RD_ZWRITE_ON) &&
         stPush1(f, 0, RDC_FBA, 0) && stPush1(f, 0, RDC_SHADE, 0);
    RdScreenVtx v[ST_CELLS * ST_CELLS * 2];
    memset(v, 0, sizeof(v));
    const int32_t ox = 2048 - (int32_t)(w / 2), oy = 2048 - (int32_t)(h / 2);
    for (int cell = 0; cell < ST_CELLS * ST_CELLS; cell++) {
        const int cx = cell % ST_CELLS, cy = cell / ST_CELLS;
        const int32_t x0 = (int32_t)((uint32_t)cx * w / ST_CELLS);
        const int32_t x1 = (int32_t)((uint32_t)(cx + 1) * w / ST_CELLS);
        const int32_t y0 = (int32_t)((uint32_t)cy * h / ST_CELLS);
        const int32_t y1 = (int32_t)((uint32_t)(cy + 1) * h / ST_CELLS);
        uint8_t rgba[4];
        stCellColor(cell, rgba);
        for (int k = 0; k < 2; k++) {
            RdScreenVtx *p = &v[cell * 2 + k];
            p->x = (ox + (k ? x1 : x0)) * 16;
            p->y = (oy + (k ? y1 : y0)) * 16;
            /* index cell, 0x8000 into its band: on D24S8 too far from
             * either edge for the 24-bit step to cross it */
            p->z = ((uint32_t)cell << 16) | 0x8000u;
            p->q = 1.0f;
            memcpy(p->rgba, rgba, 4);
        }
    }
    c = stPush(f, 0, RDC_SCREEN);
    if (c) {
        c->b[0] = RD_PRIM_SPRITES;
        c->b[1] = RD_SPACE_FULLSCREEN;
        c->u[0] = rd__frame_payload(f, v, (uint32_t)sizeof(v));
        c->u[1] = ST_CELLS * ST_CELLS * 2;
    }
    ok = ok && c;
    /* ZFog.c fogHostDraw */
    const RdTex depthView = rd_target_texture((RdTarget){scene}, RD_VIEW_DEPTH);
    ok = ok && depthView.id && stTarget(f, 4, scene, w, h) && stPush1(f, 4, RDC_PABE, 0);
    c = stPush(f, 4, RDC_ALPHA);
    if (c) {
        c->b[0] = RD_BLEND_LERP_AS;
        c->b[1] = 128;
    }
    ok = ok && c;
    c = stPush(f, 4, RDC_TEXTURE);
    if (c) {
        c->u[0] = depthView.id;
        c->b[0] = RD_TEXFN_MODULATE;
        c->b[1] = RD_TCC_RGBA;
    }
    ok = ok && c && stPush1(f, 4, RDC_ZWRITE, RD_ZWRITE_OFF) && stTest(f, 4, RD_TEST_Z_GEQUAL);
    c = stPush(f, 4, RDC_FILTER);
    if (c) {
        c->b[0] = RD_FILTER_NEAREST;
        c->b[1] = RD_FILTER_NEAREST;
    }
    ok = ok && c && stPush1(f, 4, RDC_ABE, 1) && stPush1(f, 4, RDC_SHADE, 0);
    uint8_t lut[256 * 4];
    for (int i = 0; i < 256; i++) {
        stLut(i, &lut[i * 4]);
    }
    RdPostRec p;
    memset(&p, 0, sizeof(p));
    p.rgba[0] = p.rgba[1] = p.rgba[2] = 0x80;
    p.rgba[3] = 0x80; /* fogStrength */
    p.z = 0xFFFFFF;
    p.rect[0] = (float)(0x8000 - (int32_t)w * 8);
    p.rect[1] = (float)(0x8000 - (int32_t)h * 8);
    p.rect[2] = (float)(0x8000 + (int32_t)w * 8);
    p.rect[3] = (float)(0x8000 + (int32_t)h * 8);
    p.uv[0] = 8.0f;
    p.uv[1] = 8.0f;
    p.uv[2] = (float)(8 + w * 16);
    p.uv[3] = (float)(8 + h * 16);
    p.lutOffset = rd__frame_payload(f, lut, sizeof(lut));
    c = stPush(f, 4, RDC_POST_STUB);
    if (c) {
        c->b[0] = RD_POST_FOG;
        c->u[1] = rd__frame_payload(f, &p, (uint32_t)sizeof(p));
        c->u[2] = (uint32_t)sizeof(p);
    }
    return ok && c;
}

/* the GS LERP (Cs - Cd) * A >> 7 + Cd, arithmetic shift, clamped */
static int stLerp(int cs, int cd, int a)
{
    const int v = (((cs - cd) * a) >> 7) + cd;
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

/* The sample points in SCENE's texels: the probe's five, then the centre
 * of every cell; cell[k] the cells sample k may lie in.  A texel whose
 * centre is within one texel of a cell edge (the probe's centre is exactly
 * on one whenever the texel count is odd: texel 853 of 1707 is GS x 320.0,
 * the edge of cells 7 and 8) is drawn by whichever sprite the rasteriser's
 * fill rule and the field offset give it, so both neighbours count; a cell
 * centre has one. */
#define ST_CANDIDATES 4

static void stSamples(const RdTargetRec *t, uint32_t xs[ST_SAMPLES], uint32_t ys[ST_SAMPLES],
                      int cell[ST_SAMPLES][ST_CANDIDATES])
{
    probePoints(t->tw, t->th, xs, ys);
    for (int c = 0; c < ST_CELLS * ST_CELLS; c++) {
        const uint32_t cx = (uint32_t)(c % ST_CELLS), cy = (uint32_t)(c / ST_CELLS);
        /* the cell's middle in GS pixels, then in texels */
        const double gx =
            ((double)(cx * t->w / ST_CELLS) + (double)((cx + 1) * t->w / ST_CELLS)) / 2;
        const double gy =
            ((double)(cy * t->h / ST_CELLS) + (double)((cy + 1) * t->h / ST_CELLS)) / 2;
        xs[PROBE_POINTS + c] = (uint32_t)(gx * t->tw / t->w);
        ys[PROBE_POINTS + c] = (uint32_t)(gy * t->th / t->h);
    }
    /* one texel in GS pixels */
    const double dx = (double)t->w / t->tw, dy = (double)t->h / t->th;
    for (int k = 0; k < ST_SAMPLES; k++) {
        /* the texel's centre in GS pixels, one texel either side */
        const double cx = ((double)xs[k] + 0.5) * dx, cy = ((double)ys[k] + 0.5) * dy;
        const double gx[2] = {cx - dx < 0.0 ? 0.0 : cx - dx, cx + dx};
        const double gy[2] = {cy - dy < 0.0 ? 0.0 : cy - dy, cy + dy};
        for (int j = 0; j < ST_CANDIDATES; j++) {
            cell[k][j] = stCellAt(t->w, t->h, (uint32_t)gx[j & 1], (uint32_t)gy[j >> 1]);
        }
    }
}

static struct {
    RhiTexture tex;
    RhiState state;
} s_stRead;

/* One replay of f on the path in force; *maxErr the largest channel
 * difference from the model at the sample points.  False when the replay
 * or the readback could not be done. */
static bool stRun(const RdFrame *f, RdTargetRec *t, int *maxErr, int *worst)
{
    if (!rd__replay_frame(f, 0, false)) {
        return false;
    }
    if (!s_stRead.tex.id) {
        s_stRead.tex = rhi_create_texture(&(RhiTextureDesc){
            ST_SAMPLES, 1, 1, t->format, RHI_TEX_COPY_DST | RHI_TEX_COPY_SRC, "rd fog self-test"});
        s_stRead.state = RHI_STATE_UNDEFINED;
        if (!s_stRead.tex.id) {
            return false;
        }
    }
    uint32_t xs[ST_SAMPLES], ys[ST_SAMPLES];
    static int cell[ST_SAMPLES][ST_CANDIDATES];
    stSamples(t, xs, ys, cell);
    RhiCommandList cl = rhi_begin_commands();
    if (!cl.id) {
        return false;
    }
    rd__transition(cl, t->color, &t->colorState, RHI_STATE_COPY_SRC);
    rd__transition(cl, s_stRead.tex, &s_stRead.state, RHI_STATE_COPY_DST);
    for (uint32_t k = 0; k < ST_SAMPLES; k++) {
        rhi_cmd_copy_texture(cl, t->color, (RhiRect){(int32_t)xs[k], (int32_t)ys[k], 1, 1},
                             s_stRead.tex, (int32_t)k, 0);
    }
    rhi_end_commands(cl);
    rhi_submit(cl);
    static uint8_t px[ST_SAMPLES * 4];
    if (!rd__read_rhi_texture(s_stRead.tex, &s_stRead.state, ST_SAMPLES, 1, px, sizeof(px))) {
        return false;
    }
    int m = 0;
    *worst = 0;
    for (int k = 0; k < ST_SAMPLES; k++) {
        /* the closest of the cells the texel may lie in */
        int best = 256;
        for (int j = 0; j < ST_CANDIDATES; j++) {
            uint8_t lut[4], cd[4];
            stLut(cell[k][j], lut);
            stCellColor(cell[k][j], cd);
            /* MODULATE by (0x80, 0x80, 0x80, strength 0x80), the LERP with As */
            const int as = (lut[3] * 0x80) >> 7;
            int want[4];
            for (int ch = 0; ch < 3; ch++) {
                want[ch] = stLerp((lut[ch] * 0x80) >> 7, cd[ch], as > 0x80 ? 0x80 : as);
            }
            want[3] = as;
            int e = 0;
            for (int ch = 0; ch < 4; ch++) {
                const int d = abs((int)px[k * 4 + ch] - want[ch]);
                e = d > e ? d : e;
            }
            best = e < best ? e : best;
        }
        if (best > m) {
            m = best;
            *worst = k;
        }
    }
    *maxErr = m;
    return true;
}

static bool runSelftest(void)
{
    RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (!t || !t->withDepth || !t->color.id || !t->depth.id || t->format != RHI_FMT_RGBA8_UNORM) {
        rd__log("fog: self-test: no SCENE target to run it on");
        return false;
    }
    s_st.w = t->w;
    s_st.h = t->h;
    s_st.tw = t->tw;
    s_st.th = t->th;
    static RdFrame f;
    if (!stBuildFrame(&f, rd_target(RD_TARGET_SCENE).id, t->w, t->h)) {
        rd__frame_free(&f);
        rd__log("fog: self-test: could not build its frame");
        return false;
    }
    /* the platform's order, an override's path first */
    uint8_t order[RD_FOG_PATH_COUNT];
    memcpy(order, platformOrder(), sizeof(order));
    if (s_override >= 0) {
        for (int i = RD_FOG_PATH_COUNT - 1; i > 0; i--) {
            if (order[i] == s_override) {
                order[i] = order[i - 1];
                order[i - 1] = (uint8_t)s_override;
            }
        }
    }
    const int before = g_rd.fogPath;

    enum { ST_NOT_TRIED, ST_UNSUPPORTED, ST_ERROR, ST_FAIL, ST_OK };

    /* the paths in order up to the first that passes: a path past it is
       not tried (each one copies the depth its own way, and a device need
       not run a way it does not use), unless ICO_RD_FOG_PATH=test */
    int result[RD_FOG_PATH_COUNT], err[RD_FOG_PATH_COUNT], worst[RD_FOG_PATH_COUNT];
    bool found = false;
    s_st.running = true;
    for (int i = 0; i < RD_FOG_PATH_COUNT; i++) {
        const int p = order[i];
        err[p] = worst[p] = 0;
        if (found && !s_testAll) {
            result[p] = ST_NOT_TRIED;
            continue;
        }
        if (!rd__fog_path_supported(p)) {
            result[p] = ST_UNSUPPORTED;
            continue;
        }
        rd__set_fog_path(p);
        if (!stRun(&f, t, &err[p], &worst[p])) {
            result[p] = ST_ERROR;
        } else {
            result[p] = err[p] <= ST_TOLERANCE ? ST_OK : ST_FAIL;
        }
        found = found || result[p] == ST_OK;
    }
    s_st.running = false;
    rd__frame_free(&f);
    if (s_stRead.tex.id) {
        rhi_destroy_texture(s_stRead.tex);
    }
    s_stRead.tex = (RhiTexture){0};
    /* the next replay starts SCENE from its clear, not from the grid */
    t->clearPending = 1;

    int chosen = -1;
    for (int i = 0; i < RD_FOG_PATH_COUNT && chosen < 0; i++) {
        if (result[order[i]] == ST_OK) {
            chosen = order[i];
        }
    }
    const bool passed = chosen >= 0;
    if (s_override >= 0 && rd__fog_path_supported(s_override)) {
        chosen = s_override;
    } else if (chosen < 0) {
        chosen = rd__fog_path_supported(RD_FOG_BUFFER) ? RD_FOG_BUFFER : before;
    }
    rd__set_fog_path(chosen);
    char res[160];
    size_t n = 0;
    res[0] = '\0';
    for (int i = 0; i < RD_FOG_PATH_COUNT; i++) {
        const int p = order[i];
        const char *sep = i ? ", " : "";
        switch (result[p]) {
        case ST_OK:
            n += (size_t)snprintf(res + n, sizeof(res) - n, "%s%s ok %d", sep, rd__fog_path_name(p),
                                  err[p]);
            break;
        case ST_FAIL:
            n += (size_t)snprintf(res + n, sizeof(res) - n, "%s%s fail %d", sep,
                                  rd__fog_path_name(p), err[p]);
            break;
        case ST_ERROR:
            n +=
                (size_t)snprintf(res + n, sizeof(res) - n, "%s%s error", sep, rd__fog_path_name(p));
            break;
        case ST_NOT_TRIED:
            n += (size_t)snprintf(res + n, sizeof(res) - n, "%s%s not tried", sep,
                                  rd__fog_path_name(p));
            break;
        default:
            n += (size_t)snprintf(res + n, sizeof(res) - n, "%s%s unsupported", sep,
                                  rd__fog_path_name(p));
            break;
        }
    }
    rd__log("fog: depth path %s (self-test: %s)%s%s on SCENE %ux%u at %ux%u texels",
            rd__fog_path_name(chosen), res, s_override >= 0 ? "; kept by " : "",
            s_override >= 0 ? s_overrideWhy : "", t->w, t->h, t->tw, t->th);
    if (!passed) {
        rd__log("fog: WARNING: no way of reading the depth passed the self-test on this device; "
                "the fog may cover the whole picture (%s kept)",
                rd__fog_path_name(chosen));
    }
    for (int i = 0; i < RD_FOG_PATH_COUNT; i++) {
        const int p = order[i];
        if (result[p] == ST_FAIL) {
            uint32_t xs[ST_SAMPLES], ys[ST_SAMPLES];
            static int cell[ST_SAMPLES][ST_CANDIDATES];
            stSamples(t, xs, ys, cell);
            rd__log("fog: self-test: %s differs most (%d) at texel %u,%u (index %d)",
                    rd__fog_path_name(p), err[p], xs[worst[p]], ys[worst[p]], cell[worst[p]][0]);
        }
    }
    return result[chosen] == ST_OK;
}

bool rd_fog_selftest(void)
{
    if (!g_rd.inited || !g_rd.hasDevice) {
        return false;
    }
    s_st.armed = true;
    return runSelftest();
}

void rd__fog_scene_remade(void)
{
    if (!g_rd.hasDevice) {
        return;
    }
    const RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (!t || (t->w == s_st.w && t->h == s_st.h && t->tw == s_st.tw && t->th == s_st.th)) {
        return;
    }
    s_st.w = t->w;
    s_st.h = t->h;
    s_st.tw = t->tw;
    s_st.th = t->th;
    s_probe.armed = true;
    if (s_st.armed) {
        (void)runSelftest();
    }
}

void rd__fog_path_shutdown(void)
{
    rd__fog_buffer_free();
    probeFree();
    if (s_stRead.tex.id) {
        rhi_destroy_texture(s_stRead.tex);
    }
    s_stRead.tex = (RhiTexture){0};
    s_probe.pending = false;
    s_st.armed = false;
}
