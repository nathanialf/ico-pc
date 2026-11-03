/*
 * port/platform/test/gpu_driver_test.c
 *
 * The Android build's graphics driver packages (port/platform/gpu_driver.c),
 * on any platform: meta.json read as the community
 * packages write it; bad library names, a missing name and a too high
 * minApi refused; zips made here with miniz's writer installed, and the
 * unsafe ones (a path in a name, "..", a backslash, a directory, a file
 * over the size limit, no meta.json, the library missing) refused with
 * nothing left behind; the listing sorted with broken folders left out;
 * remove; the trial marker's states; the ELF page-size check of a driver
 * library (two synthetic ELF64 headers, 4 KB and 16 KB aligned, on a 4 KB
 * and a 16 KB phone, at install too); the last-seen driver strings written,
 * read back and put into the page's words; the loss window of the trial.
 *
 * argv[1]: a folder the test may write in (the build folder).
 */
#include "gpu_driver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_fs.h"
#include "miniz.h"

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* a package as K11MCH1's AdrenoToolsDrivers releases write it */
static const char k_meta[] =
    "{\"schemaVersion\":1,\"name\":\"Turnip\",\"description\":\"Mesa's open driver for "
    "Adreno\",\"author\":\"K11MCH1\",\"packageVersion\":\"1\",\"vendor\":\"Mesa\","
    "\"driverVersion\":\"24.1\",\"minApi\":28,\"libraryName\":\"vulkan.ad07XX.so\"}";

static char s_root[1100], s_work[1024];

static void path_in(char *out, size_t n, const char *dir, const char *name)
{
    /* the test's paths are short; a cut would only make a later check fail */
    const int w = snprintf(out, n, "%s/%s", dir, name);
    if (w < 0 || (size_t)w >= n) {
        out[0] = '\0';
    }
}

static int exists(const char *dir, const char *name)
{
    char p[1200];
    path_in(p, sizeof(p), dir, name);
    return ico_path_kind(p, NULL, NULL) >= 0;
}

/* --- meta.json --------------------------------------------------------------- */

static int parse(const char *text, IcoGpuDriverMeta *m)
{
    char why[160];
    return ico_gpu_driver_parse_meta(text, strlen(text), m, why, sizeof(why));
}

