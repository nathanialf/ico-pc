/*
 * port/platform/test/gpu_driver_test.c
 *
 * The Android build's graphics driver packages (port/platform/gpu_driver.c,
 * v0.4.3 package AN-22a), on any platform: meta.json read as the community
 * packages write it; bad library names, a missing name and a too high
 * minApi refused; zips made here with miniz's writer installed, and the
 * unsafe ones (a path in a name, "..", a backslash, a directory, a file
 * over the size limit, no meta.json, the library missing) refused with
 * nothing left behind; the listing sorted with broken folders left out;
 * remove; the trial marker's states.
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
    snprintf(out, n, "%s/%s", dir, name);
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

    snprintf(s_work, sizeof(s_work), "%s/gpu_driver_test", base);
    rm_tree(s_work);
    ico_mkdir(s_work);
    snprintf(s_root, sizeof(s_root), "%s/root", s_work);
    /* the root is made by the first install */

    test_meta();
    test_install();
    test_list();
    test_trial();

    if (failures == 0) {
        rm_tree(s_work);
        printf("gpu_driver_test: all passed\n");
        return 0;
    }
    printf("gpu_driver_test: %d failure(s)\n", failures);
    return 1;
}
