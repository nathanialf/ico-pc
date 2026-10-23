/* d3d11_shader.c: the Direct3D 11 backend's run-time shader compile
 * (d3d11_shader.h).  The first part is portable C (the macros, the include
 * lookup, the flat-register rule, the cache key and file, the counters);
 * the compile through d3dcompiler_47.dll is the Windows part at the end. */
#ifdef _WIN32
#ifndef COBJMACROS
#define COBJMACROS
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3dcompiler.h>
#else
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "d3d11_shader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define D3D11S_LOG(...)                                                                            \
    do {                                                                                           \
        fprintf(stderr, "rhi_d3d11: " __VA_ARGS__);                                                \
        fputc('\n', stderr);                                                                       \
    } while (0)

/* --------------------------------------------------------------- macros */
static bool d3d11s_ident_start(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static bool d3d11s_ident(char c)
{
    return d3d11s_ident_start(c) || (c >= '0' && c <= '9');
}

int d3d11_shader_defines(const char *defines, D3d11Define *out, int max)
{
    int n = 0;
    if (!defines || !defines[0]) {
        return 0;
    }
    const char *p = defines;
    for (;;) {
        const char *end = strchr(p, ',');
        const size_t itemLen = end ? (size_t)(end - p) : strlen(p);
        const char *eq = itemLen ? memchr(p, '=', itemLen) : NULL;
        const size_t nameLen = eq ? (size_t)(eq - p) : itemLen;
        const char *val = eq ? eq + 1 : "1";
        const size_t valLen = eq ? itemLen - nameLen - 1 : 1;
        if (n >= max || nameLen == 0 || nameLen >= D3D11_SHADER_DEFINE_TEXT ||
            valLen >= D3D11_SHADER_DEFINE_TEXT || !d3d11s_ident_start(p[0])) {
            return -1;
        }
        for (size_t i = 1; i < nameLen; i++) {
            if (!d3d11s_ident(p[i])) {
                return -1;
            }
        }
        memcpy(out[n].name, p, nameLen);
        out[n].name[nameLen] = '\0';
        memcpy(out[n].value, val, valLen);
        out[n].value[valLen] = '\0';
        n++;
        if (!end) {
            return n;
        }
        p = end + 1;
    }
}

/* -------------------------------------------------------------- sources */
static const char *d3d11s_base_name(const char *path)
{
    const char *b = path;
    for (const char *c = path; *c; c++) {
        if (*c == '/' || *c == '\\') {
            b = c + 1;
        }
    }
    return b;
}

const RhiShaderSource *d3d11_shader_find_source(const RhiShaderSource *s, unsigned n,
                                                const char *name)
{
    if (!s || !name) {
        return NULL;
    }
    for (unsigned i = 0; i < n; i++) {
        if (s[i].name && strcmp(s[i].name, name) == 0) {
            return &s[i];
        }
    }
    const char *base = d3d11s_base_name(name);
    if (base == name) {
        return NULL;
    }
    for (unsigned i = 0; i < n; i++) {
        if (s[i].name && strcmp(s[i].name, base) == 0) {
            return &s[i];
        }
    }
    return NULL;
}

static bool d3d11s_has_space(const char *t, size_t len)
{
    for (size_t i = 0; i + 5 <= len; i++) {
        if (memcmp(t + i, "space", 5) == 0) {
            return true;
        }
    }
    return false;
}

/* one source: register(...) in code (not a comment, a string or a
 * preprocessor line) whose parenthesis names a space */
static bool d3d11s_scan(const RhiShaderSource *src, char *why, size_t whyn)
{
    const char *t = src->text;
    const size_t len = src->len;
    unsigned line = 1;
    bool lineStart = true;
    size_t i = 0;
    while (i < len) {
        const char c = t[i];
        if (c == '\n') {
            line++;
            lineStart = true;
            i++;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            i++;
        } else if (c == '/' && i + 1 < len && t[i + 1] == '/') {
            while (i < len && t[i] != '\n') {
                i++;
            }
        } else if (c == '/' && i + 1 < len && t[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(t[i] == '*' && t[i + 1] == '/')) {
                line += t[i] == '\n';
                i++;
            }
            i += 2;
        } else if (c == '#' && lineStart) {
            /* a directive, with its backslash-continued lines: the macros
             * that pick the register form live here (reg.hlsli's switch) */
            while (i < len && t[i] != '\n') {
                if (t[i] == '\\' && i + 1 < len && t[i + 1] == '\n') {
                    line++;
                    i++;
                }
                i++;
            }
        } else if (c == '"') {
            lineStart = false;
            i++;
            while (i < len && t[i] != '"' && t[i] != '\n') {
                i += t[i] == '\\' ? 2 : 1;
            }
            i++;
        } else if (d3d11s_ident_start(c)) {
            lineStart = false;
            const size_t b = i;
            while (i < len && d3d11s_ident(t[i])) {
                i++;
            }
            if (i - b != 8 || memcmp(t + b, "register", 8) != 0) {
                continue;
            }
            size_t j = i;
            while (j < len && (t[j] == ' ' || t[j] == '\t')) {
                j++;
            }
            if (j >= len || t[j] != '(') {
                continue;
            }
            size_t k = j + 1;
            while (k < len && t[k] != ')' && t[k] != '\n') {
                k++;
            }
            if (d3d11s_has_space(t + j + 1, k - j - 1)) {
                if (why && whyn) {
                    snprintf(why, whyn,
                             "%s:%u: register(%.*s) names a register space, which shader model "
                             "5.0 does not have (use ICO_REG)",
                             src->name, line, (int)(k - j - 1), t + j + 1);
                }
                return false;
            }
        } else {
            lineStart = false;
            i++;
        }
    }
    return true;
}

bool d3d11_shader_check_sources(const RhiShaderSource *s, unsigned n, char *why, size_t whyn)
{
    if (why && whyn) {
        why[0] = '\0';
    }
    for (unsigned i = 0; i < n; i++) {
        if (!s[i].name || !s[i].text) {
            if (why && whyn) {
                snprintf(why, whyn, "source %u: no name or no text", i);
            }
            return false;
        }
        if (strcmp(d3d11s_base_name(s[i].name), "reg.hlsli") == 0) {
            continue; /* the macro's own definition */
        }
        if (!d3d11s_scan(&s[i], why, whyn)) {
            return false;
        }
    }
    return true;
}

/* ---------------------------------------------------------------- cache */
uint64_t d3d11_shader_fnv(uint64_t h, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

static void d3d11s_put64(unsigned char *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) {
        p[i] = (unsigned char)(v >> (8 * i));
    }
}

static uint64_t d3d11s_get64(const unsigned char *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v |= (uint64_t)p[i] << (8 * i);
    }
    return v;
}

