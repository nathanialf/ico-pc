/* vk_swapchain.c: the swapchain over SDL3's Vulkan surface.  Absent when
 * the device is headless (RhiDeviceDesc.sdlWindow == NULL). */
#include "vk_internal.h"
#include "rhi_vk.h"
#include <stdlib.h>
#include <string.h>

#ifdef ICO_RHI_HAVE_SDL

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#endif
#ifdef _WIN32
/* VK_EXT_full_screen_exclusive's structures (the build does not define
 * VK_USE_PLATFORM_WIN32_KHR; VK_NO_PROTOTYPES keeps the header to types) */
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <vulkan/vulkan_win32.h>

#endif

_Static_assert(RHI_VK_TEST_SUBOPTIMAL == VK_SUBOPTIMAL_KHR, "rhi_vk.h: VK_SUBOPTIMAL_KHR");
_Static_assert(RHI_VK_TEST_SURFACE_LOST == VK_ERROR_SURFACE_LOST_KHR,
               "rhi_vk.h: VK_ERROR_SURFACE_LOST_KHR");
_Static_assert(RHI_VK_TEST_OUT_OF_DATE == VK_ERROR_OUT_OF_DATE_KHR,
               "rhi_vk.h: VK_ERROR_OUT_OF_DATE_KHR");

/* tests (rhi_vk.h): the result the next present reports instead of the
 * driver's (VK_SUCCESS: none), and the swapchains created since the
 * program started */
static VkResult s_forcePresent = VK_SUCCESS;
/* the last present reached the display (rhi_present_shown) */
static bool s_presentShown;
static uint32_t s_creations;

void vkr_test_force_present_result(int32_t result)
{
    s_forcePresent = (VkResult)result;
}

uint32_t vkr_test_swapchain_creations(void)
{
    return s_creations;
}

/* the surface's size polled after a present (vkr_poll_surface): when the
 * swapchain was last made, when the poll last ran */
static uint64_t s_swapMadeNs, s_pollNs;
/* a poll rebuild whose swapchain came out at another size than the surface
 * asked (clamped by the surface's limits): that target and the size made,
 * so the same pair is not rebuilt at every present */
static uint32_t s_pollStuckW, s_pollStuckH, s_pollMadeW, s_pollMadeH;

/* ------------------------------------------------- the display's timing
 * (rhi_present_timing; only with ICO_VK_PRESENT_WAIT=1, vk_device.c): a
 * thread waits on each present's id in turn (vkWaitForPresentKHR, which the
 * spec lets run beside any other call on the swapchain but
 * vkDestroySwapchainKHR) and notes when it returned, on SDL_GetTicksNS's
 * clock, the window's.  Ids are given to presents only while the window
 * uses the timing (rhi_present_timing_use: under its vsync lock), so every
 * other present is made as without it.  A wait that returned at once found
 * the present on the screen already: the time of the note before when that
 * was under half a millisecond before (the same refresh: a present passed
 * over in mailbox mode is satisfied by the one after it), else not known.
 *
 * The thread never holds up the presents or the swapchain: it holds its
 * lock only between waits, each wait lasts VKR_PW_WAIT_NS at most, and
 * before the swapchain is made again or destroyed the thread is held out
 * of its waits for VKR_PW_HOLD_NS at most (vkr_pw_pause); a thread still in
 * a wait then (a driver that does not keep the timeout) stops the timing
 * and the swapchain goes on.  At rhi_shutdown the thread is asked to end
 * and is waited for as long; one that does not end is left to end on its
 * own, its state with it. */
#ifdef ICO_RHI_HAVE_SDL
#define VKR_PW_NOTES 64
#define VKR_PW_WAIT_NS 4000000ull
#define VKR_PW_HOLD_NS 100000000ull

typedef struct VkrPw {
    SDL_Mutex *lock;
    SDL_Condition *cond;
    SDL_Thread *thread;
    VkDevice device;
    PFN_vkWaitForPresentKHR wait;
    bool quit, pause, busy, failed, exited, abandoned;
    VkSwapchainKHR swapchain; /* the one waited on (none: nothing to wait for) */
    uint64_t posted;          /* the last id presented to it */
    uint64_t next;            /* the next id to wait for */
    uint64_t lastNs;          /* the last note's time */
    uint64_t noteId[VKR_PW_NOTES], noteNs[VKR_PW_NOTES];
    unsigned head, count;
} VkrPw;

static VkrPw *s_pw;  /* NULL: no thread */
static bool s_pwUse; /* rhi_present_timing_use */

static void vkr_pw_note(VkrPw *p, uint64_t id, uint64_t ns)
{
    if (p->count == VKR_PW_NOTES) {
        p->head = (p->head + 1) % VKR_PW_NOTES; /* the oldest, never taken */
        p->count--;
    }
    const unsigned i = (p->head + p->count++) % VKR_PW_NOTES;
    p->noteId[i] = id;
    p->noteNs[i] = ns;
}

static void vkr_pw_free(VkrPw *p)
{
    if (p->cond) {
        SDL_DestroyCondition(p->cond);
    }
    if (p->lock) {
        SDL_DestroyMutex(p->lock);
    }
    free(p);
}

static int SDLCALL vkr_pw_thread(void *u)
{
    VkrPw *p = u;
    SDL_LockMutex(p->lock);
    while (!p->quit) {
        if (p->pause || p->failed || !p->swapchain || p->next == 0 || p->next > p->posted) {
            SDL_WaitCondition(p->cond, p->lock);
            continue;
        }
        const uint64_t id = p->next;
        const VkSwapchainKHR sc = p->swapchain;
        p->busy = true;
        SDL_UnlockMutex(p->lock);
        const uint64_t t0 = SDL_GetTicksNS();
        const VkResult r = p->wait(p->device, sc, id, VKR_PW_WAIT_NS);
        const uint64_t t = SDL_GetTicksNS();
        SDL_LockMutex(p->lock);
        p->busy = false;
        SDL_BroadcastCondition(p->cond); /* vkr_pw_pause */
        if (r == VK_TIMEOUT) {
            continue; /* not on the screen yet: waited for again */
        }
        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) {
            uint64_t ns = t;
            if (t - t0 < 100000ull) {
                /* on the screen before the wait: the refresh of the note
                   before when that was just now, else not known */
                ns = p->lastNs != 0 && t - p->lastNs < 500000ull ? p->lastNs : 0;
            }
            if (ns != 0) {
                p->lastNs = ns;
            }
            vkr_pw_note(p, id, ns);
            p->next = id + 1;
        } else if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_ERROR_SURFACE_LOST_KHR) {
            p->next = p->posted + 1; /* the swapchain is going: not timed */
        } else {
            VKR_LOG("display timing: vkWaitForPresentKHR returned %d; stopped", (int)r);
            p->failed = true;
        }
    }
    p->exited = true;
    const bool own = p->abandoned; /* rhi_shutdown did not wait for it */
    SDL_BroadcastCondition(p->cond);
    SDL_UnlockMutex(p->lock);
    if (own) {
        vkr_pw_free(p);
    }
    return 0;
}

