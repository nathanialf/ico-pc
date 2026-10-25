/*
 * port/data/test/tables_test.c
 *
 * The runtime table loader (port/data/tables.c) against the PS2 build's
 * tables: tools/gen_data_c.py writes the 73 members as C from the user's
 * base ELF and this executable compiles them under renamed symbols
 * (ico_ref_<name>, port/data/CMakeLists.txt). The loader fills the host
 * tables from the boot ELF; every byte of every table must equal the
 * compiled one. Pointer fields compare by address: both sides resolve to the
 * same symbols (ee_symbols.c's stubs here). staffRollNameData's char
 * pointers, which the compiled C points at string literals and the loader
 * at its copy of the member's string pool, compare by the strings.
 *
 * Then the manifest check: an ELF with one byte of one table changed is
 * refused, naming that table, and the tables are left as they were.
 *
 *   tables_test BASE_ELF [DISC_IMAGE]
 *   tables_test --set SLUG BASE_ELF [DISC_IMAGE] [--other OTHER_ELF]
 *
 * With DISC_IMAGE the loader reads the set's boot ELF (SCES_507.60) through
 * the VFS, as ico_pc does; without it (or when the image is absent) it reads
 * BASE_ELF. --set SLUG (jpn, kor, ntsc, aug6, jun26, cn) loads that disc's descriptor set and
 * compares with its reference (ico_ref_<slug>_<name>: gen_data_c.py --version
 * <slug>, the disc's bytes in the PAL record's form by
 * config/ee_layout.<slug>.txt, with the defaults and the permuted bit words),
 * each table's records past the disc's count zero; then OTHER_ELF (the PAL
 * ELF) is refused by the disc's set and BASE_ELF by the PAL set, each naming
 * the disc it expects. A table the set has no row for (ico_ref[_<slug>]_absent,
 * port/data/CMakeLists.txt: the hint table on the USA disc, the June demo's
 * enemy-set tables elsewhere) must stay zero on the host.
 * It exits 77 (skipped) when BASE_ELF is absent or the reference was not
 * built.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tables.h"
#include "port/data/gen/table_desc.h"

#define X(name) extern char name[], ico_ref_##name[];
ICO_TABLE_SYMBOLS(X)
#undef X
extern const char *const ico_ref_absent[];

typedef struct Sym {
    const char *name;
    char *host;
    const char *ref;
} Sym;

#define ICO_SYM_COUNT (sizeof(syms_pal) / sizeof(syms_pal[0]))

static const Sym syms_pal[] = {
#define X(name) {#name, name, ico_ref_##name},
    ICO_TABLE_SYMBOLS(X)
#undef X
};

/* The other discs' references, each built when its ELF was present at
   configure (port/data/CMakeLists.txt defines ICO_TABLES_TEST_<SLUG>). */
#ifdef ICO_TABLES_TEST_JPN
#define X(name) extern char ico_ref_jpn_##name[];
ICO_TABLE_SYMBOLS(X)
#undef X
extern const char *const ico_ref_jpn_absent[];

static const Sym syms_jpn[] = {
#define X(name) {#name, name, ico_ref_jpn_##name},
    ICO_TABLE_SYMBOLS(X)
#undef X
};
#endif

#ifdef ICO_TABLES_TEST_KOR
#define X(name) extern char ico_ref_kor_##name[];
ICO_TABLE_SYMBOLS(X)
#undef X
extern const char *const ico_ref_kor_absent[];

static const Sym syms_kor[] = {
#define X(name) {#name, name, ico_ref_kor_##name},
    ICO_TABLE_SYMBOLS(X)
#undef X
};
#endif

#ifdef ICO_TABLES_TEST_NTSC
#define X(name) extern char ico_ref_ntsc_##name[];
ICO_TABLE_SYMBOLS(X)
#undef X
extern const char *const ico_ref_ntsc_absent[];

static const Sym syms_ntsc[] = {
#define X(name) {#name, name, ico_ref_ntsc_##name},
    ICO_TABLE_SYMBOLS(X)
#undef X
};
#endif

#ifdef ICO_TABLES_TEST_AUG6
#define X(name) extern char ico_ref_aug6_##name[];
ICO_TABLE_SYMBOLS(X)
#undef X
extern const char *const ico_ref_aug6_absent[];

