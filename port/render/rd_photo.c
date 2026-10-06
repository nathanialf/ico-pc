/* rd_photo.c: photo mode's camera override and its pinned frame (package
 * PHOTO; rd.h rd_SetPhotoCamera, docs/port/RENDER_API.md "Photo mode").
 *
 * While the override is on, every frame rd_EndFrame closes that is not a
 * keep frame is copied here (its lists and payload, deep), so the scene the
 * camera moves over outlives the ring (RD_FRAME_RING keeps three frames; a
 * run of keep frames would push the last full one out).  The copy refers to
 * the source frame's temporary targets (reflections, render-to-texture
 * blocks) by id: when the ring reuses the source slot, rd__FrameReset asks
 * rd__PhotoAdoptTemp first, and a target the pin still names is left alive
 * and freed here when the pin lets it go.  Each present replays the pin
 * through rd__PhotoFrame (rd_interp.c).  Off, nothing here runs: the frames,
 * the presents and the targets are what they were without it.
 */
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"

static struct {
    int on;
    RdCamera ov;
    uint32_t flags;
    RdFrame pin; /* own lists and payload; tempTargets as the source had them */
    int pinned;
    uint8_t adopted[RD_MAX_TEMP_PER_FRAME];
    uint32_t lastNumber; /* the pin's number at the last present: firstOfTick */
    /* the log */
    uint32_t pins, presents;
    size_t maxBytes;
    RdPhotoStats last;
} s_ph;

bool rd__PhotoOn(void)
{
    return s_ph.on != 0;
}

const RdFrame *rd__PhotoPinned(void)
{
    return s_ph.pinned ? &s_ph.pin : NULL;
}

size_t rd__PhotoPinBytes(void)
{
    if (!s_ph.pinned) {
        return 0;
    }
    size_t n = s_ph.pin.payloadSize;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        n += (size_t)s_ph.pin.lists[l].count * sizeof(RdCmd);
    }
    return n;
}

/* the pin lets its frame go: the temporary targets it adopted are freed */
static void unpin(void)
{
    if (s_ph.pinned) {
        for (uint32_t i = 0; i < s_ph.pin.tempCount && i < RD_MAX_TEMP_PER_FRAME; i++) {
            if (s_ph.adopted[i]) {
                rd__TempTargetFree(s_ph.pin.tempTargets[i]);
            }
        }
    }
    memset(s_ph.adopted, 0, sizeof(s_ph.adopted));
    s_ph.pin.tempCount = 0;
    s_ph.pinned = 0;
}

static void pinFree(void)
{
    unpin();
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        free(s_ph.pin.lists[l].cmds);
    }
    free(s_ph.pin.payload);
    memset(&s_ph.pin, 0, sizeof(s_ph.pin));
}

/* s_ph.pin = f, deep; the buffers are reused from pin to pin */
static void pinCopy(const RdFrame *f)
{
    unpin();
    RdCmdList lists[RD_LIST_COUNT];
    uint8_t *payload = s_ph.pin.payload;
    uint32_t payloadCap = s_ph.pin.payloadCap;
    memcpy(lists, s_ph.pin.lists, sizeof(lists));
    bool ok = true;
    for (int l = 0; l < RD_LIST_COUNT && ok; l++) {
        RdCmdList *o = &lists[l];
        const RdCmdList *c = &f->lists[l];
        if (c->count > o->cap) {
            RdCmd *p = realloc(o->cmds, (size_t)c->count * sizeof(RdCmd));
            if (!p) {
                ok = false;
                break;
            }
            o->cmds = p;
            o->cap = c->count;
        }
        if (c->count) {
            memcpy(o->cmds, c->cmds, (size_t)c->count * sizeof(RdCmd));
        }
        o->count = c->count;
    }
    if (ok && f->payloadSize > payloadCap) {
        uint8_t *p = realloc(payload, f->payloadSize);
        if (p) {
            payload = p;
            payloadCap = f->payloadSize;
        } else {
            ok = false;
        }
    }
    if (ok && f->payloadSize) {
        memcpy(payload, f->payload, f->payloadSize);
    }
    s_ph.pin = *f;
    memcpy(s_ph.pin.lists, lists, sizeof(lists));
    s_ph.pin.payload = payload;
    s_ph.pin.payloadCap = payloadCap;
    if (!ok) {
        rd__Log("photo: could not pin frame %u (out of memory)", f->number);
        for (int l = 0; l < RD_LIST_COUNT; l++) {
            s_ph.pin.lists[l].count = 0;
        }
        s_ph.pin.payloadSize = 0;
        s_ph.pin.tempCount = 0;
        return;
    }
    if (s_ph.pin.tempCount > RD_MAX_TEMP_PER_FRAME) {
        s_ph.pin.tempCount = RD_MAX_TEMP_PER_FRAME;
    }
    s_ph.pinned = 1;
    s_ph.pins++;
    const size_t bytes = rd__PhotoPinBytes();
    if (bytes > s_ph.maxBytes) {
        s_ph.maxBytes = bytes;
    }
}

