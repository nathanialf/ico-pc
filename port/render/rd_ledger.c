/* rd_ledger.c: the scene ledger's tally, its text and the kept stages
 * (rd_ledger.h).  Pure: no renderer state, no allocation (the bug report's
 * crash path formats the stage in play through it). */
#include "rd_ledger.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *const kReasonWords[] = {
    "the game's box cull (whole object)",
    "the game's box cull (a node)",
    "the game's box cull (a part)",
    "its mesh could not be built",
    "a draw with no live mesh",
    "a batch range past its mesh",
    "photo mode's draw filter",
    "no frame open to record it",
    "the port's frustum test",
    "the port's occlusion test",
};

/* the mechanical half of the gate (tools/check_drop_ledger.py the other):
   a reason without words, or words without a reason, does not compile */
_Static_assert(sizeof(kReasonWords) / sizeof(kReasonWords[0]) == RD_LEDGER_REASONS,
               "every RdLedgerReason has its words in kReasonWords");

static const char *const kFxWords[] = {
    "fog",   "fade", "letterbox", "brightness", "film noise",    "motion blur",    "depth of field",
    "flare", "glow", "aura",      "eye blur",   "anti-aliasing", "shadow volumes",
};

_Static_assert(sizeof(kFxWords) / sizeof(kFxWords[0]) == RD_LEDGER_FX_COUNT,
               "every RdLedgerFx has its words in kFxWords");

/* the key value's word, by effect */
static const char *fxValueWord(int fx)
{
    switch (fx) {
    case RD_LEDGER_FX_FOG:
        return "strength";
    case RD_LEDGER_FX_FADE:
        return "alpha";
    case RD_LEDGER_FX_LETTERBOX:
        return "lines";
    case RD_LEDGER_FX_SHADOWS:
        return "triangles";
    default:
        return "FIX";
    }
}

const char *rd_ledger_reason_words(int reason)
{
    return reason >= 0 && reason < RD_LEDGER_REASONS ? kReasonWords[reason] : "?";
}

int rd_ledger_reason_by_picture(int reason)
{
    return reason == RD_LEDGER_FRUSTUM || reason == RD_LEDGER_OCCLUDED;
}

const char *rd_ledger_fx_words(int fx)
{
    return fx >= 0 && fx < RD_LEDGER_FX_COUNT ? kFxWords[fx] : "?";
}

/* --- the tally ----------------------------------------------------------------------- */

void rd_ledger_table_reset(RdLedgerTable *t)
{
    memset(t, 0, sizeof(*t));
}

static uint32_t nameHash(const char *s)
{
    uint32_t h = 0x811C9DC5u;
    for (uint32_t i = 0; i < RD_LEDGER_NAME - 1 && s[i] != '\0'; i++) {
        h ^= (unsigned char)s[i];
        h *= 0x01000193u;
    }
    return h;
}

int rd_ledger_slot(RdLedgerTable *t, const char *name)
{
    const char *nm = name != NULL && name[0] != '\0' ? name : "(no name)";
    char cut[RD_LEDGER_NAME];
    snprintf(cut, sizeof cut, "%s", nm);
    uint32_t at = nameHash(cut) & (RD_LEDGER_HASH - 1);
    for (uint32_t probe = 0; probe < RD_LEDGER_HASH; probe++) {
        const uint16_t h = t->hash[at];
        if (h == 0) {
            if (t->n >= RD_LEDGER_SLOTS) {
                t->pastNames++;
                return -1;
            }
            RdLedgerEntry *e = &t->e[t->n];
            memset(e, 0, sizeof(*e));
            memcpy(e->name, cut, sizeof cut);
            t->hash[at] = (uint16_t)(t->n + 1);
            return (int)t->n++;
        }
        if (strcmp(t->e[h - 1].name, cut) == 0) {
            return h - 1;
        }
        at = (at + 1) & (RD_LEDGER_HASH - 1);
    }
    t->pastNames++;
    return -1;
}

void rd_ledger_note_built(RdLedgerTable *t, int slot)
{
    if (slot >= 0 && (uint32_t)slot < t->n) {
        t->e[slot].built++;
    }
}

void rd_ledger_note_draw(RdLedgerTable *t, int slot, uint32_t tick, uint32_t tris)
{
    if (slot < 0 || (uint32_t)slot >= t->n) {
        return;
    }
    RdLedgerEntry *e = &t->e[slot];
    if (e->ticks == 0 || e->lastTick != tick) {
        e->ticks++;
        e->lastTick = tick;
    }
    e->draws++;
    e->tris += tris;
}

