/* shaders_table_test.c: every entry of the embedded table compiled for both
 * targets: SPIR-V with its magic number, DXIL as a signed DXBC container
 * (none when the table is built with ICO_SHADERS_DXIL=OFF),
 * known stage and entry, unique names. The compile itself is the build
 * (a wrong shader fails ninja with DXC's message); this checks what was
 * embedded. */
#include "shaders_gen.h"
#include <stdio.h>
#include <string.h>

static const char *const expected[] = {
    "blend_int_ps",    "blend_int_vs",     "blit_depth_ps",    "blit_ps",
    "blit_vs",         "box_reduce_ps",    "camera_probe_ps",  "crt_bloom_ps",
    "crt_blur_ps",     "crt_ps",           "crt_vs",           "date_snap_ps",
    "fog_lut_ps",      "font_ps",          "font_sheet_ps",    "fx_rect_vs",
    "fx_sprite_ps",    "sprite_aa1_ps",    "sprite_aa1_ui_vs", "sprite_aa1_world_vs",
    "sprite_ps",       "sprite_stq_ps",    "sprite_stq_ui_vs", "sprite_stq_world_vs",
    "sprite_texa_ps",  "sprite_ui_vs",     "sprite_world_vs",  "vu_grid_lit_vs",
    "vu_grid_spec_vs", "vu_grid_vs",       "vu_lit_spec_vs",   "vu_lit_vs",
    "vu_particle_vs",  "vu_prelit_vs",     "vu_probe_ps",      "vu_ps",
    "vu_reflect_vs",   "vu_skin_debug_vs", "vu_skin_spec_vs",  "vu_skin_vs",
    "vu_texa_ps",      "wrap_acc_ps",      "wrap_resolve_ps",  "yuv_field_ps",
    "yuv_ps",          "yuv_vs",           "shadow_count_ps", /* the shadow count in one draw (rd_shadow_one.c) */
};

/* the screen-prim triangles with their colour and STQ planes
 * (rd_screen_planes.c, sprite.hlsl's planes build ICO_SPRITE_PLANES): the
 * vertex entries and the pixel entries built again, with their twins */
static const char *const expectedPlanes[] = {
    "sprite_plane_ui_vs",
    "sprite_plane_world_vs",
    "sprite_stq_plane_ui_vs",
    "sprite_stq_plane_world_vs",
    "sprite_ps_planes",
    "sprite_ps_planes_nodual",
    "sprite_ps_planes_dstrov",
    "sprite_ps_planes_dstatt",
    "sprite_texa_ps_planes",
    "sprite_texa_ps_planes_nodual",
    "sprite_texa_ps_planes_dstrov",
    "sprite_texa_ps_planes_dstatt",
    "sprite_stq_ps_planes",
    "sprite_stq_ps_planes_nodual",
    "sprite_stq_ps_planes_dstrov",
    "sprite_stq_ps_planes_dstatt",
};

/* the staticBlur sprites' UINT-view twin (fx_sprite.hlsl ICO_UINT_SRC; rd_replay.c doBlurSpriteDraw) */
static const char *const expectedUint[] = {"fx_sprite_u_ps"};

/* the tiler passes (rd_replay_tiler.c): DATE's stencil bit, the fog's local read and the folded blur pair */
static const char *const expectedTiler[] = {"date_clear_ps", "date_stencil_ps", "fog_lut_local_ps",
                                            "fog_lut_local_ps_nodual", "fx_blur2_ps"};

/* the pixel-diet builds: vu_ps and vu_texa_ps without the planes' inputs
 * (ICO_VU_NO_PLANES, with their two-pass twins) and fx_sprite_ps's
 * frame-size and local-read forms (ICO_FX_FRAME, ICO_FX_DST_LOCAL) */
static const char *const expectedDiet[] = {"vu_np_ps",      "vu_np_ps_nodual",
                                           "vu_texa_np_ps", "vu_texa_np_ps_nodual",
                                           "fx_frame_ps",   "fx_local_ps"};

/* Motion blur: Smooth's accumulator (fx_sprite.hlsl; port/render/rd_blur_smooth.c) */
static const char *const expectedSmooth[] = {"fx_acc_ps", "fx_acc_resolve_ps"};

/* the crack filler above 1x (blit.hlsl; port/render/rd_crack_fill.c) */
static const char *const expectedCrack[] = {"crack_fill_ps", "crack_count_ps", "crack_fill1_ps"};

/* the mesh programs' entries' plain variants (ICO_VU_PLAIN, vu_common.hlsli:
 * the fallback's on a device of the table) */
