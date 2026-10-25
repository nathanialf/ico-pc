/* rd_msaa.c: the MSAA epoch's resolve pass and its close line (rd_replay.c,
 * "the MSAA epoch", msaaEnd; docs/port/MSAA.md).
 *
 *   rd: msaa: the epoch closes at list L (why): ... (reported once a list
 *       and cause)                                            (events)
 *   rd: msaa: the epoch stays open to the replay's end: ... (reported once)
 *                                                             (events)
 *   rd: msaa: the epoch's depth was loaded from the scene, not cleared in
 *       it: ... (reported once)                               (events)
 *   rd: msaa: the resolve pipeline is not there (or its texture group
 *       could not be made): ... the colour resolve and the depth resolve
 *       instead (reported once)                               (events) */
#include <stdio.h>
#include <string.h>

#include "rd_replay.h"

/* msaa_resolve_ps's pipeline and its group (t1 SCENE_MS's colour, t2 its
 * depth): both made before the pass begins, so a missing one leaves the
 * caller's colour resolve to run instead (rd_replay.c msaaEnd) */
static bool resolvePrepare(RhiTexture color, RhiTexture depth, RdPipeKeyInt *k, RhiPipeline *pipe,
                           RhiBindGroup *g)
{
    static bool pipeLogged, groupLogged;
    *k = rd__msaa_resolve_key(0);
    *pipe = rd__get_pipeline(k);
    if (!pipe->id) {
        if (!pipeLogged) {
            pipeLogged = true;
            rd__log("msaa: the resolve pipeline is not there (not made yet, or its creation "
                    "failed): the scene is resolved by the colour resolve and the depth resolve "
                    "instead, without the covered-majority rule (reported once)");
        }
        return false;
    }
    RhiBinding b[3];
    memset(b, 0, sizeof(b));
    b[0].slot = 1;
    b[0].type = RHI_BIND_SAMPLED_TEXTURE;
    b[0].texture = color;
    b[0].aspect = RHI_ASPECT_COLOR;
    b[1].slot = 1;
    b[1].type = RHI_BIND_SAMPLER;
    b[1].sampler = rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    b[2].slot = 2;
    b[2].type = RHI_BIND_SAMPLED_TEXTURE;
    b[2].texture = depth;
    b[2].aspect = RHI_ASPECT_DEPTH;
    *g = rd__replay_bind_group(g_rd.layoutTex, b, 3);
    if (!g->id) {
        if (!groupLogged) {
            groupLogged = true;
            rd__log("msaa: the resolve's texture group could not be made: the scene is resolved "
                    "by the colour resolve and the depth resolve instead (reported once)");
        }
        return false;
    }
    return true;
}

/* msaa_resolve_ps over the w x h pass begun: DrawCB.param the count, the
 * clear depth and the rule (blit.hlsl) */
static void resolveDraw(RhiCommandList cl, const RdPipeKeyInt *k, RhiPipeline pipe, RhiBindGroup g,
                        uint32_t samples, bool hasClear, float clearDepth, uint32_t w, uint32_t h)
{
    rd__replay_viewport(w, h);
    const RhiRect all = {0, 0, w, h};
    rhi_cmd_set_scissor(cl, &all);
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.uvRect[2] = (float)w;
    cb.uvRect[3] = (float)h;
    cb.tex[0] = (float)w;
    cb.tex[1] = (float)h;
    cb.tex[2] = 1.0f / (float)w;
    cb.tex[3] = 1.0f / (float)h;
    cb.param[0] = (float)samples;
    cb.param[1] = hasClear ? clearDepth : 0.0f; /* bit for bit */
    cb.param[2] = hasClear ? 1.0f : 0.0f;
    rd__set_pipeline(cl, pipe, k);
    rd__bind_uniform(cl, 0, rd__frame_group(w, h, 0.0f, 0.0f));
    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(cl, 2, g);
    rd__replay_draw(cl, 3, 0, 1);
}