void rd_ledger_note_drop(RdLedgerTable *t, int slot, int reason, uint32_t pic, int alt, uint32_t n)
{
    if (slot < 0 || (uint32_t)slot >= t->n || reason < 0 || reason >= RD_LEDGER_REASONS) {
        return;
    }
    RdLedgerEntry *e = &t->e[slot];
    if (e->pics[reason] == 0 || e->lastPic[reason] != pic) {
        e->pics[reason]++;
        e->lastPic[reason] = pic;
        if (alt) {
            e->alt[reason]++;
        }
    }
    e->drops[reason] += n;
}

void rd_ledger_table_add(RdLedgerTable *dst, const RdLedgerTable *src)
{
    for (uint32_t i = 0; i < src->n; i++) {
        const RdLedgerEntry *s = &src->e[i];
        const int k = rd_ledger_slot(dst, s->name);
        if (k < 0) {
            continue;
        }
        RdLedgerEntry *d = &dst->e[k];
        d->built += s->built;
        d->carried |= s->carried;
        d->ticks += s->ticks;
        d->draws += s->draws;
        d->tris += s->tris;
        for (int r = 0; r < RD_LEDGER_REASONS; r++) {
            d->drops[r] += s->drops[r];
            d->pics[r] += s->pics[r];
            d->alt[r] += s->alt[r];
        }
    }
    dst->pastNames += src->pastNames;
}

void rd_ledger_stage_begin(RdLedgerStage *s, int stage, const char *name, uint32_t firstTick)
{
    memset(s, 0, sizeof(*s));
    s->stage = stage;
    snprintf(s->name, sizeof s->name, "%s", name != NULL ? name : "");
    s->firstTick = firstTick;
    s->lastTick = firstTick > 0 ? firstTick - 1 : 0;
}

void rd_ledger_note_fx(RdLedgerStage *s, int fx, uint32_t tick, float value)
{
    if (fx < 0 || fx >= RD_LEDGER_FX_COUNT) {
        return;
    }
    RdLedgerFxRow *r = &s->fx[fx];
    if (r->ticks == 0 || r->lastTick != tick) {
        r->ticks++;
        r->lastTick = tick;
    }
    r->value = value;
}

static uint32_t entryDropPics(const RdLedgerEntry *e)
{
    uint32_t n = 0;
    for (int r = 0; r < RD_LEDGER_REASONS; r++) {
        n += e->pics[r];
    }
    return n;
}

void rd_ledger_carry(RdLedgerStage *next, const RdLedgerStage *prev)
{
    for (uint32_t i = 0; i < prev->models.n; i++) {
        const RdLedgerEntry *e = &prev->models.e[i];
        if ((e->built > 0 || e->carried) && e->draws == 0 && entryDropPics(e) == 0) {
            const int k = rd_ledger_slot(&next->models, e->name);
            if (k >= 0) {
                next->models.e[k].carried = 1;
            }
        }
    }
}

/* --- the text ------------------------------------------------------------------------ */

typedef struct Out {
    char *p;
    size_t cap, at;
} Out;

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
static void put(Out *o, const char *fmt, ...)
{
    if (o->cap == 0 || o->at >= o->cap - 1) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    const int k = vsnprintf(o->p + o->at, o->cap - o->at, fmt, ap);
    va_end(ap);
    if (k > 0) {
        o->at = o->at + (size_t)k < o->cap ? o->at + (size_t)k : o->cap - 1;
    }
}

/* a row's rank: rows left out or never drawn first (most pictures left
   out first), then the drawn ones by draws */
static uint64_t rowRank(const RdLedgerEntry *e)
{
    const uint32_t dropped = entryDropPics(e);
    if (e->draws == 0) {
        return (3ull << 62) | dropped;
    }
    if (dropped > 0) {
        return (2ull << 62) | dropped;
    }
    return e->draws;
}

static void stageLabel(const RdLedgerStage *s, char *buf, size_t n)
{
    if (s->name[0] != '\0') {
        snprintf(buf, n, "stage %d (%s)", s->stage, s->name);
    } else {
        snprintf(buf, n, "stage %d", s->stage);
    }
}

static void putRow(Out *o, const RdLedgerEntry *e, uint32_t ticks)
{
    put(o, "%s:", e->name);
    if (e->draws > 0) {
        put(o, " drawn in %u of %u ticks (%u draws, %llu triangles)", e->ticks, ticks, e->draws,
            (unsigned long long)e->tris);
    } else if (entryDropPics(e) == 0) {
        put(o, " never drawn, no reason recorded");
    } else {
        put(o, " never drawn");
    }
    if (e->built > 0) {
        put(o, ", %u mesh%s built", e->built, e->built == 1 ? "" : "es");
    }
    if (e->carried) {
        put(o, ", built before the stage began");
    }
    for (int r = 0; r < RD_LEDGER_REASONS; r++) {
        if (e->pics[r] == 0) {
            continue;
        }
        const int byPic = rd_ledger_reason_by_picture(r);
        put(o, "; left out by %s in %u %s (%u %s), %u %s", kReasonWords[r], e->pics[r],
            byPic ? "pictures" : "ticks", e->alt[r],
            byPic ? "between ticks" : "with the cameras between ticks", e->drops[r],
            r == RD_LEDGER_BOX_OBJECT ? "times" : "draws or parts");
    }
}

