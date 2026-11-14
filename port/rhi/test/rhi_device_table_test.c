/* rhi_device_table_test.c: the device table's own rules (port/rhi/
 * rhi_device_table.h): unique row ids, every setting's provenance, a row
 * sets a choice once, rows read only derived facts resolved before what
 * they set, every override link names a row that sets a choice it sets too,
 * the start-up lines and the summary's clause of five devices word for word
 * (the moto g24 power's Mali-G52, issue 123's Adreno 740 on Qualcomm's
 * driver, the owner's PowerVR DXT-48-1536, an Intel HD 530 on its final
 * Windows driver, an RTX 3090 with no row), the clause's cut, the noted
 * clause, a row's value by id.
 *
 *   rhi_device_table_test               the checks
 *   rhi_device_table_test --docs        the row list docs/port/DEVICE_TABLE.md
 *                                       holds between its markers, to stdout
 *   rhi_device_table_test --check-docs F  fails when F's list differs (the
 *                                       ctest rhi_device_table_docs) */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rhi_device_table.h"

static int s_fail;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            s_fail++;                                                                              \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

#define API(a, b, c) (((uint32_t)(a) << 22) | ((uint32_t)(b) << 12) | (uint32_t)(c))

static bool isFact(const RhiDeviceMatch *m, int *last)
{
    *last = -1;
    if (m->tiler != RHI_DT_ANY) {
        *last = RHI_DC_TILER;
    }
    if (m->tilePhone != RHI_DT_ANY) {
        *last = RHI_DC_TILE_PHONE;
    }
    if (m->profileClasses != 0u) {
        *last = RHI_DC_PROFILE_CLASS;
    }
    return *last >= 0;
}

