/*
 * port/game/eye_trail.c
 *
 * The shadow creatures' eye trails in the world (eye_trail.h).  Built with
 * the game's include path and semantics options, so the arithmetic is
 * UpdatePointBlur's (ico2/sugipon/src/enemyParts.c) through the same
 * port/math functions.
 */
#include <stdio.h>
#include <string.h>

#include "typedef.h"
#include "Matrix.h"

#include "eye_trail.h"

/* one point as UpdatePointBlur saw it */
typedef struct EyeTrailPoint {
    float world[4]; /* the point projected */
    float prev[4];  /* the point its pair is built across to (screenPos[1]'s) */
    float f;        /* the width, in view-space x */
    int prevOk;     /* 0: screenPos[1] held no point the trail saw */
    int pair[8];    /* the pair the game wrote */
} EyeTrailPoint;

typedef struct EyeTrailSlot {
    const void *key; /* the PointBlur; NULL when free */
    int num;
    unsigned stamp; /* the push clock at the last push or the init */
    /* the world positions of screenPos[0] and screenPos[1] */
    float w0[4], w1[4];
    int w0Ok, w1Ok;
    EyeTrailPoint pt[ICO_EYE_TRAIL_POINTS]; /* newest first, as the strip */
} EyeTrailSlot;

static struct {
    EyeTrailSlot slot[ICO_EYE_TRAIL_SLOTS];
    unsigned clock;
    int last; /* the slot found last */
    /* the photo mode session's counts */
    unsigned rebuilt, noTrail, first, step, tooLong;
    int logged;
} t;

static EyeTrailSlot *find(const void *key)
{
    if (key == NULL) {
        return NULL;
    }
    if (t.slot[t.last].key == key) {
        return &t.slot[t.last];
    }
    for (int i = 0; i < ICO_EYE_TRAIL_SLOTS; i++) {
        if (t.slot[i].key == key) {
            t.last = i;
            return &t.slot[i];
        }
    }
    return NULL;
}

void ico_eye_trail_init(const void *key, int num)
{
    EyeTrailSlot *s = find(key);

    if (s == NULL && key != NULL) {
        /* a free slot, else the one pushed to longest ago */
        int pick = -1;
        for (int i = 0; i < ICO_EYE_TRAIL_SLOTS; i++) {
            if (t.slot[i].key == NULL) {
                pick = i;
                break;
            }
            if (pick < 0 || t.clock - t.slot[i].stamp > t.clock - t.slot[pick].stamp) {
                pick = i;
            }
        }
        s = &t.slot[pick];
    }
    if (s == NULL) {
        return;
    }
    memset(s, 0, sizeof(*s));
    if (num < 1 || num > ICO_EYE_TRAIL_POINTS) {
        return; /* the slot stays free */
    }
    s->key = key;
    s->num = num;
    s->stamp = ++t.clock;
}

void ico_eye_trail_push(const void *key, const float *world, float f, int reset, const int *pair)
{
    EyeTrailSlot *s = find(key);
    EyeTrailPoint e;

    if (s == NULL) {
        return;
    }
    s->stamp = ++t.clock;
    if (!reset) {
        /* moveDataElements: screenPos[1] = screenPos[0], the pairs one on */
        memcpy(s->w1, s->w0, sizeof(s->w1));
        s->w1Ok = s->w0Ok;
        memmove(&s->pt[1], &s->pt[0], (size_t)(s->num - 1) * sizeof(s->pt[0]));
    }
    memcpy(s->w0, world, sizeof(s->w0));
    s->w0Ok = 1;
    memcpy(e.world, s->w0, sizeof(e.world));
    memcpy(e.prev, s->w1, sizeof(e.prev));
    e.f = f;
    e.prevOk = s->w1Ok;
    memcpy(e.pair, pair, sizeof(e.pair));
    s->pt[0] = e;
    if (reset) {
        /* resetPointBlurTrail: every pair and screen position the first */
        for (int i = 1; i < s->num; i++) {
            s->pt[i] = e;
        }
        memcpy(s->w1, s->w0, sizeof(s->w1));
        s->w1Ok = 1;
    }
}

