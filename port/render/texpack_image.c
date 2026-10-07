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
