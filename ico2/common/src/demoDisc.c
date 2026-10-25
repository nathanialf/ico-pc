/*
 * ico2/common/src/demoDisc.c
 *
 * The demo disc's hand-back to the magazine menu: an argument parser and the
 * pair of functions that record the arguments the menu started the game with
 * and later reload the menu's ELF with them.  The file name is derived: the
 * build ships no map.  The Aug-6-2001 map links the same three functions as
 * /usr/local/sce/ee/lib/libscedemo.a(libscedemo.o) right after libsndn2
 * (.text 0x1b8: sceDemoStart at +0x70 and sceDemoEnd at +0x168, as here) with
 * argv_copy and argc_copy in .scommon, so the member was built -G 8: the code
 * addresses its two words through $gp, unlike libsndn2's sound.o, whose small
 * static the demo addresses with %hi/%lo; it follows sound.o in every section.
 */
#include <eekernel.h>
#include <eeregs.h>
#include <stdio.h>

/* .sbss: the argument vector and count sceDemoStart records, which
   sceDemoEnd hands back to LoadExecPS2 with the menu's path (the Aug-6
   map's two common symbols of libscedemo.o; static here, which keeps them in
   .sbss under the game's -fno-common) */
static char **argv_copy;

static int argc_copy;

void sceDemoStart(int argc, char **argv, unsigned short *language, unsigned short *aspect,
                  unsigned short *playmode, unsigned short *toInactive, unsigned short *toTotal,
                  unsigned short *mediaType)
{
    /* the decimal-string-to-short helper, five settings and the media type */
    void readSetting(char *s, unsigned short *v) /* derived name */
    {
        int i;

        *v = 0;
        for (i = 0; s[i] != 0; i++) {
            *v = *v * 10 + s[i] - '0';
        }
    }

    int ac;
    char **av;

    ac = argc;
    av = argv;
    if (ac < 7) {
        /* not started by the demo menu: hand back the boot arguments as
           given.  The words pin a set and a read of ac and av in this arm
           after its reads of argc and argv (cse2 then keeps the copies for
           the test and the parser, the incoming registers for these two
           stores); the statements' spelling is not pinned. */
        ac = argc;
        av = argv;
        argc_copy = ac;
        argv_copy = av;
    } else {
        argv_copy = av;
        argc_copy = ac;
        readSetting(av[1], language);
        readSetting(av[2], aspect);
        readSetting(av[3], playmode);
        readSetting(av[4], toInactive);
        readSetting(av[5], toTotal);
        readSetting(av[6], mediaType);
    }
}

void sceDemoEnd(int status)
{
    *GS_BGCOLOR = 0;
    *GS_PMODE = 0;
    sprintf(argv_copy[7], "%d", status);
    LoadExecPS2("cdrom0:\\SCE_DD\\SCE_DD.ELF;1", argc_copy, argv_copy);
}