/* a field: its length (8 bytes), then its bytes, so no two different
 * field lists hash the same text */
static uint64_t d3d11s_fnv_field(uint64_t h, const void *data, size_t len)
{
    unsigned char n[8];
    d3d11s_put64(n, (uint64_t)len);
    h = d3d11_shader_fnv(h, n, sizeof(n));
    return data ? d3d11_shader_fnv(h, data, len) : h;
}

static uint64_t d3d11s_fnv_str(uint64_t h, const char *s)
{
    return d3d11s_fnv_field(h, s, s ? strlen(s) : 0);
}

uint64_t d3d11_shader_cache_key(const RhiShaderDesc *d, const char *compilerVersion,
                                const char *icoVersion)
{
    uint64_t h = D3D11_FNV_SEED;
    unsigned char stage[8];
    d3d11s_put64(stage, (uint64_t)d->stage);
    h = d3d11s_fnv_str(h, d->entryPoint);
    h = d3d11_shader_fnv(h, stage, sizeof(stage));
    h = d3d11s_fnv_str(h, d->hlslFile);
    h = d3d11s_fnv_str(h, d->defines);
    d3d11s_put64(stage, (uint64_t)d->sourceCount);
    h = d3d11_shader_fnv(h, stage, sizeof(stage));
    for (unsigned i = 0; d->sources && i < d->sourceCount; i++) {
        h = d3d11s_fnv_str(h, d->sources[i].name);
        h = d3d11s_fnv_field(h, d->sources[i].text, d->sources[i].text ? d->sources[i].len : 0);
    }
    h = d3d11s_fnv_str(h, compilerVersion);
    h = d3d11s_fnv_str(h, icoVersion);
    return h;
}

