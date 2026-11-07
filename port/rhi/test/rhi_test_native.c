/* rhi_test_native.c: the native-handle surface of port/rhi/rhi_native.h on
 * whatever backend rhi_test_run_cells is running (every backend): Vulkan and
 * D3D12 hand out their device, queue, command list and a texture's resource;
 * Direct3D 11 and OpenGL offer none of it, every call answering "not
 * offered" with zeroed outputs and a reason for the capability query.  The
 * picture checks around an external stretch are rhi_native_test.c (Vulkan,
 * lavapipe). */
#include "rhi_test_common.h"
#include "rhi.h"
#include "rhi_native.h"
#include <string.h>

static int nativeFail(const char *label, const char *what)
{
    rhi_test_log("FAIL %s: native handles: %s\n", label, what);
    return 1;
}

int rhi_test_native_surface(const char *label)
{
    int fails = 0;
    const RhiBackendKind be = rhi_backend();
    const bool vulkan = be == RHI_BACKEND_VULKAN || be == RHI_BACKEND_METAL; /* MoltenVK */
    const bool offers = vulkan || be == RHI_BACKEND_D3D12;
    const RhiNativeKind kind = vulkan ? RHI_NATIVE_VULKAN : RHI_NATIVE_D3D12;

    RhiNativeDevice d;
    memset(&d, 0x5a, sizeof(d));
    const bool gotDev = rhi_native_device(&d);
    if (gotDev != offers) {
        fails += nativeFail(label, "rhi_native_device answer");
    } else if (offers) {
        if (d.kind != kind || !d.device || !d.queue) {
            fails += nativeFail(label, "device or queue missing");
        }
        if (vulkan && (!d.instance || !d.physicalDevice || !d.getDeviceProcAddr ||
                       !d.getInstanceProcAddr || d.apiVersion == 0)) {
            fails += nativeFail(label, "Vulkan instance, physical device or loader missing");
        }
    } else if (d.kind != RHI_NATIVE_NONE || d.device || d.queue || d.physicalDevice ||
               d.getDeviceProcAddr) {
        fails += nativeFail(label, "rhi_native_device left handles on a backend with none");
    }

    RhiComputeCaps caps;
    memset(&caps, 0x5a, sizeof(caps));
    const bool ok = rhi_device_compute_caps(&caps);
    if (ok != caps.ok) {
        fails += nativeFail(label, "rhi_device_compute_caps return differs from caps.ok");
    }
    if (caps.ok && (!caps.apiVersionOk || !caps.subgroupOk || !caps.storageFormatsOk ||
                    caps.why != NULL || !offers)) {
        fails += nativeFail(label, "caps.ok without its parts");
    }
    if (!caps.ok && !caps.why) {
        fails += nativeFail(label, "caps not ok and no reason");
    }
    if (!offers && (caps.ok || caps.apiVersionOk || caps.subgroupOk || caps.storageFormatsOk ||
                    caps.float16)) {
        fails += nativeFail(label, "compute offered on a backend with none");
    }
    rhi_test_log("%s: native handles %s; external compute %s%s%s\n", label,
                 offers ? "offered" : "not offered", caps.ok ? "offered" : "not offered",
                 caps.ok ? "" : ": ", caps.ok ? "" : caps.why);

    /* an invalid texture and list are refused everywhere, outputs zeroed */
    RhiNativeTexture nt;
    memset(&nt, 0x5a, sizeof(nt));
    if (rhi_native_texture((RhiTexture){0}, &nt) || nt.kind != RHI_NATIVE_NONE || nt.image != 0) {
        fails += nativeFail(label, "a null texture has a native form");
    }
    void *cb = (void *)&nt;
    if (rhi_native_command_list((RhiCommandList){0}, &cb) || cb != NULL) {
        fails += nativeFail(label, "a null command list has a native form");
    }
    if (rhi_cmd_external_begin((RhiCommandList){0})) {
        fails += nativeFail(label, "external begin on a null list");
    }
    rhi_cmd_external_end((RhiCommandList){0});

    if (!offers) {
        /* a real texture and list: still none */
        const RhiTexture t = rhi_create_texture(&(RhiTextureDesc){
            4, 4, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_RENDER_TARGET | RHI_TEX_STORAGE, "native", 0});
        if (t.id && rhi_native_texture(t, &nt)) {
            fails += nativeFail(label, "a texture has a native form here");
        }
        RhiCommandList cl = rhi_begin_commands();
        if (cl.id) {
            if (rhi_native_command_list(cl, &cb) || rhi_cmd_external_begin(cl)) {
                fails += nativeFail(label, "a command list has a native form here");
            }
            rhi_cmd_external_end(cl);
            rhi_end_commands(cl);
            rhi_submit(cl);
        }
        rhi_wait_idle();
        rhi_destroy_texture(t);
    }
    return fails;
}
