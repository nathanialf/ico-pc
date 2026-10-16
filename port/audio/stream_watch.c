/*
 * port/audio/stream_watch.c
 *
 * The stream lines, the heartbeat's stream part and the counts
 * (stream_watch.h).
 */
#include "stream_watch.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "diag_host.h"
#include "sndn2_host.h"
#include "sndn2_internal.h"
#include "spu2_sd.h"

/* a log line's bound; the longest stream line is about 90 characters */
#define LINE 160
/* the key-off lines printed before only the count goes on */
#define KEYOFF_LINES 8

enum { W_FREE = 0, W_PENDING, W_OPEN, W_KEYED };

typedef struct Watch {
    int state;
    int slot; /* the game's stream slot, 0 or 1 */
    int no;
    int n;
    int voices[4];
    uint32_t data_bytes;
    uint32_t played;
    int countdown; /* ticks until the key-on line; 0 when none is due */
    int half_logged;
    unsigned int seq; /* the order of the game's opens */
    int page_fill;    /* the packets of the page its open ran on */
    int carried;      /* its open was carried past a full page */
} Watch;

/* two per game slot: a new open's watch can wait for its 0x3E while the
   stream it replaces still waits for its 0x43 */
#define WATCHES 4

static Watch W[WATCHES];
static unsigned int seq_next;
static IcoAudioStreamStats stats;
static IcoAudioLogSink sink;
/* the page the driver runs now (sw_page) */
static int page_fill, page_carried;
/* the once-a-run carry lines (sw_carried) */
static int carry_said, stream_carry_said;

/* the heartbeat's text, published whole: the reader takes buf[cur] */
static char status_buf[2][128];
static volatile int status_cur;

void ico_audio_stream_log_sink(IcoAudioLogSink s)
{
    sink = s;
}

void sw_log(const char *fmt, ...)
{
    char line[LINE];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (sink != NULL) {
        sink(line);
    } else {
        ico_diag_log("%s", line);
    }
}

void ico_audio_stream_watch_reset(void)
{
    memset(W, 0, sizeof W);
    memset(&stats, 0, sizeof stats);
    seq_next = 0;
    page_fill = page_carried = 0;
    carry_said = stream_carry_said = 0;
    snprintf(status_buf[0], sizeof status_buf[0], "adpcm: -, -");
    snprintf(status_buf[1], sizeof status_buf[1], "adpcm: -, -");
    status_cur = 0;
}

void ico_audio_stream_stats(IcoAudioStreamStats *out)
{
    *out = stats;
}

void ico_audio_stream_watch(int slot, int no, const int *voices, int n, int data_bytes)
{
    Watch *w = NULL;
    int i;

    stats.opened++;
    if (n < 1 || n > 4 || voices == NULL) {
        return;
    }
    /* a free entry; else the oldest one */
    for (i = 0; i < WATCHES; i++) {
        if (W[i].state == W_FREE) {
            w = &W[i];
            break;
        }
        if (w == NULL || W[i].seq < w->seq) {
            w = &W[i];
        }
    }
    memset(w, 0, sizeof *w);
    w->state = W_PENDING;
    w->slot = slot;
    w->no = no;
    w->n = n;
    for (i = 0; i < n; i++) {
        w->voices[i] = voices[i];
    }
    w->data_bytes = data_bytes > 0 ? (uint32_t)data_bytes : 0;
    w->seq = ++seq_next;
}

static Watch *find(uint32_t voice, int state_min, int state_max)
{
    int i;

    for (i = 0; i < WATCHES; i++) {
        if (W[i].state >= state_min && W[i].state <= state_max &&
            (uint32_t)W[i].voices[0] == voice) {
            return &W[i];
        }
    }
    return NULL;
}

/* the louder side of a voice's VOLL/VOLR as written (a sweep word, bit 15,
   as it is) */
static unsigned voice_vol(int v)
{
    uint16_t l = spu2_sd_get_param(vsel((uint32_t)v, SPU2_SD_VPARAM_VOLL));
    uint16_t r = spu2_sd_get_param(vsel((uint32_t)v, SPU2_SD_VPARAM_VOLR));
    int ll = ((int)(l & 0x7FFF) ^ 0x4000) - 0x4000;
    int rl = ((int)(r & 0x7FFF) ^ 0x4000) - 0x4000;

    if ((l & 0x8000) != 0) {
        return l;
    }
    if ((r & 0x8000) != 0) {
        return r;
    }
    return (unsigned)((rl < 0 ? -rl : rl) > (ll < 0 ? -ll : ll) ? r : l);
}

static unsigned voice_env(int v)
{
    return spu2_sd_get_param(vsel((uint32_t)v, SPU2_SD_VPARAM_ENVX)) & 0x7FFFu;
}

/* "2,3", "vol 3fff/3fff env 7fff/7fff" */
static void levels(const Watch *w, char *voices, size_t vs, char *lv, size_t ls)
{
    char vol[48] = "", env[48] = "";
    size_t a = 0, b = 0, c = 0;
    int i;

    voices[0] = '\0';
    for (i = 0; i < w->n; i++) {
        const char *sep = i != 0 ? "," : "";
        const char *sl = i != 0 ? "/" : "";

        a += (size_t)snprintf(voices + a, a < vs ? vs - a : 0, "%s%d", sep, w->voices[i]);
        b += (size_t)snprintf(vol + b, b < sizeof vol ? sizeof vol - b : 0, "%s%x", sl,
                              voice_vol(w->voices[i]));
        c += (size_t)snprintf(env + c, c < sizeof env ? sizeof env - c : 0, "%s%x", sl,
                              voice_env(w->voices[i]));
    }
    snprintf(lv, ls, "vol %s env %s", vol, env);
}

