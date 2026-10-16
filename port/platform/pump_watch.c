/*
 * port/platform/pump_watch.c
 *
 * The event pump's recent history (pump_watch.h). Message ids are spelled
 * out here, so the names need no windows.h (WinUser.h, Dbt.h, the PBT_
 * values of WinUser.h).
 */
#include "pump_watch.h"
#include <stdio.h>
#include <string.h>

typedef struct PumpMsg {
    uint32_t msg;
    uint64_t wparam;
    int64_t lparam;
    uint64_t ns; /* the last one received */
    uint32_t count;
} PumpMsg;

typedef struct PumpEvent {
    const char *name;
    uint64_t ns;
    uint32_t count;
} PumpEvent;

#define MSG_STACK 4
/* the controllers' arrivals kept for a removal's name */
#define PAD_KNOWN 8

typedef struct PumpPad {
    int added;
    uint32_t id;
    char name[64];
    char guid[40];
    char driver[24];
    uint64_t ns;
} PumpPad;

/* SDL's log ring: a writer takes a slot with an atomic count and marks it
   complete with seq = its number + 1, so a reader skips a line being
   written (any thread logs through SDL) */
typedef struct SdlLine {
    volatile uint32_t seq;
    int16_t category;
    int8_t priority;
    uint64_t ns;
    char text[PUMP_WATCH_SDL_TEXT];
} SdlLine;

static SdlLine s_sdl[PUMP_WATCH_SDL_LINES];
static volatile uint32_t s_sdlN;

static struct {
    const char *volatile part;
    PumpMsg msgs[PUMP_WATCH_MSGS];
    volatile uint32_t nMsgs; /* recorded so far (the ring holds the last PUMP_WATCH_MSGS) */
    PumpEvent events[PUMP_WATCH_EVENTS];
    volatile uint32_t nEvents;
    /* the messages being handled, outermost first */
    PumpMsg in[MSG_STACK];
    volatile uint32_t depth;
    /* the controllers that came and went, and the ones that arrived */
    PumpPad pads[PUMP_WATCH_PADS];
    volatile uint32_t nPads;
    PumpPad known[PAD_KNOWN];
    uint32_t nKnown;
} s_w;

void pump_watch_reset(void)
{
    memset(&s_w, 0, sizeof(s_w));
    memset(s_sdl, 0, sizeof(s_sdl));
    s_sdlN = 0;
}

void pump_watch_part(const char *part)
{
    s_w.part = part;
}

const char *pump_watch_part_now(void)
{
    return s_w.part;
}

/* the ones that come by the hundred and say nothing about a stall */
static int noisy(uint32_t msg)
{
    switch (msg) {
    case 0x0020: /* WM_SETCURSOR */
    case 0x0084: /* WM_NCHITTEST */
    case 0x00A0: /* WM_NCMOUSEMOVE */
    case 0x00FF: /* WM_INPUT */
    case 0x0200: /* WM_MOUSEMOVE */
    case 0x02A1: /* WM_MOUSEHOVER */
    case 0x02A3: /* WM_MOUSELEAVE */
    case 0x003D: /* WM_GETOBJECT */
        return 1;
    default:
        return 0;
    }
}

int pump_watch_msg(uint32_t msg, uint64_t wparam, int64_t lparam, uint64_t now_ns)
{
    const uint32_t n = s_w.nMsgs;
    PumpMsg *last = n > 0 ? &s_w.msgs[(n - 1) % PUMP_WATCH_MSGS] : NULL;

    if (noisy(msg)) {
        return 0;
    }
    if (last != NULL && last->msg == msg && last->wparam == wparam) {
        last->lparam = lparam;
        last->ns = now_ns;
        last->count++;
        return 1;
    }
    PumpMsg *m = &s_w.msgs[n % PUMP_WATCH_MSGS];
    m->msg = msg;
    m->wparam = wparam;
    m->lparam = lparam;
    m->ns = now_ns;
    m->count = 1;
    s_w.nMsgs = n + 1;
    return 1;
}

