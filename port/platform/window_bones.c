/*
 * port/platform/window_bones.c
 *
 * The window's part of issue 123's ladder (window_bones.h, bones_ladder.h).
 */
#include "window_bones.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bones_ladder.h"
#include "config.h"
#include "ico_version.h"
#include "rd.h"
#include "window_switch.h"
#ifdef __ANDROID__
#include "android/gpu_driver_android.h"
#endif

#define KEY_BONES "dev.vu_bones"
#define KEY_BONES_BY "dev.vu_bones_by"
#define KEY_TU "dev.turnip_debug"
#define KEY_TU_BY "dev.turnip_debug_by"

/* the game's (ico2/common/include/StageManager.h): a movie plays (window_switch.c
   refuses a switch then) */
extern int mpegPlay;

static struct {
    int init;
    int done;      /* a write, a stop or the port race acted on: nothing more this run */
    int healed;    /* the healed line said */
    int plainSaid; /* the "needs the plain shader set" line said */
    int nobin, nobinOff, nobinAvail;
    /* step 1 written: the switch in the run wanted (pending), asked
       (once a run), its waiting line said; the run's evidence then (the
       lines), the mark (the environment) */
    int ringPending, ringAsked, ringWaitSaid;
    BonesLadderIn ringIn;
    char ringMark[128];
} s;

static const char *game_version(void)
{
    return ICO_VERSION_LABEL[0] != '\0' ? ICO_VERSION_LABEL : "dev";
}

static const char *cfg_name(void)
{
    return ico_config_toml_path() != NULL ? ico_config_toml_path() : "config.toml";
}

/* key = value and key_by = the mark of the run so far (kept in mark when
   given); 1 when saved */
static int write_key(const char *key, const char *keyBy, const char *value,
                     const RdVuBonesEvidence *ev, char *keep, size_t keepSize)
{
    BonesMark m;
    char mark[128];

    memset(&m, 0, sizeof(m));
    snprintf(m.version, sizeof(m.version), "%s", game_version());
    m.frame = ev->frame;
    m.differ = ev->differ;
    m.twins = ev->twins;
    if (keep != NULL && bones_mark_format(keep, keepSize, &m) != 0) {
        keep[0] = '\0';
    }
    return bones_mark_format(mark, sizeof(mark), &m) == 0 &&
           ico_config_set_string(key, value) == 0 && ico_config_set_string(keyBy, mark) == 0 &&
           ico_config_save() == 0;
}

static void start(void)
{
    char line[512];
    const char *tu = ico_config_get_string(KEY_TU, "");
    const char *env = getenv("TU_DEBUG");

    s.init = 1;
    /* TU_DEBUG=nobin set at this start (android/gpu_driver_android.c, before
       the Turnip package loaded) */
    s.nobinOff = strcmp(tu, "off") == 0;
#ifdef __ANDROID__
    s.nobinAvail = ico_gpu_driver_android_turnip_package();
#else
    s.nobinAvail = 0;
#endif
    /* only when the Turnip package loaded: TU_DEBUG stays set when its
       open failed or the phone's own driver runs */
    s.nobin = bones_nobin_in_force(tu, env, s.nobinAvail);
    if (bones_ladder_start_line(line, sizeof(line), ico_config_get_string(KEY_BONES, NULL),
                                ico_config_get_string(KEY_BONES_BY, NULL), tu[0] ? tu : NULL,
                                ico_config_get_string(KEY_TU_BY, NULL), s.nobin) == 0) {
        fprintf(stderr, "%s\n", line);
    }
}

/* step 1's switch in the run (bones_restart_step's guards): the renderer
   made again in place at this pump's switch (window_switch.c, after this
   pump), with ICO_RD_VU_BONES set so its rd_init takes the ring builds */
