/*
 * ico2/fumi/include/memory.h
 *
 * The declarations of what memory.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef MEMORY_H
#define MEMORY_H

/* an address the allocator computes with: unsigned int on the EE,
   pointer-wide on the host */
#ifdef ICO_HOST
typedef __UINTPTR_TYPE__ IosMemAddr; /* derived name */
#else
typedef unsigned int IosMemAddr; /* derived name */
#endif

typedef struct IosMemPart {    /* field names derived */
    char tag[16];              /* 0x00 */
    char name[16];             /* 0x10 */
    struct IosMemPart *parent; /* 0x20, the partition this one was carved from */
    struct IosMemPart *next;   /* 0x24, the next partition carved from the same parent */
    struct IosMemPart *child;  /* 0x28, the last partition carved from this one */
    int nused;                 /* 0x2C */
    char *top;                 /* 0x30 */
    int free;                  /* 0x34 */
    char *start;               /* 0x38 */
    char *end;                 /* 0x3C */
    int total;                 /* 0x40 */
    struct IosMemNode *head;   /* 0x44 */
} IosMemPart;                  /* derived name */

/* one block of a partition, allocated or on its free list */
typedef struct IosMemNode {       /* field names derived */
    char tag[16];                 /* 0x00 */
    char name[16];                /* 0x10 */
    struct IosMemNode *prev;      /* 0x20 */
    struct IosMemNode *next;      /* 0x24 */
    struct IosMemNode *free_prev; /* 0x28 */
    struct IosMemNode *free_next; /* 0x2C */
    struct IosMemPart *part;      /* 0x30 */
    int size;                     /* 0x34 */
    int line;                     /* 0x38 */
#ifdef ICO_HOST
    /* 16-byte aligned, so the header in front of every block is 0x40 bytes
       on a 32-bit host, as on the EE, and 0x50 on a 64-bit host */
} __attribute__((aligned(16))) IosMemNode; /* derived name */
#else
    char pad3C[4];
} IosMemNode; /* derived name */
#endif

/* memory.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
IosMemPart *iosMallocInitPartition(IosMemAddr start, IosMemAddr end);
void *iosMallocDebug(IosMemPart *part, int size, const char *file, int line);
void *iosFree(void *ptr);
void iosMallocCheckLeak(IosMemPart *part);
IosMemPart *iosMallocResetPartition(IosMemPart *part);
IosMemPart *iosMallocSetPartition(IosMemPart *part, int size, int align);
int iosMallocSetPartitionName(IosMemPart *part, char *name);
void *iosReallocDebug(void *ptr, unsigned int size);
void *iosMallocDebugNoAssert(IosMemPart *part, int size, const char *file, int line);

#endif /* MEMORY_H */
