/*
 * port/platform/bones_ladder.c
 *
 * Issue 123's ladder across starts (bones_ladder.h).
 */
#include "bones_ladder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

BonesAction bones_ladder_step(const BonesLadderIn *in)
{
    if (in->portRace > 0) {
        return BONES_PORT_RACE; /* H2: the ring's bytes, not the driver's read */
    }
    if (in->retest && in->nobinAvail) {
        /* the re-test of step 2, judged only where nobin could be set (a
           Turnip package): as much evidence either way as the step took */
        if (in->differ >= BONES_NOBIN_EVENTS) {
            return BONES_WRITE_NOBIN_NEEDED;
        }
        if (in->differ == 0 && in->replays >= BONES_HEALED_REPLAYS) {
            return BONES_RETEST_CLEAR;
        }
        return BONES_WAIT;
    }
    if (in->differ == 0) {
        if ((in->ring || in->nobin) && in->replays >= BONES_HEALED_REPLAYS) {
            /* step 2 healed with step 1 beside it: whether step 1 alone
               keeps them equal is tried at the next start (not once the
               re-test said nobin is needed, nor without step 1: nothing
               else would cover the fault then) */
            return in->nobin && in->ring && !in->nobinNeeded ? BONES_WRITE_RETEST : BONES_HEALED;
        }
        return BONES_WAIT;
    }
    if (in->nobin) {
        return BONES_WRITE_OFF; /* step 2 in force and the twins still differ */
    }
    if (in->ring) {
        /* step 1 in force and the twins still differ: step 2 after as many
           events as step 1 takes untested (one stray event is not enough
           for a step that costs every pass its binning) */
        if (in->differ < BONES_NOBIN_EVENTS) {
            return BONES_WAIT;
        }
        return in->nobinAvail && !in->nobinOff ? BONES_WRITE_NOBIN : BONES_STOP;
    }
    /* neither step in force: the evidence beside the picture; step 1 only
       where the ring builds can be put in force (on the extended set the
       renderer refuses them: a restart and a key for nothing) */
    if (in->ringClear > 0) {
        return in->ringUsable ? BONES_WRITE_RING : BONES_WAIT_PLAIN;
    }
    if (in->ringFail >= BONES_SCENE_EVENTS ||
        (in->sceneOnly >= BONES_SCENE_EVENTS && in->probeWrong == 0)) {
        /* the ring read does not clear it (twice: since v0.11.0 the twins of
           a pre-pass record draw are ring reads themselves, one event each),
           or the scene pass's execution: step 1 skipped */
        return in->nobinAvail && !in->nobinOff ? BONES_WRITE_NOBIN : BONES_STOP;
    }
    if (in->differ >= BONES_EVENTS_UNTESTED) {
        /* untested beside the picture: step 1 first */
        return in->ringUsable ? BONES_WRITE_RING : BONES_WAIT_PLAIN;
    }
    return BONES_WAIT;
}

int bones_mark_format(char *out, size_t size, const BonesMark *m)
{
    const int n =
        snprintf(out, size, "vu_skin_check %s frame %u; %u of %u twins differed%s%s",
                 m->version[0] != '\0' ? m->version : "dev", (unsigned)m->frame,
                 (unsigned)m->differ, (unsigned)m->twins, m->word[0] ? "; " : "", m->word);
    return n < 0 || (size_t)n >= size ? -1 : 0;
}

int bones_mark_parse(const char *text, BonesMark *m)
{
    char version[32];
    unsigned frame, differ, twins;
    int used = 0;

    memset(m, 0, sizeof(*m));
    if (text == NULL || sscanf(text, "vu_skin_check %31s frame %u; %u of %u twins differed%n",
                               version, &frame, &differ, &twins, &used) != 4) {
        return -1;
    }
    if (strcmp(text + used, "; trial") == 0 || strcmp(text + used, "; needed") == 0) {
        snprintf(m->word, sizeof(m->word), "%s", text + used + 2);
    } else if (text[used] != '\0') {
        memset(m, 0, sizeof(*m));
        return -1;
    }
    snprintf(m->version, sizeof(m->version), "%s", version);
    m->frame = frame;
    m->differ = differ;
    m->twins = twins;
    return 0;
}

