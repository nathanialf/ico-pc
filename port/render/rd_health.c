/* rd_health.c: the picture-health guard's rule and its mark (rd_health.h
 * says what and why).  Pure: no globals, no device; the unit test is
 * port/render/test/rd_health_test.c. */
#include "rd_health.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_perf_clock.h" /* RD_RESET_*, rd_perf_reset_counted */

#define P(x) (1u << (x))

static const uint32_t k_groupParts[RD_HEALTH_GROUPS] = {
    P(RD_HEALTH_P_COMPACT),
    P(RD_HEALTH_P_REC),
    P(RD_HEALTH_P_DATE_ONE) | P(RD_HEALTH_P_FLAT0),
    P(RD_HEALTH_P_VOLUME_FS) | P(RD_HEALTH_P_COMP_SPEC) | P(RD_HEALTH_P_PASTE_SPEC),
    P(RD_HEALTH_P_EMPTY_SKIP) | P(RD_HEALTH_P_ROBUST),
};

/* switched in the run: every part but the flat outputs (their shaders are
   made with the renderer) and robust access (the device's) */
#define LIVE_PARTS                                                                                 \
    (P(RD_HEALTH_P_COMPACT) | P(RD_HEALTH_P_REC) | P(RD_HEALTH_P_DATE_ONE) |                       \
     P(RD_HEALTH_P_VOLUME_FS) | P(RD_HEALTH_P_COMP_SPEC) | P(RD_HEALTH_P_PASTE_SPEC) |             \
     P(RD_HEALTH_P_EMPTY_SKIP))

static const char *const k_partName[RD_HEALTH_PARTS] = {
    [RD_HEALTH_P_COMPACT] = "the index compaction",
    [RD_HEALTH_P_REC] = "the VU record sized from need",
    [RD_HEALTH_P_DATE_ONE] = "one DATE set-up",
    [RD_HEALTH_P_FLAT0] = "flat outputs at the provoking corner",
    [RD_HEALTH_P_VOLUME_FS] = "the shadow volumes by the stencil-only entry",
    [RD_HEALTH_P_COMP_SPEC] = "the shadow composites by their own entry",
    [RD_HEALTH_P_PASTE_SPEC] = "the glow's paste by its own entry",
    [RD_HEALTH_P_EMPTY_SKIP] = "empty composites skipped",
    [RD_HEALTH_P_ROBUST] = "robust access off",
};

uint32_t rd_health_group_parts(int g)
{
    return g >= 0 && g < RD_HEALTH_GROUPS ? k_groupParts[g] : 0u;
}

uint32_t rd_health_live_parts(void)
{
    return LIVE_PARTS;
}

char rd_health_group_letter(int g)
{
    return g >= 0 && g < RD_HEALTH_GROUPS ? (char)('a' + g) : '?';
}

const char *rd_health_part_name(int p)
{
    return p >= 0 && p < RD_HEALTH_PARTS ? k_partName[p] : "?";
}

uint32_t rd_health_groups_of(uint32_t parts)
{
    uint32_t g = 0;
    for (int i = 0; i < RD_HEALTH_GROUPS; i++) {
        if ((k_groupParts[i] & parts) != 0u) {
            g |= 1u << i;
        }
    }
    return g;
}

uint32_t rd_health_parts_of(uint32_t groups)
{
    uint32_t p = 0;
    for (int i = 0; i < RD_HEALTH_GROUPS; i++) {
        if ((groups >> i) & 1u) {
            p |= k_groupParts[i];
        }
    }
    return p;
}

void rd_health_letters(char *out, size_t size, uint32_t groups)
{
    size_t n = 0;
    if (size == 0) {
        return;
    }
    for (int i = 0; i < RD_HEALTH_GROUPS && n + 1 < size; i++) {
        if ((groups >> i) & 1u) {
            out[n++] = rd_health_group_letter(i);
        }
    }
    out[n] = '\0';
}

size_t rd_health_parts_text(char *out, size_t size, uint32_t parts)
{
    size_t at = 0;
    int left = 0;
    if (size == 0) {
        return 0;
    }
    out[0] = '\0';
    for (int p = 0; p < RD_HEALTH_PARTS; p++) {
        left += (parts >> p) & 1u;
    }
    for (int g = 0; g < RD_HEALTH_GROUPS; g++) {
        for (int p = 0; p < RD_HEALTH_PARTS; p++) {
            if (!((parts >> p) & 1u) || !((k_groupParts[g] >> p) & 1u)) {
                continue;
            }
            left--;
            const int w = snprintf(out + at, size - at, "%s(%c) %s",
                                   at == 0     ? ""
                                   : left == 0 ? " and "
                                               : ", ",
                                   rd_health_group_letter(g), k_partName[p]);
            if (w < 0) {
                return at;
            }
            at = at + (size_t)w < size ? at + (size_t)w : size - 1u;
        }
    }
    return at;
}

/* ------------------------------------------------------------ the rule */

static uint32_t popc(uint32_t v)
{
    uint32_t n = 0;
    for (; v != 0u; v &= v - 1u) {
        n++;
    }
    return n;
}

