/* rd_fixture_tool: the committed test frames (port/render/test/fixtures, .rdfix files;
 * rd_fixture.h, docs/port/TESTING_FIXTURES.md).  A host tool: no device, no
 * renderer context.
 *
 *   rd_fixture_tool expand FIXTURE OUT.rddump
 *       the frame dump the fixture stands for; refused (exit 1) when its
 *       SHA-256 is not the one the fixture pins
 *   rd_fixture_tool check FIXTURE...
 *       each fixture holds no art (rd_fixture_check) and expands to its pin
 *   rd_fixture_tool check-dir DIR MANIFEST
 *       check of every .rdfix file in DIR, and the manifest names exactly them
 *   rd_fixture_tool make DUMP OUT.rdfix [--rules FILE] [--source NAME]
 *                        [--expect PROP]... [--selfcheck]
 *       a fixture from a frame dump (.rddump, or an F12 / loss .zip: its
 *       first frame).  Developer side only: the dump is read, never
 *       committed.  --selfcheck runs the window check against DUMP and keeps
 *       its line in the fixture
 *   rd_fixture_tool remake MANIFEST SOURCES OUTDIR [NAME...]
 *       every fixture the manifest names (or the NAMEs) made again from
 *       SOURCES/<its source>, with its expectations and the window check
 *   rd_fixture_tool selfcheck FIXTURE DUMP
 *   rd_fixture_tool selfcheck-dir DIR MANIFEST [SOURCES]
 *       the window check of each manifest fixture against its source under
 *       SOURCES (default: the environment's ICO_FIXTURE_SOURCES); exit 77
 *       (skipped) when there is no sources folder
 *   rd_fixture_tool props FIXTURE           the declared properties, a line each
 *   rd_fixture_tool set-props FIXTURE PROP... replace them (the expansion is unchanged)
 *   rd_fixture_tool info FIXTURE            notes, sizes, the check's counts
 *   rd_fixture_tool rules | patterns        the default texture rules; the patterns
 *
 * A property is QUANTITY OP VALUE (OP one of >= <= > < = !=; VALUE a number,
 * or a word with = and !=), QUANTITY a name of the test helper's table
 * (port/render/test/rd_fixture_helpers.cmake).  Exit 0 done, 1 refused or
 * failed, 2 usage, 77 skipped. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dump_zip.h"
#include "rd_fixture.h"

#define MANIFEST_LINES 256

static int usage(void)
{
    fprintf(stderr, "usage: rd_fixture_tool expand|check|check-dir|make|remake|selfcheck|"
                    "selfcheck-dir|props|set-props|info|rules|patterns ... (see the source's "
                    "head or docs/port/TESTING_FIXTURES.md)\n");
    return 2;
}

static uint8_t *readAll(const char *path, size_t *n)
{
    *n = 0;
    const size_t len = strlen(path);
    if (len > 4 && (strcmp(path + len - 4, ".zip") == 0 || strcmp(path + len - 4, ".ZIP") == 0)) {
        return ico_dump_zip_extract(path, n, NULL, 0);
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    RdFxBuf b = {0};
    uint8_t blk[65536];
    size_t r;
    while ((r = fread(blk, 1, sizeof(blk), fp)) > 0) {
        rd_fx_put(&b, blk, r);
    }
    fclose(fp);
    if (b.bad) {
        rd_fx_free(&b);
        return NULL;
    }
    *n = b.n;
    return b.p ? b.p : calloc(1, 1);
}

static bool writeAll(const char *path, const uint8_t *p, size_t n)
{
    FILE *fp = fopen(path, "wb");
    bool ok = fp && fwrite(p, 1, n, fp) == n;
    return fp && fclose(fp) == 0 && ok;
}

static const char *baseName(const char *p)
{
    const char *a = strrchr(p, '/'), *b = strrchr(p, '\\');
    a = a && b ? (a > b ? a : b) : a ? a : b;
    return a ? a + 1 : p;
}

/* a property's shape: a quantity name, an operator, a value */
static bool propOk(const char *p)
{
    const char *o = p;
    while ((*o >= 'a' && *o <= 'z') || (*o >= '0' && *o <= '9') || *o == '_') {
        o++;
    }
    if (o == p) {
        return false;
    }
    const char *v = NULL;
    if (strncmp(o, ">=", 2) == 0 || strncmp(o, "<=", 2) == 0 || strncmp(o, "!=", 2) == 0) {
        v = o + 2;
    } else if (*o == '>' || *o == '<' || *o == '=') {
        v = o + 1;
    }
    return v && *v && strlen(p) < 120 && strchr(v, ' ') == NULL;
}

