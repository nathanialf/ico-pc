/* rhi_vk_present_mode_test.c: the swapchain's present mode
 * (vk/vk_present_mode.c) over the mode sets drivers offer.
 *
 * vkr_choose_present_mode is pure, so it is compiled into this test on its
 * own: no device, no surface, no Vulkan call.  The rules: with vsync,
 * MAILBOX when preferred and offered, else FIFO; without, IMMEDIATE when
 * offered, else MAILBOX, else FIFO.  Mesa's X11 WSI (the Deck's RADV, also
 * under XWayland) offers all four of the common modes, where vsync off
 * must pick IMMEDIATE: MAILBOX would look the same as vsync on.
 *
 * The FIFO fallback (forceFifo): FIFO over Android's {mailbox, fifo} with
 * vsync off, over Mesa's four modes, and with vsync on and mailbox
 * preferred; with forceFifo off every case above keeps its answer (expect
 * asks both ways: off for the case's answer, on for FIFO). */
#include "vk/vk_internal.h"
#include <stdio.h>
#include <string.h>

#define COUNT_OF(a) ((uint32_t)(sizeof(a) / sizeof((a)[0])))

static int s_failures;

static void expect(const char *what, const VkPresentModeKHR *modes, uint32_t n, bool vsync,
                   bool preferMailbox, VkPresentModeKHR want)
{
    const VkPresentModeKHR got =
        vkr_choose_present_mode(modes, n, vsync, preferMailbox, false, false);
    const char *g = vkr_present_mode_name(got), *w = vkr_present_mode_name(want);

    if (got != want) {
        printf("FAIL %s, vsync %s, mailbox %s: %s, want %s\n", what, vsync ? "on" : "off",
               preferMailbox ? "preferred" : "not preferred", g ? g : "?", w ? w : "?");
        s_failures++;
    }
    /* the same set under the FIFO fallback: FIFO whatever the case wanted */
    const VkPresentModeKHR forced =
        vkr_choose_present_mode(modes, n, vsync, preferMailbox, false, true);
    if (forced != VK_PRESENT_MODE_FIFO_KHR) {
        const char *f = vkr_present_mode_name(forced);
        printf("FAIL %s, vsync %s, mailbox %s, fifo fallback: %s, want fifo\n", what,
               vsync ? "on" : "off", preferMailbox ? "preferred" : "not preferred", f ? f : "?");
        s_failures++;
    }
}

/* the FIFO fallback over the sets the plan names: Android's stock Adreno
   driver offers {mailbox, fifo}, vsync off takes mailbox, forced takes FIFO;
   Mesa's four modes forced take FIFO; vsync on with mailbox preferred
   forced takes FIFO; FIFO latest ready asked and offered still gives way */
static void fifoFallback(void)
{
    static const VkPresentModeKHR android[] = {VK_PRESENT_MODE_MAILBOX_KHR,
                                               VK_PRESENT_MODE_FIFO_KHR};
    static const VkPresentModeKHR mesa[] = {VK_PRESENT_MODE_IMMEDIATE_KHR,
                                            VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR,
                                            VK_PRESENT_MODE_FIFO_RELAXED_KHR};
    static const VkPresentModeKHR nv[] = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR,
                                          VK_PRESENT_MODE_IMMEDIATE_KHR,
                                          VK_PRESENT_MODE_FIFO_LATEST_READY_EXT};

    const struct {
        const VkPresentModeKHR *m;
        uint32_t n;
        bool vsync, mailbox, latest, force;
        VkPresentModeKHR want;
    } c[] = {
        {android, 2, false, false, false, false, VK_PRESENT_MODE_MAILBOX_KHR},
        {android, 2, false, false, false, true, VK_PRESENT_MODE_FIFO_KHR},
        {android, 2, false, true, false, true, VK_PRESENT_MODE_FIFO_KHR},
        {mesa, 4, false, false, false, true, VK_PRESENT_MODE_FIFO_KHR},
        {mesa, 4, true, true, false, false, VK_PRESENT_MODE_MAILBOX_KHR},
        {mesa, 4, true, true, false, true, VK_PRESENT_MODE_FIFO_KHR},
        {nv, 4, true, false, true, false, VK_PRESENT_MODE_FIFO_LATEST_READY_EXT},
        {nv, 4, true, false, true, true, VK_PRESENT_MODE_FIFO_KHR},
    };

    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        const VkPresentModeKHR got = vkr_choose_present_mode(c[i].m, c[i].n, c[i].vsync,
                                                             c[i].mailbox, c[i].latest, c[i].force);
        if (got != c[i].want) {
            printf("FAIL fifo fallback case %u: got %d, want %d\n", i, (int)got, (int)c[i].want);
            s_failures++;
        }
    }
}