void rd__PhotoPin(const RdFrame *f)
{
    if (s_ph.on && f && f->closed && !f->keep) {
        pinCopy(f);
    }
}

bool rd__PhotoAdoptTemp(uint32_t id)
{
    if (!s_ph.pinned || id == 0) {
        return false;
    }
    for (uint32_t i = 0; i < s_ph.pin.tempCount; i++) {
        if (s_ph.pin.tempTargets[i] == id && !s_ph.adopted[i]) {
            s_ph.adopted[i] = 1;
            return true;
        }
    }
    return false;
}

/* a camera's eye and forward axis (the view's third row), for the log */
static void eyeForward(const RdCamera *c, float eye[3], float fwd[3])
{
    const float *v = c->view;
    for (int j = 0; j < 3; j++) {
        eye[j] = -(v[j * 4 + 0] * v[12] + v[j * 4 + 1] * v[13] + v[j * 4 + 2] * v[14]);
        fwd[j] = v[j * 4 + 2];
    }
}

/* the last closed frame of the ring that is not a keep frame */
static const RdFrame *lastScene(void)
{
    const RdFrame *f = rd__LastFrame();
    if (f && f->closed && !f->keep) {
        return f;
    }
    f = rd__PrevFrame();
    return f && f->closed && !f->keep ? f : NULL;
}

void rd_SetPhotoCamera(const RdCamera *ov, uint32_t flags)
{
    if (!g_rd.inited) {
        return;
    }
    if (ov) {
        if (!s_ph.on) {
            s_ph.on = 1;
            s_ph.pins = s_ph.presents = 0;
            s_ph.maxBytes = 0;
            s_ph.lastNumber = 0;
            const RdFrame *f = lastScene();
            if (f) {
                pinCopy(f);
            }
            const RdFrame *p = rd__PhotoPinned();
            uint32_t cmds = 0;
            for (int l = 0; p && l < RD_LIST_COUNT; l++) {
                cmds += p->lists[l].count;
            }
            float e[3] = {0}, fw[3] = {0};
            if (p && p->hasCamera) {
                eyeForward(&p->camera, e, fw);
            }
            rd__Log("photo: camera override on; pinned frame %u: %zu bytes (%u commands, %u "
                    "payload bytes, %u temporary targets); the game camera's eye (%.1f, %.1f, "
                    "%.1f), forward (%.3f, %.3f, %.3f)",
                    p ? p->number : 0u, rd__PhotoPinBytes(), cmds, p ? p->payloadSize : 0u,
                    p ? p->tempCount : 0u, (double)e[0], (double)e[1], (double)e[2], (double)fw[0],
                    (double)fw[1], (double)fw[2]);
        }
        s_ph.ov = *ov;
        s_ph.ov.cut = 0;
        s_ph.flags = flags;
        return;
    }
    if (!s_ph.on) {
        return;
    }
    float e[3], fw[3];
    eyeForward(&s_ph.ov, e, fw);
    rd__Log("photo: camera override off after %u presents; %u frames pinned, at most %zu bytes "
            "(last present: %u VU draws re-based, %u kept at the game camera, %u UI draws "
            "dropped; the override's eye (%.1f, %.1f, %.1f), forward (%.3f, %.3f, %.3f))",
            s_ph.presents, s_ph.pins, s_ph.maxBytes, s_ph.last.rebased, s_ph.last.keptCamera,
            s_ph.last.dropped, (double)e[0], (double)e[1], (double)e[2], (double)fw[0],
            (double)fw[1], (double)fw[2]);
    s_ph.on = 0;
    unpin();
    /* the next picture is the game's again: not blended across the change */
    rd_CameraCut();
}

bool rd_PhotoActive(void)
{
    return g_rd.inited && s_ph.on;
}

bool rd_PhotoSceneCamera(RdCamera *out)
{
    const RdFrame *f = s_ph.pinned ? &s_ph.pin : (g_rd.inited ? lastScene() : NULL);
    if (!f || !f->hasCamera || !out) {
        return false;
    }
    *out = f->camera;
    out->cut = 0;
    return true;
}

const RdFrame *rd__PhotoPresentFrame(void)
{
    if (!s_ph.on || !s_ph.pinned) {
        return NULL;
    }
    const int first = s_ph.pin.number != s_ph.lastNumber;
    s_ph.lastNumber = s_ph.pin.number;
    s_ph.presents++;
    return rd__PhotoFrame(&s_ph.pin, &s_ph.ov, s_ph.flags, first, &s_ph.last);
}

void rd__PhotoShutdown(void)
{
    pinFree();
    memset(&s_ph, 0, sizeof(s_ph));
}