/* the thread out of its waits, and none started, until vkr_pw_resume; a
   thread still in one after VKR_PW_HOLD_NS stops the timing */
static void vkr_pw_pause(void)
{
    VkrPw *p = s_pw;
    if (!p) {
        return;
    }
    SDL_LockMutex(p->lock);
    p->pause = true;
    const uint64_t until = SDL_GetTicksNS() + VKR_PW_HOLD_NS;
    while (p->busy) {
        const uint64_t now = SDL_GetTicksNS();
        if (now >= until) {
            VKR_LOG("display timing: a wait on a present did not end in %llu ms; stopped",
                    (unsigned long long)(VKR_PW_HOLD_NS / 1000000ull));
            p->failed = true;
            break;
        }
        SDL_WaitConditionTimeout(p->cond, p->lock, (Sint32)((until - now) / 1000000ull + 1));
    }
    p->swapchain = VK_NULL_HANDLE;
    SDL_UnlockMutex(p->lock);
}

/* waits on sc's presents from its first (the thread started the first
   time) */
static void vkr_pw_resume(VkSwapchainKHR sc)
{
    if (!g_vkr.presentWait) {
        return;
    }
    if (!s_pw) {
        VkrPw *p = calloc(1, sizeof(*p));
        if (p) {
            p->device = g_vkr.device;
            p->wait = g_vkr.waitForPresent;
            p->lock = SDL_CreateMutex();
            p->cond = SDL_CreateCondition();
            if (p->lock && p->cond) {
                p->thread = SDL_CreateThread(vkr_pw_thread, "present-wait", p);
            }
            if (!p->thread) {
                VKR_LOG("display timing: no thread (%s); off", SDL_GetError());
                vkr_pw_free(p);
                p = NULL;
            }
        }
        if (!p) {
            g_vkr.presentWait = false;
            return;
        }
        s_pw = p;
    }
    SDL_LockMutex(s_pw->lock);
    s_pw->swapchain = sc;
    s_pw->next = g_vkr.presentId + 1;
    s_pw->pause = false;
    SDL_BroadcastCondition(s_pw->cond);
    SDL_UnlockMutex(s_pw->lock);
}

/* rhi_present: the id it gave sc's queue */
static void vkr_pw_posted(uint64_t id)
{
    if (!s_pw) {
        return;
    }
    SDL_LockMutex(s_pw->lock);
    s_pw->posted = id;
    SDL_BroadcastCondition(s_pw->cond);
    SDL_UnlockMutex(s_pw->lock);
}

/* the test's stand-in (vkr_test_fake_present_wait): a wait that returns
   VK_SUCCESS 2 ms after it began (its timeout, if shorter: VK_TIMEOUT), or
   one that hangs for s_pwFakeHangMs whatever its timeout */
static int s_pwFake;
static uint32_t s_pwFakeHangMs;
static uint32_t s_pwMisuse;

static VkResult VKAPI_CALL vkr_pw_fake_wait(VkDevice device, VkSwapchainKHR swapchain,
                                            uint64_t presentId, uint64_t timeout)
{
    (void)device;
    (void)presentId;
    if (s_pwFakeHangMs) {
        SDL_Delay(s_pwFakeHangMs);
        return VK_TIMEOUT;
    }
    const bool shown = timeout >= 2000000ull;
    SDL_DelayNS(shown ? 2000000ull : timeout);
    if (*(volatile VkSwapchainKHR *)&g_vkr.swapchain != swapchain) {
        s_pwMisuse++;
    }
    return shown ? VK_SUCCESS : VK_TIMEOUT;
}

void vkr_test_fake_present_wait(int on)
{
    s_pwFake = on;
}

void vkr_test_fake_present_wait_hang(uint32_t ms)
{
    s_pwFakeHangMs = ms;
}

uint32_t vkr_test_present_wait_misuse(void)
{
    return s_pwMisuse;
}

PFN_vkWaitForPresentKHR vkr_present_wait_fake(void)
{
    return s_pwFake ? vkr_pw_fake_wait : NULL;
}

void vkr_present_wait_shutdown(void)
{
    VkrPw *p = s_pw;
    s_pw = NULL;
    s_pwUse = false;
    if (!p) {
        return;
    }
    SDL_LockMutex(p->lock);
    p->quit = true;
    SDL_BroadcastCondition(p->cond);
    const uint64_t until = SDL_GetTicksNS() + VKR_PW_HOLD_NS;
    while (!p->exited) {
        const uint64_t now = SDL_GetTicksNS();
        if (now >= until) {
            break;
        }
        SDL_WaitConditionTimeout(p->cond, p->lock, (Sint32)((until - now) / 1000000ull + 1));
    }
    if (!p->exited) {
        /* still in a wait: left to end on its own, and to free its state */
        VKR_LOG("display timing: the waiting thread did not end in %llu ms; left",
                (unsigned long long)(VKR_PW_HOLD_NS / 1000000ull));
        p->abandoned = true;
        SDL_UnlockMutex(p->lock);
        SDL_DetachThread(p->thread);
        return;
    }
    SDL_UnlockMutex(p->lock);
    SDL_WaitThread(p->thread, NULL);
    vkr_pw_free(p);
}

bool rhi_present_timing(void)
{
    bool on = false;
    if (g_vkr.presentWait && s_pw) {
        SDL_LockMutex(s_pw->lock);
        on = !s_pw->failed;
        SDL_UnlockMutex(s_pw->lock);
    }
    return on;
}