static const Sym syms_aug6[] = {
#define X(name) {#name, name, ico_ref_aug6_##name},
    ICO_TABLE_SYMBOLS(X)
#undef X
};
#endif

#ifdef ICO_TABLES_TEST_JUN26
#define X(name) extern char ico_ref_jun26_##name[];
ICO_TABLE_SYMBOLS(X)
#undef X
extern const char *const ico_ref_jun26_absent[];

static const Sym syms_jun26[] = {
#define X(name) {#name, name, ico_ref_jun26_##name},
    ICO_TABLE_SYMBOLS(X)
#undef X
};
#endif

#ifdef ICO_TABLES_TEST_CN
#define X(name) extern char ico_ref_cn_##name[];
ICO_TABLE_SYMBOLS(X)
#undef X
extern const char *const ico_ref_cn_absent[];

static const Sym syms_cn[] = {
#define X(name) {#name, name, ico_ref_cn_##name},
    ICO_TABLE_SYMBOLS(X)
#undef X
};
#endif

typedef struct RefSet {
    const char *slug;
    const Sym *syms;
    const char *const *absent; /* the symbols the set has no row for, NULL-terminated */
} RefSet;

static const RefSet ref_sets[] = {
#ifdef ICO_TABLES_TEST_JPN
    {"jpn", syms_jpn, ico_ref_jpn_absent},
#endif
#ifdef ICO_TABLES_TEST_KOR
    {"kor", syms_kor, ico_ref_kor_absent},
#endif
#ifdef ICO_TABLES_TEST_NTSC
    {"ntsc", syms_ntsc, ico_ref_ntsc_absent},
#endif
#ifdef ICO_TABLES_TEST_AUG6
    {"aug6", syms_aug6, ico_ref_aug6_absent},
#endif
#ifdef ICO_TABLES_TEST_JUN26
    {"jun26", syms_jun26, ico_ref_jun26_absent},
#endif
#ifdef ICO_TABLES_TEST_CN
    {"cn", syms_cn, ico_ref_cn_absent},
#endif
    {NULL, NULL, NULL},
};