size_t rd_ledger_format(const RdLedgerStage *s, const char *when, char *out, size_t cap)
{
    Out o = {out, cap, 0};
    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    char label[80];
    stageLabel(s, label, sizeof label);
    const uint32_t ticks = s->lastTick >= s->firstTick ? s->lastTick - s->firstTick + 1 : 0;
    const RdLedgerTable *t = &s->models;
    uint32_t drawn = 0, leftOut = 0, never = 0, silent = 0;
    for (uint32_t i = 0; i < t->n; i++) {
        const RdLedgerEntry *e = &t->e[i];
        const uint32_t d = entryDropPics(e);
        drawn += e->draws > 0;
        leftOut += d > 0;
        never += e->draws == 0;
        silent += e->draws == 0 && d == 0;
    }
    put(&o,
        "scene ledger: %s %s: ticks %u-%u (%u), %u pictures (%u between ticks); %u models: %u "
        "drawn, %u left out at times, %u never drawn (%u with no reason recorded); names past "
        "the table %u; the replay's events of the stage before %u\n",
        label, when != NULL ? when : "ended", s->firstTick, s->lastTick, ticks, s->pictures,
        s->between, t->n, drawn, leftOut, never, silent, t->pastNames, s->lateEvents);
    put(&o, "scene ledger: %s effects:", label);
    {
        const char *sep = " ";
        for (int fx = 0; fx < RD_LEDGER_FX_COUNT; fx++) {
            if (s->fx[fx].ticks == 0) {
                continue;
            }
            put(&o, "%s%s %u ticks (last %s %g)", sep, kFxWords[fx], s->fx[fx].ticks,
                fxValueWord(fx), (double)s->fx[fx].value);
            sep = ", ";
        }
        if (sep[0] == ' ') {
            put(&o, " none");
        }
    }
    put(&o, "\n");
    /* the rows by rank: an insertion sort of their indices (no
       allocation: the crash path formats through here) */
    uint16_t order[RD_LEDGER_SLOTS];
    for (uint32_t i = 0; i < t->n; i++) {
        uint32_t j = i;
        const uint64_t ri = rowRank(&t->e[i]);
        while (j > 0 && rowRank(&t->e[order[j - 1]]) < ri) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = (uint16_t)i;
    }
    const uint32_t rows = t->n < RD_LEDGER_ROWS ? t->n : RD_LEDGER_ROWS;
    uint32_t written = 0;
    while (written < rows) {
        /* a line of rows, about 1200 bytes, so the log's line stays whole */
        const size_t lineAt = o.at;
        put(&o, "scene ledger: %s models:", label);
        const char *sep = " ";
        while (written < rows && o.at - lineAt < 1200) {
            put(&o, "%s", sep);
            putRow(&o, &t->e[order[written]], ticks);
            sep = "; ";
            written++;
        }
        put(&o, "\n");
        if (o.at >= cap - 1) {
            break;
        }
    }
    if (t->n > written) {
        uint32_t quiet = 0;
        for (uint32_t i = written; i < t->n; i++) {
            const RdLedgerEntry *e = &t->e[order[i]];
            quiet += e->draws > 0 && entryDropPics(e) == 0;
        }
        put(&o,
            "scene ledger: %s: %u more models not listed (%u of them drawn and never left out)\n",
            label, t->n - written, quiet);
    }
    return o.at;
}

/* --- the kept stages ---------------------------------------------------------------- */

void rd_ledger_keep(RdLedgerKept *k, const char *text)
{
    snprintf(k->text[k->head], sizeof k->text[0], "%s", text != NULL ? text : "");
    k->head = (k->head + 1) % RD_LEDGER_KEEP;
    if (k->n < RD_LEDGER_KEEP) {
        k->n++;
    }
}

size_t rd_ledger_kept_text(const RdLedgerKept *k, const char *extra, char *out, size_t cap)
{
    Out o = {out, cap, 0};
    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    put(&o,
        "The scene ledger: what each stage contained and what was left out of its pictures, "
        "the last %u stages played, oldest first, then the stage in play.\n",
        k->n);
    for (uint32_t i = 0; i < k->n; i++) {
        const uint32_t at = (k->head + RD_LEDGER_KEEP - k->n + i) % RD_LEDGER_KEEP;
        put(&o, "\n%s", k->text[at]);
    }
    if (extra != NULL && extra[0] != '\0') {
        put(&o, "\n%s", extra);
    }
    return o.at;
}
