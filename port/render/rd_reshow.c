/*
 * port/render/rd_reshow.c
 *
 * The still re-present (docs/port/DISPLAY.md, "Interpolation between
 * ticks"): a between-tick present whose frame would draw the bytes of the
 * tick's last rendered present shows that picture again instead of
 * replaying 54 passes.
 *
 * rd__interp_frame hashes the frame it builds (rd_interp.c outHash: every
 * command, the payload, the head, the camera, the scratch meshes).  Here the
 * decision (rd__reshow_why) and the present (rd__present_blended):
 *
 *   shown again   the hash equals the last rendered present's of the same
 *                 tick, nothing replayed since (replayCounter), and what the
 *                 frame does not carry is unchanged: the settings, the
 *                 DISPLAY and SCENE targets, no texture waiting to upload,
 *                 the device alive.  The replay is the empty keep frame's
 *                 (no passes; the present chain draws DISPLAY again) with
 *                 the frame's header (exact width, camera) and the deferred
 *                 text laid out from the frame (rd__overlay_reshow_text),
 *                 which the present draws on the output.
 *   rendered      otherwise: the camera moved (no hash), a tick's first
 *                 present, a different frame, Motion blur: Smooth (its trail
 *                 is a per-present accumulator), a video, [dev]
 *                 still_represent = false (ICO_RD_STILL_REPRESENT=0).
 *
 * The tick's feedback passes (FEED128, DISPLAY) are left where the rendered
 * present left them, which is where the skipped one would have: it starts
 * from the held copies and draws the same frame.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"

static bool switchOn(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("ICO_RD_STILL_REPRESENT");
        on = !(e && e[0] == '0');
    }
    return on != 0;
}

int rd__reshow_why(const RdReshowIn *in)
{
    if (!in->enabled || in->smooth || in->video) {
        return RD_RESHOW_OFF;
    }
    if (!in->hashOk) {
        return RD_RESHOW_MOVING;
    }
    if (in->first) {
        return RD_RESHOW_FIRST;
    }
    if (!in->haveRef || in->number != in->refNumber || in->replay != in->refReplay) {
        return RD_RESHOW_STALE;
    }
    if (in->hash != in->refHash) {
        return RD_RESHOW_DIFF;
    }
    return in->stateSame ? RD_RESHOW_SHOWN : RD_RESHOW_STATE;
}

/* what the frame does not carry: the settings (with their padding, as
 * copied), the filter option, DISPLAY's and SCENE's textures and sizes */
typedef struct Snap {
    RdSettings settings;
    uint32_t filter, tex[6];
} Snap;

static void snapNow(Snap *s)
{
    memset(s, 0, sizeof(*s));
    memcpy(&s->settings, &g_rd.settings, sizeof(g_rd.settings));
    s->filter = g_rd.filterUpgrade;
    const RdTargetId ids[2] = {RD_TARGET_DISPLAY, RD_TARGET_SCENE};
    for (int k = 0; k < 2; k++) {
        const RdTargetRec *t = rd__target_rec(rd_target(ids[k]).id);
        if (t) {
            s->tex[3 * k] = t->color.id;
            s->tex[3 * k + 1] = t->tw;
            s->tex[3 * k + 2] = t->th;
        }
    }
}

static struct {
    bool have;
    uint32_t number;
    uint64_t hash, replay;
    Snap snap;
} s_ref;

static uint64_t s_why[RD_RESHOW_COUNT];

bool rd__present_blended(const RdFrame *rf, int first, bool present)
{
    bool hashOk = false;
    const uint64_t hash = rd__interp_frame_hash(&hashOk);
    static Snap now;
    snapNow(&now);
    const RdReshowIn in = {
        .enabled = switchOn() && g_rd.hasDevice && !rhi_device_lost(),
        .smooth = g_rd.settings.motionBlurSmooth != 0,
        .video = g_rd.videoShown != 0,
        .hashOk = hashOk,
        .first = first != 0,
        .haveRef = s_ref.have,
        .stateSame = memcmp(&now, &s_ref.snap, sizeof(now)) == 0 && g_rd.texDirtyCount == 0 &&
                     g_rd.texLevelsFilter == (int)g_rd.filterUpgrade,
        .number = rf->number,
        .refNumber = s_ref.number,
        .hash = hash,
        .refHash = s_ref.hash,
        .replay = g_rd.replayCounter,
        .refReplay = s_ref.replay,
    };
    const int why = rd__reshow_why(&in);
    s_why[why]++;
    if (why == RD_RESHOW_SHOWN) {
        if (!present) {
            return true; /* nothing drawn: the targets are as the last present left them */
        }
        static RdFrame shown; /* lists 11..12 of nothing, with the frame's head */
        shown.number = rf->number;
        shown.keep = 1;
        shown.wExact = rf->wExact;
        shown.aspect = rf->aspect;
        shown.gsW = rf->gsW;
        shown.gsH = rf->gsH;
        shown.cullW = rf->cullW;
        shown.hasCamera = rf->hasCamera;
        shown.camera = rf->camera;
        shown.startState = rf->startState;
        rd__overlay_reshow_text(rf);
        rd__perf_reshown();
        const bool ok = rd__replay_frame(&shown, 1, true);
        rd__overlay_reshow_text(NULL);
        s_ref.replay = g_rd.replayCounter;
        s_ref.have = ok;
        return ok;
    }
    const bool ok = rd__replay_frame(rf, (int)rf->keep, present);
    s_ref.have = ok && hashOk && in.enabled;
    s_ref.number = rf->number;
    s_ref.hash = hash;
    s_ref.replay = g_rd.replayCounter;
    snapNow(&s_ref.snap);
    return ok;
}

void rd__reshow_block(void)
{
    uint64_t n = 0;
    for (int k = 0; k < RD_RESHOW_COUNT; k++) {
        n += s_why[k];
    }
    rd__log("interp: still re-present ([dev] still_represent = %s): %llu of %llu presents shown "
            "again without a replay; rendered: %llu first of a tick, %llu with the camera moving, "
            "%llu with the frame differing from the last rendered one's, %llu with no such "
            "picture of the tick or another replay since, %llu after the settings, targets or "
            "textures changed, %llu with it off, Motion blur: Smooth or a video",
            switchOn() ? "true" : "false", (unsigned long long)s_why[RD_RESHOW_SHOWN],
            (unsigned long long)n, (unsigned long long)s_why[RD_RESHOW_FIRST],
            (unsigned long long)s_why[RD_RESHOW_MOVING], (unsigned long long)s_why[RD_RESHOW_DIFF],
            (unsigned long long)s_why[RD_RESHOW_STALE], (unsigned long long)s_why[RD_RESHOW_STATE],
            (unsigned long long)s_why[RD_RESHOW_OFF]);
    memset(s_why, 0, sizeof(s_why));
}

uint64_t rd__reshow_count(int why)
{
    return why >= 0 && why < RD_RESHOW_COUNT ? s_why[why] : 0;
}