void pump_watch_msg_enter(uint32_t msg, uint64_t wparam, int64_t lparam, uint64_t now_ns)
{
    const uint32_t d = s_w.depth;

    if (d < MSG_STACK) {
        s_w.in[d].msg = msg;
        s_w.in[d].wparam = wparam;
        s_w.in[d].lparam = lparam;
        s_w.in[d].ns = now_ns;
        s_w.in[d].count = 1;
    }
    s_w.depth = d + 1;
}

uint64_t pump_watch_msg_leave(uint64_t now_ns, char *name, size_t size)
{
    uint32_t d = s_w.depth;
    uint64_t took = 0;

    if (name != NULL && size > 0) {
        name[0] = '\0';
    }
    if (d == 0) {
        return 0;
    }
    d--;
    if (d < MSG_STACK) {
        const PumpMsg *m = &s_w.in[d];

        took = now_ns > m->ns ? now_ns - m->ns : 0;
        if (name != NULL && size > 0) {
            pump_watch_msg_name(m->msg, m->wparam, m->lparam, name, size);
        }
    }
    s_w.depth = d;
    return took;
}

void pump_watch_event(const char *name, uint64_t now_ns)
{
    const uint32_t n = s_w.nEvents;
    PumpEvent *last = n > 0 ? &s_w.events[(n - 1) % PUMP_WATCH_EVENTS] : NULL;

    if (name == NULL) {
        return;
    }
    if (last != NULL && last->name != NULL && strcmp(last->name, name) == 0) {
        last->ns = now_ns;
        last->count++;
        return;
    }
    PumpEvent *e = &s_w.events[n % PUMP_WATCH_EVENTS];
    e->name = name;
    e->ns = now_ns;
    e->count = 1;
    s_w.nEvents = n + 1;
}

static const char *syscommand_name(uint64_t sc)
{
    switch (sc & 0xFFF0u) {
    case 0xF140:
        return "SC_SCREENSAVE";
    case 0xF170:
        return "SC_MONITORPOWER";
    case 0xF020:
        return "SC_MINIMIZE";
    case 0xF030:
        return "SC_MAXIMIZE";
    case 0xF060:
        return "SC_CLOSE";
    case 0xF100:
        return "SC_KEYMENU";
    case 0xF120:
        return "SC_RESTORE";
    default:
        return NULL;
    }
}

static const char *power_name(uint64_t pbt)
{
    switch (pbt) {
    case 0x0004:
        return "PBT_APMSUSPEND";
    case 0x0007:
        return "PBT_APMRESUMESUSPEND";
    case 0x000A:
        return "PBT_APMPOWERSTATUSCHANGE";
    case 0x0012:
        return "PBT_APMRESUMEAUTOMATIC";
    case 0x8013:
        return "PBT_POWERSETTINGCHANGE";
    default:
        return NULL;
    }
}

static const char *device_change_name(uint64_t dbt)
{
    switch (dbt) {
    case 0x0007:
        return "DBT_DEVNODES_CHANGED";
    case 0x8000:
        return "DBT_DEVICEARRIVAL";
    case 0x8001:
        return "DBT_DEVICEQUERYREMOVE";
    case 0x8004:
        return "DBT_DEVICEREMOVECOMPLETE";
    case 0x8006:
        return "DBT_CUSTOMEVENT";
    default:
        return NULL;
    }
}