/* FIFO latest ready only when asked for (ICO_VK_FIFO_LATEST_READY) and
   offered, where FIFO would be taken; mailbox preferred still wins; vsync
   off never takes it */
static void latestReady(void)
{
    static const VkPresentModeKHR nv[] = {
        VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR, VK_PRESENT_MODE_MAILBOX_KHR,
        VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_LATEST_READY_EXT};
    static const VkPresentModeKHR plain[] = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR};

    const struct {
        const VkPresentModeKHR *m;
        uint32_t n;
        bool vsync, mailbox, latest;
        VkPresentModeKHR want;
    } c[] = {
        {nv, 5, true, false, true, VK_PRESENT_MODE_FIFO_LATEST_READY_EXT},
        {nv, 5, true, false, false, VK_PRESENT_MODE_FIFO_KHR},
        {nv, 5, true, true, true, VK_PRESENT_MODE_MAILBOX_KHR},
        {nv, 5, false, false, true, VK_PRESENT_MODE_IMMEDIATE_KHR},
        {plain, 2, true, false, true, VK_PRESENT_MODE_FIFO_KHR},
    };

    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        const VkPresentModeKHR got =
            vkr_choose_present_mode(c[i].m, c[i].n, c[i].vsync, c[i].mailbox, c[i].latest, false);
        if (got != c[i].want) {
            printf("FAIL latest ready case %u: got %d, want %d\n", i, (int)got, (int)c[i].want);
            s_failures++;
        }
    }
    if (strcmp(vkr_present_mode_name(VK_PRESENT_MODE_FIFO_LATEST_READY_EXT), "fifo latest ready")) {
        printf("FAIL the name of fifo latest ready\n");
        s_failures++;
    }
}