static int is(const char *v, const char *word)
{
    return v != NULL && strcmp(v, word) == 0;
}

BonesTurnip bones_turnip_state(const char *turnipDebug, const char *turnipDebugBy)
{
    BonesMark m;

    if (is(turnipDebug, "off")) {
        return BONES_TU_OFF;
    }
    if (is(turnipDebug, "retest")) {
        return BONES_TU_RETEST;
    }
    if (!is(turnipDebug, "nobin")) {
        return BONES_TU_NONE;
    }
    if (bones_mark_parse(turnipDebugBy, &m) == 0) {
        if (strcmp(m.word, "needed") == 0) {
            return BONES_TU_NEEDED;
        }
        if (strcmp(m.word, "trial") == 0) {
            return BONES_TU_TRIAL;
        }
    }
    return BONES_TU_UNMARKED;
}

int bones_nobin_wanted(const char *turnipDebug, const char *turnipDebugBy)
{
    const BonesTurnip t = bones_turnip_state(turnipDebug, turnipDebugBy);

    return t == BONES_TU_TRIAL || t == BONES_TU_NEEDED;
}

/* "written by the skin check at 0.10.5 frame 812; 3 of 5200 twins
   differed" or the player's words */
static void mark_words(char *out, size_t size, const char *by)
{
    BonesMark m;

    if (bones_mark_parse(by, &m) == 0) {
        snprintf(out, size, "written by the skin check at %s frame %u; %u of %u twins differed",
                 m.version, (unsigned)m.frame, (unsigned)m.differ, (unsigned)m.twins);
    } else {
        snprintf(out, size, "set without the skin check's mark");
    }
}

int bones_ladder_start_line(char *out, size_t size, const char *vuBones, const char *vuBonesBy,
                            const char *turnipDebug, const char *turnipDebugBy, int nobinSet)
{
    char words[160];
    BonesMark m;
    const int ring = is(vuBones, "ring");
    const BonesTurnip tu = bones_turnip_state(turnipDebug, turnipDebugBy);

    if (tu == BONES_TU_RETEST || tu == BONES_TU_UNMARKED) {
        mark_words(words, sizeof(words), turnipDebugBy);
        snprintf(
            out, size,
            "window: VU skin check ladder: the re-test of step 2 at this start: TU_DEBUG=nobin "
            "not set (%s, %s)%s; %u presents with no twin differing remove the key, %u twin "
            "events write nobin back as needed",
            tu == BONES_TU_RETEST ? "[dev] turnip_debug = retest"
                                  : "[dev] turnip_debug = nobin without the word trial or needed: "
                                    "taken as retest",
            words, ring ? "; step 1 (ring read) in force" : "", (unsigned)BONES_HEALED_REPLAYS,
            (unsigned)BONES_NOBIN_EVENTS);
        return 0;
    }
    if (tu == BONES_TU_TRIAL || tu == BONES_TU_NEEDED) {
        if (!nobinSet) {
            snprintf(out, size,
                     "window: VU skin check ladder: [dev] turnip_debug = nobin, but TU_DEBUG was "
                     "not set at this start (only before a Turnip driver package loads on "
                     "Android): step 2 is not in force");
            return 0;
        }
        if (bones_mark_parse(turnipDebugBy, &m) == 0) {
            snprintf(words, sizeof(words), "%u of %u", (unsigned)m.differ, (unsigned)m.twins);
        } else {
            snprintf(words, sizeof(words), "set without the skin check's mark");
        }
        snprintf(out, size,
                 "window: VU skin check ladder: step 2 of 2 (TU_DEBUG=nobin) for this start%s; "
                 "step 1 (ring read) %s (%s)",
                 tu == BONES_TU_NEEDED ? ", needed (the re-test without it saw the twins differ): "
                                         "kept, not re-tested"
                                       : "",
                 ring ? "did not clear the twins"
                      : "was skipped: what the check saw beside the picture pointed past it",
                 words);
        return 0;
    }
    if (is(turnipDebug, "off")) {
        mark_words(words, sizeof(words), turnipDebugBy);
        snprintf(out, size,
                 "window: VU skin check ladder: %s ([dev] turnip_debug = off, %s); the ladder "
                 "stops%s",
                 ring ? "both tried, neither cleared the twins"
                      : "step 2 (TU_DEBUG=nobin) did not clear the twins",
                 words, ring ? "; step 1 (ring read) stays in force" : "");
        return 0;
    }
    if (ring) {
        mark_words(words, sizeof(words), vuBonesBy);
        snprintf(out, size,
                 "window: VU skin check ladder: step 1 of 2 (the ring read) for this start ([dev] "
                 "vu_bones = ring, %s)",
                 words);
        return 0;
    }
    return -1;
}

