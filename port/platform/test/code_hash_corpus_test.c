/*
 * port/platform/test/code_hash_corpus_test.c
 *
 * The arm64 code hash's C twin (code_hash.c) against its Python twin
 * (tools/code_hash_a64.py) on the corpus both read
 * (tools/test/code_hash_a64_corpus.txt, the argument): real functions of
 * two Android builds of one game source (v0.11.15 and v0.11.16-rc8: the
 * bird's brain thread with the linker's erratum 843419 patch site in one
 * build, the four functions whose page registers the rules 1 and 2 decoder
 * lost), the stubs their patch sites lead to, and variants (another stub
 * instruction, a stub that does not branch back, no stub, the site's ADRP
 * off page offset 0xff8 / 0xffc, another stack slot, a real write of a page
 * register).  Each one's hash under rules 1, 2 and 3 is the file's (made by
 * the Python twin), each pair the file names hashes alike or apart under
 * the rules it names, and each instruction's written registers are the
 * file's under rules 3 and under rules 1 and 2.  No build output, no game.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../test/ico_check.h"
#include "code_hash.h"

#define MAX_ENT 64
#define MAX_STUB 8

typedef struct Ent {
    char name[48];
    uint64_t addr;
    uint32_t *w;
    size_t n, cap;
    uint64_t stub[MAX_STUB];
    uint32_t stub_w[MAX_STUB][2];
    int n_stub;
    uint64_t want[3];
    uint64_t got[3];
} Ent;

static Ent s_ent[MAX_ENT];
static int s_n;

static Ent *find(const char *name)
{
    int i;

    for (i = 0; i < s_n; i++) {
        if (strcmp(s_ent[i].name, name) == 0) {
            return &s_ent[i];
        }
    }
    return NULL;
}

static int push(Ent *e, uint32_t w)
{
    if (e->n == e->cap) {
        const size_t c = e->cap ? e->cap * 2 : 256;
        uint32_t *q = (uint32_t *)realloc(e->w, c * sizeof *q);

        if (q == NULL) {
            return -1;
        }
        e->w = q;
        e->cap = c;
    }
    e->w[e->n++] = w;
    return 0;
}

/* the entry's hashes under rules 1 to 3, its stubs as the code range
   (tools/code_hash_a64.py corpus_text: lowest to highest + 8, zeros between) */
static void hash_ent(Ent *e)
{
    uint8_t *code = (uint8_t *)malloc(e->n * 4 + 1), *text = NULL;
    IcoA64Text tx;
    size_t k;
    int r, i;

    if (code == NULL) {
        return;
    }
    for (k = 0; k < e->n; k++) {
        code[4 * k] = (uint8_t)e->w[k];
        code[4 * k + 1] = (uint8_t)(e->w[k] >> 8);
        code[4 * k + 2] = (uint8_t)(e->w[k] >> 16);
        code[4 * k + 3] = (uint8_t)(e->w[k] >> 24);
    }
    memset(&tx, 0, sizeof tx);
    if (e->n_stub > 0) {
        uint64_t lo = e->stub[0], hi = e->stub[0];

        for (i = 1; i < e->n_stub; i++) {
            lo = e->stub[i] < lo ? e->stub[i] : lo;
            hi = e->stub[i] > hi ? e->stub[i] : hi;
        }
        hi += 8;
        text = (uint8_t *)calloc(1, (size_t)(hi - lo));
        if (text == NULL) {
            free(code);
            return;
        }
        for (i = 0; i < e->n_stub; i++) {
            int j;

            for (j = 0; j < 8; j++) {
                text[e->stub[i] - lo + (uint64_t)j] =
                    (uint8_t)(e->stub_w[i][j / 4] >> (8 * (j % 4)));
            }
        }
        tx.bytes = text;
        tx.lo = lo;
        tx.hi = hi;
    }
    for (r = 1; r <= 3; r++) {
        e->got[r - 1] = ico_code_hash_text(r, ICO_CODE_A64, code, e->n * 4, e->addr, 0, 0,
                                           e->n_stub > 0 ? &tx : NULL);
    }
    free(text);
    free(code);
}

static unsigned long long hex(const char *s)
{
    return strtoull(s, NULL, 16);
}