static const char *msg_base_name(uint32_t msg)
{
    switch (msg) {
    case 0x0002:
        return "WM_DESTROY";
    case 0x0003:
        return "WM_MOVE";
    case 0x0005:
        return "WM_SIZE";
    case 0x0006:
        return "WM_ACTIVATE";
    case 0x0007:
        return "WM_SETFOCUS";
    case 0x0008:
        return "WM_KILLFOCUS";
    case 0x000F:
        return "WM_PAINT";
    case 0x0010:
        return "WM_CLOSE";
    case 0x0011:
        return "WM_QUERYENDSESSION";
    case 0x0014:
        return "WM_ERASEBKGND";
    case 0x0016:
        return "WM_ENDSESSION";
    case 0x0018:
        return "WM_SHOWWINDOW";
    case 0x001A:
        return "WM_SETTINGCHANGE";
    case 0x001C:
        return "WM_ACTIVATEAPP";
    case 0x0024:
        return "WM_GETMINMAXINFO";
    case 0x0046:
        return "WM_WINDOWPOSCHANGING";
    case 0x0047:
        return "WM_WINDOWPOSCHANGED";
    case 0x007E:
        return "WM_DISPLAYCHANGE";
    case 0x0085:
        return "WM_NCPAINT";
    case 0x0086:
        return "WM_NCACTIVATE";
    case 0x00FE:
        return "WM_INPUT_DEVICE_CHANGE";
    case 0x0100:
        return "WM_KEYDOWN";
    case 0x0101:
        return "WM_KEYUP";
    case 0x0104:
        return "WM_SYSKEYDOWN";
    case 0x0105:
        return "WM_SYSKEYUP";
    case 0x0112:
        return "WM_SYSCOMMAND";
    case 0x0113:
        return "WM_TIMER";
    case 0x0218:
        return "WM_POWERBROADCAST";
    case 0x0219:
        return "WM_DEVICECHANGE";
    case 0x0231:
        return "WM_ENTERSIZEMOVE";
    case 0x0232:
        return "WM_EXITSIZEMOVE";
    case 0x02B1:
        return "WM_WTSSESSION_CHANGE";
    case 0x02E0:
        return "WM_DPICHANGED";
    case 0x031A:
        return "WM_THEMECHANGED";
    case 0x031E:
        return "WM_DWMCOMPOSITIONCHANGED";
    case 0x0320:
        return "WM_DWMCOLORIZATIONCOLORCHANGED";
    default:
        return NULL;
    }
}

void pump_watch_msg_name(uint32_t msg, uint64_t wparam, int64_t lparam, char *out, size_t size)
{
    const char *base = msg_base_name(msg);
    const char *sub = NULL;
    const char *extra = "";

    if (size == 0) {
        return;
    }
    if (base == NULL) {
        snprintf(out, size, "message 0x%04x", (unsigned)msg);
        return;
    }
    if (msg == 0x0112) {
        sub = syscommand_name(wparam);
        if ((wparam & 0xFFF0u) == 0xF170) {
            /* the display's power: -1 on, 1 low power, 2 off */
            extra = lparam == 2 ? " off" : lparam == 1 ? " low" : lparam == -1 ? " on" : "";
        }
    } else if (msg == 0x0218) {
        sub = power_name(wparam);
    } else if (msg == 0x0219) {
        sub = device_change_name(wparam);
    } else if (msg == 0x00FE) {
        sub = wparam == 1 ? "GIDC_ARRIVAL" : wparam == 2 ? "GIDC_REMOVAL" : NULL;
    } else if (msg == 0x0006) {
        sub = (wparam & 0xFFFFu) == 0 ? "WA_INACTIVE" : "WA_ACTIVE";
    } else if (msg == 0x0005) {
        sub = wparam == 1 ? "SIZE_MINIMIZED" : wparam == 2 ? "SIZE_MAXIMIZED" : NULL;
    }
    if (sub != NULL) {
        snprintf(out, size, "%s %s%s(0x%04x)", base, sub, extra, (unsigned)msg);
    } else if (msg == 0x0112 || msg == 0x0218 || msg == 0x0219) {
        snprintf(out, size, "%s 0x%llx(0x%04x)", base, (unsigned long long)wparam, (unsigned)msg);
    } else {
        snprintf(out, size, "%s(0x%04x)", base, (unsigned)msg);
    }
}

static double ago_s(uint64_t now_ns, uint64_t ns)
{
    return now_ns > ns ? (double)(now_ns - ns) / 1e9 : 0.0;
}

