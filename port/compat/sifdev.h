/*
 * port/compat/sifdev.h
 *
 * The host build's sifdev.h: the declarations the game uses, from
 * sce/libkernl/sifdev.h (this project's own clean-room header, MIT), with
 * pointer-sized and 64-bit types made host-correct.
 *
 * The five file calls are port/data/sifdev_host.c's:
 * a "host0:" path, the development kit's PC-side device, is a file under
 * <pref>/dev/; every other device fails with -1 as before.  Descriptors are
 * small numbers from 0.
 */
#ifndef ICO_COMPAT_SIFDEV_H
#define ICO_COMPAT_SIFDEV_H

int sceClose(unsigned int fd);
int sceLseek(unsigned int fd, int offset, int whence);
int sceOpen(unsigned char *name, int flags, ...);
int sceRead(int fd, void *buf, int nbyte);
int sceWrite(int fd, void *buf, int nbyte);

#include <stddef.h>

/* PC port: the host file a "host0:" name maps to (<pref>/dev/<name>, or
   <root>/<name> after ico_host0_set_root), with the missing folders made
   when makeDirs is set.  0, or -1 (not a host0: name, a ".." component, too
   long). */
int ico_host0_path(const char *name, char *out, size_t size, int makeDirs);
/* tests: the folder host0: maps to (NULL: <pref>/dev again) */
void ico_host0_set_root(const char *dir);

#endif /* ICO_COMPAT_SIFDEV_H */
