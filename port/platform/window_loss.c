/*
 * port/platform/window_loss.c
 *
 * The window's loss dump and the loss bisect's overlap step (window_loss.h).
 * Their own file because window_host.c is at its file size gate.
 */
#include "window_loss.h"

#include <SDL3/SDL.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "diag_host.h"
#include "dump_zip.h"
#include "host_config.h"
#include "host_fs.h"
#include "rd.h"
#include "rhi.h"

/* the overlap step's rounds of the two frames (rd.h rd_loss_overlap): eight
   replays, a few seconds at the most on a phone */
#define WINDOW_BISECT_OVERLAP_ROUNDS 4u

/* the loss dump's frame number `nth` (0 the lost frame, 1 the one before)
   taken out of the zip to dump (the zip's path and `suffix`) with its size
   and aspect; false with line saying why: the zip cannot be opened (with
   the errno), is not a zip, holds no frame, or the frame cannot be written
   or read by this version (dump_zip.h ico_dump_zip_extract_why) */
bool window_bisect_dump_nth(const char *zip, int nth, const char *suffix, char *dump,
                            size_t dumpSize, uint32_t *gw, uint32_t *gh, float *aspect, char *line,
                            size_t size)
{
    size_t bytes = 0;
    char why[480];
    void *data = ico_dump_zip_extract_why(zip, nth, &bytes, NULL, 0, why, sizeof(why));

    if (data == NULL) {
        snprintf(line, size, "%s", why[0] != '\0' ? why : "the zip gave no frame (no reason)");
        return false;
    }
    snprintf(dump, dumpSize, "%s%s", zip, suffix);
    errno = 0;
    FILE *f = ico_fopen(dump, "wb");
    const int openErr = errno;
    const int wrote = f != NULL && fwrite(data, 1, bytes, f) == bytes;

    if (f != NULL && fclose(f) != 0) {
        ico_remove(dump);
        f = NULL;
    }
    free(data);
    if (!wrote || f == NULL) {
        snprintf(line, size, "the frame could not be written to %s (%s)", dump,
                 f == NULL && openErr != 0 ? strerror(openErr) : "the write failed");
        ico_remove(dump);
        return false;
    }
    if (!rd_dump_frame_size(dump, gw, gh, aspect)) {
        snprintf(line, size, "%s holds a frame (%zu bytes) this version does not read", zip, bytes);
        ico_remove(dump);
        return false;
    }
    return true;
}

bool window_bisect_overlap(const char *zip, const char *dump, const RdLossBisect *b, uint32_t gw,
                           uint32_t gh, RdLossOverlap *ov)
{
    bool ran = false;

    memset(ov, 0, sizeof(*ov));
    ov->result = RD_BISECT_NOT_RUN;
    if (b->result == RD_BISECT_LOST) {
        fprintf(stderr, "window: loss bisect: overlap step not run (the pass-by-pass replay "
                        "already lost the device)\n");
    } else if (b->result != RD_BISECT_CLEAN || rhi_device_lost()) {
        fprintf(stderr, "window: loss bisect: overlap step not run (the pass-by-pass replay did "
                        "not finish)\n");
    } else {
        char before[ICO_PATH_MAX + 32], note[480];
        uint32_t bw = 0, bh = 0;
        float ba = 0.0f;
        const bool haveBefore =
            window_bisect_dump_nth(zip, 1, ".before.rddump", before, sizeof(before), &bw, &bh, &ba,
                                   note, sizeof(note)) &&
            bw == gw && bh == gh;

        if (!haveBefore) {
            before[0] = '\0';
            fprintf(stderr,
                    "window: loss bisect: overlap step: the frame before the lost one is not "
                    "used (%s); the lost frame alone is replayed back to back\n",
                    note);
        }
        fprintf(stderr,
                "window: loss bisect: overlap step: replaying %s back to back, %u times "
                "each, with no wait for the GPU between them\n",
                haveBefore ? "the frame before the lost one and the lost frame" : "the lost frame",
                (unsigned)WINDOW_BISECT_OVERLAP_ROUNDS);
        ico_host_log_flush();
        rd_loss_overlap(dump, before[0] != '\0' ? before : NULL, WINDOW_BISECT_OVERLAP_ROUNDS, ov);
        if (before[0] != '\0') {
            ico_remove(before);
        }
        ran = ov->result != RD_BISECT_NOT_RUN;
        if (!ran) {
            fprintf(stderr, "window: loss bisect: overlap step not run: %s\n", ov->line);
        } else {
            fprintf(stderr,
                    "window: loss bisect: overlap step: frames %u%s%u, %u replays in %.1f s: %s\n",
                    (unsigned)ov->beforeFrame, ov->beforeFrame ? " and " : " (alone) ",
                    (unsigned)ov->lostFrame, (unsigned)ov->replays, ov->ms / 1000.0,
                    ov->result == RD_BISECT_LOST
                        ? (ov->lostAt ? "the device was lost during the replays"
                                      : "the device was lost at the wait after the last replay")
                        : "the device was not lost");
        }
    }
    return ran;
}

