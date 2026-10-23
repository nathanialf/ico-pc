/* d3d11_pipeline.c: pipelines of the Direct3D 11 backend (issue 126;
 * compile-only in v0.8.6: it has not run on any Windows machine).
 *
 * A pipeline is the vertex and pixel shader (compiled from the embedded
 * HLSL on first use, d11_shader_compiled), the input layout made from the
 * vertex shader's DXBC against the RhiVertexAttr list, the blend,
 * depth-stencil and rasterizer state objects, the topology and the vertex
 * strides.  The state objects are shared: each kind has a table keyed by
 * the description's bytes (Direct3D 11 keeps at most 4096 distinct objects
 * of a kind), filled on the calling thread and released at shutdown
 * (d11_pipelines_release).  The pool is RHI_PIPELINE_POOL_MAX, as on the
 * other backends, with the same full / refused / failed accounting.
 *
 * rhi_create_pipelines compiles on Win32 worker threads as the D3D12
 * backend does: the shaders (d3d11_shader_compile through the DXBC cache),
 * CreateVertexShader / CreatePixelShader and CreateInputLayout are
 * free-threaded on ID3D11Device; the tables, the pool and the log lines
 * stay on the calling thread. */
#include "d3d11_internal.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ tables
 * rhi.h's enumerations as D3D11 values, indexed by the RHI value (each with
 * a `set` flag, as d3d12_enums.h; d11_valid_pipeline_desc keeps an index in
 * range). */
typedef struct D11VertexFormatMap {
    bool set;
    DXGI_FORMAT dxgi;
} D11VertexFormatMap;

static const D11VertexFormatMap d11_vertexFormatMap[RHI_VTX_COUNT] = {
    [RHI_VTX_F32x1] = {true, DXGI_FORMAT_R32_FLOAT},
    [RHI_VTX_F32x2] = {true, DXGI_FORMAT_R32G32_FLOAT},
    [RHI_VTX_F32x3] = {true, DXGI_FORMAT_R32G32B32_FLOAT},
    [RHI_VTX_F32x4] = {true, DXGI_FORMAT_R32G32B32A32_FLOAT},
    [RHI_VTX_U8x4_UNORM] = {true, DXGI_FORMAT_R8G8B8A8_UNORM},
    [RHI_VTX_U8x4_UINT] = {true, DXGI_FORMAT_R8G8B8A8_UINT},
    [RHI_VTX_U16x2_UINT] = {true, DXGI_FORMAT_R16G16_UINT},
    [RHI_VTX_U32x1] = {true, DXGI_FORMAT_R32_UINT},
};

typedef struct D11CompareMap {
    bool set;
    D3D11_COMPARISON_FUNC d3d;
} D11CompareMap;

static const D11CompareMap d11_compareMap[RHI_CMP_COUNT] = {
    [RHI_CMP_NEVER] = {true, D3D11_COMPARISON_NEVER},
    [RHI_CMP_LESS] = {true, D3D11_COMPARISON_LESS},
    [RHI_CMP_EQUAL] = {true, D3D11_COMPARISON_EQUAL},
    [RHI_CMP_LEQUAL] = {true, D3D11_COMPARISON_LESS_EQUAL},
    [RHI_CMP_GREATER] = {true, D3D11_COMPARISON_GREATER},
    [RHI_CMP_NOTEQUAL] = {true, D3D11_COMPARISON_NOT_EQUAL},
    [RHI_CMP_GEQUAL] = {true, D3D11_COMPARISON_GREATER_EQUAL},
    [RHI_CMP_ALWAYS] = {true, D3D11_COMPARISON_ALWAYS},
};

/* Blend factors.  `alpha` is the factor of the alpha equation: D3D11, as
 * D3D12, refuses *_COLOR factors there, and for the alpha channel the
 * colour factor and the alpha factor are the same number (SRC1_COLOR.a ==
 * SRC1_ALPHA), so the substitution is exact.  The SRC1 factors are the
 * dual-source blend of target 0. */
typedef struct D11BlendFactorMap {
    bool set;
    D3D11_BLEND color;
    D3D11_BLEND alpha;
} D11BlendFactorMap;

