/* rhi_gl_glsl_test.c: the OpenGL backend's GLSL inflate (port/rhi/gl/gl_glsl.c)
 * on the CPU, on every host: a text deflated by miniz (a zlib stream, as
 * embed_shaders.py's zlib.compress writes) round-trips, a larger or smaller
 * text length, a corrupt or truncated stream, no stream and an empty text
 * give NULL, and the result is NUL-terminated.
 *
 * Exit 0 on success, 1 on a failure. */
#include "gl/gl_glsl.h"
#include "miniz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

int main(void)
{
    static const char text[] =
        "#version 430\n"
        "layout(binding = 1, std140) uniform type_DrawCB { vec4 g_col; } DrawCB;\n"
        "layout(location = 0, index = 1) out vec4 o;\n"
        "void main() { o = DrawCB.g_col; o = o * 0.5; o = o + o; o = o; }\n";
    size_t textLen = sizeof text - 1;
    unsigned char deflated[512] = {0};
    mz_ulong dn = sizeof deflated;
    CHECK(mz_compress2(deflated, &dn, (const unsigned char *)text, (mz_ulong)textLen, 9) == MZ_OK);
    CHECK(dn > 8 && deflated[0] == 0x78); /* a zlib header, what the table's check expects */

    char *out = rhi_gl_inflate_glsl(deflated, dn, textLen);
    CHECK(out != NULL);
    if (out) {
        CHECK(strlen(out) == textLen);
        CHECK(memcmp(out, text, textLen + 1) == 0); /* with the NUL */
        free(out);
    }

    /* a wrong length either way fails */
    CHECK(rhi_gl_inflate_glsl(deflated, dn, textLen + 1) == NULL);
    CHECK(rhi_gl_inflate_glsl(deflated, dn, textLen - 1) == NULL);
    CHECK(rhi_gl_inflate_glsl(deflated, dn, 0) == NULL);
    /* no stream, an empty stream, a truncated one, a corrupt one */
    CHECK(rhi_gl_inflate_glsl(NULL, dn, textLen) == NULL);
    CHECK(rhi_gl_inflate_glsl(deflated, 0, textLen) == NULL);
    CHECK(rhi_gl_inflate_glsl(deflated, dn / 2, textLen) == NULL);
    unsigned char bad[512];
    memcpy(bad, deflated, dn);
    bad[0] = 0x00;
    CHECK(rhi_gl_inflate_glsl(bad, dn, textLen) == NULL);
    memcpy(bad, deflated, dn);
    bad[dn - 1] ^= 0xff; /* the Adler-32 */
    CHECK(rhi_gl_inflate_glsl(bad, dn, textLen) == NULL);

    if (failures) {
        printf("rhi_gl_glsl_test: %d failures\n", failures);
        return 1;
    }
    printf("rhi_gl_glsl_test: ok (%zu bytes of text in %lu deflated)\n", textLen,
           (unsigned long)dn);
    return 0;
}
