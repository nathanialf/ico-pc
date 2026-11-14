/* rd_inames.c: the draws left at their tick's position, by name, and the
 * stair-steppers (rd_inames.h).  Pure. */
#include "rd_inames.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "rd_camera_blend.h"

static const char *const k_why[RD_INAME_COUNT] = {
    "unmatched", "mismatched",
    "jumped",    "turn snapped",
    "unkeyed",   "stair-steppers (matched, still this tick after moving the tick before)",
};

void rd_inames_reset(RdInameTally *t)
{
    memset(t, 0, sizeof(*t));
}

static uint32_t name_hash(const char *s)
{
    uint32_t h = 2166136261u;

    for (int i = 0; i < RD_INAMES_NAME && s[i] != '\0'; i++) {
        h = (h ^ (uint8_t)s[i]) * 16777619u;
    }
    return h;
}

void rd_inames_add(RdInameTally *t, const char *name, int why, uint32_t n)
{
    if (why < 0 || why >= RD_INAME_COUNT || n == 0) {
        return;
    }
    const char *nm = name != NULL && name[0] != '\0' ? name : "a draw";
    char key[RD_INAMES_NAME];

    (void)snprintf(key, sizeof(key), "%s", nm);
    t->total[why] += n;
    const uint32_t h = name_hash(key);

    for (uint32_t i = 0; i < RD_INAMES_SLOTS; i++) {
        RdInameSlot *s = &t->s[(h + i) % RD_INAMES_SLOTS];

        if (!s->used) {
            s->used = true;
            memcpy(s->name, key, sizeof(key));
            s->n[why] = n;
            return;
        }
        if (strcmp(s->name, key) == 0) {
            s->n[why] += n;
            return;
        }
    }
    t->lost += n;
}

typedef struct Out {
    char *buf;
    size_t cap, len;
    int total;
} Out;

static void put(Out *o, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    const int n = vsnprintf(o->len < o->cap ? o->buf + o->len : NULL,
                            o->len < o->cap ? o->cap - o->len : 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    o->total += n;
    o->len = o->len + (size_t)n < o->cap ? o->len + (size_t)n : o->cap;
}

int rd_inames_text(const RdInameTally *t, uint32_t frames, uint32_t presents, char *out, size_t cap)
{
    Out o = {out, cap, 0, 0};

    if (cap > 0) {
        out[0] = '\0';
    }
    put(&o, "interp names: draws left at their tick's position over %u frames (%u presents)",
        frames, presents);
    for (int w = 0; w < RD_INAME_COUNT; w++) {
        bool shown[RD_INAMES_SLOTS];

        memset(shown, 0, sizeof(shown));
        put(&o, "%s %s %llu", w == 0 ? ":" : ";", k_why[w], (unsigned long long)t->total[w]);
        /* the top RD_INAMES_TOP by count, the table's order among equals */
        int named = 0;

        for (; named < RD_INAMES_TOP; named++) {
            int best = -1;

            for (int i = 0; i < RD_INAMES_SLOTS; i++) {
                if (t->s[i].used && !shown[i] && t->s[i].n[w] != 0 &&
                    (best < 0 || t->s[i].n[w] > t->s[best].n[w])) {
                    best = i;
                }
            }
            if (best < 0) {
                break;
            }
            shown[best] = true;
            put(&o, "%s%s %u", named == 0 ? " (" : ", ", t->s[best].name, t->s[best].n[w]);
        }
        if (named > 0) {
            put(&o, ")");
        }
    }
    put(&o, "; names past the table %llu", (unsigned long long)t->lost);
    return o.total;
}

/* --- the stair-steppers ----------------------------------------------------- */

void rd_inames_stairs_reset(RdInameStairs *s)
{
    memset(s, 0, sizeof(*s));
}

bool rd_inames_stair(RdInameStairs *s, uint64_t key, uint32_t tick, uint32_t prevTick, bool moved)
{
    if (tick == 0) {
        return false;
    }
    if (s->used * 4u >= RD_INAMES_KEYS * 3u) {
        rd_inames_stairs_reset(s);
    }
    uint64_t h = key * 0x9E3779B97F4A7C15ull;
    uint32_t at = (uint32_t)(h >> 52) & (RD_INAMES_KEYS - 1u);

    for (uint32_t i = 0; i < RD_INAMES_KEYS; i++, at = (at + 1u) & (RD_INAMES_KEYS - 1u)) {
        RdInameKey *e = &s->k[at];

        if (e->tick == 0) {
            e->key = key;
            e->tick = tick;
            e->moved = moved;
            s->used++;
            return false;
        }
        if (e->key == key) {
            /* one verdict a tick (a key drawn twice in a frame counts once) */
            if (e->tick == tick) {
                return false;
            }
            const bool step = !moved && e->moved && e->tick == prevTick;

            e->tick = tick;
            e->moved = moved;
            return step;
        }
    }
    return false;
}

/* the origin through the view's inverse inv */
static bool origin_by(const double *inv, const float (*m)[4], float out[3])
{
    double w[4] = {0.0, 0.0, 0.0, 0.0};

    for (int r = 0; r < 4; r++) {
        for (int k = 0; k < 4; k++) {
            w[r] += inv[k * 4 + r] * m[19][k];
        }
    }
    if (!(fabs(w[3]) > 1e-12)) {
        return false;
    }
    for (int j = 0; j < 3; j++) {
        out[j] = (float)(w[j] / w[3]);
    }
    return isfinite(out[0]) && isfinite(out[1]) && isfinite(out[2]);
}

bool rd_inames_origin(const float (*m)[4], float out[3])
{
    double a[16], inv[16];

    for (int i = 0; i < 16; i++) {
        a[i] = m[4 + i / 4][i % 4];
    }
    if (!rdcb_invert4d(a, inv)) {
        return false;
    }
    return origin_by(inv, m, out);
}

bool rd_inames_origin_kept(RdInameView *k, const float (*m)[4], float out[3])
{
    if (!k->have || memcmp(k->v, &m[4][0], sizeof(k->v)) != 0) {
        double a[16];

        memcpy(k->v, &m[4][0], sizeof(k->v));
        for (int i = 0; i < 16; i++) {
            a[i] = k->v[i];
        }
        k->ok = rdcb_invert4d(a, k->inv) != 0;
        k->have = true;
    }
    return k->ok && origin_by(k->inv, m, out);
}

bool rd_inames_moved(const float a[3], const float b[3])
{
    double size = 1.0;

    for (int j = 0; j < 3; j++) {
        size = fmax(size, fmax(fabs((double)a[j]), fabs((double)b[j])));
    }
    for (int j = 0; j < 3; j++) {
        if (fabs((double)a[j] - (double)b[j]) > 1e-6 * size) {
            return true;
        }
    }
    return false;
}
