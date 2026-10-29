/* rd_passring.c: the pass ring (rd_passring.h). */
#include "rd_passring.h"
#include <string.h>

static RdPassRec s_ring[RD_PASSRING_SIZE];
static uint64_t s_seq; /* entries ever; the newest is s_ring[(s_seq - 1) % SIZE] */
static RdPassReplay s_replays[RD_PASSRING_REPLAYS];
static uint32_t s_replayCount; /* ever */
static uint32_t s_curReplay, s_curFrame;
static uint8_t s_curKeep, s_curList = 255;
static uint8_t s_pipeHold; /* rd__passring_pipeline_hold */

static RdPassReplay *curReplay(void)
{
    return s_replayCount ? &s_replays[(s_replayCount - 1) % RD_PASSRING_REPLAYS] : NULL;
}

void rd__passring_frame(uint32_t replay, uint32_t frame, int keep)
{
    s_curReplay = replay;
    s_curFrame = frame;
    s_curKeep = keep ? 1u : 0u;
    s_curList = 255;
    RdPassReplay *r = &s_replays[s_replayCount++ % RD_PASSRING_REPLAYS];
    memset(r, 0, sizeof(*r));
    r->replay = replay;
    r->frame = frame;
    r->keep = s_curKeep;
}

void rd__passring_list(int list)
{
    s_curList = list >= 0 && list < 255 ? (uint8_t)list : 255;
}

int rd__passring_list_now(void)
{
    return s_curList == 255 ? -1 : (int)s_curList;
}

const void *rd__passring_push(RdPassKind kind, uint32_t color, uint32_t cw, uint32_t ch,
                              uint32_t depth, uint32_t dw, uint32_t dh, int storage, int post)
{
    RdPassRec *e = &s_ring[s_seq % RD_PASSRING_SIZE];
    memset(e, 0, sizeof(*e));
    e->seq = ++s_seq;
    e->replay = s_curReplay;
    e->frame = s_curFrame;
    e->keep = s_curKeep;
    e->list = s_curList;
    e->kind = (uint8_t)kind;
    e->storage = storage ? 1u : 0u;
    e->post = (int8_t)(post < -1 ? -1 : post > 127 ? 127 : post);
    e->color = color;
    e->cw = cw;
    e->ch = ch;
    e->depth = depth;
    e->dw = dw;
    e->dh = dh;
    RdPassReplay *r = curReplay();
    if (r) {
        if (!r->first) {
            r->first = e->seq;
        }
        r->last = e->seq;
    }
    return (const void *)(uintptr_t)e->seq;
}

void rd__passring_pipeline_hold(int on)
{
    s_pipeHold = on ? 1u : 0u;
}

void rd__passring_pipeline(uint32_t id, uint8_t vs, uint8_t fs, uint8_t dst)
{
    if (!s_seq || s_pipeHold) {
        return;
    }
    RdPassRec *e = &s_ring[(s_seq - 1) % RD_PASSRING_SIZE];
    if (e->kind != RD_PASSK_PASS && e->kind != RD_PASSK_EXACT && e->kind != RD_PASSK_VU_PRE &&
        e->kind != RD_PASSK_VU_TRI) {
        return;
    }
    e->pipeline = id;
    e->vs = vs;
    e->fs = fs;
    e->dst = dst;
}

void rd__passring_bytes(uint64_t bytes)
{
    if (s_seq && s_ring[(s_seq - 1) % RD_PASSRING_SIZE].kind == RD_PASSK_UPLOAD) {
        s_ring[(s_seq - 1) % RD_PASSRING_SIZE].bytes = bytes;
    }
}

void rd__passring_reset(void)
{
    memset(s_ring, 0, sizeof(s_ring));
    memset(s_replays, 0, sizeof(s_replays));
    s_seq = 0;
    s_replayCount = 0;
    s_curReplay = s_curFrame = 0;
    s_curKeep = 0;
    s_curList = 255;
    s_pipeHold = 0;
}

