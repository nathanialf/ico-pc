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

    for (int i = 0; i < RD_INAMES_NAME - 1 && s[i] != '\0'; i++) {
        h = (h ^ (uint8_t)s[i]) * 16777619u;
    }
    return h;
}

uint32_t rd_inames_name_hash(const char *name)
{
    return name_hash(name != NULL && name[0] != '\0' ? name : "a draw");
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

void rd_inames_scroll(RdInameTally *t, const char *name, uint32_t n)
{
    const char *nm = name != NULL && name[0] != '\0' ? name : "a draw";
    char key[RD_INAMES_NAME];

    (void)snprintf(key, sizeof(key), "%s", nm);
    const uint32_t h = name_hash(key);

    for (uint32_t i = 0; i < RD_INAMES_SLOTS; i++) {
        RdInameSlot *s = &t->s[(h + i) % RD_INAMES_SLOTS];

        if (!s->used) {
            return;
        }
        if (strcmp(s->name, key) == 0) {
            s->scroll += n;
            return;
        }
    }
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

int rd_inames_text(const RdInameTally *t, uint32_t frames, uint32_t presents,
                   const RdInameStairs *cad, char *out, size_t cap)
{
    Out o = {out, cap, 0, 0};

    if (cap > 0) {
        out[0] = '\0';
    }
    put(&o,
        "interp names: draws left at their tick's position over %u frames (%u presents; counted "
        "at each frame's first present)",
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
            if (w == RD_INAME_UNMATCHED && t->s[best].scroll != 0) {
                put(&o, " [scroll %u]", t->s[best].scroll);
            }
            if (w == RD_INAME_STAIR && cad != NULL) {
                uint32_t judged, moved, alt;

                rd_inames_cadence(cad, name_hash(t->s[best].name), &judged, &moved, &alt);
                put(&o, " [moved %u of %u, alternating %u]", moved, judged, alt);
            }
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

static void count(RdInameKey *e, unsigned v)
{
    e->judged += e->judged < UINT16_MAX ? 1u : 0u;
    e->moved += (v & RD_INAMES_V_MOVED) && e->moved < UINT16_MAX ? 1u : 0u;
    e->alt += (v & RD_INAMES_V_ALT) && e->alt < UINT16_MAX ? 1u : 0u;
}

unsigned rd_inames_stair(RdInameStairs *s, uint64_t key, uint32_t nameHash, uint32_t tick,
                         uint32_t prevTick, bool moved)
{
    if (tick == 0) {
        return 0u;
    }
    if (s->used * 4u >= RD_INAMES_KEYS * 3u) {
        rd_inames_stairs_reset(s);
    }
    uint64_t h = key * 0x9E3779B97F4A7C15ull;
    uint32_t at = (uint32_t)(h >> 52) & (RD_INAMES_KEYS - 1u);

    for (uint32_t i = 0; i < RD_INAMES_KEYS; i++, at = (at + 1u) & (RD_INAMES_KEYS - 1u)) {
        RdInameKey *e = &s->k[at];

        if (e->tick == 0) {
            memset(e, 0, sizeof(*e));
            e->key = key;
            e->tick = tick;
            e->nameHash = nameHash;
            s->used++;
            return 0u; /* the first sight: nothing to judge against */
        }
        if (e->key != key) {
            continue;
        }
        /* one verdict a tick (a key drawn twice in a frame counts once) */
        if (e->tick == tick) {
            return 0u;
        }
        e->nameHash = nameHash;
        if (e->tick != prevTick) {
            /* not paired with the tick before's: a run broken, not judged */
            e->tick = tick;
            e->run = 0;
            e->hist = 0;
            return 0u;
        }
        const bool movedBefore = e->run > 0 && (e->hist & 1u) != 0;
        unsigned v = RD_INAMES_V_JUDGED | (moved ? RD_INAMES_V_MOVED : 0u);

        e->hist = (uint8_t)((e->hist << 1) | (moved ? 1u : 0u));
        e->run = e->run < 8u ? (uint8_t)(e->run + 1u) : 8u;
        e->tick = tick;
        if (!moved && movedBefore) {
            v |= RD_INAMES_V_STEP;
        }
        if (e->run >= 4u && ((e->hist & 0xFu) == 0x5u || (e->hist & 0xFu) == 0xAu)) {
            v |= RD_INAMES_V_ALT;
        }
        count(e, v);
        return v;
    }
    return 0u;
}

void rd_inames_cadence(const RdInameStairs *s, uint32_t nameHash, uint32_t *judged, uint32_t *moved,
                       uint32_t *alt)
{
    uint32_t j = 0, m = 0, a = 0;

    for (uint32_t i = 0; i < RD_INAMES_KEYS; i++) {
        const RdInameKey *e = &s->k[i];

        if (e->tick != 0 && e->nameHash == nameHash) {
            j += e->judged;
            m += e->moved;
            a += e->alt;
        }
    }
    *judged = j;
    *moved = m;
    *alt = a;
}

void rd_inames_stairs_window(RdInameStairs *s)
{
    for (uint32_t i = 0; i < RD_INAMES_KEYS; i++) {
        s->k[i].judged = s->k[i].moved = s->k[i].alt = 0;
    }
}

bool rd_inames_carry(RdInameCarry *k, const float (*mp)[4], const float (*mc)[4])
{
    if (!k->have || memcmp(k->vp, &mp[4][0], sizeof(k->vp)) != 0 ||
        memcmp(k->vc, &mc[4][0], sizeof(k->vc)) != 0) {
        double a[16], inv[16];

        memcpy(k->vp, &mp[4][0], sizeof(k->vp));
        memcpy(k->vc, &mc[4][0], sizeof(k->vc));
        for (int i = 0; i < 16; i++) {
            a[i] = k->vp[i];
        }
        k->ok = rdcb_invert4d(a, inv) != 0;
        if (k->ok) {
            /* row r of t: row r of Vp^-1 times Vc (rows as the VU stores
               them: a point's screen place is sum_k p_k Vc[k]) */
            for (int r = 0; r < 4; r++) {
                for (int c = 0; c < 4; c++) {
                    double v = 0.0;

                    for (int q = 0; q < 4; q++) {
                        v += inv[r * 4 + q] * (double)k->vc[q * 4 + c];
                    }
                    k->t[r * 4 + c] = v;
                }
            }
        }
        k->have = true;
    }
    return k->ok;
}

bool rd_inames_moved(const double t[16], const float (*mp)[4], const float (*mc)[4])
{
    for (int q = 16; q < 20; q++) {
        double p[4];
        double m = 0.0;

        for (int c = 0; c < 4; c++) {
            double v = 0.0;

            for (int r = 0; r < 4; r++) {
                v += (double)mp[q][r] * t[r * 4 + c];
            }
            p[c] = v;
        }
        /* X, Y and W (lane 2, the GS Z, left out), as stillRows reads them */
        for (int c = 0; c < 4; c++) {
            if (c != 2) {
                m = fmax(m, fmax(fabs(p[c]), fabs((double)mc[q][c])));
            }
        }
        for (int c = 0; c < 4; c++) {
            if (c != 2 && !(fabs(p[c] - (double)mc[q][c]) <= m * RD_INAMES_STILL_REL)) {
                return true;
            }
        }
    }
    return false;
}