void rhi_present_timing_use(bool on)
{
    s_pwUse = on;
}

int rhi_present_timing_next(uint64_t *id, uint64_t *ns)
{
    int r = -1;
    if (g_vkr.presentWait && s_pw) {
        SDL_LockMutex(s_pw->lock);
        r = s_pw->failed ? -1 : s_pw->count > 0;
        if (r > 0) {
            *id = s_pw->noteId[s_pw->head];
            *ns = s_pw->noteNs[s_pw->head];
            s_pw->head = (s_pw->head + 1) % VKR_PW_NOTES;
            s_pw->count--;
        }
        SDL_UnlockMutex(s_pw->lock);
    }
    return r;
}

/* rhi_present: an id for this present (the window uses the timing) */
static bool vkr_pw_wanted(void)
{
    return g_vkr.presentWait && s_pw && s_pwUse;
}
#else
void vkr_test_fake_present_wait(int on)
{
    (void)on;
}

void vkr_test_fake_present_wait_hang(uint32_t ms)
{
    (void)ms;
}

uint32_t vkr_test_present_wait_misuse(void)
{
    return 0;
}

PFN_vkWaitForPresentKHR vkr_present_wait_fake(void)
{
    return NULL;
}

static void vkr_pw_pause(void) {}

static void vkr_pw_resume(VkSwapchainKHR sc)
{
    (void)sc;
}

static void vkr_pw_posted(uint64_t id)
{
    (void)id;
}

void vkr_present_wait_shutdown(void) {}

bool rhi_present_timing(void)
{
    return false;
}

void rhi_present_timing_use(bool on)
{
    (void)on;
}

int rhi_present_timing_next(uint64_t *id, uint64_t *ns)
{
    (void)id;
    (void)ns;
    return -1;
}

static bool vkr_pw_wanted(void)
{
    return false;
}
#endif

uint64_t rhi_present_last_id(void)
{
    return vkr_pw_wanted() ? g_vkr.presentId : 0;
}

/* rhi.h: no count of the pictures shown on Vulkan */
bool rhi_present_statistics(uint64_t *presentCount, uint64_t *presentRefresh, uint64_t *syncRefresh,
                            uint64_t *syncNs)
{
    (void)presentCount;
    (void)presentRefresh;
    (void)syncRefresh;
    (void)syncNs;
    return false;
}

static void vkr_destroy_retired(void);

static void vkr_destroy_swap_resources(void)
{
    for (uint32_t i = 0; i < g_vkr.swapImageCount; i++) {
        vkr_release_swapchain_image(g_vkr.swapTextures[i]);
        g_vkr.swapTextures[i] = 0;
        if (g_vkr.renderDone[i]) {
            vkDestroySemaphore(g_vkr.device, g_vkr.renderDone[i], NULL);
            g_vkr.renderDone[i] = VK_NULL_HANDLE;
        }
    }
    g_vkr.swapImageCount = 0;
}

/* the retired swapchain (vkr_swapchain_create), once the new one has put
   a picture on the screen or the swapchain goes away */
static void vkr_destroy_retired(void)
{
    if (g_vkr.retired) {
        vkDestroySwapchainKHR(g_vkr.device, g_vkr.retired, NULL);
        g_vkr.retired = VK_NULL_HANDLE;
    }
}

uint32_t vkr_test_retired_swapchain(void)
{
    return g_vkr.retired != VK_NULL_HANDLE;
}

uint64_t vkr_test_swapchain_handle(bool retired)
{
    return (uint64_t)(retired ? g_vkr.retired : g_vkr.swapchain);
}

void vkr_swapchain_destroy(void)
{
    if (!g_vkr.device) {
        return;
    }
    vkr_pw_pause();
    vkr_destroy_swap_resources();
    vkr_destroy_retired();
    if (g_vkr.swapchain) {
        vkDestroySwapchainKHR(g_vkr.device, g_vkr.swapchain, NULL);
        g_vkr.swapchain = VK_NULL_HANDLE;
    }
}

/* rhi_prefer_mailbox: kept across rhi_init, which clears g_vkr */
static bool s_preferMailbox;

/* The surface's present modes (calloc'd, *n of them; NULL with *n 0 when
 * the query fails): every one, no cap */
/* rhi_set_exclusive_fullscreen: the driver may take exclusive fullscreen
 * (the default) or must keep the window composed; the next swapchain
 * creation reads it */
static bool s_exclusiveAllowed = true;

void rhi_set_exclusive_fullscreen(bool allowed)
{
    s_exclusiveAllowed = allowed;
}

#ifdef _WIN32
/* The full-screen-exclusive request chained into the swapchain and into
 * its surface queries (the spec's rule for VK_EXT_full_screen_exclusive:
 * the capabilities and present modes a swapchain is checked against are
 * the ones queried with the same VkSurfaceFullScreenExclusiveInfoEXT).
 * DEFAULT lets the driver decide (NVIDIA's takes exclusive-style
 * presentation for a window covering its monitor); DISALLOWED keeps a
 * composed window.  Kept static: the pointers outlive the call that
 * builds them. */
static VkSurfaceFullScreenExclusiveInfoEXT s_fseInfo;
#endif

/* The chain for this creation: NULL without VK_EXT_full_screen_exclusive
 * (every build but Windows, and a Windows driver without it) */
static const void *vkr_fse_chain(void)
{
#ifdef _WIN32
    if (g_vkr.fullScreenExclusive) {
        s_fseInfo = (VkSurfaceFullScreenExclusiveInfoEXT){
            .sType = VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT,
            .pNext = NULL,
            .fullScreenExclusive = s_exclusiveAllowed ? VK_FULL_SCREEN_EXCLUSIVE_DEFAULT_EXT
                                                      : VK_FULL_SCREEN_EXCLUSIVE_DISALLOWED_EXT,
        };
        return &s_fseInfo;
    }
#endif
    return NULL;
}

/* The surface's capabilities, through vkGetPhysicalDeviceSurfaceCapabilities2KHR
 * with the chain when there is one */
