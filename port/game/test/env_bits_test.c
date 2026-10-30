/*
 * port/game/test/env_bits_test.c
 *
 * Where each disc's ACTGetEnvironment reads the two-bit wall-reach field of a
 * motion record (modeBits, `switch (modeBits.word << N >> 30)` in
 * ico2/fumi/src/act-env.c).  The host's motion records hold that field at the
 * European bits 24-25 on every disc (config/motion_bits.<disc>.txt, the row
 * `MotionDef modeBits pal=24 width=2 <disc>=modeBits:<raw bit>`: the loader
 * moves the disc's own bits there), so every twin must shift by 6.  The June
 * 2001 demo's twin once read `<< 11` (the demo's raw bits 19-20), which on the
 * host are the item mode.
 *
 *   env_bits_test <source dir>
 *
 * Checks: each of the three twins and the European body in act-env.c reads
 * with a shift of 6; every config/motion_bits.*.txt names the field at pal=24
 * width=2; a synthetic word with 24-25 set reads as the field and one with
 * only 19-20 set reads as 0 through that shift.  CPU only.
 */
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            failures++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

/* the read as the game writes it */
static int env_field(unsigned word)
{
    return ((int)(word << 6)) >> 30;
}

static char *slurp(const char *path, size_t *n)
{
    static char buf[1 << 20];
    FILE *f = fopen(path, "rb");

    if (!f) {
        return NULL;
    }
    *n = fread(buf, 1, sizeof buf - 1, f);
    buf[*n] = 0;
    fclose(f);
    return buf;
}

int main(int argc, char **argv)
{
    static const char *const discs[] = {"ntsc", "aug6", "jun26", "cn", "jpn", "kor"};
    char path[1024];
    size_t n;
    char *src;
    const char *p;
    int reads = 0;
    unsigned i;

    if (argc < 2) {
        printf("usage: env_bits_test <source dir>\n");
        return 2;
    }
    CHECK(env_field(3u << 24) == -1, "bits 24-25 set read %d", env_field(3u << 24));
    CHECK(env_field(1u << 24) == 1, "bit 24 set read %d", env_field(1u << 24));
    CHECK(env_field(3u << 19) == 0, "bits 19-20 set read %d", env_field(3u << 19));

    snprintf(path, sizeof path, "%s/ico2/fumi/src/act-env.c", argv[1]);
    src = slurp(path, &n);
    CHECK(src != NULL, "cannot read %s", path);
    for (p = src; src && (p = strstr(p, "modeBits.word << ")) != NULL; p++) {
        int shift = 0;

        if (sscanf(p, "modeBits.word << %d) >> 30", &shift) == 1) {
            reads++;
            CHECK(shift == 6, "act-env.c reads the field with << %d (host bits 24-25 need << 6)",
                  shift);
        }
    }
    /* the NTSC, June 2001, August 2001 and European bodies */
    CHECK(src == NULL || reads == 4, "act-env.c has %d field reads, expected 4", reads);

    for (i = 0; i < sizeof discs / sizeof discs[0]; i++) {
        char row[64];

        snprintf(path, sizeof path, "%s/config/motion_bits.%s.txt", argv[1], discs[i]);
        src = slurp(path, &n);
        CHECK(src != NULL, "cannot read %s", path);
        snprintf(row, sizeof row, "MotionDef modeBits pal=24 width=2 %s=modeBits:", discs[i]);
        CHECK(src == NULL || strstr(src, row) != NULL, "%s has no pal=24 width=2 modeBits row",
              discs[i]);
    }
    if (failures == 0) {
        printf("env_bits: ok\n");
    }
    return failures != 0;
}