static void test_meta(void)
{
    IcoGpuDriverMeta m;
    char why[160], buf[160];

    CHECK(ico_gpu_driver_parse_meta(k_meta, strlen(k_meta), &m, why, sizeof(why)) == 0);
    CHECK(m.schemaVersion == 1 && m.minApi == 28);
    CHECK(strcmp(m.name, "Turnip") == 0 && strcmp(m.vendor, "Mesa") == 0);
    CHECK(strcmp(m.author, "K11MCH1") == 0 && strcmp(m.packageVersion, "1") == 0);
    CHECK(strcmp(m.driverVersion, "24.1") == 0);
    CHECK(strcmp(m.libraryName, "vulkan.ad07XX.so") == 0);
    CHECK(strstr(m.description, "Adreno") != NULL);
    ico_gpu_driver_folder_name(&m, buf, sizeof(buf));
    CHECK(strcmp(buf, "Turnip-24.1") == 0);
    CHECK(ico_gpu_driver_folder_ok(buf));
    ico_gpu_driver_display_name(&m, buf, sizeof(buf));
    CHECK(strcmp(buf, "Turnip 24.1") == 0);

    /* only name and libraryName are needed */
    CHECK(parse("{\"name\":\"Qualcomm v615\",\"libraryName\":\"vulkan.ad0615.so\"}", &m) == 0);
    CHECK(m.minApi == 0 && m.driverVersion[0] == '\0');
    ico_gpu_driver_folder_name(&m, buf, sizeof(buf));
    CHECK(strcmp(buf, "Qualcomm_v615") == 0);
    ico_gpu_driver_display_name(&m, buf, sizeof(buf));
    CHECK(strcmp(buf, "Qualcomm v615") == 0);

    /* refused */
    CHECK(parse("{\"name\":\"Turnip\"}", &m) != 0);                                 /* no lib */
    CHECK(parse("{\"name\":\"Turnip\",\"libraryName\":\"\"}", &m) != 0);            /* empty */
    CHECK(parse("{\"name\":\"T\",\"libraryName\":\"lib/vulkan.so\"}", &m) != 0);    /* slash */
    CHECK(parse("{\"name\":\"T\",\"libraryName\":\"..\\\\vulkan.so\"}", &m) != 0);  /* backslash */
    CHECK(parse("{\"name\":\"T\",\"libraryName\":\"../vulkan.so\"}", &m) != 0);     /* .. */
    CHECK(parse("{\"name\":\"T\",\"libraryName\":\"..vulkan.so\"}", &m) != 0);      /* .. */
    CHECK(parse("{\"name\":\"T\",\"libraryName\":\"vulkan.so.1\"}", &m) != 0);      /* not .so */
    CHECK(parse("{\"name\":\"T\",\"libraryName\":\"vulkan adreno.so\"}", &m) != 0); /* space */
    CHECK(parse("{\"name\":\"T\",\"libraryName\":7}", &m) != 0);
    CHECK(parse("{\"libraryName\":\"vulkan.so\"}", &m) != 0); /* no name */
    CHECK(parse("{\"name\":\"T\",\"libraryName\":\"v.so\",\"minApi\":\"28\"}", &m) != 0);
    CHECK(parse("{\"name\":\"T\",\"libraryName\":\"v.so\",\"minApi\":28.5}", &m) != 0);
    CHECK(parse("[1]", &m) != 0);
    CHECK(parse("{\"name\":", &m) != 0);
    CHECK(m.name[0] == '\0'); /* zeroed on failure */

    CHECK(ico_gpu_driver_library_name_ok("libvulkan_freedreno.so"));
    CHECK(!ico_gpu_driver_library_name_ok(".so"));
    CHECK(ico_gpu_driver_entry_name_ok("notgsl.so") && ico_gpu_driver_entry_name_ok("meta.json"));
    CHECK(!ico_gpu_driver_entry_name_ok("a/b.so") && !ico_gpu_driver_entry_name_ok("a\\b.so"));
    CHECK(!ico_gpu_driver_entry_name_ok("../b.so") && !ico_gpu_driver_entry_name_ok(".hidden"));

    /* folder names stay safe */
    memset(&m, 0, sizeof(m));
    snprintf(m.name, sizeof(m.name), "../../evil name");
    snprintf(m.driverVersion, sizeof(m.driverVersion), "1/2");
    ico_gpu_driver_folder_name(&m, buf, sizeof(buf));
    CHECK(ico_gpu_driver_folder_ok(buf) && strchr(buf, '/') == NULL && strstr(buf, "..") == NULL);
    snprintf(m.name, sizeof(m.name), "starting");
    m.driverVersion[0] = '\0';
    ico_gpu_driver_folder_name(&m, buf, sizeof(buf));
    CHECK(strcmp(buf, "starting") != 0 && ico_gpu_driver_folder_ok(buf));
    snprintf(m.name, sizeof(m.name), "x.tmp");
    ico_gpu_driver_folder_name(&m, buf, sizeof(buf));
    CHECK(ico_gpu_driver_folder_ok(buf));
    CHECK(!ico_gpu_driver_folder_ok("..") && !ico_gpu_driver_folder_ok("a/b") &&
          !ico_gpu_driver_folder_ok("") && !ico_gpu_driver_folder_ok("x.tmp"));
}

/* --- zips -------------------------------------------------------------------- */

typedef struct Entry {
    const char *name;
    const void *data;
    size_t size;
} Entry;

/* writes a zip of the entries to <work>/file (miniz's writer takes any
   name but one starting with '/') */
static int write_zip(const char *file, const Entry *e, int n, char *out, size_t outN)
{
    mz_zip_archive z;
    void *buf = NULL;
    size_t size = 0;

    memset(&z, 0, sizeof(z));
    if (!mz_zip_writer_init_heap(&z, 0, 0)) {
        return -1;
    }
    for (int i = 0; i < n; i++) {
        if (!mz_zip_writer_add_mem(&z, e[i].name, e[i].data, e[i].size, MZ_BEST_SPEED)) {
            mz_zip_writer_end(&z);
            return -1;
        }
    }
    if (!mz_zip_writer_finalize_heap_archive(&z, &buf, &size)) {
        mz_zip_writer_end(&z);
        return -1;
    }
    mz_zip_writer_end(&z);
    path_in(out, outN, s_work, file);
    FILE *f = fopen(out, "wb");
    const int ok = f != NULL && fwrite(buf, 1, size, f) == size;
    if (f != NULL) {
        fclose(f);
    }
    mz_free(buf);
    return ok ? 0 : -1;
}

static const char k_lib[] = "\177ELF not really a library";
static const char k_dep[] = "\177ELF a library the driver needs";

static int install(const Entry *e, int n, int api, char *folder, size_t folderN)
{
    char zip[1200], why[256];
    if (write_zip("pkg.zip", e, n, zip, sizeof(zip)) != 0) {
        fprintf(stderr, "cannot write the test zip\n");
        failures++;
        return 99;
    }
    const int r = ico_gpu_driver_install_zip(s_root, zip, api, folder, folderN, why, sizeof(why));
    if (r != 0) {
        printf("install refused (expected or not): %s\n", why);
    }
    return r;
}

