/*
 * port/platform/bones_ladder.h
 *
 * Issue 123's ladder across starts (docs/port/VU_SKIN_CHECK.md "The
 * ladder"): what the window does with the skin check's evidence of a run
 * (port/render/rd.h rd_vu_bones_evidence) on a device where the twins of a
 * skinned draw differ (Mesa's Turnip on an Adreno: Yorda's shin).  Pure: no
 * file, no renderer; window_bones.c reads the keys and writes them.
 *
 *   step 1  [dev] vu_bones = ring: the skinned draws' bones read through the
 *           ring (a storage buffer) instead of a uniform block
 *           (port/render/rd_pipeline.c rd__vu_policy_init), in the same
 *           run: the window sets ICO_RD_VU_BONES and makes the renderer
 *           again in place (window_switch.h ico_window_switch_restart, as
 *           Options > Graphics > Apply does; bones_restart_step's guards),
 *           and every later start reads the key.  Written
 *           at the run's first evidence that the ring read clears it (the
 *           ring read's twin C equal to B, or a wrong probe whose column 3
 *           drew right), or after BONES_EVENTS_UNTESTED twin events with no
 *           evidence either way (its pipelines not made yet).
 *   step 2  [dev] turnip_debug = nobin: the next start (the driver library
 *           is loaded once a process) sets TU_DEBUG=nobin
 *           before the Turnip package loads (Android only,
 *           android/gpu_driver_android.c; the package's library
 *           libvulkan_freedreno.so).  Written when the twins still differ
 *           under step 1, or straight away when the ring read beside the
 *           picture did not clear it (BONES_SCENE_EVENTS times: the twin C
 *           or, since v0.11.0, the twins of a pre-pass record draw, which
 *           read the bones through the ring as the record did), or the
 *           twins differed while the probe
 *           of the same command drew right (BONES_SCENE_EVENTS events, no
 *           wrong probe: the scene pass's execution, not the bone read).
 *           Under step 1 it takes BONES_NOBIN_EVENTS twin events, as
 *           many as step 1 itself takes without evidence (v0.10.5 wrote
 *           it on one).  Written with the mark's word "trial".  The twins
 *           differing under it: turnip_debug = off and the ladder stops.
 *   the re-test of step 2 (v0.11.0; nobin binds every pass in system
 *           memory, a cost no step should keep without evidence): healed
 *           under step 2 with step 1 also in force writes turnip_debug =
 *           retest; the next start runs without TU_DEBUG=nobin (step 1
 *           alone).  BONES_HEALED_REPLAYS replays there with no twin
 *           differing remove the key (left empty, which every reader
 *           takes for absent); BONES_NOBIN_EVENTS twin events write nobin
 *           back with the word "needed", which is kept and never re-tested
 *           again.  A turnip_debug = nobin with neither word (a v0.10.5
 *           ladder's, or set by hand) is a re-test at the next start.
 *   no step 3: the port's robust access ([dev] gpu_robust) is never
 *           touched (the user's decision, 2026-11-06).
 *
 * A step that keeps the twins equal over BONES_HEALED_REPLAYS replays is
 * "healed" (said, the key kept, but for the re-test above).  Wrong reads
 * whose bytes changed under the GPU (H2, a port race) are not the
 * driver's: the ladder does not act.
 *
 * Every key the ladder writes carries a mark beside it, <key>_by =
 * "vu_skin_check <version> frame F; D of T twins differed[; WORD]" (the
 * version, the frame of the run's last twin event, the twins that differed
 * of those read in that run; turnip_debug = nobin's word trial or needed),
 * which the start line repeats.
 */
#ifndef ICO_PLATFORM_BONES_LADDER_H
#define ICO_PLATFORM_BONES_LADDER_H

#include <stddef.h>
#include <stdint.h>

#define BONES_HEALED_REPLAYS 2000u
#define BONES_EVENTS_UNTESTED 8u
#define BONES_SCENE_EVENTS 2u
/* step 2 under step 1, and the re-test's verdict that nobin is needed */
#define BONES_NOBIN_EVENTS 8u

/* the run's state and evidence */
typedef struct BonesLadderIn {
    int ring;        /* step 1 in force this start (the renderer's word) */
    int nobin;       /* step 2 in force this start (TU_DEBUG=nobin was set) */
    int nobinOff;    /* [dev] turnip_debug = off: step 2 tried and did not clear, or refused */
    int nobinAvail;  /* the next start can set TU_DEBUG (Android, a Turnip package) */
    int nobinNeeded; /* step 2 in force with the mark's word "needed": never re-tested */
    int retest;      /* the re-test of step 2 at this start (turnip_debug = retest, or nobin
                        without the mark's word): TU_DEBUG=nobin not set */
    int ringUsable;  /* the ring builds can be put in force (the plain set on Vulkan) */
    uint32_t replays, twins, differ;
    uint32_t ringClear, ringFail, sceneOnly, probeWrong, portRace;
    uint32_t addr; /* the bone address the last wrong probe named (0: none) */
} BonesLadderIn;

