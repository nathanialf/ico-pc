/* rhi_d3d11_shader_plan_test.c: the Direct3D 11 backend's shader route on
 * the CPU (port/rhi/d3d11/d3d11_shader.c), on every host; no compiler
 * needed (the compile itself runs only on Windows).
 *
 *   - the macros text ("A=1,B=2") as name/value pairs, and the malformed
 *     forms refused
 *   - the include lookup by name and by a path's last component
 *   - the flat-register rule (no register space outside a comment, a
 *     string, a preprocessor line or reg.hlsli) on hand-made sources, on
 *     the test shaders' text (rhi_test_hlsl.h, which must match
 *     rhi_test.hlsl) and on the game's sources: the embedded ones with the
 *     shader table built with them (ICO_SHADERS_HLSL_SOURCE), else the
 *     files of port/shaders
 *   - the cache key: stable, changed by every input, distinct across the
 *     shader table's entries (ICO_HAVE_SHADER_TABLE), each of whose HLSL
 *     file resolves and whose macros parse
 *   - the cache file: the path, a round trip, and a wrong key, a changed
 *     byte, a short or a long file, a body that is not DXBC refused
 *   - the failure log's limit and the counters
 *
 * Exit 0 on success, 1 on a failure. */
#include "d3d11/d3d11_shader.h"
#include "rhi_test_hlsl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

#ifdef ICO_HAVE_SHADER_TABLE
#include "shaders_gen.h"
#endif

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* a file's bytes, NUL-terminated; NULL when it cannot be read */
static char *readFile(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    char *buf = NULL;
    size_t n = 0, cap = 0;
    for (;;) {
        if (n + 4096 + 1 > cap) {
            cap = cap ? cap * 2 : 65536;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) {
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = nb;
        }
        const size_t r = fread(buf + n, 1, 4096, f);
        n += r;
        if (r < 4096) {
            break;
        }
    }
    fclose(f);
    buf[n] = '\0';
    *len = n;
    return buf;
}

/* ---------------------------------------------------------------- macros */
static void testDefines(void)
{
    D3d11Define d[D3D11_SHADER_DEFINES_MAX];
    memset(d, 0, sizeof(d));
    CHECK(d3d11_shader_defines("", d, D3D11_SHADER_DEFINES_MAX) == 0, "empty text: no macros");
    CHECK(d3d11_shader_defines(NULL, d, D3D11_SHADER_DEFINES_MAX) == 0, "NULL: no macros");
    CHECK(d3d11_shader_defines("A=1,B_2=22", d, D3D11_SHADER_DEFINES_MAX) == 2, "two macros");
    CHECK(strcmp(d[0].name, "A") == 0 && strcmp(d[0].value, "1") == 0, "A=1: %s=%s", d[0].name,
          d[0].value);
    CHECK(strcmp(d[1].name, "B_2") == 0 && strcmp(d[1].value, "22") == 0, "B_2=22: %s=%s",
          d[1].name, d[1].value);
    CHECK(d3d11_shader_defines("ICO_NO_DUAL", d, D3D11_SHADER_DEFINES_MAX) == 1 &&
              strcmp(d[0].name, "ICO_NO_DUAL") == 0 && strcmp(d[0].value, "1") == 0,
          "a name alone is 1");
    CHECK(d3d11_shader_defines("A=", d, D3D11_SHADER_DEFINES_MAX) == 1 && d[0].value[0] == '\0',
          "an empty value is kept empty");
    const char *bad[] = {",A=1", "A=1,", "A=1,,B=2", "1A=2", "=1", "A B=1", "A-B=1"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK(d3d11_shader_defines(bad[i], d, D3D11_SHADER_DEFINES_MAX) == -1, "\"%s\" refused",
              bad[i]);
    }
    CHECK(d3d11_shader_defines("A=1,B=2,C=3", d, 2) == -1, "more macros than room: refused");
    char longName[D3D11_SHADER_DEFINE_TEXT + 8];
    memset(longName, 'N', sizeof(longName) - 1);
    longName[sizeof(longName) - 1] = '\0';
    CHECK(d3d11_shader_defines(longName, d, D3D11_SHADER_DEFINES_MAX) == -1, "long name refused");
}

