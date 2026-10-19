/*
 * port/data/tables.c
 *
 * The runtime loader of the 73 data tables: tables.h says what it does.
 * Every fact about a table (its EE range, record type, fields, CRC) comes
 * from a generated descriptor set, port/data/gen/<slug>/table_desc.c, one per
 * disc of config/regions.txt; nothing here knows a table or a disc by name.
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
static uint32_t loaded_count[ICO_TID_COUNT];
static const IcoTableSet *loaded_set;

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

static int find_sections(const IcoTableSet *set, const uint8_t *elf, size_t size, Section out[3],
                         char *err, size_t errsz)
{
    uint32_t shoff, shentsize, shnum, shstrndx, stroff, strsize, i;
    int k;

    if (size < 52 || memcmp(elf, "\177ELF\001\001", 6) != 0 || ico_le16(elf + 0x12) != 8) {
        snprintf(err, errsz, "tables: %s is not a 32-bit little-endian MIPS ELF", set->boot_elf);
        return -1;
    }
    shoff = ico_le32(elf + 0x20);
    shentsize = ico_le16(elf + 0x2E);
    shnum = ico_le16(elf + 0x30);
    shstrndx = ico_le16(elf + 0x32);
    if (shentsize < 40 || shstrndx >= shnum || shoff > size ||
        (uint64_t)shnum * shentsize > size - shoff) {
        snprintf(err, errsz, "tables: %s has no readable section headers", set->boot_elf);
        return -1;
    }
    stroff = ico_le32(elf + shoff + shstrndx * shentsize + 0x10);
    strsize = ico_le32(elf + shoff + shstrndx * shentsize + 0x14);
    if (stroff > size || strsize > size - stroff) {
        snprintf(err, errsz, "tables: %s's section names lie outside it", set->boot_elf);
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
                    snprintf(err, errsz, "tables: %s's %s section is not readable", set->boot_elf,
                             section_names[k]);
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
            snprintf(err, errsz, "tables: %s has no %s section", set->boot_elf, section_names[k]);
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

static const IcoEeFunc *find_func(const IcoTableSet *set, uint32_t addr)
{
    uint32_t lo = 0, hi = set->func_count;

    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;

        if (set->funcs[mid].addr == addr) {
            return &set->funcs[mid];
        }
        if (set->funcs[mid].addr < addr) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

static const IcoEeObject *find_object(const IcoTableSet *set, uint32_t addr)
{
    uint32_t lo = 0, hi = set->object_count;

    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;

        if (set->objects[mid].addr == addr) {
            return &set->objects[mid];
        }
        if (set->objects[mid].addr < addr) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

/* An object pointer word: the member's string pool first, then the registry. */
static void *resolve_data(const IcoTableSet *set, const IcoTableRow *row, uint32_t v)
{
    const IcoTableRow *p;
    const IcoEeObject *o;

    for (p = set->rows; p < set->rows + set->row_count; p++) {
        if (p->record == NULL && strcmp(p->member, row->member) == 0 && v >= p->ee_lo &&
            v < p->ee_hi) {
            return (char *)p->host + (v - p->ee_lo);
        }
    }
    o = find_object(set, v);
    return o != NULL ? o->obj : NULL;
}

static int decode_record(const IcoTableSet *set, const IcoTableRow *row, uint32_t index,
                         const uint8_t *ee, char *host, char *err, size_t errsz)
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
                const IcoEeFunc *e = find_func(set, v);

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
                p = resolve_data(set, row, v);
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
        case ICO_TF_BITMAP: {
            /* host bit i from bit map[i] of the disc's ee_size bytes (another
               disc's bit words keep their bits elsewhere) */
            uint32_t word = 0, b;

            for (b = 0; b < 32; b++) {
                uint32_t k = f->map[b];

                if (k != 0xFF && k < 8u * f->ee_size && (src[k >> 3] >> (k & 7) & 1)) {
                    word |= 1u << b;
                }
            }
            memcpy(dst, &word, sizeof(word));
            break;
        }
        case ICO_TF_DEFAULT:
            /* a field this disc's record lacks: its declared value (the low
               host_size bytes, little-endian as the host) */
            memcpy(dst, &f->value, f->host_size);
            break;
        default:
            snprintf(err, errsz, "tables: %s %s: field %s has an unknown kind %u", row->member,
                     row->symbol, f->path, (unsigned)f->kind);
            return -1;
        }
    }
    return 0;
}

/* The load's header line for a refused ELF: which disc this set reads. */
static void wrong_disc(const IcoTableSet *set, char *err, size_t errsz, const char *what)
{
    snprintf(err, errsz, "tables: %s: the ELF is not the boot file of %s (%s, %s)", what,
             set->product, set->boot_elf, set->disc_id);
}