typedef enum BonesAction {
    BONES_WAIT = 0,     /* nothing to do yet */
    BONES_WRITE_RING,   /* step 1 for the next start */
    BONES_WRITE_NOBIN,  /* step 2 for the next start */
    BONES_WRITE_OFF,    /* step 2 did not clear: turnip_debug = off, the ladder stops */
    BONES_STOP,         /* nothing left to try here: said, nothing written */
    BONES_HEALED,       /* the step in force kept the twins equal: said */
    BONES_PORT_RACE,    /* the ring's bytes changed under the GPU (H2): the ladder does not act */
    BONES_WAIT_PLAIN,   /* step 1 would be next, but the ring builds cannot be used here (the
                          program entries are not the plain set on Vulkan): said once, nothing
                          written, the ladder goes on */
    BONES_WRITE_RETEST, /* step 2 healed with step 1 in force: turnip_debug = retest */
    BONES_RETEST_CLEAR, /* the re-test kept the twins equal: turnip_debug removed */
    BONES_WRITE_NOBIN_NEEDED /* the re-test's twins differed: nobin with the word "needed" */
} BonesAction;

/* [dev] turnip_debug as the start reads it with its mark (bones_turnip_state) */
typedef enum BonesTurnip {
    BONES_TU_NONE = 0, /* absent, empty, or a value the ladder does not write */
    BONES_TU_TRIAL,    /* nobin, the mark's word "trial": step 2 in force */
    BONES_TU_NEEDED,   /* nobin, the mark's word "needed": step 2 in force, kept */
    BONES_TU_UNMARKED, /* nobin without either word: a re-test at this start */
    BONES_TU_RETEST,   /* retest: a re-test at this start */
    BONES_TU_OFF       /* off: the ladder stopped */
} BonesTurnip;

BonesTurnip bones_turnip_state(const char *turnipDebug, const char *turnipDebugBy);
/* whether TU_DEBUG=nobin is to be set before the Turnip package loads
   (trial or needed; android/gpu_driver_android.c) */
int bones_nobin_wanted(const char *turnipDebug, const char *turnipDebugBy);

/* the action for the run so far (called again as the evidence grows; the
   window acts once on each of WRITE_*, STOP and PORT_RACE, and says
   HEALED once) */
BonesAction bones_ladder_step(const BonesLadderIn *in);

/* Step 1's switch in the run (window_bones.c): BONES_RESTART_ASK when the
   key was written and the switch is still wanted (pending) and not asked
   yet this run (asked), with no movie playing and BONES_RESTART_AFTER_S
   seconds of the run gone (uptimeS); BONES_RESTART_WAIT while a movie
   plays or before that; BONES_RESTART_NONE otherwise (nothing pending, or
   asked once already: at most once a run) */
#define BONES_RESTART_AFTER_S 30.0

enum { BONES_RESTART_NONE = 0, BONES_RESTART_ASK, BONES_RESTART_WAIT };

int bones_restart_step(int pending, int asked, int movie, double uptimeS);

/* Whether step 2 is in force at this start: [dev] turnip_debug = nobin
   with the mark's word trial or needed (bones_nobin_wanted), TU_DEBUG
   holding nobin, and the Turnip package actually loaded (the variable is
   set before the open and stays when the open fails or the phone's own
   driver runs) */
int bones_nobin_in_force(const char *turnipDebug, const char *turnipDebugBy, const char *tuDebugEnv,
                         int turnipLoaded);

/* the mark beside a key the ladder writes */
typedef struct BonesMark {
    char version[32];
    uint32_t frame, differ, twins;
    char word[16]; /* "" or turnip_debug = nobin's "trial" or "needed" */
} BonesMark;

/* "vu_skin_check <version> frame F; D of T twins differed[; WORD]"; 0, or
   -1 when out is too small */
int bones_mark_format(char *out, size_t size, const BonesMark *m);
/* 0 when text is such a mark (m filled; the word "trial" or "needed" or
   none), -1 otherwise (absent, the player's own words) */
int bones_mark_parse(const char *text, BonesMark *m);

/* The start's line (the window's first pump) from the keys as config.toml
   holds them (NULL: absent) and whether TU_DEBUG=nobin was set at this
   start: 0 with the line in out, -1 when no key of the ladder is set. */
int bones_ladder_start_line(char *out, size_t size, const char *vuBones, const char *vuBonesBy,
                            const char *turnipDebug, const char *turnipDebugBy, int nobinSet);

/* Step 1's written line by when it takes effect: BONES_RING_NOW (the
   renderer made again in place now: "in force from the next picture"),
   BONES_RING_WAITS (the switch waits for `wait`'s words), BONES_RING_NEXT
   (the switch refused or not asked: the next start) */
enum { BONES_RING_NEXT = 0, BONES_RING_NOW, BONES_RING_WAITS };

int bones_ring_line(char *out, size_t size, const BonesLadderIn *in, int written,
                    const char *cfgName, int when, const char *wait);
/* the line before step 1's switch in the run */
int bones_restart_line(char *out, size_t size);

/* The line for an action (not BONES_WAIT): written says whether the key
   reached config.toml (cfgName the file's name; BONES_RETEST_CLEAR:
   whether it was removed); 0, or -1 for WAIT.  BONES_WRITE_RING's is
   bones_ring_line's BONES_RING_NEXT. */
int bones_ladder_action_line(char *out, size_t size, BonesAction a, const BonesLadderIn *in,
                             int written, const char *cfgName);

#endif /* ICO_PLATFORM_BONES_LADDER_H */