void d3d11_shader_cache_path(char *out, size_t n, const char *dir, uint64_t key)
{
    snprintf(out, n, "%s/%016llx.dxbc", dir ? dir : ".", (unsigned long long)key);
}

void *d3d11_shader_cache_read(const char *path, uint64_t key, size_t *len)
{
    *len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    unsigned char h[D3D11_SHADER_CACHE_HEADER];
    unsigned char *body = NULL;
    if (fread(h, 1, sizeof(h), f) != sizeof(h) || memcmp(h, D3D11_SHADER_CACHE_MAGIC, 8) != 0) {
        goto fail;
    }
    const uint64_t k = d3d11s_get64(h + 8), n = d3d11s_get64(h + 16), sum = d3d11s_get64(h + 24);
    if (k != key || n < 4 || n > D3D11_SHADER_CACHE_MAX) {
        goto fail;
    }
    body = (unsigned char *)malloc((size_t)n);
    if (!body || fread(body, 1, (size_t)n, f) != (size_t)n || fgetc(f) != EOF ||
        d3d11_shader_fnv(D3D11_FNV_SEED, body, (size_t)n) != sum || memcmp(body, "DXBC", 4) != 0) {
        goto fail;
    }
    fclose(f);
    *len = (size_t)n;
    return body;
fail:
    free(body);
    fclose(f);
    return NULL;
}

static uint32_t s_tmpSerial;

bool d3d11_shader_cache_write(const char *path, uint64_t key, const void *dxbc, size_t len)
{
    if (!path || !dxbc || len < 4 || len > D3D11_SHADER_CACHE_MAX) {
        return false;
    }
    char tmp[1100];
    const uint32_t serial = __atomic_fetch_add(&s_tmpSerial, 1u, __ATOMIC_RELAXED);
    if ((size_t)snprintf(tmp, sizeof(tmp), "%s.%u.tmp", path, (unsigned)serial) >= sizeof(tmp)) {
        return false;
    }
    unsigned char h[D3D11_SHADER_CACHE_HEADER];
    memcpy(h, D3D11_SHADER_CACHE_MAGIC, 8);
    d3d11s_put64(h + 8, key);
    d3d11s_put64(h + 16, (uint64_t)len);
    d3d11s_put64(h + 24, d3d11_shader_fnv(D3D11_FNV_SEED, dxbc, len));
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        return false;
    }
    bool ok = fwrite(h, 1, sizeof(h), f) == sizeof(h) && fwrite(dxbc, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
#ifdef _WIN32
    ok = ok && MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    ok = ok && rename(tmp, path) == 0;
#endif
    if (!ok) {
        remove(tmp);
    }
    return ok;
}

