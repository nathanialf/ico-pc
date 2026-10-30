/*
 * port/platform/test/state_format_test.c
 *
 * The save state file (state_file.h), in the folder given (the build
 * folder under ctest; "." without an argument):
 *
 *   round trip   a header and four sections (a 3 MB one mostly zeros, an
 *                odd-sized random one, an empty one, a small one) written
 *                on the writer thread and read back equal, field by field
 *                and byte by byte; the big one packs smaller; a section the
 *                callback skips is left alone; a second write replaces the
 *                file; one write in flight (BUSY until collected)
 *   refusals     every header field the validity compares, changed alone:
 *                platform, build, disc, a simulation key's value, a key
 *                only one side has, the hash alone; platform wins over
 *                build; the detail names what differs
 *   bad files    no file (EMPTY); a wrong magic, a stale version, a changed
 *                header byte (CRC), truncated by one byte, cut to 100
 *                bytes, empty, one byte appended, a flipped byte in a
 *                section's packed data (BAD_FILE)
 *   writes       too many sections, a long name: refused at once; a folder
 *                that does not exist: the write fails on its thread and
 *                leaves no .tmp behind
 *   build id     stable for the same inputs; changes with the commit and
 *                with one more registered block; the text names them
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../host_fs.h"
#include "../state_file.h"
#include "../state_registry.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static char dir[512];
static char path[640];

#define BIG_BYTES (3u * 1024u * 1024u)
#define ODD_BYTES 4097u
#define SMALL_BYTES 100u

static unsigned char *big, *odd, *small;
static int reg_a[16], reg_b[3];

static uint32_t lcg = 12345;

static unsigned char rnd(void)
{
    lcg = lcg * 1103515245u + 12345u;
    return (unsigned char)(lcg >> 16);
}

static void fill(void)
{
    uint32_t i;

    big = (unsigned char *)calloc(1, BIG_BYTES);
    odd = (unsigned char *)malloc(ODD_BYTES);
    small = (unsigned char *)malloc(SMALL_BYTES);
    if (big == NULL || odd == NULL || small == NULL) {
        printf("FAIL out of memory\n");
        exit(1);
    }
    for (i = 0; i < 65536; i++) {
        big[1024 * 1024 + i] = rnd();
    }
    for (i = 0; i < 4096; i += 4) {
        big[BIG_BYTES - 4096 + i] = (unsigned char)i;
    }
    for (i = 0; i < ODD_BYTES; i++) {
        odd[i] = rnd();
    }
    for (i = 0; i < SMALL_BYTES; i++) {
        small[i] = (unsigned char)(i * 7);
    }
}

static void make_header(IcoStateHeader *h)
{
    ico_state_header_init(h, "v0.9.6-test-abcdef0");
    snprintf(h->disc, sizeof h->disc, "pal");
    h->settings_hash = 0x1122334455667788ull;
    snprintf(h->keys, sizeof h->keys, "video.video_mode=auto\ngameplay.mirror=false\ndev.x=\n");
    h->saved_at = 1793000000;
    h->play_frames = 123456;
    h->stage_no = 29;
    h->main_tick = 98765;
    h->vsync = 197531;
}

static int make_sections(IcoStateSection *s)
{
    memset(s, 0, 4 * sizeof *s);
    s[0].name = "ARENA";
    s[0].data = big;
    s[0].bytes = BIG_BYTES;
    s[0].addr = 0x3C00000000ull;
    s[0].flags = 1;
    s[1].name = "GDATA";
    s[1].data = odd;
    s[1].bytes = ODD_BYTES;
    s[1].addr = 0x5555aaaa0000ull;
    s[2].name = "EMPTY";
    s[2].data = NULL;
    s[2].bytes = 0;
    s[3].name = "REGISTRY_BLOCKS";
    s[3].data = small;
    s[3].bytes = SMALL_BYTES;
    s[3].flags = 7;
    return 4;
}

static int write_now(const char *p, const IcoStateHeader *h, const IcoStateSection *s, int n,
                     IcoStateWriteResult *res)
{
    IcoStateResult rc = ico_state_file_write(p, h, s, n);

    if (rc != ICO_STATE_OK) {
        return (int)rc;
    }
    CHECK(ico_state_file_flush(res) == 1);
    return (int)res->result;
}

/* --- the round trip ----------------------------------------------------------- */

