/*
 * port/platform/report.c
 *
 * The report file of an abnormal end (report.h): a zip written with raw
 * file calls and static buffers, its entries deflated by miniz's tdefl.
 */
#include "report.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "host_fs.h"
#include "miniz.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

/* --- Paths --------------------------------------------------------------------- */

#define PATH_BYTES 1024

static char s_logs[PATH_BYTES];
static char s_exe[PATH_BYTES];
static char s_pref[PATH_BYTES];
static char s_ini[PATH_BYTES];
static char s_version_text[256];
#ifndef _WIN32
static long s_utc_offset_s;
#endif

void ico_report_set_paths(const char *logs_dir, const char *exe_dir, const char *pref_dir,
                          const char *ini_path)
{
    snprintf(s_logs, sizeof s_logs, "%s", logs_dir != NULL ? logs_dir : "");
    snprintf(s_exe, sizeof s_exe, "%s", exe_dir != NULL ? exe_dir : "");
    snprintf(s_pref, sizeof s_pref, "%s", pref_dir != NULL ? pref_dir : "");
    snprintf(s_ini, sizeof s_ini, "%s", ini_path != NULL ? ini_path : "");
#ifndef _WIN32
    {
        /* the offset now, so the crash path needs no time zone lookup */
        const time_t now = time(NULL);
        struct tm tm;

        if (localtime_r(&now, &tm) != NULL) {
            s_utc_offset_s = tm.tm_gmtoff;
        }
    }
#endif
}

void ico_report_set_version_text(const char *text)
{
    snprintf(s_version_text, sizeof s_version_text, "%s", text != NULL ? text : "");
}

/* dir + "/" + name into out: 0, or -1 when it does not fit or dir is "" */
static int join(char *out, size_t size, const char *dir, const char *name)
{
    const size_t n = strlen(dir);
    int c;

    if (n == 0) {
        return -1;
    }
    c = snprintf(out, size, "%s%s%s", dir, dir[n - 1] == '/' || dir[n - 1] == '\\' ? "" : "/",
                 name);
    return c < 0 || (size_t)c >= size ? -1 : 0;
}

/* --- Raw files ------------------------------------------------------------------ */

#ifdef _WIN32

typedef HANDLE RFile;
#define RFILE_NONE INVALID_HANDLE_VALUE

static wchar_t s_wpath[PATH_BYTES];

static const wchar_t *wide(const char *path)
{
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, s_wpath,
                            (int)(sizeof s_wpath / sizeof s_wpath[0])) <= 0) {
        return NULL;
    }
    return s_wpath;
}

