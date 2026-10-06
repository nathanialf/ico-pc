#include "debug_exception.h"
#include "pad.h"
#include "keyInput.h"
#include <libcdvd.h>
#include "main.h"

/* PC port (renderer wave 6, R6a): debug_exception.c on the host.

   On the PS2 this is the debug monitor: debugExceptionInit installs
   debugEEExceptionMain as the EE kernel's handler for eleven exception
   causes; it freezes the game, draws a register dump, a call trace read from
   TRTABLE.BIN / TRFILE.TXT / SRCFILE.TXT and the saved frame straight to the
   GS (VIF1 DIRECT packets of its own, debug_exception_screen.c.inc), and
   loops on the pad.  debug_assert and debug_assertMessage hang.  The host
   has no EE exceptions to trap: a fault is the host crash handler's
   (port/platform/diag_host.c), which writes the log
   and, on Windows, a message box.  So the screen and its handler are not
   compiled here; what the game calls keeps its meaning:

     debugExceptionInit, debugIOPExceptionInit  nothing to install
     debug_SetExceptionMessage                  keeps the message, as the
                                                report's second line
     debug_assertMessage, debug_assert          report the location to the
                                                log, keep it as the last
                                                failure message and abort(),
                                                which the crash handler
                                                reports (the PS2 hung)
     debugIOPExceptionMain ("IOP DEAD")         the same, as a failure */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"

/* port/platform/diag_host.h */
void ico_diag_set_failure(const char *fmt, ...);

static char exceptionMessage[1024] = ""; /* derived name */

static void hostFail(const char *what, const char *file, int line, const char *mes)
{
    if (exceptionMessage[0] != '\0') {
        fprintf(stderr, "%s: exception message: %s\n", what, exceptionMessage);
    }
    if (file != NULL && mes != NULL) {
        fprintf(stderr, "%s: %s:%d: %s\n", what, file, line, mes);
        fflush(stderr);
        ico_diag_set_failure("%s: %s:%d: %s", what, file, line, mes);
    } else if (file != NULL) {
        fprintf(stderr, "%s: %s:%d\n", what, file, line);
        fflush(stderr);
        ico_diag_set_failure("%s: %s:%d", what, file, line);
    } else {
        fprintf(stderr, "%s\n", what);
        fflush(stderr);
        ico_diag_set_failure("%s", what);
    }
    abort();
}

void debugExceptionInit(void *workBuf)
{
    (void)workBuf;
}

void debugIOPExceptionMain(void)
{
    hostFail("IOP DEAD", NULL, 0, NULL);
}

void debug_SetExceptionMessage(char *mes)
{
    snprintf(exceptionMessage, sizeof(exceptionMessage), "%s", mes);
}

void debugIOPExceptionInit(void) {}

void debug_assertMessage(const char *file, int line, const char *mes)
{
    hostFail("debug_assertMessage", file, line, mes != NULL ? mes : "");
}

void debug_assert(const char *file, int line)
{
    hostFail("debug_assert", file, line, NULL);
}