/* a string into the line at n; returns the new end (the line is cut at
   size - 1 and stays terminated) */
static size_t put_str(char *out, size_t size, size_t n, const char *s)
{
    while (*s != '\0' && n + 1 < size) {
        out[n++] = *s++;
    }
    if (n < size) {
        out[n] = '\0';
    }
    return n;
}

/* one number into the line at n, the same way */
static size_t put_f(char *out, size_t size, size_t n, const char *fmt_s, double a)
{
    char num[64];

    snprintf(num, sizeof(num), fmt_s, a);
    return put_str(out, size, n, num);
}

void pump_watch_recent(char *out, size_t size, uint64_t now_ns)
{
    size_t n = 0;
    uint32_t i, total;
    char name[96];
    char count[24];

    if (size == 0) {
        return;
    }
    out[0] = '\0';
    total = s_w.nMsgs;
    if (total > 0) {
        const uint32_t shown = total < PUMP_WATCH_MSGS ? total : PUMP_WATCH_MSGS;

        n = put_str(out, size, n, "last messages before it: ");
        for (i = 0; i < shown; i++) {
            const PumpMsg *m = &s_w.msgs[(total - 1 - i) % PUMP_WATCH_MSGS];

            pump_watch_msg_name(m->msg, m->wparam, m->lparam, name, sizeof(name));
            n = put_str(out, size, n, i > 0 ? ", " : "");
            n = put_str(out, size, n, name);
            if (m->count > 1) {
                snprintf(count, sizeof(count), " x%u", (unsigned)m->count);
                n = put_str(out, size, n, count);
            }
            n = put_f(out, size, n, " %.1f s ago", ago_s(now_ns, m->ns));
        }
        n = put_str(out, size, n, "; ");
    }
    total = s_w.nEvents;
    n = put_str(out, size, n, "SDL events before it: ");
    if (total == 0) {
        n = put_str(out, size, n, "none");
    } else {
        const uint32_t shown = total < PUMP_WATCH_EVENTS ? total : PUMP_WATCH_EVENTS;

        for (i = 0; i < shown; i++) {
            const PumpEvent *e = &s_w.events[(total - 1 - i) % PUMP_WATCH_EVENTS];

            n = put_str(out, size, n, i > 0 ? ", " : "");
            n = put_str(out, size, n, e->name != NULL ? e->name : "?");
            if (e->count > 1) {
                snprintf(count, sizeof(count), " x%u", (unsigned)e->count);
                n = put_str(out, size, n, count);
            }
            n = put_f(out, size, n, " %.1f s ago", ago_s(now_ns, e->ns));
        }
    }
    if (s_w.nPads > 0 && n + 3 < size) {
        n = put_str(out, size, n, "; ");
        pump_watch_pads_recent(out + n, size - n, now_ns);
    }
}

void pump_watch_describe(char *out, size_t size, uint64_t now_ns)
{
    size_t n = 0;
    const char *part = s_w.part;
    const uint32_t depth = s_w.depth;
    char name[96];

    if (size == 0) {
        return;
    }
    out[0] = '\0';
    n = put_str(out, size, n, "in ");
    n = put_str(out, size, n, part != NULL ? part : "the pump's own code");
    if (depth > 0) {
        /* the innermost message being handled */
        const PumpMsg *m = &s_w.in[(depth < MSG_STACK ? depth : MSG_STACK) - 1];

        pump_watch_msg_name(m->msg, m->wparam, m->lparam, name, sizeof(name));
        n = put_str(out, size, n, ", handling ");
        n = put_str(out, size, n, name);
        n = put_f(out, size, n, " for %.1f s", ago_s(now_ns, m->ns));
    } else {
        n = put_str(out, size, n, ", no window message being handled");
    }
    n = put_str(out, size, n, "; ");
    pump_watch_recent(out + n, size - n, now_ns);
}

/* --- the controllers ------------------------------------------------------ */

