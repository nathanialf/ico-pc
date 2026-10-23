/* gl_glsl.h: the OpenGL backend's GLSL text.
 *
 * The shader table (port/shaders, shaders_gen.h) holds each entry's GLSL
 * zlib-deflated (tools/spirv_to_glsl.py translates the SPIR-V, embed_shaders.py
 * deflates it); the backend inflates it when it creates a shader.  No GL
 * header: the unit runs in rhi_gl_glsl_test on every host.
 *
 * docs/port/SHADERS.md, "GLSL for the OpenGL backend". */
#ifndef PORT_RHI_GL_GL_GLSL_H
#define PORT_RHI_GL_GL_GLSL_H

#include <stddef.h>

/* The text of a deflated GLSL stream (RhiShaderDesc.glsl, glslSize and
 * textSize), as a malloc'd NUL-terminated string the caller frees; NULL for
 * no stream, a stream that is not valid zlib, or one that inflates to other
 * than `textLen` bytes (a stale table), or no memory. */
char *rhi_gl_inflate_glsl(const void *deflated, size_t len, size_t textLen);

#endif
