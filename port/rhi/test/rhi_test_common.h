/* rhi_test_common.h: the exact-texel RHI checks, run against any backend
 * (port/rhi/test/rhi_test_common.c).  rhi_vk_test.c runs them on Vulkan,
 * rhi_d3d12_test.c on D3D12, rhi_d3d11_test.c on Direct3D 11 and rhi_gl_test.c
 * on OpenGL; the expected values are the same, so the backends agree bit for bit
 * on the integer paths. */
#ifndef PORT_RHI_TEST_RHI_TEST_COMMON_H
#define PORT_RHI_TEST_RHI_TEST_COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct RhiTestConfig {
    const char *backend; /* rhi_create_backend name: "vulkan", "d3d12", "d3d11", "opengl" */
    const char *label;   /* for messages: "rhi_vk_test", "rhi_d3d12_test (WARP)" */
    bool debugLayers;    /* RhiDeviceDesc.debugLayers */
    /* Called after rhi_init; false skips the run (exit code 77).  NULL:
     * always run. */
    bool (*accept)(void);
    /* Validation / debug layer errors so far (read after rhi_shutdown);
     * NULL: none counted. */
    uint32_t (*validationErrors)(void);
} RhiTestConfig;

/* Where messages go besides stdout (NULL: stdout only). */
extern FILE *g_rhiTestLog;
void rhi_test_log(const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

/* The eight cells, the RGBA8_UINT target, R8 copies, a copied device
 * vertex buffer and depth readback, over three frames, then (texture
 * packs) BC1 and BC3 textures with levels under one block, when the
 * device has BC (RhiLimits.bcTextures), multisampling, and the
 * destination read of each kind the device reports (RhiDstRead).  Returns 0 (all
 * passed), 1 (a mismatch or error) or 77 (no device, or accept said no). */
int rhi_test_run_cells(const RhiTestConfig *cfg);

/* The vertex shaders' stores (rhi_test_stores.c: RhiLimits.vertexStores,
 * RHI_BIND_STORAGE_BUFFER_RW, RhiPipelineDesc.noRaster and the store
 * bracket): brings the backend up and down itself.  Returns 0 (the points'
 * words read back), 1 (a mismatch, an error, or a bracket taken where no
 * stores are reported) or 77 (no device, or no vertex stores: the bracket
 * refused). */
int rhi_test_vertex_stores(const RhiTestConfig *cfg);

/* The compute shaders' stores (rhi_test_compute.c: RhiLimits.computeStores,
 * RhiPipelineDesc.compute, rhi_cmd_dispatch and the compute bracket; with
 * vertex stores too, the bind-point rule): brings the backend up and down
 * itself.  Returns 0 (the words read back), 1 (a mismatch, an error, or a
 * bracket taken or a dispatch counted where no compute stores are
 * reported) or 77 (no device, or no compute stores: the bracket refused). */
int rhi_test_compute_stores(const RhiTestConfig *cfg);

/* The native-handle surface of rhi_native.h on the running backend
 * (rhi_test_native.c); needs the device up.  Returns the failure count. */
int rhi_test_native_surface(const char *label);

#endif /* PORT_RHI_TEST_RHI_TEST_COMMON_H */
