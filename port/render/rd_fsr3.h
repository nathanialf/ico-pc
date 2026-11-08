/* rd_fsr3.h: FSR 3.1, AMD's temporal upscaler (the FidelityFX SDK v1.1.4,
 * port/rhi/ffx), run at the upscale point on Vulkan (docs/port/DISPLAY.md
 * "FSR 3.1 upscaling", docs/port/RENDER.md "The upscale point").
 *
 * Options > Picture > Upscaling = FSR 3.1 (RdSettings.upscale
 * RD_UPSCALE_FSR3) turns the upscale point on through the settings
 * (rd_upscale.c rd__upscale_follow) at the quality's ratio, and the hook
 * (rd_upscale_hook.c) hands each dispatch here.  One context per (render
 * size, display size, flags): made at the first dispatch of a size on the
 * render thread, made again when a size or a flag changes, given back with
 * UPSCALED (rd__upscale_free: a display change) and destroyed once the
 * pictures that used it have finished, or at the present's shutdown.  The
 * FidelityFX Vulkan backend owns its pipelines, descriptors and textures;
 * it records into the replay's own command list between
 * rhi_cmd_external_begin and _end (rhi_native.h).
 *
 * In a build without the upscaler (ICO_FFX off) or on a device that cannot
 * run it, rd_upscale_temporal_supported() is false with its reason logged
 * once, the row shows "(off: not available)" and nothing here runs. */
#ifndef RD_FSR3_H
#define RD_FSR3_H

#include <stdbool.h>
#include <stdint.h>

#include "rd_upscale.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The dispatch: in->color, in->depth (SCENE's D32F_S8 read through its
 * depth aspect) and in->velocity into in->out (UPSCALED, a storage
 * texture), outside any pass on cl.  False (nothing written; the reason
 * logged, the first 8 then every 100th) without a context, an input or the
 * native handles. */
bool rd__fsr3_dispatch(RhiCommandList cl, const RdUpscaleIn *in);

/* The support answer asked again at the next question, and the context
 * given back unless keep: from rd__upscale_free (a display change, the
 * targets made again).  keep (FSR 3.1 stays in force): the context stays,
 * and the next dispatch uses it when its render size, display size and
 * flags are the ones asked (a window drag at the same sizes, a remade
 * scene of the same size: no ffxCreateContext, RdFsr3Stats.kept), else
 * retires it.  A context given back is destroyed once the pictures that
 * used it have finished (a later dispatch), or at rd__fsr3_shutdown */
void rd__fsr3_free(bool keep);
/* Every context destroyed, the GPU idle first: the present's shutdown
 * (rd_shutdown, the render thread stopped) */
void rd__fsr3_shutdown(void);
/* rd__upscale_replay_begin, before every replay: the retired contexts the
 * GPU has finished with destroyed (a switch to Off frees the memory) */
void rd__fsr3_collect(void);

/* Why FSR 3.1 is not available ("" when it is): the log's words */
const char *rd_upscale_temporal_why(void);

/* The device runs FSR 3.1's 16-bit shaders (shaderFloat16 offered and enabled by the RHI: the
 * compute caps' float16 with the external extensions); false in a build without it */
bool rd__fsr3_fp16(void);

/* The build carries FSR 3.1 (ICO_FFX: the FidelityFX SDK linked) */
bool rd__fsr3_built(void);
/* The tests and rd_replay_tool --fsr3: why FSR 3.1 cannot be expected on
 * the device in use (no FidelityFX in the build, no device, not Vulkan, a
 * Vulkan device below 1.2), NULL when it must be available: a test then
 * fails, rather than skips, when rd_upscale_temporal_supported() says no */
const char *rd__fsr3_not_expected(void);

/* The tests: true makes rd_upscale_temporal_supported() false (the row's
 * unsupported path on a device that can run it) until called with false */
void rd__fsr3_test_unsupported(bool on);

typedef struct RdFsr3Stats {
    uint32_t made, destroyed;    /* contexts */
    uint32_t kept;               /* contexts kept across rd__fsr3_free(true) */
    double createMs;             /* the last ffxCreateContext and its queries, ms */
    uint32_t dispatched;         /* ffxDispatch returned OK */
    uint32_t failed;             /* ffxDispatch or a context failed */
    uint32_t refused;            /* not dispatched: an input or a handle missing */
    uint32_t resets;             /* dispatches told reset (a cut, a new history) */
    uint32_t messages;           /* FidelityFX's own messages (logged) */
    uint32_t velocityCleared;    /* dispatches whose replay drew no velocity pass */
    uint32_t renderW, renderH;   /* the context's (the last made) */
    uint32_t displayW, displayH; /* */
    int32_t phases;              /* its jitter phase count */
    double memoryMb, aliasableMb;
    char version[16]; /* the provider's ("3.1.4"), "" before a context */
} RdFsr3Stats;

const RdFsr3Stats *rd__fsr3_stats(void);

#ifdef __cplusplus
}
#endif

#endif