/* --------------------------------------------------------------- sources */
static void testFindSource(void)
{
    const RhiShaderSource s[] = {
        {"common.hlsli", "// c\n", 5},
        {"reg.hlsli", "// r\n", 5},
        {"sprite.hlsl", "// s\n", 5},
    };
    CHECK(d3d11_shader_find_source(s, 3, "reg.hlsli") == &s[1], "by name");
    CHECK(d3d11_shader_find_source(s, 3, "../shaders/common.hlsli") == &s[0], "by a path's name");
    CHECK(d3d11_shader_find_source(s, 3, "port\\shaders\\sprite.hlsl") == &s[2],
          "by a Windows path's name");
    CHECK(d3d11_shader_find_source(s, 3, "missing.hlsli") == NULL, "missing");
    CHECK(d3d11_shader_find_source(s, 3, "dir/") == NULL, "a directory");
    CHECK(d3d11_shader_find_source(NULL, 0, "reg.hlsli") == NULL, "no table");
}

static bool checkOne(const char *name, const char *text, char *why, size_t whyn)
{
    RhiShaderSource s = {name, text, strlen(text)};
    return d3d11_shader_check_sources(&s, 1, why, whyn);
}

static void testCheckRule(void)
{
    char why[256];
    CHECK(checkOne("a.hlsl", "cbuffer A : register(b0) { float4 x; };\n", why, sizeof(why)),
          "a flat register passes: %s", why);
    CHECK(!checkOne("a.hlsl", "\n\ncbuffer A : register(b0, space0) { float4 x; };\n", why,
                    sizeof(why)),
          "a register space is refused");
    CHECK(strstr(why, "a.hlsl:3:") != NULL, "the offender's file and line: %s", why);
    CHECK(!checkOne("a.hlsl", "Texture2D t : register (t1,space2);\n", why, sizeof(why)),
          "spacing does not hide it");
    CHECK(checkOne("a.hlsl", "// cbuffer A : register(b0, space0)\n", why, sizeof(why)),
          "a line comment passes: %s", why);
    CHECK(checkOne("a.hlsl", "/* register(b0,\n space0) */ float4 x;\n", why, sizeof(why)),
          "a block comment passes: %s", why);
    CHECK(checkOne("a.hlsl", "#define R(r, s) register(r, space##s)\n", why, sizeof(why)),
          "a preprocessor line passes: %s", why);
    CHECK(checkOne("a.hlsl", "#define R(r, s) \\\n    register(r, space##s)\nfloat4 x;\n", why,
                   sizeof(why)),
          "a continued preprocessor line passes: %s", why);
    CHECK(
        checkOne("a.hlsl", "static const char *s = \"register(b0, space0)\";\n", why, sizeof(why)),
        "a string passes: %s", why);
    CHECK(
        checkOne("a.hlsl", "float my_register(float space) { return space; }\n", why, sizeof(why)),
        "another identifier passes: %s", why);
    CHECK(checkOne("reg.hlsli", "float4 x : register(c0, space1);\n", why, sizeof(why)),
          "reg.hlsli is exempt: %s", why);
    RhiShaderSource none = {NULL, NULL, 0};
    CHECK(!d3d11_shader_check_sources(&none, 1, why, sizeof(why)), "a source without a name");
}

/* rhi_test_hlsl.h is rhi_test.hlsl as it is now, and FXC can take it */
static void testTestShaders(void)
{
    char why[256];
    CHECK(checkOne("rhi_test.hlsl", rhi_test_hlsl, why, sizeof(why)), "rhi_test_hlsl: %s", why);
    CHECK(strstr(rhi_test_hlsl, "#ifdef ICO_D3D11") != NULL, "rhi_test_hlsl has the D3D11 switch");
#ifdef ICO_RHI_TEST_SHADER_DIR
    size_t len = 0;
    char *text = readFile(ICO_RHI_TEST_SHADER_DIR "/rhi_test.hlsl", &len);
    if (text) {
        CHECK(len == sizeof(rhi_test_hlsl) - 1 && memcmp(text, rhi_test_hlsl, len) == 0,
              "rhi_test_hlsl.h is stale: run port/rhi/test/shaders/gen_shaders.sh");
        free(text);
    } else {
        printf("rhi_d3d11_shader_plan_test: %s/rhi_test.hlsl not readable; freshness not checked\n",
               ICO_RHI_TEST_SHADER_DIR);
    }
#endif
}