static const D11BlendFactorMap d11_blendFactorMap[RHI_BF_COUNT] = {
    [RHI_BF_ZERO] = {true, D3D11_BLEND_ZERO, D3D11_BLEND_ZERO},
    [RHI_BF_ONE] = {true, D3D11_BLEND_ONE, D3D11_BLEND_ONE},
    [RHI_BF_SRC_COLOR] = {true, D3D11_BLEND_SRC_COLOR, D3D11_BLEND_SRC_ALPHA},
    [RHI_BF_ONE_MINUS_SRC_COLOR] = {true, D3D11_BLEND_INV_SRC_COLOR, D3D11_BLEND_INV_SRC_ALPHA},
    [RHI_BF_DST_COLOR] = {true, D3D11_BLEND_DEST_COLOR, D3D11_BLEND_DEST_ALPHA},
    [RHI_BF_ONE_MINUS_DST_COLOR] = {true, D3D11_BLEND_INV_DEST_COLOR, D3D11_BLEND_INV_DEST_ALPHA},
    [RHI_BF_SRC_ALPHA] = {true, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_SRC_ALPHA},
    [RHI_BF_ONE_MINUS_SRC_ALPHA] = {true, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA},
    [RHI_BF_DST_ALPHA] = {true, D3D11_BLEND_DEST_ALPHA, D3D11_BLEND_DEST_ALPHA},
    [RHI_BF_ONE_MINUS_DST_ALPHA] = {true, D3D11_BLEND_INV_DEST_ALPHA, D3D11_BLEND_INV_DEST_ALPHA},
    [RHI_BF_CONSTANT] = {true, D3D11_BLEND_BLEND_FACTOR, D3D11_BLEND_BLEND_FACTOR},
    [RHI_BF_ONE_MINUS_CONSTANT] = {true, D3D11_BLEND_INV_BLEND_FACTOR,
                                   D3D11_BLEND_INV_BLEND_FACTOR},
    [RHI_BF_SRC1_COLOR] = {true, D3D11_BLEND_SRC1_COLOR, D3D11_BLEND_SRC1_ALPHA},
    [RHI_BF_ONE_MINUS_SRC1_COLOR] = {true, D3D11_BLEND_INV_SRC1_COLOR, D3D11_BLEND_INV_SRC1_ALPHA},
    [RHI_BF_SRC1_ALPHA] = {true, D3D11_BLEND_SRC1_ALPHA, D3D11_BLEND_SRC1_ALPHA},
    [RHI_BF_ONE_MINUS_SRC1_ALPHA] = {true, D3D11_BLEND_INV_SRC1_ALPHA, D3D11_BLEND_INV_SRC1_ALPHA},
};

typedef struct D11BlendOpMap {
    bool set;
    D3D11_BLEND_OP d3d;
} D11BlendOpMap;

static const D11BlendOpMap d11_blendOpMap[RHI_BO_COUNT] = {
    [RHI_BO_ADD] = {true, D3D11_BLEND_OP_ADD},
    [RHI_BO_SUBTRACT] = {true, D3D11_BLEND_OP_SUBTRACT},
    [RHI_BO_REVERSE_SUBTRACT] = {true, D3D11_BLEND_OP_REV_SUBTRACT},
};

/* D3D11's INCR/DECR wrap; INCR_SAT/DECR_SAT clamp. */
typedef struct D11StencilOpMap {
    bool set;
    D3D11_STENCIL_OP d3d;
} D11StencilOpMap;

static const D11StencilOpMap d11_stencilOpMap[RHI_SO_COUNT] = {
    [RHI_SO_KEEP] = {true, D3D11_STENCIL_OP_KEEP},
    [RHI_SO_ZERO] = {true, D3D11_STENCIL_OP_ZERO},
    [RHI_SO_REPLACE] = {true, D3D11_STENCIL_OP_REPLACE},
    [RHI_SO_INCR_WRAP] = {true, D3D11_STENCIL_OP_INCR},
    [RHI_SO_DECR_WRAP] = {true, D3D11_STENCIL_OP_DECR},
    [RHI_SO_INCR_CLAMP] = {true, D3D11_STENCIL_OP_INCR_SAT},
    [RHI_SO_DECR_CLAMP] = {true, D3D11_STENCIL_OP_DECR_SAT},
    [RHI_SO_INVERT] = {true, D3D11_STENCIL_OP_INVERT},
};

typedef struct D11TopologyMap {
    bool set;
    D3D11_PRIMITIVE_TOPOLOGY topo;
} D11TopologyMap;

static const D11TopologyMap d11_topologyMap[RHI_TOPO_COUNT] = {
    [RHI_TOPO_TRIANGLE_LIST] = {true, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST},
    [RHI_TOPO_TRIANGLE_STRIP] = {true, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP},
    [RHI_TOPO_LINE_LIST] = {true, D3D11_PRIMITIVE_TOPOLOGY_LINELIST},
    [RHI_TOPO_POINT_LIST] = {true, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST},
};

/* ------------------------------------------------------ state-object tables
 * Open addressing over an FNV-1a hash of the description's bytes (the
 * descriptions are zeroed before they are filled, so padding never differs).
 * D3D11 hands back the same object for an identical description anyway;
 * the table keeps one reference per distinct description, so pipelines
 * hold raw pointers and a destroyed pipeline releases nothing here.  Only
 * the calling thread of rhi_create_pipeline(s) touches the tables. */