static VkResult vkr_surface_caps(const void *chain, VkSurfaceCapabilitiesKHR *caps)
{
#ifdef _WIN32
    if (chain != NULL) {
        const VkPhysicalDeviceSurfaceInfo2KHR info = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
            .pNext = chain,
            .surface = g_vkr.surface,
        };
        VkSurfaceCapabilities2KHR c2 = {.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR};
        const VkResult r = vkGetPhysicalDeviceSurfaceCapabilities2KHR(g_vkr.phys, &info, &c2);
        if (r == VK_SUCCESS) {
            *caps = c2.surfaceCapabilities;
        }
        return r;
    }
#endif
    (void)chain;
    return vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vkr.phys, g_vkr.surface, caps);
}

/* The surface's present modes, through
 * vkGetPhysicalDeviceSurfacePresentModes2EXT with the chain when there is
 * one */
static VkResult vkr_present_modes_query(const void *chain, uint32_t *n, VkPresentModeKHR *modes)
{
#ifdef _WIN32
    if (chain != NULL) {
        const VkPhysicalDeviceSurfaceInfo2KHR info = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
            .pNext = chain,
            .surface = g_vkr.surface,
        };
        const PFN_vkGetPhysicalDeviceSurfacePresentModes2EXT fn =
            (PFN_vkGetPhysicalDeviceSurfacePresentModes2EXT)g_vkr.presentModes2;
        return fn(g_vkr.phys, &info, n, modes);
    }
#endif
    (void)chain;
    return vkGetPhysicalDeviceSurfacePresentModesKHR(g_vkr.phys, g_vkr.surface, n, modes);
}

/* One line per swapchain creation whose full-screen-exclusive state
 * differs from the last one logged (Windows only: elsewhere there is
 * nothing to say) */
static void vkr_fse_log(const void *chain)
{
#ifdef _WIN32
    static int s_logged = -1; /* 0 disallowed, 1 allowed, 2 not available */
    const int now = chain == NULL ? 2 : (s_exclusiveAllowed ? 1 : 0);

    if (now != s_logged) {
        s_logged = now;
        if (now == 2) {
            fprintf(stderr, "rhi: swapchain exclusive fullscreen not available on this driver\n");
        } else {
            fprintf(stderr, "rhi: swapchain exclusive fullscreen %s\n",
                    now == 0 ? "disallowed (VK_EXT_full_screen_exclusive)"
                             : "allowed (the driver decides)");
        }
    }
#else
    /* without the extension the flag is only stored (clang: set but not used) */
    (void)chain;
    (void)s_exclusiveAllowed;
#endif
}

static VkPresentModeKHR *vkr_surface_present_modes(const void *chain, uint32_t *n)
{
    *n = 0;
    if (vkr_present_modes_query(chain, n, NULL) != VK_SUCCESS || *n == 0) {
        *n = 0;
        return NULL;
    }
    VkPresentModeKHR *modes = calloc(*n, sizeof(*modes));
    if (!modes) {
        *n = 0;
        return NULL;
    }
    VkResult r = vkr_present_modes_query(chain, n, modes);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) {
        *n = 0;
    }
    return modes;
}

/* "immediate mailbox fifo fifo_relaxed": the offered modes for the log */
static void vkr_present_mode_list(char *out, size_t size, const VkPresentModeKHR *modes, uint32_t n)
{
    size_t len = 0;
    out[0] = '\0';
    for (uint32_t i = 0; i < n && len < size; i++) {
        const char *name = vkr_present_mode_name(modes[i]);
        int k = name ? snprintf(out + len, size - len, "%s%s", len ? " " : "", name)
                     : snprintf(out + len, size - len, "%s%d", len ? " " : "", (int)modes[i]);
        if (k < 0) {
            break;
        }
        len += (size_t)k;
    }
    if (n == 0) {
        snprintf(out, size, "none");
    }
}

/* An acquire's semaphore no submit waited on (an image acquired, then the
 * swapchain recreated or released before the frame was drawn): an empty
 * submit waits on it, so it is unsignalled before it is used again */
static void vkr_drain_acquire_wait(void)
{
    if (g_vkr.acquireWaitPending && !vkr_submit_empty()) {
        /* the wait could not be queued: the device is idle, so recreate the
         * frame's semaphore instead of leaving it signalled */
        VkSemaphoreCreateInfo asci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore fresh;
        if (VKR_CHECK(vkCreateSemaphore(g_vkr.device, &asci, NULL, &fresh))) {
            for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
                if (g_vkr.frames[i].acquireSem == g_vkr.acquireSem) {
                    vkDestroySemaphore(g_vkr.device, g_vkr.acquireSem, NULL);
                    g_vkr.frames[i].acquireSem = fresh;
                    fresh = VK_NULL_HANDLE;
                    break;
                }
            }
            if (fresh) {
                vkDestroySemaphore(g_vkr.device, fresh, NULL);
            }
        }
    }
}