/* the root holds no folder but the installed ones (no staging left) */
static int count_cb(const char *path, const char *name, void *user)
{
    (void)path;
    (void)name;
    (*(int *)user)++;
    return 0;
}

static int files_under_root(void)
{
    int n = 0;
    ico_dir_walk(s_root, 1, count_cb, &n);
    return n;
}

static void test_install(void)
{
    char folder[160], zip[1200], why[256];
    const Entry good[] = {
        {"meta.json", k_meta, sizeof(k_meta) - 1},
        {"vulkan.ad07XX.so", k_lib, sizeof(k_lib)},
        {"notgsl.so", k_dep, sizeof(k_dep)},
    };

    CHECK(install(good, 3, 34, folder, sizeof(folder)) == ICO_GPU_DRIVER_OK);
    CHECK(strcmp(folder, "Turnip-24.1") == 0);
    {
        char dir[1200];
        path_in(dir, sizeof(dir), s_root, folder);
        CHECK(exists(dir, "meta.json") && exists(dir, "vulkan.ad07XX.so") &&
              exists(dir, "notgsl.so"));
        char lib[1300];
        path_in(lib, sizeof(lib), dir, "vulkan.ad07XX.so");
        unsigned long long size = 0;
        CHECK(ico_path_kind(lib, &size, NULL) == 0 && size == sizeof(k_lib));
    }
    CHECK(!exists(s_root, "Turnip-24.1.tmp"));
    /* the same package again replaces it */
    CHECK(install(good, 2, 34, folder, sizeof(folder)) == ICO_GPU_DRIVER_OK);
    {
        char dir[1200];
        path_in(dir, sizeof(dir), s_root, folder);
        CHECK(!exists(dir, "notgsl.so"));
    }
    const int before = files_under_root();

    /* the API level */
    CHECK(install(good, 3, 27, folder, sizeof(folder)) == ICO_GPU_DRIVER_BAD);
    CHECK(folder[0] == '\0');
    CHECK(install(good, 3, 28, folder, sizeof(folder)) == ICO_GPU_DRIVER_OK);

    /* unsafe or broken packages: nothing new under the root */
    const Entry slip[] = {
        {"meta.json", k_meta, sizeof(k_meta) - 1},
        {"vulkan.ad07XX.so", k_lib, sizeof(k_lib)},
        {"../evil.so", k_dep, sizeof(k_dep)},
    };
    CHECK(install(slip, 3, 34, folder, sizeof(folder)) == ICO_GPU_DRIVER_BAD);
    CHECK(!exists(s_work, "evil.so"));
    const Entry sub[] = {
        {"meta.json", k_meta, sizeof(k_meta) - 1},
        {"vulkan.ad07XX.so", k_lib, sizeof(k_lib)},
        {"lib/evil.so", k_dep, sizeof(k_dep)},
    };
    CHECK(install(sub, 3, 34, folder, sizeof(folder)) == ICO_GPU_DRIVER_BAD);
    const Entry back[] = {
        {"meta.json", k_meta, sizeof(k_meta) - 1},
        {"vulkan.ad07XX.so", k_lib, sizeof(k_lib)},
        {"x\\evil.so", k_dep, sizeof(k_dep)},
    };
    CHECK(install(back, 3, 34, folder, sizeof(folder)) == ICO_GPU_DRIVER_BAD);
    const Entry dir[] = {
        {"meta.json", k_meta, sizeof(k_meta) - 1},
        {"vulkan.ad07XX.so", k_lib, sizeof(k_lib)},
        {"extra/", NULL, 0},
    };
    CHECK(install(dir, 3, 34, folder, sizeof(folder)) == ICO_GPU_DRIVER_BAD);
    const Entry nometa[] = {{"vulkan.ad07XX.so", k_lib, sizeof(k_lib)}};
    CHECK(install(nometa, 1, 34, folder, sizeof(folder)) == ICO_GPU_DRIVER_BAD);
    const Entry nolib[] = {{"meta.json", k_meta, sizeof(k_meta) - 1},
                           {"notgsl.so", k_dep, sizeof(k_dep)}};
    CHECK(install(nolib, 2, 34, folder, sizeof(folder)) == ICO_GPU_DRIVER_BAD);
    static const char badLibMeta[] = "{\"name\":\"T\",\"libraryName\":\"../vulkan.so\"}";
    const Entry badlib[] = {{"meta.json", badLibMeta, sizeof(badLibMeta) - 1},
                            {"vulkan.so", k_lib, sizeof(k_lib)}};
    CHECK(install(badlib, 2, 34, folder, sizeof(folder)) == ICO_GPU_DRIVER_BAD);

    /* the size limit per file (a small one here; 64 MB in the game) */
    {
        static char big[5000];
        memset(big, 'x', sizeof(big));
        const Entry large[] = {
            {"meta.json", k_meta, sizeof(k_meta) - 1},
            {"vulkan.ad07XX.so", k_lib, sizeof(k_lib)},
            {"big.so", big, sizeof(big)},
        };
        CHECK(write_zip("big.zip", large, 3, zip, sizeof(zip)) == 0);
        CHECK(ico_gpu_driver_install_zip_max(s_root, zip, 34, 4096, folder, sizeof(folder), why,
                                             sizeof(why)) == ICO_GPU_DRIVER_BAD);
        CHECK(ico_gpu_driver_install_zip_max(s_root, zip, 34, 5000, folder, sizeof(folder), why,
                                             sizeof(why)) == ICO_GPU_DRIVER_OK);
        CHECK(install(good, 3, 34, folder, sizeof(folder)) == ICO_GPU_DRIVER_OK);
    }
    /* not a zip */
    {
        path_in(zip, sizeof(zip), s_work, "plain.zip");
        FILE *f = fopen(zip, "wb");
        if (f != NULL) {
            fputs("not a zip at all", f);
            fclose(f);
        }
        CHECK(ico_gpu_driver_install_zip(s_root, zip, 34, folder, sizeof(folder), why,
                                         sizeof(why)) == ICO_GPU_DRIVER_BAD);
        path_in(zip, sizeof(zip), s_work, "missing.zip");
        CHECK(ico_gpu_driver_install_zip(s_root, zip, 34, folder, sizeof(folder), why,
                                         sizeof(why)) == ICO_GPU_DRIVER_BAD);
    }
    CHECK(files_under_root() == before + 1); /* the good package's notgsl.so is back */
}