/* The loss dump (docs/port/RENDER.md, "The loss dump"): the frame of the
   submission that failed and the one replayed before it, as one zip in the
   logs folder, logs/loss-<date>-<time>.zip, with a copy of the log
   (rd_loss_plan names the frames; rd_dump_loss_frame writes each, from the
   ring or from the memory copy the renderer made when it reused the slot,
   rd.h; dump_zip.h ico_loss_dump_zip, stored: no deflate), so a report
   carries the frame the next start's loss bisect replays and the developers
   can replay it too. The log says which replay and frame each file is and,
   when the lost frame is not held any more, that it is not and which frame was
   written instead (issue 179: v0.11.3's dump held frames 132 and 131 of a
   loss at 129). Each slow step is logged and flushed before it begins (the
   frames' writes, the zip), so a step that never returns names itself (issue
   123: after a GPU hang no loss dump line ever followed the pass ring). The
   oldest loss zips go beyond ICO_LOSS_DUMP_KEEP, never the one just written.
   Its name (the file name alone) into name, "" when none was written; the
   activity file notes the loss either way (diag_host.h
   ico_diag_note_device_loss). */
void window_loss_dump(char *name, size_t name_size)
{
    char logs[ICO_PATH_MAX], zip[ICO_PATH_MAX], lost[ICO_PATH_MAX + 64], before[ICO_PATH_MAX + 64],
        stamp[32], why[160], text[900];
    const time_t now = time(NULL);
    const struct tm *tm = localtime(&now);
    const uint64_t t0 = SDL_GetTicksNS();
    uint32_t lostFrame = 0, beforeFrame = 0;
    bool lostCopy = false, beforeCopy = false;
    RdLossPlan plan;

    name[0] = '\0';
    if (tm == NULL || strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", tm) == 0) {
        snprintf(stamp, sizeof(stamp), "unknown");
    }
    if (ico_host_logs_dir(logs, sizeof(logs)) != 0 ||
        ico_loss_dump_path(zip, sizeof(zip), logs, stamp) != 0) {
        fprintf(stderr, "window: loss dump: no usable logs folder\n");
        ico_diag_note_device_loss(NULL);
        return;
    }
    snprintf(lost, sizeof(lost), "%s/loss-%s-lost.rddump", logs, stamp);
    snprintf(before, sizeof(before), "%s/loss-%s-before.rddump", logs, stamp);
    rd_loss_plan(&plan);
    rd_loss_plan_text(&plan, text, sizeof(text));
    fprintf(stderr, "window: loss dump: %s\n", text);
    if (plan.copiesUnused) {
        fprintf(stderr, "window: loss dump: copies in memory: not used (a copy in progress did "
                        "not finish; the frames come from the ring)\n");
    } else {
        fprintf(stderr,
                "window: loss dump: copies in memory (made only while a slot wait had blocked "
                "100 ms or more): %u held (%u KB; the most %u KB), %llu made in %llu recycled "
                "frames, %llu too big, %llu failed, %llu held back by the throttle\n",
                plan.copies, plan.copyKB, plan.copyMaxKB, (unsigned long long)plan.copiesTaken,
                (unsigned long long)plan.copiesSeen, (unsigned long long)plan.copiesBig,
                (unsigned long long)plan.copiesFailed, (unsigned long long)plan.copiesThrottled);
    }
    if (plan.writeFrame == 0) {
        fprintf(stderr, "window: loss dump: the renderer holds no frame to write\n");
        ico_diag_note_device_loss(NULL);
        return;
    }
    if (plan.frameHeld) {
        fprintf(stderr, "window: loss dump: writing frame %u, the lost one (replay %u)%s, to %s\n",
                (unsigned)plan.writeFrame, (unsigned)plan.replay,
                plan.writeFromCopy ? ", from the copy made in memory" : "", lost);
    } else {
        fprintf(stderr,
                "window: loss dump: the lost frame %u is not held any more (the renderer keeps the "
                "last three frames and the copies above): writing frame %u instead, the nearest "
                "held, to %s; it is NOT the frame whose submission failed\n",
                (unsigned)plan.frame, (unsigned)plan.writeFrame, lost);
    }
    ico_host_log_flush();
    if (!rd_dump_loss_frame(plan.writeFrame, lost, &lostFrame, &lostCopy)) {
        fprintf(stderr,
                "window: loss dump: frame %u not written (not held any more, or the write "
                "failed)\n",
                (unsigned)plan.writeFrame);
        ico_remove(lost);
        ico_diag_note_device_loss(NULL);
        return;
    }
    if (plan.writeBefore != 0) {
        if (plan.writeFrame == plan.frame && plan.writeBefore == plan.before) {
            fprintf(stderr,
                    "window: loss dump: writing frame %u (the one replayed before it)%s to %s\n",
                    (unsigned)plan.writeBefore,
                    plan.writeBeforeFromCopy ? ", from the copy made in memory," : "", before);
        } else {
            fprintf(stderr,
                    "window: loss dump: writing frame %u (the newest held below the one above; "
                    "the frame replayed before the lost one was %u%s) to %s\n",
                    (unsigned)plan.writeBefore, (unsigned)plan.before,
                    plan.beforeHeld ? "" : ", not held any more", before);
        }
        ico_host_log_flush();
        if (!rd_dump_loss_frame(plan.writeBefore, before, &beforeFrame, &beforeCopy)) {
            beforeFrame = 0;
        }
    } else {
        fprintf(stderr, "window: loss dump: no older frame is held to write as the one before\n");
    }
    if (beforeFrame == 0) {
        ico_remove(before);
    }
    {
        unsigned long long a = 0, b = 0, l = 0;

        (void)ico_path_kind(lost, &a, NULL);
        if (beforeFrame) {
            (void)ico_path_kind(before, &b, NULL);
        }
        if (ico_diag_log_path() != NULL && ico_diag_log_path()[0] != '\0') {
            (void)ico_path_kind(ico_diag_log_path(), &l, NULL);
        }
        fprintf(stderr, "window: loss dump: zipping %.1f MB (%s and the log) at store level\n",
                (double)(a + b + l) / (1024.0 * 1024.0),
                beforeFrame ? "the two frames" : "the frame");
    }
    ico_host_log_flush();
    if (ico_loss_dump_zip(zip, lost, beforeFrame ? before : NULL, ico_diag_log_path(), 1, why,
                          sizeof(why)) < 0) {
        fprintf(stderr, "window: loss dump: zip not written: %s\n", why);
        ico_remove(lost);
        ico_remove(before);
        ico_diag_note_device_loss(NULL);
        return;
    }
    IcoPruneInfo pruned;

    (void)ico_loss_dump_prune_except(logs, ICO_LOSS_DUMP_KEEP, zip, &pruned);
    const double secs = (double)(SDL_GetTicksNS() - t0) / 1e9;
    if (beforeFrame) {
        fprintf(stderr,
                "window: loss dump: frames %u (%s) and %u (the one before) written to %s in "
                "%.1f s\n",
                (unsigned)lostFrame,
                plan.frameHeld && lostFrame == plan.frame ? "the lost one" : "NOT the lost one",
                (unsigned)beforeFrame, zip, secs);
    } else {
        fprintf(stderr, "window: loss dump: frame %u (%s) written to %s in %.1f s\n",
                (unsigned)lostFrame,
                plan.frameHeld && lostFrame == plan.frame ? "the lost one" : "NOT the lost one",
                zip, secs);
    }
    (void)lostCopy;
    (void)beforeCopy;
    fprintf(stderr,
            "window: loss dump: %d loss zip%s in the logs folder before the oldest were deleted "
            "(newest %d kept, this one always): %d deleted%s%s%s\n",
            pruned.found, pruned.found == 1 ? "" : "s", ICO_LOSS_DUMP_KEEP, pruned.removed,
            pruned.removed > 0 ? " (" : "", pruned.removed > 0 ? pruned.names : "",
            pruned.removed > 0 ? ")" : "");
    if (pruned.saved) {
        fprintf(stderr,
                "window: loss dump: the name order alone would have deleted this zip (the clock "
                "went back or the zone changed: the older zips carry later times); it was kept\n");
    }
    snprintf(name, name_size, "loss-%s.zip", stamp);
    ico_diag_note_device_loss(zip);
}
