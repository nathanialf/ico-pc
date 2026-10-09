/* rd_video.c: the FMV picture on the output (port/fmv/rd_video.h).
 *
 * The PS2 movie player did not draw through the game's display: it set up
 * its own 720-wide interlaced display environment and sent each picture
 * from the IPU straight into it (ito/mpeg/mv_disp.c).  The port's DISPLAY
 * target is the game's reduced 512 x H/2 frame, which would halve the
 * picture's resolution, so the FMV path goes where the PS2's did, to the
 * output: one pass draws the picture into the presenter's 4:3 box
 * (pillarboxed in a wider window, letterboxed in a taller one, the rest
 * black), converting the decoder's 4:2:0 planes with the IPU's CSC in
 * yuv.hlsl, then presents.  DISPLAY and the game's frames are not touched.
 *
 * Layout: the box is the PS2 display area (rd_video_set_display, 720 x 576 or
 * 480); a w x h picture sits in it at ((dispW - w) / 2, (dispH - h) / 2),
 * the offsets mv_videodec.c's dispSetTags used, scaled with the area.  A
 * picture wider or taller than the area is fitted into the box on that side
 * (drawPicture), never cut by it.
 *
 * Under the CRT filter the films go through it as the game's
 * frames do: the picture is drawn into a target of the PS2 display area
 * (dispW x dispH, the clear colour around the picture, mirrored as shown),
 * and rd__crt_record_film draws that through the tube into the same box,
 * on the film's grid (rd__crt_film_grid: the game's 512 triads, the film's
 * field lines).  The clear frames (rd_video_clear) take the same road, so
 * the tube shows them as it shows the film.
 *
 * A field of an interlaced film (rd_video_field) takes one more pass in
 * front: the previous, current and next pictures go into the plane texture
 * one under the other, yuv_field_ps deinterlaces and converts the field
 * into an RGBA8 target of the picture's size, and the box pass scales
 * that target instead of the planes (yuv.hlsl's header).  Pictures given
 * to rd_video_frame take the single pass as before.
 *
 * GPU objects: its own two shaders and pipeline (one per output format)
 * over rd's bind group layouts (group 0 FrameCB, group 1 DrawCB, group 2
 * t1/s1/t2), one R8 plane texture, and per frame in flight an upload buffer
 * for the planes and the two uniform blocks.  Each call waits for its frame
 * slot (rhi_wait_frame), records, submits and presents on its own, as
 * rd_end_frame does for a game frame; it runs on the simulation thread
 * between game frames (the scheduler draws none while a movie plays).
 */
#include <string.h>
#include "../fmv/rd_video.h"
#include "rd_internal.h"
#include "shader_consts.h"
#include "shaders_gen.h"

static struct {
    bool ready;
    RhiShader vs, fs;
    RhiPipeline pipe[2]; /* by output: [0] swapchain format, [1] RGBA8 */
    RhiFormat pipeFmt[2];
    RhiTexture planes;
    RhiState planesState;
    uint32_t planesW, planesH;
    RhiBuffer upload[RHI_FRAMES_IN_FLIGHT];
    uint8_t *uploadMap[RHI_FRAMES_IN_FLIGHT];
    uint64_t uploadCap[RHI_FRAMES_IN_FLIGHT];
    uint32_t dispW, dispH;
    float clear[4];
    RhiTexture area; /* the display area under the CRT filter */
    RhiState areaState;
    uint32_t areaW, areaH;
    RhiShader fieldFs; /* a field: yuv_field_ps into fieldTex (w x h RGBA8) */
    RhiPipeline fieldPipe;
    RhiTexture fieldTex;
    RhiState fieldState;
    uint32_t fieldW, fieldH;
} s_v = {.dispW = 720, .dispH = 576, .clear = {0.0f, 0.0f, 0.0f, 1.0f}};

void rd_video_set_display(uint32_t dispW, uint32_t dispH)
{
    s_v.dispW = dispW ? dispW : 720;
    s_v.dispH = dispH ? dispH : 576;
}

