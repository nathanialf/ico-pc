/* vk_glass.c: the display's own times of past presents, read back (rhi.h
 * rhi_present_glass; VK_GOOGLE_display_timing).
 *
 * Where the device offers VK_GOOGLE_display_timing on a window on Android
 * (the one platform whose screen's rule and on-glass line use the times:
 * rd_pace_screen_live.c, rd_as_made_timer.c; elsewhere the extension is
 * not enabled and the driver's present path is v0.11.5's), the extension
 * is enabled, and while the renderer presents pictures as made
 * (rhi_present_glass_use: vsync off, Uncapped, the render thread on; every
 * other mode keeps v0.11.5's present exactly) every present chains one
 * VkPresentTimeGOOGLE:
 * an id (1, 2, ...) and a desiredPresentTime of 0 unless the even cadence
 * asked one for that present (rhi_present_desire: the time of the refresh
 * it was aimed at less half a refresh, on the clock actualPresentTime is
 * on, so it is the display's own times' clock and needs no mapping).  The
 * specification
 * (VkPresentTimeGOOGLE): "desiredPresentTime specifies that the image
 * must not be displayed to the user earlier than this time ... A value of
 * zero specifies that the presentation engine may display the image at
 * any time", so the present asks nothing of the display: which pictures
 * are shown and when is as without it, in every present mode.  The
 * extension adds no state to the swapchain's creation, no wait and no
 * other command.  After each present, on the presenting thread and under
 * the swapchain's lock (vkGetPastPresentationTimingGOOGLE's swapchain is
 * externally synchronized), the past presents' times the driver has are
 * read without a wait (the call returns what it has) into a ring the
 * renderer takes from (rhi_present_glass); the refresh
 * (vkGetRefreshCycleDurationGOOGLE) for each new swapchain and again every
 * second (a failed read tried again 100 ms later, the period known before
 * kept; a change logged).  A present the
 * display never showed (replaced in mailbox mode) has no time reported.
 *
 * VK_EXT_present_timing is not used: its present-time requests and its
 * time domains are a larger surface whose read-only use needs more than
 * this; the GOOGLE extension's read back is the narrower one.
 *
 * The test's stand-in (vkr_test_fake_glass): ids and times without the
 * extension (each present's time the clock when it was made, every third
 * present not reported), so the ring and its reader run on lavapipe. */
#include <stdlib.h>
#include <string.h>

#include "vk_internal.h"

#define GLASS_RING 256u /* power of two */
/* the refresh read again this often (a rate pin, ANativeWindow_setFrameRate,
   or an LTPO screen changes it without a new swapchain), and after a failed
   read this soon: one vkGetRefreshCycleDurationGOOGLE a second at most on
   the presenting thread, against the up to four past-time reads of every
   present */
#define GLASS_REFRESH_EVERY_NS 1000000000ull
#define GLASS_REFRESH_RETRY_NS 100000000ull

static bool s_want;          /* asked of the device: the extension enabled */
static const char *s_whyOff; /* the start line's reason where not asked */
static int s_use;            /* __atomic: rhi_present_glass_use (pictures as made) */
static bool s_on;            /* the entry points found: ids chained */
static int s_fake;           /* the test's stand-in */
static VkDevice s_dev;
static PFN_vkGetPastPresentationTimingGOOGLE s_past;
static PFN_vkGetRefreshCycleDurationGOOGLE s_refresh;
static uint32_t s_nextId;                         /* under the swapchain's lock */
static VkSwapchainKHR s_refreshSc;                /* the swapchain the refresh was last read for */
static uint64_t s_refreshNext;                    /* vkr_now_ns from which it is read again */
static uint32_t s_refreshFails, s_refreshChanges; /* for the lines */
static uint64_t s_refreshNs;                      /* __atomic */
static _Thread_local uint32_t t_lastId;
static _Thread_local uint64_t t_desire; /* the next present's desired time (rhi_present_desire) */

/* the ring: written under the swapchain's lock (one writer at a time),
   read by the renderer's one reader.  The contract: s_tail is the
   consumer's alone, stored only by rhi_present_glass with max > 0 (the
   thread that collects the records, rd_glass_live.c) and by
   vkr_glass_device_made (the device made again, no present or reader
   running); a call with max 0 (rd_perf.c: the calling thread's last id
   only, on the submit thread) never touches it, so it cannot rewind times
   the reader already took */