static int cmdExpand(const char *fixture, const char *out)
{
    RdFixture fx;
    char why[512] = "";
    uint8_t *x = NULL;
    size_t n = 0;
    if (!rd_fixture_read(fixture, &fx, why, sizeof(why)) ||
        !rd_fixture_expand(&fx, true, &x, &n, why, sizeof(why))) {
        fprintf(stderr, "%s: %s\n", fixture, why);
        rd_fixture_free(&fx);
        return 1;
    }
    rd_fixture_free(&fx);
    const bool ok = writeAll(out, x, n);
    free(x);
    if (!ok) {
        fprintf(stderr, "%s: cannot write %s\n", fixture, out);
        return 1;
    }
    char hex[65];
    rd_fx_sha_hex(fx.sha, hex);
    printf("%s: expanded to %s (%zu bytes, SHA-256 %s)\n", baseName(fixture), out, n, hex);
    return 0;
}

static bool checkOne(const char *path)
{
    RdFixture fx;
    char why[512] = "", stats[512] = "";
    uint8_t *x = NULL;
    size_t n = 0;
    bool ok = rd_fixture_read(path, &fx, why, sizeof(why)) &&
              rd_fixture_check(&fx, why, sizeof(why), stats, sizeof(stats)) &&
              rd_fixture_expand(&fx, true, &x, &n, why, sizeof(why));
    uint32_t props = 0;
    for (uint32_t i = 0; ok && i < fx.noteCount; i++) {
        if (strncmp(fx.notes[i], "expect=", 7) == 0) {
            props++;
            if (!propOk(fx.notes[i] + 7)) {
                snprintf(why, sizeof(why), "the property '%s' is not QUANTITY OP VALUE",
                         fx.notes[i] + 7);
                ok = false;
            }
        }
    }
    if (ok && props == 0) {
        snprintf(why, sizeof(why),
                 "it declares no property (expect=): a fixture must say what "
                 "it is meant to carry");
        ok = false;
    }
    if (ok) {
        printf("%s: no art; %s; expands to its pin (%zu bytes); %u properties\n", baseName(path),
               stats, n, props);
    } else {
        fprintf(stderr, "%s: %s\n", path, why);
    }
    free(x);
    rd_fixture_free(&fx);
    return ok;
}

typedef struct Manifest {
    char name[MANIFEST_LINES][64];
    char source[MANIFEST_LINES][256];
    char rules[MANIFEST_LINES][128];
    char props[MANIFEST_LINES][512]; /* blank-separated */
    int n;
} Manifest;

/* MANIFEST: "NAME SOURCE [rules=FILE] [PROP...]" a line, '#' comments; NAME
 * the fixture's file name without .rdfix, SOURCE the dump's name below the
 * sources folder (relative: no leading '/', no '..'), FILE below the
 * manifest's folder */
