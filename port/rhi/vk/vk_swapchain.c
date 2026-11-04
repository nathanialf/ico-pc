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

/* The swapchain's lock: the acquire, the present and the swapchain's
 * making again (rhi_resize_swapchain, rhi_release_surface,
 * rhi_recreate_surface) hold it, so a present on the window's render
 * thread (port/platform/render_thread.c) and an acquire or a rebuild on the
 * window's thread never interleave.  SDL's mutexes are recursive: a
 * present that rebuilds calls rhi_resize_swapchain inside it.  Made at the
 * first acquire or rebuild, on the window's thread before a render thread
 * exists (the swap of a racing second creation keeps one); none without
 * SDL (headless: no swapchain). */
#ifdef ICO_RHI_HAVE_SDL
static SDL_Mutex *s_swapLock;

static void vkr_swap_lock(void)
{
    SDL_Mutex *m = (SDL_Mutex *)SDL_GetAtomicPointer((void **)&s_swapLock);
    if (!m) {
        SDL_Mutex *made = SDL_CreateMutex();
        if (made && !SDL_CompareAndSwapAtomicPointer((void **)&s_swapLock, NULL, made)) {
            SDL_DestroyMutex(made);
        }
        m = (SDL_Mutex *)SDL_GetAtomicPointer((void **)&s_swapLock);
    }
    if (m) {
        SDL_LockMutex(m);
    }
}

static void vkr_swap_unlock(void)
{
    SDL_Mutex *m = (SDL_Mutex *)SDL_GetAtomicPointer((void **)&s_swapLock);
    if (m) {
        SDL_UnlockMutex(m);
    }
}
#else
static void vkr_swap_lock(void) {}

static void vkr_swap_unlock(void) {}
#endif

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
    rhi_thread_begin(); /* the process's cores (rhi.h rhi_set_thread_begin) */
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
/* rhi_present_fifo_fallback: every later swapchain presents FIFO with the
 * surface's fewest images; kept across rhi_init like s_preferMailbox */
static bool s_fifoFallback;
/* rhi_present_fifo_start: the window's question for a driver whose first
 * swapchain presents FIFO (NULL asks nothing); kept across rhi_init */
static RhiFifoStartFn s_fifoStart;

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

/* Every slot's image dropped (the swapchain made again or released, the
 * device idle and the submit thread with it) */
static void vkr_acq_reset_all(void)
{
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        memset(&g_vkr.frames[i].acq, 0, sizeof(g_vkr.frames[i].acq));
    }
}

/* An acquire's semaphore no submit waited on (an image acquired, then the
 * swapchain recreated or released before the frame was drawn), of any
 * slot: an empty submit waits on it, so it is unsignalled before it is used
 * again */
static void vkr_drain_acquire_wait(void)
{
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        VkrFrame *f = &g_vkr.frames[i];
        if (!f->acq.waitPending || vkr_submit_empty(f)) {
            continue;
        }
        /* the wait could not be queued: the device is idle, so recreate the
         * slot's semaphore instead of leaving it signalled */
        VkSemaphoreCreateInfo asci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore fresh;
        if (VKR_CHECK(vkCreateSemaphore(g_vkr.device, &asci, NULL, &fresh))) {
            vkDestroySemaphore(g_vkr.device, f->acquireSem, NULL);
            f->acquireSem = fresh;
        }
        f->acq.waitPending = false;
    }
}

/* [dev] prerotate (exported as ICO_VK_PREROTATE): anything but "0" (unset
 * too) makes a swapchain in the surface's own turn where the surface is
 * shown turned; "0" keeps identity, the compositor turning every image */
static bool vkr_prerotate_on(void)
{
    const char *e = getenv("ICO_VK_PREROTATE");
    return !(e != NULL && e[0] == '0' && e[1] == '\0');
}

