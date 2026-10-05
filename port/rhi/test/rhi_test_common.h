/* rhi_test_common.h: the exact-texel RHI checks, run against any backend
 * (port/rhi/test/rhi_test_common.c).  rhi_vk_test.c runs them on Vulkan,
 * rhi_d3d12_test.c on D3D12; the expected values are the same, so the two
 * backends agree bit for bit on the integer paths. */
#ifndef PORT_RHI_TEST_RHI_TEST_COMMON_H
#define PORT_RHI_TEST_RHI_TEST_COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct RhiTestConfig {
    const char *backend; /* rhi_CreateBackend name: "vulkan", "d3d12" */
    const char *label;   /* for messages: "rhi_vk_test", "rhi_d3d12_test (WARP)" */
    bool debugLayers;    /* RhiDeviceDesc.debugLayers */
    /* Called after rhi_Init; false skips the run (exit code 77).  NULL:
     * always run. */
    bool (*accept)(void);
    /* Validation / debug layer errors so far (read after rhi_Shutdown);
     * NULL: none counted. */
    uint32_t (*validationErrors)(void);
} RhiTestConfig;

/* Where messages go besides stdout (NULL: stdout only). */
extern FILE *g_rhiTestLog;
void rhi_test_Log(const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

/* The eight cells, the RGBA8_UINT target, R8 copies, a copied device
 * vertex buffer and depth readback, over three frames.  Returns 0 (all
 * passed), 1 (a mismatch or error) or 77 (no device, or accept said no). */
int rhi_test_RunCells(const RhiTestConfig *cfg);

#endif /* PORT_RHI_TEST_RHI_TEST_COMMON_H */