static bool readManifest(const char *path, Manifest *m)
{
    memset(m, 0, sizeof(*m));
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "%s: cannot open\n", path);
        return false;
    }
    char line[2048];
    int ln = 0;
    bool ok = true;
    while (ok && fgets(line, sizeof(line), fp)) {
        ln++;
        char *h = strchr(line, '#');
        if (h) {
            *h = '\0';
        }
        char *tok[64];
        int nt = 0;
        for (char *p = line; *p && nt < 64;) {
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
                p++;
            }
            if (!*p) {
                break;
            }
            tok[nt++] = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
                p++;
            }
            if (*p) {
                *p++ = '\0';
            }
        }
        if (nt == 0) {
            continue;
        }
        if (nt < 2 || m->n >= MANIFEST_LINES || strlen(tok[0]) >= 64 || strlen(tok[1]) >= 256 ||
            tok[1][0] == '/' || tok[1][0] == '\\' || strstr(tok[1], "..") || strchr(tok[0], '/')) {
            fprintf(stderr,
                    "%s:%d: want NAME SOURCE [rules=FILE] [PROP...] with a relative "
                    "SOURCE\n",
                    path, ln);
            ok = false;
            break;
        }
        snprintf(m->name[m->n], 64, "%s", tok[0]);
        snprintf(m->source[m->n], 256, "%s", tok[1]);
        for (int i = 2; i < nt; i++) {
            if (strncmp(tok[i], "rules=", 6) == 0) {
                snprintf(m->rules[m->n], 128, "%s", tok[i] + 6);
            } else if (propOk(tok[i]) &&
                       strlen(m->props[m->n]) + strlen(tok[i]) + 2 < sizeof(m->props[0])) {
                strcat(m->props[m->n], m->props[m->n][0] ? " " : "");
                strcat(m->props[m->n], tok[i]);
            } else {
                fprintf(stderr, "%s:%d: '%s' is no property (QUANTITY OP VALUE)\n", path, ln,
                        tok[i]);
                ok = false;
            }
        }
        m->n++;
    }
    fclose(fp);
    return ok;
}

static void dirOf(const char *path, char *out, size_t size)
{
    snprintf(out, size, "%s", path);
    char *a = strrchr(out, '/'), *b = strrchr(out, '\\');
    char *s = a && b ? (a > b ? a : b) : a ? a : b;
    if (s) {
        *s = '\0';
    } else {
        snprintf(out, size, ".");
    }
}

static bool selfcheckLine(const RdFixture *fx, const uint8_t *src, size_t srcN, char *line,
                          size_t size, bool *clean)
{
    uint8_t *x = NULL;
    size_t xn = 0;
    char why[512] = "";
    uint64_t windows = 0, hits = 0;
    *clean = false;
    if (!rd_fixture_expand(fx, false, &x, &xn, why, sizeof(why)) ||
        !rd_fixture_selfcheck(src, srcN, x, xn, &windows, &hits, why, sizeof(why))) {
        snprintf(line, size, "%s", why);
        free(x);
        return false;
    }
    free(x);
    *clean = hits == 0;
    snprintf(line, size, "%llu of %llu generated 32-byte windows found in the source's art",
             (unsigned long long)hits, (unsigned long long)windows);
    return true;
}

static int makeOne(const char *dumpPath, const char *outPath, const char *rulesPath,
                   const char *source, const char *const *props, int nprops, bool selfcheck)
{
    size_t n = 0;
    uint8_t *dump = readAll(dumpPath, &n);
    if (!dump) {
        fprintf(stderr, "%s: cannot read the dump\n", dumpPath);
        return 1;
    }
    char *rules = NULL;
    if (rulesPath) {
        size_t rn = 0;
        uint8_t *r = readAll(rulesPath, &rn);
        rules = r ? realloc(r, rn + 1) : NULL;
        if (!rules) {
            free(r);
            free(dump);
            fprintf(stderr, "%s: cannot read the rules\n", rulesPath);
            return 1;
        }
        rules[rn] = '\0';
    }
    RdFixture fx;
    char why[512] = "", report[1024] = "";
    bool ok = rd_fixture_make(dump, n, rules, &fx, report, sizeof(report), why, sizeof(why));
    if (ok) {
        ok = rd_fixture_add_note(&fx, "source", source ? source : baseName(dumpPath));
        for (int i = 0; ok && i < nprops; i++) {
            ok = propOk(props[i]) && rd_fixture_add_note(&fx, "expect", props[i]);
            if (!ok) {
                snprintf(why, sizeof(why), "'%s' is no property (QUANTITY OP VALUE)", props[i]);
            }
        }
    }
    bool clean = true;
    if (ok && selfcheck) {
        char line[200];
        ok = selfcheckLine(&fx, dump, n, line, sizeof(line), &clean);
        if (ok) {
            (void)rd_fixture_add_note(&fx, "selfcheck", line);
            printf("%s: window check: %s\n", baseName(outPath), line);
        } else {
            snprintf(why, sizeof(why), "window check: %s", line);
        }
    }
    free(dump);
    free(rules);
    if (ok && !clean) {
        snprintf(why, sizeof(why),
                 "the window check found generated bytes in the source: "
                 "not written");
        ok = false;
    }
    if (ok) {
        char t[512] = "";
        ok = rd_fixture_check(&fx, t, sizeof(t), NULL, 0) &&
             rd_fixture_write(outPath, &fx, why, sizeof(why));
        if (t[0]) {
            snprintf(why, sizeof(why), "%s", t);
        }
    }
    if (!ok) {
        fprintf(stderr, "%s: %s\n", dumpPath, why);
        rd_fixture_free(&fx);
        return 1;
    }
    char hex[65];
    rd_fx_sha_hex(fx.sha, hex);
    printf("%s: %s\n%s: written (expands to %llu bytes, SHA-256 %s)\n", baseName(outPath), report,
           outPath, (unsigned long long)fx.expandedBytes, hex);
    rd_fixture_free(&fx);
    return 0;
}