/* the turns a swapchain made for these capabilities at w x h would have */
static uint32_t vkr_turns_for(const VkSurfaceCapabilitiesKHR *caps, uint32_t w, uint32_t h)
{
    return vkr_prerotate_turns(
        (uint32_t)caps->currentTransform, (uint32_t)caps->supportedTransforms, w, h,
        caps->maxImageExtent.width, caps->maxImageExtent.height, vkr_prerotate_on());
}

bool vkr_swapchain_create(uint32_t w, uint32_t h, bool vsync)
{
    /* a rebuild the submit thread's present left is this one (the device
     * idle, rhi_resize_swapchain; vkr_take_deferred) */
    g_vkr.deferred = 0;
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

    /* one image over the surface's minimum, so a present has a free image
     * without waiting on the display; the FIFO fallback asks for exactly the
     * minimum (the queue the driver blocked on under mailbox stays short),
     * and two at least (with one, every acquire waits for the image to
     * leave the display) */
    bool fifoStart = false;
    if (!vsync && !s_fifoFallback && s_fifoStart != NULL) {
        /* the window's driver question (Qualcomm's own driver): FIFO from
         * the first swapchain, at the fewest images, as the fallback's */
        const RhiLimits *lim = rhi_limits();

        fifoStart = lim != NULL && s_fifoStart(lim->vendorId, lim->driverText) != 0;
    }
    const bool fifoNow = s_fifoFallback || fifoStart;
    uint32_t count =
        fifoNow ? (caps.minImageCount > 2u ? caps.minImageCount : 2u) : caps.minImageCount + 1u;
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
    /* An Android phone held sideways reports a turned current transform
     * (ROTATE_90 or 270; the extent is the window's own, landscape).
     * Presented with identity, the compositor turns every image on the GPU
     * before it shows it, and that work preempts the game's (Google's
     * "Handling device orientation efficiently in Vulkan with
     * pre-rotation", android-developers.googleblog.com, 2020-02: 1-3 ms a
     * frame and a 40 % higher GPU clock on a Pixel 4 XL).  Made with the
     * current transform as preTransform, the images are the extent turned
     * (w and h swapped at a quarter turn, as Google's sample and Arm's
     * surface_rotation sample make them) and the compositor shows them as
     * they are: the renderer draws its picture at w x h as before, then
     * turns it into the image in its last pass (port/render/rd_prerotate.c;
     * rhi_swapchain_rotation).  [dev] prerotate = false
     * (ICO_VK_PREROTATE=0) keeps identity.  Otherwise identity, or the
     * current transform where identity is not offered */
    const uint32_t turns = vkr_turns_for(&caps, w, h);
    VkSurfaceTransformFlagBitsKHR pre = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    if (turns != 0 || !(caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)) {
        pre = caps.currentTransform;
    }
    const uint32_t imageW = (turns & 1u) ? h : w, imageH = (turns & 1u) ? w : h;
    {
        static bool s_transformLogged;
        if (!s_transformLogged && turns == 0 &&
            (pre != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR ||
             caps.currentTransform != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)) {
            s_transformLogged = true;
            VKR_LOG("surface transform 0x%x (offered 0x%x): presenting with %s",
                    (unsigned)caps.currentTransform, (unsigned)caps.supportedTransforms,
                    pre != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                        ? "the current transform (identity is not offered)"
                    : !vkr_prerotate_on()
                        ? "identity, the compositor rotates ([dev] prerotate = false, "
                          "ICO_VK_PREROTATE=0)"
                        : "identity, the compositor rotates");
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
        .imageExtent = {imageW, imageH},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = pre,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = vkr_choose_present_mode(modes, nModes, vsync, s_preferMailbox,
                                               g_vkr.fifoLatestReady, fifoNow),
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
            vkr_acq_reset_all();
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
    g_vkr.mailbox =
        ci.presentMode ==
        VK_PRESENT_MODE_MAILBOX_KHR; /* the actual mode: vsync off falls back to it where immediate is not offered (Android) */
    g_vkr.presentMode = ci.presentMode;
    g_vkr.swapWidth = w;
    g_vkr.swapHeight = h;
    g_vkr.swapTurns = turns;
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
        vkr_acq_reset_all();
        return false;
    }
    vkGetSwapchainImagesKHR(g_vkr.device, sc, &n, images);
    VkSemaphoreCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    bool semaphoresMade = true;
    for (uint32_t i = 0; i < n; i++) {
        g_vkr.swapTextures[i] = vkr_register_swapchain_image(images[i], pick.format,
                                                             g_vkr.swapRhiFormat, imageW, imageH);
        if (!VKR_CHECK(vkCreateSemaphore(g_vkr.device, &sci, NULL, &g_vkr.renderDone[i]))) {
            g_vkr.renderDone[i] = VK_NULL_HANDLE;
            semaphoresMade = false;
        }
    }
    g_vkr.swapImageCount = n;
    g_vkr.swapMinImages = caps.minImageCount;
    if (!semaphoresMade) {
        /* a present on that image would have nothing to wait on: no
         * swapchain, as for too many images above */
        vkr_destroy_swap_resources();
        vkDestroySwapchainKHR(g_vkr.device, sc, NULL);
        g_vkr.swapchain = VK_NULL_HANDLE;
        vkr_acq_reset_all();
        return false;
    }
    vkr_acq_reset_all();
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
        VKR_LOG("swapchain %ux%u, %u images, present mode %s (vsync %s%s%s); offered: %s; "
                "format %d colour space %d (%s offered, %s), composite alpha 0x%x, "
                "pre-transform 0x%x, images asked %u got %u, window %dx%d pixels, video driver %s",
                w, h, n, vkr_present_mode_name(ci.presentMode), vsync ? "on" : "off",
                vsync && s_preferMailbox && !s_fifoFallback ? ", mailbox preferred" : "",
                s_fifoFallback ? ", fifo fallback"
                : fifoStart    ? ", fifo from the start"
                               : "",
                offered, (int)pick.format, (int)pick.colorSpace,
                pickedFirst ? "first" : "not first", found ? "wanted" : "fallback",
                (unsigned)ci.compositeAlpha, (unsigned)ci.preTransform, count, n, winW, winH, drv);
    }
    {
        /* a line at every change of the turn (the first turned swapchain,
         * the phone turned over: 90 to 270, prerotate's escape) */
        static uint32_t s_loggedTurns;
        static uint64_t s_turnChanges;
        if (turns != s_loggedTurns) {
            s_loggedTurns = turns;
            s_turnChanges++;
            if (turns != 0) {
                VKR_LOG("swapchain %ux%u pre-rotated %u (the surface's transform 0x%x; the "
                        "compositor no longer rotates): images %ux%u, the present turns the "
                        "picture into them (turn change %llu)",
                        w, h, turns * 90u, (unsigned)caps.currentTransform, imageW, imageH,
                        (unsigned long long)s_turnChanges);
            } else {
                VKR_LOG("swapchain %ux%u no longer pre-rotated (surface transform 0x%x%s; turn "
                        "change %llu)",
                        w, h, (unsigned)caps.currentTransform,
                        vkr_prerotate_on() ? "" : ", [dev] prerotate = false",
                        (unsigned long long)s_turnChanges);
            }
        }
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

static void vkr_release_surface(void)
{
    if (!g_vkr.device || !g_vkr.surface) {
        return;
    }
    g_vkr.deferred = 0; /* the surface goes: no rebuild left for the next acquire */
    vkr_drain_acquire_wait();
    const uint64_t t0 = vkr_now_ns();
    vkDeviceWaitIdle(g_vkr.device);
    g_vkr.stats.waitIdles++;
    g_vkr.stats.fenceWaitNs += vkr_now_ns() - t0;
    vkr_swapchain_destroy();
    vkr_acq_reset_all();
    vkDestroySurfaceKHR(g_vkr.instance, g_vkr.surface, NULL);
    g_vkr.surface = VK_NULL_HANDLE;
    VKR_LOG("surface released; the device is kept");
}

void rhi_release_surface(void)
{
    vkr_swap_lock();
    vkr_release_surface();
    vkr_swap_unlock();
}

static bool vkr_recreate_surface(void *window)
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

bool rhi_recreate_surface(void *window)
{
    vkr_swap_lock();
    const bool ok = vkr_recreate_surface(window);
    vkr_swap_unlock();
    return ok;
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
 * when it has one, else the window's pixel size; and its turn
 * (vkr_turns_for: the surface's transform now, else the swapchain's) */
static bool vkr_swap_target(uint32_t *w, uint32_t *h, uint32_t *turns)
{
    int ww = (int)g_vkr.swapWidth, wh = (int)g_vkr.swapHeight;
#ifdef ICO_RHI_HAVE_SDL
    SDL_GetWindowSizeInPixels((SDL_Window *)g_vkr.window, &ww, &wh);
#endif
    *w = ww > 0 ? (uint32_t)ww : 0;
    *h = wh > 0 ? (uint32_t)wh : 0;
    *turns = g_vkr.swapTurns;
    VkSurfaceCapabilitiesKHR caps;
    if (g_vkr.surface &&
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vkr.phys, g_vkr.surface, &caps) == VK_SUCCESS) {
        if (caps.currentExtent.width != UINT32_MAX) {
            *w = caps.currentExtent.width;
            *h = caps.currentExtent.height;
        }
        *turns = vkr_turns_for(&caps, *w, *h);
    }
    return *w != 0 && *h != 0;
}

/* a swapchain made now would differ from this one: another size, or
 * another turn (an Android phone turned over, 90 to 270: the same size,
 * and Android's present reports suboptimal until the swapchain follows) */
static bool vkr_swap_differs(uint32_t w, uint32_t h, uint32_t turns)
{
    return w != g_vkr.swapWidth || h != g_vkr.swapHeight || turns != g_vkr.swapTurns;
}

/* how many swapchains were made again for a turn alone (the log's count) */
static uint64_t s_turnRebuilds;

static void vkr_note_turn_rebuild(uint32_t w, uint32_t h, uint32_t turns, const char *where)
{
    if (w != g_vkr.swapWidth || h != g_vkr.swapHeight || turns == g_vkr.swapTurns) {
        return;
    }
    const uint64_t n = ++s_turnRebuilds;
    if (n <= 8u || n % 100u == 0u) {
        VKR_LOG("the surface turned (%u to %u degrees) at %s: the swapchain made again (%llu "
                "times so far)",
                g_vkr.swapTurns * 90u, turns * 90u, where, (unsigned long long)n);
    }
}

static bool vkr_resize_swapchain(uint32_t width, uint32_t height, bool vsync)
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

bool rhi_resize_swapchain(uint32_t width, uint32_t height, bool vsync)
{
    vkr_swap_lock();
    const bool ok = vkr_resize_swapchain(width, height, vsync);
    vkr_swap_unlock();
    return ok;
}

void rhi_prefer_mailbox(bool on)
{
    s_preferMailbox = on;
}

void rhi_present_fifo_fallback(bool on)
{
    s_fifoFallback = on;
}

bool rhi_present_fifo_fallback_on(void)
{
    return s_fifoFallback;
}

void rhi_present_fifo_start(RhiFifoStartFn eligible)
{
    s_fifoStart = eligible;
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

uint32_t rhi_swapchain_rotation(void)
{
    if (g_vkr.swapchain) {
        return g_vkr.swapTurns;
    }
    if (g_vkr.window || !g_vkr.device) {
        return 0; /* a window between swapchains, or no device */
    }
    /* headless: the tests' twin of a turned present (rd_prerotate.c draws
     * the picture turned into a texture of its own and reads it back
     * turned back); 90, 180 or 270 */
    static int s_fake = -1;
    if (s_fake < 0) {
        const char *e = getenv("ICO_VK_FAKE_TRANSFORM");
        s_fake = 0;
        if (e != NULL) {
            s_fake = strcmp(e, "90") == 0    ? 1
                     : strcmp(e, "180") == 0 ? 2
                     : strcmp(e, "270") == 0 ? 3
                                             : 0;
        }
        if (s_fake != 0) {
            VKR_LOG("ICO_VK_FAKE_TRANSFORM=%s: the headless present is turned %d degrees and "
                    "read back turned back (the twin of a pre-rotated swapchain)",
                    e, s_fake * 90);
        }
    }
    return (uint32_t)s_fake;
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
 * size, as vkr_swap_target) is asked at every present for the first 15 s
 * after a swapchain is made, then once a second; a different size rebuilds
 * the swapchain as a suboptimal present with a size change does, and the
 * renderer's output follows it at the next acquire
 * (rd__output_follow_swapchain).  A present on the submit thread asks the
 * same and leaves the rebuild to the next acquire (VKR_DEFER_POLL). */
void rhi_surface_poll_restart(void)
{
    s_swapMadeNs = vkr_now_ns();
}

/* the poll's rebuild at the size (and turn) it found */
static void vkr_poll_resize(uint32_t w, uint32_t h, uint32_t turns)
{
    if (!vkr_swap_differs(w, h, turns)) {
        return;
    }
    vkr_note_turn_rebuild(w, h, turns, "the surface poll");
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

static void vkr_poll_surface(bool defer)
{
    if (!vkr_poll_surface_on() || !g_vkr.swapchain) {
        return;
    }
    const uint64_t now = vkr_now_ns();
    if (now - s_swapMadeNs >= VKR_POLL_ALWAYS_NS && now - s_pollNs < VKR_POLL_PERIOD_NS) {
        return;
    }
    s_pollNs = now;
    uint32_t w = 0, h = 0, turns = 0;
    if (!vkr_swap_target(&w, &h, &turns) || !vkr_swap_differs(w, h, turns)) {
        return;
    }
    if (w == s_pollStuckW && h == s_pollStuckH && g_vkr.swapWidth == s_pollMadeW &&
        g_vkr.swapHeight == s_pollMadeH) {
        return; /* this size was tried and the surface's limits gave another */
    }
    if (defer) {
        g_vkr.deferred |= VKR_DEFER_POLL;
        g_vkr.deferW = w;
        g_vkr.deferH = h;
        g_vkr.deferTurns = turns;
        return;
    }
    vkr_poll_resize(w, h, turns);
}

/* The swapchain's work a present on the submit thread left (VkrState.
 * deferred), done now: at an acquire, the swapchain's lock held and the
 * submit thread idle (rhi_acquire_backbuffer).  The same rebuilds the
 * present makes on the thread that records */
static void vkr_take_deferred(void)
{
    const uint32_t d = g_vkr.deferred;
    if (d == 0) {
        return;
    }
    g_vkr.deferred = 0;
    const uint64_t n = ++g_vkr.deferredCount;
    if (n <= 8u || n % 100u == 0u) {
        VKR_LOG("a present on the submit thread found %s: the swapchain made again at the next "
                "acquire (%llu times so far)",
                (d & VKR_DEFER_SURFACE_LOST) ? "the surface lost"
                : (d & VKR_DEFER_REBUILD)    ? "the swapchain out of date"
                : (d & VKR_DEFER_SIZE)       ? "another size (suboptimal)"
                                             : "another surface size (the poll)",
                (unsigned long long)n);
    }
    if (d & VKR_DEFER_SURFACE_LOST) {
        vkr_surface_lost("present");
        return;
    }
    if (d & VKR_DEFER_REBUILD) {
        int w = (int)g_vkr.swapWidth, h = (int)g_vkr.swapHeight;
#ifdef ICO_RHI_HAVE_SDL
        SDL_GetWindowSizeInPixels((SDL_Window *)g_vkr.window, &w, &h);
#endif
        rhi_resize_swapchain((uint32_t)w, (uint32_t)h, g_vkr.vsync);
        return;
    }
    if (d & VKR_DEFER_SIZE) {
        uint32_t w = 0, h = 0, turns = 0;
        if (vkr_swap_target(&w, &h, &turns) && vkr_swap_differs(w, h, turns)) {
            vkr_note_turn_rebuild(w, h, turns, "a suboptimal present");
            rhi_resize_swapchain(w, h, g_vkr.vsync);
        }
        return;
    }
    if (d & VKR_DEFER_POLL) {
        vkr_poll_resize(g_vkr.deferW, g_vkr.deferH, g_vkr.deferTurns);
    }
}

/* The images acquired by slots other than the recording thread's and not
 * yet presented: the submit thread's job still holds them (an image of a
 * slot with no such job was given up: a replay that failed after its
 * acquire, adopted by the next acquire, vkr_acquire_backbuffer) */
static uint32_t vkr_acquired_ahead(const VkrFrame *cur)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        const VkrFrame *o = &g_vkr.frames[i];
        if (o != cur && o->acq.acquired && rhi_slot_guard_pending(i)) {
            n++;
        }
    }
    return n;
}

/* An acquire may go on while the submit thread still holds n images: the
 * spec lets an acquire without a timeout run while no more images are
 * acquired than swapImageCount - minImageCount (the swapchain's one over the
 * minimum, vkr_swapchain_create, makes that 1; the FIFO fallback's exact
 * minimum makes it 0) */
static bool vkr_acquire_ahead_ok(const VkrFrame *cur)
{
    const uint32_t n = vkr_acquired_ahead(cur);
    return n == 0 || (g_vkr.swapImageCount > g_vkr.swapMinImages &&
                      n <= g_vkr.swapImageCount - g_vkr.swapMinImages);
}

/* *lost: the surface went away (the caller makes it again, the submit
 * thread idle first).  ahead: the submit thread still holds the picture
 * before's image: the acquire does not wait for an image (a timeout of 0),
 * so it never holds the swapchain's lock, and with it that present, while
 * the presentation engine frees one; *busy: none was free (the caller
 * waits for the present before and acquires again) */
static RhiTexture vkr_acquire_backbuffer(bool ahead, bool *lost, bool *busy)
{
    RhiTexture out = {0};
    __atomic_store_n(&s_presentShown, false, __ATOMIC_RELAXED); /* until its present shows it */
    if (!g_vkr.swapchain) {
        return out;
    }
    VkrFrame *f = vkr_cur_frame();
    if (f->acq.acquired) {
        out.id = g_vkr.swapTextures[f->acq.image];
        return out;
    }
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        VkrFrame *o = &g_vkr.frames[i];
        if (o != f && o->acq.acquired && !rhi_slot_guard_pending(i)) {
            /* given up by a replay of slot o that failed after its acquire
             * (no submit thread's job will present it): this slot takes it,
             * with the semaphore its acquire signalled, as the one slot
             * took its own image again before the slots were apart */
            const VkSemaphore sem = f->acquireSem;
            f->acquireSem = o->acquireSem;
            o->acquireSem = sem;
            f->acq = o->acq;
            memset(&o->acq, 0, sizeof(o->acq));
            out.id = g_vkr.swapTextures[f->acq.image];
            return out;
        }
    }
    uint32_t idx = 0;
    const uint64_t t0 = vkr_now_ns();
    VkResult r = vkAcquireNextImageKHR(g_vkr.device, g_vkr.swapchain, ahead ? 0u : UINT64_MAX,
                                       f->acquireSem, VK_NULL_HANDLE, &idx);
    g_vkr.stats.acquireNs += vkr_now_ns() - t0;
    if (ahead && (r == VK_NOT_READY || r == VK_TIMEOUT)) {
        *busy = true; /* nothing signalled: the semaphore stays as it was */
        return out;
    }
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        return out; /* rd_present recreates via rhi_resize_swapchain */
    }
    if (r == VK_ERROR_SURFACE_LOST_KHR) {
        /* this frame is skipped (the semaphore was not signalled; the
         * caller's retry acquires on the new swapchain) */
        *lost = true;
        return out;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        VKR_CHECK(r);
        return out;
    }
    f->acq.image = idx;
    f->acq.acquired = true;
    f->acq.signalled = false;
    f->acq.waitPending = true;
    f->acq.seq = ++g_vkr.acqSeq;
    out.id = g_vkr.swapTextures[idx];
    return out;
}

