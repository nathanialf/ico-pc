/*
 * port/render/rd_icon_hold.c
 *
 * The title's last picture held on screen while the icon capture's frames
 * are drawn: rd_icon_hold.h, rd.h rd_icon_hold.
 */
#include <string.h>
#include "rd_internal.h"
#include "rd_icon_hold.h"

/* the game thread's words (atomics): the hold asked for, its session (one a
 * rd_icon_hold(true) from off), the pass's frames, and the session whose
 * copy was taken (rd_icon_hold_ready) */
static uint32_t s_req, s_gen, s_first = RD_ICON_HOLD_NONE, s_last = RD_ICON_HOLD_NONE;
static uint32_t s_heldGen;

enum { HOLD_OFF = 0, HOLD_ARMED, HOLD_SHOWING, HOLD_DONE };

/* the presenting thread's */
static struct {
    uint32_t gen; /* the session this state is for */
    int state;
    RhiTexture tex;
    RhiState texState;
    RhiFormat fmt;
    uint32_t w, h;
    bool held;          /* tex holds a picture of this session */
    uint32_t copies;    /* armed: presents copied */
    uint32_t heldFrame; /* the frame of the last copy */
    uint32_t shown;     /* showing: presents drawn from the copy */
    uint32_t black;     /* showing: presents cleared to black (no copy) */
    uint32_t from, to;  /* showing: the first and the last frame covered */
} s_h;

void rd_icon_hold(bool on)
{
    if (on) {
        if (__atomic_load_n(&s_req, __ATOMIC_ACQUIRE)) {
            return; /* the session goes on */
        }
        /* a new session: no pass frames yet */
        __atomic_store_n(&s_first, RD_ICON_HOLD_NONE, __ATOMIC_RELAXED);
        __atomic_store_n(&s_last, RD_ICON_HOLD_NONE, __ATOMIC_RELAXED);
        __atomic_add_fetch(&s_gen, 1u, __ATOMIC_RELEASE);
        __atomic_store_n(&s_req, 1u, __ATOMIC_RELEASE);
        return;
    }
    __atomic_store_n(&s_req, 0u, __ATOMIC_RELEASE);
}

bool rd_icon_hold_ready(void)
{
    return __atomic_load_n(&s_req, __ATOMIC_ACQUIRE) &&
           __atomic_load_n(&s_heldGen, __ATOMIC_ACQUIRE) ==
               __atomic_load_n(&s_gen, __ATOMIC_ACQUIRE);
}

void rd__icon_hold_pass(bool on, uint32_t frame)
{
    if (on) {
        /* last first, so a present never reads a first with an old last */
        __atomic_store_n(&s_last, RD_ICON_HOLD_NONE, __ATOMIC_RELEASE);
        if (__atomic_load_n(&s_first, __ATOMIC_ACQUIRE) == RD_ICON_HOLD_NONE) {
            __atomic_store_n(&s_first, frame, __ATOMIC_RELEASE);
        }
        return;
    }
    if (__atomic_load_n(&s_first, __ATOMIC_ACQUIRE) != RD_ICON_HOLD_NONE) {
        __atomic_store_n(&s_last, frame, __ATOMIC_RELEASE);
    }
}

bool rd__icon_hold_tainted(uint32_t frame, float alpha, uint32_t first, uint32_t last)
{
    if (first == RD_ICON_HOLD_NONE || frame < first) {
        return false; /* before the pass's first frame (a blend's older one is older still) */
    }
    if (last == RD_ICON_HOLD_NONE || frame <= last) {
        return true; /* the pass on, or one of its frames */
    }
    return !(alpha >= 1.0f || frame - 1u > last); /* a blend with the pass's last frame */
}

static void texFree(void)
{
    if (s_h.tex.id) {
        rhi_destroy_texture(s_h.tex);
    }
    s_h.tex = (RhiTexture){0};
    s_h.held = false;
}

/* the line at the end of a showing session: back (a present past the
 * pass's frames, frame) or released (rd_icon_hold(false) first) */
static void resumeLine(bool back, uint32_t frame)
{
    rd__log("icon: the title held on screen for %u presents while the capture ran (its frames "
            "%u..%u drawn off screen, the held picture frame %u's, copied %u times): %s %u",
            s_h.shown, s_h.from, s_h.to, s_h.heldFrame, s_h.copies,
            back ? "the live picture is back at frame" : "the hold released at frame", frame);
    if (s_h.black) {
        rd__log("icon: %u presents of the capture's frames were black: no present came between "
                "the hold's start and the pass (the window minimised or skipping presents)",
                s_h.black);
    }
}

/* the output's pass ended for a copy or a draw */
static void passEnd(RhiCommandList cl, bool open)
{
    if (open) {
        rhi_cmd_end_render_pass(cl);
    }
}

/* and opened again, loading, for the overlay */
static void passAgain(RhiCommandList cl, RhiTexture out, uint32_t w, uint32_t h, bool open)
{
    if (!open) {
        return;
    }
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = out;
    p.color[0].load = RHI_LOAD_LOAD;
    p.colorCount = 1;
    p.width = w;
    p.height = h;
    rhi_cmd_begin_render_pass(cl, &p);
}

