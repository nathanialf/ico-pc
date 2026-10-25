#include "mv_defs.h"
#include "mv_strfile.h"
#include "cdvd.h"

int strFileOpen(char *self, char *name)
{
    strcpy(self + PAL_NTSCAUG6JUN26(0x38, 0x34), name);
    iosCdvdDirectStOpen((struct IosCdvdHandle *)self);
    return 1;
}

int strFileClose(char *self)
{
    iosCdvdDirectStClose((struct IosCdvdHandle *)self);
    return 1;
}

int strFileRead(char *self, void *buf, int n, int *eof)
{
    return iosCdvdDirectStRead((struct IosCdvdHandle *)self, buf, n, eof);
}