/* The recording thread's slot's image.  With the submit thread presenting
 * the picture before (RhiLimits.slotOverlap) it may be acquired before that
 * present, when the swapchain has an image to spare; otherwise, and when
 * that thread's present left the swapchain to be made again, the call
 * guard's wait first (rhi_call_guard_settle: the submit thread idle) */
RhiTexture rhi_acquire_backbuffer(void)
{
    vkr_swap_lock();
    if (g_vkr.deferred != 0 || !vkr_acquire_ahead_ok(vkr_cur_frame())) {
        vkr_swap_unlock();
        rhi_call_guard_settle();
        vkr_swap_lock();
        vkr_take_deferred();
    }
    bool lost = false, busy = false;
    RhiTexture out = vkr_acquire_backbuffer(vkr_acquired_ahead(vkr_cur_frame()) != 0, &lost, &busy);
    if (busy) {
        /* no image free beside the picture before's: after its present */
        static uint64_t s_aheadBusy;
        const uint64_t n = ++s_aheadBusy;
        if (n <= 8u || n % 100u == 0u) {
            VKR_LOG("an acquire beside the submit thread found no image free: it waits for the "
                    "present before (%llu times so far)",
                    (unsigned long long)n);
        }
        vkr_swap_unlock();
        rhi_call_guard_settle();
        vkr_swap_lock();
        vkr_take_deferred();
        out = vkr_acquire_backbuffer(false, &lost, &busy);
    }
    if (lost) {
        /* the surface made again waits for the device: the submit thread
         * idle first, as for the deferred rebuilds above */
        vkr_swap_unlock();
        rhi_call_guard_settle();
        vkr_swap_lock();
        vkr_surface_lost("acquire");
    }
    vkr_swap_unlock();
    return out;
}

