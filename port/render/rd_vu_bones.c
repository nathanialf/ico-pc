/* rd_vu_bones.c: issue 123's ladder, the renderer's side (rd_vu_skin_check.h
 * "the ladder's side", rd.h rd_vu_bones_evidence; docs/port/VU_SKIN_CHECK.md
 * "The ladder").
 *
 * On Mesa's Turnip the twins of a skinned draw (rd_vu_skin_check.c) can
 * differ: the bones read as a uniform block at the draw's offset gave other
 * bytes than the CPU wrote (Yorda's shin, v0.10.3's streak).  Step 1 of the
 * ladder reads the same bytes through the ring instead, a storage buffer:
 * the cluster entries' ring builds (vu_common.hlsli ICO_VU_BONES_RING), the
 * block's qword index in VuCB's mark lane (rd_replay_vu.c vuSource).  The
 * check draws them beside the picture before they are switched in (column 3
 * of the probe, twin C inside the pass); the window (window_bones.c) reads
 * the run's evidence from here and writes [dev] vu_bones = ring for the
 * next start, where rd__vu_policy_init puts the ring builds in place of the
 * plain variants.  Step 2 (TU_DEBUG=nobin, the Turnip package's binning
 * off) is the window's and the Android driver loader's alone. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd.h"
#include "rd_vu_skin_check.h"

#define RB_VS_COUNT 3u /* RD_VS_VU_SKIN, _SKIN_SPEC, _SKIN_DEBUG */

/* the ring builds made here (the check's column 3 and twin C) while the
 * run's entries are the plain variants */
static RhiShader s_ring[RB_VS_COUNT];
static bool s_tried[RB_VS_COUNT];

/* the evidence (rd.h RdVuBonesEvidence), each word an atomic: written on
 * the render thread at the harvest, read by the window's pump */
enum {
    EV_STARTED,
    EV_USABLE,
    EV_RING,
    EV_REPLAYS,
    EV_TWINS,
    EV_DIFFER,
    EV_RING_CLEAR,
    EV_RING_FAIL,
    EV_SCENE_ONLY,
    EV_PROBE_WRONG,
    EV_PORT_RACE,
    EV_ADDR,
    EV_FRAME,
    EV_COUNT
};

static uint32_t s_ev[EV_COUNT];
/* the draws not drawn under the ring builds: no index (rd__vu_bones_ring_miss) */
static uint32_t s_miss;

static uint32_t evGet(int i)
{
    return __atomic_load_n(&s_ev[i], __ATOMIC_RELAXED);
}

static void evAdd(int i, uint32_t n)
{
    (void)__atomic_add_fetch(&s_ev[i], n, __ATOMIC_RELAXED);
}

static void evSet(int i, uint32_t v)
{
    __atomic_store_n(&s_ev[i], v, __ATOMIC_RELAXED);
}

void rd__vu_bones_ring_name(uint8_t vs, char *out, size_t size)
{
    const char *nm = rd__vs_name(vs); /* "vu_skin_vs" */
    const size_t n = strlen(nm);
    snprintf(out, size, "%.*s_ring_plain_vs", (int)(n > 3 ? n - 3 : 0), nm);
}

RhiShader rd__vu_bones_ring_shader(uint8_t vs)
{
    if (vs < RD_VS_VU_SKIN || vs > RD_VS_VU_SKIN_DEBUG) {
        return (RhiShader){0};
    }
    if (g_rd.vuBonesRing) {
        return g_rd.vsPlain[vs]; /* the run's own entries (rd__vu_policy_init) */
    }
    if (!g_rd.vuFallbackPlain) {
        /* the ring builds are plain variants: beside the extended set they
         * would not draw what the draw draws */
        return (RhiShader){0};
    }
    const uint32_t i = (uint32_t)(vs - RD_VS_VU_SKIN);
    if (!s_tried[i]) {
        s_tried[i] = true;
        char name[64];
        rd__vu_bones_ring_name(vs, name, sizeof(name));
        s_ring[i] = rd__make_shader(name);
    }
    return s_ring[i];
}

/* rd_set_vu_bones: the window's choice in the run, read by rd_init on the
 * same thread (no environment written while other threads read it) */
static char s_setMode[16];
static char s_setBy[160];
static bool s_set;

void rd_set_vu_bones(const char *mode, const char *by)
{
    snprintf(s_setMode, sizeof(s_setMode), "%s", mode ? mode : "");
    snprintf(s_setBy, sizeof(s_setBy), "%s", by ? by : "");
    s_set = mode != NULL;
}

const char *rd__vu_bones_asked(void)
{
    return s_set ? s_setMode : getenv("ICO_RD_VU_BONES");
}

const char *rd__vu_bones_by(void)
{
    return s_set ? s_setBy : getenv("ICO_RD_VU_BONES_BY");
}

