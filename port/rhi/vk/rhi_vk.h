/* rhi_vk.h: Vulkan-backend extras outside rhi.h, for tests and tools only.
 * Nothing in port/render may include it. */
#ifndef PORT_RHI_VK_RHI_VK_H
#define PORT_RHI_VK_RHI_VK_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus

extern "C" {
#endif

/* Number of validation-layer errors reported since rhi_init (still readable
 * after rhi_shutdown).  Always 0 when the layer is not installed. */
uint32_t rhi_vk_validation_error_count(void);

/* For rhi_vk_test's cache cell: the drop vkr_pipeline_cache_init makes.
 * `path` is the cache file as rhi_set_pipeline_cache_path takes it; when its
 * name is pipelines<...>.vkcache every other pipelines*.vkcache and
 * pipelines*.vkcache.tmp in its folder is removed, with one log line each.
 * Returns the number removed; the removed names, one per line, are copied to
 * `log` (when not NULL; logCap bytes). */
uint32_t vkr_cache_drop_others(const char *path, char *log, size_t logCap);

/* For rhi_vk_swapchain_test: the next rhi_present reports
 * `result` (a VkResult value) instead of what the driver returned, once;
 * the image is still presented.  RHI_VK_TEST_SUBOPTIMAL takes the
 * suboptimal path (a recreation only when the size changed),
 * RHI_VK_TEST_SURFACE_LOST the lost-surface path (a new surface and
 * swapchain).  vkr_test_swapchain_creations counts the swapchains created
 * since the program started. */
#define RHI_VK_TEST_SUBOPTIMAL 1000001003      /* VK_SUBOPTIMAL_KHR */
#define RHI_VK_TEST_SURFACE_LOST (-1000000000) /* VK_ERROR_SURFACE_LOST_KHR */
#define RHI_VK_TEST_OUT_OF_DATE (-1000001004)  /* VK_ERROR_OUT_OF_DATE_KHR */
void vkr_test_force_present_result(int32_t result);
uint32_t vkr_test_swapchain_creations(void);
/* 1 while a swapchain retired by a recreation is still held (until the
 * first present on the new one succeeds), else 0 */
uint32_t vkr_test_retired_swapchain(void);
/* the current (or the retired) swapchain's handle, 0 for none (the tests) */
uint64_t vkr_test_swapchain_handle(bool retired);

/* For rhi_vk_swapchain_test: the display's timing of presents
 * (rhi_present_timing) on a stand-in for vkWaitForPresentKHR, which
 * lavapipe's headless surface does not offer: from the next rhi_init the
 * thread runs as with the extensions (without ICO_VK_PRESENT_WAIT), no
 * VkPresentIdKHR is chained, and a wait returns VK_SUCCESS 2 ms after it
 * began (its timeout, if shorter: VK_TIMEOUT).  With
 * vkr_test_fake_present_wait_hang(ms) a wait instead takes ms whatever its
 * timeout (a driver that does not keep it; 0: off).
 * vkr_test_present_wait_misuse counts the waits that ended on another
 * swapchain than they began on (one made again or destroyed during a wait:
 * never, the thread is held out first). */
void vkr_test_fake_present_wait(int on);
void vkr_test_fake_present_wait_hang(uint32_t ms);
uint32_t vkr_test_present_wait_misuse(void);

/* For rhi_vk_test's limits cell (ICO_VK_FAKE_LIMITS): the name of the
 * VkPhysicalDeviceLimits member the last pipeline, allocation or sampler
 * reached ("maxBoundDescriptorSets", ...), NULL when none since rhi_init;
 * and the device's limits as rhi_init left them. */
const char *vkr_test_last_limit(void);
uint32_t vkr_test_limit(const char *name);

/* For rd_present_test's allocation fallback: rhi_create_texture returns a
 * null texture for any texture with more than this many texels (width times
 * height), as a device out of memory does.  0 turns it off. */
void vkr_test_fail_texels_above(uint64_t texels);

/* For rhi_vk_test's render-area cell (vk_cmd.c vkr_pass_area_fits): the
 * render passes since rhi_init whose area was larger than an attachment
 * (each clamped to the attachments).  vkr_test_pass_area_no_assert(true)
 * keeps the debug build's assert quiet while a test makes such a pass on
 * purpose. */
uint32_t vkr_test_pass_area_count(void);
void vkr_test_pass_area_no_assert(bool on);

/* For rhi_vk_test's sync cell (vk_sync.c): the submission sync in use,
 * "timeline" or "fences" (ICO_VK_FAKE_API=1.1), and the submits since
 * rhi_init that waited for their fence slot's earlier submission (the
 * fence ring all in flight; 0 on the timeline path). */
const char *vkr_test_sync_kind(void);
uint32_t vkr_test_sync_recycle_waits(void);

/* For the same cell (vk_device.c): the Vulkan version the device is used
 * at (VK_API_VERSION_1_0 under ICO_VK_FAKE_API=1.0, issue 158), whether
 * its render passes are VkRenderPass objects, and whether the UINT views
 * carry their usage structure (Vulkan 1.1, or VK_KHR_maintenance2 on 1.0),
 * and whether the extended queries reach the device (false under
 * ICO_VK_FAKE_API=1.0bare: a 1.0 loader without
 * VK_KHR_get_physical_device_properties2, review finding 5) */
uint32_t vkr_test_api_version(void);
bool vkr_test_legacy_passes(void);
bool vkr_test_view_usage(void);
bool vkr_test_props2(void);

#ifdef __cplusplus
}

#endif
#endif /* PORT_RHI_VK_RHI_VK_H */