static void log_levels(const Watch *w, const char *when)
{
    char voices[32];
    char lv[112];

    levels(w, voices, sizeof voices, lv, sizeof lv);
    if (when[0] != '\0') {
        sw_log("adpcm: %d %svoices %s %s", w->no, when, voices, lv);
    } else if (w->carried) {
        /* the key-on line: the page its open ran on (issue 101) */
        sw_log("adpcm: %d voices %s %s (opened on a full page: carried past its 255)", w->no,
               voices, lv);
    } else {
        sw_log("adpcm: %d voices %s %s (opened on a page of %d packets)", w->no, voices, lv,
               w->page_fill);
    }
}

void sw_page(int fill, int carried)
{
    page_fill = fill;
    page_carried = carried != 0;
}

void sw_open(uint32_t voice)
{
    Watch *w = find(voice, W_PENDING, W_PENDING);

    if (w != NULL) {
        w->state = W_OPEN;
        w->page_fill = page_fill;
        w->carried = page_carried;
        if (page_carried || page_fill >= 255) {
            stats.opens_on_full++;
        }
    }
}

void sw_keyon(uint32_t voice)
{
    Watch *w = find(voice, W_OPEN, W_OPEN);

    if (w != NULL) {
        w->state = W_KEYED;
        w->countdown = 2;
    }
}

void sw_played(uint32_t voice, uint32_t bytes)
{
    Watch *w = find(voice, W_KEYED, W_KEYED);

    if (w == NULL) {
        return;
    }
    w->played += bytes;
    if (!w->half_logged && w->data_bytes != 0 && w->played >= w->data_bytes / 2) {
        w->half_logged = 1;
        log_levels(w, "at half: ");
    }
}

void sw_stop(uint32_t voice)
{
    Watch *w = find(voice, W_OPEN, W_KEYED);

    if (w != NULL) {
        log_levels(w, "at close: ");
        w->state = W_FREE;
    }
}

/* the newest watch of the game's stream slot `slot` */
static const Watch *of_slot(int slot)
{
    const Watch *best = NULL;
    int i;

    for (i = 0; i < WATCHES; i++) {
        if (W[i].state != W_FREE && W[i].slot == slot && (best == NULL || W[i].seq > best->seq)) {
            best = &W[i];
        }
    }
    return best;
}

static void publish(void)
{
    int next = status_cur ^ 1;
    char *out = status_buf[next];
    size_t size = sizeof status_buf[next];
    size_t n = (size_t)snprintf(out, size, "adpcm: ");
    int s;

    for (s = 0; s < 2 && n < size; s++) {
        const Watch *w = of_slot(s);
        const char *sep = s != 0 ? ", " : "";

        if (w == NULL) {
            n += (size_t)snprintf(out + n, size - n, "%s-", sep);
        } else if (w->state != W_KEYED) {
            n += (size_t)snprintf(out + n, size - n, "%s%d@%d opening", sep, w->no, w->voices[0]);
        } else {
            char voices[32];
            char lv[112];

            levels(w, voices, sizeof voices, lv, sizeof lv);
            n += (size_t)snprintf(out + n, size - n, "%s%d@%d %s", sep, w->no, w->voices[0], lv);
        }
    }
    status_cur = next;
}

void sw_tick(void)
{
    int i;

    for (i = 0; i < WATCHES; i++) {
        Watch *w = &W[i];

        if (w->state == W_KEYED && w->countdown > 0 && --w->countdown == 0) {
            log_levels(w, "");
        }
    }
    publish();
}

void ico_audio_stream_status(char *out, size_t size)
{
    snprintf(out, size, "%s", status_buf[status_cur]);
}

void sw_page_full(void)
{
    stats.pages_full++;
}

static const char *stream_packet(int cmd)
{
    switch (cmd) {
    case 0x3E:
        return "open";
    case 0x40:
        return "volume";
    case 0x41:
        return "pitch";
    case 0x42:
        return "play";
    case 0x43:
        return "stop";
    default:
        return NULL;
    }
}

void sw_carried(int cmd)
{
    const char *sp = stream_packet(cmd);

    stats.carried++;
    if (!carry_said) {
        carry_said = 1;
        sw_log("sndn2: the first packet carried past the page's 255 (packet 0x%02X): the PS2 "
               "would have lost it; the driver runs it after the page's own",
               (unsigned)cmd);
    }
    if (sp == NULL) {
        return;
    }
    stats.stream_carried++;
    if (!stream_carry_said) {
        stream_carry_said = 1;
        sw_log("sndn2: the first stream packet carried past the page's 255 (0x%02X, a stream's "
               "%s): without the carry the stream's volume would have been lost",
               (unsigned)cmd, sp);
    }
}

void sw_dropped(void)
{
    stats.dropped++;
}

void ico_sndn2_stream_keyoff(int voice, const char *path)
{
    int i;
    int no = -1;

    stats.keyoffs++;
    if (stats.keyoffs > KEYOFF_LINES) {
        return;
    }
    for (i = 0; i < WATCHES; i++) {
        if (W[i].state != W_FREE) {
            int k;

            for (k = 0; k < W[i].n; k++) {
                if (W[i].voices[k] == voice) {
                    no = W[i].no;
                }
            }
        }
    }
    if (no >= 0) {
        sw_log("sndn2: key-off from the sequencer (%s) on stream voice %d (stream %d)%s", path,
               voice, no, stats.keyoffs == KEYOFF_LINES ? "; further ones are only counted" : "");
    } else {
        sw_log("sndn2: key-off from the sequencer (%s) on stream voice %d (no stream open)%s", path,
               voice, stats.keyoffs == KEYOFF_LINES ? "; further ones are only counted" : "");
    }
}
