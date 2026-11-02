/* rd_cache_name_test.c: the Vulkan pipeline cache file's name from the
 * shader set and the pipeline format (port/render/rd_cache_name.h):
 *   - builds: two builds (their commit texts v0.9.7-43-g16854ce8e1 and
 *     v0.9.8-rc2, which named two files before) with the same modules,
 *     each table in its own memory, name the same file: the name has no
 *     build in it, so a build with the same shaders starts warm;
 *   - one byte of one module changed (its first, its last, a middle one
 *     of the largest), a module one word longer, a module renamed, its
 *     stage changed, two modules swapped, one dropped, one added, or the
 *     format raised: each another name;
 *   - the form: "pipelines-" + 16 lowercase hex digits + ".vkcache", 34
 *     characters, the form vk_pipeline.c vkr_cache_drop_others keeps and
 *     d3d11_shader_cache_folder reads as the window's; the key printed in
 *     it; a buffer of 35 takes it, 34 and NULL are refused with "";
 *   - an empty table and a NULL name still give a name. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_cache_name.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL: ");                                                                      \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

enum { NMOD = 5 };

/* a table as the generated one holds it: name, stage, the SPIR-V */
typedef struct Mod {
    const char *name;
    int stage;
    unsigned char *spirv;
    size_t len;
} Mod;

static const char *const k_names[NMOD] = {"sprite_ui_vs", "sprite_ps", "vu_lit_vs",
                                          "sprite_texa_ps", "font_sheet_ps_dstrov"};
static const int k_stages[NMOD] = {0, 1, 0, 1, 1};
static const size_t k_lens[NMOD] = {1231 * 4, 8268 * 4, 51409 * 4, 10892 * 4, 64};

/* the same bytes every time it is made (the module's words as DXC would
   give them: the SPIR-V magic first, then a fill of its name and place) */
static void makeTable(Mod *t)
{
    for (int i = 0; i < NMOD; i++) {
        t[i].name = k_names[i];
        t[i].stage = k_stages[i];
        t[i].len = k_lens[i];
        t[i].spirv = malloc(t[i].len);
        uint32_t s = 0x07230203u + (uint32_t)i * 2654435761u;
        for (size_t b = 0; b < t[i].len; b++) {
            s = s * 1664525u + 1013904223u;
            t[i].spirv[b] = (unsigned char)(s >> 24);
        }
        t[i].spirv[0] = 0x03;
        t[i].spirv[1] = 0x02;
        t[i].spirv[2] = 0x23;
        t[i].spirv[3] = 0x07;
    }
}

static void freeTable(Mod *t)
{
    for (int i = 0; i < NMOD; i++) {
        free(t[i].spirv);
    }
}

static uint64_t keyOf(const Mod *t, int n, uint32_t format)
{
    RdCacheKey k;
    rd_cache_key_begin(&k, format);
    for (int i = 0; i < n; i++) {
        rd_cache_key_module(&k, t[i].name, t[i].stage, t[i].spirv, t[i].len);
    }
    return rd_cache_key_end(&k);
}

/* the name of t's modules: what rd_pipeline_cache_file gives a build whose
   table is t, whatever its commit text */
static void nameOf(const Mod *t, int n, uint32_t format, char *out, size_t cap)
{
    if (rd_cache_file_name(out, cap, keyOf(t, n, format)) != 0) {
        out[0] = '\0';
    }
}

static void testBuilds(void)
{
    Mod a[NMOD], b[NMOD];
    char na[64], nb[64];
    makeTable(a);
    makeTable(b);
    CHECK(a[2].spirv != b[2].spirv, "the two tables share memory");
    nameOf(a, NMOD, RD_PIPELINE_CACHE_FORMAT, na, sizeof na);
    nameOf(b, NMOD, RD_PIPELINE_CACHE_FORMAT, nb, sizeof nb);
    CHECK(strcmp(na, nb) == 0,
          "builds v0.9.7-43-g16854ce8e1 and v0.9.8-rc2 with the same modules: %s and %s", na, nb);
    CHECK(strcmp(na, "pipelines-v0.9.7-43-g16854ce8e1.vkcache") != 0 && strstr(na, "v0.9") == NULL,
          "the name holds a build: %s", na);
    freeTable(a);
    freeTable(b);
}