static void testStructure(void)
{
    size_t n;
    const RhiDeviceRow *rows = rhi_device_table_rows(&n);
    CHECK(n > 0 && n <= RHI_DT_MAX_ROWS, "%zu rows (at most %d)", n, RHI_DT_MAX_ROWS);
    for (int c = 0; c < RHI_DC_COUNT; c++) {
        CHECK(strcmp(rhi_device_choice_name((RhiDeviceChoice)c), "?") != 0, "choice %d unnamed", c);
        CHECK(rhi_device_choice_default_words((RhiDeviceChoice)c)[0] != '\0',
              "choice %d's default unworded", c);
    }
    for (size_t r = 0; r < n; r++) {
        const RhiDeviceRow *row = &rows[r];
        CHECK(row->id && row->id[0], "row %zu without an id", r);
        CHECK(row->matchWords && row->matchWords[0], "row %s without match words", row->id);
        CHECK(row->settingCount > 0, "row %s sets nothing", row->id);
        CHECK(rhi_device_table_find(row->id) == (int)r, "row id %s not unique", row->id);
        CHECK(row->match.vendorCount <= RHI_DT_MAX_VENDORS, "row %s: vendors", row->id);
        int fact;
        const bool readsFact = isFact(&row->match, &fact);
        for (uint8_t s = 0; s < row->settingCount; s++) {
            const RhiDeviceSetting *st = &row->settings[s];
            CHECK(st->choice < RHI_DC_COUNT, "row %s: choice %u", row->id, (unsigned)st->choice);
            CHECK(st->what && st->what[0], "row %s: a setting without words", row->id);
            CHECK(st->issue != NULL, "row %s %s: issue NULL", row->id,
                  rhi_device_choice_name((RhiDeviceChoice)st->choice));
            CHECK(st->evidence && st->evidence[0], "row %s %s: no evidence", row->id,
                  rhi_device_choice_name((RhiDeviceChoice)st->choice));
            CHECK(st->since && strncmp(st->since, "v0.", 3) == 0, "row %s %s: since '%s'", row->id,
                  rhi_device_choice_name((RhiDeviceChoice)st->choice),
                  st->since ? st->since : "(null)");
            CHECK(st->revisit && st->revisit[0], "row %s %s: no revisit condition", row->id,
                  rhi_device_choice_name((RhiDeviceChoice)st->choice));
            CHECK(!readsFact || (int)st->choice > fact,
                  "row %s reads a fact (%s) not resolved before %s", row->id,
                  rhi_device_choice_name((RhiDeviceChoice)fact),
                  rhi_device_choice_name((RhiDeviceChoice)st->choice));
            /* PROFILE_CLASS and TILE_CLASS are facts too: a row reading the
               class sets nothing at or before TILE_CLASS */
            CHECK(row->match.profileClasses == 0u || st->choice > RHI_DC_TILE_CLASS,
                  "row %s reads the class and sets %s", row->id,
                  rhi_device_choice_name((RhiDeviceChoice)st->choice));
            for (uint8_t t = 0; t < s; t++) {
                CHECK(row->settings[t].choice != st->choice, "row %s sets %s twice", row->id,
                      rhi_device_choice_name((RhiDeviceChoice)st->choice));
            }
        }
        /* the links: each names a row that sets a choice this one sets */
        const char *o = row->overrides;
        while (o && *o) {
            while (*o == ' ') {
                o++;
            }
            const size_t k = strcspn(o, " ");
            if (k == 0) {
                break;
            }
            char id[64];
            snprintf(id, sizeof id, "%.*s", (int)k, o);
            const int t = rhi_device_table_find(id);
            CHECK(t >= 0 && t != (int)r, "row %s overrides '%s', no other row", row->id, id);
            bool shared = false;
            for (uint8_t a = 0; t >= 0 && a < row->settingCount; a++) {
                for (uint8_t b = 0; b < rows[t].settingCount; b++) {
                    shared = shared || row->settings[a].choice == rows[t].settings[b].choice;
                }
            }
            CHECK(shared, "row %s overrides %s but they share no choice", row->id, id);
            o += k;
        }
    }
    /* the subpass class's vendor fake resolves its setters on another
       vendor with the real facts: they read none */
    for (size_t r = 0; r < n; r++) {
        for (uint8_t s = 0; s < rows[r].settingCount; s++) {
            int fact;
            if (rows[r].settings[s].choice == RHI_DC_SUBPASS_CLASS) {
                CHECK(!isFact(&rows[r].match, &fact), "row %s sets SUBPASS_CLASS on a fact",
                      rows[r].id);
            }
        }
    }
    CHECK(rhi_device_table_row_value("mali-g52", RHI_DC_COMPACTION_DEFAULT, 7) ==
              RD_VU_COMPACT_ARM_DEFAULT,
          "mali-g52's compaction default");
    CHECK(rhi_device_table_row_value("powervr-dxt-48-1536", RHI_DC_COMPACTION_DEFAULT, 7) ==
              RD_VU_COMPACT_DEFAULT,
          "the DXT's compaction default");
    CHECK(rhi_device_table_row_value("mali-g52", RHI_DC_TILER, 7) == 7, "a choice a row lacks");
    CHECK(rhi_device_table_row_value("no-such-row", RHI_DC_TILER, 5) == 5, "no row");
}

typedef struct Dev {
    const char *tag;
    RhiDeviceId id;
    const char *header;
    const char *summary;
} Dev;