static int cmdRemake(const char *manifest, const char *sources, const char *outDir, int nnames,
                     char **names)
{
    static Manifest m;
    if (!readManifest(manifest, &m)) {
        return 1;
    }
    char mdir[1024];
    dirOf(manifest, mdir, sizeof(mdir));
    int failures = 0, made = 0;
    for (int i = 0; i < m.n; i++) {
        bool want = nnames == 0;
        for (int k = 0; k < nnames; k++) {
            want = want || strcmp(names[k], m.name[i]) == 0;
        }
        if (!want) {
            continue;
        }
        char src[1400], out[1200], rules[1200];
        snprintf(src, sizeof(src), "%s/%s", sources, m.source[i]);
        snprintf(out, sizeof(out), "%s/%s.rdfix", outDir, m.name[i]);
        snprintf(rules, sizeof(rules), "%s/%s", mdir, m.rules[i]);
        const char *props[64];
        int np = 0;
        char copy[512];
        snprintf(copy, sizeof(copy), "%s", m.props[i]);
        for (char *p = strtok(copy, " "); p && np < 64; p = strtok(NULL, " ")) {
            props[np++] = p;
        }
        failures += makeOne(src, out, m.rules[i][0] ? rules : NULL, m.source[i], props, np, true);
        made++;
    }
    printf("remake: %d fixtures made, %d failed\n", made - failures, failures);
    return failures || made == 0 ? 1 : 0;
}

static bool hasSuffix(const char *s, const char *x)
{
    const size_t a = strlen(s), b = strlen(x);
    return a >= b && strcmp(s + a - b, x) == 0;
}

static int cmdCheckDir(const char *dir, const char *manifest, int nfiles, char **files)
{
    static Manifest m;
    if (!readManifest(manifest, &m)) {
        return 1;
    }
    int failures = 0;
    /* every manifest fixture is there and checks */
    for (int i = 0; i < m.n; i++) {
        char p[1200];
        snprintf(p, sizeof(p), "%s/%s.rdfix", dir, m.name[i]);
        if (!checkOne(p)) {
            failures++;
            continue;
        }
        RdFixture fx;
        char why[256];
        if (rd_fixture_read(p, &fx, why, sizeof(why))) {
            /* the fixture's properties are the manifest's */
            char got[512] = "";
            for (uint32_t k = 0; k < fx.noteCount; k++) {
                if (strncmp(fx.notes[k], "expect=", 7) == 0 &&
                    strlen(got) + strlen(fx.notes[k]) < sizeof(got) - 2) {
                    strcat(got, got[0] ? " " : "");
                    strcat(got, fx.notes[k] + 7);
                }
            }
            if (strcmp(got, m.props[i]) != 0) {
                fprintf(stderr, "%s: declares '%s', the manifest '%s' (set-props, or remake)\n", p,
                        got, m.props[i]);
                failures++;
            }
            rd_fixture_free(&fx);
        }
    }
    /* and every file is the manifest's (the files: given by the caller, the
       folder's *.rdfix) */
    for (int k = 0; k < nfiles; k++) {
        if (!hasSuffix(files[k], ".rdfix")) {
            continue;
        }
        char name[256];
        snprintf(name, sizeof(name), "%s", baseName(files[k]));
        name[strlen(name) - 6] = '\0';
        bool listed = false;
        for (int i = 0; i < m.n; i++) {
            listed = listed || strcmp(m.name[i], name) == 0;
        }
        if (!listed) {
            fprintf(stderr, "%s: not in %s\n", files[k], manifest);
            failures++;
        }
    }
    printf("check-dir: %d fixtures in the manifest, %d failures\n", m.n, failures);
    return failures ? 1 : 0;
}