void rd_health_reset(RdHealth *h)
{
    memset(h, 0, sizeof(*h));
    h->phase = RD_HEALTH_WATCH;
}

int rd_health_pic(RdHealth *h, const RdHealthPic *p, RdHealthAct *out)
{
    memset(out, 0, sizeof(*out));
    if (!p->judged || h->phase == RD_HEALTH_DONE || p->replay < h->settleFrom) {
        return RD_HEALTH_NONE;
    }
    const bool bad = rd_perf_reset_counted(p->sign);
    h->win = (h->win << 1) | (bad ? 1u : 0u); /* 32 bits: the oldest drops out */
    h->winN = h->winN < RD_HEALTH_WINDOW ? h->winN + 1u : RD_HEALTH_WINDOW;
    const uint32_t fails = popc(h->win);
    out->fails = fails;
    out->judged = h->winN;
    if (fails >= RD_HEALTH_TRIGGER) {
        out->kind = RD_HEALTH_TRIGGER_HIT;
        return out->kind;
    }
    if (h->phase == RD_HEALTH_AFTER) {
        h->clean = fails <= RD_HEALTH_CLEAN_FAILS ? h->clean + 1u : 0u;
        if (h->clean >= RD_HEALTH_CLEAN) {
            out->kind = RD_HEALTH_RECOVERED;
            out->culprit = h->lastGroups;
            out->fromStart = h->fromStart;
            h->culprit = h->lastGroups;
            h->phase = RD_HEALTH_WATCH;
            h->clean = 0;
            h->fromStart = false;
            return out->kind;
        }
    }
    return RD_HEALTH_NONE;
}

void rd_health_take(RdHealth *h, uint32_t applicable, uint32_t settleFrom, RdHealthAct *out)
{
    uint32_t groups = 0, live = 0, next = 0, liveGroup = 0;
    while (h->nextGroup < RD_HEALTH_GROUPS) {
        const int g = h->nextGroup++;
        const uint32_t parts = applicable & ~h->off & k_groupParts[g];
        if (parts == 0u) {
            continue; /* none of its parts on here: passed over */
        }
        groups |= 1u << g;
        h->off |= parts;
        live |= parts & LIVE_PARTS;
        next |= parts & ~LIVE_PARTS;
        if ((parts & ~LIVE_PARTS) != 0u) {
            h->pendingGroups |= 1u << g;
        }
        if ((parts & LIVE_PARTS) != 0u) {
            liveGroup = 1u << g;
            break; /* watched before the next */
        }
    }
    h->groups |= groups;
    out->groups = groups;
    out->live = live;
    out->next = next;
    h->win = 0;
    h->winN = 0;
    h->clean = 0;
    h->settleFrom = settleFrom;
    if (live != 0u) {
        out->kind = RD_HEALTH_STEP;
        h->phase = RD_HEALTH_AFTER;
        h->lastGroups = liveGroup;
        h->fromStart = false;
        h->steps++;
        return;
    }
    out->kind = RD_HEALTH_EXHAUSTED;
    out->pendingGroups = h->pendingGroups;
    h->phase = RD_HEALTH_DONE;
}

/* ------------------------------------------------------------ the mark */

static const char k_markHead[] = "picture health ";

int rd_health_mark_format(char *value, size_t valueSize, char *by, size_t bySize,
                          const RdHealthMark *m)
{
    char letters[8], named[8];
    rd_health_letters(letters, sizeof letters, m->groups);
    rd_health_letters(named, sizeof named, m->named);
    if (valueSize == 0 || bySize == 0) {
        return -1;
    }
    const int a = snprintf(value, valueSize, "%s", letters);
    const char *state = m->state == RD_HEALTH_MARK_CULPRIT     ? "culprit "
                        : m->state == RD_HEALTH_MARK_PENDING   ? "pending "
                        : m->state == RD_HEALTH_MARK_EXHAUSTED ? "exhausted"
                                                               : "stepping";
    const bool withNamed = m->state == RD_HEALTH_MARK_CULPRIT || m->state == RD_HEALTH_MARK_PENDING;
    const int b = snprintf(by, bySize, "%s%s; %s%s%s%s", k_markHead, m->version, state,
                           withNamed ? named : "", m->evidence[0] ? "; " : "", m->evidence);
    return a < 0 || (size_t)a >= valueSize || b < 0 || (size_t)b >= bySize ? -1 : 0;
}

/* letters a..e into groups bits; -1 another character or none */
static int lettersParse(const char *s, size_t n, uint32_t *groups)
{
    *groups = 0;
    if (n == 0) {
        return -1;
    }
    for (size_t i = 0; i < n; i++) {
        if (s[i] < 'a' || s[i] >= 'a' + RD_HEALTH_GROUPS) {
            return -1;
        }
        *groups |= 1u << (s[i] - 'a');
    }
    return 0;
}