static unsigned char *out_buf[4];
static int skip_arena;

static void *want_section(void *user, const IcoStateSectionInfo *s)
{
    int i;

    (void)user;
    for (i = 0; i < 4; i++) {
        static const char *const names[4] = {"ARENA", "GDATA", "EMPTY", "REGISTRY_BLOCKS"};

        if (strcmp(s->name, names[i]) == 0) {
            if (i == 0 && skip_arena) {
                return NULL;
            }
            free(out_buf[i]);
            out_buf[i] = (unsigned char *)malloc(s->raw_bytes > 0 ? (size_t)s->raw_bytes : 1);
            if (out_buf[i] != NULL && s->raw_bytes > 0) {
                memset(out_buf[i], 0xA5, (size_t)s->raw_bytes);
            }
            return out_buf[i];
        }
    }
    printf("FAIL an unknown section %s\n", s->name);
    fails++;
    return NULL;
}

static void test_round_trip(void)
{
    IcoStateHeader h, r;
    IcoStateSection s[4];
    IcoStateWriteResult res;
    int n = make_sections(s);
    int i;

    make_header(&h);
    remove(path);
    CHECK(!ico_state_file_busy());
    CHECK(ico_state_file_poll(&res) == 0);
    CHECK(ico_state_file_flush(&res) == 0);
    CHECK(ico_state_file_write(path, &h, s, n) == ICO_STATE_OK);
    CHECK(ico_state_file_busy());
    /* one in flight, and a finished one waits to be collected */
    CHECK(ico_state_file_write(path, &h, s, n) == ICO_STATE_BUSY);
    CHECK(ico_state_file_flush(&res) == 1);
    CHECK(!ico_state_file_busy());
    CHECK(ico_state_file_poll(&res) == 0);
    CHECK(res.result == ICO_STATE_OK && res.why[0] == '\0');
    CHECK(res.n_sections == 4);
    CHECK(res.sections[0].raw_bytes == BIG_BYTES);
    CHECK(res.sections[0].packed_bytes < BIG_BYTES / 8);
    CHECK(res.sections[2].raw_bytes == 0 && res.sections[2].packed_bytes > 0);
    printf("round trip: %llu bytes, ARENA %u -> %llu, written in %.1f ms\n",
           (unsigned long long)res.file_bytes, BIG_BYTES,
           (unsigned long long)res.sections[0].packed_bytes, res.write_ms);

    CHECK(ico_state_file_read_header(path, &r) == ICO_STATE_OK);
    CHECK(r.version == ICO_STATE_FORMAT_VERSION);
    CHECK(memcmp(r.build_id, h.build_id, 16) == 0);
    CHECK(strcmp(r.commit, h.commit) == 0);
    CHECK(strcmp(r.platform, h.platform) == 0);
    CHECK(strcmp(r.disc, "pal") == 0);
    CHECK(r.settings_hash == h.settings_hash);
    CHECK(strcmp(r.keys, h.keys) == 0);
    CHECK(r.saved_at == h.saved_at && r.play_frames == h.play_frames);
    CHECK(r.stage_no == 29 && r.main_tick == 98765 && r.vsync == 197531);
    CHECK(r.file_bytes == res.file_bytes);
    CHECK(r.n_sections == 4);
    for (i = 0; i < 4 && i < r.n_sections; i++) {
        CHECK(strcmp(r.sections[i].name, s[i].name) == 0);
        CHECK(r.sections[i].raw_bytes == s[i].bytes);
        CHECK(r.sections[i].addr == s[i].addr);
        CHECK(r.sections[i].flags == s[i].flags);
        CHECK(r.sections[i].packed_bytes == res.sections[i].packed_bytes);
    }
    CHECK(ico_state_header_check(&r, &h, NULL, 0) == ICO_STATE_OK);

    skip_arena = 0;
    CHECK(ico_state_file_read(path, &r, want_section, NULL) == ICO_STATE_OK);
    CHECK(out_buf[0] != NULL && memcmp(out_buf[0], big, BIG_BYTES) == 0);
    CHECK(out_buf[1] != NULL && memcmp(out_buf[1], odd, ODD_BYTES) == 0);
    CHECK(out_buf[2] != NULL);
    CHECK(out_buf[3] != NULL && memcmp(out_buf[3], small, SMALL_BYTES) == 0);

    /* a skipped section is not touched */
    memset(out_buf[0], 0x5A, BIG_BYTES);
    skip_arena = 1;
    CHECK(ico_state_file_read(path, NULL, want_section, NULL) == ICO_STATE_OK);
    CHECK(out_buf[0][0] == 0x5A && out_buf[0][BIG_BYTES - 1] == 0x5A);
    CHECK(memcmp(out_buf[1], odd, ODD_BYTES) == 0);
    skip_arena = 0;

    /* a second write replaces the file */
    h.stage_no = 4;
    odd[17] ^= 0xFF;
    CHECK(write_now(path, &h, s, n, &res) == ICO_STATE_OK);
    CHECK(ico_state_file_read(path, &r, want_section, NULL) == ICO_STATE_OK);
    CHECK(r.stage_no == 4);
    CHECK(memcmp(out_buf[1], odd, ODD_BYTES) == 0);
}

