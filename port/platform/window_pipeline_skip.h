/*
 * port/platform/window_pipeline_skip.h
 *
 * [dev] pipeline_skip at the window's start (issue 161;
 * port/platform/pipeline_skip.h has the record).  Its own file because
 * window_host.c is at its file size gate.
 */
#ifndef ICO_PLATFORM_WINDOW_PIPELINE_SKIP_H
#define ICO_PLATFORM_WINDOW_PIPELINE_SKIP_H

#include <SDL3/SDL.h>
#include <stdint.h>

/* After rd_init made the device, before the start-up set or the lazy run:
   writes the record after an end inside a family the renderer can draw
   another way (one line, one box over window), and turns that family off
   on the record's device and driver (rd_set_sprite_planes_off); cfgName
   names config.toml for the lines */
void window_pipeline_skip(SDL_Window *window, const char *cfgName);
/* The 10 s block (window_host.c perf_drain, perf_log): a replay's
   screen-prim triangle commands through the plain sprite builds
   (RdPerfRecord.spriteNoPlanes) added up, and with the sprite planes
   builds off the block's line "window: sprite no-planes: N commands in
   these 10 s (the sprite planes builds off)", the count then reset */
void window_pipeline_skip_count(uint32_t commands);
void window_pipeline_skip_line(void);

#endif