bool vkr_swapchain_create(uint32_t w, uint32_t h, bool vsync)
{
    if (!g_vkr.surface) {
        return false;
    }
    /* Windows: whether the driver may take exclusive fullscreen, chained
     * into the queries below and the swapchain (rhi_set_exclusive_fullscreen) */
    const void *fse = vkr_fse_chain();
    VkSurfaceCapabilitiesKHR caps;
    if (!VKR_CHECK(vkr_surface_caps(fse, &caps))) {
        return false;
    }
    if (caps.currentExtent.width != UINT32_MAX) {
        w = caps.currentExtent.width;
        h = caps.currentExtent.height;
    }
    if (w < caps.minImageExtent.width) {
        w = caps.minImageExtent.width;
    }
    if (h < caps.minImageExtent.height) {
        h = caps.minImageExtent.height;
    }
    if (w > caps.maxImageExtent.width) {
        w = caps.maxImageExtent.width;
    }
    if (h > caps.maxImageExtent.height) {
        h = caps.maxImageExtent.height;
    }
    if (w == 0 || h == 0) {
        return false; /* minimised: the caller retries on the next resize */
    }

    /* BGRA8 UNORM in sRGB-nonlinear colour space: the game's colours are
     * written as-is, like the PS2's DAC */
    uint32_t nf = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_vkr.phys, g_vkr.surface, &nf, NULL);
    VkSurfaceFormatKHR *fmts = calloc(nf ? nf : 1, sizeof(*fmts));
    if (!fmts) {
        return false;
    }
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_vkr.phys, g_vkr.surface, &nf, fmts);
    VkSurfaceFormatKHR pick = nf ? fmts[0] : (VkSurfaceFormatKHR){VK_FORMAT_B8G8R8A8_UNORM, 0};
    bool found = false;
    for (uint32_t i = 0; i < nf && !found; i++) {
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            fmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            pick = fmts[i];
            found = true;
        }
    }
    for (uint32_t i = 0; i < nf && !found; i++) {
        if (fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM &&
            fmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            pick = fmts[i];
            found = true;
        }
    }
    /* what the surface offers (first 8) and whether the pick was the first
     * offered, for the swapchain line below and its once-per-run list */
    char fmtList[160];
    fmtList[0] = '\0';
    for (uint32_t i = 0; i < nf && i < 8; i++) {
        const size_t used = strlen(fmtList);
        snprintf(fmtList + used, sizeof(fmtList) - used, "%s%d/%d", i ? " " : "",
                 (int)fmts[i].format, (int)fmts[i].colorSpace);
    }
    const bool pickedFirst =
        nf && fmts[0].format == pick.format && fmts[0].colorSpace == pick.colorSpace;
    const uint32_t nOffered = nf;
    free(fmts);
    if (!found) {
        VKR_LOG("surface offers neither BGRA8 nor RGBA8 UNORM; using format %d", (int)pick.format);
    }
    g_vkr.swapFormat = pick.format;
    g_vkr.swapColorSpace = pick.colorSpace;
    g_vkr.swapRhiFormat =
        pick.format == VK_FORMAT_R8G8B8A8_UNORM ? RHI_FMT_RGBA8_UNORM : RHI_FMT_BGRA8_UNORM;

    uint32_t count = caps.minImageCount + 1u;
    if (caps.maxImageCount && count > caps.maxImageCount) {
        count = caps.maxImageCount;
    }
    if (count > VKR_MAX_SWAP_IMAGES) {
        count = VKR_MAX_SWAP_IMAGES;
    }
    vkr_drain_acquire_wait();
    /* the present mode (vk_present_mode.c) from the modes the surface
     * offers, queried once per creation */
    uint32_t nModes = 0;
    VkPresentModeKHR *modes = vkr_surface_present_modes(fse, &nModes);
    char offered[128];
    vkr_present_mode_list(offered, sizeof(offered), modes, nModes);
    /* the compositor turns the picture (an Android phone held sideways
     * reports a rotated current transform); presenting in the window's own
     * orientation keeps every pass and the readbacks unrotated.  The
     * current transform only where identity is not offered. */
    VkSurfaceTransformFlagBitsKHR pre = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    if (!(caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)) {
        pre = caps.currentTransform;
    }
    {
        static bool s_transformLogged;
        if (!s_transformLogged &&
            (pre != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR ||
             caps.currentTransform != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)) {
            s_transformLogged = true;
            VKR_LOG("surface transform 0x%x (offered 0x%x): presenting with %s",
                    (unsigned)caps.currentTransform, (unsigned)caps.supportedTransforms,
                    pre == VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                        ? "identity, the compositor rotates"
                        : "the current transform (identity is not offered)");
        }
    }
    VkSwapchainKHR old = g_vkr.swapchain;
    vkr_pw_pause(); /* no wait on the old one while it is retired */
    VkSwapchainCreateInfoKHR ci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = fse,
        .surface = g_vkr.surface,
        .minImageCount = count,
        .imageFormat = pick.format,
        .imageColorSpace = pick.colorSpace,
        .imageExtent = {w, h},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = pre,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode =
            vkr_choose_present_mode(modes, nModes, vsync, s_preferMailbox, g_vkr.fifoLatestReady),
        .clipped = VK_TRUE,
        .oldSwapchain = old,
    };
    if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }
    free(modes);
    VkSwapchainKHR sc;
    if (!VKR_CHECK(vkCreateSwapchainKHR(g_vkr.device, &ci, NULL, &sc))) {
        if (old) {
            /* the old swapchain is retired even when the creation fails, and
             * a retired one may not be passed as oldSwapchain again: drop it
             * (the device is idle, rhi_resize_swapchain), so the next acquire
             * returns id 0 and the caller's retry creates from scratch */
            vkr_destroy_swap_resources();
            vkr_destroy_retired();
            vkDestroySwapchainKHR(g_vkr.device, old, NULL);
            g_vkr.swapchain = VK_NULL_HANDLE;
            g_vkr.swapAcquired = false;
            g_vkr.presentSignalled = false;
            g_vkr.acquireWaitPending = false;
        }
        return false;
    }
    s_creations++;
    g_vkr.stats.swapchains++;
    vkr_fse_log(fse);
    s_swapMadeNs = vkr_now_ns(); /* the poll runs at every present again */
    vkr_destroy_swap_resources();
    /* the old swapchain stays until the first present on the new one
     * succeeds (rhi_present): the screen keeps its last picture through the
     * rebuild and the first replay instead of going black. A retired one
     * still held means no present reached `old`: the retired one is the one
     * on the screen, so it stays and `old` (never shown) goes instead
     * (a second rebuild before a present, as a frame-rate change that
     * also changes the present mode) */
    if (g_vkr.retired) {
        vkDestroySwapchainKHR(g_vkr.device, old, NULL);
    } else {
        g_vkr.retired = old;
    }
    g_vkr.swapchain = sc;
    g_vkr.mailbox = vsync && ci.presentMode == VK_PRESENT_MODE_MAILBOX_KHR;
    g_vkr.presentMode = ci.presentMode;
    g_vkr.swapWidth = w;
    g_vkr.swapHeight = h;
    g_vkr.vsync = vsync;

    /* the implementation may create more images than minImageCount asks
       for, and an acquire can return any of them: every one needs a slot */
    VkImage images[VKR_MAX_SWAP_IMAGES];
    uint32_t n = 0;
    vkGetSwapchainImagesKHR(g_vkr.device, sc, &n, NULL);
    if (n > VKR_MAX_SWAP_IMAGES) {
        VKR_LOG("swapchain has %u images, more than %u", n, (unsigned)VKR_MAX_SWAP_IMAGES);
        vkDestroySwapchainKHR(g_vkr.device, sc, NULL);
        g_vkr.swapchain = VK_NULL_HANDLE;
        g_vkr.swapAcquired = false;
        g_vkr.presentSignalled = false;
        g_vkr.acquireWaitPending = false;
        return false;
    }
    vkGetSwapchainImagesKHR(g_vkr.device, sc, &n, images);
    VkSemaphoreCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    bool semaphoresMade = true;
    for (uint32_t i = 0; i < n; i++) {
        g_vkr.swapTextures[i] =
            vkr_register_swapchain_image(images[i], pick.format, g_vkr.swapRhiFormat, w, h);
        if (!VKR_CHECK(vkCreateSemaphore(g_vkr.device, &sci, NULL, &g_vkr.renderDone[i]))) {
            g_vkr.renderDone[i] = VK_NULL_HANDLE;
            semaphoresMade = false;
        }
    }
    g_vkr.swapImageCount = n;
    if (!semaphoresMade) {
        /* a present on that image would have nothing to wait on: no
         * swapchain, as for too many images above */
        vkr_destroy_swap_resources();
        vkDestroySwapchainKHR(g_vkr.device, sc, NULL);
        g_vkr.swapchain = VK_NULL_HANDLE;
        g_vkr.swapAcquired = false;
        g_vkr.presentSignalled = false;
        g_vkr.acquireWaitPending = false;
        return false;
    }
    g_vkr.swapAcquired = false;
    g_vkr.presentSignalled = false;
    g_vkr.acquireWaitPending = false;
    vkr_pw_resume(sc);
    /* a line when the present mode, the image count or the size differs
       from the last swapchain (always the first).  The size counts so a
       log shows every rebuild the renderer's output then follows (a
       window-edge drag prints a line per size) */
    static VkPresentModeKHR s_loggedMode;
    static uint32_t s_loggedCount, s_loggedW, s_loggedH;
    static bool s_logged;
    if (!s_logged || s_loggedMode != ci.presentMode || s_loggedCount != n || s_loggedW != w ||
        s_loggedH != h) {
        s_logged = true;
        s_loggedMode = ci.presentMode;
        s_loggedCount = n;
        s_loggedW = w;
        s_loggedH = h;
        /* the window's own pixel size and SDL's video driver (x11, wayland,
         * windows, android, ...): a driver-side mismatch (the swapchain
         * extent against the window, a format or alpha the compositor
         * treats differently) reads from this line alone */
        int winW = 0, winH = 0;
        const char *drv = "?";
#ifdef ICO_RHI_HAVE_SDL
        if (g_vkr.window) {
            SDL_GetWindowSizeInPixels((SDL_Window *)g_vkr.window, &winW, &winH);
        }
        if (SDL_GetCurrentVideoDriver()) {
            drv = SDL_GetCurrentVideoDriver();
        }
#endif
        VKR_LOG("swapchain %ux%u, %u images, present mode %s (vsync %s%s); offered: %s; "
                "format %d colour space %d (%s offered, %s), composite alpha 0x%x, "
                "pre-transform 0x%x, images asked %u got %u, window %dx%d pixels, video driver %s",
                w, h, n, vkr_present_mode_name(ci.presentMode), vsync ? "on" : "off",
                vsync && s_preferMailbox ? ", mailbox preferred" : "", offered, (int)pick.format,
                (int)pick.colorSpace, pickedFirst ? "first" : "not first",
                found ? "wanted" : "fallback", (unsigned)ci.compositeAlpha,
                (unsigned)ci.preTransform, count, n, winW, winH, drv);
    }
    {
        static bool s_formatsLogged;
        if (!s_formatsLogged) {
            s_formatsLogged = true;
            VKR_LOG("surface formats (%u offered, first 8 as format/colour space): %s",
                    (unsigned)nOffered, fmtList);
        }
    }
    return true;
}