#define D11_STATE_TABLE 4096u /* a power of two; D3D11's own limit per kind */

typedef enum D11StateKind {
    D11_STATE_BLEND = 0,
    D11_STATE_DS,
    D11_STATE_RS,
    D11_STATE_KINDS
} D11StateKind;

typedef union D11StateDesc {
    D3D11_BLEND_DESC blend;
    D3D11_DEPTH_STENCIL_DESC ds;
    D3D11_RASTERIZER_DESC rs;
} D11StateDesc;

typedef struct D11StateEntry {
    bool used;
    D11StateDesc desc;
    IUnknown *obj;
} D11StateEntry;

static D11StateEntry *s_states[D11_STATE_KINDS];
static uint32_t s_stateCount[D11_STATE_KINDS];

static const char *const s_stateName[D11_STATE_KINDS] = {"blend", "depth-stencil", "rasterizer"};

static size_t d11_state_size(D11StateKind k)
{
    return k == D11_STATE_BLEND ? sizeof(D3D11_BLEND_DESC)
           : k == D11_STATE_DS  ? sizeof(D3D11_DEPTH_STENCIL_DESC)
                                : sizeof(D3D11_RASTERIZER_DESC);
}

static uint32_t d11_state_hash(const void *p, size_t n)
{
    const uint8_t *b = p;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h;
}

/* The state object of a description (made the first time it is seen);
 * NULL when the device refuses it or the table is full (logged). */
static IUnknown *d11_state_get(D11StateKind k, const D11StateDesc *d, HRESULT *hrOut)
{
    *hrOut = S_OK;
    if (!s_states[k]) {
        s_states[k] = calloc(D11_STATE_TABLE, sizeof(D11StateEntry));
        if (!s_states[k]) {
            D11_LOG("pipeline: out of memory for the %s state table", s_stateName[k]);
            return NULL;
        }
    }
    const size_t n = d11_state_size(k);
    uint32_t i = d11_state_hash(d, n) & (D11_STATE_TABLE - 1u);
    for (uint32_t probe = 0; probe < D11_STATE_TABLE;
         probe++, i = (i + 1u) & (D11_STATE_TABLE - 1u)) {
        D11StateEntry *e = &s_states[k][i];
        if (e->used) {
            if (memcmp(&e->desc, d, n) == 0) {
                return e->obj;
            }
            continue;
        }
        /* one slot always stays free so a lookup ends */
        if (s_stateCount[k] + 1u >= D11_STATE_TABLE) {
            static bool s_fullLogged[D11_STATE_KINDS];
            if (!s_fullLogged[k]) {
                s_fullLogged[k] = true;
                D11_LOG("pipeline: %u distinct %s states, the most Direct3D 11 keeps; a "
                        "pipeline needing another is not created (logged once)",
                        s_stateCount[k], s_stateName[k]);
            }
            return NULL;
        }
        HRESULT hr;
        IUnknown *obj = NULL;
        switch (k) {
        case D11_STATE_BLEND:
            hr = ID3D11Device_CreateBlendState(g_d11.device, &d->blend, (ID3D11BlendState **)&obj);
            break;
        case D11_STATE_DS:
            hr = ID3D11Device_CreateDepthStencilState(g_d11.device, &d->ds,
                                                      (ID3D11DepthStencilState **)&obj);
            break;
        default:
            hr = ID3D11Device_CreateRasterizerState(g_d11.device, &d->rs,
                                                    (ID3D11RasterizerState **)&obj);
            break;
        }
        if (!d11_check(hr, "ID3D11Device_Create*State", __FILE__, __LINE__) || !obj) {
            *hrOut = FAILED(hr) ? hr : E_FAIL;
            return NULL;
        }
        e->used = true;
        memcpy(&e->desc, d, n);
        e->obj = obj;
        s_stateCount[k]++;
        return obj;
    }
    return NULL;
}

D11Pipeline *d11_get_pipeline(uint32_t id)
{
    return d3dp_pool_get(&g_d11.pipelines, id);
}

static void d11_pipeline_release_objects(D11Pipeline *p, bool now)
{
    IUnknown *objs[3] = {(IUnknown *)p->vs, (IUnknown *)p->ps, (IUnknown *)p->il};
    for (int i = 0; i < 3; i++) {
        if (!objs[i]) {
            continue;
        }
        if (now) {
            IUnknown_Release(objs[i]);
        } else {
            d11_defer(objs[i]);
        }
    }
    p->vs = NULL;
    p->ps = NULL;
    p->il = NULL;
    /* the state objects belong to the tables */
    p->blend = NULL;
    p->ds = NULL;
    p->rs = NULL;
}

