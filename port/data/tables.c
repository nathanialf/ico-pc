/*
 * port/data/tables.c
 *
 * The runtime loader of the 73 data tables: tables.h says what it does.
 * Every fact about a table (its EE range, record type, fields, CRC) comes
 * from the generated
 * port/data/gen/table_desc.c; nothing here knows a table by name.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tables.h"
#include "port/data/gen/table_desc.h"
#include "../include/ico_endian.h"

#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "the table loader copies the EE's little-endian words as they are"
#endif

static uint32_t loaded_rows, loaded_records;

static const char *const section_names[] = {".data", ".rodata", ".sdata"};

/* The CRC-32 (zlib's) of a byte range. */
static uint32_t ico_tables_crc32(const uint8_t *p, size_t n)
{
    static uint32_t table[256];
    uint32_t c;
    size_t i;

    if (table[1] == 0) {
        for (i = 0; i < 256; i++) {
            int k;

            c = (uint32_t)i;
            for (k = 0; k < 8; k++) {
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
    }
    c = 0xFFFFFFFFu;
    for (i = 0; i < n; i++) {
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

/* A section of the ELF: its address, and its bytes in the image. */
typedef struct Section {
    uint32_t addr, size;
    const uint8_t *bytes;
} Section;

static int find_sections(const uint8_t *elf, size_t size, Section out[3], char *err, size_t errsz)
{
    uint32_t shoff, shentsize, shnum, shstrndx, stroff, strsize, i;
    int k;

    if (size < 52 || memcmp(elf, "\177ELF\001\001", 6) != 0 || ico_le16(elf + 0x12) != 8) {
        snprintf(err, errsz, "tables: %s is not a 32-bit little-endian MIPS ELF",
                 ICO_TABLES_BOOT_ELF);
        return -1;
    }
    shoff = ico_le32(elf + 0x20);
    shentsize = ico_le16(elf + 0x2E);
    shnum = ico_le16(elf + 0x30);
    shstrndx = ico_le16(elf + 0x32);
    if (shentsize < 40 || shstrndx >= shnum || shoff > size ||
        (uint64_t)shnum * shentsize > size - shoff) {
        snprintf(err, errsz, "tables: %s has no readable section headers", ICO_TABLES_BOOT_ELF);
        return -1;
    }
    stroff = ico_le32(elf + shoff + shstrndx * shentsize + 0x10);
    strsize = ico_le32(elf + shoff + shstrndx * shentsize + 0x14);
    if (stroff > size || strsize > size - stroff) {
        snprintf(err, errsz, "tables: %s's section names lie outside it", ICO_TABLES_BOOT_ELF);
        return -1;
    }
    memset(out, 0, 3 * sizeof(*out));
    for (i = 0; i < shnum; i++) {
        const uint8_t *sh = elf + shoff + i * shentsize;
        uint32_t name = ico_le32(sh);
        uint32_t off = ico_le32(sh + 0x10);
        uint32_t sz = ico_le32(sh + 0x14);

        if (name >= strsize) {
            continue;
        }
        for (k = 0; k < 3; k++) {
            size_t len = strlen(section_names[k]);

            if (name + len < strsize &&
                memcmp(elf + stroff + name, section_names[k], len + 1) == 0) {
                if (ico_le32(sh + 4) != 1 /* SHT_PROGBITS */ || off > size || sz > size - off) {
                    snprintf(err, errsz, "tables: %s's %s section is not readable",
                             ICO_TABLES_BOOT_ELF, section_names[k]);
                    return -1;
                }
                out[k].addr = ico_le32(sh + 0x0C);
                out[k].size = sz;
                out[k].bytes = elf + off;
            }
        }
    }
    for (k = 0; k < 3; k++) {
        if (out[k].bytes == NULL) {
            snprintf(err, errsz, "tables: %s has no %s section", ICO_TABLES_BOOT_ELF,
                     section_names[k]);
            return -1;
        }
    }
    return 0;
}

/* The row's bytes in the ELF, or NULL when its range is outside its section. */
static const uint8_t *row_bytes(const IcoTableRow *row, const Section *secs)
{
    const Section *s = &secs[row->section];

    if (row->ee_lo < s->addr || row->ee_hi > s->addr + s->size || row->ee_lo >= row->ee_hi) {
        return NULL;
    }
    return s->bytes + (row->ee_lo - s->addr);
}

static const IcoEeFunc *find_func(uint32_t addr)
{
    uint32_t lo = 0, hi = ico_ee_func_count;

    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;

        if (ico_ee_funcs[mid].addr == addr) {
            return &ico_ee_funcs[mid];
        }
        if (ico_ee_funcs[mid].addr < addr) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

static const IcoEeObject *find_object(uint32_t addr)
{
    uint32_t lo = 0, hi = ico_ee_object_count;

    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;

        if (ico_ee_objects[mid].addr == addr) {
            return &ico_ee_objects[mid];
        }
        if (ico_ee_objects[mid].addr < addr) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

/* An object pointer word: the member's string pool first, then the registry. */
static void *resolve_data(const IcoTableRow *row, uint32_t v)
{
    const IcoTableRow *p;
    const IcoEeObject *o;

    for (p = ico_table_rows; p < ico_table_rows + ico_table_row_count; p++) {
        if (p->record == NULL && strcmp(p->member, row->member) == 0 && v >= p->ee_lo &&
            v < p->ee_hi) {
            return (char *)p->host + (v - p->ee_lo);
        }
    }
    o = find_object(v);
    return o != NULL ? o->obj : NULL;
}

static int decode_record(const IcoTableRow *row, uint32_t index, const uint8_t *ee, char *host,
                         char *err, size_t errsz)
{
    const IcoTableRecord *rec = row->record;
    uint32_t i;

    for (i = 0; i < rec->field_count; i++) {
        const IcoTableField *f = &rec->fields[i];
        const uint8_t *src = ee + f->ee_offset;
        char *dst = host + f->host_offset;
        uint32_t v;

        switch (f->kind) {
        case ICO_TF_INT:
        case ICO_TF_FLOAT:
        case ICO_TF_BYTES:
            /* the same width on both sides (table_desc.c asserts it) */
            memcpy(dst, src, (size_t)f->ee_size * f->count);
            break;
        case ICO_TF_BITS: {
            uint32_t unit = f->ee_size == 4   ? ico_le32(src)
                            : f->ee_size == 2 ? ico_le16(src)
                                              : src[0];
            uint32_t mask = f->width >= 32 ? 0xFFFFFFFFu : (1u << f->width) - 1;

            v = unit >> f->bit & mask;
            if (f->is_signed && f->width < 32 && (v >> (f->width - 1) & 1)) {
                v |= ~mask;
            }
            f->set(host, v);
            break;
        }
        case ICO_TF_FUNC: {
            void (*fn)(void) = NULL;

            v = ico_le32(src);
            if (v != 0) {
                const IcoEeFunc *e = find_func(v);

                if (e == NULL) {
                    snprintf(
                        err, errsz,
                        "tables: %s %s[%u].%s: no function is known at 0x%08X (the word at "
                        "0x%08X)",
                        row->member, row->symbol, (unsigned)index, f->path, (unsigned)v,
                        (unsigned)(row->ee_lo + row->start + index * rec->ee_size + f->ee_offset));
                    return -1;
                }
                fn = e->fn;
            }
            memcpy(dst, &fn, sizeof(fn));
            break;
        }
        case ICO_TF_DATA: {
            void *p = NULL;

            v = ico_le32(src);
            if (v != 0) {
                p = resolve_data(row, v);
                if (p == NULL) {
                    snprintf(
                        err, errsz,
                        "tables: %s %s[%u].%s: no object is known at 0x%08X (the word at "
                        "0x%08X)",
                        row->member, row->symbol, (unsigned)index, f->path, (unsigned)v,
                        (unsigned)(row->ee_lo + row->start + index * rec->ee_size + f->ee_offset));
                    return -1;
                }
            }
            memcpy(dst, &p, sizeof(p));
            break;
        }
        default:
            snprintf(err, errsz, "tables: %s %s: field %s has an unknown kind %u", row->member,
                     row->symbol, f->path, (unsigned)f->kind);
            return -1;
        }
    }
    return 0;
}

int ico_tables_load_elf(const uint8_t *elf, size_t size, char *err, size_t errsz)
{
    Section secs[3];
    uint32_t r, i, records = 0;

    if (find_sections(elf, size, secs, err, errsz) != 0) {
        return -1;
    }
    /* every row checked before any is written */
    for (r = 0; r < ico_table_row_count; r++) {
        const IcoTableRow *row = &ico_table_rows[r];
        const uint8_t *b = row_bytes(row, secs);
        uint32_t crc;

        if (b == NULL) {
            snprintf(err, errsz, "tables: %s's %s range 0x%08X..0x%08X is not in %s's %s",
                     row->member, section_names[row->section], (unsigned)row->ee_lo,
                     (unsigned)row->ee_hi, ICO_TABLES_BOOT_ELF, section_names[row->section]);
            return -1;
        }
        crc = ico_tables_crc32(b, row->ee_hi - row->ee_lo);
        if (crc != row->crc32) {
            snprintf(err, errsz,
                     "tables: %s (%s 0x%08X..0x%08X) has CRC-32 %08X, expected %08X: %s is not "
                     "the PAL retail boot ELF (SCES-50760)",
                     row->member, section_names[row->section], (unsigned)row->ee_lo,
                     (unsigned)row->ee_hi, (unsigned)crc, (unsigned)row->crc32,
                     ICO_TABLES_BOOT_ELF);
            return -1;
        }
        if (row->record != NULL &&
            (uint64_t)row->start + (uint64_t)row->count * row->record->ee_size >
                row->ee_hi - row->ee_lo) {
            snprintf(err, errsz, "tables: %s: %u records do not fit its range", row->member,
                     (unsigned)row->count);
            return -1;
        }
    }
    /* the pools first: the pointer words resolve into them */
    for (r = 0; r < ico_table_row_count; r++) {
        const IcoTableRow *row = &ico_table_rows[r];

        if (row->record == NULL) {
            memcpy(row->host, row_bytes(row, secs), row->ee_hi - row->ee_lo);
        }
    }
    for (r = 0; r < ico_table_row_count; r++) {
        const IcoTableRow *row = &ico_table_rows[r];
        const uint8_t *b = row_bytes(row, secs);

        if (row->record == NULL) {
            continue;
        }
        for (i = 0; i < row->count; i++) {
            if (decode_record(row, i, b + row->start + i * row->record->ee_size,
                              (char *)row->host + (size_t)i * row->record->host_size, err,
                              errsz) != 0) {
                return -1;
            }
        }
        records += row->count;
    }
    loaded_rows = ico_table_row_count;
    loaded_records = records;
    return 0;
}

int ico_tables_load_vfs(IcoVfs *vfs, char *err, size_t errsz)
{
    IcoVfsFile f;
    uint8_t *buf;
    uint32_t size;
    int rc;

    if (vfs == NULL) {
        snprintf(err, errsz, "tables: no disc to read %s from", ICO_TABLES_BOOT_ELF);
        return -1;
    }
    if (ico_vfs_open(vfs, ICO_TABLES_BOOT_ELF, &f) != 0) {
        snprintf(err, errsz, "tables: the disc has no %s", ICO_TABLES_BOOT_ELF);
        return -1;
    }
    size = ico_vfs_size(&f);
    buf = malloc(size ? size : 1);
    if (buf == NULL) {
        snprintf(err, errsz, "tables: out of memory reading %s", ICO_TABLES_BOOT_ELF);
        return -1;
    }
    if (ico_vfs_read(&f, 0, buf, size) != (int64_t)size) {
        snprintf(err, errsz, "tables: cannot read %s from the disc", ICO_TABLES_BOOT_ELF);
        free(buf);
        return -1;
    }
    rc = ico_tables_load_elf(buf, size, err, errsz);
    free(buf);
    return rc;
}

uint32_t ico_tables_loaded_rows(void)
{
    return loaded_rows;
}

uint32_t ico_tables_loaded_records(void)
{
    return loaded_records;
}