bool rd__msaa_resolve(RhiCommandList cl, RdTargetRec *ts, RhiTexture color, RhiState *colorState,
                      RhiTexture depth, RhiState *depthState, uint32_t samples, bool hasClear,
                      float clearDepth)
{
    static bool ruleOffLogged;
    RdPipeKeyInt k;
    RhiPipeline pipe;
    RhiBindGroup g;
    /* the pipeline and the group before the pass: the pass's DONT_CARE loads
     * leave SCENE undefined unless the draw runs */
    if (!resolvePrepare(color, depth, &k, &pipe, &g)) {
        return false;
    }
    if (!hasClear && !ruleOffLogged) {
        ruleOffLogged = true;
        rd__log("msaa: the epoch's depth was loaded from the scene, not cleared in it: no sample "
                "is told apart as uncovered, each pixel resolves to the mean colour and smallest "
                "depth of all its samples (reported once)");
    }
    rd__transition(cl, color, colorState, RHI_STATE_SHADER_READ);
    rd__transition(cl, depth, depthState, RHI_STATE_SHADER_READ);
    rd__transition(cl, ts->color, &ts->colorState, RHI_STATE_RENDER_TARGET);
    rd__transition(cl, ts->depth, &ts->depthState, RHI_STATE_DEPTH_WRITE);
    /* SCENE's colour and depth written whole; the stencil (a shadow
     * reset's clear recorded before the resolve) loaded and kept */
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = ts->color;
    p.color[0].load = RHI_LOAD_DONT_CARE;
    p.colorCount = 1;
    p.depth.texture = ts->depth;
    p.depth.depthLoad = RHI_LOAD_DONT_CARE;
    p.depth.stencilLoad = RHI_LOAD_LOAD;
    p.depth.stencilStore = RHI_STORE_STORE;
    p.width = ts->tw;
    p.height = ts->th;
    rhi_cmd_begin_render_pass(cl, &p);
    resolveDraw(cl, &k, pipe, g, samples, hasClear, clearDepth, ts->tw, ts->th);
    rhi_cmd_end_render_pass(cl);
    return true;
}

/* the lists logged, a bit each (RD_LIST_COUNT: the replay's end), per cause */
static uint32_t s_closeLogged[RD_MSAA_CLOSE_COUNT];

uint8_t rd__msaa_close_note(int cause)
{
    const int l = cause == RD_MSAA_CLOSE_END ? RD_LIST_COUNT : rd__passring_list_now();
    if (l < 0 || l > RD_LIST_COUNT || cause < 0 || cause >= RD_MSAA_CLOSE_COUNT) {
        return 0;
    }
    const uint32_t bit = 1u << (uint32_t)l;
    if (s_closeLogged[cause] & bit) {
        return (uint8_t)(l + 1);
    }
    s_closeLogged[cause] |= bit;
    if (cause == RD_MSAA_CLOSE_END) {
        rd__log("msaa: the epoch stays open to the replay's end: every list anti-aliased (the fog "
                "per sample where the device shades per sample) (reported once)");
        return (uint8_t)(l + 1);
    }
    static const char *const kWhy[RD_MSAA_CLOSE_END] = {
        "a read or copy of the scene's colour or depth (a texture read, a copy, a DATE snapshot, "
        "the fog's depth read)",
        "the shadow count's pass: the scene's depth with another colour",
        "a pass on the scene's colour with another depth"};
    char lists[64];
    if (l > 0) {
        snprintf(lists, sizeof(lists), "lists 0-%d anti-aliased and list %d up to there", l - 1, l);
    } else {
        snprintf(lists, sizeof(lists), "list 0 anti-aliased up to there");
    }
    rd__log("msaa: the epoch closes at list %d (%s): %s, the fog and the glow run on the resolved "
            "picture (reported once a list and cause)",
            l, kWhy[cause], lists);
    return (uint8_t)(l + 1);
}