static void testDevices(void)
{
    static const Dev devs[] = {
        {"Mali-G52",
         {RHI_DT_VULKAN, RHI_DT_ANDROID, 0, 0, 0x13B5u, 0x74021000u, 9, API(1, 1, 177),
          "Mali-G52 MC2",
          "device 13b5:74021000; driver Mali-G52 MC2 "
          "v1.r32p1-01eac0.2a893c04ca0026c2e6802dbe7d7af5c5, version 32.1.0",
          "32.1.0"},
         "device table: Mali-G52 MC2 (device 13b5:74021000; driver ID 9, version 32.1.0; Vulkan "
         "1.1.177; Android): 8 of the table's 27 rows: tile-vendors, android, tiler, tile-phone, "
         "arm, mali, mali-g52, tile-classes; overridden here: other-tiler",
         "device table rows tile-vendors, android, tiler, tile-phone, arm, mali, mali-g52, "
         "tile-classes"},
        {"Adreno 740",
         {RHI_DT_VULKAN, RHI_DT_ANDROID, 0, 0, 0x5143u, 0x43050A01u, 8, API(1, 3, 128),
          "Adreno (TM) 740",
          "device 5143:43050a01; driver Qualcomm Technologies Inc. Adreno Vulkan Driver Driver "
          "Build: bad34dcf83, I1064ae0a1f, 1764326394 Date: 11/28/25 Compiler Version: "
          "E031.41.03.64 Driver Branch:, version 512.676.1",
          "512.676.1"},
         "device table: Adreno (TM) 740 (device 5143:43050a01; driver ID 8, version 512.676.1; "
         "Vulkan 1.3.128; Android): 13 of the table's 27 rows: tile-vendors, android, tiler, "
         "tile-phone, qualcomm-android, qualcomm, qualcomm-text, qualcomm-text-android, "
         "qualcomm-vendor, adreno, adreno-740-qualcomm, stock-qualcomm-android, tile-classes; "
         "overridden here: other-tiler",
         "device table rows tile-vendors, android, tiler, tile-phone, qualcomm-android, "
         "qualcomm, qualcomm-text, qualcomm-text-android, qualcomm-vendor, adreno, "
         "adreno-740-qualcomm, stock-qualcomm-android, tile-classes"},
        {"PowerVR DXT",
         {RHI_DT_VULKAN, RHI_DT_ANDROID, 0, 0, 0x1010u, 0x71061212u, 7, API(1, 4, 344),
          "PowerVR D-Series DXT-48-1536 MC1",
          "device 1010:71061212; driver PowerVR D-Series Vulkan Driver 26.1@7036548, version "
          "1.693.3716",
          "1.693.3716"},
         "device table: PowerVR D-Series DXT-48-1536 MC1 (device 1010:71061212; driver ID 7, "
         "version 1.693.3716; Vulkan 1.4.344; Android): 7 of the table's 27 rows: tile-vendors, "
         "android, tiler, tile-phone, powervr, powervr-dxt-48-1536, tile-classes; overridden "
         "here: other-tiler",
         "device table rows tile-vendors, android, tiler, tile-phone, powervr, "
         "powervr-dxt-48-1536, tile-classes"},
        {"Intel HD 530",
         {RHI_DT_VULKAN, RHI_DT_WINDOWS, 0, 0, 0x8086u, 0x1912u, 5, API(1, 3, 215),
          "Intel(R) HD Graphics 530",
          "device 8086:1912; driver Intel Corporation Intel driver, version 101.2125", "101.2125"},
         "device table: Intel(R) HD Graphics 530 (device 8086:1912; driver ID 5, version "
         "101.2125; Vulkan 1.3.215; Windows): 1 of the table's 27 rows: intel-windows-1.3.215",
         "device table rows intel-windows-1.3.215"},
        {"RTX 3090",
         {RHI_DT_VULKAN, RHI_DT_WINDOWS, 0, 0, 0x10DEu, 0x2204u, 4, API(1, 4, 341),
          "NVIDIA GeForce RTX 3090", "device 10de:2204; driver NVIDIA 610.88, version 610.88",
          "610.88"},
         "device table: NVIDIA GeForce RTX 3090 (device 10de:2204; driver ID 4, version 610.88; "
         "Vulkan 1.4.341; Windows): none of the table's 27 rows: every choice as capability and "
         "self-tests decide",
         "device table rows none"},
    };
    char line[2048];
    for (size_t i = 0; i < sizeof devs / sizeof devs[0]; i++) {
        RhiDeviceChoices c;
        rhi_device_table_eval(&devs[i].id, NULL, &c);
        CHECK(c.conflictChoice < 0, "%s: a conflict", devs[i].tag);
        const size_t k = rhi_device_table_header(line, sizeof line, &devs[i].id, &c);
        CHECK(k == strlen(line), "%s: header length", devs[i].tag);
        CHECK(strcmp(line, devs[i].header) == 0, "%s header:\n  got  %s\n  want %s", devs[i].tag,
              line, devs[i].header);
        rhi_device_table_summary(line, sizeof line, &c);
        CHECK(strcmp(line, devs[i].summary) == 0, "%s summary:\n  got  %s\n  want %s", devs[i].tag,
              line, devs[i].summary);
        CHECK(strlen(line) < RHI_DT_SUMMARY_MAX, "%s summary over its cap", devs[i].tag);
        size_t rowCount;
        rhi_device_table_rows(&rowCount);
        for (size_t r = 0; r < rowCount; r++) {
            const size_t m = rhi_device_table_row_line(line, sizeof line, &c, (int)r);
            CHECK(m == strlen(line) && m + 1u < sizeof line, "%s row %zu's line cut", devs[i].tag,
                  r);
            CHECK(strncmp(line, "device table: ", 14) == 0, "%s row %zu's line", devs[i].tag, r);
        }
        if (i == 0) {
            /* the first new row's line (MALI179's reason), word for word */
            rhi_device_table_row_line(line, sizeof line, &c, rhi_device_table_find("mali-g52"));
#if RD_VU_COMPACT_ARM_DEFAULT
            const char *want = NULL;
#else
            const char *want =
                "device table: mali-g52 (vendor 0x13B5, not MoltenVK, a device name with "
                "\"Mali-G52\"): the index compaction's Mali-G52 class (issue 179, v0.11.6; "
                "revisit: never: a class); the index compaction off by default (issue 179, "
                "v0.11.7; revisit: the cause found, or a G52 log of complete pictures with it on)";
#endif
            CHECK(want == NULL || strcmp(line, want) == 0, "mali-g52's line:\n  got  %s\n  want %s",
                  line, want ? want : "");
            /* an overridden setting says so: other-tiler on the Mali */
            rhi_device_table_row_line(line, sizeof line, &c, rhi_device_table_find("other-tiler"));
            CHECK(strstr(line, "; overridden by mali; revisit:") != NULL, "other-tiler's line: %s",
                  line);
            CHECK(c.value[RHI_DC_COMPACTION_CLASS] == RHI_DT_CMP_G52 &&
                      c.value[RHI_DC_COMPACTION_DEFAULT] == RD_VU_COMPACT_ARM_DEFAULT,
                  "the Mali-G52's compaction");
        }
        if (i == 1) {
            const char *r = rhi_device_choice_reason(&c, RHI_DC_VU_PREPASS_OFF);
            CHECK(r && strcmp(r, "Adreno 740 on Qualcomm's driver, issue 123") == 0,
                  "the 740's pre-pass reason");
        }
    }
    /* a fake replacing a row's value says so in that row's line */
    {
        RhiDeviceFakes fk;
        memset(&fk, 0, sizeof fk);
        fk.compactArm = 1;
        RhiDeviceChoices c;
        rhi_device_table_eval(&devs[2].id, &fk, &c);
        rhi_device_table_row_line(line, sizeof line, &c,
                                  rhi_device_table_find("powervr-dxt-48-1536"));
        CHECK(strstr(line, "; replaced by ICO_RD_VU_COMPACT_ARM, a test; revisit:") != NULL,
              "the DXT row under ICO_RD_VU_COMPACT_ARM=1: %s", line);
    }
    /* the clause's cut: every row in force */
    {
        RhiDeviceChoices c;
        memset(&c, 0, sizeof c);
        size_t rowCount;
        rhi_device_table_rows(&rowCount);
        for (size_t r = 0; r < rowCount; r++) {
            c.matched[r] = c.applied[r] = 1;
        }
        c.rowsMatched = c.rowsApplied = (uint8_t)rowCount;
        rhi_device_table_summary(line, sizeof line, &c);
        char want[96];
        snprintf(want, sizeof want, "device table rows: %zu (the start lines name them)", rowCount);
        CHECK(strcmp(line, want) == 0, "the clause past its cap: %s", line);
        char small[16];
        const size_t k = rhi_device_table_summary(small, sizeof small, &c);
        CHECK(k == sizeof small - 1u && strlen(small) == k, "the clause cut at the buffer");
    }
    /* the noted clause */
    {
        rhi_device_table_note(NULL);
        CHECK(rhi_device_table_noted() == NULL, "no note before a device");
        RhiDeviceChoices c;
        rhi_device_table_eval(&devs[4].id, NULL, &c);
        rhi_device_table_note(&c);
        CHECK(rhi_device_table_noted() && strcmp(rhi_device_table_noted(), devs[4].summary) == 0,
              "the noted clause");
    }
}