/* ----------------------------------------------- the game's sources
 * The embedded table when the build has one, else port/shaders' files read
 * from the source tree (the same set embed_shaders.py takes: *.hlsl,
 * *.hlsli, *.h, sorted by name). */
#define GAME_SOURCES_MAX 48
static RhiShaderSource s_game[GAME_SOURCES_MAX];
static char *s_gameText[GAME_SOURCES_MAX];
static char s_gameName[GAME_SOURCES_MAX][64];
static unsigned s_gameCount;

static int byName(const void *a, const void *b)
{
    return strcmp(((const RhiShaderSource *)a)->name, ((const RhiShaderSource *)b)->name);
}

static bool hasSuffix(const char *s, const char *suf)
{
    const size_t n = strlen(s), k = strlen(suf);
    return n > k && strcmp(s + n - k, suf) == 0;
}

static void loadGameSources(void)
{
#if defined(ICO_HAVE_SHADER_TABLE) && !defined(ICO_SHADERS_NO_HLSL_SOURCE)
    for (unsigned i = 0; i < g_icoShaderSourceCount && i < GAME_SOURCES_MAX; i++) {
        s_game[i].name = g_icoShaderSources[i].name;
        s_game[i].text = g_icoShaderSources[i].text;
        s_game[i].len = g_icoShaderSources[i].len;
        s_gameCount = i + 1;
    }
    printf("rhi_d3d11_shader_plan_test: %u embedded sources\n", s_gameCount);
#elif defined(ICO_GAME_SHADER_DIR) && !defined(_WIN32)
    DIR *dir = opendir(ICO_GAME_SHADER_DIR);
    struct dirent *e;
    while (dir && (e = readdir(dir)) != NULL && s_gameCount < GAME_SOURCES_MAX) {
        if (!(hasSuffix(e->d_name, ".hlsl") || hasSuffix(e->d_name, ".hlsli") ||
              hasSuffix(e->d_name, ".h")) ||
            strlen(e->d_name) >= sizeof(s_gameName[0])) {
            continue;
        }
        char path[1024];
        size_t len = 0;
        snprintf(path, sizeof(path), "%s/%s", ICO_GAME_SHADER_DIR, e->d_name);
        char *text = readFile(path, &len);
        if (!text) {
            continue;
        }
        const unsigned k = s_gameCount++;
        memcpy(s_gameName[k], e->d_name, strlen(e->d_name) + 1); /* shorter: checked above */
        s_gameText[k] = text;
        s_game[k].name = s_gameName[k];
        s_game[k].text = text;
        s_game[k].len = len;
    }
    if (dir) {
        closedir(dir);
    }
    qsort(s_game, s_gameCount, sizeof(s_game[0]), byName);
    printf("rhi_d3d11_shader_plan_test: %u sources read from %s\n", s_gameCount,
           ICO_GAME_SHADER_DIR);
#endif
    (void)byName;
    (void)hasSuffix;
    (void)s_gameName;
}

static void testGameSources(void)
{
    if (s_gameCount == 0) {
        printf("rhi_d3d11_shader_plan_test: no game sources in this build; skipped that part\n");
        return;
    }
    char why[512];
    CHECK(d3d11_shader_check_sources(s_game, s_gameCount, why, sizeof(why)), "game sources: %s",
          why);
    const RhiShaderSource *reg = d3d11_shader_find_source(s_game, s_gameCount, "reg.hlsli");
    CHECK(reg && strstr(reg->text, "#ifdef ICO_D3D11") && strstr(reg->text, "register(r)"),
          "reg.hlsli drops the space under ICO_D3D11");
    /* every #include "x" of every source resolves among them */
    int includes = 0;
    for (unsigned i = 0; i < s_gameCount; i++) {
        const char *p = s_game[i].text;
        while ((p = strstr(p, "#include \"")) != NULL) {
            p += 10;
            const char *q = strchr(p, '"');
            if (!q || q - p >= 64) {
                break;
            }
            char name[64];
            memcpy(name, p, (size_t)(q - p));
            name[q - p] = '\0';
            CHECK(d3d11_shader_find_source(s_game, s_gameCount, name) != NULL,
                  "%s includes %s, which is not among the sources", s_game[i].name, name);
            includes++;
            p = q;
        }
    }
    CHECK(includes > 0, "the sources include each other");
}

