/* upscale_pictures.h: F12's and the bug-report row's upscaler pictures
 * (issue 203; port/render/rd_upscale_capture.h, docs/port/DISPLAY.md "The
 * upscaler's pictures"). With a temporal upscaler in force the renderer
 * writes, beside <stem>-present.png, <stem>-scene.png (the picture before
 * the upscaler), -velocity.png, -depth.png and -composite.png (the
 * upscaler's output); the bug-report row adds them to its zip while the zip
 * stays under GitHub's attachment limit. */
#ifndef ICO_UPSCALE_PICTURES_H
#define ICO_UPSCALE_PICTURES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GitHub's attachment limit is 25 MB: the zip is kept at most this big */
#define ICO_REPORT_ZIP_CAP (24u * 1000u * 1000u)
/* the bug-report row's pictures at most this wide (halved until they fit:
   the lines a report shows keep their pixels at 1440p) */
#define ICO_UPSCALE_PICTURE_MAX_W 2560u
#define ICO_UPSCALE_PICTURES 4

/* the four suffixes: "-scene.png", "-velocity.png", "-depth.png",
   "-composite.png" */
const char *ico_upscale_picture_suffix(int i);

/* stem of "<stem>-present.png" into out; -1 when present_png does not end
   so or out is too small */
int ico_upscale_pictures_stem(const char *present_png, char *out, size_t size);

/* F12 (port/platform/upscale_pictures_arm.c): asks the renderer for the
   four pictures beside present_png (for_report: at most
   ICO_UPSCALE_PICTURE_MAX_W wide); nothing when present_png is "" */
void ico_upscale_pictures_arm(const char *present_png, int for_report);

/* The pictures that exist beside present_png added to zip (NULL or "":
   none, F12's loose files), each only while the zip, the pictures before
   it and reserve bytes stay within cap; line says which went in, and why
   one did not. Returns the count of pictures found beside present_png
   (0: no upscaler was in force; *added the count put in the zip). */
int ico_upscale_pictures_zip(const char *present_png, const char *zip, uint64_t cap,
                             uint64_t reserve, int *added, char *line, size_t size);

/* After F12's present picture is written (window_host.c): the line naming
   the pictures (none: nothing said), and with a bug-report zip the
   pictures added to it under ICO_REPORT_ZIP_CAP, the present's own bytes
   reserved.  The zip takes composite, scene, velocity, depth in that order
   (the last left out first under the cap) */
void ico_upscale_pictures_done(const char *present_png, const char *zip);

/* The renderer's capture outcome as the window reads it (rd.h
   rd_upscale_capture_state; the same values) */
enum {
    ICO_UPSCALE_OUTCOME_NONE = 0,
    ICO_UPSCALE_OUTCOME_PENDING,
    ICO_UPSCALE_OUTCOME_WRITTEN,
    ICO_UPSCALE_OUTCOME_FAILED,
    ICO_UPSCALE_OUTCOME_NO_UPSCALER,
    ICO_UPSCALE_OUTCOME_EXPIRED
};

/* After F12's present picture is written, with the capture's outcome: not
   PENDING, ico_upscale_pictures_done's work at once (with a line when an
   upscaler was asked and gave none); PENDING (the present after the
   request did not dispatch the upscaler: a keep frame), present_png and
   zip kept, a line, and 1 returned: the caller adds the present to the
   zip now and ico_upscale_pictures_step adds the four when the outcome
   comes (the renderer's wait is bounded, rd_upscale_capture.h) */
int ico_upscale_pictures_after(const char *present_png, const char *zip, int outcome);
/* Each pump: while a present waits, the outcome read again; when it is
   not PENDING the pictures named and added to the kept zip (1 returned) */
int ico_upscale_pictures_step(int outcome);
/* a present waits for its upscaler pictures */
int ico_upscale_pictures_waiting(void);
/* upscale_pictures_arm.c: ico_upscale_pictures_after and _step with the
   renderer's outcome */
void ico_upscale_pictures_after_present(const char *present_png, const char *zip);
void ico_upscale_pictures_pump(void);

#ifdef __cplusplus
}
#endif

#endif
