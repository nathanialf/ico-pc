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
 *
 * With DISC_IMAGE the loader reads SCES_507.60 through the VFS, as ico_pc
 * does; without it (or when the image is absent) it reads BASE_ELF.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tables.h"
#include "port/data/gen/table_desc.h"

#define X(name) extern char name[], ico_ref_##name[];
ICO_TABLE_SYMBOLS(X)
#undef X

typedef struct Sym {
    const char *name;
    char *host;
    const char *ref;
} Sym;

static const Sym syms[] = {
#define X(name) {#name, name, ico_ref_##name},
    ICO_TABLE_SYMBOLS(X)
#undef X
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

static const IcoTableRow *row_of(const char *symbol)
{
    uint32_t i;

    for (i = 0; i < ico_table_row_count; i++) {
        if (ico_table_rows[i].symbol != NULL && strcmp(ico_table_rows[i].symbol, symbol) == 0) {
            return &ico_table_rows[i];
        }
    }
    return NULL;
}

/* Every table against its compiled reference; the number that differ. */
static int compare_all(int verbose)
{
    size_t i;
    int bad = 0;

    for (i = 0; i < sizeof(syms) / sizeof(syms[0]); i++) {
        const IcoTableRow *row = row_of(syms[i].name);
        size_t size;
        int same;

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
            same = memcmp(syms[i].host, syms[i].ref, size) == 0;
            if (!same && verbose) {
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

int main(int argc, char **argv)
{
    char err[512];
    uint8_t *elf, *bad;
    size_t size;
    const IcoTableRow *row;
    IcoVfs *vfs = NULL;
    int rc;

    if (argc < 2) {
        fprintf(stderr, "usage: tables_test BASE_ELF [DISC_IMAGE]\n");
        return 2;
    }
    elf = read_file(argv[1], &size);
    if (elf == NULL) {
        fprintf(stderr, "tables_test: cannot read %s\n", argv[1]);
        return 2;
    }
    rc = compare_all(0);
    printf("     before loading, %d of %d tables differ from the reference\n", rc,
           (int)(sizeof(syms) / sizeof(syms[0])));
    check(rc > 0, "before loading, the host tables are not the reference (the binary holds none)");

    if (argc > 2) {
        FILE *f = fopen(argv[2], "rb");

        if (f != NULL) {
            fclose(f);
            vfs = ico_vfs_mount(&ico_vfs_iso9660, argv[2]);
        }
    }
    if (vfs != NULL) {
        rc = ico_tables_load_vfs(vfs, err, sizeof(err));
        printf("     loaded %s through the VFS from %s\n", ICO_TABLES_BOOT_ELF, argv[2]);
    } else {
        rc = ico_tables_load_elf(elf, size, err, sizeof(err));
        printf("     loaded %s (no disc image)\n", argv[1]);
    }
    if (rc != 0) {
        printf("     %s\n", err);
    }
    check(rc == 0, "the loader accepts the retail ELF");
    printf("     %u rows, %u records\n", (unsigned)ico_tables_loaded_rows(),
           (unsigned)ico_tables_loaded_records());
    check(compare_all(1) == 0, "every table equals the PS2 build's compiled table");

    /* one byte of one table changed: refused, nothing written */
    bad = malloc(size);
    memcpy(bad, elf, size);
    row = row_of("seDef");
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
    rc = ico_tables_load_elf(bad, size, err, sizeof(err));
    printf("     %s\n", err);
    check(rc != 0 && strstr(err, "sedef") != NULL && strstr(err, "CRC-32") != NULL,
          "the manifest CRC refuses the modified ELF, naming the table");
    check(compare_all(0) == 0, "a refused ELF leaves the tables as they were");

    /* not an ELF at all */
    memset(bad, 0, 64);
    rc = ico_tables_load_elf(bad, size, err, sizeof(err));
    check(rc != 0, "a file that is not an ELF is refused");

    free(bad);
    free(elf);
    if (vfs != NULL) {
        ico_vfs_unmount(vfs);
    }
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