static RhiGlassTime s_ring[GLASS_RING];
static uint32_t s_head, s_tail; /* __atomic */

void vkr_test_fake_glass(int on)
{
    s_fake = on;
}

void vkr_glass_device_exts(bool offered, bool surface, const char **exts, uint32_t *count)
{
    s_want = false;
    s_on = false;
    s_whyOff = "no window";
    if (!surface) {
        return;
    }
    if (s_fake) {
        VKR_LOG("display timing (read back): a test's stand-in (no extension)");
        return;
    }
#ifdef __ANDROID__
    s_whyOff = "VK_GOOGLE_display_timing not offered";
    if (offered && *count < 40u) {
        exts[(*count)++] = VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME;
        s_want = true;
    }
#else
    /* the times serve the screen's rule and the on-glass line, Android's
       alone: the extension is not enabled, nothing chained */
    (void)exts;
    (void)count;
    s_whyOff = offered ? "not on this platform: VK_GOOGLE_display_timing is offered but used on "
                         "Android only, not enabled"
                       : "not on this platform (Android only)";
#endif
}

void rhi_present_glass_use(bool on)
{
    if (__atomic_load_n(&s_use, __ATOMIC_RELAXED) != (int)on) {
        __atomic_store_n(&s_use, (int)on, __ATOMIC_RELAXED);
    }
}

