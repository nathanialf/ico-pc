/* shaders_table_test.c: every entry of the embedded table compiled for both
 * targets: SPIR-V with its magic number, DXIL as a signed DXBC container,
 * known stage and entry, unique names. The compile itself is the build
 * (a wrong shader fails ninja with DXC's message); this checks what was
 * embedded. */
#include "shaders_gen.h"
#include <stdio.h>
#include <string.h>

static const char *const expected[] = {
    "sprite_ui_vs", "sprite_world_vs", "sprite_ps",  "blit_vs",    "blit_ps", "blit_fix_ps",
    "blend_int_vs", "blend_int_ps",    "fog_lut_vs", "fog_lut_ps", "font_vs", "font_ps",
};

int main(void)
{
    int failures = 0;
    for (unsigned i = 0; i < g_icoShaderCount; i++) {
        const IcoShaderBlob *b = &g_icoShaders[i];
        const uint8_t *s = b->spirv, *d = b->dxil;
        unsigned magic =
            (unsigned)s[0] | (unsigned)s[1] << 8 | (unsigned)s[2] << 16 | (unsigned)s[3] << 24;
        int hashed = 0;
        for (int k = 4; k < 20; k++) {
            hashed |= d[k];
        }
        if (b->spirv_len < 32 || b->spirv_len % 4 || magic != 0x07230203u) {
            printf("FAIL %s: SPIR-V\n", b->name);
            failures++;
        }
        if (b->dxil_len < 64 || memcmp(d, "DXBC", 4) != 0 || !hashed) {
            printf("FAIL %s: DXIL missing or unsigned\n", b->name);
            failures++;
        }
        if (b->stage != ICO_SHADER_STAGE_VERTEX && b->stage != ICO_SHADER_STAGE_FRAGMENT) {
            printf("FAIL %s: stage\n", b->name);
            failures++;
        }
        if (!b->entry || !b->entry[0] || ico_FindShader(b->name) != b) {
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
        if (!ico_FindShader(expected[i])) {
            printf("FAIL %s: not in the table\n", expected[i]);
            failures++;
        }
    }
    if (g_icoShaderCount != sizeof(expected) / sizeof(expected[0])) {
        printf("FAIL table has %u entries, expected %zu\n", g_icoShaderCount,
               sizeof(expected) / sizeof(expected[0]));
        failures++;
    }
    printf("shaders_table_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