int rd_health_mark_parse(const char *value, const char *by, RdHealthMark *m)
{
    memset(m, 0, sizeof(*m));
    if (value == NULL || by == NULL || lettersParse(value, strlen(value), &m->groups) != 0 ||
        strncmp(by, k_markHead, sizeof k_markHead - 1u) != 0) {
        return -1;
    }
    const char *v = by + sizeof k_markHead - 1u;
    const char *semi = strchr(v, ';');
    if (semi == NULL || semi == v || (size_t)(semi - v) >= sizeof m->version) {
        return -1;
    }
    memcpy(m->version, v, (size_t)(semi - v));
    m->version[semi - v] = '\0';
    const char *st = semi + 1;
    while (*st == ' ') {
        st++;
    }
    const size_t stLen = strcspn(st, ";");
    if (stLen == 8 && strncmp(st, "stepping", 8) == 0) {
        m->state = RD_HEALTH_MARK_STEPPING;
    } else if (stLen == 9 && strncmp(st, "exhausted", 9) == 0) {
        m->state = RD_HEALTH_MARK_EXHAUSTED;
    } else if (stLen > 8 && strncmp(st, "culprit ", 8) == 0) {
        m->state = RD_HEALTH_MARK_CULPRIT;
        if (lettersParse(st + 8, stLen - 8, &m->named) != 0) {
            return -1;
        }
    } else if (stLen > 8 && strncmp(st, "pending ", 8) == 0) {
        m->state = RD_HEALTH_MARK_PENDING;
        if (lettersParse(st + 8, stLen - 8, &m->named) != 0) {
            return -1;
        }
    } else {
        return -1;
    }
    if ((m->named & ~m->groups) != 0u) {
        return -1; /* a group named that is not off */
    }
    const char *ev = st + stLen;
    if (*ev == ';') {
        ev++;
        while (*ev == ' ') {
            ev++;
        }
        snprintf(m->evidence, sizeof m->evidence, "%s", ev);
    }
    return 0;
}

int rd_health_mark_start(const char *value, const char *by, const char *version, RdHealthMark *m)
{
    memset(m, 0, sizeof(*m));
    const bool hasValue = value != NULL && value[0] != '\0';
    const bool hasBy = by != NULL && by[0] != '\0';
    if (!hasValue && !hasBy) {
        return RD_HEALTH_START_NONE;
    }
    if (rd_health_mark_parse(value, by, m) != 0) {
        return RD_HEALTH_START_DROP_FORM;
    }
    if (version == NULL || strcmp(m->version, version) != 0) {
        return RD_HEALTH_START_DROP_VERSION;
    }
    return RD_HEALTH_START_KEEP;
}

/* the highest group of groups, -1 none */
static int topGroup(uint32_t groups)
{
    int top = -1;
    for (int i = 0; i < RD_HEALTH_GROUPS; i++) {
        if ((groups >> i) & 1u) {
            top = i;
        }
    }
    return top;
}

void rd_health_begin(RdHealth *h, const RdHealthMark *m)
{
    rd_health_reset(h);
    if (m == NULL || m->groups == 0u) {
        return;
    }
    h->groups = m->groups;
    h->off = rd_health_parts_of(m->groups);
    h->nextGroup = topGroup(m->groups) + 1;
    switch (m->state) {
    case RD_HEALTH_MARK_CULPRIT:
        h->culprit = m->named;
        break;
    case RD_HEALTH_MARK_EXHAUSTED:
        h->phase = RD_HEALTH_DONE;
        break;
    default:
        /* pending, or stepping (the last group taken not confirmed): the
           start's pictures confirm them */
        h->phase = RD_HEALTH_AFTER;
        h->fromStart = true;
        h->lastGroups = m->state == RD_HEALTH_MARK_PENDING && m->named != 0u
                            ? m->named
                            : 1u << topGroup(m->groups);
        break;
    }
}

void rd_health_mark_of(const RdHealth *h, const char *version, const char *evidence,
                       RdHealthMark *m)
{
    memset(m, 0, sizeof(*m));
    snprintf(m->version, sizeof m->version, "%s", version != NULL ? version : "");
    snprintf(m->evidence, sizeof m->evidence, "%s", evidence != NULL ? evidence : "");
    m->groups = h->groups;
    if (h->phase == RD_HEALTH_DONE && h->pendingGroups == 0u) {
        m->state = RD_HEALTH_MARK_EXHAUSTED;
    } else if (h->phase == RD_HEALTH_DONE) {
        m->state = RD_HEALTH_MARK_PENDING;
        m->named = h->pendingGroups;
    } else if (h->culprit != 0u && h->phase == RD_HEALTH_WATCH) {
        m->state = RD_HEALTH_MARK_CULPRIT;
        m->named = h->culprit;
    } else if (h->pendingGroups != 0u) {
        m->state = RD_HEALTH_MARK_PENDING;
        m->named = h->pendingGroups;
    } else if (h->fromStart && h->phase == RD_HEALTH_AFTER) {
        m->state = RD_HEALTH_MARK_PENDING;
        m->named = h->lastGroups;
    } else {
        m->state = RD_HEALTH_MARK_STEPPING;
    }
}