int main(void)
{
    latestReady();
    fifoFallback();
    /* Mesa (RADV, lavapipe on X11): every common mode */
    static const VkPresentModeKHR mesa[] = {VK_PRESENT_MODE_IMMEDIATE_KHR,
                                            VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR,
                                            VK_PRESENT_MODE_FIFO_RELAXED_KHR};
    expect("mesa", mesa, COUNT_OF(mesa), true, false, VK_PRESENT_MODE_FIFO_KHR);
    expect("mesa", mesa, COUNT_OF(mesa), true, true, VK_PRESENT_MODE_MAILBOX_KHR);
    expect("mesa", mesa, COUNT_OF(mesa), false, false, VK_PRESENT_MODE_IMMEDIATE_KHR);
    expect("mesa", mesa, COUNT_OF(mesa), false, true, VK_PRESENT_MODE_IMMEDIATE_KHR);

    /* NVIDIA-like ordering: FIFO first, immediate after mailbox */
    static const VkPresentModeKHR nv[] = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR,
                                          VK_PRESENT_MODE_IMMEDIATE_KHR};
    expect("nvidia", nv, COUNT_OF(nv), true, false, VK_PRESENT_MODE_FIFO_KHR);
    expect("nvidia", nv, COUNT_OF(nv), true, true, VK_PRESENT_MODE_MAILBOX_KHR);
    expect("nvidia", nv, COUNT_OF(nv), false, false, VK_PRESENT_MODE_IMMEDIATE_KHR);
    expect("nvidia", nv, COUNT_OF(nv), false, true, VK_PRESENT_MODE_IMMEDIATE_KHR);

    /* no immediate (Wayland compositors, some Windows drivers): mailbox
       without vsync */
    static const VkPresentModeKHR noImm[] = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR};
    expect("fifo+mailbox", noImm, COUNT_OF(noImm), false, false, VK_PRESENT_MODE_MAILBOX_KHR);
    expect("fifo+mailbox", noImm, COUNT_OF(noImm), true, false, VK_PRESENT_MODE_FIFO_KHR);

    /* FIFO only (the one mode the spec guarantees): FIFO in every case */
    static const VkPresentModeKHR fifo[] = {VK_PRESENT_MODE_FIFO_KHR};
    for (int v = 0; v < 2; v++) {
        for (int m = 0; m < 2; m++) {
            expect("fifo only", fifo, COUNT_OF(fifo), v != 0, m != 0, VK_PRESENT_MODE_FIFO_KHR);
        }
    }
    /* nothing offered (a failed query): FIFO */
    expect("empty", NULL, 0, false, true, VK_PRESENT_MODE_FIFO_KHR);

    /* a list longer than the 16 the old picker kept, the wanted modes past
       the end of that: no cap, every entry is looked at */
    VkPresentModeKHR many[40];
    for (uint32_t i = 0; i < COUNT_OF(many); i++) {
        many[i] = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
    }
    many[0] = VK_PRESENT_MODE_FIFO_KHR;
    many[30] = VK_PRESENT_MODE_MAILBOX_KHR;
    many[39] = VK_PRESENT_MODE_IMMEDIATE_KHR;
    expect("40 modes", many, COUNT_OF(many), false, false, VK_PRESENT_MODE_IMMEDIATE_KHR);
    expect("40 modes", many, COUNT_OF(many), true, true, VK_PRESENT_MODE_MAILBOX_KHR);
    expect("39 modes", many, 39, false, false, VK_PRESENT_MODE_MAILBOX_KHR);

    /* the names the logs and rhi_present_mode_name use */
    static const struct {
        VkPresentModeKHR m;
        const char *name;
    } names[] = {{VK_PRESENT_MODE_IMMEDIATE_KHR, "immediate"},
                 {VK_PRESENT_MODE_MAILBOX_KHR, "mailbox"},
                 {VK_PRESENT_MODE_FIFO_KHR, "fifo"},
                 {VK_PRESENT_MODE_FIFO_RELAXED_KHR, "fifo_relaxed"}};

    for (uint32_t i = 0; i < COUNT_OF(names); i++) {
        const char *n = vkr_present_mode_name(names[i].m);
        if (n == NULL || strcmp(n, names[i].name) != 0) {
            printf("FAIL name of mode %d: %s, want %s\n", (int)names[i].m, n ? n : "NULL",
                   names[i].name);
            s_failures++;
        }
    }
    if (vkr_present_mode_name(VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR) != NULL) {
        printf("FAIL a shared mode has a name\n");
        s_failures++;
    }

    /* the display's timing of presents (vkr_present_wait_wanted): off
       unless ICO_VK_PRESENT_WAIT=1 asks for it, even where the driver
       offers it (when it was used whenever offered, an RTX 3090 on Windows
       showed nothing usable in mailbox mode); and only with the
       extensions and a thread */
    static const struct {
        bool offered;
        const char *asked;
        bool thread, want;
    } pw[] = {
        {true, NULL, true, false}, {true, "", true, false},   {true, "0", true, false},
        {true, "1", true, true},   {true, "yes", true, true}, {false, "1", true, false},
        {true, "1", false, false}, {true, "00", true, true},
    };

    for (uint32_t i = 0; i < COUNT_OF(pw); i++) {
        if (vkr_present_wait_wanted(pw[i].offered, pw[i].asked, pw[i].thread) != pw[i].want) {
            printf("FAIL present wait: offered %d, asked %s, thread %d: want %s\n",
                   (int)pw[i].offered, pw[i].asked ? pw[i].asked : "(unset)", (int)pw[i].thread,
                   pw[i].want ? "on" : "off");
            s_failures++;
        }
    }

    if (s_failures) {
        printf("rhi_vk_present_mode_test: %d failures\n", s_failures);
        return 1;
    }
    printf("rhi_vk_present_mode_test: ok\n");
    return 0;
}