/* ------------------------------------------------------------- the key */
static void testKeyInputs(void)
{
    const RhiShaderSource src[] = {{"a.hlsl", "float4 f() { return 0; }\n", 25},
                                   {"b.hlsli", "// b\n", 5}};
    RhiShaderDesc d;
    memset(&d, 0, sizeof(d));
    d.stage = RHI_STAGE_FRAGMENT;
    d.entryPoint = "main_ps";
    d.hlslFile = "a.hlsl";
    d.defines = "A=1";
    d.sources = src;
    d.sourceCount = 2;
    const uint64_t k = d3d11_shader_cache_key(&d, "10.0.1", "v0.8.6");
    CHECK(k == d3d11_shader_cache_key(&d, "10.0.1", "v0.8.6"), "stable");
    CHECK(k != d3d11_shader_cache_key(&d, "10.0.2", "v0.8.6"), "the compiler's version");
    CHECK(k != d3d11_shader_cache_key(&d, "10.0.1", "v0.8.7"), "the program's version");
    RhiShaderDesc e = d;
    e.stage = RHI_STAGE_VERTEX;
    CHECK(k != d3d11_shader_cache_key(&e, "10.0.1", "v0.8.6"), "the stage");
    e = d;
    e.entryPoint = "main2_ps";
    CHECK(k != d3d11_shader_cache_key(&e, "10.0.1", "v0.8.6"), "the entry");
    e = d;
    e.defines = "A=2";
    CHECK(k != d3d11_shader_cache_key(&e, "10.0.1", "v0.8.6"), "the macros");
    e = d;
    e.hlslFile = "b.hlsli";
    CHECK(k != d3d11_shader_cache_key(&e, "10.0.1", "v0.8.6"), "the file");
    e = d;
    e.sourceCount = 1;
    CHECK(k != d3d11_shader_cache_key(&e, "10.0.1", "v0.8.6"), "the source set");
    const RhiShaderSource src2[] = {{"a.hlsl", "float4 f() { return 1; }\n", 25},
                                    {"b.hlsli", "// b\n", 5}};
    e = d;
    e.sources = src2;
    CHECK(k != d3d11_shader_cache_key(&e, "10.0.1", "v0.8.6"), "one byte of a source");
    /* fields are length-prefixed: moving text between two fields changes
     * the key */
    e = d;
    e.entryPoint = "main_psA=1";
    e.defines = "";
    CHECK(k != d3d11_shader_cache_key(&e, "10.0.1", "v0.8.6"), "field boundaries");
    char path[256];
    d3d11_shader_cache_path(path, sizeof(path), "cache", 0x0123456789abcdefull);
    CHECK(strcmp(path, "cache/0123456789abcdef.dxbc") == 0, "path %s", path);
    d3d11_shader_cache_path(path, sizeof(path), "c", 0x1ull);
    CHECK(strcmp(path, "c/0000000000000001.dxbc") == 0, "path %s", path);
}

#ifdef ICO_HAVE_SHADER_TABLE
/* every entry of the table: its HLSL file among the sources, its macros
 * parsed, its key its own */