/* --- the refusals ------------------------------------------------------------- */

static void test_refusals(void)
{
    IcoStateHeader now, saved;
    char detail[160];

    make_header(&now);
    saved = now;
    CHECK(ico_state_header_check(&saved, &now, detail, sizeof detail) == ICO_STATE_OK);
    CHECK(detail[0] == '\0');

    saved = now;
    snprintf(saved.platform, sizeof saved.platform, "other-os");
    CHECK(ico_state_header_check(&saved, &now, detail, sizeof detail) == ICO_STATE_OTHER_PLATFORM);
    CHECK(strcmp(detail, "other-os") == 0);
    /* platform before build */
    saved.build_id[3] ^= 1;
    CHECK(ico_state_header_check(&saved, &now, detail, sizeof detail) == ICO_STATE_OTHER_PLATFORM);

    saved = now;
    saved.build_id[15] ^= 0x80;
    snprintf(saved.commit, sizeof saved.commit, "v0.9.5");
    CHECK(ico_state_header_check(&saved, &now, detail, sizeof detail) == ICO_STATE_OTHER_BUILD);
    CHECK(strcmp(detail, "v0.9.5") == 0);

    saved = now;
    snprintf(saved.disc, sizeof saved.disc, "ntsc");
    CHECK(ico_state_header_check(&saved, &now, detail, sizeof detail) == ICO_STATE_OTHER_DISC);
    CHECK(strcmp(detail, "ntsc") == 0);

    saved = now;
    snprintf(saved.keys, sizeof saved.keys,
             "video.video_mode=auto\ngameplay.mirror=true\ndev.x=\n");
    saved.settings_hash ^= 1;
    CHECK(ico_state_header_check(&saved, &now, detail, sizeof detail) == ICO_STATE_OTHER_SETTINGS);
    CHECK(strcmp(detail, "gameplay.mirror=true (now false)") == 0);

    /* a key the saving build had and this one lacks, and the reverse */
    saved = now;
    snprintf(saved.keys, sizeof saved.keys,
             "video.video_mode=auto\ngameplay.mirror=false\ndev.x=\nold.key=1\n");
    CHECK(ico_state_header_check(&saved, &now, detail, sizeof detail) == ICO_STATE_OTHER_SETTINGS);
    CHECK(strcmp(detail, "old.key=1 (now absent)") == 0);
    saved = now;
    snprintf(saved.keys, sizeof saved.keys, "video.video_mode=auto\ngameplay.mirror=false\n");
    CHECK(ico_state_header_check(&saved, &now, detail, sizeof detail) == ICO_STATE_OTHER_SETTINGS);
    CHECK(strcmp(detail, "dev.x=(absent) (now )") == 0);

    /* the texts agree, the hash does not */
    saved = now;
    saved.settings_hash ^= 0x100;
    CHECK(ico_state_header_check(&saved, &now, detail, sizeof detail) == ICO_STATE_OTHER_SETTINGS);
    CHECK(detail[0] != '\0');
    /* no detail buffer */
    CHECK(ico_state_header_check(&saved, &now, NULL, 0) == ICO_STATE_OTHER_SETTINGS);
}