uint32_t rd_passring_count(void)
{
    return s_seq < RD_PASSRING_SIZE ? (uint32_t)s_seq : RD_PASSRING_SIZE;
}

const RdPassRec *rd_passring_get(uint32_t i)
{
    const uint32_t n = rd_passring_count();
    if (i >= n) {
        return NULL;
    }
    return &s_ring[(s_seq - n + i) % RD_PASSRING_SIZE];
}

uint32_t rd_passring_replay_count(void)
{
    return s_replayCount < RD_PASSRING_REPLAYS ? s_replayCount : RD_PASSRING_REPLAYS;
}

const RdPassReplay *rd_passring_replay_get(uint32_t i)
{
    const uint32_t n = rd_passring_replay_count();
    if (i >= n) {
        return NULL;
    }
    return &s_replays[(s_replayCount - n + i) % RD_PASSRING_REPLAYS];
}

const RdPassReplay *rd_passring_replay_of(uint64_t seq)
{
    for (uint32_t i = 0; i < rd_passring_replay_count(); i++) {
        const RdPassReplay *r = rd_passring_replay_get(i);
        if (r->first && seq >= r->first && seq <= r->last) {
            return r;
        }
    }
    return NULL;
}

uint32_t rd_passring_print_start(void)
{
    const uint32_t n = rd_passring_count(), nr = rd_passring_replay_count();
    if (!n) {
        return 0;
    }
    /* the oldest of the last RD_PASSRING_PRINT_REPLAYS replays that has an
     * entry */
    uint64_t from = 0;
    for (uint32_t i = nr > RD_PASSRING_PRINT_REPLAYS ? nr - RD_PASSRING_PRINT_REPLAYS : 0; i < nr;
         i++) {
        const RdPassReplay *r = rd_passring_replay_get(i);
        if (r->first) {
            from = r->first;
            break;
        }
    }
    const uint64_t oldest = rd_passring_get(0)->seq;
    if (!from || from < oldest) {
        return 0;
    }
    return (uint32_t)(from - oldest);
}

static const char *kindName(uint8_t k)
{
    static const char *const names[RD_PASSK_COUNT] = {
        "pass",    "copy",    "snapshot",    "exact blend",
        "uploads", "present", "VU pre-pass", "VU triangle pre-pass"};
    return k < RD_PASSK_COUNT ? names[k] : "?";
}

/* an entry's parts, as rd_passring_format and rd_passring_format_bisect
 * write them */
typedef struct EntryText {
    char list[16], post[48], target[96], pipe[64];
} EntryText;

static void entryText(const RdPassRec *e, const char *(*postName)(int), EntryText *t)
{
    if (e->list == 255) {
        snprintf(t->list, sizeof(t->list), "-");
    } else {
        snprintf(t->list, sizeof(t->list), "%u", (unsigned)e->list);
    }
    if (e->post < 0) {
        t->post[0] = '\0';
    } else if (postName) {
        snprintf(t->post, sizeof(t->post), ", effect %s", postName(e->post));
    } else {
        snprintf(t->post, sizeof(t->post), ", effect %d", (int)e->post);
    }
    switch (e->kind) {
    case RD_PASSK_COPY:
    case RD_PASSK_EXACT:
        snprintf(t->target, sizeof(t->target), "target %u (%ux%u) -> target %u (%ux%u)",
                 (unsigned)e->color, (unsigned)e->cw, (unsigned)e->ch, (unsigned)e->depth,
                 (unsigned)e->dw, (unsigned)e->dh);
        break;
    case RD_PASSK_UPLOAD:
        snprintf(t->target, sizeof(t->target), "%llu bytes", (unsigned long long)e->bytes);
        break;
    case RD_PASSK_PRESENT:
        snprintf(t->target, sizeof(t->target), "from target %u (%ux%u) to %ux%u",
                 (unsigned)e->color, (unsigned)e->cw, (unsigned)e->ch, (unsigned)e->dw,
                 (unsigned)e->dh);
        break;
    case RD_PASSK_VU_PRE:
        snprintf(t->target, sizeof(t->target), "%u vertices into the record (%ux%u)",
                 (unsigned)e->dw, (unsigned)e->cw, (unsigned)e->ch);
        break;
    case RD_PASSK_VU_TRI:
        snprintf(t->target, sizeof(t->target), "%u triangle slots into the record (%ux%u)",
                 (unsigned)e->dw, (unsigned)e->cw, (unsigned)e->ch);
        break;
    default:
        if (e->depth) {
            snprintf(t->target, sizeof(t->target), "colour %u (%ux%u), depth %u (%ux%u)",
                     (unsigned)e->color, (unsigned)e->cw, (unsigned)e->ch, (unsigned)e->depth,
                     (unsigned)e->dw, (unsigned)e->dh);
        } else {
            snprintf(t->target, sizeof(t->target), "colour %u (%ux%u)", (unsigned)e->color,
                     (unsigned)e->cw, (unsigned)e->ch);
        }
        break;
    }
    if (e->pipeline) {
        snprintf(t->pipe, sizeof(t->pipe), "pipeline %u (vs %u fs %u dst %u)",
                 (unsigned)e->pipeline, (unsigned)e->vs, (unsigned)e->fs, (unsigned)e->dst);
    } else {
        t->pipe[0] = '\0';
    }
}

