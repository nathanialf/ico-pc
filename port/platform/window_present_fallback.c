/*
 * port/platform/window_present_fallback.c
 *
 * The window's side of the FIFO fallback (window_present_fallback.h).
 */
#include "window_present_fallback.h"
#include <stdio.h>
#include <stdlib.h>
#include "diag_host.h"
#include "present_fallback.h"
#include "rd.h"
#include "rhi.h"
#include "video_options.h"

static IcoPresentFallback s_fallback;
static IcoPresentFallbackMode s_mode;
static int s_modeRead;
static int s_pending; /* the switch asked for, applied at the step's end */

void window_fifo_set(void)
{
    if (!s_modeRead) {
        /* auto, the default, FIFO after the blocks; off keeps mailbox;
           fifo presents FIFO from the start (this runs before the first
           swapchain) */
        s_modeRead = 1;
        s_mode = ico_present_fallback_mode(getenv("ICO_PRESENT_FALLBACK"));
        if (s_mode == ICO_PRESENT_FALLBACK_FIFO) {
            fprintf(stderr, "window: presents FIFO from the start ([dev] present_fallback=fifo)\n");
        } else if (s_mode == ICO_PRESENT_FALLBACK_OFF) {
            fprintf(stderr, "window: the FIFO fallback is off ([dev] present_fallback=off): "
                            "presents that block under mailbox keep mailbox\n");
        }
    }
    rhi_present_fifo_fallback(s_fallback.taken || s_mode == ICO_PRESENT_FALLBACK_FIFO);
    /* auto on Android: Qualcomm's own driver makes its first swapchain
       FIFO at the fewest images when vsync is off (the rhi asks the
       driver's ids, so this works before the device exists); off keeps
       mailbox, fifo needs no question; elsewhere (Windows on Snapdragon)
       no question, so Vsync Off stays immediate there */
    rhi_present_fifo_start(ico_present_fallback_start_asked(s_mode, ICO_PRESENT_FALLBACK_ANDROID)
                               ? ico_present_fallback_qualcomm_own
                               : NULL);
}

/* Whether the swapchain in use is the one FIFO from the start made
   (present_fallback.h ico_present_fallback_start): after the device exists */
static int start_active(void)
{
    IcoVideoOptions o;
    const RhiLimits *l = rhi_limits();

    ico_video_get(&o);
    return rhi_backend() == RHI_BACKEND_VULKAN && l != NULL && !rhi_present_mailbox() &&
           !s_fallback.taken &&
           ico_present_fallback_start(s_mode, ICO_PRESENT_FALLBACK_ANDROID, o.vsync, l->vendorId,
                                      l->driverText);
}

/* What the window says about a present it notes (present_fallback.h):
   vsync off in the options, the swapchain in mailbox mode, Vulkan, a
   driver the switch is for (Qualcomm's own; others answer OTHER); the dev key off keeps mailbox; FIFO from the
   start needs no switch */
static int gate(void)
{
    IcoVideoOptions o;
    const RhiLimits *l = rhi_limits();

    ico_video_get(&o);
    if (o.vsync || !rhi_present_mailbox() || rhi_backend() != RHI_BACKEND_VULKAN || l == NULL ||
        !ico_present_fallback_driver_eligible(l->vendorId, l->driverText) ||
        s_mode == ICO_PRESENT_FALLBACK_FIFO) {
        return ICO_PRESENT_FALLBACK_GATE_NO;
    }
    if (s_mode == ICO_PRESENT_FALLBACK_OFF) {
        return ICO_PRESENT_FALLBACK_GATE_KEPT;
    }
    /* the switch was measured on Qualcomm's own driver only; on a Mali-G52 it
       lengthened the blocks (issue 179) */
    return ico_present_fallback_qualcomm_own(l->vendorId, l->driverText)
               ? ICO_PRESENT_FALLBACK_GATE_YES
               : ICO_PRESENT_FALLBACK_GATE_OTHER;
}

const char *window_fifo_words(void)
{
    static char words[96];
    static int s_startLogged;
    const int start = start_active();

    if (start && !s_startLogged) {
        /* once, before the present mode line that asks for these words */
        char line[160];

        s_startLogged = 1;
        ico_present_fallback_start_line(line, sizeof line);
        fprintf(stderr, "%s\n", line);
    }
    ico_present_fallback_mode_suffix(&s_fallback, s_mode, gate() == ICO_PRESENT_FALLBACK_GATE_YES,
                                     start, words, sizeof words);
    return words;
}

void window_fifo_note(double presentMs, unsigned replay, const char *framerateName,
                      const char *where, const char *split)
{
    if (presentMs > 250.0) {
        const RhiLimits *l = rhi_limits();
        ico_log_limited("window: present blocked",
                        "window: a present blocked %.0f ms in the driver's present call (replay "
                        "%u; %s on %s, %s; present %s, framerate %s%s)%s%s",
                        presentMs, replay, rhi_backend_kind_name(rhi_backend()), rhi_adapter_name(),
                        l && l->driverText ? l->driverText : "driver unknown",
                        rhi_present_mode_name(), framerateName, where ? where : "",
                        split && split[0] ? ": " : "", split ? split : "");
    }
    const int got = ico_present_fallback_note(&s_fallback, SDL_GetTicksNS(), presentMs, gate());
    char line[512];

    if (got == ICO_PRESENT_FALLBACK_SWITCH) {
        const RhiLimits *l = rhi_limits();
        RhiStats st = {0};

        rhi_get_stats(&st);
        ico_present_fallback_switch_line(&s_fallback, rhi_adapter_name(),
                                         l != NULL ? l->driverText : NULL, (unsigned)st.swapImages,
                                         line, sizeof line);
        fprintf(stderr, "%s\n", line);
        s_pending = 1;
    } else if (got == ICO_PRESENT_FALLBACK_OTHER) {
        const RhiLimits *l = rhi_limits();

        ico_present_fallback_other_line(&s_fallback, rhi_adapter_name(),
                                        l != NULL ? l->driverText : NULL, line, sizeof line);
        fprintf(stderr, "%s\n", line);
    } else if (got == ICO_PRESENT_FALLBACK_KEPT) {
        ico_present_fallback_kept_line(&s_fallback, line, sizeof line);
        fprintf(stderr, "%s\n", line);
    }
}

Uint64 window_fifo_apply(void)
{
    uint32_t sw = 0, sh = 0;
    Uint64 ns = 0;

    if (!s_pending) {
        return 0;
    }
    s_pending = 0;
    rhi_present_fifo_fallback(true);
    if (rhi_swapchain_size(&sw, &sh) && sw > 0 && sh > 0) {
        const Uint64 t0 = SDL_GetTicksNS();

        rd_resize_output(sw, sh);
        ns = SDL_GetTicksNS() - t0;
    }
    fprintf(stderr, "window: present mode %s%s\n", rhi_present_mode_name(), window_fifo_words());
    return ns;
}

const char *window_fifo_block(void)
{
    static char words[96];

    ico_present_fallback_block_suffix(&s_fallback, SDL_GetTicksNS(), words, sizeof words);
    return words;
}
