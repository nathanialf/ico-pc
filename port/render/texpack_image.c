/* texpack_image.c: the loaded image of a texture-pack file (texpack.h
 * TexpackImage), shared by the PNG and DDS loaders. */
#include <stdlib.h>
#include <string.h>
#include "texpack.h"

void texpack_FreeImage(TexpackImage *img)
{
    if (img == NULL) {
        return;
    }
    free(img->blob);
    memset(img, 0, sizeof(*img));
}

/* written by texpack_Init before the loader thread starts, read by the
   loaders after: the thread's creation orders the two */
static uint32_t s_maxSide;

void texpack_SetMaxSide(uint32_t side)
{
    s_maxSide = side;
}

uint32_t texpack_MaxSide(void)
{
    return s_maxSide;
}