int rd_passring_format(const RdPassRec *e, const char *(*postName)(int), char *out, size_t n)
{
    EntryText t;
    entryText(e, postName, &t);
    return snprintf(out, n, "#%llu replay %u frame %u%s list %s: %s%s, %s%s%s%s",
                    (unsigned long long)e->seq, (unsigned)e->replay, (unsigned)e->frame,
                    e->keep ? " (keep)" : "", t.list, kindName(e->kind),
                    e->storage ? " (storage)" : "", t.target, t.post, t.pipe[0] ? ", " : "",
                    t.pipe);
}

int rd_passring_format_bisect(const RdPassRec *e, uint32_t pass, uint32_t of, uint32_t draws,
                              const char *(*postName)(int), char *out, size_t n)
{
    EntryText t;
    char ofText[24];
    entryText(e, postName, &t);
    if (of) {
        snprintf(ofText, sizeof(ofText), " of %u", (unsigned)of);
    } else {
        ofText[0] = '\0';
    }
    return snprintf(out, n, "pass %u%s, list %s, %s, %s%s %s%s, %u draw%s", (unsigned)pass, ofText,
                    t.list, t.pipe[0] ? t.pipe : "no pipeline", kindName(e->kind),
                    e->storage ? " (storage)" : "", t.target, t.post, (unsigned)draws,
                    draws == 1 ? "" : "s");
}

void rd_passring_print(FILE *f, const char *(*postName)(int))
{
    const uint32_t nr = rd_passring_replay_count();
    fprintf(f, "rd: the last %u replays recorded (pass numbers are the checkpoint markers):\n", nr);
    for (uint32_t i = 0; i < nr; i++) {
        const RdPassReplay *r = rd_passring_replay_get(i);
        fprintf(f, "rd:   replay %u, frame %u%s: passes #%llu..#%llu\n", (unsigned)r->replay,
                (unsigned)r->frame, r->keep ? " (keep)" : "", (unsigned long long)r->first,
                (unsigned long long)r->last);
    }
    const uint32_t n = rd_passring_count(), start = rd_passring_print_start();
    const RdPassRec *s0 = rd_passring_get(start);
    fprintf(f, "rd: the last %u passes recorded, oldest first (the last %u replays%s):\n",
            n - start, (unsigned)RD_PASSRING_PRINT_REPLAYS,
            s0 && start == 0 && rd_passring_replay_of(s0->seq) &&
                    rd_passring_replay_of(s0->seq)->first < s0->seq
                ? ", the oldest one's first passes past the ring"
                : "");
    for (uint32_t i = start; i < n; i++) {
        char line[320];
        rd_passring_format(rd_passring_get(i), postName, line, sizeof(line));
        fprintf(f, "rd:   %s\n", line);
    }
    fflush(f);
}