void d11_pipelines_release(void)
{
    /* a failed rhi_init calls this with the pools zeroed (next 0, no data):
     * the walk does nothing */
    for (uint32_t i = 0; i < g_d11.pipelines.next; i++) {
        D11Pipeline *p = d3dp_pool_at(&g_d11.pipelines, i);
        if (p) {
            d11_pipeline_release_objects(p, true);
        }
    }
    for (uint32_t k = 0; k < D11_STATE_KINDS; k++) {
        if (s_states[k]) {
            for (uint32_t i = 0; i < D11_STATE_TABLE; i++) {
                if (s_states[k][i].used && s_states[k][i].obj) {
                    IUnknown_Release(s_states[k][i].obj);
                }
            }
            free(s_states[k]);
            s_states[k] = NULL;
        }
        s_stateCount[k] = 0;
    }
}

/* ------------------------------------------------------------- descriptions */
static D3D11_RENDER_TARGET_BLEND_DESC d11_blend(const RhiBlendState *b, RhiFormat fmt)
{
    D3D11_RENDER_TARGET_BLEND_DESC s;
    memset(&s, 0, sizeof(s));
    s.RenderTargetWriteMask = (UINT8)(b->writeMask & 0xFu); /* R G B A = bits 0..3 in both */
    s.SrcBlend = s.SrcBlendAlpha = D3D11_BLEND_ONE;
    s.DestBlend = s.DestBlendAlpha = D3D11_BLEND_ZERO;
    s.BlendOp = s.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bool enable = b->enable;
    if (enable && d11_formatMap[fmt].isInteger) {
        D11_LOG("pipeline: blending on an integer target is ignored");
        enable = false;
    }
    if (!enable) {
        return s;
    }
    s.BlendEnable = TRUE;
    s.SrcBlend = d11_blendFactorMap[b->srcColor].color;
    s.DestBlend = d11_blendFactorMap[b->dstColor].color;
    s.BlendOp = d11_blendOpMap[b->colorOp].d3d;
    s.SrcBlendAlpha = d11_blendFactorMap[b->srcAlpha].alpha;
    s.DestBlendAlpha = d11_blendFactorMap[b->dstAlpha].alpha;
    s.BlendOpAlpha = d11_blendOpMap[b->alphaOp].d3d;
    return s;
}

static D3D11_DEPTH_STENCILOP_DESC d11_stencil(const RhiStencilFace *f)
{
    D3D11_DEPTH_STENCILOP_DESC s = {
        d11_stencilOpMap[f->fail].d3d,
        d11_stencilOpMap[f->depthFail].d3d,
        d11_stencilOpMap[f->pass].d3d,
        d11_compareMap[f->compare].d3d,
    };
    return s;
}

static bool d11_valid_pipeline_desc(const RhiPipelineDesc *d)
{
    if (d->topology >= RHI_TOPO_COUNT || d->colorCount > RHI_MAX_COLOR_TARGETS ||
        d->depthFormat >= RHI_FMT_COUNT || d->vertexAttrCount > RHI_MAX_VERTEX_ATTRS ||
        d->vertexBindingCount > RHI_MAX_VERTEX_ATTRS || d->layoutCount > RHI_MAX_BIND_SLOTS ||
        (d->vertexAttrCount && !d->vertexAttrs) || (d->vertexBindingCount && !d->vertexBindings) ||
        (d->layoutCount && !d->layouts)) {
        return false;
    }
    for (uint32_t i = 0; i < d->colorCount; i++) {
        const RhiBlendState *b = &d->blend[i];
        if (d->colorFormats[i] <= RHI_FMT_UNKNOWN || d->colorFormats[i] >= RHI_FMT_COUNT ||
            b->srcColor >= RHI_BF_COUNT || b->dstColor >= RHI_BF_COUNT ||
            b->srcAlpha >= RHI_BF_COUNT || b->dstAlpha >= RHI_BF_COUNT ||
            b->colorOp >= RHI_BO_COUNT || b->alphaOp >= RHI_BO_COUNT) {
            return false;
        }
    }
    const RhiDepthStencilState *ds = &d->depthStencil;
    const RhiStencilFace *faces[2] = {&ds->front, &ds->back};
    for (int i = 0; i < 2; i++) {
        if (faces[i]->compare >= RHI_CMP_COUNT || faces[i]->pass >= RHI_SO_COUNT ||
            faces[i]->fail >= RHI_SO_COUNT || faces[i]->depthFail >= RHI_SO_COUNT) {
            return false;
        }
    }
    for (uint32_t i = 0; i < d->vertexAttrCount; i++) {
        if (d->vertexAttrs[i].format >= RHI_VTX_COUNT ||
            d->vertexAttrs[i].binding >= RHI_MAX_VERTEX_ATTRS) {
            return false;
        }
    }
    for (uint32_t i = 0; i < d->vertexBindingCount; i++) {
        if (d->vertexBindings[i].binding >= RHI_MAX_VERTEX_ATTRS) {
            return false;
        }
    }
    return ds->depthCompare < RHI_CMP_COUNT;
}