static void ring_switch_poll(void)
{
    char line[640];
    const double up = (double)SDL_GetTicks() / 1000.0;
    const int step = bones_restart_step(s.ringPending, s.ringAsked, mpegPlay != 0, up);

    if (step == BONES_RESTART_NONE) {
        return;
    }
    if (step == BONES_RESTART_WAIT) {
        if (!s.ringWaitSaid) {
            s.ringWaitSaid = 1;
            if (bones_ring_line(line, sizeof(line), &s.ringIn, 1, cfg_name(), BONES_RING_WAITS,
                                mpegPlay != 0 ? "the movie's end" : "the run's first 30 s") == 0) {
                fprintf(stderr, "%s\n", line);
            }
        }
        return;
    }
    s.ringAsked = 1;
    s.ringPending = 0;
    /* to the next rd_init through its setter: no environment written while
       other threads may read it */
    rd_set_vu_bones("ring", s.ringMark);
    if (ico_window_switch_restart("issue 123's ladder: the bones through the ring") != 0) {
        /* the switch machinery refuses (its line above): the next start */
        if (!s.ringWaitSaid && bones_ring_line(line, sizeof(line), &s.ringIn, 1, cfg_name(),
                                               BONES_RING_NEXT, NULL) == 0) {
            fprintf(stderr, "%s\n", line);
        }
        s.done = 1;
        return;
    }
    if (!s.ringWaitSaid &&
        bones_ring_line(line, sizeof(line), &s.ringIn, 1, cfg_name(), BONES_RING_NOW, NULL) == 0) {
        fprintf(stderr, "%s\n", line);
    }
    if (bones_restart_line(line, sizeof(line)) == 0) {
        fprintf(stderr, "%s\n", line);
    }
}

void window_bones_pump(void)
{
    RdVuBonesEvidence ev;
    BonesLadderIn in;
    BonesAction a;
    char line[640];
    int written = 0;

    rd_vu_bones_evidence(&ev);
    if (!s.init) {
        if (!ev.started) {
            return; /* the renderer not up yet: TU_DEBUG and the modules not this start's */
        }
        start();
    }
    if (s.done) {
        return;
    }
    if (s.ringPending) {
        ring_switch_poll();
        return;
    }
    if (s.ringAsked && !ev.ring) {
        /* the renderer made again in place, not up on the ring builds yet
           (or it fell back): nothing decided on the old evidence */
        if (ev.started) {
            s.done = 1; /* up without them: the next start reads the key */
        }
        return;
    }
    if (ev.twins == 0) {
        return; /* no twin read yet (or not a device the twins run on) */
    }
    memset(&in, 0, sizeof(in));
    in.ring = ev.ring;
    in.nobin = s.nobin;
    in.nobinOff = s.nobinOff;
    in.nobinAvail = s.nobinAvail;
    in.ringUsable = ev.ringUsable;
    in.replays = ev.replays;
    in.twins = ev.twins;
    in.differ = ev.differ;
    in.ringClear = ev.ringClear;
    in.ringFail = ev.ringFail;
    in.sceneOnly = ev.sceneOnly;
    in.probeWrong = ev.probeWrong;
    in.portRace = ev.portRace;
    in.addr = ev.addr;
    a = bones_ladder_step(&in);
    switch (a) {
    case BONES_WAIT:
        return;
    case BONES_HEALED:
        if (s.healed) {
            return;
        }
        s.healed = 1;
        break;
    case BONES_WAIT_PLAIN:
        /* said once; the evidence may still point to step 2 later */
        if (s.plainSaid) {
            return;
        }
        s.plainSaid = 1;
        break;
    case BONES_WRITE_RING:
        written = write_key(KEY_BONES, KEY_BONES_BY, "ring", &ev, s.ringMark, sizeof(s.ringMark));
        if (written) {
            /* step 1 in this run: the switch at this pump or a later one */
            s.ringIn = in;
            s.ringPending = 1;
            ring_switch_poll();
            return;
        }
        s.done = 1;
        break;
    case BONES_WRITE_NOBIN:
        written = write_key(KEY_TU, KEY_TU_BY, "nobin", &ev, NULL, 0);
        s.done = 1;
        break;
    case BONES_WRITE_OFF:
        written = write_key(KEY_TU, KEY_TU_BY, "off", &ev, NULL, 0);
        s.done = 1;
        break;
    default: /* BONES_STOP, BONES_PORT_RACE: said, nothing written */
        s.done = 1;
        break;
    }
    if (bones_ladder_action_line(line, sizeof(line), a, &in, written, cfg_name()) == 0) {
        fprintf(stderr, "%s\n", line);
    }
}