bool d3d11_shader_cache_mkdir(const char *dir)
{
    if (!dir || !dir[0]) {
        return false;
    }
#ifdef _WIN32
    if (CreateDirectoryA(dir, NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
        const DWORD a = GetFileAttributesA(dir);
        return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
    }
    return false;
#else
    if (mkdir(dir, 0755) == 0) {
        return true;
    }
    struct stat st;
    return errno == EEXIST && stat(dir, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

/* ------------------------------------------------------------- counters */
static uint32_t s_hits, s_compiles, s_failed;

void d3d11_shader_stats(const char *cacheDir, D3d11ShaderStats *out, bool log)
{
    D3d11ShaderStats st;
    st.hits = __atomic_load_n(&s_hits, __ATOMIC_RELAXED);
    st.compiles = __atomic_load_n(&s_compiles, __ATOMIC_RELAXED);
    st.failed = __atomic_load_n(&s_failed, __ATOMIC_RELAXED);
    if (out) {
        *out = st;
    }
    if (log) {
        D3D11S_LOG("shader cache %s: %u hits, %u compiles, %u failed",
                   cacheDir && cacheDir[0] ? cacheDir : "(none)", (unsigned)st.hits,
                   (unsigned)st.compiles, (unsigned)st.failed);
    }
}

bool d3d11_shader_log_failure(uint32_t n)
{
    return n >= 1 && (n <= 8 || n % 100 == 0);
}

/* the shader's name for the log lines */
static const char *d3d11s_name(const RhiShaderDesc *d)
{
    return d->debugName ? d->debugName : d->entryPoint ? d->entryPoint : "?";
}

/* a failure that never reached the compiler (no source, bad macros, no
 * compiler): counted with the compiles that failed, the reason in err and
 * in the log under the same limit */
static bool d3d11s_fail(const RhiShaderDesc *d, char *err, size_t errn, const char *why)
{
    const uint32_t n = __atomic_add_fetch(&s_failed, 1u, __ATOMIC_RELAXED);
    if (err && errn) {
        snprintf(err, errn, "%s", why);
    }
    if (n <= 8) {
        D3D11S_LOG("shader %s: %s", d3d11s_name(d), why);
    } else if (d3d11_shader_log_failure(n)) {
        D3D11S_LOG("%u shaders failed so far", (unsigned)n);
    }
    return false;
}

/* --------------------------------------------------------------- compile */
#ifdef _WIN32

/* the compiler, loaded once (InitOnceExecuteOnce: the workers may ask
 * together); kept loaded for the process */
static INIT_ONCE s_loadOnce = INIT_ONCE_STATIC_INIT;
static pD3DCompile s_d3dCompile;
static char s_compilerVersion[48];
static uint32_t s_warned;   /* the first warning text logged */
static uint32_t s_cacheBad; /* the unwritable cache dir logged */

/* the module's file version from its VS_VERSIONINFO resource (the
 * VS_FIXEDFILEINFO after its 0xFEEF04BD signature), without version.dll */
static void d3d11s_module_version(HMODULE m, char *out, size_t n)
{
    snprintf(out, n, "version unknown");
    HRSRC r = FindResourceA(m, MAKEINTRESOURCEA(1), MAKEINTRESOURCEA(16) /* RT_VERSION */);
    HGLOBAL g = r ? LoadResource(m, r) : NULL;
    const unsigned char *p = g ? (const unsigned char *)LockResource(g) : NULL;
    const DWORD size = r ? SizeofResource(m, r) : 0;
    for (DWORD i = 0; p && i + 16 <= size; i += 4) {
        uint32_t w[4];
        memcpy(w, p + i, sizeof(w));
        if (w[0] == 0xFEEF04BDu) {
            /* dwSignature, dwStrucVersion, dwFileVersionMS, dwFileVersionLS */
            snprintf(out, n, "%u.%u.%u.%u", (unsigned)(w[2] >> 16), (unsigned)(w[2] & 0xFFFFu),
                     (unsigned)(w[3] >> 16), (unsigned)(w[3] & 0xFFFFu));
            return;
        }
    }
}

static BOOL CALLBACK d3d11s_load(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    (void)once;
    (void)param;
    (void)ctx;
    /* the system's copy first (System32), then the usual search, which
     * finds one placed beside the program on a system without it */
    HMODULE m = LoadLibraryExA("d3dcompiler_47.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m) {
        m = LoadLibraryA("d3dcompiler_47.dll");
    }
    s_d3dCompile = m ? (pD3DCompile)(void *)GetProcAddress(m, "D3DCompile") : NULL;
    if (!s_d3dCompile) {
        D3D11S_LOG("d3dcompiler_47.dll not found or without D3DCompile (error %lu): the "
                   "Direct3D 11 renderer cannot compile its shaders",
                   (unsigned long)GetLastError());
        return TRUE;
    }
    char file[MAX_PATH];
    const DWORD fn = GetModuleFileNameA(m, file, (DWORD)sizeof(file));
    if (fn == 0 || fn >= sizeof(file)) {
        snprintf(file, sizeof(file), "?");
    }
    d3d11s_module_version(m, s_compilerVersion, sizeof(s_compilerVersion));
    D3D11S_LOG("shader compiler d3dcompiler_47.dll loaded (%s) from %s", s_compilerVersion, file);
    return TRUE;
}

static bool d3d11s_compiler(void)
{
    InitOnceExecuteOnce(&s_loadOnce, d3d11s_load, NULL, NULL);
    return s_d3dCompile != NULL;
}

const char *d3d11_shader_compiler_version(void)
{
    BOOL pending = FALSE;
    /* without loading it: NULL until a compile has */
    if (!InitOnceBeginInitialize(&s_loadOnce, INIT_ONCE_CHECK_ONLY, &pending, NULL) || pending) {
        return NULL;
    }
    return s_d3dCompile ? s_compilerVersion : NULL;
}

/* ID3DInclude as a C vtable: Open hands out the embedded text (no copy,
 * so Close has nothing to free); the object lives on the compiling
 * thread's stack */
typedef struct D3d11Include {
    ID3DInclude iface; /* first: the compiler's pointer is this struct's */
    const RhiShaderSource *sources;
    unsigned count;
} D3d11Include;

static HRESULT STDMETHODCALLTYPE d3d11s_inc_open(ID3DInclude *self, D3D_INCLUDE_TYPE type,
                                                 const char *name, const void *parent,
                                                 const void **data, UINT *bytes)
{
    (void)type;
    (void)parent;
    const D3d11Include *inc = (const D3d11Include *)self;
    const RhiShaderSource *s = d3d11_shader_find_source(inc->sources, inc->count, name);
    if (!s || !s->text) {
        return E_FAIL;
    }
    *data = s->text;
    *bytes = (UINT)s->len;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11s_inc_close(ID3DInclude *self, const void *data)
{
    (void)self;
    (void)data;
    return S_OK;
}

static ID3DIncludeVtbl s_includeVtbl = {.Open = d3d11s_inc_open, .Close = d3d11s_inc_close};

/* the compiler's message blob as text in out (it is NUL-terminated by the
 * compiler, but not trusted to be) */
static void d3d11s_blob_text(ID3DBlob *b, char *out, size_t n)
{
    if (!out || !n) {
        return;
    }
    out[0] = '\0';
    if (!b) {
        return;
    }
    const char *p = (const char *)ID3D10Blob_GetBufferPointer(b);
    const size_t len = (size_t)ID3D10Blob_GetBufferSize(b);
    size_t k = len < n - 1 ? len : n - 1;
    memcpy(out, p, k);
    while (k > 0 && (out[k - 1] == '\0' || out[k - 1] == '\n' || out[k - 1] == '\r')) {
        k--;
    }
    out[k] = '\0';
}

bool d3d11_shader_compile(const RhiShaderDesc *d, const char *cacheDir, const char *icoVersion,
                          void **dxbc, size_t *len, char *err, size_t errn)
{
    char why[1024];
    *dxbc = NULL;
    *len = 0;
    if (err && errn) {
        err[0] = '\0';
    }
    if (!d->hlslFile || !d->entryPoint || !d->sources || d->sourceCount == 0) {
        return d3d11s_fail(d, err, errn,
                           "no HLSL file, entry or embedded sources (built without "
                           "ICO_SHADERS_HLSL_SOURCE?)");
    }
    const RhiShaderSource *src = d3d11_shader_find_source(d->sources, d->sourceCount, d->hlslFile);
    if (!src || !src->text) {
        snprintf(why, sizeof(why), "%s is not among the %u embedded sources", d->hlslFile,
                 d->sourceCount);
        return d3d11s_fail(d, err, errn, why);
    }
    D3d11Define defs[D3D11_SHADER_DEFINES_MAX];
    const int nd = d3d11_shader_defines(d->defines, defs, D3D11_SHADER_DEFINES_MAX);
    if (nd < 0) {
        snprintf(why, sizeof(why), "malformed macros \"%s\"", d->defines ? d->defines : "");
        return d3d11s_fail(d, err, errn, why);
    }
    if (!d3d11s_compiler()) {
        return d3d11s_fail(d, err, errn, "d3dcompiler_47.dll is not available");
    }

    /* the cache first */
    const bool cached = cacheDir && cacheDir[0];
    const uint64_t key = d3d11_shader_cache_key(d, s_compilerVersion, icoVersion);
    char path[1024];
    if (cached) {
        d3d11_shader_cache_path(path, sizeof(path), cacheDir, key);
        *dxbc = d3d11_shader_cache_read(path, key, len);
        if (*dxbc) {
            __atomic_add_fetch(&s_hits, 1u, __ATOMIC_RELAXED);
            return true;
        }
    }

    /* the macros: the entry's, then ICO_D3D11 (reg.hlsli's flat registers) */
    D3D_SHADER_MACRO macros[D3D11_SHADER_DEFINES_MAX + 2];
    int m = 0;
    for (int i = 0; i < nd; i++) {
        macros[m].Name = defs[i].name;
        macros[m].Definition = defs[i].value;
        m++;
    }
    macros[m].Name = "ICO_D3D11";
    macros[m].Definition = "1";
    m++;
    macros[m].Name = NULL;
    macros[m].Definition = NULL;

    D3d11Include inc;
    inc.iface.lpVtbl = &s_includeVtbl;
    inc.sources = d->sources;
    inc.count = d->sourceCount;
    const char *target = d->stage == RHI_STAGE_VERTEX ? "vs_5_0" : "ps_5_0";
    ID3DBlob *code = NULL, *msgs = NULL;
    const HRESULT hr =
        s_d3dCompile(src->text, (SIZE_T)src->len, src->name, macros, &inc.iface, d->entryPoint,
                     target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &msgs);
    if (FAILED(hr) || !code) {
        char text[1024];
        d3d11s_blob_text(msgs, text, sizeof(text));
        if (msgs) {
            ID3D10Blob_Release(msgs);
        }
        if (code) {
            ID3D10Blob_Release(code);
        }
        snprintf(why, sizeof(why), "%s %s (%s) failed to compile (HRESULT 0x%08lx): %s", src->name,
                 d->entryPoint, target, (unsigned long)hr, text[0] ? text : "no message");
        return d3d11s_fail(d, err, errn, why);
    }
    if (msgs) {
        /* warnings: the first text once, so a compiler that warns where DXC
         * did not is visible without a line per shader */
        if (__atomic_exchange_n(&s_warned, 1u, __ATOMIC_RELAXED) == 0) {
            char text[1024];
            d3d11s_blob_text(msgs, text, sizeof(text));
            if (text[0]) {
                D3D11S_LOG("shader compiler warnings (first shader with any, %s): %s",
                           d3d11s_name(d), text);
            }
        }
        ID3D10Blob_Release(msgs);
    }
    const size_t n = (size_t)ID3D10Blob_GetBufferSize(code);
    void *out = n ? malloc(n) : NULL;
    if (!out) {
        ID3D10Blob_Release(code);
        return d3d11s_fail(d, err, errn, "out of memory for the DXBC");
    }
    memcpy(out, ID3D10Blob_GetBufferPointer(code), n);
    ID3D10Blob_Release(code);
    __atomic_add_fetch(&s_compiles, 1u, __ATOMIC_RELAXED);
    if (cached &&
        !(d3d11_shader_cache_mkdir(cacheDir) && d3d11_shader_cache_write(path, key, out, n)) &&
        __atomic_exchange_n(&s_cacheBad, 1u, __ATOMIC_RELAXED) == 0) {
        D3D11S_LOG("shader cache %s could not be written: the shaders are compiled at every start",
                   cacheDir);
    }
    *dxbc = out;
    *len = n;
    return true;
}

#else /* !_WIN32 */

bool d3d11_shader_compile(const RhiShaderDesc *d, const char *cacheDir, const char *icoVersion,
                          void **dxbc, size_t *len, char *err, size_t errn)
{
    (void)cacheDir;
    (void)icoVersion;
    *dxbc = NULL;
    *len = 0;
    return d3d11s_fail(d, err, errn, "Direct3D 11 shaders compile on Windows only");
}

const char *d3d11_shader_compiler_version(void)
{
    return NULL;
}

#endif