static int cmdSelfcheckDir(const char *dir, const char *manifest, const char *sources)
{
    if (!sources || !sources[0]) {
        sources = getenv("ICO_FIXTURE_SOURCES");
    }
    if (!sources || !sources[0]) {
        printf("SKIP: no sources folder (ICO_FIXTURE_SOURCES): the window check runs where the "
               "source dumps are\n");
        return 77;
    }
    static Manifest m;
    if (!readManifest(manifest, &m)) {
        return 1;
    }
    int failures = 0, checked = 0;
    for (int i = 0; i < m.n; i++) {
        char p[1200], src[1400];
        snprintf(p, sizeof(p), "%s/%s.rdfix", dir, m.name[i]);
        snprintf(src, sizeof(src), "%s/%s", sources, m.source[i]);
        size_t n = 0;
        uint8_t *dump = readAll(src, &n);
        if (!dump) {
            printf("%s: its source %s is not in the sources folder: not checked\n", m.name[i],
                   m.source[i]);
            continue;
        }
        RdFixture fx;
        char why[512] = "", line[256] = "";
        bool clean = false;
        const bool ok = rd_fixture_read(p, &fx, why, sizeof(why)) &&
                        selfcheckLine(&fx, dump, n, line, sizeof(line), &clean);
        free(dump);
        rd_fixture_free(&fx);
        checked++;
        if (!ok || !clean) {
            fprintf(stderr, "%s: window check: %s\n", m.name[i], ok ? line : why);
            failures++;
        } else {
            printf("%s: window check: %s\n", m.name[i], line);
        }
    }
    printf("selfcheck-dir: %d checked, %d failures\n", checked, failures);
    if (checked == 0) {
        printf("SKIP: no source in the sources folder\n");
        return 77;
    }
    return failures ? 1 : 0;
}

static int cmdProps(const char *path, bool info)
{
    RdFixture fx;
    char why[512] = "", stats[512] = "";
    if (!rd_fixture_read(path, &fx, why, sizeof(why))) {
        fprintf(stderr, "%s: %s\n", path, why);
        return 1;
    }
    if (info) {
        char hex[65];
        rd_fx_sha_hex(fx.sha, hex);
        const bool ok = rd_fixture_check(&fx, why, sizeof(why), stats, sizeof(stats));
        printf("%s: format %u, dump version %u, skeleton %zu bytes, expands to %llu bytes (SHA-256 "
               "%s)\n%s: %s\n",
               baseName(path), fx.format, fx.dumpVersion, fx.skelBytes,
               (unsigned long long)fx.expandedBytes, hex, baseName(path), ok ? stats : why);
    }
    for (uint32_t i = 0; i < fx.noteCount; i++) {
        if (info) {
            printf("  %s\n", fx.notes[i]);
        } else if (strncmp(fx.notes[i], "expect=", 7) == 0) {
            printf("%s\n", fx.notes[i] + 7);
        }
    }
    rd_fixture_free(&fx);
    return 0;
}

