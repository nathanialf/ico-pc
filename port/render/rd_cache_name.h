/*
 * port/render/rd_cache_name.h
 *
 * The Vulkan pipeline cache file's name: pipelines-<key>.vkcache, the key
 * 16 hex digits of a 64-bit hash of what the driver's pipelines are made
 * from, not of the build.  The driver's cache data is its compiled
 * pipelines, each found by the driver's own hash of the whole creation
 * (the SPIR-V modules and every state); its header names the device and
 * the driver (vendor id, device id, pipelineCacheUUID), which
 * port/rhi/vk/vk_pipeline.c vkr_cache_load checks before the data is
 * used.  So the file is keyed by:
 *   - every embedded SPIR-V module, by its name, stage, length and the
 *     XXH3-64 of its bytes (the table g_icoShaders the renderer loads its
 *     modules from, port/shaders/embed_shaders.py), in the table's order;
 *   - RD_PIPELINE_CACHE_FORMAT, raised by hand when a change must not use
 *     the caches written before it (a creation the driver's hash would take
 *     for an old one; none is known: a state change alone makes new
 *     pipelines in the same file).
 * A build whose shaders are the same as the last one's names the same file
 * and starts warm; a build that changed any module names another, and the
 * other pipelines*.vkcache files are removed (vk_pipeline.c
 * vkr_cache_drop_others: the per-build names pipelines-<commit>.vkcache of
 * the builds before this rule among them).  The Direct3D 11 backend's DXBC
 * folder takes the same name with .d3d11 (d3d11_shader.h
 * d3d11_shader_cache_folder); each of its files is keyed by the HLSL
 * sources themselves, so a source change the SPIR-V does not show still
 * compiles again.
 *
 * Pure C (rd_cache_name.c, library ico_cache_name with ico_texpack_name's
 * xxh3.c): the unit test is port/render/test/rd_cache_name_test.c; the
 * renderer's walk of its own table is rd_pipeline_cache_file (rd.h).
 */
#ifndef PORT_RENDER_RD_CACHE_NAME_H
#define PORT_RENDER_RD_CACHE_NAME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the pipeline format's number in the key (above) */
#define RD_PIPELINE_CACHE_FORMAT 1u

/* the key being made: rd_cache_key_begin, one rd_cache_key_module per
   module, rd_cache_key_end */
typedef struct RdCacheKey {
    uint64_t h;       /* FNV-1a 64 over the records so far */
    uint32_t modules; /* the modules added */
} RdCacheKey;

void rd_cache_key_begin(RdCacheKey *k, uint32_t format);
/* one module: its name (NULL as ""), stage, and len bytes at spirv (NULL
   when len is 0) */
void rd_cache_key_module(RdCacheKey *k, const char *name, int stage, const void *spirv, size_t len);
/* the key (the module count folded in) */
uint64_t rd_cache_key_end(const RdCacheKey *k);

/* "pipelines-<key as 16 lowercase hex digits>.vkcache" into out; 0, or -1
   (out "" when cap allows) when cap is under 35 bytes (34 and the end) */
int rd_cache_file_name(char *out, size_t cap, uint64_t key);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RENDER_RD_CACHE_NAME_H */