int ico_tables_load_elf(const IcoTableSet *set, const uint8_t *elf, size_t size, char *err,
                        size_t errsz)
{
    Section secs[3];
    uint32_t r, i, records = 0;

    if (set == NULL) {
        snprintf(err, errsz, "tables: no descriptor set to load with");
        return -1;
    }
    if (find_sections(set, elf, size, secs, err, errsz) != 0) {
        return -1;
    }
    /* every row checked before any is written */
    for (r = 0; r < set->row_count; r++) {
        const IcoTableRow *row = &set->rows[r];
        const uint8_t *b = row_bytes(row, secs);
        uint32_t crc;

        if (b == NULL) {
            char what[160];

            snprintf(what, sizeof(what), "%s's %s range 0x%08X..0x%08X is outside its %s",
                     row->member, section_names[row->section], (unsigned)row->ee_lo,
                     (unsigned)row->ee_hi, section_names[row->section]);
            wrong_disc(set, err, errsz, what);
            return -1;
        }
        crc = ico_tables_crc32(b, row->ee_hi - row->ee_lo);
        if (crc != row->crc32) {
            snprintf(err, errsz,
                     "tables: %s (%s 0x%08X..0x%08X) has CRC-32 %08X, expected %08X: the ELF is "
                     "not the boot file of %s (%s, %s)",
                     row->member, section_names[row->section], (unsigned)row->ee_lo,
                     (unsigned)row->ee_hi, (unsigned)crc, (unsigned)row->crc32, set->product,
                     set->boot_elf, set->disc_id);
            return -1;
        }
        if (row->record != NULL &&
            ((uint64_t)row->start + (uint64_t)row->count * row->record->ee_size >
                 row->ee_hi - row->ee_lo ||
             row->count > row->host_count)) {
            snprintf(err, errsz, "tables: %s: %u records do not fit its range", row->member,
                     (unsigned)row->count);
            return -1;
        }
        if (row->record == NULL && row->ee_hi - row->ee_lo > row->host_count) {
            snprintf(err, errsz, "tables: %s: its %u-byte pool does not fit the host's %u",
                     row->member, (unsigned)(row->ee_hi - row->ee_lo), (unsigned)row->host_count);
            return -1;
        }
    }
    /* the pools first: the pointer words resolve into them */
    for (r = 0; r < set->row_count; r++) {
        const IcoTableRow *row = &set->rows[r];

        if (row->record == NULL) {
            uint32_t n = row->ee_hi - row->ee_lo;

            memcpy(row->host, row_bytes(row, secs), n);
            memset((char *)row->host + n, 0, row->host_count - n);
        }
    }
    memset(loaded_count, 0, sizeof(loaded_count));
    for (r = 0; r < set->row_count; r++) {
        const IcoTableRow *row = &set->rows[r];
        const uint8_t *b = row_bytes(row, secs);

        if (row->record == NULL) {
            continue;
        }
        for (i = 0; i < row->count; i++) {
            if (decode_record(set, row, i, b + row->start + i * row->record->ee_size,
                              (char *)row->host + (size_t)i * row->record->host_size, err,
                              errsz) != 0) {
                loaded_set = NULL;
                return -1;
            }
        }
        /* a disc with fewer records than the host array holds: the rest
           are zero, whatever an earlier load left */
        memset((char *)row->host + (size_t)row->count * row->record->host_size, 0,
               (size_t)(row->host_count - row->count) * row->record->host_size);
        if (row->id < ICO_TID_COUNT) {
            loaded_count[row->id] = row->count;
        }
        records += row->count;
    }
    loaded_rows = set->row_count;
    loaded_records = records;
    loaded_set = set;
    return 0;
}

int ico_tables_load_vfs(IcoVfs *vfs, const IcoTableSet *set, char *err, size_t errsz)
{
    IcoVfsFile f;
    uint8_t *buf;
    uint32_t size;
    int rc;

    if (set == NULL) {
        snprintf(err, errsz, "tables: no descriptor set to load with");
        return -1;
    }
    if (vfs == NULL) {
        snprintf(err, errsz, "tables: no disc to read %s from", set->boot_elf);
        return -1;
    }
    if (ico_vfs_open(vfs, set->boot_elf, &f) != 0) {
        snprintf(err, errsz, "tables: the disc has no %s, so it is not %s (%s)", set->boot_elf,
                 set->product, set->disc_id);
        return -1;
    }
    size = ico_vfs_size(&f);
    buf = malloc(size ? size : 1);
    if (buf == NULL) {
        snprintf(err, errsz, "tables: out of memory reading %s", set->boot_elf);
        return -1;
    }
    if (ico_vfs_read(&f, 0, buf, size) != (int64_t)size) {
        snprintf(err, errsz, "tables: cannot read %s from the disc", set->boot_elf);
        free(buf);
        return -1;
    }
    rc = ico_tables_load_elf(set, buf, size, err, errsz);
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

uint32_t ico_tables_count(IcoTableId id)
{
    return (unsigned)id < ICO_TID_COUNT ? loaded_count[id] : 0;
}

const IcoTableSet *ico_tables_loaded_set(void)
{
    return loaded_set;
}