void vkr_glass_device_made(VkDevice dev)
{
    s_dev = dev;
    s_nextId = 0;
    s_refreshSc = VK_NULL_HANDLE;
    s_refreshNext = 0;
    __atomic_store_n(&s_refreshNs, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tail, __atomic_load_n(&s_head, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
    if (s_fake) {
        s_on = true;
        return;
    }
    if (!s_want) {
        VKR_LOG("display timing (read back): off (%s)", s_whyOff ? s_whyOff : "not asked");
        return;
    }
    s_past = (PFN_vkGetPastPresentationTimingGOOGLE)vkGetDeviceProcAddr(
        dev, "vkGetPastPresentationTimingGOOGLE");
    s_refresh = (PFN_vkGetRefreshCycleDurationGOOGLE)vkGetDeviceProcAddr(
        dev, "vkGetRefreshCycleDurationGOOGLE");
    s_on = s_past != NULL && s_refresh != NULL;
    VKR_LOG("display timing (read back): %s",
            s_on ? "VK_GOOGLE_display_timing on (read only, while pictures are presented as "
                   "made: each present an id and a desired time of 0, \"any time\"; the past "
                   "presents' times read after each present, no wait; in every other mode "
                   "nothing chained, nothing read)"
                 : "off (VK_GOOGLE_display_timing's entry points not found)");
}

void vkr_glass_device_gone(void)
{
    s_on = false;
    s_want = false;
    s_dev = VK_NULL_HANDLE;
    s_past = NULL;
    s_refresh = NULL;
}

const void *vkr_glass_chain(VkPresentTimesInfoGOOGLE *info, VkPresentTimeGOOGLE *t,
                            const void *next, uint32_t *id)
{
    *id = 0;
    if (!s_on || !__atomic_load_n(&s_use, __ATOMIC_RELAXED)) {
        t_desire = 0; /* never carried over to a present that chains one */
        return next;  /* v0.11.5's present: nothing chained */
    }
    if (++s_nextId == 0) {
        s_nextId = 1; /* 0 is "none" */
    }
    *id = s_nextId;
    if (s_fake) {
        return next; /* the stand-in chains nothing */
    }
    t->presentID = s_nextId;
    /* any time (0: nothing asked of the display), or the even cadence's
       time for this present (rhi_present_desire, the display's clock) */
    t->desiredPresentTime = t_desire;
    t_desire = 0;
    *info = (VkPresentTimesInfoGOOGLE){.sType = VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE,
                                       .pNext = next,
                                       .swapchainCount = 1,
                                       .pTimes = t};
    return info;
}

static void ringPut(const RhiGlassTime *g)
{
    const uint32_t head = __atomic_load_n(&s_head, __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE);
    if (head - tail >= GLASS_RING) {
        return; /* the reader fell a ring behind: the time is lost (its id unmatched) */
    }
    s_ring[head % GLASS_RING] = *g;
    __atomic_store_n(&s_head, head + 1u, __ATOMIC_RELEASE);
}

void rhi_present_desire(uint64_t desiredNs)
{
    t_desire = s_on && !s_fake ? desiredNs : 0;
}

void vkr_glass_presented(VkSwapchainKHR sc, uint32_t id, bool shown)
{
    t_lastId = shown ? id : 0u;
    if (!s_on || sc == VK_NULL_HANDLE || id == 0u) {
        return; /* no id chained (not as made): nothing read */
    }
    if (s_fake) {
        /* every third present not reported (as one replaced) */
        if (shown && id % 3u != 0u) {
            const uint64_t now = vkr_now_ns();
            const RhiGlassTime g = {id, now, now, 0, 0};
            ringPut(&g);
        }
        __atomic_store_n(&s_refreshNs, 8333333u, __ATOMIC_RELAXED);
        return;
    }
    const uint64_t now = vkr_now_ns();
    if (sc != s_refreshSc || now >= s_refreshNext) {
        /* a new swapchain, a second since the last read, or a retry: a read
           that fails keeps the period known before and is tried again
           GLASS_REFRESH_RETRY_NS later (the renderer's screen model starts
           its fit again on a changed period, rd_pace_screen.c) */
        VkRefreshCycleDurationGOOGLE rc = {0};
        const VkResult rr = s_refresh(s_dev, sc, &rc);
        s_refreshSc = sc;
        if (rr == VK_SUCCESS && rc.refreshDuration != 0u) {
            const uint64_t was = __atomic_load_n(&s_refreshNs, __ATOMIC_RELAXED);
            __atomic_store_n(&s_refreshNs, rc.refreshDuration, __ATOMIC_RELAXED);
            s_refreshNext = now + GLASS_REFRESH_EVERY_NS;
            if (was != 0u && was != rc.refreshDuration) {
                const uint32_t k = s_refreshChanges++;
                if (k < 8u || k % 100u == 0u) {
                    VKR_LOG("display timing (read back): the display's refresh is now %.4f ms (was "
                            "%.4f ms; change %u)",
                            (double)rc.refreshDuration / 1e6, (double)was / 1e6, k + 1u);
                }
            }
        } else {
            s_refreshNext = now + GLASS_REFRESH_RETRY_NS;
            const uint32_t k = s_refreshFails++;
            if (k < 8u || k % 100u == 0u) {
                VKR_LOG("display timing (read back): the display's refresh could not be read "
                        "(VkResult %d, %llu ns; failure %u): the period known before kept, read "
                        "again in 100 ms",
                        (int)rr, (unsigned long long)rc.refreshDuration, k + 1u);
            }
        }
    }
    VkPastPresentationTimingGOOGLE past[16];
    for (int round = 0; round < 4; round++) {
        uint32_t n = 16;
        const VkResult r = s_past(s_dev, sc, &n, past);
        if (r != VK_SUCCESS && r != VK_INCOMPLETE) {
            return;
        }
        for (uint32_t i = 0; i < n; i++) {
            const RhiGlassTime g = {past[i].presentID, past[i].actualPresentTime,
                                    past[i].earliestPresentTime, past[i].presentMargin,
                                    past[i].desiredPresentTime};
            ringPut(&g);
        }
        if (r != VK_INCOMPLETE) {
            return;
        }
    }
}

uint32_t rhi_present_glass(uint64_t *lastId, uint64_t *refreshNs, RhiGlassTime *out, uint32_t max)
{
    if (lastId != NULL) {
        *lastId = t_lastId;
    }
    if (refreshNs != NULL) {
        *refreshNs = __atomic_load_n(&s_refreshNs, __ATOMIC_RELAXED);
    }
    if (max == 0u || out == NULL) {
        return 0; /* the id and the refresh alone: not the ring's consumer */
    }
    uint32_t n = 0;
    uint32_t tail = __atomic_load_n(&s_tail, __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&s_head, __ATOMIC_ACQUIRE);
    while (n < max && tail != head) {
        out[n++] = s_ring[tail % GLASS_RING];
        tail++;
    }
    __atomic_store_n(&s_tail, tail, __ATOMIC_RELEASE);
    return n;
}
