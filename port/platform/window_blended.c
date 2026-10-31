/*
 * port/platform/window_blended.c
 *
 * F12's blended present's frame into its zip (window_blended.h).
 */
#include "window_blended.h"
#include <stdio.h>
#include <string.h>
#include "dump_zip.h"
#include "host_config.h"
#include "host_fs.h"
#include "rd.h"

static char s_dump[ICO_PATH_MAX + 16], s_zip[ICO_PATH_MAX];
static int s_pending;

int window_blended_arm(const char *dump, const char *zip)
{
    window_blended_pump();
    s_pending = 0;
    if (dump == NULL || zip == NULL) {
        (void)rd_dump_blended_arm(NULL);
        return 0;
    }
    const size_t n = strlen(dump);
    const size_t stem = n > 7 ? n - 7 : 0;

    snprintf(s_dump, sizeof(s_dump), "%.*s-blended.rddump", (int)stem, dump);
    snprintf(s_zip, sizeof(s_zip), "%s", zip);
    s_pending = rd_dump_blended_arm(s_dump);
    return s_pending;
}

void window_blended_pump(void)
{
    if (!s_pending) {
        return;
    }
    uint32_t fr = 0;
    float a = 1.0f;
    const int r = rd_dump_blended_result(&fr, &a);
    char why[160] = "";
    const int added = r == 1 && ico_zip_move_file(s_zip, s_dump, why, sizeof(why), NULL) == 0;

    if (r >= 0) {
        fprintf(
            stderr, "window: F12: the present of frame %u at alpha %.9g %s%s%s\n", fr, (double)a,
            r == 0  ? "was the tick's own picture (the zip's frame)"
            : added ? "was drawn between the ticks: the zip holds the blended present's "
                      "frame (rd_replay_tool replays it as drawn), "
                    : "was drawn between the ticks; its frame is not in the zip: ",
            r == 0 ? "" : (added ? s_dump : why), r == 0 || added ? "" : "; the .rddump is kept");
    } else if (r == -1) {
        ico_remove(s_dump);
        fprintf(stderr, "window: F12: the blended present of frame %u could not be written\n", fr);
    } else if (r == -3) {
        fprintf(stderr,
                "window: F12: no present between the ticks followed (frame %u): the zip "
                "holds the frame and the tick before, no blended present's frame\n",
                fr);
    }
    s_pending = r == -2;
}
