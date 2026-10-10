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
    "yuv_ps",          "yuv_vs",
};

/* the same entries compiled with other macros: the fog reading the
 * depth's words (FOG_BUFFER, rd_fog_path.c), with and without the second
 * output */
static const char *const expectedVariants[] = {"fog_lut_buffer_ps", "fog_lut_buffer_ps_nodual"};

/* the gs_dual_out entries without the second output (ICO_NO_DUAL) */
static const char *const expectedNoDual[] = {
    "sprite_ps_nodual",     "sprite_texa_ps_nodual", "sprite_aa1_ps_nodual",
    "sprite_stq_ps_nodual", "fog_lut_ps_nodual",     "font_ps_nodual",
    "vu_ps_nodual",         "vu_texa_ps_nodual",     "font_sheet_ps_nodual",
};

int main(void)
{
    int failures = 0;
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
        printf("  %-16s %-8s spirv %6zu B  dxil %6zu B\n", b->name,
               b->stage == ICO_SHADER_STAGE_VERTEX ? "vertex" : "fragment", b->spirv_len,
               b->dxil_len);
    }
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        if (!ico_find_shader(expected[i])) {
            printf("FAIL %s: not in the table\n", expected[i]);
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
    if (g_icoShaderCount != sizeof(expected) / sizeof(expected[0]) + nNoDual + nVariants) {
        printf("FAIL table has %u entries, expected %zu\n", g_icoShaderCount,
               sizeof(expected) / sizeof(expected[0]) + nNoDual + nVariants);
        failures++;
    }
    printf("shaders_table_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