int bones_nobin_in_force(const char *turnipDebug, const char *turnipDebugBy, const char *tuDebugEnv,
                         int turnipLoaded)
{
    return bones_nobin_wanted(turnipDebug, turnipDebugBy) && tuDebugEnv != NULL &&
           strstr(tuDebugEnv, "nobin") != NULL && turnipLoaded;
}

int bones_restart_step(int pending, int asked, int movie, double uptimeS)
{
    if (!pending || asked) {
        return BONES_RESTART_NONE;
    }
    if (movie || uptimeS < BONES_RESTART_AFTER_S) {
        return BONES_RESTART_WAIT;
    }
    return BONES_RESTART_ASK;
}

int bones_restart_line(char *out, size_t size)
{
    const int n = snprintf(out, size,
                           "window: VU bones: switching to the ring read now (the renderer made "
                           "again in place, as Options > Graphics > Apply does); [dev] vu_bones = "
                           "ring written for every later start");
    return n < 0 || (size_t)n >= size ? -1 : 0;
}

int bones_ring_line(char *out, size_t size, const BonesLadderIn *in, int written,
                    const char *cfgName, int when, const char *wait)
{
    char addr[48], beside[160], tail[160];
    const char *cfg = cfgName != NULL ? cfgName : "config.toml";

    if (in->addr) {
        snprintf(addr, sizeof(addr), "bone address %u", (unsigned)in->addr);
    } else {
        snprintf(addr, sizeof(addr), "a bone address the probe did not name");
    }
    if (in->ringClear) {
        snprintf(beside, sizeof(beside), "the ring read beside the picture cleared it %u times",
                 (unsigned)in->ringClear);
    } else {
        snprintf(beside, sizeof(beside),
                 "the ring read untested beside the picture: its pipelines not made yet");
    }
    if (!written) {
        snprintf(tail, sizeof(tail), "the bones are read as before");
    } else if (when == BONES_RING_NOW) {
        snprintf(tail, sizeof(tail), "in force from the next picture");
    } else if (when == BONES_RING_WAITS) {
        snprintf(tail, sizeof(tail),
                 "the switch to the ring read waits for %s (else the next start reads it)",
                 wait != NULL ? wait : "?");
    } else {
        snprintf(tail, sizeof(tail),
                 "the next start reads the skinned draws' bones through the ring");
    }
    const int n = snprintf(out, size,
                           "window: VU bones: [dev] vu_bones = ring %s (the skin check saw %u twin "
                           "events at %s over %u presents); %s; %s (%s)",
                           written ? "is written" : "could not be written", (unsigned)in->differ,
                           addr, (unsigned)in->replays, beside, tail, cfg);
    return n < 0 ? -1 : 0;
}

