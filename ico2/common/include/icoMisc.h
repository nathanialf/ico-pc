/*
 * ico2/common/include/icoMisc.h
 *
 * The declarations of what icoMisc.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ICOMISC_H
#define ICOMISC_H

/* icoMisc.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
void ExitIcoMisc(void);
void DummyIcoMisc(void);
void DispIcoMisc(void);
void InitIcoMisc(int *arg);
void ExecIcoMisc(void);
/* Six debug words nothing reads. */
extern int dbgC0;
extern int dbgC1;
extern int dbgC2;
extern int dbgC3;
extern int dbgC4;
extern int dbgC5;

#endif /* ICOMISC_H */
