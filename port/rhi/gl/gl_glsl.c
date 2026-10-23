/* gl_glsl.c: inflating the table's GLSL (gl_glsl.h). */
#include "gl_glsl.h"
#include "miniz.h"
#include <stdlib.h>

char *rhi_gl_inflate_glsl(const void *deflated, size_t len, size_t textLen)
{
    if (!deflated || len == 0 || textLen == 0 || len > 0x7fffffffu || textLen > 0x7fffffffu) {
        return NULL;
    }
    char *text = malloc(textLen + 1);
    if (!text) {
        return NULL;
    }
    mz_ulong n = (mz_ulong)textLen;
    int rc =
        mz_uncompress((unsigned char *)text, &n, (const unsigned char *)deflated, (mz_ulong)len);
    if (rc != MZ_OK || n != (mz_ulong)textLen) {
        free(text);
        return NULL;
    }
    text[textLen] = '\0';
    return text;
}