/* ---------------------------------------------------------------- the docs */

#define DOC_BEGIN "<!-- device-table:begin -->"
#define DOC_END "<!-- device-table:end -->"

typedef struct Buf {
    char *p;
    size_t n, cap;
} Buf;

static void put(Buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void put(Buf *b, const char *fmt, ...)
{
    for (;;) {
        va_list ap;
        va_start(ap, fmt);
        const int k = vsnprintf(b->p ? b->p + b->n : NULL, b->p ? b->cap - b->n : 0, fmt, ap);
        va_end(ap);
        if (k < 0) {
            return;
        }
        if (b->p && b->n + (size_t)k < b->cap) {
            b->n += (size_t)k;
            return;
        }
        const size_t want = (b->cap ? b->cap * 2 : 65536) + (size_t)k;
        char *q = realloc(b->p, want);
        if (q == NULL) {
            return;
        }
        b->p = q;
        b->cap = want;
    }
}

static const char *bitsWords(uint32_t bits, int kind, char *out, size_t cap)
{
    static const char *const backends[] = {"Vulkan", "D3D12", "Metal", "OpenGL", "D3D11"};
    static const char *const oses[] = {"Linux", "Windows", "macOS", "Android", "iOS"};
    size_t at = 0;
    out[0] = '\0';
    for (int i = 0; i < 32; i++) {
        if ((bits & (1u << i)) == 0u) {
            continue;
        }
        const char *w = kind == 0   ? (i < 5 ? backends[i] : "?")
                        : kind == 1 ? (i < 5 ? oses[i] : "?")
                                    : rhi_device_choice_value_name(RHI_DC_PROFILE_CLASS, i);
        const int k = snprintf(out + at, cap - at, "%s%s", at ? ", " : "", w);
        if (k < 0 || (size_t)k >= cap - at) {
            break;
        }
        at += (size_t)k;
    }
    return out;
}

static const char *triWords(uint8_t t)
{
    return t == RHI_DT_YES ? "yes" : "no";
}

static void docs(Buf *b)
{
    static const char *const drivers[] = {"",
                                          "Qualcomm's own driver by its id (vendor 0x5143, "
                                          "driver id 8 or 0)",
                                          "Qualcomm's own driver by its text (vendor 0x5143, "
                                          "driver id not 18, no Mesa or Turnip in the text)",
                                          "a Mesa driver (driver id 18, or Mesa or Turnip in the "
                                          "text)",
                                          "Turnip (driver id 18, or Turnip in the text; no "
                                          "vendor test)",
                                          "Intel's Windows driver (driver id 5, or id 0 on a "
                                          "Windows build)"};
    size_t n;
    const RhiDeviceRow *rows = rhi_device_table_rows(&n);
    char w[256];
    put(b, "%s\n", DOC_BEGIN);
    put(b, "<!-- generated by rhi_device_table_test --docs; the ctest rhi_device_table_docs "
           "fails on any difference -->\n\n");
    put(b, "### The choices\n\n");
    put(b, "| Choice | Default (no row) |\n| --- | --- |\n");
    for (int c = 0; c < RHI_DC_COUNT; c++) {
        put(b, "| `%s` | %s |\n", rhi_device_choice_name((RhiDeviceChoice)c),
            rhi_device_choice_default_words((RhiDeviceChoice)c));
    }
    put(b, "\n### The rows (%zu, in table order)\n", n);
    for (size_t r = 0; r < n; r++) {
        const RhiDeviceRow *row = &rows[r];
        const RhiDeviceMatch *m = &row->match;
        put(b, "\n#### `%s`\n\n", row->id);
        put(b, "Matches %s:", row->matchWords);
        if (m->backends) {
            put(b, " backend %s;", bitsWords(m->backends, 0, w, sizeof w));
        }
        if (m->oses) {
            put(b, " OS %s;", bitsWords(m->oses, 1, w, sizeof w));
        }
        if (m->vendorCount) {
            put(b, " vendor");
            for (uint8_t i = 0; i < m->vendorCount; i++) {
                put(b, "%s 0x%04X", i ? "," : "", (unsigned)m->vendors[i]);
            }
            put(b, ";");
        }
        if (m->deviceMask) {
            put(b, " device 0x%08X under mask 0x%08X;", (unsigned)m->device,
                (unsigned)m->deviceMask);
        }
        if (m->driver && m->driver < sizeof drivers / sizeof drivers[0]) {
            put(b, " driver: %s;", drivers[m->driver]);
        }
        if (m->apiMax) {
            put(b, " Vulkan %u.%u.%u or older;", (unsigned)(m->apiMax >> 22) & 0x7Fu,
                (unsigned)(m->apiMax >> 12) & 0x3FFu, (unsigned)m->apiMax & 0xFFFu);
        }
        if (m->nameHas) {
            put(b, " device name holds \"%s\";", m->nameHas);
        }
        if (m->textHas) {
            put(b, " driver text holds \"%s\" (any case);", m->textHas);
        }
        if (m->moltenvk) {
            put(b, " MoltenVK %s;", triWords(m->moltenvk));
        }
        if (m->customDriver) {
            put(b, " a driver package %s;", triWords(m->customDriver));
        }
        if (m->tiler) {
            put(b, " a tile-based GPU (TILER) %s;", triWords(m->tiler));
        }
        if (m->tilePhone) {
            put(b, " a tile-based phone (TILE_PHONE) %s;", triWords(m->tilePhone));
        }
        if (m->profileClasses) {
            put(b, " profile class %s;", bitsWords(m->profileClasses, 2, w, sizeof w));
        }
        if (row->overrides && row->overrides[0]) {
            put(b, " overrides %s;", row->overrides);
        }
        /* the line's last ';' becomes '.' */
        if (b->n > 0 && b->p[b->n - 1] == ';') {
            b->p[b->n - 1] = '.';
        } else {
            put(b, ".");
        }
        put(b, "\n\n| Choice | Value | What | Issue | Since | Evidence | Revisit |\n");
        put(b, "| --- | --- | --- | --- | --- | --- | --- |\n");
        for (uint8_t s = 0; s < row->settingCount; s++) {
            const RhiDeviceSetting *st = &row->settings[s];
            put(b, "| `%s` | %s | %s%s%s%s | %s | %s | %s | %s |\n",
                rhi_device_choice_name((RhiDeviceChoice)st->choice),
                rhi_device_choice_value_name((RhiDeviceChoice)st->choice, st->value), st->what,
                st->reason ? " (\"" : "", st->reason ? st->reason : "", st->reason ? "\")" : "",
                st->issue[0] ? st->issue : "-", st->since, st->evidence, st->revisit);
        }
    }
    put(b, "\n%s\n", DOC_END);
}

static int checkDocs(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        printf("FAIL %s: cannot open\n", path);
        return 1;
    }
    Buf file = {NULL, 0, 0};
    char chunk[4096];
    size_t k;
    while ((k = fread(chunk, 1, sizeof chunk, f)) > 0) {
        put(&file, "%.*s", (int)k, chunk);
    }
    fclose(f);
    Buf want = {NULL, 0, 0};
    docs(&want);
    const char *a = file.p ? strstr(file.p, DOC_BEGIN) : NULL;
    const char *z = a ? strstr(a, DOC_END) : NULL;
    int rc = 0;
    if (a == NULL || z == NULL) {
        printf("FAIL %s: no %s ... %s block\n", path, DOC_BEGIN, DOC_END);
        rc = 1;
    } else {
        const size_t have = (size_t)(z - a) + strlen(DOC_END) + 1u; /* with the newline */
        if (have > (size_t)(file.p + file.n - a) || have != want.n ||
            memcmp(a, want.p, want.n) != 0) {
            printf("FAIL %s: the row list differs from the table; regenerate it with "
                   "rhi_device_table_test --docs (between the markers)\n",
                   path);
            rc = 1;
        } else {
            printf("rhi_device_table_docs: %s matches the table\n", path);
        }
    }
    free(file.p);
    free(want.p);
    return rc;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--docs") == 0) {
        Buf b = {NULL, 0, 0};
        docs(&b);
        fwrite(b.p, 1, b.n, stdout);
        free(b.p);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "--check-docs") == 0) {
        return checkDocs(argv[2]);
    }
    testStructure();
    testDevices();
    if (s_fail) {
        printf("rhi_device_table: %d failures\n", s_fail);
        return 1;
    }
    printf("rhi_device_table: ok\n");
    return 0;
}