static void testShaderTable(void)
{
    if (s_gameCount == 0) {
        printf("rhi_d3d11_shader_plan_test: no sources to resolve the table against; skipped\n");
        return;
    }
    uint64_t *keys = (uint64_t *)calloc(g_icoShaderCount ? g_icoShaderCount : 1, sizeof(uint64_t));
    if (!keys) {
        CHECK(0, "out of memory");
        return;
    }
    int macros = 0;
    for (unsigned i = 0; i < g_icoShaderCount; i++) {
        const IcoShaderBlob *b = &g_icoShaders[i];
        RhiShaderDesc d;
        memset(&d, 0, sizeof(d));
        d.stage = b->stage == ICO_SHADER_STAGE_VERTEX ? RHI_STAGE_VERTEX : RHI_STAGE_FRAGMENT;
        d.entryPoint = b->entry;
        d.debugName = b->name;
        d.hlslFile = b->hlslFile;
        d.defines = b->defines;
        d.sources = s_game;
        d.sourceCount = s_gameCount;
        CHECK(d3d11_shader_find_source(s_game, s_gameCount, b->hlslFile) != NULL,
              "%s: %s is not among the sources", b->name, b->hlslFile);
        D3d11Define defs[D3D11_SHADER_DEFINES_MAX];
        const int n = d3d11_shader_defines(b->defines, defs, D3D11_SHADER_DEFINES_MAX);
        CHECK(n >= 0, "%s: macros \"%s\" do not parse", b->name, b->defines);
        macros += n > 0 ? n : 0;
        keys[i] = d3d11_shader_cache_key(&d, "10.0.0.0", "test");
        CHECK(keys[i] == d3d11_shader_cache_key(&d, "10.0.0.0", "test"), "%s: key not stable",
              b->name);
        for (unsigned j = 0; j < i; j++) {
            const IcoShaderBlob *o = &g_icoShaders[j];
            const bool same = o->stage == b->stage && strcmp(o->entry, b->entry) == 0 &&
                              strcmp(o->hlslFile, b->hlslFile) == 0 &&
                              strcmp(o->defines, b->defines) == 0;
            CHECK(same || keys[j] != keys[i], "%s and %s: the same key %016llx", o->name, b->name,
                  (unsigned long long)keys[i]);
            if (same) {
                printf("rhi_d3d11_shader_plan_test: %s and %s compile the same inputs\n", o->name,
                       b->name);
            }
        }
    }
    free(keys);
    printf("rhi_d3d11_shader_plan_test: %u table entries, %d macros, keys distinct\n",
           g_icoShaderCount, macros);
    CHECK(g_icoShaderCount > 0, "the table has entries");
}
#endif

/* ---------------------------------------------------------- the file */
#define CACHE_DIR "rhi_d3d11_shader_plan_cache"

static bool writeBytes(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    const bool ok = fwrite(data, 1, len, f) == len;
    return fclose(f) == 0 && ok;
}