/* --- bad files ---------------------------------------------------------------- */

static unsigned char *file_bytes;
static long file_n;

static void load_file(void)
{
    FILE *f = ico_fopen(path, "rb");

    file_bytes = NULL;
    file_n = 0;
    if (f == NULL) {
        return;
    }
    fseek(f, 0, SEEK_END);
    file_n = ftell(f);
    fseek(f, 0, SEEK_SET);
    file_bytes = (unsigned char *)malloc((size_t)file_n + 1);
    if (file_bytes != NULL && fread(file_bytes, 1, (size_t)file_n, f) != (size_t)file_n) {
        file_n = 0;
    }
    fclose(f);
}

static void put_variant(const char *p, const unsigned char *bytes, long n)
{
    FILE *f = ico_fopen(p, "wb");

    if (f == NULL) {
        printf("FAIL cannot write %s\n", p);
        fails++;
        return;
    }
    if (n > 0) {
        fwrite(bytes, 1, (size_t)n, f);
    }
    fclose(f);
}

static IcoStateResult header_of(const unsigned char *bytes, long n)
{
    char p[700];
    IcoStateHeader h;

    snprintf(p, sizeof p, "%s/state_format_variant.ics", dir);
    put_variant(p, bytes, n);
    return ico_state_file_read_header(p, &h);
}

static IcoStateResult read_of(const unsigned char *bytes, long n)
{
    char p[700];

    snprintf(p, sizeof p, "%s/state_format_variant.ics", dir);
    put_variant(p, bytes, n);
    return ico_state_file_read(p, NULL, want_section, NULL);
}

static void test_bad_files(void)
{
    char p[700];
    IcoStateHeader h;
    unsigned char *v;
    IcoStateHeader r;

    snprintf(p, sizeof p, "%s/state_format_no_such_file.ics", dir);
    CHECK(ico_state_file_read_header(p, &h) == ICO_STATE_EMPTY);
    CHECK(ico_state_file_read(p, NULL, want_section, NULL) == ICO_STATE_EMPTY);

    load_file();
    CHECK(file_bytes != NULL && file_n > ICO_STATE_HEADER_BYTES);
    if (file_bytes == NULL || file_n <= ICO_STATE_HEADER_BYTES) {
        return;
    }
    CHECK(header_of(file_bytes, file_n) == ICO_STATE_OK);
    v = (unsigned char *)malloc((size_t)file_n + 1);
    if (v == NULL) {
        return;
    }
    /* the magic */
    memcpy(v, file_bytes, (size_t)file_n);
    v[0] = 'X';
    CHECK(header_of(v, file_n) == ICO_STATE_BAD_FILE);
    /* a stale version (the field at byte 8) */
    memcpy(v, file_bytes, (size_t)file_n);
    v[8] = (unsigned char)(ICO_STATE_FORMAT_VERSION + 1);
    CHECK(header_of(v, file_n) == ICO_STATE_BAD_FILE);
    v[8] = 0;
    CHECK(header_of(v, file_n) == ICO_STATE_BAD_FILE);
    /* a byte of the commit text: the CRC */
    memcpy(v, file_bytes, (size_t)file_n);
    v[40] ^= 0x20;
    CHECK(header_of(v, file_n) == ICO_STATE_BAD_FILE);
    /* a byte of the keys text, behind the table: the CRC too */
    memcpy(v, file_bytes, (size_t)file_n);
    v[ICO_STATE_HEADER_BYTES + 4 * ICO_STATE_TABLE_ENTRY_BYTES + 2] ^= 1;
    CHECK(header_of(v, file_n) == ICO_STATE_BAD_FILE);
    /* truncated by one byte, cut short, empty, one byte too long */
    memcpy(v, file_bytes, (size_t)file_n);
    CHECK(header_of(v, file_n - 1) == ICO_STATE_BAD_FILE);
    CHECK(header_of(v, 100) == ICO_STATE_BAD_FILE);
    CHECK(header_of(v, 0) == ICO_STATE_BAD_FILE);
    v[file_n] = 0;
    CHECK(header_of(v, file_n + 1) == ICO_STATE_BAD_FILE);
    /* a flipped byte in the first section's packed data: the header reads,
       the section does not */
    CHECK(ico_state_file_read_header(path, &r) == ICO_STATE_OK);
    memcpy(v, file_bytes, (size_t)file_n);
    v[r.sections[1].offset + r.sections[1].packed_bytes / 2] ^= 0x10;
    CHECK(header_of(v, file_n) == ICO_STATE_OK);
    CHECK(read_of(v, file_n) == ICO_STATE_BAD_FILE);
    /* and the unchanged copy reads */
    CHECK(read_of(file_bytes, file_n) == ICO_STATE_OK);
    snprintf(p, sizeof p, "%s/state_format_variant.ics", dir);
    remove(p);
    free(v);
    free(file_bytes);
}