static void copy_text(char *out, size_t size, const char *s)
{
    snprintf(out, size, "%s", s != NULL ? s : "");
}

void pump_watch_pad(int added, uint32_t id, const char *name, const char *guid, const char *driver,
                    uint64_t now_ns)
{
    const uint32_t n = s_w.nPads;
    PumpPad *p = &s_w.pads[n % PUMP_WATCH_PADS];
    const PumpPad *k = NULL;
    uint32_t i;

    for (i = 0; i < s_w.nKnown && i < PAD_KNOWN; i++) {
        if (s_w.known[i].id == id) {
            k = &s_w.known[i];
        }
    }
    p->added = added != 0;
    p->id = id;
    p->ns = now_ns;
    copy_text(p->name, sizeof(p->name), name != NULL ? name : k != NULL ? k->name : "(unnamed)");
    copy_text(p->guid, sizeof(p->guid), guid != NULL ? guid : k != NULL ? k->guid : "?");
    copy_text(p->driver, sizeof(p->driver), driver != NULL ? driver : k != NULL ? k->driver : "?");
    s_w.nPads = n + 1;
    if (added) {
        /* the id's slot, else the next one round (the oldest) */
        PumpPad *slot = NULL;

        for (i = 0; i < s_w.nKnown && i < PAD_KNOWN; i++) {
            if (s_w.known[i].id == id) {
                slot = &s_w.known[i];
            }
        }
        if (slot == NULL) {
            slot = &s_w.known[s_w.nKnown % PAD_KNOWN];
            s_w.nKnown++;
        }
        *slot = *p;
    }
}

/* "\"name\" (GUID g, driver)" */
static size_t put_pad(char *out, size_t size, size_t n, const PumpPad *p)
{
    n = put_str(out, size, n, "\"");
    n = put_str(out, size, n, p->name);
    n = put_str(out, size, n, "\" (GUID ");
    n = put_str(out, size, n, p->guid);
    n = put_str(out, size, n, ", ");
    n = put_str(out, size, n, p->driver);
    return put_str(out, size, n, ")");
}

void pump_watch_pad_newest(char *out, size_t size)
{
    const uint32_t n = s_w.nPads;

    if (size == 0) {
        return;
    }
    out[0] = '\0';
    if (n > 0) {
        (void)put_pad(out, size, 0, &s_w.pads[(n - 1) % PUMP_WATCH_PADS]);
    }
}

uint64_t pump_watch_pad_last_ns(void)
{
    const uint32_t n = s_w.nPads;

    return n > 0 ? s_w.pads[(n - 1) % PUMP_WATCH_PADS].ns : 0;
}

void pump_watch_pads_recent(char *out, size_t size, uint64_t now_ns)
{
    const uint32_t total = s_w.nPads;
    const uint32_t shown = total < PUMP_WATCH_PADS ? total : PUMP_WATCH_PADS;
    size_t n = 0;
    uint32_t i;

    if (size == 0) {
        return;
    }
    out[0] = '\0';
    if (total == 0) {
        return;
    }
    n = put_str(out, size, n, "controllers before it: ");
    for (i = 0; i < shown; i++) {
        const PumpPad *p = &s_w.pads[(total - 1 - i) % PUMP_WATCH_PADS];

        n = put_str(out, size, n, i > 0 ? ", " : "");
        n = put_str(out, size, n, p->added ? "added " : "removed ");
        n = put_pad(out, size, n, p);
        n = put_f(out, size, n, " %.1f s ago", ago_s(now_ns, p->ns));
    }
}

const char *pump_watch_guid_driver(const unsigned char guid[16], int windows)
{
    static char other[24];

    switch (guid[14]) {
    case 'x':
        return "XInput";
    case 'h':
        return "HIDAPI";
    case 'r':
        return "RawInput";
    case 'w':
        return "Windows.Gaming.Input";
    case 'g':
        return "GameInput";
    case 'v':
        return "virtual";
    case 'm':
        return "MFi";
    case 0:
        return windows ? "DirectInput" : "the system's";
    default:
        snprintf(other, sizeof(other), "driver '%c'",
                 guid[14] >= 0x20 && guid[14] < 0x7F ? (char)guid[14] : '?');
        return other;
    }
}