/* A surface on the window rhi_init was given (or the last
 * rhi_recreate_surface named).  The first failure of a run of them is
 * logged: on Android the window has no native surface while the app is in
 * the background, and every frame's retry would log it again. */
#ifdef ICO_RHI_HAVE_SDL
static bool s_surfaceFailLogged;
#endif

static bool vkr_surface_create(void)
{
#ifdef ICO_RHI_HAVE_SDL
    if (!g_vkr.window || !g_vkr.instance || g_vkr.surface) {
        return g_vkr.surface != VK_NULL_HANDLE;
    }
    VkSurfaceKHR surface = VK_NULL_HANDLE;
#ifdef __ANDROID__
    /* through the loader rhi_init used (vk_surface_android.c), which logs
       nothing while the app has no native window */
    if (!vkr_create_window_surface(g_vkr.window, &surface)) {
        if (!s_surfaceFailLogged) {
            s_surfaceFailLogged = true;
            VKR_LOG("no window surface (tried again at the next frame)");
        }
        return false;
    }
#else
    if (!SDL_Vulkan_CreateSurface((SDL_Window *)g_vkr.window, g_vkr.instance, NULL, &surface)) {
        if (!s_surfaceFailLogged) {
            s_surfaceFailLogged = true;
            VKR_LOG("SDL_Vulkan_CreateSurface: %s (tried again at the next frame)", SDL_GetError());
        }
        return false;
    }
#endif
    /* the queue presents to the old surface; a new one is asked again */
    VkBool32 present = VK_FALSE;
    if (g_vkr.phys) {
        vkGetPhysicalDeviceSurfaceSupportKHR(g_vkr.phys, g_vkr.queueFamily, surface, &present);
    }
    if (!present) {
        if (!s_surfaceFailLogged) {
            s_surfaceFailLogged = true;
            VKR_LOG("the new surface cannot be presented to from queue family %u",
                    g_vkr.queueFamily);
        }
        vkDestroySurfaceKHR(g_vkr.instance, surface, NULL);
        return false;
    }
    g_vkr.surface = surface;
    if (s_surfaceFailLogged) {
        VKR_LOG("surface made again");
    }
    s_surfaceFailLogged = false;
    return true;
#else
    return false;
#endif
}

