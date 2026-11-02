/*
 * port/render/rd_cache_name.c
 *
 * The Vulkan pipeline cache file's name from the shader set and the
 * pipeline format (rd_cache_name.h).
 */
#include "rd_cache_name.h"

#include <stdio.h>
#include <string.h>

#include "xxh3.h"

#define FNV_OFFSET 0xcbf29ce484222325ull
#define FNV_PRIME 0x100000001b3ull

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= FNV_PRIME;
    }
    return h;
}

/* v as 8 bytes, least significant first (the same key on any host) */
static uint64_t fnv64(uint64_t h, uint64_t v)
{
    unsigned char b[8];
    for (int i = 0; i < 8; i++) {
        b[i] = (unsigned char)(v >> (8 * i));
    }
    return fnv(h, b, sizeof b);
}

void rd_cache_key_begin(RdCacheKey *k, uint32_t format)
{
    static const char tag[] = "ico-pc pipeline cache";
    k->h = fnv(FNV_OFFSET, tag, sizeof tag - 1);
    k->h = fnv64(k->h, format);
    k->modules = 0;
}

void rd_cache_key_module(RdCacheKey *k, const char *name, int stage, const void *spirv, size_t len)
{
    const char *n = name != NULL ? name : "";
    /* the name with its end, so "ab" + "c" is not "a" + "bc" */
    k->h = fnv(k->h, n, strlen(n) + 1);
    k->h = fnv64(k->h, (uint64_t)(int64_t)stage);
    k->h = fnv64(k->h, (uint64_t)len);
    k->h = fnv64(k->h, xxh3_64(len != 0 ? spirv : NULL, len));
    k->modules++;
}

uint64_t rd_cache_key_end(const RdCacheKey *k)
{
    return fnv64(k->h, k->modules);
}

int rd_cache_file_name(char *out, size_t cap, uint64_t key)
{
    /* "pipelines-" 10 + 16 digits + ".vkcache" 8 + the end */
    if (out == NULL || cap < 35) {
        if (out != NULL && cap > 0) {
            out[0] = '\0';
        }
        return -1;
    }
    snprintf(out, cap, "pipelines-%016llx.vkcache", (unsigned long long)key);
    return 0;
}
