/*
 * port/game/demo_host.c
 *
 * The June 2001 demo's sceDemoStart. On the PS2 the magazine disc's menu
 * starts the demo's ELF with its settings as decimal strings (argv[1..6]:
 * language, aspect, playmode, the two timeouts and the media type), and
 * libscedemo's sceDemoStart parses them (ico2/common/src/demoDisc.c, which
 * the port does not build: it ends in LoadExecPS2 of the menu's ELF). The
 * port has no menu: this stub hands common/src/main.c the port's own
 * values. main.c only prints the first five and reads the media type
 * (0 = CD, anything else = DVD for iosCdvdMediaType); the idle countdown
 * the timeouts would drive is not run (main.c sampleModeExec), and
 * sceDemoEnd is never called.
 */
#include <libscf.h>

#include "diag_host.h"
#include "sysconf.h"
#include "video_options.h"

/* common/src/main.c (the JUN26 arm) calls it with argc 0 and argv 0 */
void sceDemoStart(int argc, char **argv, unsigned short *language, unsigned short *aspect,
                  unsigned short *playmode, unsigned short *toInactive, unsigned short *toTotal,
                  unsigned short *mediaType);

void sceDemoStart(int argc, char **argv, unsigned short *language, unsigned short *aspect,
                  unsigned short *playmode, unsigned short *toInactive, unsigned short *toTotal,
                  unsigned short *mediaType)
{
    int scf;

    (void)argc;
    (void)argv;
    /* the language: [game] language, else the host's (port/config/sysconf.c), in
       libscf's numbering, which the demo's menu passes too (0 Japanese, 1 English, ...) */
    scf = sceScfGetLanguage();
    *language = (unsigned short)(scf >= 0 ? scf : ICO_SCF_LANGUAGE_ENGLISH);
    /* the aspect: 0 for a 4:3 picture, 2 for a wide one, from [video] aspect */
    *aspect = (unsigned short)(ico_video_aspect() > 1.34f ? 2 : 0);
    *playmode = 0;
    *toInactive = 0;
    *toTotal = 0;
    *mediaType = 1; /* a DVD: the demo's disc image is one (iosCdvdMediaType 2) */
    ico_diag_milestone("demo: the magazine menu's settings from the port: language %u aspect %u "
                       "playmode 0 timeouts 0 media DVD",
                       (unsigned)*language, (unsigned)*aspect);
}