/* --- SDL's own log ---------------------------------------------------------- */

void pump_watch_sdl_log(int category, int priority, const char *text, uint64_t now_ns)
{
    const uint32_t n = __atomic_fetch_add(&s_sdlN, 1u, __ATOMIC_RELAXED);
    SdlLine *l = &s_sdl[n % PUMP_WATCH_SDL_LINES];
    size_t k = 0;

    __atomic_store_n(&l->seq, 0u, __ATOMIC_RELEASE);
    l->category = (int16_t)(category < 0 ? -1 : category > 32767 ? 32767 : category);
    l->priority = (int8_t)(priority < 0 ? 0 : priority > 100 ? 100 : priority);
    l->ns = now_ns;
    /* one line: SDL's hex dumps carry newlines */
    for (; text != NULL && text[k] != '\0' && k + 1 < sizeof(l->text); k++) {
        l->text[k] = text[k] == '\n' || text[k] == '\r' ? ' ' : text[k];
    }
    l->text[k] = '\0';
    __atomic_store_n(&l->seq, n + 1u, __ATOMIC_RELEASE);
}

uint32_t pump_watch_sdl_count(void)
{
    return __atomic_load_n(&s_sdlN, __ATOMIC_ACQUIRE);
}

const char *pump_watch_sdl_category(int category)
{
    static const char *const names[] = {"APPLICATION", "ERROR",  "ASSERT", "SYSTEM", "AUDIO",
                                        "VIDEO",       "RENDER", "INPUT",  "TEST",   "GPU"};

    return category >= 0 && category < (int)(sizeof(names) / sizeof(names[0])) ? names[category]
                                                                               : "custom";
}

const char *pump_watch_sdl_priority(int priority)
{
    static const char *const names[] = {"?",    "trace", "verbose", "debug",
                                        "info", "warn",  "error",   "critical"};

    return priority >= 0 && priority < (int)(sizeof(names) / sizeof(names[0])) ? names[priority]
                                                                               : "?";
}

int pump_watch_sdl_text(char *out, size_t size, const char *prefix, uint64_t since_ns,
                        uint64_t now_ns)
{
    const uint32_t total = pump_watch_sdl_count();
    const uint32_t first = total > PUMP_WATCH_SDL_LINES ? total - PUMP_WATCH_SDL_LINES : 0;
    size_t n = 0;
    int lines = 0;
    uint32_t i;

    if (size == 0) {
        return 0;
    }
    out[0] = '\0';
    for (i = first; i < total; i++) {
        const SdlLine *l = &s_sdl[i % PUMP_WATCH_SDL_LINES];
        char line[PUMP_WATCH_SDL_TEXT + 96];
        uint64_t ns;
        int cat, pri, k;

        if (__atomic_load_n(&l->seq, __ATOMIC_ACQUIRE) != i + 1u) {
            continue; /* being written, or written over */
        }
        ns = l->ns;
        cat = l->category;
        pri = l->priority;
        k = snprintf(line, sizeof(line), "%s%.1f s ago %s %s: %s\n", prefix != NULL ? prefix : "",
                     ago_s(now_ns, ns), pump_watch_sdl_category(cat), pump_watch_sdl_priority(pri),
                     l->text);
        if (__atomic_load_n(&l->seq, __ATOMIC_ACQUIRE) != i + 1u || k < 0) {
            continue;
        }
        if (since_ns != 0 && ns < since_ns) {
            continue;
        }
        if (n + (size_t)k + 1 > size) {
            break;
        }
        memcpy(out + n, line, (size_t)k + 1);
        n += (size_t)k;
        lines++;
    }
    return lines;
}