int bones_ladder_action_line(char *out, size_t size, BonesAction a, const BonesLadderIn *in,
                             int written, const char *cfgName)
{
    const char *cfg = cfgName != NULL ? cfgName : "config.toml";
    const char *put = written ? "is written to" : "could not be written to";

    switch (a) {
    case BONES_WRITE_RING:
        return bones_ring_line(out, size, in, written, cfgName, BONES_RING_NEXT, NULL);
    case BONES_WRITE_NOBIN:
        snprintf(out, size,
                 "window: VU skin check ladder: step 2 of 2 (TU_DEBUG=nobin) for the next start "
                 "(the driver library is loaded once a process): [dev] turnip_debug = nobin %s %s; "
                 "step 1 (ring read) %s (%u of %u)",
                 put, cfg,
                 in->ring ? "did not clear the twins"
                 : in->ringFail > 0
                     ? "was skipped: the ring read beside the picture did not clear the twins"
                     : "was skipped: the twins differed while the probe of the same command drew "
                       "right (the scene pass's execution)",
                 (unsigned)in->differ, (unsigned)in->twins);
        return 0;
    case BONES_WRITE_OFF:
        snprintf(out, size,
                 "window: VU skin check ladder: %s (%u of %u under TU_DEBUG=nobin); [dev] "
                 "turnip_debug = off %s %s and the ladder stops",
                 in->ring ? "both tried, neither cleared the twins"
                          : "step 2 (TU_DEBUG=nobin) did not clear the twins",
                 (unsigned)in->differ, (unsigned)in->twins, put, cfg);
        return 0;
    case BONES_STOP:
        if (in->nobinOff) {
            snprintf(out, size,
                     "window: VU skin check ladder: both tried, neither cleared the twins (%u of "
                     "%u%s; [dev] turnip_debug = off): the ladder stops",
                     (unsigned)in->differ, (unsigned)in->twins,
                     in->ring ? " under the ring read" : "");
        } else {
            snprintf(out, size,
                     "window: VU skin check ladder: step 1 (ring read) %s the twins (%u of %u); "
                     "step 2 (TU_DEBUG=nobin) needs a Turnip driver package on Android, not this "
                     "device's driver: the ladder stops",
                     in->ring ? "did not clear" : "does not clear", (unsigned)in->differ,
                     (unsigned)in->twins);
        }
        return 0;
    case BONES_HEALED:
        snprintf(out, size,
                 "window: VU skin check ladder: %s healed the twins: 0 of %u differ over %u "
                 "presents%s",
                 in->nobin ? "step 2 (TU_DEBUG=nobin)" : "step 1 (ring read)", (unsigned)in->twins,
                 (unsigned)in->replays,
                 !in->nobin        ? ""
                 : in->nobinNeeded ? "; [dev] turnip_debug = nobin is kept (needed: the re-test "
                                     "without it saw the twins differ)"
                 : !in->ring       ? "; [dev] turnip_debug = nobin is kept (step 1 not in force: "
                                     "nothing else would cover the fault)"
                                   : "; [dev] turnip_debug = nobin is kept");
        return 0;
    case BONES_WRITE_RETEST:
        snprintf(out, size,
                 "window: VU skin check ladder: step 2 (TU_DEBUG=nobin) healed the twins with step "
                 "1 (ring read) beside it: 0 of %u differ over %u presents; [dev] turnip_debug = "
                 "retest %s %s: the next start runs without TU_DEBUG=nobin, to see whether step 1 "
                 "alone keeps them equal",
                 (unsigned)in->twins, (unsigned)in->replays, put, cfg);
        return 0;
    case BONES_RETEST_CLEAR:
        snprintf(out, size,
                 "window: VU skin check ladder: the re-test without TU_DEBUG=nobin kept the twins "
                 "equal: 0 of %u differ over %u presents; [dev] turnip_debug %s %s: step 2 not "
                 "needed%s",
                 (unsigned)in->twins, (unsigned)in->replays,
                 written ? "is removed from" : "could not be removed from", cfg,
                 in->ring ? "; step 1 (ring read) stays in force" : "");
        return 0;
    case BONES_WRITE_NOBIN_NEEDED:
        snprintf(out, size,
                 "window: VU skin check ladder: the re-test without TU_DEBUG=nobin: the twins "
                 "differ again (%u of %u); [dev] turnip_debug = nobin %s %s, marked needed: step 2 "
                 "for every later start, not re-tested again",
                 (unsigned)in->differ, (unsigned)in->twins, put, cfg);
        return 0;
    case BONES_WAIT_PLAIN:
        snprintf(out, size,
                 "window: VU skin check ladder: waits: the ring read needs the plain shader set "
                 "(this run's program entries are not it, or not on Vulkan): step 1 not written "
                 "(%u of %u twins differ)",
                 (unsigned)in->differ, (unsigned)in->twins);
        return 0;
    case BONES_PORT_RACE:
        snprintf(out, size,
                 "window: VU skin check ladder: the wrong reads are the ring's bytes changing "
                 "under the GPU (H2, a port race: %u checks), not the driver's read: the ladder "
                 "does not act",
                 (unsigned)in->portRace);
        return 0;
    default:
        break;
    }
    return -1;
}