static bool ensureInit(void)
{
    if (!g_rd.inited || !g_rd.hasDevice) {
        return false;
    }
    if (!s_v.ready) {
        s_v.vs = rd__make_shader("yuv_vs");
        s_v.fs = rd__make_shader("yuv_ps");
        s_v.ready = s_v.vs.id && s_v.fs.id;
    }
    return s_v.ready;
}

static RhiPipeline pipelineFor(RhiFormat fmt)
{
    for (int i = 0; i < 2; i++) {
        if (s_v.pipe[i].id && s_v.pipeFmt[i] == fmt) {
            return s_v.pipe[i];
        }
    }
    int slot = s_v.pipe[0].id ? 1 : 0;
    if (s_v.pipe[slot].id) {
        rhi_destroy_pipeline(s_v.pipe[slot]);
    }
    RhiBindGroupLayout layouts[3] = {g_rd.layoutFrame, g_rd.layoutDraw, g_rd.layoutTex};
    RhiPipelineDesc d;
    memset(&d, 0, sizeof(d));
    d.vertex = s_v.vs;
    d.fragment = s_v.fs;
    d.layouts = layouts;
    d.layoutCount = 3;
    d.topology = RHI_TOPO_TRIANGLE_LIST;
    d.cullNone = true;
    d.blend[0].writeMask = 0xF;
    d.colorFormats[0] = fmt;
    d.colorCount = 1;
    d.depthFormat = RHI_FMT_UNKNOWN;
    d.debugName = "rd video";
    s_v.pipe[slot] = rhi_create_pipeline(&d);
    s_v.pipeFmt[slot] = fmt;
    return s_v.pipe[slot];
}

static bool ensureUpload(int slot, uint64_t need)
{
    if (s_v.uploadCap[slot] >= need) {
        return true;
    }
    if (s_v.upload[slot].id) {
        rhi_destroy_buffer(s_v.upload[slot]);
    }
    uint64_t cap = 1u << 20;
    while (cap < need) {
        cap *= 2;
    }
    s_v.upload[slot] = rhi_create_buffer(&(RhiBufferDesc){cap, RHI_BUF_UNIFORM | RHI_BUF_COPY_SRC,
                                                          RHI_MEM_UPLOAD, "rd video upload"});
    s_v.uploadMap[slot] = s_v.upload[slot].id ? rhi_map_buffer(s_v.upload[slot]) : NULL;
    s_v.uploadCap[slot] = s_v.uploadMap[slot] ? cap : 0;
    return s_v.uploadMap[slot] != NULL;
}

static RhiBindGroup uniformGroup(RhiBindGroupLayout layout, uint32_t bindSlot, RhiBuffer buf,
                                 uint64_t off, uint32_t size)
{
    RhiBinding b;
    memset(&b, 0, sizeof(b));
    b.slot = bindSlot;
    /* rd's frame and draw layouts take a dynamic uniform; the
     * movie's two blocks are the group's base, bound with offset 0 (one
     * draw a frame: no group to share) */
    b.type = RHI_BIND_UNIFORM_BUFFER_DYNAMIC;
    b.buffer = buf;
    b.offset = off;
    b.size = size;
    return rhi_create_bind_group(&(RhiBindGroupDesc){layout, &b, 1});
}

/* The movie's box: the presenter's 4:3 box (rd_present.c rd__present_box).
 * Whatever the aspect option, the 4:3 movie is pillarboxed
 * in the output (in a 16:9 Enhanced presentation the scene fills the window
 * and the movie keeps 4:3); the full-height and resolution options do not
 * apply (the picture goes straight to the output). */
static void box43(uint32_t outW, uint32_t outH, RhiRect *box)
{
    rd__present_box(outW, outH, 4.0f / 3.0f, box);
}

/* The output: the swapchain's next image, or rd's headless present target
 * (tests).  On success *tex, *state and the size are set. */
