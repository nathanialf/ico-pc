/* xxh3.h: XXH3-64 (xxh3.c), shared by the texture pack names
 * (texpack_name.h) and the mesh identity of model packs (rd_mesh.h
 * rd_vu_mesh_desc_hash).  Library ico_texpack_name. */
#ifndef PORT_RENDER_XXH3_H
#define PORT_RENDER_XXH3_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* XXH3-64 of n bytes at p, seed 0, the default secret (xxHash v0.8.2's
   XXH3_64bits).  p may be null when n is 0. */
uint64_t xxh3_64(const void *p, size_t n);

#ifdef __cplusplus
}
#endif

#endif