/* --- refused and failed writes ------------------------------------------------- */

static void test_writes(void)
{
    IcoStateHeader h;
    IcoStateSection s[ICO_STATE_MAX_SECTIONS + 1];
    IcoStateWriteResult res;
    char p[700], tmp[720];
    int i;

    make_header(&h);
    for (i = 0; i <= ICO_STATE_MAX_SECTIONS; i++) {
        s[i].name = "S";
        s[i].data = small;
        s[i].bytes = 1;
        s[i].addr = 0;
        s[i].flags = 0;
    }
    CHECK(ico_state_file_write(path, &h, s, ICO_STATE_MAX_SECTIONS + 1) == ICO_STATE_IO);
    s[0].name = "SIXTEEN_LETTERS_";
    CHECK(ico_state_file_write(path, &h, s, 1) == ICO_STATE_IO);
    s[0].name = "";
    CHECK(ico_state_file_write(path, &h, s, 1) == ICO_STATE_IO);
    s[0].name = "S";
    s[0].data = NULL;
    CHECK(ico_state_file_write(path, &h, s, 1) == ICO_STATE_IO);
    CHECK(!ico_state_file_busy());

    s[0].data = small;
    snprintf(p, sizeof p, "%s/no_such_folder_state/slot1.ics", dir);
    snprintf(tmp, sizeof tmp, "%s.tmp", p);
    CHECK(write_now(p, &h, s, 1, &res) == ICO_STATE_IO);
    CHECK(res.why[0] != '\0');
    CHECK(ico_path_kind(tmp, NULL, NULL) < 0);
    CHECK(ico_path_kind(p, NULL, NULL) < 0);
    /* the writer is free again */
    CHECK(!ico_state_file_busy());
}

/* --- the build id ------------------------------------------------------------- */

static void test_build_id(void)
{
    uint8_t a[16], b[16], c[16];
    char text[2048];

    ico_state_registry_reset();
    CHECK(ico_state_register("test.a", reg_a, sizeof reg_a, ICO_STATE_SCAN) == 0);
    ico_state_build_id("commit-1", a, text, sizeof text);
    ico_state_build_id("commit-1", b, NULL, 0);
    CHECK(memcmp(a, b, 16) == 0);
    CHECK(strstr(text, "commit commit-1\n") != NULL);
    CHECK(strstr(text, "reg test.a 64 1\n") != NULL);
    CHECK(strstr(text, "platform ") != NULL);
    ico_state_build_id("commit-2", c, NULL, 0);
    CHECK(memcmp(a, c, 16) != 0);
    CHECK(ico_state_register("test.b", reg_b, sizeof reg_b, 0) == 0);
    ico_state_build_id("commit-1", c, NULL, 0);
    CHECK(memcmp(a, c, 16) != 0);
    /* the two halves differ (two hashes, not one repeated) */
    CHECK(memcmp(a, a + 8, 8) != 0);
    CHECK(strcmp(ico_state_platform_tag(), "unknown") != 0);
    ico_state_registry_reset();
}

int main(int argc, char **argv)
{
    snprintf(dir, sizeof dir, "%s", argc > 1 ? argv[1] : ".");
    snprintf(path, sizeof path, "%s/state_format_test.ics", dir);
    fill();
    test_build_id();
    test_round_trip();
    test_refusals();
    test_bad_files();
    test_writes();
    remove(path);
    if (fails != 0) {
        printf("state_format_test: %d failure(s)\n", fails);
        return 1;
    }
    printf("state_format_test: ok\n");
    return 0;
}
