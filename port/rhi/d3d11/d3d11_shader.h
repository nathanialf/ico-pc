/* d3d11_shader.h: the Direct3D 11 backend's shader route.
 *
 * Shader model 5.0 has no compiler on the build hosts (FXC is Windows-only),
 * so the backend compiles the embedded HLSL at run time with
 * d3dcompiler_47.dll (a Windows 10/11 system component, loaded with
 * LoadLibrary as d3d12.dll is) and keeps each result in an on-disk DXBC
 * cache, one file per shader, keyed by everything the result depends on.
 *
 * The unit has no device: the backend's pipeline workers call
 * d3d11_shader_compile, concurrently, for every vertex and pixel shader a
 * pipeline needs.  Everything but the compile itself is portable (no
 * Windows header) and runs in rhi_d3d11_shader_plan on every host.
 *
 * docs/port/SHADERS.md, "Direct3D 11: run-time compile". */
#ifndef PORT_RHI_D3D11_D3D11_SHADER_H
#define PORT_RHI_D3D11_D3D11_SHADER_H

#include "../rhi.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------- the macros */
#define D3D11_SHADER_DEFINES_MAX 16
#define D3D11_SHADER_DEFINE_TEXT 64

typedef struct D3d11Define {
    char name[D3D11_SHADER_DEFINE_TEXT];
    char value[D3D11_SHADER_DEFINE_TEXT];
} D3d11Define;

/* The table entry's macros ("A=1,B=2"; "" or NULL for none) as name/value
 * pairs; a name without '=' gets "1".  Returns the count, or -1 when the
 * text is malformed (an empty item, a name that is not an identifier, a
 * name or value too long, more than max items). */
int d3d11_shader_defines(const char *defines, D3d11Define *out, int max);

/* ------------------------------------------------------------ the sources */
/* The embedded source the compile's includes resolve to: the name as
 * written, else its last path component ("../shaders/common.hlsli" finds
 * "common.hlsli").  NULL when none. */
const RhiShaderSource *d3d11_shader_find_source(const RhiShaderSource *s, unsigned n,
                                                const char *name);

/* The flat-register rule FXC needs: no `register(...)` outside a
 * preprocessor line names a space (comments and strings skipped;
 * reg.hlsli, which holds the macro, exempt).  false with the first
 * offender ("file:line: ...") in why. */
bool d3d11_shader_check_sources(const RhiShaderSource *s, unsigned n, char *why, size_t whyn);

/* -------------------------------------------------------------- the cache */
/* FNV-1a 64 over bytes, continuing from h (D3D11_FNV_SEED to start) */
#define D3D11_FNV_SEED 0xcbf29ce484222325ull
uint64_t d3d11_shader_fnv(uint64_t h, const void *data, size_t len);

/* The cache key: FNV-1a 64 over the entry, the stage, the HLSL file, the
 * macros, every embedded source's name and text (in the table's order), the
 * compiler's version and the program's version, each length-prefixed. */
uint64_t d3d11_shader_cache_key(const RhiShaderDesc *d, const char *compilerVersion,
                                const char *icoVersion);

/* "<dir>/<key as 16 hex digits>.dxbc" */
void d3d11_shader_cache_path(char *out, size_t n, const char *dir, uint64_t key);

/* The cache file: a 32-byte header, then the DXBC.
 *   0  "ICODXBC1"
 *   8  the key (little-endian u64)
 *  16  the DXBC's length (little-endian u64)
 *  24  FNV-1a 64 of the DXBC (little-endian u64)
 *  32  the DXBC container ("DXBC" ...) */
#define D3D11_SHADER_CACHE_MAGIC "ICODXBC1"
#define D3D11_SHADER_CACHE_HEADER 32
#define D3D11_SHADER_CACHE_MAX (16u << 20) /* a body larger than this is refused */

/* The DXBC of path when its header names key and its body checks (length,
 * hash, "DXBC"); malloc'd, the caller frees.  NULL otherwise (no file, or
 * another key, a short or corrupt file: the caller compiles again and
 * writes over it). */
void *d3d11_shader_cache_read(const char *path, uint64_t key, size_t *len);
/* Writes the file through <path>.<n>.tmp renamed over path (workers may
 * write the same key at once: the last rename wins, with the same bytes). */
bool d3d11_shader_cache_write(const char *path, uint64_t key, const void *dxbc, size_t len);
/* mkdir dir (one level); true when it exists afterwards */
bool d3d11_shader_cache_mkdir(const char *dir);

/* ---------------------------------------------------------- the counters */
typedef struct D3d11ShaderStats {
    uint32_t hits;     /* cache files used instead of a compile */
    uint32_t compiles; /* successful compiles */
    uint32_t failed;   /* failed compiles (and shaders with no source) */
} D3d11ShaderStats;

/* The counters since the start; with log, the line
 * "rhi_d3d11: shader cache <dir>: N hits, M compiles, K failed". */
void d3d11_shader_stats(const char *cacheDir, D3d11ShaderStats *out, bool log);
/* Whether the n-th failure (1-based) gets its message logged: the first
 * eight, then every 100th */
bool d3d11_shader_log_failure(uint32_t n);

/* ------------------------------------------------------------ the compile */
/* The DXBC of d's entry (d->hlslFile's embedded text, d->entryPoint,
 * vs_5_0 / ps_5_0, d->defines plus ICO_D3D11=1, includes from d->sources),
 * from the cache in cacheDir when it holds this key (icoVersion is part of
 * it), else compiled and written there (cacheDir NULL or "": no cache).
 * *dxbc is malloc'd; the caller frees.  On failure, false and the
 * compiler's message (or the reason) in err.  Free-threaded: the only
 * shared state is the compiler's module, loaded once, and the counters.
 * Windows only; elsewhere it fails with a message. */
bool d3d11_shader_compile(const RhiShaderDesc *d, const char *cacheDir, const char *icoVersion,
                          void **dxbc, size_t *len, char *err, size_t errn);
/* "10.0.22621.3233" once the compiler is loaded ("version unknown" when its
 * file says none); NULL before the first compile or when it is missing */
const char *d3d11_shader_compiler_version(void);

#endif