/* One pipeline's creation in three steps (rhi.h rhi_create_pipelines), as
 * the D3D12 backend: d11_pipe_prepare on the calling thread (the checks,
 * the state objects from the tables), d11_pipe_compile on any thread (the
 * shaders, the input layout), d11_pipe_finish on the calling thread (the
 * pool slot, the log lines, the debug layer's messages). */
typedef struct D11PipeJob {
    bool ok;       /* prepared: there is something to compile */
    bool compiled; /* the shaders and the input layout were made */
    D11Shader *vs, *ps;
    ID3D11BlendState *blend;
    ID3D11DepthStencilState *ds;
    ID3D11RasterizerState *rs;
    ID3D11InputLayout *il;
    HRESULT hr; /* CreateInputLayout's error, S_OK otherwise */
    float ms;
    const RhiPipelineDesc *d;
    char err[256]; /* why the compile step failed */
} D11PipeJob;

/* a creation that failed other than for a full pool (RhiStats
 * pipelineFailures): hr the driver's error, S_OK for a description this
 * backend refused or a shader that did not compile (its line written) */
static void d11_pipe_failed(HRESULT hr)
{
    g_d11.counts.pipelineFailures++;
    g_d11.counts.pipelineLastError = (int64_t)hr;
}

/* the first 8 then every 100th, as the pool's line */
static bool d11_pipe_log_wanted(uint64_t n)
{
    return n <= 8u || n % 100u == 0u;
}

static bool d11_pipe_prepare(const RhiPipelineDesc *d, D11PipeJob *j)
{
    memset(j, 0, sizeof(*j));
    j->d = d;
    const char *name = d && d->debugName ? d->debugName : "?";
    if (!d || !d11_valid_pipeline_desc(d)) {
        D11_LOG("pipeline %s: invalid description", name);
        return false;
    }
    /* the destination read needs ROVs (off on this backend: RhiLimits.rov)
     * or local read (Vulkan only) */
    if (d->dstRead == RHI_DST_READ_ATTACHMENT ||
        (d->dstRead == RHI_DST_READ_STORAGE && (!g_d11.limits.rov || d->colorCount != 0)) ||
        d->dstRead >= RHI_DST_READ_COUNT) {
        D11_LOG("pipeline %s: destination read %d not available", name, (int)d->dstRead);
        return false;
    }
    j->vs = d11_get_shader(d->vertex);
    j->ps = d->fragment.id ? d11_get_shader(d->fragment) : NULL;
    if (!j->vs || j->vs->desc.stage != RHI_STAGE_VERTEX) {
        D11_LOG("pipeline %s: no vertex shader", name);
        return false;
    }
    if (d->fragment.id && (!j->ps || j->ps->desc.stage != RHI_STAGE_FRAGMENT)) {
        D11_LOG("pipeline %s: invalid pixel shader", name);
        return false;
    }
    for (uint32_t g = 0; g < d->layoutCount; g++) {
        if (!d11_get_layout(d->layouts[g].id)) {
            D11_LOG("pipeline %s: invalid bind group layout %u", name, g);
            return false;
        }
    }

    D11StateDesc sd;
    HRESULT hr = S_OK;
    /* blend: independent per target; target 0 carries the dual-source
     * factors (SRC1) when the RHI blend names them */
    memset(&sd, 0, sizeof(sd));
    sd.blend.AlphaToCoverageEnable = FALSE;
    sd.blend.IndependentBlendEnable = d->colorCount > 1 ? TRUE : FALSE;
    for (uint32_t i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) {
        D3D11_RENDER_TARGET_BLEND_DESC *t = &sd.blend.RenderTarget[i];
        t->RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        t->SrcBlend = t->SrcBlendAlpha = D3D11_BLEND_ONE;
        t->DestBlend = t->DestBlendAlpha = D3D11_BLEND_ZERO;
        t->BlendOp = t->BlendOpAlpha = D3D11_BLEND_OP_ADD;
    }
    for (uint32_t i = 0; i < d->colorCount; i++) {
        sd.blend.RenderTarget[i] = d11_blend(&d->blend[i], d->colorFormats[i]);
    }
    j->blend = (ID3D11BlendState *)d11_state_get(D11_STATE_BLEND, &sd, &hr);
    if (!j->blend) {
        D11_LOG("pipeline %s: no blend state", name);
        j->hr = hr;
        return false;
    }

    /* depth-stencil: reversed depth is the caller's compare (GEQUAL) */
    memset(&sd, 0, sizeof(sd));
    const RhiDepthStencilState *dss = &d->depthStencil;
    if (d->depthFormat != RHI_FMT_UNKNOWN) {
        sd.ds.DepthEnable = dss->depthTest ? TRUE : FALSE;
        sd.ds.DepthWriteMask = (dss->depthTest && dss->depthWrite) ? D3D11_DEPTH_WRITE_MASK_ALL
                                                                   : D3D11_DEPTH_WRITE_MASK_ZERO;
        sd.ds.DepthFunc = d11_compareMap[dss->depthCompare].d3d;
        sd.ds.StencilEnable =
            (dss->stencilTest && d11_formatMap[d->depthFormat].stencil) ? TRUE : FALSE;
        sd.ds.StencilReadMask = dss->stencilReadMask;
        sd.ds.StencilWriteMask = dss->stencilWriteMask;
    } else {
        sd.ds.DepthEnable = FALSE;
        sd.ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        sd.ds.DepthFunc = D3D11_COMPARISON_ALWAYS;
        sd.ds.StencilEnable = FALSE;
        sd.ds.StencilReadMask = 0xFF;
        sd.ds.StencilWriteMask = 0xFF;
    }
    sd.ds.FrontFace = d11_stencil(&dss->front);
    sd.ds.BackFace = d11_stencil(&dss->back);
    j->ds = (ID3D11DepthStencilState *)d11_state_get(D11_STATE_DS, &sd, &hr);
    if (!j->ds) {
        D11_LOG("pipeline %s: no depth-stencil state", name);
        j->hr = hr;
        return false;
    }

    /* rasterizer: the game never culls (rhi.h: cullNone is always true);
     * the scissor is always on (every pass sets one) */
    memset(&sd, 0, sizeof(sd));
    sd.rs.FillMode = D3D11_FILL_SOLID;
    sd.rs.CullMode = D3D11_CULL_NONE;
    sd.rs.FrontCounterClockwise = FALSE;
    sd.rs.DepthClipEnable = TRUE;
    sd.rs.ScissorEnable = TRUE;
    sd.rs.MultisampleEnable = d->samples > 1u ? TRUE : FALSE;
    sd.rs.AntialiasedLineEnable = FALSE;
    j->rs = (ID3D11RasterizerState *)d11_state_get(D11_STATE_RS, &sd, &hr);
    if (!j->rs) {
        D11_LOG("pipeline %s: no rasterizer state", name);
        j->hr = hr;
        return false;
    }
    j->ok = true;
    return true;
}

