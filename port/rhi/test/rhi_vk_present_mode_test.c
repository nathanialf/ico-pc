/* rhi_vk_present_mode_test.c: the swapchain's present mode
 * (vk/vk_present_mode.c, v0.3.1) over the mode sets drivers offer.
 *
 * vkr_choose_present_mode is pure, so it is compiled into this test on its
 * own: no device, no surface, no Vulkan call.  The rules: with vsync,
 * MAILBOX when preferred and offered, else FIFO; without, IMMEDIATE when
 * offered, else MAILBOX, else FIFO.  Mesa's X11 WSI (the Deck's RADV, also
 * under XWayland) offers all four of the common modes, which is where vsync
 * off used to pick MAILBOX and look the same as vsync on. */
#include "vk/vk_internal.h"
#include <stdio.h>
#include <string.h>

#define COUNT_OF(a) ((uint32_t)(sizeof(a) / sizeof((a)[0])))

static int s_failures;

static void expect(const char *what, const VkPresentModeKHR *modes, uint32_t n, bool vsync,
                   bool preferMailbox, VkPresentModeKHR want)
{
    const VkPresentModeKHR got = vkr_choose_present_mode(modes, n, vsync, preferMailbox);
    const char *g = vkr_present_mode_name(got), *w = vkr_present_mode_name(want);

    if (got != want) {
        printf("FAIL %s, vsync %s, mailbox %s: %s, want %s\n", what, vsync ? "on" : "off",
               preferMailbox ? "preferred" : "not preferred", g ? g : "?", w ? w : "?");
        s_failures++;
    }
}

int main(void)
{
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

    if (s_failures) {
        printf("rhi_vk_present_mode_test: %d failures\n", s_failures);
        return 1;
    }
    printf("rhi_vk_present_mode_test: ok\n");
    return 0;
}
