/*
 * port/compat/libscf.h
 *
 * The host build's libscf.h: the declarations the game uses, from
 * sce/libscf/libscf.h (this project's own clean-room header, MIT), with
 * pointer-sized and 64-bit types made host-correct.
 */
#ifndef ICO_COMPAT_LIBSCF_H
#define ICO_COMPAT_LIBSCF_H

int sceScfGetLanguage(void);
int sceScfGetSummerTime(void);
int sceScfGetTimeZone(void);

#endif /* ICO_COMPAT_LIBSCF_H */
