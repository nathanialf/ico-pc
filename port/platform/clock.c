/*
 * port/platform/clock.c
 *
 * The wall clock behind sceCdReadClock and the EE timer counters (clock.h).
 */
#include "clock.h"
#include "host_loop.h"
#include <eeregs.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* --- the wall clock ------------------------------------------------------- */

static int fixed_choice = -1; /* -1: not decided; read the environment */

void ico_clock_set_fixed(int fixed)
{
    fixed_choice = fixed != 0;
}

void ico_clock_pack(IcoClockBcd *out, int year, int month, int day, int hour, int minute,
                    int second)
{
#define BCD(v) ((uint8_t)((((v) / 10) % 10) << 4 | ((v) % 10)))
    out->stat = 0;
    out->second = BCD(second);
    out->minute = BCD(minute);
    out->hour = BCD(hour);
    out->pad = 0;
    out->day = BCD(day);
    out->month = BCD(month);
    out->year = BCD(year % 100);
#undef BCD
}

void ico_clock_now(IcoClockBcd *out)
{
    if (fixed_choice < 0) {
        const char *env = getenv("ICO_FIXED_CLOCK");

        fixed_choice = env == NULL || strcmp(env, "0") != 0;
    }
    if (fixed_choice) {
        ico_clock_pack(out, 2002, 1, 1, 0, 0, 0);
    } else {
        time_t now = time(NULL);
        struct tm tm;

#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        ico_clock_pack(out, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
                       tm.tm_sec);
    }
}

/* --- the EE timers -------------------------------------------------------- */

enum { TIMERS = 4, MODE_CUE = 0x80, MODE_OVFF = 0x800 };

#define BUS_HZ 147456000ull

/* one vsync lasts vsync_num/vsync_den seconds: 1/50, or 1001/60000 */
static unsigned long long vsync_num = 1;

static unsigned long long vsync_den = 50;

static unsigned long long hblank_hz = 15625;

static unsigned long long carry[TIMERS]; /* numerator left over, over vsync_den * 65536 */

static volatile unsigned int *timer_count(int n)
{
    return (volatile unsigned int *)(ico_hw_eeio + 0x800 * n);
}

static volatile unsigned int *timer_mode(int n)
{
    return (volatile unsigned int *)(ico_hw_eeio + 0x800 * n + 0x10);
}

void ico_clock_set_vsync_hz(int hz)
{
    unsigned long long den = hz == 60 ? 60000 : 50;
    int n;

    if (den == vsync_den) {
        return;
    }
    if (hz == 60) {
        vsync_num = 1001;
        vsync_den = 60000;
        hblank_hz = 15734;
    } else {
        vsync_num = 1;
        vsync_den = 50;
        hblank_hz = 15625;
    }
    /* the fractions carried over were in the old rate's units */
    for (n = 0; n < TIMERS; n++) {
        carry[n] = 0;
    }
}

void ico_clock_timers_reset(void)
{
    int n;

    for (n = 0; n < TIMERS; n++) {
        *timer_count(n) = 0;
        *timer_mode(n) = 0;
        carry[n] = 0;
    }
}

void ico_clock_timers_step(unsigned frac_q16)
{
    int n;

    for (n = 0; n < TIMERS; n++) {
        unsigned int mode = *timer_mode(n);
        unsigned long long hz;
        unsigned long long num, den, ticks;
        unsigned int count;

        if ((mode & MODE_CUE) == 0) {
            continue;
        }
        switch (mode & 3) {
        case 0:
            hz = BUS_HZ;
            break;
        case 1:
            hz = BUS_HZ / 16;
            break;
        case 2:
            hz = BUS_HZ / 256;
            break;
        default:
            hz = hblank_hz;
            break;
        }
        num = hz * vsync_num * frac_q16 + carry[n];
        den = vsync_den * 65536ull;
        ticks = num / den;
        carry[n] = num % den;
        count = (*timer_count(n) & 0xFFFFu) + (unsigned int)ticks;
        if (count > 0xFFFFu) {
            *timer_mode(n) = mode | MODE_OVFF;
        }
        *timer_count(n) = count & 0xFFFFu;
    }
}

/* The game's video mode word, systemStatus[0] (common/src/main.c:37): 0 for
   NTSC (60 Hz), 1 for PAL, as host_loop.c's ico_host_vsync_hz reads it. A
   pointer the program sets, not an extern: this file is in ico_platform,
   which tests link without the game. */
static const volatile int *mode_word;

void ico_clock_set_mode_word(const volatile int *word)
{
    mode_word = word;
}

static void on_vsync(void *user)
{
    (void)user;
    if (mode_word != NULL) {
        ico_clock_set_vsync_hz(*mode_word == 0 ? 60 : 50);
    }
    ico_clock_timers_step(65536u);
}

static int attached;

void ico_clock_timers_attach(void)
{
    if (!attached && ico_host_on_vsync_register(on_vsync, NULL) == 0) {
        attached = 1;
    }
}