static int failures;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long n;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n);
    if (buf == NULL || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

static const IcoTableRow *row_of(const IcoTableSet *set, const char *symbol)
{
    uint32_t i;

    for (i = 0; i < set->row_count; i++) {
        if (set->rows[i].symbol != NULL && strcmp(set->rows[i].symbol, symbol) == 0) {
            return &set->rows[i];
        }
    }
    return NULL;
}

/* The host bytes from `from` to the array's end are zero (the records a disc
   with fewer than the host array holds leaves empty). */
static int zero_tail(const IcoTableRow *row, const char *host, size_t from)
{
    size_t k, end = (size_t)row->host_count * row->record->host_size;

    for (k = from; k < end; k++) {
        if (host[k] != 0) {
            return 0;
        }
    }
    return 1;
}

/* The reference's twin of a host pointer into a host table (the June 2001
   demo's enemy sets point into each other's tables): the same offset in that
   table's reference; NULL when it points into none (a registry object or a
   string pool: the same address on both sides). */
static const char *ref_of(const IcoTableSet *set, const Sym *syms, const char *p)
{
    size_t i;

    for (i = 0; i < ICO_SYM_COUNT; i++) {
        const IcoTableRow *row = row_of(set, syms[i].name);

        if (row != NULL && row->record != NULL && p >= syms[i].host &&
            p < syms[i].host + (size_t)row->host_count * row->record->host_size) {
            return syms[i].ref + (p - syms[i].host);
        }
    }
    return NULL;
}

/* A record table's size bytes against the reference, its pointers into host
   tables compared by their offset there (ref_of). */
static int same_records(const IcoTableSet *set, const Sym *syms, const IcoTableRow *row,
                        const char *host, const char *ref, size_t size)
{
    const IcoTableRecord *rec = row->record;
    char *buf;
    uint32_t k, f;
    int same;

    if (memcmp(host, ref, size) == 0) {
        return 1;
    }
    buf = malloc(size ? size : 1);
    if (buf == NULL) {
        return 0;
    }
    memcpy(buf, host, size);
    for (k = 0; k < row->count; k++) {
        for (f = 0; f < rec->field_count; f++) {
            char *at = buf + (size_t)k * rec->host_size + rec->fields[f].host_offset;
            const char *p, *r;

            if (rec->fields[f].kind != ICO_TF_DATA) {
                continue;
            }
            memcpy(&p, at, sizeof(p));
            r = p != NULL ? ref_of(set, syms, p) : NULL;
            if (r != NULL) {
                memcpy(at, &r, sizeof(r));
            }
        }
    }
    same = memcmp(buf, ref, size) == 0;
    free(buf);
    return same;
}

static int is_absent(const char *const *absent, const char *symbol)
{
    while (absent != NULL && *absent != NULL) {
        if (strcmp(*absent, symbol) == 0) {
            return 1;
        }
        absent++;
    }
    return 0;
}

/* The host array's bytes of a symbol this set has no row for: another set's
   row names the same host array. 0 when no set has one. */
static size_t host_size_any(const char *symbol)
{
    int k;

    for (k = 0; k < ICO_TABLE_SET_COUNT; k++) {
        const IcoTableRow *row = row_of(ico_table_sets[k], symbol);

        if (row != NULL && row->record != NULL) {
            return (size_t)row->host_count * row->record->host_size;
        }
    }
    return 0;
}

/* Every table of the set against its compiled reference; the number that
   differ. A symbol the set has no row for (absent) has no reference: its host
   table must be zero. */
static int compare_all(const IcoTableSet *set, const Sym *syms, const char *const *absent,
                       int verbose)
{
    size_t i;
    int bad = 0;

    for (i = 0; i < ICO_SYM_COUNT; i++) {
        const IcoTableRow *row = row_of(set, syms[i].name);
        size_t size;
        int same;

        if (is_absent(absent, syms[i].name)) {
            size_t k;

            size = host_size_any(syms[i].name);
            same = 1;
            for (k = 0; k < size; k++) {
                if (syms[i].host[k] != 0) {
                    same = 0;
                    break;
                }
            }
            if (verbose) {
                printf("%s %-26s %8zu bytes (no row in this set: zero)\n", same ? "    " : "DIFF",
                       syms[i].name, size);
            }
            bad += !same;
            continue;
        }
        if (row == NULL) {
            /* staffRollNameDataNum: a derived int, no row of its own */
            same = memcmp(syms[i].host, syms[i].ref, sizeof(int)) == 0;
            size = sizeof(int);
        } else if (strcmp(row->record->type, "char*") == 0) {
            char *const *h = (char *const *)(void *)syms[i].host;
            char *const *r = (char *const *)(const void *)syms[i].ref;
            uint32_t k;

            size = (size_t)row->count * row->record->host_size;
            same = 1;
            for (k = 0; k < row->count; k++) {
                if ((h[k] == NULL) != (r[k] == NULL) || (h[k] != NULL && strcmp(h[k], r[k]) != 0)) {
                    if (verbose) {
                        printf("     %s[%u] differs\n", syms[i].name, (unsigned)k);
                    }
                    same = 0;
                    break;
                }
            }
        } else {
            size = (size_t)row->count * row->record->host_size;
            same = same_records(set, syms, row, syms[i].host, syms[i].ref, size);
            if (same && row->count < row->host_count && !zero_tail(row, syms[i].host, size)) {
                if (verbose) {
                    printf("     %s: records %u.. are not zero\n", syms[i].name,
                           (unsigned)row->count);
                }
                same = 0;
            } else if (!same && verbose) {
                size_t k = 0;

                while (syms[i].host[k] == syms[i].ref[k]) {
                    k++;
                }
                printf("     %s: first difference at byte %zu (record %zu, offset %zu of %s)\n",
                       syms[i].name, k, k / row->record->host_size, k % row->record->host_size,
                       row->record->type);
            }
        }
        if (verbose) {
            printf("%s %-26s %8zu bytes\n", same ? "    " : "DIFF", syms[i].name, size);
        }
        bad += !same;
    }
    return bad;
}

/* The loader on a non-PAL set: its reference, its counts, and the refusal of
   each disc's ELF by the other's set. */
static int main_set(const IcoTableSet *set, const char *elf_path, const char *image,
                    const char *other_path)
{
    char err[512];
    uint8_t *elf, *other;
    size_t size, osize;
    IcoVfs *vfs = NULL;
    const Sym *syms = NULL;
    const char *const *absent = NULL;
    uint32_t i;
    int rc;

    for (i = 0; ref_sets[i].slug != NULL; i++) {
        if (strcmp(set->slug, ref_sets[i].slug) == 0) {
            syms = ref_sets[i].syms;
            absent = ref_sets[i].absent;
        }
    }
    if (syms == NULL) {
        printf("tables_test: no %s reference was built (the ELF was absent at configure): "
               "skipped\n",
               set->slug);
        return 77;
    }
    elf = read_file(elf_path, &size);
    if (elf == NULL) {
        printf("tables_test: no %s: skipped\n", elf_path);
        return 77;
    }
    rc = compare_all(set, syms, absent, 0);
    printf("     before loading, %d of %d tables differ from the %s reference\n", rc,
           (int)ICO_SYM_COUNT, set->slug);
    check(rc > 0, "before loading, the host tables are not the reference (the binary holds none)");
    if (image != NULL) {
        FILE *f = fopen(image, "rb");

        if (f != NULL) {
            fclose(f);
            vfs = ico_vfs_mount(&ico_vfs_iso9660, image);
        }
    }
    if (vfs != NULL) {
        rc = ico_tables_load_vfs(vfs, set, err, sizeof(err));
        printf("     loaded %s through the VFS from %s\n", set->boot_elf, image);
    } else {
        rc = ico_tables_load_elf(set, elf, size, err, sizeof(err));
        printf("     loaded %s (no disc image)\n", elf_path);
    }
    if (rc != 0) {
        printf("     %s\n", err);
    }
    check(rc == 0, "the loader accepts the set's retail ELF");
    check(ico_tables_loaded_set() == set, "the loaded set is the one asked for");
    printf("     %u rows, %u records\n", (unsigned)ico_tables_loaded_rows(),
           (unsigned)ico_tables_loaded_records());
    for (i = 0; i < set->row_count; i++) {
        const IcoTableRow *row = &set->rows[i];

        if (row->record != NULL && row->count != row->host_count) {
            printf("     %-24s %5u of the host's %5u records\n", row->symbol,
                   (unsigned)ico_tables_count((IcoTableId)row->id), (unsigned)row->host_count);
        }
    }
    check(compare_all(set, syms, absent, 1) == 0,
          "every table equals the reference in the PAL record's form, the rest zero");

    /* each disc's ELF under the other's set: refused, naming the disc expected */
    other = other_path != NULL ? read_file(other_path, &osize) : NULL;
    if (other != NULL) {
        rc = ico_tables_load_elf(set, other, osize, err, sizeof(err));
        printf("     %s\n", err);
        check(rc != 0 && strstr(err, set->product) != NULL,
              "the other disc's ELF is refused by this set, naming this set's disc");
        check(compare_all(set, syms, absent, 0) == 0,
              "a refused ELF leaves the tables as they were");
        rc = ico_tables_load_elf(ico_table_sets[0], elf, size, err, sizeof(err));
        printf("     %s\n", err);
        check(rc != 0 && strstr(err, ico_table_sets[0]->product) != NULL,
              "this ELF is refused by the PAL set, naming the European disc");
        free(other);
    } else {
        printf("     (no --other ELF: the cross refusals are not checked)\n");
    }
    free(elf);
    if (vfs != NULL) {
        ico_vfs_unmount(vfs);
    }
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    char err[512];
    uint8_t *elf, *bad;
    size_t size;
    const IcoTableRow *row;
    const IcoTableSet *set = &ico_table_set_pal;
    const Sym *syms = syms_pal;
    const char *const *absent = ico_ref_absent;
    IcoVfs *vfs = NULL;
    int rc;

    if (argc >= 4 && strcmp(argv[1], "--set") == 0) {
        const char *other = NULL, *image = NULL;
        int k;

        set = NULL;
        for (k = 0; k < ICO_TABLE_SET_COUNT; k++) {
            if (strcmp(ico_table_sets[k]->slug, argv[2]) == 0) {
                set = ico_table_sets[k];
            }
        }
        if (set == NULL) {
            fprintf(stderr, "tables_test: no descriptor set %s\n", argv[2]);
            return 2;
        }
        for (k = 4; k < argc; k++) {
            if (strcmp(argv[k], "--other") == 0 && k + 1 < argc) {
                other = argv[++k];
            } else {
                image = argv[k];
            }
        }
        return main_set(set, argv[3], image, other);
    }
    if (argc < 2) {
        fprintf(stderr, "usage: tables_test BASE_ELF [DISC_IMAGE]\n"
                        "       tables_test --set SLUG BASE_ELF [DISC_IMAGE] [--other ELF]\n");
        return 2;
    }
    elf = read_file(argv[1], &size);
    if (elf == NULL) {
        fprintf(stderr, "tables_test: cannot read %s\n", argv[1]);
        return 2;
    }
    rc = compare_all(set, syms, absent, 0);
    printf("     before loading, %d of %d tables differ from the reference\n", rc,
           (int)ICO_SYM_COUNT);
    check(rc > 0, "before loading, the host tables are not the reference (the binary holds none)");

    if (argc > 2) {
        FILE *f = fopen(argv[2], "rb");

        if (f != NULL) {
            fclose(f);
            vfs = ico_vfs_mount(&ico_vfs_iso9660, argv[2]);
        }
    }
    if (vfs != NULL) {
        rc = ico_tables_load_vfs(vfs, set, err, sizeof(err));
        printf("     loaded %s through the VFS from %s\n", set->boot_elf, argv[2]);
    } else {
        rc = ico_tables_load_elf(set, elf, size, err, sizeof(err));
        printf("     loaded %s (no disc image)\n", argv[1]);
    }
    if (rc != 0) {
        printf("     %s\n", err);
    }
    check(rc == 0, "the loader accepts the retail ELF");
    printf("     %u rows, %u records\n", (unsigned)ico_tables_loaded_rows(),
           (unsigned)ico_tables_loaded_records());
    check(compare_all(set, syms, absent, 1) == 0,
          "every table equals the PS2 build's compiled table");

    /* one byte of one table changed: refused, nothing written */
    bad = malloc(size);
    memcpy(bad, elf, size);
    row = row_of(set, "seDef");
    {
        /* the ELF's file offset of the row: .rodata's address and offset */
        uint32_t shoff = (uint32_t)bad[0x20] | (uint32_t)bad[0x21] << 8 |
                         (uint32_t)bad[0x22] << 16 | (uint32_t)bad[0x23] << 24;
        unsigned shnum = bad[0x30] | bad[0x31] << 8;
        unsigned i;
        int patched = 0;

        for (i = 0; i < shnum && !patched; i++) {
            const uint8_t *sh = bad + shoff + i * 40;
            uint32_t addr, off, sz;

            memcpy(&addr, sh + 0x0C, 4);
            memcpy(&off, sh + 0x10, 4);
            memcpy(&sz, sh + 0x14, 4);
            if (row->ee_lo >= addr && row->ee_hi <= addr + sz && addr != 0) {
                bad[off + (row->ee_lo - addr) + 40] ^= 0x01;
                patched = 1;
            }
        }
        check(patched, "patched one byte of seDef in a copy of the ELF");
    }
    memset(err, 0, sizeof(err));
    rc = ico_tables_load_elf(set, bad, size, err, sizeof(err));
    printf("     %s\n", err);
    check(rc != 0 && strstr(err, "sedef") != NULL && strstr(err, "CRC-32") != NULL,
          "the manifest CRC refuses the modified ELF, naming the table");
    check(compare_all(set, syms, absent, 0) == 0, "a refused ELF leaves the tables as they were");

    /* not an ELF at all */
    memset(bad, 0, 64);
    rc = ico_tables_load_elf(set, bad, size, err, sizeof(err));
    check(rc != 0, "a file that is not an ELF is refused");

    free(bad);
    free(elf);
    if (vfs != NULL) {
        ico_vfs_unmount(vfs);
    }
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