static const char *const expectedPlain[] = {
    "vu_prelit_plain_vs",     "vu_lit_plain_vs",  "vu_lit_spec_plain_vs",
    "vu_reflect_plain_vs",    "vu_skin_plain_vs", "vu_skin_spec_plain_vs",
    "vu_skin_debug_plain_vs", "vu_grid_plain_vs", "vu_grid_lit_plain_vs",
    "vu_grid_spec_plain_vs",
};

/* the vertex and triangle pre-passes (vu_tri.hlsl, vu_particle.hlsl;
 * docs/port/RENDER.md "The vertex and triangle pre-passes"): each mesh
 * program's pre-pass entry, the pixel shader of the record, the triangles'
 * and sprites' record stages and the main shaders drawing from the record */
static const char *const expectedPrepass[] = {
    "vu_prelit_pre_vs",
    "vu_lit_pre_vs",
    "vu_lit_spec_pre_vs",
    "vu_reflect_pre_vs",
    "vu_skin_pre_vs",
    "vu_skin_spec_pre_vs",
    "vu_skin_debug_pre_vs",
    "vu_grid_pre_vs",
    "vu_grid_lit_pre_vs",
    "vu_grid_spec_pre_vs",
    "vu_pre_ps",
    "vu_tri_rule_pre_vs",
    "vu_tri_tick_pre_vs",
    "vu_tri_planes_pre_vs",
    "vu_particle_pre_vs",
    "vu_main_vs",
    "vu_particle_main_vs",
};

/* the pre-pass's batch builds (ICO_VU_PRE_BATCH, vu_common.hlsli: the
 * entries above reading their VuCB and VuBoneCB from the ring at t5 for a
 * draw of many entries; rd_replay_vu.c vuPrepassRun, g_rd.vuPreBatch) */
static const char *const expectedBatch[] = {
    "vu_prelit_batch_pre_vs",     "vu_lit_batch_pre_vs",      "vu_lit_spec_batch_pre_vs",
    "vu_reflect_batch_pre_vs",    "vu_skin_batch_pre_vs",     "vu_skin_spec_batch_pre_vs",
    "vu_skin_debug_batch_pre_vs", "vu_grid_batch_pre_vs",     "vu_grid_lit_batch_pre_vs",
    "vu_grid_spec_batch_pre_vs",  "vu_tri_rule_batch_pre_vs", "vu_tri_tick_batch_pre_vs",
    "vu_tri_planes_batch_pre_vs", "vu_particle_batch_pre_vs",
};

/* the draw merge's builds (ICO_VU_MERGE, vu_common.hlsli: the mesh and grid
 * programs' entries, both variants, and vu_main_vs reading every draw's
 * VuCB and VuBoneCB from the ring at t5 for a draw of many; rd_replay_vu.c
 * vuDraw, g_rd.vuMerge) */
static const char *const expectedMerge[] = {
    "vu_prelit_merge_vs",         "vu_lit_merge_vs",
    "vu_lit_spec_merge_vs",       "vu_reflect_merge_vs",
    "vu_grid_merge_vs",           "vu_grid_lit_merge_vs",
    "vu_grid_spec_merge_vs",      "vu_prelit_merge_plain_vs",
    "vu_lit_merge_plain_vs",      "vu_lit_spec_merge_plain_vs",
    "vu_reflect_merge_plain_vs",  "vu_grid_merge_plain_vs",
    "vu_grid_lit_merge_plain_vs", "vu_grid_spec_merge_plain_vs",
    "vu_merge_main_vs",
};

/* entries the record path retired: the triangle stage of the vertex
 * records alone (vu_tri_vs) and the fallback's own pixel shaders (the
 * programs' entries draw with vu_ps and vu_texa_ps, as the main shaders) */
static const char *const retired[] = {"vu_tri_vs", "vu_legacy_ps", "vu_legacy_texa_ps"};

/* the anti-aliasing's (rd_present.c, rd_replay.c's MSAA epoch) */
static const char *const expectedAa[] = {"aa_reduce_ps", "depth_resolve_ps", "depth_upsample_ps",
                                         "msaa_resolve_ps"};

/* the same entries compiled with other macros: the fog reading the
 * depth's words (FOG_BUFFER, rd_fog_path.c) and the multisampled depth per
 * sample (FOG_MS, the MSAA epoch), with and without the second output */
static const char *const expectedVariants[] = {"fog_lut_buffer_ps", "fog_lut_buffer_ps_nodual",
                                               "fog_lut_ms_ps", "fog_lut_ms_ps_nodual"};