typedef struct VideoOut {
    RhiTexture tex;
    RhiState *state;
    RhiState localState;
    RhiFormat fmt;
    uint32_t w, h;
    bool window;
} VideoOut;

static bool acquireOut(VideoOut *o)
{
    memset(o, 0, sizeof(*o));
    o->window = rhi_swapchain_format() != RHI_FMT_UNKNOWN;
    if (o->window) {
        o->tex = rhi_acquire_backbuffer();
        if (!o->tex.id) {
            rhi_resize_swapchain(g_rd.settings.outputWidth, g_rd.settings.outputHeight,
                                 g_rd.settings.vsync != 0);
            o->tex = rhi_acquire_backbuffer();
        }
        if (o->tex.id) {
            rd__output_follow_swapchain(); /* the image's own size */
        }
        o->localState = RHI_STATE_UNDEFINED;
        o->state = &o->localState;
        o->fmt = rhi_swapchain_format();
        o->w = g_rd.settings.outputWidth;
        o->h = g_rd.settings.outputHeight;
        return o->tex.id && o->w && o->h;
    }
    if (!rd__present_acquire()) {
        return false;
    }
    o->tex = g_rd.presentOut;
    o->state = &g_rd.presentOutState;
    o->fmt = RHI_FMT_RGBA8_UNORM;
    o->w = g_rd.presentOutW;
    o->h = g_rd.presentOutH;
    return o->tex.id != 0;
}

/* The display area's target (dispW x dispH, RGBA8) */
static bool ensureArea(void)
{
    if (s_v.area.id && s_v.areaW == s_v.dispW && s_v.areaH == s_v.dispH) {
        return true;
    }
    if (s_v.area.id) {
        rhi_destroy_texture(s_v.area);
    }
    s_v.area = rhi_create_texture(&(RhiTextureDesc){s_v.dispW, s_v.dispH, 1, RHI_FMT_RGBA8_UNORM,
                                                    RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED,
                                                    "rd video area"});
    s_v.areaState = RHI_STATE_UNDEFINED;
    s_v.areaW = s_v.dispW;
    s_v.areaH = s_v.dispH;
    return s_v.area.id != 0;
}

/* One pass on dst (dw x dh, RENDER_TARGET): cleared to the clear colour,
 * the picture (when there is one) drawn into box as the PS2 placed it in
 * its display area, the rectangle mirrored with the mirror mode. */
