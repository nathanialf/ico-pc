/*
 * ico2/fumi/include/obj_manager.h
 *
 * The declarations of what obj_manager.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef OBJ_MANAGER_H
#define OBJ_MANAGER_H

#include "typedef.h"

/* obj_manager.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
void iosOmExeEachGObj(int idx, void (*fn)(GObj *, int), int arg);
void iosOmExeEachGObjAll(void (*fn)(GObj *, int), int arg);
int iosOmReturnExeEachGObj(int link, int (*fn)(GObj *, int), int arg, int flag);

void iosOmGetGObjStatus(int *total, int *used);

GObj *iosOmSearchGObjId(int idx, GObj *target);
GObj *iosOmSearchGObjIdAll(GObj *id);
void iosOmBeforeFuncStandard(void);
int iosOmSendMail(GObj *g, int type, void *arg);
int iosOmSendMailLink(int link, int type, void *arg);
int iosOmExeMail(void (*func)(IosMail));
void iosOmInit(void);
void iosOmMain(void);
void iosOmCreateDL(void);

#endif /* OBJ_MANAGER_H */
