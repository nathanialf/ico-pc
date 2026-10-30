/*
 * port/platform/state_file.c
 *
 * The save state container (state_file.h): the build id, the header's
 * validity, the writer thread and the reader.
 */
#include "state_file.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "fiber.h"
#include "game_data_range.h"
#include "host_fs.h"
#include "ico_thread.h"
#include "miniz.h"
#include "sched.h"
#include "state_registry.h"
#include "xxh3.h"

/* --- little-endian fields --------------------------------------------------- */

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void put64(unsigned char *p, uint64_t v)
{
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

static uint32_t get32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t get64(const unsigned char *p)
{
    return (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32;
}

/* a text field: at most size - 1 bytes, the rest zero */
static void put_text(unsigned char *p, size_t size, const char *s)
{
    size_t n = s != NULL ? strlen(s) : 0;

    if (n > size - 1) {
        n = size - 1;
    }
    memset(p, 0, size);
    if (n > 0) {
        memcpy(p, s, n);
    }
}

static void get_text(char *out, size_t out_size, const unsigned char *p, size_t size)
{
    size_t n = 0;

    while (n < size && n < out_size - 1 && p[n] != 0) {
        n++;
    }
    memcpy(out, p, n);
    out[n] = '\0';
}

/* The header's byte offsets */
enum {
    H_MAGIC = 0,
    H_VERSION = 8,
    H_HEADER_BYTES = 12,
    H_BUILD_ID = 16,
    H_COMMIT = 32,
    H_PLATFORM = 80,
    H_DISC = 96,
    H_SETTINGS_HASH = 112,
    H_SAVED_AT = 120,
    H_PLAY_FRAMES = 128,
    H_STAGE_NO = 132,
    H_MAIN_TICK = 136,
    H_VSYNC = 140,
    H_SECTIONS = 144,
    H_KEYS_BYTES = 148,
    H_FILE_BYTES = 152,
    H_CRC = 160,
    H_ENTRY_BYTES = 164,
    H_END = 168
};

/* A table entry's byte offsets */
enum {
    T_NAME = 0,
    T_OFFSET = 16,
    T_PACKED = 24,
    T_RAW = 32,
    T_ADDR = 40,
    T_CRC = 48,
    T_FLAGS = 52,
    T_END = 56
};

_Static_assert(H_END <= ICO_STATE_HEADER_BYTES, "the header's fields fit its 512 bytes");
_Static_assert(T_END <= ICO_STATE_TABLE_ENTRY_BYTES, "a table entry's fields fit its 64 bytes");
_Static_assert(sizeof(((IcoStateHeader *)0)->commit) == H_PLATFORM - H_COMMIT, "commit field");
_Static_assert(sizeof(((IcoStateHeader *)0)->platform) == H_DISC - H_PLATFORM, "platform field");
_Static_assert(sizeof(((IcoStateHeader *)0)->disc) == H_SETTINGS_HASH - H_DISC, "disc field");

/* the bytes in front of the first section: header, table, keys text */
static size_t prefix_bytes(int n_sections, size_t keys_bytes)
{
    return ICO_STATE_HEADER_BYTES + (size_t)n_sections * ICO_STATE_TABLE_ENTRY_BYTES + keys_bytes;
}

static size_t keys_len(const IcoStateHeader *h)
{
    size_t n = 0;

    while (n < sizeof h->keys - 1 && h->keys[n] != '\0') {
        n++;
    }
    return n;
}

/* --- the platform and the build id ----------------------------------------- */

const char *ico_state_platform_tag(void)
{
#if defined(_WIN32) && defined(__x86_64__)
    return "windows-x64";
#elif defined(_WIN32) && defined(__aarch64__)
    return "windows-arm64";
#elif defined(__ANDROID__) && defined(__aarch64__)
    return "android-arm64";
#elif defined(__ANDROID__) && defined(__x86_64__)
    return "android-x64";
#elif defined(__APPLE__) && defined(ICO_IOS)
    return "ios-arm64";
#elif defined(__APPLE__) && defined(__aarch64__)
    return "macos-arm64";
#elif defined(__APPLE__) && defined(__x86_64__)
    return "macos-x64";
#elif defined(__linux__) && defined(__x86_64__)
    return "linux-x64";
#elif defined(__linux__) && defined(__aarch64__)
    return "linux-arm64";
#else
    return "unknown";
#endif
}

typedef struct Text {
    char *p;
    size_t size;
    size_t len; /* may exceed size: the length the whole text needs */
} Text;

static void text_add(Text *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void text_add(Text *t, const char *fmt, ...)
{
    va_list ap;
    int w;

    va_start(ap, fmt);
    w = vsnprintf(t->p != NULL && t->len < t->size ? t->p + t->len : NULL,
                  t->p != NULL && t->len < t->size ? t->size - t->len : 0, fmt, ap);
    va_end(ap);
    if (w > 0) {
        t->len += (size_t)w;
    }
}

static void build_text(Text *t, const char *commit)
{
    IcoMemRange r[2];
    const void *sched_p;
    size_t sched_n = 0;
    int i;

    if (ico_game_data_ranges(r) != 0) {
        memset(r, 0, sizeof r);
    }
    ico_sched_state_block(&sched_p, &sched_n);
    text_add(t, "commit %s\n", commit != NULL ? commit : "");
    text_add(t, "compiler %s\n", __VERSION__);
    text_add(t, "platform %s\n", ico_state_platform_tag());
    text_add(t, "format %d\n", ICO_STATE_FORMAT_VERSION);
    text_add(t, "sched %llu\n", (unsigned long long)sched_n);
    text_add(t, "fiber_stack %lu\n", (unsigned long)ICO_FIBER_STACK_SIZE);
    /* the arena's size is a constant (arena.c maps exactly this much) */
    text_add(t, "arena %llu\n", (unsigned long long)ICO_ARENA_EE_SIZE + ICO_ARENA_HEADROOM);
    text_add(t, "gdata %llu\n", (unsigned long long)(r[ICO_GAME_DATA].hi - r[ICO_GAME_DATA].lo));
    text_add(t, "gbss %llu\n", (unsigned long long)(r[ICO_GAME_BSS].hi - r[ICO_GAME_BSS].lo));
    for (i = 0; i < ico_state_registry_count(); i++) {
        const IcoStateEntry *e = ico_state_registry_entry(i);

        text_add(t, "reg %s %llu %u\n", e->name, (unsigned long long)e->n, e->flags);
    }
}

#define BUILD_ID_PREFIX "ico-pc save state build id, second half\n"

void ico_state_build_id(const char *commit, uint8_t out[16], char *text, size_t size)
{
    Text t = {NULL, 0, 0};
    char *buf;
    uint64_t a, b;
    const size_t prefix = sizeof BUILD_ID_PREFIX - 1;

    build_text(&t, commit);
    buf = (char *)malloc(prefix + t.len + 1);
    if (buf == NULL) {
        /* no id: nothing matches it but another failed id, and the state
           host refuses saves on the out-of-memory log anyway */
        memset(out, 0, 16);
        if (text != NULL && size > 0) {
            text[0] = '\0';
        }
        return;
    }
    memcpy(buf, BUILD_ID_PREFIX, prefix);
    t.p = buf + prefix;
    t.size = t.len + 1;
    t.len = 0;
    build_text(&t, commit);
    a = xxh3_64(buf + prefix, t.len);
    b = xxh3_64(buf, prefix + t.len);
    put64(out, a);
    put64(out + 8, b);
    if (text != NULL && size > 0) {
        snprintf(text, size, "%s", buf + prefix);
    }
    free(buf);
}

void ico_state_header_init(IcoStateHeader *h, const char *commit)
{
    memset(h, 0, sizeof *h);
    h->version = ICO_STATE_FORMAT_VERSION;
    ico_state_build_id(commit, h->build_id, NULL, 0);
    snprintf(h->commit, sizeof h->commit, "%s", commit != NULL ? commit : "");
    snprintf(h->platform, sizeof h->platform, "%s", ico_state_platform_tag());
}

/* --- the validity ---------------------------------------------------------- */

/* the value of `key` in a "key=value\n" text: its start and length, or -1 */
static int key_value(const char *text, const char *key, size_t key_n, const char **v, size_t *n)
{
    const char *line = text;

    while (line != NULL && *line != '\0') {
        const char *end = strchr(line, '\n');
        const size_t len = end != NULL ? (size_t)(end - line) : strlen(line);

        if (len > key_n && memcmp(line, key, key_n) == 0 && line[key_n] == '=') {
            *v = line + key_n + 1;
            *n = len - key_n - 1;
            return 0;
        }
        line = end != NULL ? end + 1 : NULL;
    }
    return -1;
}

/* the first key of `now` whose value differs in `saved` (or a key only one
   of them has): 1 and out = "key=saved (now cur)", else 0 */
static int keys_first_diff(const char *saved, const char *now, char *out, size_t size)
{
    int pass;

    for (pass = 0; pass < 2; pass++) {
        const char *a = pass == 0 ? now : saved;
        const char *b = pass == 0 ? saved : now;
        const char *line = a;

        while (line != NULL && *line != '\0') {
            const char *end = strchr(line, '\n');
            const size_t len = end != NULL ? (size_t)(end - line) : strlen(line);
            const char *eq = memchr(line, '=', len);

            if (eq != NULL) {
                const size_t key_n = (size_t)(eq - line);
                const char *va = eq + 1;
                const size_t na = len - key_n - 1;
                const char *vb;
                size_t nb;

                if (key_value(b, line, key_n, &vb, &nb) != 0) {
                    /* a key only one side lists */
                    if (pass == 0) {
                        snprintf(out, size, "%.*s=(absent) (now %.*s)", (int)key_n, line, (int)na,
                                 va);
                    } else {
                        snprintf(out, size, "%.*s=%.*s (now absent)", (int)key_n, line, (int)na,
                                 va);
                    }
                    return 1;
                }
                if (na != nb || memcmp(va, vb, na) != 0) {
                    /* pass 0 reads `now` first: put the saved value first */
                    snprintf(out, size, "%.*s=%.*s (now %.*s)", (int)key_n, line, (int)nb, vb,
                             (int)na, va);
                    return 1;
                }
            }
            line = end != NULL ? end + 1 : NULL;
        }
    }
    return 0;
}

static void say(char *detail, size_t n, const char *fmt, const char *s)
{
    if (detail != NULL && n > 0) {
        snprintf(detail, n, fmt, s);
    }
}

IcoStateResult ico_state_header_check(const IcoStateHeader *saved, const IcoStateHeader *now,
                                      char *detail, size_t n)
{
    say(detail, n, "%s", "");
    if (strncmp(saved->platform, now->platform, sizeof saved->platform) != 0) {
        say(detail, n, "%s", saved->platform);
        return ICO_STATE_OTHER_PLATFORM;
    }
    if (memcmp(saved->build_id, now->build_id, sizeof saved->build_id) != 0) {
        say(detail, n, "%s", saved->commit);
        return ICO_STATE_OTHER_BUILD;
    }
    if (strncmp(saved->disc, now->disc, sizeof saved->disc) != 0) {
        say(detail, n, "%s", saved->disc);
        return ICO_STATE_OTHER_DISC;
    }
    {
        char none[1];

        if (keys_first_diff(saved->keys, now->keys, detail != NULL && n > 0 ? detail : none,
                            detail != NULL && n > 0 ? n : sizeof none)) {
            return ICO_STATE_OTHER_SETTINGS;
        }
    }
    if (saved->settings_hash != now->settings_hash) {
        say(detail, n, "%s", "the settings differ");
        return ICO_STATE_OTHER_SETTINGS;
    }
    return ICO_STATE_OK;
}

/* --- serialising the prefix ------------------------------------------------ */

/* header + table + keys into p (prefix_bytes long), the CRC included */
static void encode_prefix(unsigned char *p, const IcoStateHeader *h, const IcoStateSectionInfo *s,
                          int n, uint64_t file_bytes)
{
    const size_t kn = keys_len(h);
    const size_t total = prefix_bytes(n, kn);
    int i;

    memset(p, 0, total);
    memcpy(p + H_MAGIC, ICO_STATE_MAGIC, 8);
    put32(p + H_VERSION, h->version);
    put32(p + H_HEADER_BYTES, ICO_STATE_HEADER_BYTES);
    memcpy(p + H_BUILD_ID, h->build_id, 16);
    put_text(p + H_COMMIT, sizeof h->commit, h->commit);
    put_text(p + H_PLATFORM, sizeof h->platform, h->platform);
    put_text(p + H_DISC, sizeof h->disc, h->disc);
    put64(p + H_SETTINGS_HASH, h->settings_hash);
    put64(p + H_SAVED_AT, (uint64_t)h->saved_at);
    put32(p + H_PLAY_FRAMES, h->play_frames);
    put32(p + H_STAGE_NO, (uint32_t)h->stage_no);
    put32(p + H_MAIN_TICK, h->main_tick);
    put32(p + H_VSYNC, h->vsync);
    put32(p + H_SECTIONS, (uint32_t)n);
    put32(p + H_KEYS_BYTES, (uint32_t)kn);
    put64(p + H_FILE_BYTES, file_bytes);
    put32(p + H_ENTRY_BYTES, ICO_STATE_TABLE_ENTRY_BYTES);
    for (i = 0; i < n; i++) {
        unsigned char *e = p + ICO_STATE_HEADER_BYTES + (size_t)i * ICO_STATE_TABLE_ENTRY_BYTES;

        put_text(e + T_NAME, ICO_STATE_NAME_BYTES, s[i].name);
        put64(e + T_OFFSET, s[i].offset);
        put64(e + T_PACKED, s[i].packed_bytes);
        put64(e + T_RAW, s[i].raw_bytes);
        put64(e + T_ADDR, s[i].addr);
        put32(e + T_CRC, s[i].crc);
        put32(e + T_FLAGS, s[i].flags);
    }
    memcpy(p + total - kn, h->keys, kn);
    put32(p + H_CRC, (uint32_t)mz_crc32(MZ_CRC32_INIT, p, total));
}

/* --- the writer ------------------------------------------------------------ */

#define PATH_BYTES 1024
#define OUT_CHUNK (256u * 1024u)

typedef struct Job {
    char path[PATH_BYTES];
    char tmp[PATH_BYTES + 8];
    IcoStateHeader h;
    IcoStateSection s[ICO_STATE_MAX_SECTIONS];
    char names[ICO_STATE_MAX_SECTIONS][ICO_STATE_NAME_BYTES];
    int n;
    IcoStateWriteResult res;
} Job;

static Job job;
static IcoMutex *job_mu;
static IcoThread *job_thread;
static int job_state; /* 0 none, 1 running, 2 done and not collected */

static void job_fail(const char *what)
{
    if (job.res.why[0] == '\0') {
        snprintf(job.res.why, sizeof job.res.why, "%s", what);
    }
    job.res.result = ICO_STATE_IO;
}

static int fwrite_all(FILE *f, const void *p, size_t n)
{
    return n == 0 || fwrite(p, 1, n, f) == n ? 0 : -1;
}

/* one section, deflated, at the file's current position */
static int write_section(FILE *f, tdefl_compressor *c, unsigned char *out, const IcoStateSection *s,
                         IcoStateSectionInfo *info)
{
    const unsigned char *in = (const unsigned char *)s->data;
    uint64_t left = s->bytes;

    info->raw_bytes = s->bytes;
    info->packed_bytes = 0;
    info->addr = s->addr;
    info->flags = s->flags;
    info->crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, NULL, 0);
    if (s->bytes > 0) {
        info->crc = (uint32_t)mz_crc32(info->crc, in, (size_t)s->bytes);
    }
    if (tdefl_init(c, NULL, NULL,
                   (int)tdefl_create_comp_flags_from_zip_params(1, -15, MZ_DEFAULT_STRATEGY)) !=
        TDEFL_STATUS_OKAY) {
        return -1;
    }
    for (;;) {
        size_t in_n = (size_t)(left > (uint64_t)(1u << 30) ? (1u << 30) : left);
        size_t out_n = OUT_CHUNK;
        const tdefl_status st = tdefl_compress(c, in, &in_n, out, &out_n,
                                               left - in_n == 0 ? TDEFL_FINISH : TDEFL_NO_FLUSH);

        if (in_n > 0) {
            in += in_n;
            left -= in_n;
        }
        if (st < 0) {
            return -1;
        }
        if (fwrite_all(f, out, out_n) != 0) {
            return -1;
        }
        info->packed_bytes += out_n;
        if (st == TDEFL_STATUS_DONE) {
            return left == 0 ? 0 : -1;
        }
    }
}

static void writer(void *arg)
{
    const double t0 = ico_thread_now_ms();
    const size_t kn = keys_len(&job.h);
    const size_t prefix = prefix_bytes(job.n, kn);
    unsigned char *head = (unsigned char *)calloc(1, prefix);
    unsigned char *out = (unsigned char *)malloc(OUT_CHUNK);
    tdefl_compressor *c = (tdefl_compressor *)malloc(sizeof(tdefl_compressor));
    FILE *f = NULL;
    uint64_t at = prefix;
    int i;

    (void)arg;
    if (head == NULL || out == NULL || c == NULL) {
        job_fail("out of memory");
        goto done;
    }
    f = ico_fopen(job.tmp, "wb");
    if (f == NULL) {
        job_fail("cannot create the file");
        goto done;
    }
    if (fwrite_all(f, head, prefix) != 0) {
        job_fail("cannot write the file");
        goto done;
    }
    for (i = 0; i < job.n; i++) {
        IcoStateSectionInfo *info = &job.res.sections[i];

        snprintf(info->name, sizeof info->name, "%s", job.names[i]);
        info->offset = at;
        if (write_section(f, c, out, &job.s[i], info) != 0) {
            job_fail("cannot write a section");
            goto done;
        }
        at += info->packed_bytes;
    }
    encode_prefix(head, &job.h, job.res.sections, job.n, at);
    if (fseek(f, 0, SEEK_SET) != 0 || fwrite_all(f, head, prefix) != 0) {
        job_fail("cannot write the header");
        goto done;
    }
    if (ico_fsync(f) != 0) {
        job_fail("cannot sync the file");
        goto done;
    }
    if (fclose(f) != 0) {
        f = NULL;
        job_fail("cannot close the file");
        goto done;
    }
    f = NULL;
    if (ico_rename_replace(job.tmp, job.path) != 0) {
        job_fail("cannot rename the file into place");
        goto done;
    }
    job.res.file_bytes = at;
done:
    if (f != NULL) {
        fclose(f);
    }
    if (job.res.result != ICO_STATE_OK) {
        ico_remove(job.tmp);
    }
    free(head);
    free(out);
    free(c);
    job.res.n_sections = job.n;
    job.res.write_ms = ico_thread_now_ms() - t0;
    ico_mutex_lock(job_mu);
    job_state = 2;
    ico_mutex_unlock(job_mu);
}

IcoStateResult ico_state_file_write(const char *path, const IcoStateHeader *h,
                                    const IcoStateSection *s, int n)
{
    int i;

    if (job_mu == NULL) {
        job_mu = ico_mutex_create();
        if (job_mu == NULL) {
            return ICO_STATE_IO;
        }
    }
    if (ico_state_file_busy()) {
        return ICO_STATE_BUSY;
    }
    if (path == NULL || strlen(path) >= PATH_BYTES || h == NULL || n < 0 ||
        n > ICO_STATE_MAX_SECTIONS || (n > 0 && s == NULL)) {
        return ICO_STATE_IO;
    }
    for (i = 0; i < n; i++) {
        if (s[i].name == NULL || s[i].name[0] == '\0' ||
            strlen(s[i].name) >= ICO_STATE_NAME_BYTES || (s[i].bytes > 0 && s[i].data == NULL)) {
            return ICO_STATE_IO;
        }
    }
    memset(&job, 0, sizeof job);
    snprintf(job.path, sizeof job.path, "%s", path);
    snprintf(job.tmp, sizeof job.tmp, "%s.tmp", path);
    job.h = *h;
    job.h.keys[sizeof job.h.keys - 1] = '\0';
    job.n = n;
    for (i = 0; i < n; i++) {
        job.s[i] = s[i];
        snprintf(job.names[i], sizeof job.names[i], "%s", s[i].name);
        job.s[i].name = job.names[i];
    }
    job.res.result = ICO_STATE_OK;
    ico_mutex_lock(job_mu);
    job_state = 1;
    ico_mutex_unlock(job_mu);
    job_thread = ico_thread_create(writer, NULL);
    if (job_thread == NULL) {
        ico_mutex_lock(job_mu);
        job_state = 0;
        ico_mutex_unlock(job_mu);
        return ICO_STATE_IO;
    }
    return ICO_STATE_OK;
}

int ico_state_file_busy(void)
{
    int st;

    if (job_mu == NULL) {
        return 0;
    }
    ico_mutex_lock(job_mu);
    st = job_state;
    ico_mutex_unlock(job_mu);
    return st != 0;
}

static void collect(IcoStateWriteResult *out)
{
    ico_thread_join(job_thread);
    job_thread = NULL;
    if (out != NULL) {
        *out = job.res;
    }
    ico_mutex_lock(job_mu);
    job_state = 0;
    ico_mutex_unlock(job_mu);
}

int ico_state_file_poll(IcoStateWriteResult *out)
{
    int st;

    if (job_mu == NULL) {
        return 0;
    }
    ico_mutex_lock(job_mu);
    st = job_state;
    ico_mutex_unlock(job_mu);
    if (st != 2) {
        return 0;
    }
    collect(out);
    return 1;
}

int ico_state_file_flush(IcoStateWriteResult *out)
{
    if (!ico_state_file_busy()) {
        return 0;
    }
    collect(out);
    return 1;
}

/* --- the reader ------------------------------------------------------------ */

static IcoStateResult read_header_f(FILE *f, IcoStateHeader *out)
{
    unsigned char hb[ICO_STATE_HEADER_BYTES];
    unsigned char *p = NULL;
    long size;
    uint32_t n, kn, crc;
    size_t prefix;
    uint32_t i;
    IcoStateResult rc = ICO_STATE_BAD_FILE;

    memset(out, 0, sizeof *out);
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        return ICO_STATE_BAD_FILE;
    }
    if ((unsigned long)size < sizeof hb || fread(hb, 1, sizeof hb, f) != sizeof hb) {
        return ICO_STATE_BAD_FILE;
    }
    if (memcmp(hb + H_MAGIC, ICO_STATE_MAGIC, 8) != 0 ||
        get32(hb + H_VERSION) != ICO_STATE_FORMAT_VERSION ||
        get32(hb + H_HEADER_BYTES) != ICO_STATE_HEADER_BYTES ||
        get32(hb + H_ENTRY_BYTES) != ICO_STATE_TABLE_ENTRY_BYTES) {
        return ICO_STATE_BAD_FILE;
    }
    n = get32(hb + H_SECTIONS);
    kn = get32(hb + H_KEYS_BYTES);
    if (n > ICO_STATE_MAX_SECTIONS || kn >= ICO_STATE_KEYS_MAX ||
        get64(hb + H_FILE_BYTES) != (uint64_t)size) {
        return ICO_STATE_BAD_FILE; /* truncated, or not what it says */
    }
    prefix = prefix_bytes((int)n, kn);
    if ((uint64_t)prefix > (uint64_t)size) {
        return ICO_STATE_BAD_FILE;
    }
    p = (unsigned char *)malloc(prefix);
    if (p == NULL) {
        return ICO_STATE_BAD_FILE;
    }
    memcpy(p, hb, sizeof hb);
    if (fread(p + sizeof hb, 1, prefix - sizeof hb, f) != prefix - sizeof hb) {
        goto out;
    }
    crc = get32(p + H_CRC);
    put32(p + H_CRC, 0);
    if ((uint32_t)mz_crc32(MZ_CRC32_INIT, p, prefix) != crc) {
        goto out;
    }
    out->version = get32(p + H_VERSION);
    memcpy(out->build_id, p + H_BUILD_ID, 16);
    get_text(out->commit, sizeof out->commit, p + H_COMMIT, sizeof out->commit);
    get_text(out->platform, sizeof out->platform, p + H_PLATFORM, sizeof out->platform);
    get_text(out->disc, sizeof out->disc, p + H_DISC, sizeof out->disc);
    out->settings_hash = get64(p + H_SETTINGS_HASH);
    out->saved_at = (int64_t)get64(p + H_SAVED_AT);
    out->play_frames = get32(p + H_PLAY_FRAMES);
    out->stage_no = (int32_t)get32(p + H_STAGE_NO);
    out->main_tick = get32(p + H_MAIN_TICK);
    out->vsync = get32(p + H_VSYNC);
    out->file_bytes = (uint64_t)size;
    out->n_sections = (int)n;
    for (i = 0; i < n; i++) {
        const unsigned char *e =
            p + ICO_STATE_HEADER_BYTES + (size_t)i * ICO_STATE_TABLE_ENTRY_BYTES;
        IcoStateSectionInfo *s = &out->sections[i];

        get_text(s->name, sizeof s->name, e + T_NAME, ICO_STATE_NAME_BYTES);
        s->offset = get64(e + T_OFFSET);
        s->packed_bytes = get64(e + T_PACKED);
        s->raw_bytes = get64(e + T_RAW);
        s->addr = get64(e + T_ADDR);
        s->crc = get32(e + T_CRC);
        s->flags = get32(e + T_FLAGS);
        if (s->name[0] == '\0' || s->offset < prefix || s->offset > (uint64_t)size ||
            s->packed_bytes > (uint64_t)size - s->offset) {
            goto out;
        }
    }
    memcpy(out->keys, p + prefix - kn, kn);
    out->keys[kn] = '\0';
    rc = ICO_STATE_OK;
out:
    free(p);
    if (rc != ICO_STATE_OK) {
        memset(out, 0, sizeof *out);
    }
    return rc;
}

static FILE *open_state(const char *path, IcoStateResult *why)
{
    FILE *f = ico_fopen(path, "rb");

    if (f == NULL) {
        *why = ico_path_kind(path, NULL, NULL) < 0 ? ICO_STATE_EMPTY : ICO_STATE_BAD_FILE;
    }
    return f;
}

IcoStateResult ico_state_file_read_header(const char *path, IcoStateHeader *out)
{
    IcoStateResult rc = ICO_STATE_OK;
    FILE *f = open_state(path, &rc);

    if (f == NULL) {
        memset(out, 0, sizeof *out);
        return rc;
    }
    rc = read_header_f(f, out);
    fclose(f);
    return rc;
}

#define IN_CHUNK (64u * 1024u)

/* inflates s's packed bytes at the file's position into dst */
static int inflate_section(FILE *f, const IcoStateSectionInfo *s, unsigned char *dst,
                           tinfl_decompressor *d, unsigned char *in)
{
    unsigned char dummy[1];
    unsigned char *base = s->raw_bytes > 0 ? dst : dummy;
    uint64_t packed_left = s->packed_bytes;
    uint64_t out_pos = 0;
    size_t in_have = 0, in_pos = 0;
    tinfl_status st;

    if (fseek(f, (long)s->offset, SEEK_SET) != 0) {
        return -1;
    }
    tinfl_init(d);
    for (;;) {
        size_t in_n, out_n;

        if (in_pos == in_have && packed_left > 0) {
            in_have = (size_t)(packed_left > IN_CHUNK ? IN_CHUNK : packed_left);
            if (fread(in, 1, in_have, f) != in_have) {
                return -1;
            }
            packed_left -= in_have;
            in_pos = 0;
        }
        in_n = in_have - in_pos;
        out_n = (size_t)(s->raw_bytes - out_pos);
        st = tinfl_decompress(d, in + in_pos, &in_n, base, base + out_pos, &out_n,
                              TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF |
                                  (packed_left > 0 ? TINFL_FLAG_HAS_MORE_INPUT : 0));
        in_pos += in_n;
        out_pos += out_n;
        if (st == TINFL_STATUS_DONE) {
            break;
        }
        if (st != TINFL_STATUS_NEEDS_MORE_INPUT || (packed_left == 0 && in_pos == in_have)) {
            return -1; /* corrupt, longer than its raw size, or cut short */
        }
    }
    if (out_pos != s->raw_bytes || in_pos != in_have || packed_left != 0) {
        return -1;
    }
    if ((uint32_t)mz_crc32(MZ_CRC32_INIT, s->raw_bytes > 0 ? dst : NULL, (size_t)s->raw_bytes) !=
        s->crc) {
        return -1;
    }
    return 0;
}

IcoStateResult ico_state_file_read(const char *path, IcoStateHeader *h, IcoStateSectionFn fn,
                                   void *user)
{
    IcoStateResult rc = ICO_STATE_OK;
    FILE *f = open_state(path, &rc);
    IcoStateHeader *hh = h;
    tinfl_decompressor *d = NULL;
    unsigned char *in = NULL;
    int i;

    if (f == NULL) {
        if (h != NULL) {
            memset(h, 0, sizeof *h);
        }
        return rc;
    }
    if (hh == NULL) {
        hh = (IcoStateHeader *)malloc(sizeof *hh);
        if (hh == NULL) {
            fclose(f);
            return ICO_STATE_BAD_FILE;
        }
    }
    rc = read_header_f(f, hh);
    if (rc != ICO_STATE_OK) {
        goto out;
    }
    d = (tinfl_decompressor *)malloc(sizeof *d);
    in = (unsigned char *)malloc(IN_CHUNK);
    if (d == NULL || in == NULL) {
        rc = ICO_STATE_BAD_FILE;
        goto out;
    }
    for (i = 0; i < hh->n_sections; i++) {
        unsigned char *dst = fn != NULL ? (unsigned char *)fn(user, &hh->sections[i]) : NULL;

        if (dst == NULL) {
            continue;
        }
        if (inflate_section(f, &hh->sections[i], dst, d, in) != 0) {
            rc = ICO_STATE_BAD_FILE;
            goto out;
        }
    }
out:
    free(d);
    free(in);
    if (hh != h) {
        free(hh);
    }
    fclose(f);
    return rc;
}