void rhi_release_surface(void)
{
    if (!g_vkr.device || !g_vkr.surface) {
        return;
    }
    vkr_drain_acquire_wait();
    const uint64_t t0 = vkr_now_ns();
    vkDeviceWaitIdle(g_vkr.device);
    g_vkr.stats.waitIdles++;
    g_vkr.stats.fenceWaitNs += vkr_now_ns() - t0;
    vkr_swapchain_destroy();
    g_vkr.swapAcquired = false;
    g_vkr.presentSignalled = false;
    g_vkr.acquireWaitPending = false;
    vkDestroySurfaceKHR(g_vkr.instance, g_vkr.surface, NULL);
    g_vkr.surface = VK_NULL_HANDLE;
    VKR_LOG("surface released; the device is kept");
}

bool rhi_recreate_surface(void *window)
{
    if (!g_vkr.device) {
        return false;
    }
    if (window) {
        g_vkr.window = window;
    }
    if (!g_vkr.window) {
        return false; /* headless */
    }
    rhi_release_surface();
    if (!vkr_surface_create()) {
        return false;
    }
    int w = 0, h = 0;
#ifdef ICO_RHI_HAVE_SDL
    SDL_GetWindowSizeInPixels((SDL_Window *)g_vkr.window, &w, &h);
#endif
    if (!vkr_swapchain_create((uint32_t)w, (uint32_t)h, g_vkr.vsync)) {
        /* the surface stays: rhi_resize_swapchain makes the swapchain at
         * the next frame */
        return false;
    }
    VKR_LOG("surface and swapchain made again (%ux%u)", g_vkr.swapWidth, g_vkr.swapHeight);
    return true;
}

/* The surface went away under the swapchain (VK_ERROR_SURFACE_LOST_KHR;
 * Android destroys the window's surface before the app hears it is in the
 * background): a new surface and swapchain, or none until the window has a
 * surface again */
static void vkr_surface_lost(const char *where)
{
    VKR_LOG("surface lost at %s; making it again", where);
    rhi_recreate_surface(NULL);
}

/* The size a swapchain made now would have: the surface's current extent
 * when it has one, else the window's pixel size */
static bool vkr_swap_target_size(uint32_t *w, uint32_t *h)
{
    int ww = (int)g_vkr.swapWidth, wh = (int)g_vkr.swapHeight;
#ifdef ICO_RHI_HAVE_SDL
    SDL_GetWindowSizeInPixels((SDL_Window *)g_vkr.window, &ww, &wh);
#endif
    *w = ww > 0 ? (uint32_t)ww : 0;
    *h = wh > 0 ? (uint32_t)wh : 0;
    VkSurfaceCapabilitiesKHR caps;
    if (g_vkr.surface &&
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vkr.phys, g_vkr.surface, &caps) == VK_SUCCESS &&
        caps.currentExtent.width != UINT32_MAX) {
        *w = caps.currentExtent.width;
        *h = caps.currentExtent.height;
    }
    return *w != 0 && *h != 0;
}

bool rhi_resize_swapchain(uint32_t width, uint32_t height, bool vsync)
{
    if (!g_vkr.surface) {
        /* released (the background) or lost: made again when the window
         * has a surface */
        if (!g_vkr.device || !g_vkr.window || !vkr_surface_create()) {
            return false;
        }
    }
    const uint64_t t0 = vkr_now_ns();
    vkDeviceWaitIdle(g_vkr.device);
    g_vkr.stats.waitIdles++;
    g_vkr.stats.fenceWaitNs += vkr_now_ns() - t0;
    return vkr_swapchain_create(width, height, vsync);
}

void rhi_prefer_mailbox(bool on)
{
    s_preferMailbox = on;
}

bool rhi_present_mailbox(void)
{
    return g_vkr.swapchain && g_vkr.mailbox;
}

const char *rhi_present_mode_name(void)
{
    const char *name = g_vkr.swapchain ? vkr_present_mode_name(g_vkr.presentMode) : NULL;
    return name ? name : "none";
}

RhiFormat rhi_swapchain_format(void)
{
    /* a window device keeps its format while the surface is released or
     * lost, so the renderer goes on presenting to it (and skips frames)
     * rather than switching to its headless output */
    return g_vkr.surface || g_vkr.window ? g_vkr.swapRhiFormat : RHI_FMT_UNKNOWN;
}

bool rhi_swapchain_size(uint32_t *w, uint32_t *h)
{
    if (!g_vkr.swapchain || !g_vkr.swapWidth || !g_vkr.swapHeight) {
        return false;
    }
    *w = g_vkr.swapWidth;
    *h = g_vkr.swapHeight;
    return true;
}

RhiTexture rhi_acquire_backbuffer(void)
{
    RhiTexture out = {0};
    s_presentShown = false; /* until this frame's present shows it */
    if (!g_vkr.swapchain) {
        return out;
    }
    if (g_vkr.swapAcquired) {
        out.id = g_vkr.swapTextures[g_vkr.swapImage];
        return out;
    }
    VkrFrame *f = vkr_cur_frame();
    uint32_t idx = 0;
    const uint64_t t0 = vkr_now_ns();
    VkResult r = vkAcquireNextImageKHR(g_vkr.device, g_vkr.swapchain, UINT64_MAX, f->acquireSem,
                                       VK_NULL_HANDLE, &idx);
    g_vkr.stats.acquireNs += vkr_now_ns() - t0;
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        return out; /* rd_present recreates via rhi_resize_swapchain */
    }
    if (r == VK_ERROR_SURFACE_LOST_KHR) {
        /* this frame is skipped (the semaphore was not signalled; the
         * caller's retry acquires on the new swapchain) */
        vkr_surface_lost("acquire");
        return out;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        VKR_CHECK(r);
        return out;
    }
    g_vkr.swapImage = idx;
    g_vkr.swapAcquired = true;
    g_vkr.presentSignalled = false;
    g_vkr.acquireWaitPending = true;
    g_vkr.acquireSem = f->acquireSem;
    out.id = g_vkr.swapTextures[idx];
    return out;
}

/* Android, or ICO_VK_POLL_SURFACE=1 (the tests) */
static bool vkr_poll_surface_on(void)
{
#ifdef __ANDROID__
    return true;
#else
    const char *e = getenv("ICO_VK_POLL_SURFACE");
    return e && e[0] == '1';
#endif
}