/* the gs_dual_out entries without the second output (ICO_NO_DUAL) */
static const char *const expectedNoDual[] = {
    "sprite_ps_nodual",     "sprite_texa_ps_nodual", "sprite_aa1_ps_nodual",
    "sprite_stq_ps_nodual", "fog_lut_ps_nodual",     "font_ps_nodual",
    "vu_ps_nodual",         "vu_texa_ps_nodual",     "font_sheet_ps_nodual",
};

/* the gs_dual_out entries computing the GS blend from the destination
 * (ICO_DST_READ: rasterizer-ordered view, attachment read) */
static const char *const expectedDst[] = {
    "sprite_ps_dstrov",
    "sprite_ps_dstatt",
    "sprite_texa_ps_dstrov",
    "sprite_texa_ps_dstatt",
    "sprite_aa1_ps_dstrov",
    "sprite_aa1_ps_dstatt",
    "sprite_stq_ps_dstrov",
    "sprite_stq_ps_dstatt",
    "fog_lut_ps_dstrov",
    "fog_lut_ps_dstatt",
    "fog_lut_buffer_ps_dstrov",
    "fog_lut_buffer_ps_dstatt",
    "font_ps_dstrov",
    "font_ps_dstatt",
    "font_sheet_ps_dstrov",
    "font_sheet_ps_dstatt",
    "vu_ps_dstrov",
    "vu_ps_dstatt",
    "vu_texa_ps_dstrov",
    "vu_texa_ps_dstatt",
};

/* the entries GLSL cannot express (tools/spirv_to_glsl.py: an input
 * attachment, SPIR-V Dim SubpassData): the _dstatt builds, the local fog-LUT
 * and fx passes and the date stencil. With GLSL on, exactly these have none. */
static const char *const expectedNoGlsl[] = {
    "date_stencil_ps",
    "fog_lut_buffer_ps_dstatt",
    "fog_lut_local_ps",
    "fog_lut_local_ps_nodual",
    "fog_lut_ps_dstatt",
    "font_ps_dstatt",
    "font_sheet_ps_dstatt",
    "fx_local_ps",
    "sprite_aa1_ps_dstatt",
    "sprite_ps_dstatt",
    "sprite_ps_planes_dstatt",
    "sprite_stq_ps_dstatt",
    "sprite_stq_ps_planes_dstatt",
    "sprite_texa_ps_dstatt",
    "sprite_texa_ps_planes_dstatt",
    "vu_ps_dstatt",
    "vu_texa_ps_dstatt",
};

