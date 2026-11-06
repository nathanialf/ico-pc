/*
 * port/platform/bones_ladder.h
 *
 * Issue 123's ladder across starts (docs/port/VU_SKIN_CHECK.md "The
 * ladder"): what the window does with the skin check's evidence of a run
 * (port/render/rd.h rd_vu_bones_evidence) on a device where the twins of a
 * skinned draw differ (Mesa's Turnip on an Adreno: Yorda's shin).  Pure: no
 * file, no renderer; window_bones.c reads the keys and writes them.
 *
 *   step 1  [dev] vu_bones = ring: the next start reads the skinned draws'
 *           bones through the ring (a storage buffer) instead of a uniform
 *           block (port/render/rd_pipeline.c rd__vu_policy_init).  Written
 *           at the run's first evidence that the ring read clears it (the
 *           ring read's twin C equal to B, or a wrong probe whose column 3
 *           drew right), or after BONES_EVENTS_UNTESTED twin events with no
 *           evidence either way (its pipelines not made yet).
 *   step 2  [dev] turnip_debug = nobin: the next start sets TU_DEBUG=nobin
 *           before the Turnip package loads (Android only,
 *           android/gpu_driver_android.c; the package's library
 *           libvulkan_freedreno.so).  Written when the twins still differ
 *           under step 1, or straight away when the ring read beside the
 *           picture did not clear it, or the twins differed while the probe
 *           of the same command drew right (BONES_SCENE_EVENTS events, no
 *           wrong probe: the scene pass's execution, not the bone read).
 *           Kept only while the twins stay equal under it; else
 *           turnip_debug = off and the ladder stops.
 *   no step 3: the port's robust access ([dev] gpu_robust) is never
 *           touched (the user's decision, 2026-11-06).
 *
 * A step that keeps the twins equal over BONES_HEALED_REPLAYS replays is
 * "healed" (said, nothing written).  Wrong reads whose bytes changed under
 * the GPU (H2, a port race) are not the driver's: the ladder does not act.
 *
 * Every key the ladder writes carries a mark beside it, <key>_by =
 * "vu_skin_check <version> frame F; D of T twins differed" (the version,
 * the frame of the run's last twin event, the twins that differed of those
 * read in that run), which the start line repeats.
 */
#ifndef ICO_PLATFORM_BONES_LADDER_H
#define ICO_PLATFORM_BONES_LADDER_H

#include <stddef.h>
#include <stdint.h>

#define BONES_HEALED_REPLAYS 2000u
#define BONES_EVENTS_UNTESTED 8u
#define BONES_SCENE_EVENTS 2u

/* the run's state and evidence */
typedef struct BonesLadderIn {
    int ring;       /* step 1 in force this start (the renderer's word) */
    int nobin;      /* step 2 in force this start (TU_DEBUG=nobin was set) */
    int nobinOff;   /* [dev] turnip_debug = off: step 2 tried and did not clear, or refused */
    int nobinAvail; /* the next start can set TU_DEBUG (Android, a Turnip package) */
    uint32_t replays, twins, differ;
    uint32_t ringClear, ringFail, sceneOnly, probeWrong, portRace;
    uint32_t addr; /* the bone address the last wrong probe named (0: none) */
} BonesLadderIn;

typedef enum BonesAction {
    BONES_WAIT = 0,    /* nothing to do yet */
    BONES_WRITE_RING,  /* step 1 for the next start */
    BONES_WRITE_NOBIN, /* step 2 for the next start */
    BONES_WRITE_OFF,   /* step 2 did not clear: turnip_debug = off, the ladder stops */
    BONES_STOP,        /* nothing left to try here: said, nothing written */
    BONES_HEALED,      /* the step in force kept the twins equal: said */
    BONES_PORT_RACE    /* the ring's bytes changed under the GPU (H2): the ladder does not act */
} BonesAction;

/* the action for the run so far (called again as the evidence grows; the
   window acts once on each of WRITE_*, STOP and PORT_RACE, and says
   HEALED once) */
BonesAction bones_ladder_step(const BonesLadderIn *in);

/* the mark beside a key the ladder writes */
typedef struct BonesMark {
    char version[32];
    uint32_t frame, differ, twins;
} BonesMark;

/* "vu_skin_check <version> frame F; D of T twins differed"; 0, or -1 when
   out is too small */
int bones_mark_format(char *out, size_t size, const BonesMark *m);
/* 0 when text is such a mark (m filled), -1 otherwise (absent, the
   player's own words) */
int bones_mark_parse(const char *text, BonesMark *m);

/* The start's line (the window's first pump) from the keys as config.toml
   holds them (NULL: absent) and whether TU_DEBUG=nobin was set at this
   start: 0 with the line in out, -1 when no key of the ladder is set. */
int bones_ladder_start_line(char *out, size_t size, const char *vuBones, const char *vuBonesBy,
                            const char *turnipDebug, const char *turnipDebugBy, int nobinSet);

/* The line for an action (not BONES_WAIT): written says whether the key
   reached config.toml (cfgName the file's name); 0, or -1 for WAIT. */
int bones_ladder_action_line(char *out, size_t size, BonesAction a, const BonesLadderIn *in,
                             int written, const char *cfgName);

#endif /* ICO_PLATFORM_BONES_LADDER_H */
