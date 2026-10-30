/*
 * port/platform/pipeline_skip.h
 *
 * Issue 161 (the Retroid Pocket 6, Adreno 740 with Qualcomm's compiler
 * E031.41.03.47): a start that ended while the graphics driver created a
 * pipeline of a family the renderer can draw another way leaves that
 * family out of every later start on the same device and driver.
 *
 * The record is [dev] pipeline_skip in config.toml, written by the window
 * (window_host.c) at the start that follows such an end, as
 *     "<family>; <device>; <driver>; <date>"
 * e.g. "sprite_planes; Adreno (TM) 740; E031.41.03.47; 2026-10-30": the
 * family's word (pipeline_skip_family_word), the device's name as the
 * graphics backend gives it (rhi_adapter_name), the driver's compiler build
 * (RhiLimits.compilerVersion without "Compiler Version:") or, where the
 * driver names none, its version, and the day it was written.  Every start
 * reads it: on the same device and driver the renderer leaves the family
 * out of its start-up set and never creates it on first use (rd.h
 * rd_set_sprite_planes_off: the screen-prim triangles draw through the
 * plain sprite builds); on another device or driver it is kept but not
 * used, and said so in the log.  Setting it to "" (or deleting the line)
 * makes the family again.
 *
 * Which family a note names (pipeline_skip_family_of_note) is read from the
 * start-up activity file's note (diag_host.h), the pipeline or the batch
 * the ended start was creating, and only when every pipeline it names is of
 * the family: a lazy run's single creation ("creating pipeline <key>
 * (<vs> N words + <fs> M words)") or a batch whose named vertex shaders
 * are all of it, with none left unnamed ("... and N more").  The careful
 * start-up set makes the sprite planes builds last and one a batch
 * (port/render/rd_pipeline.c), so its note names exactly one pipeline.
 *
 * Pure C, no globals: the unit test is port/platform/test/pipeline_skip_test.c.
 */
#ifndef ICO_PIPELINE_SKIP_H
#define ICO_PIPELINE_SKIP_H

#include <stddef.h>

/* The families a start can leave out: each has a fallback that draws the
   same draws another way */
enum {
    PIPELINE_SKIP_NONE = 0,
    /* the screen-prim triangles' planes builds (sprite_plane_*_vs,
       sprite_stq_plane_*_vs with sprite_ps_planes, sprite_texa_ps_planes,
       sprite_stq_ps_planes and their twins; port/render/rd_screen_planes.c):
       the plain sprite builds draw them without the planes */
    PIPELINE_SKIP_SPRITE_PLANES = 1
};

typedef struct PipelineSkip {
    int family;      /* PIPELINE_SKIP_* */
    char device[96]; /* "Adreno (TM) 740" */
    char driver[64]; /* "E031.41.03.47" */
    char date[16];   /* "2026-10-30" */
} PipelineSkip;

/* The family every pipeline of the note belongs to, PIPELINE_SKIP_NONE when
   the note names none, another kind, or a mix (or NULL) */
int pipeline_skip_family_of_note(const char *note);
/* Whether a vertex shader's name is a sprite planes build */
int pipeline_skip_vs_is_sprite_planes(const char *name);
/* "sprite_planes" for the value; "" for none */
const char *pipeline_skip_family_word(int family);
/* "the sprite planes builds" for the log */
const char *pipeline_skip_family_text(int family);
/* the plain words for a player's box ("some of the shapes on the screen") */
const char *pipeline_skip_family_plain(int family);
/* The value for s (its fields cut at ';', '"', '\\' and line ends, which
   become spaces): 0, or -1 when s names no family or out is too small */
int pipeline_skip_format(char *out, size_t size, const PipelineSkip *s);
/* The record in value (as pipeline_skip_format writes it; spaces around
   the fields are taken off): 0, or -1 when it names no known family (""
   included); the device, driver and date may be empty */
int pipeline_skip_parse(const char *value, PipelineSkip *out);
/* Whether the record applies to this device and driver: both equal (ASCII
   case ignored), an empty field of the record matching any */
int pipeline_skip_matches(const PipelineSkip *s, const char *device, const char *driver);
/* The driver's word for the record: the compiler build after "Version:" in
   compiler ("Compiler Version: E031.41.03.47" -> "E031.41.03.47") when it
   names one, else the text after the last "version " in driver_text (the
   device line's "..., version 512.676.53"), else "" */
void pipeline_skip_driver_word(char *out, size_t size, const char *compiler,
                               const char *driver_text);

#endif /* ICO_PIPELINE_SKIP_H */
