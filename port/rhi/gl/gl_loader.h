/* gl_loader.h: the OpenGL entry points of the OpenGL backend of port/rhi/rhi.h.
 *
 * The backend links no system GL library: every entry point is fetched with
 * SDL_GL_GetProcAddress once the context is current (gl_loader_load) into
 * the struct the other files call through, glf.BindBuffer(...), with the
 * types and enums of SDL's own headers (SDL_opengl.h, SDL_opengl_glext.h).
 * GL_FUNCS lists them: X(required, type, Name) for glName.  A required entry
 * (OpenGL 4.3 core) that the driver does not give fails the load with a line
 * naming it; an optional one (core in a later version, an extension the
 * backend checks before use) is left NULL. */
#ifndef PORT_RHI_GL_GL_LOADER_H
#define PORT_RHI_GL_GL_LOADER_H

/* the OpenGL 1.x entry points' function types as well (SDL_opengl.h) */
#ifndef SDL_OPENGL_1_FUNCTION_TYPEDEFS
#define SDL_OPENGL_1_FUNCTION_TYPEDEFS 1
#endif
#include <SDL3/SDL_opengl.h>
#include <stdbool.h>

/* clang-format off */
#define GL_FUNCS(X)                                                                      \
    /* state and queries */                                                              \
    X(1, PFNGLGETSTRINGPROC, GetString)                                                  \
    X(1, PFNGLGETSTRINGIPROC, GetStringi)                                                \
    X(1, PFNGLGETINTEGERVPROC, GetIntegerv)                                              \
    X(1, PFNGLGETINTEGER64VPROC, GetInteger64v)                                          \
    X(1, PFNGLGETINTEGERI_VPROC, GetIntegeri_v)                                          \
    X(1, PFNGLGETFLOATVPROC, GetFloatv)                                                  \
    X(1, PFNGLGETERRORPROC, GetError)                                                    \
    X(1, PFNGLENABLEPROC, Enable)                                                        \
    X(1, PFNGLDISABLEPROC, Disable)                                                      \
    X(1, PFNGLENABLEIPROC, Enablei)                                                      \
    X(1, PFNGLDISABLEIPROC, Disablei)                                                    \
    X(1, PFNGLISENABLEDPROC, IsEnabled)                                                  \
    X(1, PFNGLFINISHPROC, Finish)                                                        \
    X(1, PFNGLFLUSHPROC, Flush)                                                          \
    X(1, PFNGLPIXELSTOREIPROC, PixelStorei)                                              \
    X(1, PFNGLHINTPROC, Hint)                                                            \
    /* rasterizer, viewport, output merger */                                            \
    X(1, PFNGLVIEWPORTPROC, Viewport)                                                    \
    X(1, PFNGLSCISSORPROC, Scissor)                                                      \
    X(1, PFNGLDEPTHRANGEFPROC, DepthRangef)                                              \
    X(1, PFNGLCULLFACEPROC, CullFace)                                                    \
    X(1, PFNGLFRONTFACEPROC, FrontFace)                                                  \
    X(1, PFNGLPROVOKINGVERTEXPROC, ProvokingVertex)                                      \
    X(1, PFNGLPOLYGONOFFSETPROC, PolygonOffset)                                          \
    X(1, PFNGLLINEWIDTHPROC, LineWidth)                                                  \
    X(1, PFNGLMINSAMPLESHADINGPROC, MinSampleShading)                                    \
    X(1, PFNGLSAMPLEMASKIPROC, SampleMaski)                                              \
    X(1, PFNGLCLEARBUFFERFVPROC, ClearBufferfv)                                          \
    X(1, PFNGLCLEARBUFFERUIVPROC, ClearBufferuiv)                                        \
    X(1, PFNGLCLEARBUFFERIVPROC, ClearBufferiv)                                          \
    X(1, PFNGLCLEARBUFFERFIPROC, ClearBufferfi)                                          \
    X(1, PFNGLBLENDFUNCSEPARATEPROC, BlendFuncSeparate)                                  \
    X(1, PFNGLBLENDFUNCSEPARATEIPROC, BlendFuncSeparatei)                                \
    X(1, PFNGLBLENDEQUATIONSEPARATEPROC, BlendEquationSeparate)                          \
    X(1, PFNGLBLENDEQUATIONSEPARATEIPROC, BlendEquationSeparatei)                        \
    X(1, PFNGLBLENDCOLORPROC, BlendColor)                                                \
    X(1, PFNGLCOLORMASKPROC, ColorMask)                                                  \
    X(1, PFNGLCOLORMASKIPROC, ColorMaski)                                                \
    X(1, PFNGLDEPTHFUNCPROC, DepthFunc)                                                  \
    X(1, PFNGLDEPTHMASKPROC, DepthMask)                                                  \
    X(1, PFNGLSTENCILFUNCSEPARATEPROC, StencilFuncSeparate)                              \
    X(1, PFNGLSTENCILOPSEPARATEPROC, StencilOpSeparate)                                  \
    X(1, PFNGLSTENCILMASKSEPARATEPROC, StencilMaskSeparate)                              \
    /* buffers */                                                                        \
    X(1, PFNGLGENBUFFERSPROC, GenBuffers)                                                \
    X(1, PFNGLDELETEBUFFERSPROC, DeleteBuffers)                                          \
    X(1, PFNGLBINDBUFFERPROC, BindBuffer)                                                \
    X(1, PFNGLBINDBUFFERRANGEPROC, BindBufferRange)                                      \
    X(1, PFNGLBINDBUFFERBASEPROC, BindBufferBase)                                        \
    X(1, PFNGLBUFFERDATAPROC, BufferData)                                                \
    X(1, PFNGLBUFFERSUBDATAPROC, BufferSubData)                                          \
    X(1, PFNGLMAPBUFFERRANGEPROC, MapBufferRange)                                        \
    X(1, PFNGLUNMAPBUFFERPROC, UnmapBuffer)                                              \
    X(1, PFNGLFLUSHMAPPEDBUFFERRANGEPROC, FlushMappedBufferRange)                        \
    X(1, PFNGLCOPYBUFFERSUBDATAPROC, CopyBufferSubData)                                  \
    X(1, PFNGLGETBUFFERSUBDATAPROC, GetBufferSubData)                                    \
    /* textures and samplers */                                                          \
    X(1, PFNGLGENTEXTURESPROC, GenTextures)                                              \
    X(1, PFNGLDELETETEXTURESPROC, DeleteTextures)                                        \
    X(1, PFNGLBINDTEXTUREPROC, BindTexture)                                              \
    X(1, PFNGLACTIVETEXTUREPROC, ActiveTexture)                                          \
    X(1, PFNGLTEXSTORAGE2DPROC, TexStorage2D)                                            \
    X(1, PFNGLTEXSTORAGE2DMULTISAMPLEPROC, TexStorage2DMultisample)                      \
    X(1, PFNGLTEXSUBIMAGE2DPROC, TexSubImage2D)                                          \
    X(1, PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC, CompressedTexSubImage2D)                      \
    X(1, PFNGLTEXPARAMETERIPROC, TexParameteri)                                          \
    X(1, PFNGLTEXPARAMETERFPROC, TexParameterf)                                          \
    X(1, PFNGLTEXTUREVIEWPROC, TextureView)                                              \
    X(1, PFNGLCOPYIMAGESUBDATAPROC, CopyImageSubData)                                    \
    X(1, PFNGLGETINTERNALFORMATIVPROC, GetInternalformativ)                              \
    X(1, PFNGLBINDIMAGETEXTUREPROC, BindImageTexture)                                    \
    X(1, PFNGLGETTEXIMAGEPROC, GetTexImage)                                              \
    X(1, PFNGLGENSAMPLERSPROC, GenSamplers)                                              \
    X(1, PFNGLDELETESAMPLERSPROC, DeleteSamplers)                                        \
    X(1, PFNGLBINDSAMPLERPROC, BindSampler)                                              \
    X(1, PFNGLSAMPLERPARAMETERIPROC, SamplerParameteri)                                  \
    X(1, PFNGLSAMPLERPARAMETERFPROC, SamplerParameterf)                                  \
    /* framebuffers */                                                                   \
    X(1, PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers)                                      \
    X(1, PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers)                                \
    X(1, PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer)                                      \
    X(1, PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D)                            \
    X(1, PFNGLFRAMEBUFFERTEXTUREPROC, FramebufferTexture)                                \
    X(1, PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus)                        \
    X(1, PFNGLDRAWBUFFERSPROC, DrawBuffers)                                              \
    X(1, PFNGLDRAWBUFFERPROC, DrawBuffer)                                                \
    X(1, PFNGLREADBUFFERPROC, ReadBuffer)                                                \
    X(1, PFNGLBLITFRAMEBUFFERPROC, BlitFramebuffer)                                      \
    X(1, PFNGLINVALIDATEFRAMEBUFFERPROC, InvalidateFramebuffer)                          \
    X(1, PFNGLINVALIDATESUBFRAMEBUFFERPROC, InvalidateSubFramebuffer)                    \
    X(1, PFNGLREADPIXELSPROC, ReadPixels)                                                \
    /* programs (separable) and program pipelines */                                     \
    X(1, PFNGLCREATESHADERPROGRAMVPROC, CreateShaderProgramv)                            \
    X(1, PFNGLGETPROGRAMIVPROC, GetProgramiv)                                            \
    X(1, PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog)                                  \
    X(1, PFNGLDELETEPROGRAMPROC, DeleteProgram)                                          \
    X(1, PFNGLUSEPROGRAMPROC, UseProgram)                                                \
    X(1, PFNGLGENPROGRAMPIPELINESPROC, GenProgramPipelines)                              \
    X(1, PFNGLDELETEPROGRAMPIPELINESPROC, DeleteProgramPipelines)                        \
    X(1, PFNGLBINDPROGRAMPIPELINEPROC, BindProgramPipeline)                              \
    X(1, PFNGLUSEPROGRAMSTAGESPROC, UseProgramStages)                                    \
    X(1, PFNGLVALIDATEPROGRAMPIPELINEPROC, ValidateProgramPipeline)                      \
    X(1, PFNGLGETPROGRAMPIPELINEIVPROC, GetProgramPipelineiv)                            \
    X(1, PFNGLGETPROGRAMPIPELINEINFOLOGPROC, GetProgramPipelineInfoLog)                  \
    /* vertex input (ARB_vertex_attrib_binding, 4.3) */                                  \
    X(1, PFNGLGENVERTEXARRAYSPROC, GenVertexArrays)                                      \
    X(1, PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays)                                \
    X(1, PFNGLBINDVERTEXARRAYPROC, BindVertexArray)                                      \
    X(1, PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray)                      \
    X(1, PFNGLDISABLEVERTEXATTRIBARRAYPROC, DisableVertexAttribArray)                    \
    X(1, PFNGLVERTEXATTRIBFORMATPROC, VertexAttribFormat)                                \
    X(1, PFNGLVERTEXATTRIBIFORMATPROC, VertexAttribIFormat)                              \
    X(1, PFNGLVERTEXATTRIBBINDINGPROC, VertexAttribBinding)                              \
    X(1, PFNGLVERTEXBINDINGDIVISORPROC, VertexBindingDivisor)                            \
    X(1, PFNGLBINDVERTEXBUFFERPROC, BindVertexBuffer)                                    \
    /* draws */                                                                          \
    X(1, PFNGLDRAWARRAYSPROC, DrawArrays)                                                \
    X(1, PFNGLDRAWARRAYSINSTANCEDPROC, DrawArraysInstanced)                              \
    X(1, PFNGLDRAWARRAYSINSTANCEDBASEINSTANCEPROC, DrawArraysInstancedBaseInstance)      \
    X(1, PFNGLDRAWELEMENTSBASEVERTEXPROC, DrawElementsBaseVertex)                        \
    X(1, PFNGLDRAWELEMENTSINSTANCEDBASEVERTEXPROC, DrawElementsInstancedBaseVertex)      \
    X(1, PFNGLDRAWELEMENTSINSTANCEDBASEVERTEXBASEINSTANCEPROC,                           \
      DrawElementsInstancedBaseVertexBaseInstance)                                       \
    /* synchronisation */                                                                \
    X(1, PFNGLFENCESYNCPROC, FenceSync)                                                  \
    X(1, PFNGLCLIENTWAITSYNCPROC, ClientWaitSync)                                        \
    X(1, PFNGLWAITSYNCPROC, WaitSync)                                                    \
    X(1, PFNGLDELETESYNCPROC, DeleteSync)                                                \
    X(1, PFNGLGETSYNCIVPROC, GetSynciv)                                                  \
    X(1, PFNGLMEMORYBARRIERPROC, MemoryBarrier)                                          \
    /* queries (ARB_timer_query, 3.3) */                                                 \
    X(1, PFNGLGENQUERIESPROC, GenQueries)                                                \
    X(1, PFNGLDELETEQUERIESPROC, DeleteQueries)                                          \
    X(1, PFNGLQUERYCOUNTERPROC, QueryCounter)                                            \
    X(1, PFNGLGETQUERYOBJECTIVPROC, GetQueryObjectiv)                                    \
    X(1, PFNGLGETQUERYOBJECTUI64VPROC, GetQueryObjectui64v)                              \
    /* KHR_debug (4.3) */                                                                \
    X(1, PFNGLDEBUGMESSAGECALLBACKPROC, DebugMessageCallback)                            \
    X(1, PFNGLDEBUGMESSAGECONTROLPROC, DebugMessageControl)                              \
    X(1, PFNGLDEBUGMESSAGEINSERTPROC, DebugMessageInsert)                                \
    X(1, PFNGLPUSHDEBUGGROUPPROC, PushDebugGroup)                                        \
    X(1, PFNGLPOPDEBUGGROUPPROC, PopDebugGroup)                                          \
    X(1, PFNGLOBJECTLABELPROC, ObjectLabel)                                              \
    /* later than 4.3: ARB_clip_control and ARB_texture_barrier (4.5),                   \
     * ARB_buffer_storage (4.4); gl_device.c checks them */                              \
    X(0, PFNGLCLIPCONTROLPROC, ClipControl)                                              \
    X(0, PFNGLBUFFERSTORAGEPROC, BufferStorage)                                          \
    X(0, PFNGLTEXTUREBARRIERPROC, TextureBarrier)
/* clang-format on */

#define GL__FIELD(req, type, name) type name;

typedef struct GlFuncs {
    GL_FUNCS(GL__FIELD)
} GlFuncs;

#undef GL__FIELD

/* The entry points (gl_loader.c); every other file of the backend calls
 * them as glf.Name(...).  The macro comes after SDL's headers, so it renames
 * nothing of theirs; not "gl", which is the backend's RHI_BACKEND_PREFIX
 * (rhi_backend_names.h pastes it into every entry point's name). */
extern GlFuncs rhi_gl_funcs;
#define glf rhi_gl_funcs

/* Fetches every entry point of GL_FUNCS with SDL_GL_GetProcAddress (the
 * context current).  False when a required one is missing: the line
 * "rhi_gl: the driver has no glName (OpenGL 4.3 core)" names the first.
 * *optionalMissing counts the optional ones left NULL. */
bool gl_loader_load(unsigned *optionalMissing);
/* Clears every entry point (rhi_shutdown). */
void gl_loader_clear(void);
/* How many entry points GL_FUNCS lists (the test and the log line). */
unsigned gl_loader_count(void);

#endif /* PORT_RHI_GL_GL_LOADER_H */