/* UpdatePointBlur's pair for one point, the current matrix screen */
static void pairOf(const EyeTrailPoint *e, void *view, int *out)
{
    static float zUnit[4] __attribute__((aligned(16))) = {0.0f, 0.0f, 1.0f, 0.0f};
    float a[4] __attribute__((aligned(16)));
    float b[4] __attribute__((aligned(16)));
    float c[4] __attribute__((aligned(16)));
    float s0[4] __attribute__((aligned(16)));
    float s1[4] __attribute__((aligned(16)));
    float w[4] __attribute__((aligned(16)));
    float scale;

    memcpy(w, e->world, sizeof(w));
    _ApplyMatrix(a, view, w);
    _RotTransPersCurrentMatrix(s0, a);
    a[0] = a[0] + e->f;
    _RotTransPersCurrentMatrix(b, a);
    scale = b[0] - s0[0];
    memcpy(w, e->prev, sizeof(w));
    _ApplyMatrix(a, view, w);
    _RotTransPersCurrentMatrix(s1, a);
    for (int k = 0; k < 4; k++) {
        s1[k] = ps2_operand(s1[k]); /* as the update reads screenPos[1] */
    }
    _SubVector(c, s1, s0);
    c[2] = 0.0f;
    _OuterProduct(c, c, zUnit);
    _NormalizeVector(c, c);
    _ScaleVector(c, c, scale);
    c[2] = 0.0f;
    _AddVectorXYZ(b, s0, c);
    _FTOI4Vector(out, b);
    _SubVectorXYZ(b, s0, c);
    _FTOI4Vector(out + 4, b);
}

int ico_eye_trail_rebuild(const void *key, int num, const int *strip, const void *view,
                          const void *screen, int *out, int cap)
{
    EyeTrailSlot *s = find(key);
    float cur[16] __attribute__((aligned(16)));
    float vm[16] __attribute__((aligned(16)));
    float sm[16] __attribute__((aligned(16)));

    if (s == NULL || s->num != num) {
        t.noTrail++;
        return 0;
    }
    if (num * 2 > cap) {
        t.tooLong++;
        return 0;
    }
    for (int i = 0; i < num; i++) {
        if (!s->pt[i].prevOk) {
            t.first++;
            return 0;
        }
        if (memcmp(strip + i * 8, s->pt[i].pair, sizeof(s->pt[i].pair)) != 0) {
            t.step++;
            return 0;
        }
    }
    memcpy(vm, view, sizeof(vm));
    memcpy(sm, screen, sizeof(sm));
    _GetCurrentMatrix(cur);
    _SetCurrentMatrix(sm);
    for (int i = 0; i < num; i++) {
        pairOf(&s->pt[i], vm, out + i * 8);
    }
    _SetCurrentMatrix(cur);
    t.rebuilt++;
    if (!t.logged) {
        t.logged = 1;
        fprintf(stderr, "photo: eye trails drawn from the photo camera (%d points each)\n", num);
    }
    return 1;
}

void ico_eye_trail_photo_end(void)
{
    const unsigned kept = t.noTrail + t.first + t.step + t.tooLong;

    if (t.rebuilt != 0 || kept != 0) {
        fprintf(stderr,
                "photo: eye trails: %u drawn from the photo camera, %u as the game drew them (%u "
                "with no trail, %u just started, %u not the trail's strip, %u too long)\n",
                t.rebuilt, kept, t.noTrail, t.first, t.step, t.tooLong);
    }
    t.rebuilt = t.noTrail = t.first = t.step = t.tooLong = 0;
    t.logged = 0;
}

void ico_eye_trail_counts(unsigned *rebuilt, unsigned *kept)
{
    if (rebuilt != NULL) {
        *rebuilt = t.rebuilt;
    }
    if (kept != NULL) {
        *kept = t.noTrail + t.first + t.step + t.tooLong;
    }
}

void ico_eye_trail_reset(void)
{
    memset(&t, 0, sizeof(t));
}