void rd__vu_bones_policy(bool asked, bool on, bool usable, const char *why)
{
    evSet(EV_RING, on ? 1u : 0u);
    evSet(EV_USABLE, usable ? 1u : 0u);
    evSet(EV_STARTED, 1u);
    if (!on) {
        if (asked) {
            rd__log("VU bones: through VuBoneCB ([dev] vu_bones = ring not used: %s)",
                    why ? why : "?");
        } else {
            rd__log("VU bones: through VuBoneCB");
        }
        return;
    }
    /* the mark the window wrote beside the key ([dev] vu_bones_by, exported
     * as ICO_RD_VU_BONES_BY: "vu_skin_check <version> frame F; D of T twins
     * differed") */
    const char *by = rd__vu_bones_by();
    char words[192];
    if (by && strncmp(by, "vu_skin_check ", 14) == 0) {
        snprintf(words, sizeof(words), "written by the skin check at %s", by + 14);
    } else if (by && by[0]) {
        snprintf(words, sizeof(words), "vu_bones_by = %s", by);
    } else {
        snprintf(words, sizeof(words), "set without the skin check's mark");
    }
    rd__log("VU bones: through the ring (storage buffer) on the skinned entries ([dev] vu_bones = "
            "ring, %s)",
            words);
}

void rd__vu_bones_ring_miss(uint64_t bonesOff)
{
    if (s_miss++ == 0) {
        rd__log("VU bones: a skinned draw's bone block (ring offset %llu) lies past what t5 "
                "holds: not drawn (the ring read has no index for it; counted at the end of "
                "the run)",
                (unsigned long long)bonesOff);
    }
}

void rd__vu_bones_twins_read(uint32_t twins, uint32_t differ)
{
    if (!twins) {
        return;
    }
    evAdd(EV_REPLAYS, 1u);
    evAdd(EV_TWINS, twins);
    evAdd(EV_DIFFER, differ);
}

void rd__vu_bones_event(uint32_t frame, int ring, int probe)
{
    evSet(EV_FRAME, frame);
    if (ring == 1) {
        evAdd(EV_RING_CLEAR, 1u);
    } else if (ring == 0) {
        evAdd(EV_RING_FAIL, 1u);
    }
    if (probe == 1) {
        evAdd(EV_SCENE_ONLY, 1u);
    } else if (probe == 0) {
        evAdd(EV_PROBE_WRONG, 1u);
    }
}

void rd__vu_bones_check(bool bytesDiffer, int c3, uint32_t addr)
{
    if (bytesDiffer) {
        evAdd(EV_PORT_RACE, 1u); /* H2: the ring's bytes, not the driver's read */
        return;
    }
    if (addr) {
        evSet(EV_ADDR, addr);
    }
    if (c3 == 1) {
        evAdd(EV_RING_CLEAR, 1u);
    } else if (c3 == 0) {
        evAdd(EV_RING_FAIL, 1u);
    }
}

void rd_vu_bones_evidence(RdVuBonesEvidence *out)
{
    memset(out, 0, sizeof(*out));
    out->started = evGet(EV_STARTED) != 0;
    out->ring = evGet(EV_RING) != 0;
    out->ringUsable = evGet(EV_USABLE) != 0;
    out->replays = evGet(EV_REPLAYS);
    out->twins = evGet(EV_TWINS);
    out->differ = evGet(EV_DIFFER);
    out->ringClear = evGet(EV_RING_CLEAR);
    out->ringFail = evGet(EV_RING_FAIL);
    out->sceneOnly = evGet(EV_SCENE_ONLY);
    out->probeWrong = evGet(EV_PROBE_WRONG);
    out->portRace = evGet(EV_PORT_RACE);
    out->addr = evGet(EV_ADDR);
    out->frame = evGet(EV_FRAME);
}

void rd__vu_bones_shutdown(void)
{
    const uint32_t twins = evGet(EV_TWINS), differ = evGet(EV_DIFFER);
    if (evGet(EV_RING) && twins) {
        /* the run's verdict on step 1 (the window's ladder decides the next
         * start from the same counts) */
        if (differ == 0) {
            rd__log("VU skin check: the ring read: healed (0 of %u twins differ over %u "
                    "replays)",
                    twins, evGet(EV_REPLAYS));
        } else {
            rd__log("VU skin check: the ring read: not healed (%u of %u twins differ over %u "
                    "replays)",
                    differ, twins, evGet(EV_REPLAYS));
        }
    }
    if (s_miss) {
        rd__log("VU bones: %u skinned draws not drawn: their bone block past what t5 holds",
                s_miss);
    }
    for (uint32_t i = 0; i < RB_VS_COUNT; i++) {
        if (s_ring[i].id && g_rd.hasDevice) {
            rhi_destroy_shader(s_ring[i]);
        }
        s_ring[i] = (RhiShader){0};
        s_tried[i] = false;
    }
    for (int i = 0; i < EV_COUNT; i++) {
        evSet(i, 0u);
    }
    s_miss = 0;
}
