/* rhi_gl.h: test-only extras of the OpenGL backend; port/render never
 * includes it. */
#ifndef PORT_RHI_GL_RHI_GL_H
#define PORT_RHI_GL_RHI_GL_H

#include <stdbool.h>
#include <stdint.h>

/* The next rhi_init reports RhiLimits.dualSourceBlend false whatever the
 * driver offers (the renderer's two-pass blend; ICO_GL_FAKE_NO_DUAL=1 does
 * the same).  Kept across rhi_shutdown. */
void rhi_gl_fake_no_dual(bool on);
/* Errors the driver's debug output (KHR_debug) reported since the last
 * rhi_init with RhiDeviceDesc.debugLayers (still readable after
 * rhi_shutdown): GL_DEBUG_TYPE_ERROR and _UNDEFINED_BEHAVIOR messages. */
uint32_t rhi_gl_debug_error_count(void);
/* The debug output was on for the last rhi_init (a context with
 * KHR_debug, debugLayers asked for). */
bool rhi_gl_debug_active(void);

#endif /* PORT_RHI_GL_RHI_GL_H */