static RFile r_open_read(const char *path)
{
    const wchar_t *w = wide(path);

    if (w == NULL) {
        return RFILE_NONE;
    }
    return CreateFileW(w, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

/* a new file; RFILE_NONE when it exists (*exists 1) or cannot be made */
static RFile r_create(const char *path, int *exists)
{
    const wchar_t *w = wide(path);
    HANDLE h;

    *exists = 0;
    if (w == NULL) {
        return RFILE_NONE;
    }
    h = CreateFileW(w, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_EXISTS) {
        *exists = 1;
    }
    return h;
}

static long r_read(RFile f, void *buf, size_t n)
{
    DWORD got = 0;

    if (!ReadFile(f, buf, (DWORD)n, &got, NULL)) {
        return -1;
    }
    return (long)got;
}

static int r_write(RFile f, const void *buf, size_t n)
{
    DWORD done = 0;

    return WriteFile(f, buf, (DWORD)n, &done, NULL) && done == n ? 0 : -1;
}

static int r_seek(RFile f, uint64_t at)
{
    LARGE_INTEGER li;

    li.QuadPart = (LONGLONG)at;
    return SetFilePointerEx(f, li, NULL, FILE_BEGIN) ? 0 : -1;
}

static void r_close(RFile f)
{
    CloseHandle(f);
}

static void r_remove(const char *path)
{
    const wchar_t *w = wide(path);

    if (w != NULL) {
        DeleteFileW(w);
    }
}

#else

typedef int RFile;
#define RFILE_NONE (-1)

static RFile r_open_read(const char *path)
{
    return open(path, O_RDONLY | O_CLOEXEC);
}

static RFile r_create(const char *path, int *exists)
{
    const int fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0644);

    *exists = fd < 0 && errno == EEXIST;
    return fd;
}

static long r_read(RFile f, void *buf, size_t n)
{
    ssize_t r;

    do {
        r = read(f, buf, n);
    } while (r < 0 && errno == EINTR);
    return (long)r;
}

static int r_write(RFile f, const void *buf, size_t n)
{
    const char *p = (const char *)buf;

    while (n > 0) {
        const ssize_t w = write(f, p, n);

        if (w < 0 && errno == EINTR) {
            continue;
        }
        if (w <= 0) {
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

static int r_seek(RFile f, uint64_t at)
{
    return lseek(f, (off_t)at, SEEK_SET) == (off_t)at ? 0 : -1;
}

static void r_close(RFile f)
{
    close(f);
}

static void r_remove(const char *path)
{
    unlink(path);
}

#endif

/* --- The local time, without a time zone lookup -------------------------------- */

typedef struct Civil {
    int y, mo, d, h, mi, s;
} Civil;

static void civil_now(Civil *c)
{
#ifdef _WIN32
    SYSTEMTIME st;

    GetLocalTime(&st);
    c->y = st.wYear;
    c->mo = st.wMonth;
    c->d = st.wDay;
    c->h = st.wHour;
    c->mi = st.wMinute;
    c->s = st.wSecond;
#else
    /* days from the civil date algorithm (H. Hinnant, "chrono-Compatible
       Low-Level Date Algorithms", civil_from_days) */
    long long t = (long long)time(NULL) + s_utc_offset_s;
    long long days = t >= 0 ? t / 86400 : (t - 86399) / 86400;
    long long secs = t - days * 86400;
    long long z = days + 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const long long doe = z - era * 146097;
    const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long long mp = (5 * doy + 2) / 153;
    const long long d = doy - (153 * mp + 2) / 5 + 1;
    const long long m = mp < 10 ? mp + 3 : mp - 9;

    c->y = (int)(yoe + era * 400 + (m <= 2));
    c->mo = (int)m;
    c->d = (int)d;
    c->h = (int)(secs / 3600);
    c->mi = (int)(secs / 60 % 60);
    c->s = (int)(secs % 60);
#endif
}

void ico_report_stamp(char *out, size_t size)
{
    Civil c;

    civil_now(&c);
    snprintf(out, size, "%04d%02d%02d-%02d%02d%02d", c.y, c.mo, c.d, c.h, c.mi, c.s);
}

/* --- The zip -------------------------------------------------------------------------- */

#define ZIP_ENTRIES 16
#define ZIP_NAME 64

typedef struct ZipEntry {
    char name[ZIP_NAME];
    uint32_t crc, csize, usize, offset;
    uint16_t method;
} ZipEntry;

static struct {
    RFile f;
    uint64_t at; /* the write position */
    int failed;
    int count;
    ZipEntry e[ZIP_ENTRIES];
    uint16_t dos_time, dos_date;
} s_zip;

static tdefl_compressor s_deflate;
static unsigned char s_in[65536];
static unsigned char s_out[65536];

static void put16(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

static void put32(unsigned char *p, uint32_t v)
{
    put16(p, v & 0xffff);
    put16(p + 2, v >> 16);
}

static void zwrite(const void *p, size_t n)
{
    if (s_zip.failed) {
        return;
    }
    if (r_write(s_zip.f, p, n) != 0) {
        s_zip.failed = 1;
        return;
    }
    s_zip.at += n;
}

static void local_header(const ZipEntry *e)
{
    unsigned char h[30];
    const size_t n = strlen(e->name);

    put32(h, 0x04034b50);
    put16(h + 4, 20);
    put16(h + 6, 0x0800); /* UTF-8 names */
    put16(h + 8, e->method);
    put16(h + 10, s_zip.dos_time);
    put16(h + 12, s_zip.dos_date);
    put32(h + 14, e->crc);
    put32(h + 18, e->csize);
    put32(h + 22, e->usize);
    put16(h + 26, (uint32_t)n);
    put16(h + 28, 0);
    zwrite(h, sizeof h);
    zwrite(e->name, n);
}

static int zip_begin(const char *path, int *exists)
{
    Civil c;

    memset(&s_zip, 0, sizeof s_zip);
    s_zip.f = r_create(path, exists);
    if (s_zip.f == RFILE_NONE) {
        return -1;
    }
    civil_now(&c);
    s_zip.dos_time = (uint16_t)(c.h << 11 | c.mi << 5 | c.s / 2);
    s_zip.dos_date = (uint16_t)((c.y < 1980 ? 0 : c.y - 1980) << 9 | c.mo << 5 | c.d);
    return 0;
}

/* One entry from a reader: read(user, buf, n) as r_read. store: kept as
   it is (a zip in the zip). The entry, or NULL when there is no room. */
typedef long (*ZipRead)(void *user, void *buf, size_t n);

static ZipEntry *zip_add(const char *name, ZipRead rd, void *user, int store)
{
    ZipEntry *e;
    uint32_t crc = 0, usize = 0, csize = 0;
    int done = 0;

    if (s_zip.failed || s_zip.count == ZIP_ENTRIES) {
        return NULL;
    }
    e = &s_zip.e[s_zip.count];
    snprintf(e->name, sizeof e->name, "%s", name);
    e->method = store ? 0 : 8;
    e->offset = (uint32_t)s_zip.at;
    e->crc = e->csize = e->usize = 0;
    local_header(e);
    if (!store) {
        tdefl_init(&s_deflate, NULL, NULL,
                   tdefl_create_comp_flags_from_zip_params(3, -15, MZ_DEFAULT_STRATEGY));
    }
    while (!done && !s_zip.failed) {
        const long got = rd(user, s_in, sizeof s_in);
        size_t in_pos = 0;

        if (got <= 0) {
            done = 1;
        } else {
            crc = (uint32_t)mz_crc32(crc, s_in, (size_t)got);
            usize += (uint32_t)got;
        }
        if (store) {
            if (got > 0) {
                zwrite(s_in, (size_t)got);
                csize += (uint32_t)got;
            }
            continue;
        }
        for (;;) {
            size_t in_n = got > 0 ? (size_t)got - in_pos : 0;
            size_t out_n = sizeof s_out;
            const tdefl_status st = tdefl_compress(&s_deflate, s_in + in_pos, &in_n, s_out, &out_n,
                                                   done ? TDEFL_FINISH : TDEFL_NO_FLUSH);

            in_pos += in_n;
            if (out_n > 0) {
                zwrite(s_out, out_n);
                csize += (uint32_t)out_n;
            }
            if (st < 0) {
                s_zip.failed = 1;
                break;
            }
            if (done ? st == TDEFL_STATUS_DONE : in_pos >= (size_t)got && out_n < sizeof s_out) {
                break;
            }
        }
    }
    e->crc = crc;
    e->csize = csize;
    e->usize = usize;
    {
        /* the sizes into the local header */
        const uint64_t end = s_zip.at;
        unsigned char h[12];

        put32(h, crc);
        put32(h + 4, csize);
        put32(h + 8, usize);
        if (r_seek(s_zip.f, e->offset + 14u) != 0 || r_write(s_zip.f, h, sizeof h) != 0 ||
            r_seek(s_zip.f, end) != 0) {
            s_zip.failed = 1;
        }
    }
    s_zip.count++;
    return e;
}

static long file_read(void *user, void *buf, size_t n)
{
    return r_read(*(RFile *)user, buf, n);
}

/* 1 when the file was added, 0 when it is not there, -1 on a failure */
static int zip_add_file(const char *name, const char *path, int store)
{
    RFile f;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    f = r_open_read(path);
    if (f == RFILE_NONE) {
        return 0;
    }
    zip_add(name, file_read, &f, store);
    r_close(f);
    return s_zip.failed ? -1 : 1;
}

typedef struct MemRead {
    const char *p;
    size_t left;
} MemRead;

static long mem_read(void *user, void *buf, size_t n)
{
    MemRead *m = (MemRead *)user;
    const size_t k = m->left < n ? m->left : n;

    memcpy(buf, m->p, k);
    m->p += k;
    m->left -= k;
    return (long)k;
}

static void zip_add_mem(const char *name, const char *data, size_t n)
{
    MemRead m = {data, n};

    zip_add(name, mem_read, &m, 0);
}

static int zip_end(void)
{
    const uint64_t cd = s_zip.at;
    unsigned char h[46];
    int i;

    for (i = 0; i < s_zip.count; i++) {
        const ZipEntry *e = &s_zip.e[i];
        const size_t n = strlen(e->name);

        put32(h, 0x02014b50);
        put16(h + 4, 20);
        put16(h + 6, 20);
        put16(h + 8, 0x0800);
        put16(h + 10, e->method);
        put16(h + 12, s_zip.dos_time);
        put16(h + 14, s_zip.dos_date);
        put32(h + 16, e->crc);
        put32(h + 20, e->csize);
        put32(h + 24, e->usize);
        put16(h + 28, (uint32_t)n);
        put16(h + 30, 0);
        put16(h + 32, 0);
        put16(h + 34, 0);
        put16(h + 36, 0);
        put32(h + 38, 0);
        put32(h + 42, e->offset);
        zwrite(h, sizeof h);
        zwrite(e->name, n);
    }
    put32(h, 0x06054b50);
    put16(h + 4, 0);
    put16(h + 6, 0);
    put16(h + 8, (uint32_t)s_zip.count);
    put16(h + 10, (uint32_t)s_zip.count);
    put32(h + 12, (uint32_t)(s_zip.at - cd));
    put32(h + 16, (uint32_t)cd);
    put16(h + 20, 0);
    zwrite(h, 22);
    r_close(s_zip.f);
    return s_zip.failed ? -1 : 0;
}

/* --- report.txt ------------------------------------------------------------------------ */

static char s_text[128 * 1024];
static size_t s_text_len;

static void text_add(const char *s, size_t n)
{
    if (s_text_len + n > sizeof s_text - 1) {
        n = sizeof s_text - 1 - s_text_len;
    }
    memcpy(s_text + s_text_len, s, n);
    s_text_len += n;
    s_text[s_text_len] = '\0';
}

static void text_line(const char *s)
{
    text_add(s, strlen(s));
    text_add("\n", 1);
}

/* label, then s, then a newline */
static void text_kv(const char *label, const char *s)
{
    text_add(label, strlen(label));
    text_line(s);
}

/* A file's lines: fn(line) for each (cut at LINE_BYTES - 1 bytes). */
#define LINE_BYTES 1024
static char s_line[LINE_BYTES];

static int each_line(const char *path, void (*fn)(const char *line, void *user), void *user)
{
    RFile f;
    size_t n = 0;
    long got;

    if (path == NULL || path[0] == '\0' || (f = r_open_read(path)) == RFILE_NONE) {
        return -1;
    }
    while ((got = r_read(f, s_in, sizeof s_in)) > 0) {
        for (long i = 0; i < got; i++) {
            const char c = (char)s_in[i];

            if (c == '\n') {
                s_line[n] = '\0';
                fn(s_line, user);
                n = 0;
            } else if (c != '\r' && n < sizeof s_line - 1) {
                s_line[n++] = c;
            }
        }
    }
    if (n > 0) {
        s_line[n] = '\0';
        fn(s_line, user);
    }
    r_close(f);
    return 0;
}

static int starts(const char *s, const char *p)
{
    return strncmp(s, p, strlen(p)) == 0;
}

/* the log's key lines: the latest of each */
enum {
    K_SUMMARY,
    K_VERSION,
    K_DEVICE,
    K_ROBUST,
    K_BLEND,
    K_INDEX,
    K_FOG,
    K_SHADERS,
    K_WINDOW,
    K_COUNT
};

static char s_key[K_COUNT][LINE_BYTES];

static int key_of(const char *l)
{
    if (starts(l, "ico_pc: summary:")) {
        return K_SUMMARY;
    }
    if (starts(l, "ico_pc: version ") || starts(l, "ico_pc: development build")) {
        return K_VERSION;
    }
    if (strstr(l, "index values up to") != NULL &&
        (strstr(l, "(Vulkan ") != NULL || strstr(l, "(D3D12") != NULL)) {
        return K_DEVICE;
    }
    if (strstr(l, "loss diagnostics:") != NULL) {
        return K_ROBUST;
    }
    if (starts(l, "rd: blend: ") && strstr(l, "self-test") != NULL &&
        !starts(l, "rd: blend: self-test:")) {
        return K_BLEND;
    }
    if (starts(l, "rd: VU index values")) {
        return K_INDEX;
    }
    if (starts(l, "rd: fog: depth path")) {
        return K_FOG;
    }
    if (starts(l, "rd: shaders:")) {
        return K_SHADERS;
    }
    if (starts(l, "window: ") && strstr(l, " preset (resolution ") != NULL) {
        return K_WINDOW;
    }
    return -1;
}

#define RING 20
static char s_ring[RING][LINE_BYTES];
static int s_ring_n, s_ring_head;
static char s_before[RING][LINE_BYTES];
static int s_before_n, s_before_head;
static char s_block[48 * 1024];
static size_t s_block_len;
static int s_in_block, s_block_cut;

static void log_line(const char *l, void *user)
{
    const int k = key_of(l);

    (void)user;
    if (k >= 0) {
        snprintf(s_key[k], sizeof s_key[k], "%s", l);
    }
    if (starts(l, "ico_pc: ======")) {
        /* a report block: the lines before it are the ring's */
        memcpy(s_before, s_ring, sizeof s_ring);
        s_before_n = s_ring_n;
        s_before_head = s_ring_head;
        s_in_block = 1;
        s_block_len = 0;
        s_block_cut = 0;
    }
    if (s_in_block) {
        const size_t n = strlen(l);

        if (s_block_len + n + 1 < sizeof s_block) {
            memcpy(s_block + s_block_len, l, n);
            s_block[s_block_len + n] = '\n';
            s_block_len += n + 1;
        } else {
            s_block_cut = 1;
        }
        return;
    }
    if (starts(l, "ico_pc:") || starts(l, "window:") || starts(l, "rd:")) {
        snprintf(s_ring[s_ring_head], sizeof s_ring[0], "%s", l);
        s_ring_head = (s_ring_head + 1) % RING;
        if (s_ring_n < RING) {
            s_ring_n++;
        }
    }
}

static char s_stages[LINE_BYTES];

static void ach_line(const char *l, void *user)
{
    const char *p = l;

    (void)user;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (starts(p, "stages") && (p[6] == ' ' || p[6] == '=')) {
        snprintf(s_stages, sizeof s_stages, "%s", p);
    }
}

static void version_line(const char *l, void *user)
{
    (void)user;
    if (l[0] != '\0') {
        text_kv("  ", l);
    }
}

static void build_text(const IcoReportInputs *in)
{
    char path[PATH_BYTES + 32];
    char line[256];
    Civil c;
    int i;

    s_text_len = 0;
    s_text[0] = '\0';
    civil_now(&c);
    text_line("ICO PC report (attach this whole zip to the issue)");
    text_kv("what ended the run: ", in->what != NULL ? in->what : "?");
    snprintf(line, sizeof line, "written %04d-%02d-%02d %02d:%02d:%02d, %.0f s into the run", c.y,
             c.mo, c.d, c.h, c.mi, c.s, in->uptime_s);
    text_line(line);
    text_line("VERSION.txt:");
    if (s_version_text[0] != '\0') {
        text_line(s_version_text);
    } else if (!(join(path, sizeof path, s_exe, "VERSION.txt") == 0 &&
                 each_line(path, version_line, NULL) == 0) &&
               !(join(path, sizeof path, s_exe, "../VERSION.txt") == 0 &&
                 each_line(path, version_line, NULL) == 0)) {
        text_line("  (none beside the program: a development build)");
    }
    s_stages[0] = '\0';
    if (join(path, sizeof path, s_pref, "achievements.toml") == 0) {
        (void)each_line(path, ach_line, NULL);
    }
    text_kv("achievements.toml: ", s_stages[0] != '\0' ? s_stages : "(no stages line)");
    text_kv("summary: ", in->summary != NULL && in->summary[0] != '\0'
                             ? in->summary
                             : "(not reached: the run ended before the renderer started)");

    memset(s_key, 0, sizeof s_key);
    s_ring_n = s_ring_head = s_before_n = s_before_head = 0;
    s_in_block = 0;
    s_block_len = 0;
    s_block_cut = 0;
    if (in->log_path == NULL || each_line(in->log_path, log_line, NULL) != 0) {
        text_line("");
        text_line("(the log could not be read)");
        return;
    }
    text_line("");
    text_line("the log's key lines (the latest of each):");
    for (i = 0; i < K_COUNT; i++) {
        if (s_key[i][0] != '\0') {
            text_kv("  ", s_key[i]);
        }
    }
    {
        /* the last lines before the report block (or the end) */
        char (*ring)[LINE_BYTES] = s_in_block ? s_before : s_ring;
        const int n = s_in_block ? s_before_n : s_ring_n;
        const int head = s_in_block ? s_before_head : s_ring_head;

        text_line("");
        text_line(s_in_block ? "the last ico_pc:/window:/rd: lines before the report:"
                             : "the last ico_pc:/window:/rd: lines:");
        for (i = 0; i < n; i++) {
            text_kv("  ", ring[(head - n + i + RING) % RING]);
        }
    }
    if (s_in_block) {
        text_line("");
        text_line("the report (crash or watchdog, with the symbolized stack):");
        text_add(s_block, s_block_len);
        if (s_block_cut) {
            text_line("(cut here: the rest is in ico-pc.log)");
        }
    }
    if (in->extra != NULL && in->extra[0] != '\0') {
        text_line("");
        text_add(in->extra, strlen(in->extra));
    }
}

/* --- The report --------------------------------------------------------------------------- */

int ico_report_write(const IcoReportInputs *in, char *name, size_t name_size)
{
    char stamp[32], zname[64], zpath[PATH_BYTES + 64], path[PATH_BYTES + 64];
    int exists = 0, k;

    if (s_logs[0] == '\0') {
        snprintf(name, name_size, "no logs folder");
        return -1;
    }
    ico_report_stamp(stamp, sizeof stamp);
    for (k = 1; k <= 9; k++) {
        if (k == 1) {
            snprintf(zname, sizeof zname, "report-%s.zip", stamp);
        } else {
            snprintf(zname, sizeof zname, "report-%s-%d.zip", stamp, k);
        }
        if (join(zpath, sizeof zpath, s_logs, zname) != 0) {
            snprintf(name, name_size, "the logs folder's path is too long");
            return -1;
        }
        if (zip_begin(zpath, &exists) == 0) {
            break;
        }
        if (!exists) {
            snprintf(name, name_size, "%s could not be created", zname);
            return -1;
        }
    }
    if (k > 9) {
        snprintf(name, name_size, "too many reports this second");
        return -1;
    }
    build_text(in);
    zip_add_mem("report.txt", s_text, s_text_len);
    (void)zip_add_file("ico-pc.log", in->log_path, 0);
    if (join(path, sizeof path, s_logs, "ico-pc-previous.log") == 0) {
        (void)zip_add_file("ico-pc-previous.log", path, 0);
    }
    if (join(path, sizeof path, s_logs, "startup-activity.txt") == 0) {
        (void)zip_add_file("startup-activity.txt", path, 0);
    }
    if (join(path, sizeof path, s_pref, "config.toml") == 0) {
        (void)zip_add_file("config.toml", path, 0);
    }
    (void)zip_add_file("ico-pc.ini", s_ini, 0);
    if (in->loss_zip != NULL && in->loss_zip[0] != '\0') {
        const char *b = in->loss_zip + strlen(in->loss_zip);

        while (b > in->loss_zip && b[-1] != '/' && b[-1] != '\\') {
            b--;
        }
        (void)zip_add_file(b, in->loss_zip, 1);
    }
    if (zip_end() != 0) {
        r_remove(zpath);
        snprintf(name, name_size, "%s could not be written", zname);
        return -1;
    }
    snprintf(name, name_size, "%s", zname);
    return 0;
}

/* --- Pruning (normal paths) ------------------------------------------------------------- */

#define PRUNE_MAX 256

static char s_prune[PRUNE_MAX][PATH_BYTES];
static int s_prune_n;

static int prune_visit(const char *path, const char *name, void *user)
{
    const size_t n = strlen(name);

    (void)user;
    if (n > 11 && strncmp(name, "report-", 7) == 0 && strcmp(name + n - 4, ".zip") == 0 &&
        strlen(path) < PATH_BYTES && s_prune_n < PRUNE_MAX) {
        snprintf(s_prune[s_prune_n++], PATH_BYTES, "%s", path);
    }
    return 0;
}

/* A name's order key: its report-YYYYMMDD-HHMMSS stamp, then the -k
   suffix (the plain name of a second is k = 0, so it is older than its -1) */
static void prune_key(const char *path, const char **stamp, long *k)
{
    const char *b = path + strlen(path);
    char *end;

    while (b > path && b[-1] != '/' && b[-1] != '\\') {
        b--;
    }
    *stamp = b + 7; /* after "report-" */
    *k = 0;
    if (strlen(b) > 7 + 15 && b[7 + 15] == '-') {
        *k = strtol(b + 7 + 15 + 1, &end, 10);
    }
}

static int prune_cmp(const void *pa, const void *pb)
{
    const char *sa, *sb;
    long ka, kb;
    int c;

    prune_key((const char *)pa, &sa, &ka);
    prune_key((const char *)pb, &sb, &kb);
    c = strncmp(sa, sb, 15);
    if (c != 0) {
        return c;
    }
    return ka < kb ? -1 : ka > kb;
}

int ico_report_prune(int keep)
{
    int removed = 0, i;

    s_prune_n = 0;
    if (s_logs[0] == '\0' || ico_dir_walk(s_logs, 0, prune_visit, NULL) < 0) {
        return -1;
    }
    /* report-YYYYMMDD-HHMMSS[-k].zip: the folder's order is the file
       system's, and byte order is not age (report-X-1.zip sorts before
       report-X.zip but is newer): sorted by stamp, then k (the plain name
       as 0), oldest first */
    qsort(s_prune, (size_t)s_prune_n, sizeof s_prune[0], prune_cmp);
    for (i = 0; i + (keep > 0 ? keep : 0) < s_prune_n; i++) {
        removed += ico_remove(s_prune[i]) == 0;
    }
    return removed;
}