static void testCacheFile(void)
{
    CHECK(d3d11_shader_cache_mkdir(CACHE_DIR), "mkdir %s", CACHE_DIR);
    CHECK(d3d11_shader_cache_mkdir(CACHE_DIR), "mkdir of an existing folder");
    CHECK(!d3d11_shader_cache_mkdir(""), "mkdir of nothing");
    const uint64_t key = 0xfeedfacecafebeefull;
    char path[256];
    d3d11_shader_cache_path(path, sizeof(path), CACHE_DIR, key);
    unsigned char body[96];
    memset(body, 0, sizeof(body));
    memcpy(body, "DXBC", 4);
    for (size_t i = 4; i < sizeof(body); i++) {
        body[i] = (unsigned char)(i * 7u + 3u);
    }
    remove(path);
    size_t len = 1;
    CHECK(d3d11_shader_cache_read(path, key, &len) == NULL && len == 0, "no file: a miss");
    CHECK(d3d11_shader_cache_write(path, key, body, sizeof(body)), "write %s", path);
    void *got = d3d11_shader_cache_read(path, key, &len);
    CHECK(got && len == sizeof(body) && memcmp(got, body, len) == 0, "round trip");
    free(got);
    CHECK(d3d11_shader_cache_read(path, key ^ 1u, &len) == NULL, "another key: a miss");
    CHECK(!d3d11_shader_cache_write(path, key, body, 3), "a body under 4 bytes is not written");

    size_t fileLen = 0;
    char *file = readFile(path, &fileLen);
    CHECK(file && fileLen == D3D11_SHADER_CACHE_HEADER + sizeof(body), "file size %zu", fileLen);
    if (file && fileLen == D3D11_SHADER_CACHE_HEADER + sizeof(body)) {
        CHECK(memcmp(file, D3D11_SHADER_CACHE_MAGIC, 8) == 0, "magic");
        /* a changed byte of the body: the hash refuses it */
        file[D3D11_SHADER_CACHE_HEADER + 40] ^= 0x10;
        CHECK(writeBytes(path, file, fileLen), "rewrite");
        CHECK(d3d11_shader_cache_read(path, key, &len) == NULL, "a changed byte is refused");
        file[D3D11_SHADER_CACHE_HEADER + 40] ^= 0x10;
        /* a changed magic */
        file[7] = '2';
        CHECK(writeBytes(path, file, fileLen), "rewrite");
        CHECK(d3d11_shader_cache_read(path, key, &len) == NULL, "another magic is refused");
        file[7] = '1';
        /* short and long files */
        CHECK(writeBytes(path, file, fileLen - 1), "rewrite");
        CHECK(d3d11_shader_cache_read(path, key, &len) == NULL, "a short file is refused");
        CHECK(writeBytes(path, file, 20), "rewrite");
        CHECK(d3d11_shader_cache_read(path, key, &len) == NULL, "a short header is refused");
        char *longer = (char *)malloc(fileLen + 1);
        if (longer) {
            memcpy(longer, file, fileLen);
            longer[fileLen] = 0;
            CHECK(writeBytes(path, longer, fileLen + 1), "rewrite");
            CHECK(d3d11_shader_cache_read(path, key, &len) == NULL, "a long file is refused");
            free(longer);
        }
        /* intact again: read back */
        CHECK(writeBytes(path, file, fileLen), "rewrite");
        got = d3d11_shader_cache_read(path, key, &len);
        CHECK(got && len == sizeof(body), "the intact file reads back");
        free(got);
    }
    free(file);
    /* a body that is not a DXBC container, its hash right */
    body[0] = 'X';
    CHECK(d3d11_shader_cache_write(path, key, body, sizeof(body)), "write");
    CHECK(d3d11_shader_cache_read(path, key, &len) == NULL, "a body that is not DXBC is refused");
    remove(path);
#ifdef _WIN32
    _rmdir(CACHE_DIR);
#else
    rmdir(CACHE_DIR);
#endif
}