static void drawPicture(RhiCommandList cl, RhiTexture dst, RhiFormat fmt, uint32_t dw, uint32_t dh,
                        const RhiRect *box, bool picture, uint32_t w, uint32_t h, bool mirror,
                        RhiTexture src, RhiBuffer buf, uint64_t cbOff, uint64_t dcOff)
{
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = dst;
    p.color[0].load = RHI_LOAD_CLEAR;
    memcpy(p.color[0].clear, s_v.clear, sizeof(s_v.clear));
    p.colorCount = 1;
    p.width = dw;
    p.height = dh;
    rhi_cmd_begin_render_pass(cl, &p);
    if (picture) {
        /* the picture's rectangle in the box, from its place in the PS2
           display area (integer offsets as mv_videodec.c computed them).
           A picture larger than the area (a 576-line film in a 480-line
           area) is fitted: the area grows to the picture on that side, so
           the whole picture fills the box instead of a 1.2x picture whose
           top and bottom the box cuts. */
        const uint32_t aw = w > s_v.dispW ? w : s_v.dispW;
        const uint32_t ah = h > s_v.dispH ? h : s_v.dispH;
        const int32_t ox = ((int32_t)aw - (int32_t)w) >> 1;
        const int32_t oy = ((int32_t)ah - (int32_t)h) >> 1;
        const float sx = (float)box->w / (float)aw, sy = (float)box->h / (float)ah;
        RhiViewport vp = {(float)box->x + (float)ox * sx,
                          (float)box->y + (float)oy * sy,
                          (float)w * sx,
                          (float)h * sy,
                          0.0f,
                          1.0f};
        if (mirror) {
            vp.x =
                (float)box->x + (float)box->w - (float)ox * sx - vp.w; /* the rectangle mirrored */
        }
        RhiRect sc = *box;
        rhi_cmd_set_viewport(cl, &vp);
        rhi_cmd_set_scissor(cl, &sc);
        RhiPipeline pipe = pipelineFor(fmt);
        if (pipe.id) {
            RhiBinding tb[3];
            memset(tb, 0, sizeof(tb));
            tb[0].slot = 1;
            tb[0].type = RHI_BIND_SAMPLED_TEXTURE;
            tb[0].texture = src;
            tb[1].slot = 1;
            tb[1].type = RHI_BIND_SAMPLER;
            tb[1].sampler =
                rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
            tb[2].slot = 2;
            tb[2].type = RHI_BIND_SAMPLED_TEXTURE;
            tb[2].texture = g_rd.dummy;
            rhi_cmd_set_pipeline(cl, pipe);
            rhi_cmd_set_bind_group(
                cl, 0, uniformGroup(g_rd.layoutFrame, 0, buf, cbOff, (uint32_t)sizeof(IcoFrameCB)));
            rhi_cmd_set_bind_group(
                cl, 1, uniformGroup(g_rd.layoutDraw, 1, buf, dcOff, (uint32_t)sizeof(IcoDrawCB)));
            rhi_cmd_set_bind_group(
                cl, 2, rhi_create_bind_group(&(RhiBindGroupDesc){g_rd.layoutTex, tb, 3}));
            rhi_cmd_draw(cl, 3, 0, 1);
        }
    }
    rhi_cmd_end_render_pass(cl);
}

/* The field pass's shader, pipeline and w x h target. */
static bool ensureField(uint32_t w, uint32_t h)
{
    if (!s_v.fieldFs.id) {
        s_v.fieldFs = rd__make_shader("yuv_field_ps");
        if (!s_v.fieldFs.id) {
            return false;
        }
    }
    if (!s_v.fieldPipe.id) {
        RhiBindGroupLayout layouts[3] = {g_rd.layoutFrame, g_rd.layoutDraw, g_rd.layoutTex};
        RhiPipelineDesc d;
        memset(&d, 0, sizeof(d));
        d.vertex = s_v.vs;
        d.fragment = s_v.fieldFs;
        d.layouts = layouts;
        d.layoutCount = 3;
        d.topology = RHI_TOPO_TRIANGLE_LIST;
        d.cullNone = true;
        d.blend[0].writeMask = 0xF;
        d.colorFormats[0] = RHI_FMT_RGBA8_UNORM;
        d.colorCount = 1;
        d.depthFormat = RHI_FMT_UNKNOWN;
        d.debugName = "rd video field";
        s_v.fieldPipe = rhi_create_pipeline(&d);
        if (!s_v.fieldPipe.id) {
            return false;
        }
    }
    if (!s_v.fieldTex.id || s_v.fieldW != w || s_v.fieldH != h) {
        if (s_v.fieldTex.id) {
            rhi_destroy_texture(s_v.fieldTex);
        }
        s_v.fieldTex = rhi_create_texture(&(RhiTextureDesc){w, h, 1, RHI_FMT_RGBA8_UNORM,
                                                            RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED,
                                                            "rd video field"});
        s_v.fieldState = RHI_STATE_UNDEFINED;
        s_v.fieldW = w;
        s_v.fieldH = h;
    }
    return s_v.fieldTex.id != 0;
}

/* The field pass: the planes (three pictures) deinterlaced and converted
 * 1:1 into fieldTex, with the DrawCB at dcOff. */