bool rhi_present_shown(void)
{
    return __atomic_load_n(&s_presentShown, __ATOMIC_RELAXED);
}

/* rhi.h: the acquire's wait (vkAcquireNextImageKHR's) is the acquire's
 * own: nothing to make ahead of it */
bool rhi_wait_present_room(void)
{
    return false;
}

/* The slot whose image the present shows: the oldest acquired (with the
 * submit thread, the picture before the one the render thread acquired
 * since; vkr_acquire_backbuffer adopts an image given up, so an older one
 * is never left behind); NULL when none */
static VkrFrame *vkr_present_slot(void)
{
    VkrFrame *best = NULL;
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        VkrFrame *o = &g_vkr.frames[i];
        if (o->acq.acquired && (best == NULL || o->acq.seq < best->acq.seq)) {
            best = o;
        }
    }
    return best;
}

/* The calling thread's last present timed apart (rhi.h rhi_present_split):
 * the lock's wait, vkQueuePresentKHR and the surface poll after it; valid
 * once the lock was taken (called: the call made too) */
static _Thread_local RhiPresentSplit t_split;
static _Thread_local bool t_splitValid;

bool rhi_present_split(RhiPresentSplit *out)
{
    *out = t_split;
    if (!t_splitValid) {
        memset(out, 0, sizeof(*out));
    }
    return t_splitValid;
}