/* Any thread: the shaders (compiled once each, d11_shader_compiled) and
 * the input layout from the vertex shader's input signature. */
static void d11_pipe_compile(D11PipeJob *j)
{
    if (!j->ok) {
        return;
    }
    const uint64_t t0 = d11_now_ns();
    const RhiPipelineDesc *d = j->d;
    char err[200];
    err[0] = '\0';
    if (!d11_shader_compiled(j->vs, err, sizeof(err))) {
        snprintf(j->err, sizeof(j->err), "vertex shader %s: %s", j->vs->name, err);
        goto done;
    }
    if (j->ps && !d11_shader_compiled(j->ps, err, sizeof(err))) {
        snprintf(j->err, sizeof(j->err), "pixel shader %s: %s", j->ps->name, err);
        goto done;
    }
    if (d->vertexAttrCount) {
        D3D11_INPUT_ELEMENT_DESC ie[RHI_MAX_VERTEX_ATTRS];
        memset(ie, 0, sizeof(ie));
        const D11Shader *vs = j->vs;
        for (uint32_t i = 0; i < d->vertexAttrCount; i++) {
            const RhiVertexAttr *a = &d->vertexAttrs[i];
            const int e =
                vs->sigCount > 0 ? d3dp_location_element(vs->sig, vs->sigCount, a->location) : -1;
            if (e < 0) {
                snprintf(j->err, sizeof(j->err), "the vertex shader has no input at location %u",
                         a->location);
                goto done;
            }
            bool inst = false;
            for (uint32_t k = 0; k < d->vertexBindingCount; k++) {
                if (d->vertexBindings[k].binding == a->binding) {
                    inst = d->vertexBindings[k].perInstance;
                }
            }
            ie[i].SemanticName = vs->sig[e].name;
            ie[i].SemanticIndex = vs->sig[e].semanticIndex;
            ie[i].Format = d11_vertexFormatMap[a->format].dxgi;
            ie[i].InputSlot = a->binding;
            ie[i].AlignedByteOffset = a->offset;
            ie[i].InputSlotClass =
                inst ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA;
            ie[i].InstanceDataStepRate = inst ? 1u : 0u;
        }
        j->hr = ID3D11Device_CreateInputLayout(g_d11.device, ie, d->vertexAttrCount, vs->dxbc,
                                               vs->dxbcSize, &j->il);
        if (FAILED(j->hr)) {
            j->il = NULL;
            snprintf(j->err, sizeof(j->err), "CreateInputLayout failed (HRESULT 0x%08lx)",
                     (unsigned long)j->hr);
            goto done;
        }
    }
    j->compiled = true;
done:
    j->ms = (float)((double)(d11_now_ns() - t0) / 1e6);
}

