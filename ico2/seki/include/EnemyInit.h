/*
 * ico2/seki/include/EnemyInit.h
 *
 * The declarations of what EnemyInit.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ENEMYINIT_H
#define ENEMYINIT_H

float (*enemy_GetPositionTable(int idx, int sub_idx))[4];
void enemy_Initialize(void);

extern int EnemyKindNum;

/* PC port: the enemy set records are declared here, not in EnemyInit.c, as
   upstream's June 2001 demo arm of this header declares them: the June 2001
   demo's sets of stage models 02..09 and its enemySetTable are data tables
   the loader fills from that disc's ELF (the extra rows of
   config/data_schema.jun26.txt; port/data/gen/table_defs.c defines them). */
#include <libvu0.h>

/* one kind's position list and the kind it places */
typedef struct EnemyKindRec { /* field names derived */
    int *list;                /* 0x0 */
    int kind;                 /* 0x4 */
} EnemyKindRec;               /* derived name */

/* a model file's placement set: its kind lists and its position table */
typedef struct EnemyModelSet { /* field names derived */
    EnemyKindRec *kinds;       /* 0x0 */
    int kindNum;               /* 0x4 */
    sceVu0FVECTOR *pos;        /* 0x8 */
    int posNum;                /* 0xC */
} EnemyModelSet;               /* derived name */

/* the stage table's record: a pointer and a count */
typedef struct EnemySet { /* field names derived */
    EnemyModelSet **list; /* 0x0 */
    int n;                /* 0x4 */
} EnemySet;               /* derived name */

extern struct EnemyModelSet enemy_enemymodel01_enemymodel04;
/* PC port: enemymodel01 (EnemyInit.c) is declared nowhere else, as upstream's
   June 2001 demo arm has it: the June 2001 demo's enemySetTable (a data table)
   points at it */

#endif /* ENEMYINIT_H */
