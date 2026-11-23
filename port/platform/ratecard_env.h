/*
 * port/platform/ratecard_env.h
 *
 * The rate card's results file for the renderer (port/render/rd_ratecard.h):
 * logs/ratecard.txt beside the activity file and the version its key holds,
 * exported as ICO_RATECARD_FILE and ICO_RATECARD_VERSION at the start
 * (diag_host.c, right after the attempts' marks).  Nothing is read or written
 * here; the file is the renderer's.
 */
#ifndef ICO_PLATFORM_RATECARD_ENV_H
#define ICO_PLATFORM_RATECARD_ENV_H

void ratecard_env_begin(const char *activityPath, const char *version);

#endif