static RhiPipeline d11_pipe_finish(const RhiPipelineDesc *d, D11PipeJob *j)
{
    RhiPipeline out = {0};
    const char *name = d && d->debugName ? d->debugName : "?";
    if (!j->ok) {
        return out;
    }
    if (!j->compiled) {
        /* a shader that failed is logged by the compile (rate-limited);
         * every pipeline it fails is counted, the first 8 and every 100th
         * written */
        static uint64_t s_failed;
        s_failed++;
        if (d11_pipe_log_wanted(s_failed)) {
            D11_LOG("pipeline %s not created: %s (%llu so far%s)", name, j->err,
                    (unsigned long long)s_failed,
                    s_failed == 8u ? "; from here every 100th is written" : "");
        }
        D11_RELEASE(j->il);
        d11_pipe_failed(j->hr);
        d11_drain_messages();
        return out;
    }
    D11Pipeline *p = NULL;
    const uint32_t id = d3dp_pool_alloc(&g_d11.pipelines, (void **)&p);
    if (!id) {
        /* the first 8 then every 100th, as the Vulkan pool's line
           (vk_device.c vkr_pool_alloc); the 10 s summary has the count */
        const uint64_t full = g_d11.pipelines.full;
        if (d11_pipe_log_wanted(full)) {
            D11_LOG("pipeline pool full (%u); %llu refused so far%s", g_d11.pipelines.cap,
                    (unsigned long long)full,
                    full == 8u ? "; from here every 100th is written" : "");
        }
        D11_RELEASE(j->il);
        return out;
    }
    memset(p, 0, sizeof(*p));
    p->vs = j->vs->vs;
    ID3D11VertexShader_AddRef(p->vs);
    if (j->ps && j->ps->ps) {
        p->ps = j->ps->ps;
        ID3D11PixelShader_AddRef(p->ps);
    }
    p->il = j->il; /* the job's reference moves to the pipeline */
    j->il = NULL;
    p->blend = j->blend;
    p->ds = j->ds;
    p->rs = j->rs;
    p->topo = d11_topologyMap[d->topology].topo;
    for (uint32_t i = 0; i < d->vertexBindingCount; i++) {
        const RhiVertexBinding *vb = &d->vertexBindings[i];
        p->strides[vb->binding] = vb->stride;
        p->bindingMask |= 1u << vb->binding;
        if (vb->perInstance) {
            p->instanceMask |= 1u << vb->binding;
        }
    }
    p->layoutCount = d->layoutCount;
    for (uint32_t g = 0; g < d->layoutCount; g++) {
        p->layoutIds[g] = d->layouts[g].id;
    }
    p->colorCount = d->colorCount;
    for (uint32_t i = 0; i < d->colorCount; i++) {
        p->colorFormats[i] = d->colorFormats[i];
    }
    p->depthFormat = d->depthFormat;
    p->samples = d->samples > 1u ? d->samples : 1u;
    d11_set_name((ID3D11DeviceChild *)p->il, d->debugName);
    d11_drain_messages();
    out.id = id;
    return out;
}

RhiPipeline rhi_create_pipeline(const RhiPipelineDesc *d)
{
    D11PipeJob j;
    if (!d11_pipe_prepare(d, &j)) {
        d11_pipe_failed(j.hr);
        return (RhiPipeline){0};
    }
    d11_pipe_compile(&j);
    return d11_pipe_finish(d, &j);
}

/* ------------------------------------------------- several at once (rhi.h)
 * Win32 threads, as d3d12_pipeline.c: the workers take the next job index
 * from one counter until none is left; each finished job counts and sets an
 * event the calling thread waits on, calling the caller's wait function at
 * most every RHI_PIPELINE_WAIT_MS.  Several jobs naming one shader compile
 * it once (d11_shader_compiled serialises them on the shader's lock). */
typedef struct D11PipeWork {
    D11PipeJob *jobs;
    uint32_t n;
    volatile LONG next, finished;
    HANDLE wake;
} D11PipeWork;

/* the workers' stack, reserved (not committed): FXC may recurse deeply,
 * and a new thread's default is 1 MB */
#define D11_PIPE_STACK_BYTES (8u * 1024u * 1024u)

static DWORD WINAPI d11_pipe_worker(LPVOID arg)
{
    D11PipeWork *w = arg;
    for (;;) {
        const LONG i = InterlockedIncrement(&w->next) - 1;
        if (i < 0 || (uint32_t)i >= w->n) {
            break;
        }
        d11_pipe_compile(&w->jobs[i]);
        InterlockedIncrement(&w->finished);
        SetEvent(w->wake);
    }
    return 0;
}

