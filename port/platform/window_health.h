/*
 * port/platform/window_health.h
 *
 * The window's part of the picture-health guard (port/render/rd_health.h,
 * issue 179): the guard's mark in config.toml, [dev] picture_health and
 * picture_health_by, written by the game alone (as gpu_robust_by and
 * vu_bones_by are), read at the start and removed after an update.
 *
 *   window_health_start(cfgName)
 *       before the renderer starts (window_host.c, after
 *       window_robust_start): the mark read; this version's handed to the
 *       renderer (rd_health_set_start: the start begins with its groups
 *       off), robust access on for this start when its group (e) is off
 *       (ICO_GPU_ROBUST=1 for the device, never written to the settings;
 *       the player's gpu_robust rules where set); another version's or a
 *       form that is not the game's removed (both keys left empty: the
 *       config store has no removal, every reader takes "" for absent).
 *       One line says which.
 *   window_health_pump()
 *       the window's pump (window_host.c, beside window_bones_pump): the
 *       guard's new state (rd_health_persist_take) written with its mark,
 *       one line each time.
 */
#ifndef ICO_PLATFORM_WINDOW_HEALTH_H
#define ICO_PLATFORM_WINDOW_HEALTH_H

void window_health_start(const char *cfgName);
void window_health_pump(void);

#endif
