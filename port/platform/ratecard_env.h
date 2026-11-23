/*
 * port/platform/ratecard_env.h
 *
 * The rate card's results file for the renderer (port/render/rd_ratecard.h):
 * logs/ratecard.txt beside the activity file and the version its key holds,
 * exported as ICO_RATECARD_FILE and ICO_RATECARD_VERSION at the start
 * (diag_host.c, right after the attempts' marks), and ICO_RATECARD_SKIP
 * (its words) when the run before ended badly (a crash, an ANR, a signal
 * other than 9, a device loss, a GPU hang, the watchdog: the end as the
 * cull's step classed it): no card on that start.  Nothing is read or
 * written here; the file is the renderer's (the attempts' marks read its
 * "measuring" as the card's end, vu_zero_mark_ratecard_measuring).
 */
#ifndef ICO_PLATFORM_RATECARD_ENV_H
#define ICO_PLATFORM_RATECARD_ENV_H

void ratecard_env_begin(const char *activityPath, const char *version);

#endif