/* armed: the output into the hold texture */
static void copyIn(RhiCommandList cl, RhiTexture out, RhiState *outState, RhiFormat fmt, uint32_t w,
                   uint32_t h, bool open, uint32_t frame)
{
    if (!s_h.tex.id || s_h.w != w || s_h.h != h || s_h.fmt != fmt) {
        texFree();
        s_h.tex = rhi_create_texture(
            &(RhiTextureDesc){w, h, 1, fmt, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "rd icon hold", 0});
        s_h.texState = RHI_STATE_UNDEFINED;
        s_h.w = w;
        s_h.h = h;
        s_h.fmt = fmt;
        if (!s_h.tex.id) {
            static unsigned n;
            if (n++ < 8) {
                rd__log("icon: no %ux%u texture for the held picture: the capture's frames "
                        "show black",
                        w, h);
            }
            return;
        }
    }
    passEnd(cl, open);
    rd__transition(cl, out, outState, RHI_STATE_COPY_SRC);
    rd__transition(cl, s_h.tex, &s_h.texState, RHI_STATE_COPY_DST);
    rhi_cmd_copy_texture(cl, out, (RhiRect){0, 0, w, h}, s_h.tex, 0, 0);
    rd__transition(cl, out, outState, RHI_STATE_RENDER_TARGET);
    passAgain(cl, out, w, h, open);
    s_h.held = true;
    s_h.copies++;
    s_h.heldFrame = frame;
    __atomic_store_n(&s_heldGen, s_h.gen, __ATOMIC_RELEASE);
}

/* showing: the held copy over the whole output, or black without one */
static void drawHeld(RhiCommandList cl, RhiTexture out, RhiState *outState, RhiFormat fmt,
                     uint32_t w, uint32_t h, bool open)
{
    passEnd(cl, open);
    rd__transition(cl, out, outState, RHI_STATE_RENDER_TARGET);
    const RhiRect full = {0, 0, w, h};
    if (s_h.held && s_h.tex.id) {
        /* one texel a pixel, nearest: the bytes copied (blit_ps's modulate
           by 0x80 is the identity); a window resized meanwhile scales it */
        const RdFilter f = s_h.w == w && s_h.h == h ? RD_FILTER_NEAREST : RD_FILTER_LINEAR;
        rd__transition(cl, s_h.tex, &s_h.texState, RHI_STATE_SHADER_READ);
        rd__present_blit(cl, s_h.tex, s_h.w, s_h.h, out, fmt, w, h, RHI_LOAD_CLEAR, &full, f, 0,
                         0.0f);
        s_h.shown++;
    } else {
        RhiRenderPassDesc p;
        memset(&p, 0, sizeof(p));
        p.color[0].texture = out;
        p.color[0].load = RHI_LOAD_CLEAR; /* black */
        p.colorCount = 1;
        p.width = w;
        p.height = h;
        rhi_cmd_begin_render_pass(cl, &p);
        rhi_cmd_end_render_pass(cl);
        s_h.black++;
    }
    passAgain(cl, out, w, h, open);
}

void rd__icon_hold_record(RhiCommandList cl, RhiTexture out, RhiState *outState, RhiFormat fmt,
                          uint32_t w, uint32_t h, bool open)
{
    if (!__atomic_load_n(&s_req, __ATOMIC_ACQUIRE)) {
        if (s_h.state != HOLD_OFF) {
            if (s_h.state == HOLD_SHOWING) {
                RdPresentInfo pi;
                resumeLine(false, rd_last_present_info(&pi) ? pi.frame : 0);
            }
            texFree();
            s_h.state = HOLD_OFF;
        }
        return;
    }
    const uint32_t gen = __atomic_load_n(&s_gen, __ATOMIC_ACQUIRE);
    if (gen != s_h.gen || s_h.state == HOLD_OFF) {
        texFree();
        s_h.gen = gen;
        s_h.state = HOLD_ARMED;
        s_h.copies = s_h.heldFrame = s_h.shown = s_h.black = s_h.from = s_h.to = 0;
    }
    if (s_h.state == HOLD_DONE || !out.id || !w || !h) {
        return;
    }
    RdPresentInfo pi;
    uint32_t frame = 0;
    float alpha = 1.0f;
    if (rd_last_present_info(&pi)) {
        frame = pi.frame;
        alpha = pi.alpha;
    }
    const uint32_t first = __atomic_load_n(&s_first, __ATOMIC_ACQUIRE);
    const uint32_t last = __atomic_load_n(&s_last, __ATOMIC_ACQUIRE);
    if (!rd__icon_hold_tainted(frame, alpha, first, last)) {
        if (s_h.state == HOLD_ARMED) {
            copyIn(cl, out, outState, fmt, w, h, open, frame);
        } else {
            resumeLine(true, frame);
            texFree();
            s_h.state = HOLD_DONE;
        }
        return;
    }
    if (s_h.state == HOLD_ARMED) {
        s_h.state = HOLD_SHOWING;
        s_h.from = frame;
        rd__log("icon: the capture's frames are drawn off screen from frame %u: %s", frame,
                s_h.held ? "the title's last picture stays on screen" : "no picture held, black");
    }
    s_h.to = frame;
    drawHeld(cl, out, outState, fmt, w, h, open);
}

void rd__icon_hold_shutdown(void)
{
    texFree();
    s_h.state = HOLD_OFF;
    s_h.gen = 0;
    __atomic_store_n(&s_heldGen, 0u, __ATOMIC_RELEASE);
}
