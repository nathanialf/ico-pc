/*
 * port/platform/window_bones.c
 *
 * The window's part of issue 123's ladder (window_bones.h, bones_ladder.h).
 */
#include "window_bones.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bones_ladder.h"
#include "config.h"
#include "ico_version.h"
#include "rd.h"
#ifdef __ANDROID__
#include "android/gpu_driver_android.h"
#endif

#define KEY_BONES "dev.vu_bones"
#define KEY_BONES_BY "dev.vu_bones_by"
#define KEY_TU "dev.turnip_debug"
#define KEY_TU_BY "dev.turnip_debug_by"

static struct {
    int init;
    int done;   /* a write, a stop or the port race acted on: nothing more this run */
    int healed; /* the healed line said */
    int nobin, nobinOff, nobinAvail;
} s;

static const char *game_version(void)
{
    return ICO_VERSION_LABEL[0] != '\0' ? ICO_VERSION_LABEL : "dev";
}

static const char *cfg_name(void)
{
    return ico_config_toml_path() != NULL ? ico_config_toml_path() : "config.toml";
}

/* key = value and key_by = the mark of the run so far; 1 when saved */
static int write_key(const char *key, const char *keyBy, const char *value,
                     const RdVuBonesEvidence *ev)
{
    BonesMark m;
    char mark[128];

    memset(&m, 0, sizeof(m));
    snprintf(m.version, sizeof(m.version), "%s", game_version());
    m.frame = ev->frame;
    m.differ = ev->differ;
    m.twins = ev->twins;
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
    s.nobin = strcmp(tu, "nobin") == 0 && env != NULL && strstr(env, "nobin") != NULL;
    s.nobinOff = strcmp(tu, "off") == 0;
#ifdef __ANDROID__
    s.nobinAvail = ico_gpu_driver_android_turnip_package();
#else
    s.nobinAvail = 0;
#endif
    if (bones_ladder_start_line(line, sizeof(line), ico_config_get_string(KEY_BONES, NULL),
                                ico_config_get_string(KEY_BONES_BY, NULL), tu[0] ? tu : NULL,
                                ico_config_get_string(KEY_TU_BY, NULL), s.nobin) == 0) {
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
    if (ev.twins == 0) {
        return; /* no twin read yet (or not a device the twins run on) */
    }
    memset(&in, 0, sizeof(in));
    in.ring = ev.ring;
    in.nobin = s.nobin;
    in.nobinOff = s.nobinOff;
    in.nobinAvail = s.nobinAvail;
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
    case BONES_WRITE_RING:
        written = write_key(KEY_BONES, KEY_BONES_BY, "ring", &ev);
        s.done = 1;
        break;
    case BONES_WRITE_NOBIN:
        written = write_key(KEY_TU, KEY_TU_BY, "nobin", &ev);
        s.done = 1;
        break;
    case BONES_WRITE_OFF:
        written = write_key(KEY_TU, KEY_TU_BY, "off", &ev);
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
