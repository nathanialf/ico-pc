/* rd_png.h: the port's minimal PNG writer (rd_png.c), for callers outside
 * the renderer (the texture export, texpack_export.c).  rd_internal.h
 * declares rd_write_png for the renderer's own files. */
#ifndef PORT_RENDER_RD_PNG_H
#define PORT_RENDER_RD_PNG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 8-bit RGBA (or RGB with withAlpha = 0), stored deflate.  The source is
 * RGBA8 either way, rows pitch (>= w * 4) bytes apart.  Any thread once
 * rd_png_prepare has run. */
bool rd_write_png(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t pitch,
                  int withAlpha);

/* Builds the CRC table rd_write_png keeps (otherwise built by the first
 * write).  Call it on the game thread before another thread writes a PNG,
 * so that thread and the game's own writes (Dump textures, the photo
 * mode) never build the table at the same time. */
void rd_png_prepare(void);

#ifdef __cplusplus
}
#endif
#endif /* PORT_RENDER_RD_PNG_H */