static int cmdSetProps(const char *path, int n, char **props)
{
    RdFixture fx;
    char why[512] = "";
    if (!rd_fixture_read(path, &fx, why, sizeof(why))) {
        fprintf(stderr, "%s: %s\n", path, why);
        return 1;
    }
    rd_fixture_drop_notes(&fx, "expect");
    bool ok = true;
    for (int i = 0; ok && i < n; i++) {
        ok = propOk(props[i]) && rd_fixture_add_note(&fx, "expect", props[i]);
        if (!ok) {
            snprintf(why, sizeof(why), "'%s' is no property (QUANTITY OP VALUE)", props[i]);
        }
    }
    ok = ok && rd_fixture_write(path, &fx, why, sizeof(why));
    rd_fixture_free(&fx);
    if (!ok) {
        fprintf(stderr, "%s: %s\n", path, why);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        return usage();
    }
    const char *cmd = argv[1];
    if (strcmp(cmd, "expand") == 0 && argc == 4) {
        return cmdExpand(argv[2], argv[3]);
    }
    if (strcmp(cmd, "check") == 0 && argc >= 3) {
        int failures = 0;
        for (int i = 2; i < argc; i++) {
            failures += !checkOne(argv[i]);
        }
        return failures ? 1 : 0;
    }
    if (strcmp(cmd, "check-dir") == 0 && argc >= 4) {
        return cmdCheckDir(argv[2], argv[3], argc - 4, argv + 4);
    }
    if (strcmp(cmd, "make") == 0 && argc >= 4) {
        const char *rules = NULL, *source = NULL;
        const char *props[64];
        int np = 0;
        bool self = false;
        for (int i = 4; i < argc; i++) {
            if (strcmp(argv[i], "--rules") == 0 && i + 1 < argc) {
                rules = argv[++i];
            } else if (strcmp(argv[i], "--source") == 0 && i + 1 < argc) {
                source = argv[++i];
            } else if (strcmp(argv[i], "--expect") == 0 && i + 1 < argc && np < 64) {
                props[np++] = argv[++i];
            } else if (strcmp(argv[i], "--selfcheck") == 0) {
                self = true;
            } else {
                return usage();
            }
        }
        return makeOne(argv[2], argv[3], rules, source, props, np, self);
    }
    if (strcmp(cmd, "remake") == 0 && argc >= 5) {
        return cmdRemake(argv[2], argv[3], argv[4], argc - 5, argv + 5);
    }
    if (strcmp(cmd, "selfcheck") == 0 && argc == 4) {
        RdFixture fx;
        char why[512] = "", line[256] = "";
        size_t n = 0;
        uint8_t *dump = readAll(argv[3], &n);
        bool clean = false;
        const bool ok = dump && rd_fixture_read(argv[2], &fx, why, sizeof(why)) &&
                        selfcheckLine(&fx, dump, n, line, sizeof(line), &clean);
        free(dump);
        if (ok) {
            rd_fixture_free(&fx);
        }
        printf("%s: window check: %s\n", baseName(argv[2]), ok ? line : dump ? why : "no dump");
        return ok && clean ? 0 : 1;
    }
    if (strcmp(cmd, "selfcheck-dir") == 0 && (argc == 4 || argc == 5)) {
        return cmdSelfcheckDir(argv[2], argv[3], argc == 5 ? argv[4] : NULL);
    }
    if (strcmp(cmd, "props") == 0 && argc == 3) {
        return cmdProps(argv[2], false);
    }
    if (strcmp(cmd, "info") == 0 && argc == 3) {
        return cmdProps(argv[2], true);
    }
    if (strcmp(cmd, "set-props") == 0 && argc >= 4) {
        return cmdSetProps(argv[2], argc - 3, argv + 3);
    }
    if (strcmp(cmd, "rules") == 0) {
        fputs(rd_fx_default_rules, stdout);
        return 0;
    }
    if (strcmp(cmd, "patterns") == 0) {
        for (int i = 0; rd_fx_pattern_name(i); i++) {
            printf("%s\n", rd_fx_pattern_name(i));
        }
        return 0;
    }
    return usage();
}