int main(int argc, char **argv)
{
    FILE *f;
    char line[512];
    Ent *cur = NULL;
    int rels = 0, writes = 0, carried = 0, i, sites_b = -1, sites_a = -1;

    if (argc < 2 || (f = fopen(argv[1], "r")) == NULL) {
        fprintf(stderr,
                "code_hash_corpus: no corpus (argument: tools/test/code_hash_a64_corpus.txt)\n");
        return 1;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        char *t[16];
        int n = 0;
        char *p = strtok(line, " \t\r\n");

        while (p != NULL && n < 16 && p[0] != '#') {
            t[n++] = p;
            p = strtok(NULL, " \t\r\n");
        }
        if (n == 0) {
            continue;
        }
        if ((strcmp(t[0], "fn") == 0 && n == 3) || (strcmp(t[0], "from") == 0 && n == 4)) {
            const Ent *b = strcmp(t[0], "from") == 0 ? find(t[2]) : NULL;

            CHECK(s_n < MAX_ENT && (strcmp(t[0], "fn") == 0 || b != NULL), "corpus entry %s", t[1]);
            if (s_n >= MAX_ENT || (strcmp(t[0], "from") == 0 && b == NULL)) {
                break;
            }
            cur = &s_ent[s_n++];
            memset(cur, 0, sizeof *cur);
            snprintf(cur->name, sizeof cur->name, "%s", t[1]);
            cur->addr = hex(t[n - 1]);
            if (b != NULL) {
                size_t k;

                for (k = 0; k < b->n; k++) {
                    if (push(cur, b->w[k]) != 0) {
                        return 1;
                    }
                }
                memcpy(cur->stub, b->stub, sizeof cur->stub);
                memcpy(cur->stub_w, b->stub_w, sizeof cur->stub_w);
                cur->n_stub = b->n_stub;
            }
        } else if (strcmp(t[0], "w") == 0 && cur != NULL) {
            for (i = 1; i < n; i++) {
                if (push(cur, (uint32_t)hex(t[i])) != 0) {
                    return 1;
                }
            }
        } else if (strcmp(t[0], "set") == 0 && cur != NULL && n == 3) {
            const unsigned long k = strtoul(t[1], NULL, 10);

            CHECK(k < cur->n, "%s: set %lu inside it", cur->name, k);
            if (k < cur->n) {
                cur->w[k] = (uint32_t)hex(t[2]);
            }
        } else if (strcmp(t[0], "stubs") == 0 && cur != NULL) {
            cur->n_stub = 0;
        } else if (strcmp(t[0], "stub") == 0 && cur != NULL && n == 4 && cur->n_stub < MAX_STUB) {
            cur->stub[cur->n_stub] = hex(t[1]);
            cur->stub_w[cur->n_stub][0] = (uint32_t)hex(t[2]);
            cur->stub_w[cur->n_stub][1] = (uint32_t)hex(t[3]);
            cur->n_stub++;
        } else if (strcmp(t[0], "hash") == 0 && cur != NULL && n == 4) {
            cur->want[0] = hex(t[1]);
            cur->want[1] = hex(t[2]);
            cur->want[2] = hex(t[3]);
            hash_ent(cur);
            CHECK(cur->got[0] == cur->want[0] && cur->got[1] == cur->want[1] &&
                      cur->got[2] == cur->want[2],
                  "%s: %016llx %016llx %016llx, the Python twin's %016llx %016llx %016llx",
                  cur->name, (unsigned long long)cur->got[0], (unsigned long long)cur->got[1],
                  (unsigned long long)cur->got[2], (unsigned long long)cur->want[0],
                  (unsigned long long)cur->want[1], (unsigned long long)cur->want[2]);
        } else if ((strcmp(t[0], "same") == 0 || strcmp(t[0], "differ") == 0) && n == 4) {
            const Ent *x = find(t[1]), *y = find(t[2]);
            const int r = atoi(t[3]);

            CHECK(x != NULL && y != NULL && r >= 1 && r <= 3, "relation %s %s", t[1], t[2]);
            if (x != NULL && y != NULL && r >= 1 && r <= 3) {
                CHECK((x->got[r - 1] == y->got[r - 1]) == (strcmp(t[0], "same") == 0),
                      "rules %d: %s and %s should be %s", r, t[1], t[2], t[0]);
                rels++;
            }
        } else if (strcmp(t[0], "writes") == 0 && n == 4) {
            const uint32_t w = (uint32_t)hex(t[1]);

            CHECK(ico_code_a64_writes_rules(3, w) == (uint32_t)hex(t[2]) &&
                      ico_code_a64_writes(w) == (uint32_t)hex(t[2]) &&
                      ico_code_a64_writes_rules(2, w) == (uint32_t)hex(t[3]) &&
                      ico_code_a64_writes_rules(1, w) == (uint32_t)hex(t[3]),
                  "writes %08x: %08x (rules 2: %08x), the file says %s %s", w,
                  ico_code_a64_writes_rules(3, w), ico_code_a64_writes_rules(2, w), t[2], t[3]);
            writes++;
        } else if (strcmp(t[0], "carried") == 0 && n == 4) {
            const Ent *x = find(t[1]);
            const unsigned long k = strtoul(t[2], NULL, 10);

            CHECK(x != NULL && k < x->n, "carried %s %s", t[1], t[2]);
            if (x != NULL && k < x->n) {
                uint32_t *ws = (uint32_t *)malloc(x->n * sizeof *ws);
                uint8_t st[MAX_STUB * 8 + 8];
                IcoA64Text tx;
                int got = -2;

                if (ws != NULL) {
                    memset(&tx, 0, sizeof tx);
                    if (x->n_stub == 1) { /* the corpus' patched functions hold one stub */
                        int j;

                        for (j = 0; j < 8; j++) {
                            st[j] = (uint8_t)(x->stub_w[0][j / 4] >> (8 * (j % 4)));
                        }
                        tx.bytes = st;
                        tx.lo = x->stub[0];
                        tx.hi = x->stub[0] + 8;
                    }
                    (void)ico_code_a64_unpatch(x->w, x->n, x->addr, x->n_stub == 1 ? &tx : NULL,
                                               ws);
                    got = ico_code_a64_page_carried(ws, x->n, (size_t)k);
                    free(ws);
                }
                CHECK(got == atoi(t[3]), "carried %s %lu: %d, the Python twin's %s", t[1], k, got,
                      t[3]);
                carried++;
            }
        } else {
            CHECK(0, "corpus line %s", t[0]);
        }
    }
    fclose(f);
    /* the bird's site: one in the rc8 copy, none in v0.11.15's */
    {
        const Ent *a = find("subBirdBrainMain_a"), *b = find("subBirdBrainMain_b");
        uint32_t *ws;

        CHECK(a != NULL && b != NULL && b->n_stub == 1, "the bird's two copies, rc8's stub");
        if (a != NULL && b != NULL && b->n_stub == 1 &&
            (ws = (uint32_t *)malloc(b->n * sizeof *ws)) != NULL) {
            uint8_t st[8];
            IcoA64Text tx;
            int j;

            for (j = 0; j < 8; j++) {
                st[j] = (uint8_t)(b->stub_w[0][j / 4] >> (8 * (j % 4)));
            }
            tx.bytes = st;
            tx.lo = b->stub[0];
            tx.hi = b->stub[0] + 8;
            sites_b = (int)ico_code_a64_unpatch(b->w, b->n, b->addr, &tx, ws);
            sites_a = (int)ico_code_a64_unpatch(a->w, a->n, a->addr, &tx, ws);
            free(ws);
        }
        CHECK(sites_b == 1 && sites_a == 0, "patch sites: rc8's bird %d (want 1), v0.11.15's %d",
              sites_b, sites_a);
    }
    CHECK(s_n >= 20 && rels >= 20 && writes >= 30,
          "the corpus: %d functions, %d relations, %d "
          "instructions",
          s_n, rels, writes);
    CHECK(carried >= 600, "the corpus: %d carry verdicts", carried);
    printf("code_hash_corpus: %d functions under rules 1 to 3, %d relations, %d instructions' "
           "writes, %d carry verdicts\n",
           s_n, rels, writes, carried);
    for (i = 0; i < s_n; i++) {
        free(s_ent[i].w);
    }
    return ico_check_summary("code_hash_corpus");
}
