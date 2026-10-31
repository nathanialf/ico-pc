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
}

/* What the window says about a present it notes (present_fallback.h):
   vsync off in the options, the swapchain in mailbox mode, Vulkan, a
   driver the switch is for; the dev key off keeps mailbox; FIFO from the
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
    return s_mode == ICO_PRESENT_FALLBACK_OFF ? ICO_PRESENT_FALLBACK_GATE_KEPT
                                              : ICO_PRESENT_FALLBACK_GATE_YES;
}

const char *window_fifo_words(void)
{
    static char words[96];

    ico_present_fallback_mode_suffix(&s_fallback, s_mode, gate() == ICO_PRESENT_FALLBACK_GATE_YES,
                                     words, sizeof words);
    return words;
}

void window_fifo_note(double presentMs, unsigned replay, const char *framerateName)
{
    if (presentMs > 250.0) {
        const RhiLimits *l = rhi_limits();
        ico_log_limited("window: present blocked",
                        "window: a present blocked %.0f ms in the driver's present call (replay "
                        "%u; %s on %s, %s; present %s, framerate %s)",
                        presentMs, replay, rhi_backend_kind_name(rhi_backend()), rhi_adapter_name(),
                        l && l->driverText ? l->driverText : "driver unknown",
                        rhi_present_mode_name(), framerateName);
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