#define VKR_POLL_ALWAYS_NS (15ull * 1000000000ull) /* every present this long */
#define VKR_POLL_PERIOD_NS 1000000000ull           /* then once a second */

/* The swapchain follows the surface after a present that reported
 * nothing.  On Android the surface can change size in the first
 * seconds (the system bars hidden, the turn to landscape, the cutout mode
 * applied) while the driver goes on returning VK_SUCCESS and SDL's size
 * event comes late or never; the compositor then scales and offsets the old
 * size's image.  The surface's current extent (else the window's pixel
 * size, as vkr_swap_target_size) is asked at every present for the first 15 s
 * after a swapchain is made, then once a second; a different size rebuilds
 * the swapchain as a suboptimal present with a size change does, and the
 * renderer's output follows it at the next acquire
 * (rd__output_follow_swapchain). */
void rhi_surface_poll_restart(void)
{
    s_swapMadeNs = vkr_now_ns();
}

static void vkr_poll_surface(void)
{
    if (!vkr_poll_surface_on() || !g_vkr.swapchain) {
        return;
    }
    const uint64_t now = vkr_now_ns();
    if (now - s_swapMadeNs >= VKR_POLL_ALWAYS_NS && now - s_pollNs < VKR_POLL_PERIOD_NS) {
        return;
    }
    s_pollNs = now;
    uint32_t w = 0, h = 0;
    if (!vkr_swap_target_size(&w, &h) || (w == g_vkr.swapWidth && h == g_vkr.swapHeight)) {
        return;
    }
    if (w == s_pollStuckW && h == s_pollStuckH && g_vkr.swapWidth == s_pollMadeW &&
        g_vkr.swapHeight == s_pollMadeH) {
        return; /* this size was tried and the surface's limits gave another */
    }
    const uint32_t ow = g_vkr.swapWidth, oh = g_vkr.swapHeight;
    if (!rhi_resize_swapchain(w, h, g_vkr.vsync)) {
        return; /* the next acquire finds no swapchain and rd_present retries */
    }
    VKR_LOG("swapchain %ux%u follows the surface (was %ux%u)", g_vkr.swapWidth, g_vkr.swapHeight,
            ow, oh);
    s_pollStuckW = s_pollStuckH = s_pollMadeW = s_pollMadeH = 0;
    if (g_vkr.swapWidth != w || g_vkr.swapHeight != h) {
        s_pollStuckW = w;
        s_pollStuckH = h;
        s_pollMadeW = g_vkr.swapWidth;
        s_pollMadeH = g_vkr.swapHeight;
    }
}

bool rhi_present_shown(void)
{
    return s_presentShown;
}

void rhi_present(void)
{
    s_presentShown = false;
    if (!g_vkr.swapchain || !g_vkr.swapAcquired) {
        return;
    }
    /* the presenting list's submit signalled the semaphore already
     * (rhi_submit); otherwise an empty batch after the frame's work */
    if (!g_vkr.presentSignalled && !vkr_submit_present_signal()) {
        return;
    }
    VkPresentInfoKHR pi = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &g_vkr.renderDone[g_vkr.swapImage],
        .swapchainCount = 1,
        .pSwapchains = &g_vkr.swapchain,
        .pImageIndices = &g_vkr.swapImage,
    };
    /* the display's timing: an id the thread waits on (rhi_present_timing) */
    uint64_t presentId = 0;
    VkPresentIdKHR pid = {.sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR, .swapchainCount = 1};
    if (vkr_pw_wanted()) {
        presentId = ++g_vkr.presentId;
        pid.pPresentIds = &presentId;
        if (!g_vkr.presentWaitFake) {
            pi.pNext = &pid;
        }
    }
    const uint64_t t0 = vkr_now_ns();
    VkResult r = vkQueuePresentKHR(g_vkr.queue, &pi);
    g_vkr.stats.presentNs += vkr_now_ns() - t0;
    g_vkr.stats.presents++;
    g_vkr.swapAcquired = false;
    g_vkr.presentSignalled = false;
    if (g_vkr.retired && (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)) {
        /* the new swapchain's picture is on its way to the screen: the one
         * it replaced can go */
        vkr_destroy_retired();
    }
    if (s_forcePresent != VK_SUCCESS && (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)) {
        r = s_forcePresent; /* a test's result for this present, once */
        s_forcePresent = VK_SUCCESS;
    }
#ifdef __ANDROID__
    const bool rebuild = r == VK_ERROR_OUT_OF_DATE_KHR;
#else
    /* the desktop: suboptimal recreates too, at the window's pixel size;
     * Wayland compositors and gamescope report it when another swapchain
     * could be scanned out directly */
    const bool rebuild = r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR;
#endif
    if (rebuild) {
        int w = (int)g_vkr.swapWidth, h = (int)g_vkr.swapHeight;
#ifdef ICO_RHI_HAVE_SDL
        SDL_GetWindowSizeInPixels((SDL_Window *)g_vkr.window, &w, &h);
#endif
        rhi_resize_swapchain((uint32_t)w, (uint32_t)h, g_vkr.vsync);
    } else if (r == VK_SUBOPTIMAL_KHR) {
        /* Android only: the image was shown.  Recreated only when the size
         * changed: with the identity transform on a rotated Android display
         * the driver reports suboptimal at every present, and a recreation
         * would change nothing */
        uint32_t w = 0, h = 0;
        if (vkr_swap_target_size(&w, &h) && (w != g_vkr.swapWidth || h != g_vkr.swapHeight)) {
            rhi_resize_swapchain(w, h, g_vkr.vsync);
        }
    } else if (r == VK_ERROR_SURFACE_LOST_KHR) {
        vkr_surface_lost("present");
    } else if (r != VK_SUCCESS) {
        VKR_CHECK(r);
    }
    /* an image queued for the display (a suboptimal one too: it is shown) */
    s_presentShown = r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR;
    if (presentId != 0 && s_presentShown && !rebuild) {
        vkr_pw_posted(presentId);
    }
    if (!rebuild && (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)) {
        vkr_poll_surface(); /* a size change the present did not report */
    }
}