/* ------------------------------------------------------ the cache folder */
static void testCacheFolder(void)
{
    char dir[256], build[64];
    bool window = true;
    memset(dir, 'x', sizeof(dir));
    memset(build, 'x', sizeof(build));
    CHECK(d3d11_shader_cache_folder("C:\\Users\\a\\pref\\pipelines-v0.8.6-3-gabc.vkcache", dir,
                                    sizeof(dir), build, sizeof(build), &window) &&
              window,
          "the window's path");
    CHECK(strcmp(dir, "C:\\Users\\a\\pref\\pipelines-v0.8.6-3-gabc.d3d11") == 0,
          "beside the Vulkan file: %s", dir);
    CHECK(strcmp(build, "v0.8.6-3-gabc") == 0, "the build: %s", build);
    CHECK(d3d11_shader_cache_folder("/home/a/.local/share/ico/pipelines-unknown.vkcache", dir,
                                    sizeof(dir), build, sizeof(build), &window) &&
              window && strcmp(dir, "/home/a/.local/share/ico/pipelines-unknown.d3d11") == 0 &&
              strcmp(build, "unknown") == 0,
          "a forward-slash path: %s, %s", dir, build);
    CHECK(d3d11_shader_cache_folder("rhi_test.cache", dir, sizeof(dir), build, sizeof(build),
                                    &window) &&
              !window && strcmp(dir, "rhi_test.cache.d3d11") == 0 &&
              strcmp(build, "rhi_test.cache") == 0,
          "a test's path: %s, %s", dir, build);
    CHECK(d3d11_shader_cache_folder("pipelines-.vkcache", dir, sizeof(dir), build, sizeof(build),
                                    &window) &&
              !window && strcmp(dir, "pipelines-.vkcache.d3d11") == 0,
          "no build in the name: not the window's form (%s)", dir);
    CHECK(!d3d11_shader_cache_folder(NULL, dir, sizeof(dir), build, sizeof(build), &window) &&
              !window && dir[0] == '\0' && build[0] == '\0',
          "no path");
    CHECK(!d3d11_shader_cache_folder("", dir, sizeof(dir), build, sizeof(build), NULL),
          "an empty path");
    char tiny[8];
    CHECK(!d3d11_shader_cache_folder("pipelines-abc.vkcache", tiny, sizeof(tiny), build,
                                     sizeof(build), &window) &&
              tiny[0] == '\0' && build[0] == '\0' && !window,
          "a folder past its room");
    CHECK(d3d11_shader_cache_is_other("pipelines-v0.8.5.d3d11", "pipelines-v0.8.6.d3d11"),
          "another build's folder");
    CHECK(d3d11_shader_cache_is_other("pipelines-v0.8.5.D3D11", "pipelines-v0.8.6.d3d11"),
          "the tail in any case");
    CHECK(!d3d11_shader_cache_is_other("pipelines-v0.8.6.d3d11", "pipelines-v0.8.6.d3d11"),
          "ours is kept");
    CHECK(!d3d11_shader_cache_is_other("pipelines-v0.8.5.vkcache", "pipelines-v0.8.6.d3d11") &&
              !d3d11_shader_cache_is_other("saves.d3d11", "pipelines-v0.8.6.d3d11") &&
              !d3d11_shader_cache_is_other(NULL, "pipelines-v0.8.6.d3d11"),
          "nothing else");
}

/* --------------------------------------------------- the log and counters */
static void testCounters(void)
{
    CHECK(!d3d11_shader_log_failure(0), "0 is no failure");
    for (uint32_t n = 1; n <= 8; n++) {
        CHECK(d3d11_shader_log_failure(n), "failure %u logged", (unsigned)n);
    }
    CHECK(!d3d11_shader_log_failure(9) && !d3d11_shader_log_failure(99) &&
              !d3d11_shader_log_failure(101),
          "after the first eight, counted only");
    CHECK(d3d11_shader_log_failure(100) && d3d11_shader_log_failure(200), "every 100th logged");
    D3d11ShaderStats st;
    memset(&st, 0xff, sizeof(st));
    d3d11_shader_stats(NULL, &st, false);
    CHECK(st.hits == 0 && st.compiles == 0 && st.failed == 0, "counters start at zero");
#ifndef _WIN32
    /* off Windows the compile fails with a reason, counted */
    RhiShaderDesc d;
    memset(&d, 0, sizeof(d));
    d.entryPoint = "main_ps";
    void *dxbc = (void *)&d;
    size_t len = 7;
    char err[128];
    err[0] = '\0';
    CHECK(!d3d11_shader_compile(&d, NULL, "test", &dxbc, &len, err, sizeof(err)) && !dxbc &&
              len == 0 && err[0],
          "no compile off Windows");
    d3d11_shader_stats(NULL, &st, true);
    CHECK(st.failed == 1 && st.hits == 0 && st.compiles == 0, "the failure counted");
    CHECK(d3d11_shader_compiler_version() == NULL, "no compiler version off Windows");
#endif
}

int main(void)
{
    testDefines();
    testFindSource();
    testCheckRule();
    testTestShaders();
    loadGameSources();
    testGameSources();
    testKeyInputs();
#ifdef ICO_HAVE_SHADER_TABLE
    testShaderTable();
#else
    printf("rhi_d3d11_shader_plan_test: no shader table in this build (DXC missing); skipped "
           "that part\n");
#endif
    testCacheFile();
    testCacheFolder();
    testCounters();
    for (unsigned i = 0; i < s_gameCount; i++) {
        free(s_gameText[i]);
    }
    if (failures) {
        printf("rhi_d3d11_shader_plan_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("rhi_d3d11_shader_plan_test: all checks passed\n");
    return 0;
}