uint32_t rhi_create_pipelines(const RhiPipelineDesc *descs, uint32_t n, RhiPipeline *out,
                              RhiPipelineBatch *batch)
{
    uint32_t made = 0, threads = 1;
    if (batch) {
        batch->threads = 1;
    }
    if (!descs || !out || n == 0) {
        return 0;
    }
    D11PipeWork w;
    memset(&w, 0, sizeof(w));
    w.jobs = calloc(n, sizeof(D11PipeJob));
    w.n = n;
    if (!w.jobs) {
        for (uint32_t i = 0; i < n; i++) {
            out[i] = rhi_create_pipeline(&descs[i]);
            made += out[i].id != 0;
            if (batch && batch->ms) {
                batch->ms[i] = 0.0f;
            }
        }
        return made;
    }
    for (uint32_t i = 0; i < n; i++) {
        if (!d11_pipe_prepare(&descs[i], &w.jobs[i])) {
            d11_pipe_failed(w.jobs[i].hr);
        }
    }
    bool done = false;
    if (n > 1 || (batch && batch->wait)) {
        HANDLE th[RHI_PIPELINE_THREADS_MAX];
        uint32_t started = 0;
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        uint32_t want = si.dwNumberOfProcessors > 1 ? (uint32_t)si.dwNumberOfProcessors - 1u : 1u;
        if (want > RHI_PIPELINE_THREADS_MAX) {
            want = RHI_PIPELINE_THREADS_MAX;
        }
        if (want > n) {
            want = n;
        }
        if (batch && batch->maxThreads > 0 && want > batch->maxThreads) {
            want = batch->maxThreads; /* the careful set: one compile at a time */
        }
        w.wake = CreateEventW(NULL, FALSE, FALSE, NULL);
        /* kernel32's CreateThread by address: the game's PS2 kernel defines a
         * CreateThread of its own (port/platform/kernel_host.c), so the name
         * cannot be linked against kernel32 (d3d12_pipeline.c does the same) */
        typedef HANDLE(WINAPI * CreateThreadFn)(LPSECURITY_ATTRIBUTES, SIZE_T,
                                                LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
        HMODULE k32 = GetModuleHandleA("kernel32.dll");
        CreateThreadFn createThread =
            k32 ? (CreateThreadFn)(void (*)(void))GetProcAddress(k32, "CreateThread") : NULL;
        for (; w.wake && createThread && started < want; started++) {
            th[started] = createThread(NULL, D11_PIPE_STACK_BYTES, d11_pipe_worker, &w,
                                       STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
            if (!th[started]) {
                break;
            }
        }
        if (started > 0) {
            uint64_t last = d11_now_ns();
            while ((uint32_t)InterlockedCompareExchange(&w.finished, 0, 0) < n) {
                WaitForSingleObject(w.wake, RHI_PIPELINE_WAIT_MS);
                const uint64_t now = d11_now_ns();
                if (batch && batch->wait &&
                    now - last >= (uint64_t)RHI_PIPELINE_WAIT_MS * 1000000ull) {
                    batch->wait(batch->ctx,
                                (uint32_t)InterlockedCompareExchange(&w.finished, 0, 0));
                    last = d11_now_ns();
                }
            }
            WaitForMultipleObjects(started, th, TRUE, INFINITE);
            for (uint32_t i = 0; i < started; i++) {
                CloseHandle(th[i]);
            }
            threads = started;
            done = true;
        } else if (w.wake) {
            D11_LOG("pipelines: no worker thread (error %lu); creating them on this thread",
                    (unsigned long)GetLastError());
        }
        if (w.wake) {
            CloseHandle(w.wake);
        }
    }
    if (!done) {
        for (uint32_t i = 0; i < n; i++) {
            d11_pipe_compile(&w.jobs[i]);
            if (batch && batch->wait && i + 1 < n) {
                batch->wait(batch->ctx, i + 1);
            }
        }
    }
    for (uint32_t i = 0; i < n; i++) {
        out[i] = d11_pipe_finish(&descs[i], &w.jobs[i]);
        made += out[i].id != 0;
        if (batch && batch->ms) {
            batch->ms[i] = w.jobs[i].ms;
        }
    }
    if (batch) {
        batch->threads = threads;
    }
    free(w.jobs);
    return made;
}

void rhi_destroy_pipeline(RhiPipeline h)
{
    D11Pipeline *p = d11_get_pipeline(h.id);
    if (!p) {
        return;
    }
    /* a list of this frame may still bind it: released with the slot */
    d11_pipeline_release_objects(p, false);
    d3dp_pool_release(&g_d11.pipelines, h.id);
}
