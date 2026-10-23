/* shaders_glsl_test.c: every entry of the embedded table that has GLSL,
 * compiled and linked by the OpenGL backend (rhi_create_shader: the
 * deflated text inflated, a separable program of its stage) with the
 * driver's debug output on.  A compile or link failure (the driver's log is
 * the backend's line) or a debug output error fails the run.  The _dstrov
 * builds need a fragment shader interlock extension (their text stops with
 * #error without one): on a device without RhiLimits.fragmentInterlock they
 * are counted as skipped, not compiled.  --list prints each entry's sizes.
 *
 * Exit 0 on success, 1 on a failure, 77 (skipped) when no OpenGL context
 * can be made or the table was built without GLSL (ICO_SHADERS_GLSL=OFF).
 * ctest runs it with LIBGL_ALWAYS_SOFTWARE=1 and SDL_VIDEO_DRIVER=offscreen
 * (Mesa's llvmpipe in the container). */
#include "gl/rhi_gl.h"
#include "rhi.h"
#include "shaders_gen.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#ifndef ICO_SHADERS_NO_GLSL
static int endsWith(const char *s, const char *tail)
{
    const size_t n = strlen(s), k = strlen(tail);
    return n >= k && strcmp(s + n - k, tail) == 0;
}
#endif

int main(int argc, char **argv)
{
    const int list = argc > 1 && strcmp(argv[1], "--list") == 0;
#ifdef ICO_SHADERS_NO_GLSL
    (void)list;
    printf("SKIP shaders_glsl: the table was built without GLSL (ICO_SHADERS_GLSL=OFF)\n");
    return 77;
#else
    if (!rhi_create_backend("opengl")) {
        printf("SKIP shaders_glsl: the OpenGL backend is not linked into this build\n");
        return 77;
    }
    RhiDeviceDesc dd = {NULL, false, true, "shaders_glsl_test"};
    if (!rhi_init(&dd)) {
        printf("SKIP shaders_glsl: no OpenGL context (see messages above)\n");
        return 77;
    }
    const RhiLimits *lim = rhi_limits();
    printf("shaders_glsl: %s, fragment interlock %s, debug output %s\n", rhi_adapter_name(),
           lim->fragmentInterlock ? "yes" : "no", rhi_gl_debug_active() ? "on" : "off");
    unsigned made = 0, failed = 0, skipped = 0, none = 0;
    uint64_t ns = 0;
    for (unsigned i = 0; i < g_icoShaderCount; i++) {
        const IcoShaderBlob *b = &g_icoShaders[i];
        if (!b->glsl) {
            none++;
            continue;
        }
        const int interlock = endsWith(b->name, "_dstrov");
        if (list) {
            printf("  %-28s %-8s glsl %6zu B deflated, %7zu B text%s\n", b->name,
                   b->stage == ICO_SHADER_STAGE_VERTEX ? "vertex" : "fragment", b->glsl_len,
                   b->glsl_text_len, interlock ? ", interlock" : "");
        }
        if (interlock && !lim->fragmentInterlock) {
            skipped++;
            continue;
        }
        RhiShaderDesc d;
        memset(&d, 0, sizeof(d));
        d.stage = b->stage == ICO_SHADER_STAGE_VERTEX ? RHI_STAGE_VERTEX : RHI_STAGE_FRAGMENT;
        d.bytecode = b->glsl;
        d.bytecodeSize = b->glsl_len;
        d.textSize = b->glsl_text_len;
        d.entryPoint = b->entry;
        d.debugName = b->name;
        const uint64_t t0 = SDL_GetTicksNS();
        const RhiShader s = rhi_create_shader(&d);
        ns += SDL_GetTicksNS() - t0;
        if (!s.id) {
            printf("FAIL %s: not compiled (the backend's line above)\n", b->name);
            failed++;
            continue;
        }
        made++;
        rhi_destroy_shader(s);
    }
    rhi_wait_idle();
    rhi_shutdown();
    const uint32_t errors = rhi_gl_debug_error_count();
    printf("shaders_glsl: %u compiled in %.0f ms, %u failed, %u skipped (no interlock), %u "
           "without GLSL, %u debug output errors\n",
           made, (double)ns / 1e6, failed, skipped, none, errors);
    if (made == 0) {
        printf("FAIL shaders_glsl: nothing compiled\n");
        return 1;
    }
    if (failed || errors) {
        return 1;
    }
    printf("shaders_glsl: all checks passed\n");
    return 0;
#endif
}