/* --- list, remove ------------------------------------------------------------ */

static void write_file(const char *dir, const char *name, const char *text)
{
    char p[1200];
    path_in(p, sizeof(p), dir, name);
    FILE *f = fopen(p, "wb");
    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

static void test_list_zips(void)
{
    char names[8][160], dir[1200];

    path_in(dir, sizeof(dir), s_work, "zips");
    ico_mkdir(dir);
    write_file(dir, "a.zip", "x");
    write_file(dir, "B.ZIP", "x");
    write_file(dir, "incoming.zip", "x");
    write_file(dir, "incoming.zip.tmp", "x");
    write_file(dir, "notes.txt", "x");
    write_file(dir, ".hidden.zip", "x");
    write_file(dir, "zip", "x");
    {
        char sub[1200];
        path_in(sub, sizeof(sub), dir, "folder.zip");
        ico_mkdir(sub);
        write_file(sub, "inner.zip", "x");
    }
    memset(names, 0, sizeof(names));
    const int n = ico_gpu_driver_list_zips(dir, names, 8);
    CHECK(n == 2);
    if (n == 2) {
        CHECK(strcmp(names[0], "B.ZIP") == 0); /* byte order */
        CHECK(strcmp(names[1], "a.zip") == 0);
    }
    CHECK(ico_gpu_driver_list_zips(dir, names, 1) == 1);
    CHECK(ico_gpu_driver_list_zips(dir, names, 0) == 0);
    path_in(dir, sizeof(dir), s_work, "nowhere");
    CHECK(ico_gpu_driver_list_zips(dir, names, 8) == 0);
}

static void test_list(void)
{
    IcoGpuDriver d[ICO_GPU_DRIVER_LIST_MAX];
    char folder[160], dir[1200];
    static const char metaA[] = "{\"name\":\"Adreno 615\",\"driverVersion\":\"0.615.0\","
                                "\"libraryName\":\"vulkan.ad0615.so\"}";
    const Entry a[] = {{"meta.json", metaA, sizeof(metaA) - 1},
                       {"vulkan.ad0615.so", k_lib, sizeof(k_lib)}};

    CHECK(install(a, 2, 34, folder, sizeof(folder)) == ICO_GPU_DRIVER_OK);
    CHECK(strcmp(folder, "Adreno_615-0.615.0") == 0);
    /* broken folders: no meta.json, a bad one, the library missing, a
       staging folder, a file in the root */
    path_in(dir, sizeof(dir), s_root, "Empty");
    ico_mkdir(dir);
    path_in(dir, sizeof(dir), s_root, "Bad");
    ico_mkdir(dir);
    write_file(dir, "meta.json", "{not json");
    path_in(dir, sizeof(dir), s_root, "NoLib");
    ico_mkdir(dir);
    write_file(dir, "meta.json", "{\"name\":\"N\",\"libraryName\":\"n.so\"}");
    path_in(dir, sizeof(dir), s_root, "Half.tmp");
    ico_mkdir(dir);
    write_file(dir, "meta.json", "{\"name\":\"H\",\"libraryName\":\"h.so\"}");
    write_file(dir, "h.so", "x");
    write_file(s_root, "meta.json", "{\"name\":\"R\",\"libraryName\":\"r.so\"}");
    write_file(s_root, "r.so", "x");

    const int n = ico_gpu_driver_list(s_root, d, ICO_GPU_DRIVER_LIST_MAX);
    CHECK(n == 2);
    if (n == 2) {
        CHECK(strcmp(d[0].folder, "Adreno_615-0.615.0") == 0);
        CHECK(strcmp(d[1].folder, "Turnip-24.1") == 0);
        CHECK(strcmp(d[1].meta.libraryName, "vulkan.ad07XX.so") == 0);
    }
    CHECK(ico_gpu_driver_list(s_root, d, 1) == 1);

    CHECK(ico_gpu_driver_remove(s_root, "Adreno_615-0.615.0") == 0);
    CHECK(!exists(s_root, "Adreno_615-0.615.0"));
    CHECK(ico_gpu_driver_list(s_root, d, ICO_GPU_DRIVER_LIST_MAX) == 1);
    CHECK(ico_gpu_driver_remove(s_root, "Adreno_615-0.615.0") != 0); /* gone */
    CHECK(ico_gpu_driver_remove(s_root, "..") != 0);
    CHECK(ico_gpu_driver_remove(s_root, "../work") != 0);
    CHECK(ico_gpu_driver_remove(s_root, "Empty") == 0);

    /* a root that does not exist lists nothing */
    path_in(dir, sizeof(dir), s_work, "nowhere");
    CHECK(ico_gpu_driver_list(dir, d, ICO_GPU_DRIVER_LIST_MAX) == 0);
}

/* --- the trial marker -------------------------------------------------------- */

static void test_trial(void)
{
    char m[256];

    ico_gpu_driver_trial_ok(s_root);
    CHECK(ico_gpu_driver_trial_marker(s_root, m, sizeof(m)) == 0 && m[0] == '\0');
    CHECK(!ico_gpu_driver_trial_crashed(s_root, "Turnip-24.1"));
    CHECK(ico_gpu_driver_trial_begin(s_root, "Turnip-24.1") == 0);
    CHECK(ico_gpu_driver_trial_marker(s_root, m, sizeof(m)) == 1 && strcmp(m, "Turnip-24.1") == 0);
    CHECK(ico_gpu_driver_trial_crashed(s_root, "Turnip-24.1"));
    CHECK(!ico_gpu_driver_trial_crashed(s_root, "Other-1"));
    CHECK(!ico_gpu_driver_trial_crashed(s_root, ""));
    /* a newer trial replaces the marker */
    CHECK(ico_gpu_driver_trial_begin(s_root, "Other-1") == 0);
    CHECK(ico_gpu_driver_trial_crashed(s_root, "Other-1"));
    CHECK(!ico_gpu_driver_trial_crashed(s_root, "Turnip-24.1"));
    CHECK(!exists(s_root, "starting.tmp"));
    /* the marker never lists as a driver */
    {
        IcoGpuDriver d[4];
        CHECK(ico_gpu_driver_list(s_root, d, 4) == 1);
    }
    ico_gpu_driver_trial_ok(s_root);
    CHECK(!ico_gpu_driver_trial_crashed(s_root, "Other-1"));
    CHECK(!exists(s_root, "starting"));
    CHECK(ico_gpu_driver_trial_begin(s_root, "../x") != 0);
}

/* --- the 16 KB page check ----------------------------------------------------- */

static void le(unsigned char *p, unsigned long long v, int bytes)
{
    for (int i = 0; i < bytes; i++) {
        p[i] = (unsigned char)(v >> (8 * i));
    }
}

/* a little-endian ELF64 header with a program header table of PT_LOAD
   (twice, with the two alignments), PT_DYNAMIC after them; size 64 + 4 * 56 */
static size_t make_elf64(unsigned char *b, unsigned long long align0, unsigned long long align1)
{
    memset(b, 0, 64 + 4 * 56);
    memcpy(b, "\177ELF", 4);
    b[4] = 2; /* 64-bit */
    b[5] = 1; /* little-endian */
    b[6] = 1;
    le(b + 16, 3, 2);   /* ET_DYN */
    le(b + 18, 183, 2); /* EM_AARCH64 */
    le(b + 32, 64, 8);  /* e_phoff */
    le(b + 52, 64, 2);  /* e_ehsize */
    le(b + 54, 56, 2);  /* e_phentsize */
    le(b + 56, 4, 2);   /* e_phnum */
    const unsigned long long types[4] = {6 /* PT_PHDR */, 1, 1, 2 /* PT_DYNAMIC */};
    const unsigned long long aligns[4] = {8, align0, align1, 8};
    for (int i = 0; i < 4; i++) {
        unsigned char *ph = b + 64 + i * 56;
        le(ph, types[i], 4);
        le(ph + 48, aligns[i], 8);
    }
    return 64 + 4 * 56;
}

static void test_pages(void)
{
    unsigned char e4k[300], e16k[300], e16kMixed[300];
    const size_t n4 = make_elf64(e4k, 0x1000, 0x1000);
    const size_t n16 = make_elf64(e16k, 0x4000, 0x10000);
    make_elf64(e16kMixed, 0x4000, 0x1000); /* one segment still 4 KB aligned */
    unsigned long long least = 99;

    /* the 4 KB library: refused on 16 KB pages, fine on 4 KB pages */
    CHECK(ico_gpu_driver_elf_pages_ok(e4k, n4, 16384, &least) == 0 && least == 0x1000);
    CHECK(ico_gpu_driver_elf_pages_ok(e4k, n4, 4096, &least) == 1 && least == 0x1000);
    CHECK(ico_gpu_driver_elf_pages_ok(e4k, n4, 0, NULL) == 1);
    /* the 16 KB library (and a 64 KB one): fine on both */
    CHECK(ico_gpu_driver_elf_pages_ok(e16k, n16, 16384, &least) == 1 && least == 0x4000);
    CHECK(ico_gpu_driver_elf_pages_ok(e16k, n16, 4096, NULL) == 1);
    /* one 4 KB segment among 16 KB ones is enough to refuse */
    CHECK(ico_gpu_driver_elf_pages_ok(e16kMixed, n16, 16384, &least) == 0 && least == 0x1000);
    /* what cannot be read is not refused: not ELF, big-endian, a table past
       the bytes given, too short, NULL */
    CHECK(ico_gpu_driver_elf_pages_ok(k_lib, sizeof(k_lib), 16384, NULL) == 1);
    CHECK(ico_gpu_driver_elf_pages_ok(e4k, 40, 16384, NULL) == 1);
    CHECK(ico_gpu_driver_elf_pages_ok(e4k, 64 + 56, 16384, NULL) == 1);
    CHECK(ico_gpu_driver_elf_pages_ok(NULL, 0, 16384, NULL) == 1);
    {
        unsigned char be[300];
        memcpy(be, e4k, n4);
        be[5] = 2;
        CHECK(ico_gpu_driver_elf_pages_ok(be, n4, 16384, NULL) == 1);
    }

    /* at install: the library and every other .so of the package */
    {
        char folder[160], zip[1200], why[256];
        const Entry old4k[] = {{"meta.json", k_meta, sizeof(k_meta) - 1},
                               {"vulkan.ad07XX.so", e4k, n4}};
        CHECK(write_zip("pages.zip", old4k, 2, zip, sizeof(zip)) == 0);
        const int before = files_under_root();
        CHECK(ico_gpu_driver_install_zip_pages(s_root, zip, 34, 16384, folder, sizeof(folder), why,
                                               sizeof(why)) == ICO_GPU_DRIVER_BAD);
        CHECK(strcmp(why, ICO_GPU_DRIVER_PAGES_MESSAGE) == 0);
        CHECK(strstr(why, "16 KB memory pages") != NULL && folder[0] == '\0');
        CHECK(files_under_root() == before); /* nothing left behind */
        /* the same package on a 4 KB phone (and with the plain call) installs */
        CHECK(ico_gpu_driver_install_zip_pages(s_root, zip, 34, 4096, folder, sizeof(folder), why,
                                               sizeof(why)) == ICO_GPU_DRIVER_OK);
        CHECK(ico_gpu_driver_install_zip(s_root, zip, 34, folder, sizeof(folder), why,
                                         sizeof(why)) == ICO_GPU_DRIVER_OK);
        const Entry new16k[] = {{"meta.json", k_meta, sizeof(k_meta) - 1},
                                {"vulkan.ad07XX.so", e16k, n16}};
        CHECK(write_zip("pages16.zip", new16k, 2, zip, sizeof(zip)) == 0);
        CHECK(ico_gpu_driver_install_zip_pages(s_root, zip, 34, 16384, folder, sizeof(folder), why,
                                               sizeof(why)) == ICO_GPU_DRIVER_OK);
        /* a helper library with 4 KB alignment is refused too */
        const Entry dep4k[] = {{"meta.json", k_meta, sizeof(k_meta) - 1},
                               {"vulkan.ad07XX.so", e16k, n16},
                               {"notgsl.so", e4k, n4}};
        CHECK(write_zip("pagesdep.zip", dep4k, 3, zip, sizeof(zip)) == 0);
        CHECK(ico_gpu_driver_install_zip_pages(s_root, zip, 34, 16384, folder, sizeof(folder), why,
                                               sizeof(why)) == ICO_GPU_DRIVER_BAD);
        CHECK(ico_gpu_driver_install_zip_pages(s_root, zip, 34, 4096, folder, sizeof(folder), why,
                                               sizeof(why)) == ICO_GPU_DRIVER_OK);
    }
}

/* --- what a driver was last seen to be ------------------------------------------ */

static void test_seen(void)
{
    IcoGpuDriverSeen w, r;
    char t[256];

    /* Qualcomm's own driver, as the Retroid Pocket 6's log writes it */
    memset(&w, 0, sizeof(w));
    snprintf(w.name, sizeof(w.name), "Qualcomm Technologies Inc. Adreno Vulkan Driver");
    snprintf(w.version, sizeof(w.version), "512.676.1");
    w.driverId = 8;
    w.vulkanMajor = 1;
    w.vulkanMinor = 3;
    w.known = 1;
    CHECK(ico_gpu_driver_seen_read(s_root, NULL, &r) == 0 && !r.known);
    CHECK(ico_gpu_driver_seen_write(s_root, NULL, &w) == 0);
    CHECK(exists(s_root, "last_seen_system.json") && !exists(s_root, "last_seen_system.json.tmp"));
    CHECK(ico_gpu_driver_seen_read(s_root, NULL, &r) == 1 && r.known);
    CHECK(strcmp(r.name, w.name) == 0 && strcmp(r.version, "512.676.1") == 0 && r.driverId == 8);
    CHECK(r.vulkanMajor == 1 && r.vulkanMinor == 3 && r.mesa[0] == '\0');
    ico_gpu_driver_seen_text(&r, t, sizeof(t));
    CHECK(strcmp(t, "Qualcomm's driver 512.676.1, Vulkan 1.3") == 0);

    /* Turnip in an installed package's folder (Mesa 25.3.0-devel) */
    memset(&w, 0, sizeof(w));
    snprintf(w.name, sizeof(w.name), "turnip Mesa driver");
    snprintf(w.version, sizeof(w.version), "25.3.0");
    snprintf(w.mesa, sizeof(w.mesa), "Mesa 25.3.0-devel");
    w.driverId = 18;
    w.vulkanMajor = 1;
    w.vulkanMinor = 4;
    w.known = 1;
    CHECK(ico_gpu_driver_seen_read(s_root, "Turnip-24.1", &r) == 0);
    CHECK(ico_gpu_driver_seen_write(s_root, "Turnip-24.1", &w) == 0);
    CHECK(exists(s_root, "Turnip-24.1/last_seen.json"));
    CHECK(ico_gpu_driver_seen_read(s_root, "Turnip-24.1", &r) == 1);
    CHECK(strcmp(r.mesa, "Mesa 25.3.0-devel") == 0 && r.driverId == 18);
    ico_gpu_driver_seen_text(&r, t, sizeof(t));
    CHECK(strcmp(t, "Turnip, Mesa 25.3.0-devel, Vulkan 1.4") == 0);
    /* the system driver's file is another file */
    CHECK(ico_gpu_driver_seen_read(s_root, NULL, &r) == 1 && r.driverId == 8);
    /* a Turnip that names no Mesa version shows its number */
    w.mesa[0] = '\0';
    ico_gpu_driver_seen_text(&w, t, sizeof(t));
    CHECK(strcmp(t, "Turnip, 25.3.0, Vulkan 1.4") == 0);
    /* any other driver: name, version, Vulkan, id */
    memset(&w, 0, sizeof(w));
    snprintf(w.name, sizeof(w.name), "Mali-G610");
    snprintf(w.version, sizeof(w.version), "44.0.1");
    w.driverId = 12;
    w.vulkanMajor = 1;
    w.vulkanMinor = 1;
    w.known = 1;
    ico_gpu_driver_seen_text(&w, t, sizeof(t));
    CHECK(strcmp(t, "Mali-G610, 44.0.1, Vulkan 1.1, driver ID 12") == 0);
    /* not known: empty; a short buffer is cut and terminated */
    memset(&r, 0, sizeof(r));
    ico_gpu_driver_seen_text(&r, t, sizeof(t));
    CHECK(t[0] == '\0');
    ico_gpu_driver_seen_text(&w, t, 10);
    CHECK(strlen(t) < 10);
    /* quotes and backslashes in a name survive; a bad folder is refused;
       a damaged file and a file without a name read as not seen */
    memset(&w, 0, sizeof(w));
    snprintf(w.name, sizeof(w.name), "odd \"name\" \\ here");
    w.known = 1;
    CHECK(ico_gpu_driver_seen_write(s_root, NULL, &w) == 0);
    CHECK(ico_gpu_driver_seen_read(s_root, NULL, &r) == 1 && strcmp(r.name, w.name) == 0);
    CHECK(ico_gpu_driver_seen_write(s_root, "../x", &w) != 0);
    CHECK(ico_gpu_driver_seen_write(s_root, "last_seen_system.json", &w) != 0);
    write_file(s_root, "last_seen_system.json", "{not json");
    CHECK(ico_gpu_driver_seen_read(s_root, NULL, &r) == 0 && !r.known);
    write_file(s_root, "last_seen_system.json", "{\"version\": \"1\"}");
    CHECK(ico_gpu_driver_seen_read(s_root, NULL, &r) == 0);
    /* neither file lists as a driver, and a folder named like the system
       file cannot be installed */
    {
        IcoGpuDriver d[8];
        const int n = ico_gpu_driver_list(s_root, d, 8);
        for (int i = 0; i < n; i++) {
            CHECK(strcmp(d[i].folder, "last_seen_system.json") != 0);
        }
        CHECK(!ico_gpu_driver_folder_ok("last_seen_system.json"));
    }
}

/* --- the loss window ----------------------------------------------------------- */

static void test_loss(void)
{
    /* a loss under a package within 30 s of the first present counts */
    CHECK(ico_gpu_driver_loss_counts(1, 0));
    CHECK(ico_gpu_driver_loss_counts(1, 12000));
    CHECK(ico_gpu_driver_loss_counts(1, ICO_GPU_DRIVER_LOSS_WINDOW_MS - 1));
    CHECK(!ico_gpu_driver_loss_counts(1, ICO_GPU_DRIVER_LOSS_WINDOW_MS));
    CHECK(!ico_gpu_driver_loss_counts(1, 600000));
    /* the phone's own driver: never */
    CHECK(!ico_gpu_driver_loss_counts(0, 0));
    CHECK(!ico_gpu_driver_loss_counts(0, 5000));
    CHECK(ICO_GPU_DRIVER_LOSS_WINDOW_MS == 30000ull);
    /* what the start does with it: the marker written again after the
       300-present trial removed it makes the next start see a crash */
    ico_gpu_driver_trial_ok(s_root);
    CHECK(!ico_gpu_driver_trial_crashed(s_root, "Turnip-24.1"));
    if (ico_gpu_driver_loss_counts(1, 12000)) {
        CHECK(ico_gpu_driver_trial_begin(s_root, "Turnip-24.1") == 0);
    }
    CHECK(ico_gpu_driver_trial_crashed(s_root, "Turnip-24.1"));
    ico_gpu_driver_trial_ok(s_root);
}

/* --- cleanup ----------------------------------------------------------------- */

static int rm_cb(const char *path, const char *name, void *user)
{
    (void)name;
    (void)user;
    ico_remove(path);
    return 0;
}

static void rm_tree(const char *dir)
{
    /* the files two levels down (work/root/<folder>/<file>), then the
       folders the test makes */
    static const char *const folders[] = {
        "root/Turnip-24.1", "root/Adreno_615-0.615.0", "root/Empty", "root/Bad",
        "root/NoLib",       "root/Half.tmp",           "root",       ""};
    char p[1200];

    ico_dir_walk(dir, 2, rm_cb, NULL);
    for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); i++) {
        snprintf(p, sizeof(p), "%s%s%s", dir, folders[i][0] ? "/" : "", folders[i]);
        ico_rmdir(p);
    }
}

int main(int argc, char **argv)
{
    const char *base = argc > 1 ? argv[1] : ".";

    snprintf(s_work, sizeof(s_work), "%s/gpu_driver_work", base); /* not the executable's name */
    rm_tree(s_work);
    ico_mkdir(s_work);
    snprintf(s_root, sizeof(s_root), "%s/root", s_work);
    /* the root is made by the first install */

    test_meta();
    test_install();
    test_list();
    test_list_zips();
    test_trial();
    test_pages();
    test_seen();
    test_loss();

    if (failures == 0) {
        rm_tree(s_work);
        printf("gpu_driver_test: all passed\n");
        return 0;
    }
    printf("gpu_driver_test: %d failure(s)\n", failures);
    return 1;
}
