/* rd_jitter.c: the scene's sub-pixel jitter (rd_jitter.h; docs/port/RENDER.md
 * "Jitter").
 *
 * The jitter is a texel offset added to the exact viewport's centre offset
 * (FrameCB vpo.xy) of the frame groups of a scene-class pass, after the GS
 * position is made: gs_px_to_ndc and vu_homogeneous_position already add
 * vpo.xy to every position they map (common.hlsli, vu_common.hlsli), so no
 * vertex shader changes and the GS's snap, region test and side bands run
 * on the GS position as before.  FrameCB jitter.xy holds the same values for
 * the pixel side: the attribute planes (gs_plane_offset), the side bands'
 * pixel test (vu_pixel_in_picture) and fullscreen_triangle take them out, so
 * a pixel is evaluated at its jittered GS position and a pass's reads by
 * pixel stay on their texels.
 *
 * Here: the jitter set (the temporal upscaler's per present, Halton (2, 3)
 * over its phase count, rd_upscale.c rd__upscale_replay_begin; the
 * developer switch ICO_RD_JITTER_TEST=x,y, [dev] jitter_test, and
 * rd_replay_tool's --jitter x,y), which targets take it,
 * the count of the frame groups it moved, and the Halton (2, 3) table. */
#include "rd_jitter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_layer.h"

static float s_jitter[2];
static bool s_on;    /* a jitter set (0, 0 among them) */
static bool s_asked; /* set or cleared by a call, or the switch read */
static uint32_t s_groups;

void rd__frame_jitter_set(float x, float y)
{
    s_jitter[0] = x;
    s_jitter[1] = y;
    s_on = true;
    s_asked = true;
    s_groups = 0;
}

void rd__frame_jitter_clear(void)
{
    s_jitter[0] = s_jitter[1] = 0.0f;
    s_on = false;
    s_asked = true;
    s_groups = 0;
}

bool rd__frame_jitter_parse(const char *s, float out[2])
{
    if (!s) {
        return false;
    }
    char *end = NULL;
    const float x = strtof(s, &end);
    if (end == s) {
        return false;
    }
    while (*end == ' ') {
        end++;
    }
    if (*end != ',') {
        return false;
    }
    const char *t = end + 1;
    const float y = strtof(t, &end);
    if (end == t) {
        return false;
    }
    while (*end == ' ') {
        end++;
    }
    /* the range a temporal upscaler's jitter keeps (a NaN fails both tests) */
    if (*end != '\0' || !(x >= -0.5f && x <= 0.5f) || !(y >= -0.5f && y <= 0.5f)) {
        return false;
    }
    out[0] = x;
    out[1] = y;
    return true;
}

/* the developer switch, read once when nothing set the jitter first */
static void readSwitch(void)
{
    s_asked = true;
    const char *e = getenv("ICO_RD_JITTER_TEST");
    if (!e || !*e) {
        return;
    }
    float j[2];
    if (!rd__frame_jitter_parse(e, j)) {
        rd__log("jitter: ICO_RD_JITTER_TEST=%s is not x,y with each -0.5..0.5 ([dev] "
                "jitter_test); no jitter",
                e);
        return;
    }
    rd__frame_jitter_set(j[0], j[1]);
    rd__log("jitter: %.4g, %.4g texels on the scene-class passes (SCENE, WORK2, AURA_WORK) of "
            "every frame ([dev] jitter_test, a developer A/B)",
            (double)j[0], (double)j[1]);
}

bool rd__frame_jitter_get(float out[2])
{
    if (!s_asked) {
        readSwitch();
    }
    out[0] = s_on ? s_jitter[0] : 0.0f;
    out[1] = s_on ? s_jitter[1] : 0.0f;
    return s_on;
}

const float *rd__frame_jitter_of(const struct RdTargetRec *tc)
{
    if (!s_asked) {
        readSwitch();
    }
    if (!s_on || !tc) {
        return NULL;
    }
    /* the scene-class targets the scene's geometry draws into or tests
     * against SCENE's depth: SCENE, the glow's mask WORK2 (the lamps' meshes
     * Z-tested against SCENE's depth; the mask's sprite reads SCENE pixel
     * for pixel) and AURA_WORK (SCENE's Z).  DISPLAY is the reduction's
     * (not moved: the post chain's reads are by pixel), DISPLAY_HELD the
     * motion blur's copy, the DATE snapshot a copy pixel for pixel; the work
     * buffers and the shadow levels hold the post chain's reductions */
    if (tc != &g_rd.targets[RD_TARGET_SCENE] && tc != &g_rd.targets[RD_TARGET_WORK2] &&
        tc != &g_rd.targets[RD_TARGET_AURA_WORK]) {
        return NULL;
    }
    /* SCENE's record holding the display layer's composite (rd_layer.h):
     * the display-scale 2D after the upscale, never moved */
    if (tc == &g_rd.targets[RD_TARGET_SCENE] && rd__layer_active()) {
        return NULL;
    }
    s_groups++;
    return s_jitter;
}

uint32_t rd__frame_jitter_groups(void)
{
    return s_groups;
}

/* the radical inverse of i in base b as the double num / den: exact
 * (num < den = b^digits, both below 2^53 for every 32-bit i) */
static double radicalInverse(uint32_t i, uint32_t b)
{
    uint64_t num = 0, den = 1;
    while (i > 0) {
        num = num * b + i % b;
        den *= b;
        i /= b;
    }
    return (double)num / (double)den;
}

void rd__jitter_halton(uint32_t index, float out[2])
{
    const uint32_t i = index + 1u; /* the sequence from its first term (index 0 is 1) */
    out[0] = (float)(radicalInverse(i, 2u) - 0.5);
    out[1] = (float)(radicalInverse(i, 3u) - 0.5);
}
