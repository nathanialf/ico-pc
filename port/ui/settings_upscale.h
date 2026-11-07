/*
 * port/ui/settings_upscale.h
 *
 * The Upscaling rows' helpers on Options > Picture: the FSR 3.1 value (offered
 * only where the device can run it), the Upscaling quality row, and the
 * words and rules around them (settings_upscale.c).  settings.c builds the
 * page and calls these; they live in their own file so settings.c stays
 * under the repository's 256 KiB file cap.
 */
#ifndef PORT_UI_SETTINGS_UPSCALE_H
#define PORT_UI_SETTINGS_UPSCALE_H

#include "video_options.h"

#ifdef __cplusplus
extern "C" {
#endif

/* whether FSR 3.1 can run (the installed query, else the renderer's answer) */
int ui__temporal_supported(void);
/* FSR 3.1 in force for the options: saved, supported, no CRT filter and no
   Downsampling (rd__upscale_temporal_on's rule); it replaces MSAA/SSAA */
int ui__temporal_on(const IcoVideoOptions *o);
/* the value text of UI_OPT_UPSCALE (Off, FSR 1.0, FSR 3.1 with
   " (off: not available)" when saved but not runnable) and of
   UI_OPT_UPSCALE_QUALITY; buf holds the composed ones */
const char *ui__upscale_text(const IcoVideoOptions *o, int opt, char *buf, unsigned size);
/* Left/Right on UI_OPT_UPSCALE (FSR 3.1 skipped unless supported) and on
   UI_OPT_UPSCALE_QUALITY, in o */
void ui__upscale_step(IcoVideoOptions *o, int opt, int dir);
/* whether UI_OPT_UPSCALE_SHARPNESS / UI_OPT_UPSCALE_QUALITY show now */
int ui__upscale_row_shown(int opt);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_SETTINGS_UPSCALE_H */