static void drawField(RhiCommandList cl, uint32_t w, uint32_t h, RhiBuffer buf, uint64_t cbOff,
                      uint64_t dcOff)
{
    rd__transition(cl, s_v.fieldTex, &s_v.fieldState, RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = s_v.fieldTex;
    p.color[0].load = RHI_LOAD_CLEAR;
    p.colorCount = 1;
    p.width = w;
    p.height = h;
    rhi_cmd_begin_render_pass(cl, &p);
    RhiViewport vp = {0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f};
    rhi_cmd_set_viewport(cl, &vp);
    rhi_cmd_set_scissor(cl, &(RhiRect){0, 0, w, h});
    RhiBinding tb[3];
    memset(tb, 0, sizeof(tb));
    tb[0].slot = 1;
    tb[0].type = RHI_BIND_SAMPLED_TEXTURE;
    tb[0].texture = s_v.planes;
    tb[1].slot = 1;
    tb[1].type = RHI_BIND_SAMPLER;
    tb[1].sampler = rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    tb[2].slot = 2;
    tb[2].type = RHI_BIND_SAMPLED_TEXTURE;
    tb[2].texture = g_rd.dummy;
    rhi_cmd_set_pipeline(cl, s_v.fieldPipe);
    rhi_cmd_set_bind_group(
        cl, 0, uniformGroup(g_rd.layoutFrame, 0, buf, cbOff, (uint32_t)sizeof(IcoFrameCB)));
    rhi_cmd_set_bind_group(
        cl, 1, uniformGroup(g_rd.layoutDraw, 1, buf, dcOff, (uint32_t)sizeof(IcoDrawCB)));
    rhi_cmd_set_bind_group(cl, 2,
                           rhi_create_bind_group(&(RhiBindGroupDesc){g_rd.layoutTex, tb, 3}));
    rhi_cmd_draw(cl, 3, 0, 1);
    rhi_cmd_end_render_pass(cl);
    rd__transition(cl, s_v.fieldTex, &s_v.fieldState, RHI_STATE_SHADER_READ);
}

/* yuv.hlsl g_mode.w */
#define YUV_FIELD 1u        /* three pictures, a field of the middle one */
#define YUV_FIELD_ODD 2u    /* the field's rows are the odd ones */
#define YUV_FIELD_SECOND 4u /* the picture's second field in time */
#define YUV_CONVERTED 8u    /* yuv_ps: t1 is yuv_field_ps's target */
#define YUV_FILL_SHIFT 4u   /* RdVideoFill */

/* One present: the picture (pics != NULL: one picture, or with field
 * flags (yuv.hlsl g_mode.w, YUV_FIELD set) the previous, current and next)
 * or the clear colour alone. */
static int presentVideo(const RdVideoPicture *pics, uint32_t w, uint32_t h, uint32_t fieldFlags,
                        const uint8_t rgba[4])
{
    if (!ensureInit()) {
        return -1;
    }
    const bool isField = pics != NULL && (fieldFlags & YUV_FIELD) != 0;
    const uint32_t npics = isField ? 3u : 1u;
    /* the ring for the CRT pass's uniforms (and the slot kept in step with
       the replays' either way) */
    if (!rd__begin_own_frame(64u * 1024u)) {
        return -1;
    }
    const int slot = (int)rhi_frame_slot();
    const RhiLimits *lim = rhi_limits();
    const uint32_t cw = (w + 1) / 2, ch = (h + 1) / 2;
    const uint32_t tw = 2 * cw, th = h + ch, tall = th * npics;
    const uint32_t pitchA = lim->copyRowPitchAlign ? lim->copyRowPitchAlign : 1;
    const uint32_t rowPitch = (tw + pitchA - 1) / pitchA * pitchA;
    const uint64_t ua = lim->uniformAlign ? lim->uniformAlign : 256;
    const uint64_t offA = lim->copyOffsetAlign ? lim->copyOffsetAlign : 4;
    const uint64_t cbOff = 0;
    const uint64_t dcOff = (sizeof(IcoFrameCB) + ua - 1) / ua * ua;
    /* the field pass's DrawCB after the box pass's */
    const uint64_t dcFieldOff = (dcOff + sizeof(IcoDrawCB) + ua - 1) / ua * ua;
    const uint64_t texOff =
        ((dcFieldOff + sizeof(IcoDrawCB) + ua - 1) / ua * ua + offA - 1) / offA * offA;
    const bool picture = pics != NULL && pics[0].y != NULL && w > 0 && h > 0;

    if (isField && !ensureField(w, h)) {
        return -1;
    }
    if (!ensureUpload(slot, texOff + (picture ? (uint64_t)rowPitch * tall : 0))) {
        return -1;
    }
    uint8_t *map = s_v.uploadMap[slot];
    RhiBuffer buf = s_v.upload[slot];

    /* the planes into one R8 image: Y on top, Cb | Cr below (a field: the
       three pictures so, one under the other) */
    if (picture) {
        if (!s_v.planes.id || s_v.planesW != tw || s_v.planesH != tall) {
            if (s_v.planes.id) {
                rhi_destroy_texture(s_v.planes);
            }
            s_v.planes = rhi_create_texture(&(RhiTextureDesc){tw, tall, 1, RHI_FMT_R8_UNORM,
                                                              RHI_TEX_SAMPLED | RHI_TEX_COPY_DST,
                                                              "rd video planes"});
            s_v.planesState = RHI_STATE_UNDEFINED;
            s_v.planesW = tw;
            s_v.planesH = tall;
            if (!s_v.planes.id) {
                return -1;
            }
        }
        for (uint32_t k = 0; k < npics; k++) {
            const RdVideoPicture *pk = &pics[k];
            uint8_t *dst = map + texOff + (size_t)k * th * rowPitch;
            for (uint32_t r = 0; r < h; r++) {
                memcpy(dst + (size_t)r * rowPitch, pk->y + (size_t)r * pk->pitch[0], w);
                if (tw > w) {
                    dst[(size_t)r * rowPitch + w] = dst[(size_t)r * rowPitch + w - 1];
                }
            }
            for (uint32_t r = 0; r < ch; r++) {
                uint8_t *row = dst + (size_t)(h + r) * rowPitch;
                memcpy(row, pk->u + (size_t)r * pk->pitch[1], cw);
                memcpy(row + cw, pk->v + (size_t)r * pk->pitch[2], cw);
            }
        }
    }
    /* the output last: a backbuffer acquired is presented below (a failure
     * above would have left a swapchain image acquired and never presented) */
    VideoOut out;
    if (!acquireOut(&out)) {
        return -1;
    }

    /* group 0: a FrameCB (unused by yuv.hlsl, bound for the layout) */
    IcoFrameCB fcb;
    memset(&fcb, 0, sizeof(fcb));
    fcb.target[0] = (float)out.w;
    fcb.target[1] = (float)out.h;
    memcpy(map + cbOff, &fcb, sizeof(fcb));
    IcoDrawCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.tex[0] = (float)w;
    dcb.tex[1] = (float)h;
    dcb.mode[0] = cw;
    dcb.mode[1] = h;
    /* the films follow the mirror mode (rd_set_mirror or
       RdSettings.mirror) */
    dcb.param[0] = rd__mirror_on() ? 1.0f : 0.0f;
    if (isField) {
        /* the field pass converts at the picture's size; the box pass
           scales its target */
        IcoDrawCB fdc = dcb;
        fdc.mode[2] = th;
        fdc.mode[3] = fieldFlags;
        fdc.param[0] = 0.0f;
        memcpy(map + dcFieldOff, &fdc, sizeof(fdc));
        dcb.mode[3] = YUV_CONVERTED;
    }
    memcpy(map + dcOff, &dcb, sizeof(dcb));

    RhiCommandList cl = rhi_begin_commands();
    if (!cl.id) {
        return -1;
    }
    if (picture) {
        rd__transition(cl, s_v.planes, &s_v.planesState, RHI_STATE_COPY_DST);
        rhi_cmd_copy_buffer_to_texture(cl, buf, texOff, rowPitch, s_v.planes, 0,
                                       (RhiRect){0, 0, tw, tall});
        rd__transition(cl, s_v.planes, &s_v.planesState, RHI_STATE_SHADER_READ);
        if (isField) {
            drawField(cl, w, h, buf, cbOff, dcFieldOff);
        }
    }
    const RhiTexture src = isField ? s_v.fieldTex : s_v.planes;
    RhiRect box;
    box43(out.w, out.h, &box);
    rd__note_present_box(out.w, out.h, &box); /* for the tests */
    if (rgba != NULL) {
        /* dispClear's colour: what the PS2 showed around the picture (the
           whole screen there; the whole output here, bars included, or the
           whole display area under the CRT filter) */
        for (int i = 0; i < 3; i++) {
            s_v.clear[i] = (float)rgba[i] / 255.0f;
        }
    }
    const bool mirror = dcb.param[0] != 0.0f;
    bool filtered = false;
    if (rd__crt_on() && ensureArea()) {
        /* the display area 1:1 (the picture at its PS2 offsets), then
           the tube over it into the box */
        rd__transition(cl, s_v.area, &s_v.areaState, RHI_STATE_RENDER_TARGET);
        const RhiRect all = {0, 0, s_v.dispW, s_v.dispH};
        drawPicture(cl, s_v.area, RHI_FMT_RGBA8_UNORM, s_v.dispW, s_v.dispH, &all, picture, w, h,
                    mirror, src, buf, cbOff, dcOff);
        rd__transition(cl, s_v.area, &s_v.areaState, RHI_STATE_SHADER_READ);
        uint32_t vw, vh;
        rd__crt_film_grid(s_v.dispH, &vw, &vh);
        rd__transition(cl, out.tex, out.state, RHI_STATE_RENDER_TARGET);
        filtered = rd__crt_record_film(cl, s_v.area, s_v.dispW, s_v.dispH, vw, vh, out.tex, out.fmt,
                                       out.w, out.h, &box);
    }
    if (!filtered) {
        rd__transition(cl, out.tex, out.state, RHI_STATE_RENDER_TARGET);
        drawPicture(cl, out.tex, out.fmt, out.w, out.h, &box, picture, w, h, mirror, src, buf,
                    cbOff, dcOff);
    }
    if (out.window) {
        rd__transition(cl, out.tex, out.state, RHI_STATE_PRESENT);
    }
    rhi_end_commands(cl);
    rhi_submit(cl);
    if (out.window) {
        rhi_present();
    }
    g_rd.videoShown = 1; /* rd_present leaves the picture until a game frame closes */
    return 0;
}

/* presentVideo through rd__on_host: the driver work off the fiber's stack */
typedef struct VideoCall {
    const RdVideoPicture *pics;
    uint32_t w, h, fieldFlags;
    const uint8_t *rgba;
    int ret;
} VideoCall;

static void videoOnHost(void *arg)
{
    VideoCall *c = (VideoCall *)arg;
    c->ret = presentVideo(c->pics, c->w, c->h, c->fieldFlags, c->rgba);
}

static int presentVideoOnHost(const RdVideoPicture *pics, uint32_t w, uint32_t h,
                              uint32_t fieldFlags, const uint8_t rgba[4])
{
    VideoCall c = {pics, w, h, fieldFlags, rgba, -1};
    rd__on_host(videoOnHost, &c);
    return c.ret;
}

/* rd_video_presents: counted on the thread that calls rd_video_frame (the
   game's), read by the window's statistics between vsyncs on the same one */
static uint32_t s_presents;

static uint32_t s_presentFails;

int rd_video_frame(const uint8_t *y, const uint8_t *u, const uint8_t *v, const uint32_t pitch[3],
                   uint32_t w, uint32_t h)
{
    if (!y || !u || !v || !pitch || w == 0 || h == 0) {
        s_presentFails++;
        return -1;
    }
    const RdVideoPicture pic = {y, u, v, {pitch[0], pitch[1], pitch[2]}};
    const int r = presentVideoOnHost(&pic, w, h, 0, NULL);
    if (r == 0) {
        s_presents++;
    } else {
        s_presentFails++;
    }
    return r;
}

static bool pictureOk(const RdVideoPicture *p)
{
    return p != NULL && p->y != NULL && p->u != NULL && p->v != NULL;
}

int rd_video_field(const RdVideoPicture *prev, const RdVideoPicture *cur,
                   const RdVideoPicture *next, uint32_t w, uint32_t h, int field, int top_first,
                   RdVideoFill fill)
{
    if (!pictureOk(cur) || w == 0 || h == 0) {
        s_presentFails++;
        return -1;
    }
    const RdVideoPicture pics[3] = {pictureOk(prev) ? *prev : *cur, *cur,
                                    pictureOk(next) ? *next : *cur};
    /* the field's rows: the first field in time is the top one (even rows)
       when top_first */
    const bool odd = (field != 0) == (top_first != 0);
    const uint32_t flags = YUV_FIELD | (odd ? YUV_FIELD_ODD : 0u) |
                           (field != 0 ? YUV_FIELD_SECOND : 0u) |
                           ((uint32_t)fill & 3u) << YUV_FILL_SHIFT;
    const int r = presentVideoOnHost(pics, w, h, flags, NULL);
    if (r == 0) {
        s_presents++;
    } else {
        s_presentFails++;
    }
    return r;
}

uint32_t rd_video_presents(uint32_t *failed)
{
    if (failed != NULL) {
        *failed = s_presentFails;
    }
    return s_presents;
}

int rd_video_clear(const uint8_t rgba[4])
{
    return presentVideoOnHost(NULL, 0, 0, 0, rgba);
}

void rd_video_shutdown(void)
{
    if (!g_rd.hasDevice) {
        memset(&s_v.pipe, 0, sizeof(s_v.pipe));
        s_v.fieldPipe = (RhiPipeline){0};
        s_v.ready = false;
        return;
    }
    rhi_wait_idle();
    for (int i = 0; i < 2; i++) {
        if (s_v.pipe[i].id) {
            rhi_destroy_pipeline(s_v.pipe[i]);
        }
        s_v.pipe[i] = (RhiPipeline){0};
    }
    for (int i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        if (s_v.upload[i].id) {
            rhi_destroy_buffer(s_v.upload[i]);
        }
        s_v.upload[i] = (RhiBuffer){0};
        s_v.uploadMap[i] = NULL;
        s_v.uploadCap[i] = 0;
    }
    if (s_v.planes.id) {
        rhi_destroy_texture(s_v.planes);
    }
    s_v.planes = (RhiTexture){0};
    s_v.planesW = s_v.planesH = 0;
    if (s_v.area.id) {
        rhi_destroy_texture(s_v.area);
    }
    s_v.area = (RhiTexture){0};
    s_v.areaW = s_v.areaH = 0;
    if (s_v.fieldTex.id) {
        rhi_destroy_texture(s_v.fieldTex);
    }
    s_v.fieldTex = (RhiTexture){0};
    s_v.fieldW = s_v.fieldH = 0;
    if (s_v.fieldPipe.id) {
        rhi_destroy_pipeline(s_v.fieldPipe);
    }
    s_v.fieldPipe = (RhiPipeline){0};
    if (s_v.fieldFs.id) {
        rhi_destroy_shader(s_v.fieldFs);
    }
    s_v.fieldFs = (RhiShader){0};
    if (s_v.vs.id) {
        rhi_destroy_shader(s_v.vs);
    }
    if (s_v.fs.id) {
        rhi_destroy_shader(s_v.fs);
    }
    s_v.vs = s_v.fs = (RhiShader){0};
    s_v.ready = false;
}