/* defer: the submit thread's present (rhi_slot_guard_worker with the
 * slots apart): what would make the swapchain or the surface again is left
 * to the next acquire (vkr_take_deferred) */
static void vkr_present(bool defer)
{
    __atomic_store_n(&s_presentShown, false, __ATOMIC_RELAXED);
    VkrFrame *f = vkr_present_slot();
    if (!g_vkr.swapchain || f == NULL) {
        return;
    }
    /* the presenting list's submit signalled the semaphore already
     * (rhi_submit); otherwise an empty batch after the slot's work */
    if (!f->acq.signalled && !vkr_submit_present_signal(f)) {
        return;
    }
    VkPresentInfoKHR pi = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &g_vkr.renderDone[f->acq.image],
        .swapchainCount = 1,
        .pSwapchains = &g_vkr.swapchain,
        .pImageIndices = &f->acq.image,
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
    t_split.callNs = vkr_now_ns() - t0;
    t_split.called = true;
    vkr_late_add(&g_vkr.late.presentNs, t_split.callNs);
    vkr_late_add(&g_vkr.late.presents, 1u);
    f->acq.acquired = false;
    f->acq.signalled = false;
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
        if (defer) {
            g_vkr.deferred |= VKR_DEFER_REBUILD;
        } else {
            int w = (int)g_vkr.swapWidth, h = (int)g_vkr.swapHeight;
#ifdef ICO_RHI_HAVE_SDL
            SDL_GetWindowSizeInPixels((SDL_Window *)g_vkr.window, &w, &h);
#endif
            rhi_resize_swapchain((uint32_t)w, (uint32_t)h, g_vkr.vsync);
        }
    } else if (r == VK_SUBOPTIMAL_KHR) {
        /* Android only: the image was shown.  Recreated only when the size
         * or the turn changed: Android reports suboptimal at every present
         * whose swapchain's preTransform is not the surface's transform, so
         * with identity on a turned display ([dev] prerotate = false, or a
         * turn the images could not take) at every present, where a
         * recreation would change nothing; pre-rotated, when the phone is
         * turned over (90 to 270), which a swapchain in the new turn ends */
        uint32_t w = 0, h = 0, turns = 0;
        if (vkr_swap_target(&w, &h, &turns) && vkr_swap_differs(w, h, turns)) {
            if (defer) {
                g_vkr.deferred |= VKR_DEFER_SIZE;
            } else {
                vkr_note_turn_rebuild(w, h, turns, "a suboptimal present");
                rhi_resize_swapchain(w, h, g_vkr.vsync);
            }
        }
    } else if (r == VK_ERROR_SURFACE_LOST_KHR) {
        if (defer) {
            g_vkr.deferred |= VKR_DEFER_SURFACE_LOST;
        } else {
            vkr_surface_lost("present");
        }
    } else if (r != VK_SUCCESS) {
        VKR_CHECK(r);
    }
    /* an image queued for the display (a suboptimal one too: it is shown) */
    const bool shown = r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR;
    __atomic_store_n(&s_presentShown, shown, __ATOMIC_RELAXED);
    if (presentId != 0 && shown && !rebuild) {
        vkr_pw_posted(presentId);
    }
    if (!rebuild && (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)) {
        const uint64_t p0 = vkr_now_ns();
        vkr_poll_surface(defer); /* a size change the present did not report */
        t_split.pollNs = vkr_now_ns() - p0;
    }
}

/* The oldest acquired image presented (vkr_present_slot).  On the submit
 * thread with the slots apart (vk_device.c g_vkr.slotOverlap) a rebuild it
 * asks for waits for the next acquire */
void rhi_present(void)
{
    const uint64_t l0 = vkr_now_ns();
    vkr_swap_lock();
    memset(&t_split, 0, sizeof(t_split));
    t_split.lockNs = vkr_now_ns() - l0;
    t_splitValid = true; /* the lock's wait kept even when no call follows */
    vkr_present(g_vkr.slotOverlap && rhi_slot_guard_worker());
    vkr_swap_unlock();
}
