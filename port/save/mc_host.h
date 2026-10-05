/*
 * port/save/mc_host.h
 *
 * Controls of the host memory card (mc_host.c): libmc over a folder.
 * docs/port/SAVES.md describes the layout and the result codes.
 */
#ifndef ICO_SAVE_MC_HOST_H
#define ICO_SAVE_MC_HOST_H

/* The card folder (port 0): PATH/BESCES-50760ico/ holds the game's files.
   Call before sceMcInit; without it sceMcInit asks ico_host_saves_dir
   (host_config.h: saves= in ico-pc.ini, else <pref dir>/memcard). The
   folder is created by the game's first Mkdir, never before. */
void ico_mc_host_set_root(const char *dir);
const char *ico_mc_host_root(void);
/* 1 while a request has been issued and its sceMcSync has not returned it */
int ico_mc_host_pending(void);

/* The card's size as sceMcGetInfo reports free space: 1 KB clusters */
#define ICO_MC_HOST_CLUSTERS 8000
#endif /* ICO_SAVE_MC_HOST_H */