static void testChanges(void)
{
    Mod t[NMOD];
    makeTable(t);
    const uint64_t base = keyOf(t, NMOD, RD_PIPELINE_CACHE_FORMAT);
    char nbase[64], n[64];
    (void)rd_cache_file_name(nbase, sizeof nbase, base);

    /* one byte: the first module's first, the largest's middle, the last
       module's last */
    static const struct {
        int mod;
        int where; /* 0 first, 1 middle, 2 last */
    } bytes[] = {{0, 0}, {2, 1}, {4, 2}, {1, 2}};

    for (size_t i = 0; i < sizeof bytes / sizeof bytes[0]; i++) {
        Mod *m = &t[bytes[i].mod];
        const size_t at = bytes[i].where == 0 ? 0 : bytes[i].where == 1 ? m->len / 2 : m->len - 1;
        m->spirv[at] ^= 0x01;
        (void)rd_cache_file_name(n, sizeof n, keyOf(t, NMOD, RD_PIPELINE_CACHE_FORMAT));
        CHECK(strcmp(n, nbase) != 0, "byte %zu of %s changed: the same name %s", at, m->name, n);
        m->spirv[at] ^= 0x01;
    }
    CHECK(keyOf(t, NMOD, RD_PIPELINE_CACHE_FORMAT) == base, "the bytes put back: another key");

    /* one word longer (the bytes before it the same) */
    {
        Mod l[NMOD];
        memcpy(l, t, sizeof l);
        unsigned char *longer = malloc(t[3].len + 4);
        memcpy(longer, t[3].spirv, t[3].len);
        memset(longer + t[3].len, 0, 4);
        l[3].spirv = longer;
        l[3].len = t[3].len + 4;
        CHECK(keyOf(l, NMOD, RD_PIPELINE_CACHE_FORMAT) != base, "a module a word longer");
        l[3].len = t[3].len - 4;
        CHECK(keyOf(l, NMOD, RD_PIPELINE_CACHE_FORMAT) != base, "a module a word shorter");
        free(longer);
    }
    /* renamed, another stage */
    {
        Mod r[NMOD];
        memcpy(r, t, sizeof r);
        r[1].name = "sprite_ps_nodual";
        CHECK(keyOf(r, NMOD, RD_PIPELINE_CACHE_FORMAT) != base, "a module renamed");
        r[1].name = NULL;
        CHECK(keyOf(r, NMOD, RD_PIPELINE_CACHE_FORMAT) != base, "a module's name NULL");
        memcpy(r, t, sizeof r);
        r[1].stage = 0;
        CHECK(keyOf(r, NMOD, RD_PIPELINE_CACHE_FORMAT) != base, "a module's stage changed");
    }
    /* the name's end is part of the record: "ab" + "c" is not "a" + "bc" */
    {
        unsigned char one = 0x5a;
        Mod x[2] = {{"ab", 0, &one, 1}, {"c", 0, &one, 1}};
        Mod y[2] = {{"a", 0, &one, 1}, {"bc", 0, &one, 1}};
        CHECK(keyOf(x, 2, 1) != keyOf(y, 2, 1), "names joined differently: the same key");
    }
    /* two swapped, one dropped, one added */
    {
        Mod s[NMOD + 1];
        memcpy(s, t, sizeof(Mod) * NMOD);
        const Mod tmp = s[0];
        s[0] = s[1];
        s[1] = tmp;
        CHECK(keyOf(s, NMOD, RD_PIPELINE_CACHE_FORMAT) != base, "two modules swapped");
        CHECK(keyOf(t, NMOD - 1, RD_PIPELINE_CACHE_FORMAT) != base, "the last module dropped");
        memcpy(s, t, sizeof(Mod) * NMOD);
        s[NMOD] = t[0];
        CHECK(keyOf(s, NMOD + 1, RD_PIPELINE_CACHE_FORMAT) != base, "a module added");
    }
    /* the format */
    CHECK(keyOf(t, NMOD, RD_PIPELINE_CACHE_FORMAT + 1u) != base, "the format raised");
    freeTable(t);
}

static void testForm(void)
{
    char out[64];
    CHECK(rd_cache_file_name(out, sizeof out, 0x0123456789abcdefull) == 0, "a 64-byte buffer");
    CHECK(strcmp(out, "pipelines-0123456789abcdef.vkcache") == 0, "the form: %s", out);
    CHECK(strlen(out) == 34, "34 characters: %zu", strlen(out));
    CHECK(rd_cache_file_name(out, sizeof out, 0) == 0 &&
              strcmp(out, "pipelines-0000000000000000.vkcache") == 0,
          "key 0: %s", out);
    CHECK(rd_cache_file_name(out, sizeof out, ~0ull) == 0 &&
              strcmp(out, "pipelines-ffffffffffffffff.vkcache") == 0,
          "key ~0: %s", out);
    /* the drop's and the D3D11 folder's form: "pipelines" first,
       ".vkcache" last, more than 18 characters */
    CHECK(strncmp(out, "pipelines-", 10) == 0 && strcmp(out + strlen(out) - 8, ".vkcache") == 0 &&
              strlen(out) > 18,
          "the window's form: %s", out);
    {
        char exact[35], shorter[34];
        CHECK(rd_cache_file_name(exact, sizeof exact, 42) == 0 &&
                  strcmp(exact, "pipelines-000000000000002a.vkcache") == 0,
              "35 bytes: %s", exact);
        memset(shorter, 'x', sizeof shorter);
        CHECK(rd_cache_file_name(shorter, sizeof shorter, 42) == -1 && shorter[0] == '\0',
              "34 bytes taken");
        CHECK(rd_cache_file_name(NULL, 64, 42) == -1, "NULL taken");
    }
    /* an empty table */
    {
        RdCacheKey k;
        rd_cache_key_begin(&k, RD_PIPELINE_CACHE_FORMAT);
        const uint64_t empty = rd_cache_key_end(&k);
        CHECK(rd_cache_file_name(out, sizeof out, empty) == 0 && strlen(out) == 34,
              "an empty table: %s", out);
        rd_cache_key_module(&k, "", 0, NULL, 0);
        CHECK(rd_cache_key_end(&k) != empty, "one empty module counts");
    }
}

int main(void)
{
    testBuilds();
    testChanges();
    testForm();
    printf("rd_cache_name_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