int main(void)
{
    int failures = 0;
    unsigned glslCount = 0;
    (void)glslCount;
    for (unsigned i = 0; i < g_icoShaderCount; i++) {
        const IcoShaderBlob *b = &g_icoShaders[i];
        const uint8_t *s = b->spirv, *d = b->dxil;
        unsigned magic =
            (unsigned)s[0] | (unsigned)s[1] << 8 | (unsigned)s[2] << 16 | (unsigned)s[3] << 24;
        if (b->spirv_len < 32 || b->spirv_len % 4 || magic != 0x07230203u) {
            printf("FAIL %s: SPIR-V\n", b->name);
            failures++;
        }
#ifdef ICO_SHADERS_NO_DXIL
        /* built without DXIL (ICO_SHADERS_DXIL=OFF) */
        if (d || b->dxil_len) {
            printf("FAIL %s: DXIL in a table built without\n", b->name);
            failures++;
        }
#else
        int hashed = 0;
        for (int k = 4; k < 20; k++) {
            hashed |= d[k];
        }
        if (b->dxil_len < 64 || memcmp(d, "DXBC", 4) != 0 || !hashed) {
            printf("FAIL %s: DXIL missing or unsigned\n", b->name);
            failures++;
        }
#endif
#ifdef ICO_SHADERS_NO_GLSL
        /* built without GLSL (ICO_SHADERS_GLSL=OFF) */
        if (b->glsl || b->glsl_len || b->glsl_text_len) {
            printf("FAIL %s: GLSL in a table built without\n", b->name);
            failures++;
        }
#else
        /* an entry GLSL cannot express has none; the rest are deflated text
         * (the translation step's own test compiles them) */
        if (b->glsl && (b->glsl_len < 8 || b->glsl_text_len < 16 || b->glsl[0] != 0x78)) {
            printf("FAIL %s: GLSL is not a zlib stream\n", b->name);
            failures++;
        }
        glslCount += b->glsl != NULL;
        {
            int expectNone = 0;
            for (size_t k = 0; k < sizeof expectedNoGlsl / sizeof expectedNoGlsl[0]; k++) {
                expectNone |= strcmp(expectedNoGlsl[k], b->name) == 0;
            }
            if (expectNone != (b->glsl == NULL)) {
                printf("FAIL %s: %s GLSL\n", b->name, expectNone ? "has" : "lacks");
                failures++;
            }
            if (b->glsl == NULL && (b->glsl_len || b->glsl_text_len)) {
                printf("FAIL %s: GLSL length without GLSL\n", b->name);
                failures++;
            }
        }
#endif
        /* the Direct3D 11 columns: the HLSL file (always named; a source
         * of the embedded set when the sources are embedded) and the macros */
        if (!b->hlslFile || !b->hlslFile[0] || !b->defines || strlen(b->hlslFile) < 6 ||
            strcmp(b->hlslFile + strlen(b->hlslFile) - 5, ".hlsl") != 0) {
            printf("FAIL %s: hlslFile or defines\n", b->name);
            failures++;
        }
#ifndef ICO_SHADERS_NO_HLSL_SOURCE
        {
            int found = 0;
            for (unsigned k = 0; k < g_icoShaderSourceCount; k++) {
                found |= strcmp(g_icoShaderSources[k].name, b->hlslFile) == 0;
            }
            if (!found) {
                printf("FAIL %s: %s is not among the embedded sources\n", b->name, b->hlslFile);
                failures++;
            }
        }
#endif
        if (b->stage != ICO_SHADER_STAGE_VERTEX && b->stage != ICO_SHADER_STAGE_FRAGMENT) {
            printf("FAIL %s: stage\n", b->name);
            failures++;
        }
        if (!b->entry || !b->entry[0] || ico_find_shader(b->name) != b) {
            printf("FAIL %s: entry or lookup\n", b->name);
            failures++;
        }
        size_t n = strlen(b->name);
        int suffixVs = n > 3 && strcmp(b->name + n - 3, "_vs") == 0;
        if (suffixVs != (b->stage == ICO_SHADER_STAGE_VERTEX)) {
            printf("FAIL %s: stage does not match the name\n", b->name);
            failures++;
        }
        printf("  %-16s %-8s spirv %6zu B  dxil %6zu B  glsl %6zu B (%6zu B text)  %s %s\n",
               b->name, b->stage == ICO_SHADER_STAGE_VERTEX ? "vertex" : "fragment", b->spirv_len,
               b->dxil_len, b->glsl_len, b->glsl_text_len, b->hlslFile, b->defines);
    }
#ifndef ICO_SHADERS_NO_GLSL
    if (glslCount == 0) {
        printf("FAIL: a table built with GLSL has no GLSL\n");
        failures++;
    }
    printf("GLSL: %u entries with, %u without (expected without: %zu)\n", glslCount,
           g_icoShaderCount - glslCount, sizeof expectedNoGlsl / sizeof expectedNoGlsl[0]);
    if (g_icoShaderCount - glslCount != sizeof expectedNoGlsl / sizeof expectedNoGlsl[0]) {
        printf("FAIL: the entries without GLSL are not the expected set\n");
        failures++;
    }
#endif
#ifdef ICO_SHADERS_NO_HLSL_SOURCE
    if (g_icoShaderSourceCount != 0) {
        printf("FAIL: sources in a table built without\n");
        failures++;
    }
#else
    if (g_icoShaderSourceCount == 0) {
        printf("FAIL: a table built with the HLSL sources has none\n");
        failures++;
    }
    for (unsigned k = 0; k < g_icoShaderSourceCount; k++) {
        const IcoShaderSource *s = &g_icoShaderSources[k];
        if (!s->name || !s->text || s->len == 0 || strlen(s->text) != s->len) {
            printf("FAIL source %u: name, text or length\n", k);
            failures++;
        }
    }
    printf("  %u HLSL sources embedded\n", g_icoShaderSourceCount);
#endif
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        if (!ico_find_shader(expected[i])) {
            printf("FAIL %s: not in the table\n", expected[i]);
            failures++;
        }
    }
    const size_t nAa = sizeof(expectedAa) / sizeof(expectedAa[0]);
    for (size_t i = 0; i < nAa; i++) {
        if (!ico_find_shader(expectedAa[i])) {
            printf("FAIL %s: not in the table\n", expectedAa[i]);
            failures++;
        }
    }
    const size_t nNoDual = sizeof(expectedNoDual) / sizeof(expectedNoDual[0]);
    for (size_t i = 0; i < nNoDual; i++) {
        if (!ico_find_shader(expectedNoDual[i])) {
            printf("FAIL %s: not in the table\n", expectedNoDual[i]);
            failures++;
        }
    }
    const size_t nVariants = sizeof(expectedVariants) / sizeof(expectedVariants[0]);
    for (size_t i = 0; i < nVariants; i++) {
        if (!ico_find_shader(expectedVariants[i])) {
            printf("FAIL %s: not in the table\n", expectedVariants[i]);
            failures++;
        }
    }
    const size_t nPlanes = sizeof(expectedPlanes) / sizeof(expectedPlanes[0]);
    for (size_t i = 0; i < nPlanes; i++) {
        if (!ico_find_shader(expectedPlanes[i])) {
            printf("FAIL %s: not in the table\n", expectedPlanes[i]);
            failures++;
        }
    }
    const size_t nUint = sizeof(expectedUint) / sizeof(expectedUint[0]);
    for (size_t i = 0; i < nUint; i++) {
        if (!ico_find_shader(expectedUint[i])) {
            printf("FAIL %s: not in the table\n", expectedUint[i]);
            failures++;
        }
    }
    const size_t nTiler = sizeof(expectedTiler) / sizeof(expectedTiler[0]);
    for (size_t i = 0; i < nTiler; i++) {
        if (!ico_find_shader(expectedTiler[i])) {
            printf("FAIL %s: not in the table\n", expectedTiler[i]);
            failures++;
        }
    }
    const size_t nDiet = sizeof(expectedDiet) / sizeof(expectedDiet[0]);
    for (size_t i = 0; i < nDiet; i++) {
        if (!ico_find_shader(expectedDiet[i])) {
            printf("FAIL %s: not in the table\n", expectedDiet[i]);
            failures++;
        }
    }
    const size_t nSmooth = sizeof(expectedSmooth) / sizeof(expectedSmooth[0]);
    for (size_t i = 0; i < nSmooth; i++) {
        if (!ico_find_shader(expectedSmooth[i])) {
            printf("FAIL %s: not in the table\n", expectedSmooth[i]);
            failures++;
        }
    }
    const size_t nCrack = sizeof(expectedCrack) / sizeof(expectedCrack[0]);
    for (size_t i = 0; i < nCrack; i++) {
        if (!ico_find_shader(expectedCrack[i])) {
            printf("FAIL %s: not in the table\n", expectedCrack[i]);
            failures++;
        }
    }
    const size_t nPlain = sizeof(expectedPlain) / sizeof(expectedPlain[0]);
    for (size_t i = 0; i < nPlain; i++) {
        if (!ico_find_shader(expectedPlain[i])) {
            printf("FAIL %s: not in the table\n", expectedPlain[i]);
            failures++;
        }
    }
    const size_t nPrepass = sizeof(expectedPrepass) / sizeof(expectedPrepass[0]);
    for (size_t i = 0; i < nPrepass; i++) {
        if (!ico_find_shader(expectedPrepass[i])) {
            printf("FAIL %s: not in the table\n", expectedPrepass[i]);
            failures++;
        }
    }
    const size_t nBatch = sizeof(expectedBatch) / sizeof(expectedBatch[0]);
    for (size_t i = 0; i < nBatch; i++) {
        if (!ico_find_shader(expectedBatch[i])) {
            printf("FAIL %s: not in the table\n", expectedBatch[i]);
            failures++;
        }
    }
    const size_t nMerge = sizeof(expectedMerge) / sizeof(expectedMerge[0]);
    for (size_t i = 0; i < nMerge; i++) {
        if (!ico_find_shader(expectedMerge[i])) {
            printf("FAIL %s: not in the table\n", expectedMerge[i]);
            failures++;
        }
    }
    for (size_t i = 0; i < sizeof(retired) / sizeof(retired[0]); i++) {
        if (ico_find_shader(retired[i])) {
            printf("FAIL %s: still in the table\n", retired[i]);
            failures++;
        }
    }
    const size_t nDst = sizeof(expectedDst) / sizeof(expectedDst[0]);
    for (size_t i = 0; i < nDst; i++) {
        if (!ico_find_shader(expectedDst[i])) {
            printf("FAIL %s: not in the table\n", expectedDst[i]);
            failures++;
        }
    }
    const size_t nAll = sizeof(expected) / sizeof(expected[0]) + nAa + nNoDual + nVariants + nDst +
                        nUint + nTiler + nDiet + nPlanes + nPlain + nPrepass + nBatch + nSmooth +
                        nCrack;
    if (g_icoShaderCount != nAll) {
        printf("FAIL table has %u entries, expected %zu\n", g_icoShaderCount, nAll);
        failures++;
    }
    printf("shaders_table_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
