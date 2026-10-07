/* texpack_pending_stubs.c: stand-ins for the texture pack's names
 * (texpack_name.c) while it is not in the tree yet
 * (port/render/CMakeLists.txt adds this file only then).  Every name is
 * refused, so no pack is ever indexed and the game's own textures are
 * drawn.  Deleted when texpack_name.c lands.
 */
#include <string.h>

#include "texpack.h"

#if ICO_TEXPACK_STUB_NAMES
int texpack_Candidates(const TexpackSource *src, uint32_t boundLevel, TexpackName *out, int max)
{
    (void)src;
    (void)boundLevel;
    (void)out;
    (void)max;
    return -1;
}

int texpack_FormatName(const TexpackName *n, char *buf, size_t size)
{
    (void)n;
    (void)buf;
    (void)size;
    return -1;
}

int texpack_ParseName(const char *fileName, TexpackName *out)
{
    (void)fileName;
    memset(out, 0, sizeof(*out));
    return -1;
}
#endif
