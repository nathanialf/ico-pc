#include "typedef.h"
#include "debug.h"
#include "debug_exception.h"
#include "memory.h"
#include <eekernel.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#ifdef ICO_HEAP_STATS

/* port/platform/arena.c: per-partition bytes in use and high-water mark */
void ico_heap_stats_alloc(const void *part, const char *name, unsigned int bytes);
void ico_heap_stats_free(const void *part, unsigned int bytes);

#endif
#ifdef ICO_HEAP_ASAN
/* A diagnostic build (docs/port/BOOT_DIAG.md, "ICO_HEAP_ASAN"): with
 * AddressSanitizer, every block header, the slack after each block's
 * requested size and every free area are poisoned while the game runs, so
 * the first write past a block's end reports the writer. The allocator works
 * as before (same partitions, offsets and bookkeeping); its public functions
 * are the wrappers at the end of this file, which unpoison the heap around
 * the original code and poison it again from the node lists afterwards. */
#ifndef __SANITIZE_ADDRESS__
#error "ICO_HEAP_ASAN needs -fsanitize=address"
#endif
#define iosMallocInitPartition heapAsan_iosMallocInitPartition
#define iosMallocSetPartition heapAsan_iosMallocSetPartition
#define iosMallocResetPartition heapAsan_iosMallocResetPartition
#define iosMallocClearPartition heapAsan_iosMallocClearPartition
#define iosMallocDebug heapAsan_iosMallocDebug
#define iosMallocDebugNoAssert heapAsan_iosMallocDebugNoAssert
#define iosMallocAlignDebug heapAsan_iosMallocAlignDebug
#define _iosFreeWithFill heapAsan__iosFreeWithFill
#define iosFree heapAsan_iosFree
#define iosMallocCheckLeak heapAsan_iosMallocCheckLeak
#define iosMallocCheckLeak2 heapAsan_iosMallocCheckLeak2
#define iosReallocDebug heapAsan_iosReallocDebug

IosMemPart *iosMallocInitPartition(IosMemAddr start, IosMemAddr end);
IosMemPart *iosMallocSetPartition(IosMemPart *part, int size, int align);
IosMemPart *iosMallocResetPartition(IosMemPart *part);
void iosMallocClearPartition(IosMemPart *part);
void *iosMallocDebug(IosMemPart *part, int size, const char *file, int line);
void *iosMallocDebugNoAssert(IosMemPart *part, int size, const char *file, int line);
void *iosMallocAlignDebug(IosMemPart *part, int size, int align, const char *file, int line);
void _iosFreeWithFill(int *ptr, char *file, int line);
void *iosFree(void *ptr);
void iosMallocCheckLeak(IosMemPart *part);
void iosMallocCheckLeak2(__INTPTR_TYPE__ part, int offset);
void *iosReallocDebug(void *ptr, unsigned int size);
/* records the bytes a block's caller asked for (its node, its size) */
static void heapAsanReq(void *node, unsigned int bytes);

#endif
/* The allocator's record sizes, in bytes and in quadwords. The EE build
 * spells them as the literals it was written with; the host derives them
 * from the records, which gives the EE's values on a 32-bit host (checked
 * below) and pointer-wide headers on a 64-bit one (docs/port/LAYOUT.md):
 *   NODE_SIZE   the block header in front of every allocation (IosMemNode)
 *   PART_SIZE   the partition record at the head of a partition, rounded to
 *               a quadword (IosMemPart)
 *   PART_NEED   what carving a partition costs besides its size
 *   PART_MIN    the smallest partition iosMallocInitPartition accepts
 *   ADDR_MASK   rounds an address down to a quadword */
#ifdef ICO_HOST
#define NODE_SIZE ((int)sizeof(IosMemNode))
#define NODE_QW (NODE_SIZE >> 4)
#define PART_SIZE ((int)((sizeof(IosMemPart) + 15) & ~(__SIZE_TYPE__)15))
#define PART_NEED (PART_SIZE + NODE_SIZE)
#define PART_MIN (PART_SIZE + NODE_SIZE + 16)
#define PART_AVAIL_QW (NODE_QW + 1)
#define ADDR_MASK (~(IosMemAddr)0xF)

_Static_assert(sizeof(void *) != 4 || (NODE_SIZE == 64 && PART_SIZE == 80),
               "the EE's allocator records on 32-bit hosts");

#else
#define NODE_SIZE 64
#define NODE_QW 4
#define PART_SIZE 80
#define PART_NEED 144
#define PART_MIN 160
#define PART_AVAIL_QW 5
#define ADDR_MASK 0xFFFFFFF0
#endif

typedef struct IosMemTag { /* field names derived */
    char c[16];
} IosMemTag; /* derived name */

/* the node name the heap walk copies out before printing it */
/* */
static char nodeName[32]; /* derived name */

inline IosMemPart *iosMallocInitPartition(IosMemAddr start, IosMemAddr end)
{
    IosMemPart *part;
    IosMemNode *node;
    IosMemAddr top;

    part = (IosMemPart *)((start + 0xF) & ADDR_MASK);
    top = (end + 1) & ADDR_MASK;

    if (top - (IosMemAddr)part < PART_MIN) {
        debug_StdPrintfDummy("mem:partition size too small\n");
        return 0;
    }

    *(IosMemTag *)part = *(IosMemTag *)"<PARTITION>____";

    part->parent = 0;
    part->next = 0;
    part->child = 0;

    part->start = (char *)(node = (IosMemNode *)((char *)part + PART_SIZE));
    part->end = (char *)top;
    part->total = (top - (IosMemAddr)node) >> 4;

    part->nused = 0;
    part->top = (char *)top;
    part->free = (top - (IosMemAddr)node) >> 4;

    part->head = node;

    *(IosMemTag *)node = *(IosMemTag *)"<FREE AREA>____";
    node->prev = 0;
    node->next = 0;
    node->free_prev = 0;
    node->free_next = 0;
    node->size = part->free - NODE_QW;

    debug_StdPrintfDummy("mem:init partition 0x%08x - 0x%08x\n", part->start, part->end - 1);
    return part;
}

IosMemPart *iosMallocSetPartition(IosMemPart *part, int size, int align)
{
    IosMemPart *base;
    int avail;
    int need;

    if (part == 0) {
        debug_StdPrintfDummy("mem:null partition pointer\n");
        return 0;
    }
    if (strcmp(part->tag, "<PARTITION>____") != 0) {
        debug_StdPrintfDummy("mem:illegal partition pointer\n");
        return 0;
    }
    avail = part->free - PART_AVAIL_QW;
    need = (((size + 0xF) & 0xFFFFFFF0) + PART_NEED) >> 4;
    if (avail < need) {
        debug_StdPrintfDummy("mem: memory lack %dqw > parent:%dqw\n", need, avail);
        return 0;
    }
    base = (IosMemPart *)(part->top - (need << 4));
    debug_StdPrintfDummy("mem:set partition 0x%08x\n", base);
    if (iosMallocInitPartition((IosMemAddr)base, (IosMemAddr)part->top - 1) == 0) {
        debug_StdPrintfDummy("mem:fail init partition\n");
        return 0;
    }
    base->parent = part;
    if (part->child != 0) {
        base->next = part->child;
    }
    part->nused = part->nused + 1;
    part->top = (char *)base;
    part->free = part->free - need;
    part->child = base;
    part->head->size = part->head->size - need;
    return base;
}

IosMemPart *iosMallocResetPartition(IosMemPart *part)
{
    IosMemNode *node;
    IosMemPart *parent;
    IosMemPart *next;
    IosMemPart *child;

    if (part == 0) {
        debug_StdPrintfDummy("mem:null partition pointer\n");
        return 0;
    }
    if (strcmp(part->tag, "<PARTITION>____") != 0) {
        debug_StdPrintfDummy("mem:illegal partition pointer\n");
        return 0;
    }
    node = (IosMemNode *)part->start;
    if (node != 0) {
        do {
            *(IosMemTag *)node = *(IosMemTag *)" free memory   ";
            node = node->next;
        } while (node != 0);
    }
    parent = part->parent;
    next = part->next;
    child = part->child;
    iosMallocInitPartition((IosMemAddr)part, (IosMemAddr)part->end);
    part->parent = parent;
    part->next = next;
    part->child = child;
    return part;
}

int iosMallocSetPartitionName(IosMemPart *part, char *name)
{
    if (part == 0) {
        debug_StdPrintfDummy("mem:null partition pointer\n");
        return 0;
    }
    if (strcmp(part->tag, "<PARTITION>____") != 0) {
        debug_StdPrintfDummy("mem:illegal partition pointer\n");
        return 0;
    }
    strcpy(part->name, name);
}

void iosMallocClearPartition(IosMemPart *part)
{
    IosMemPart *child;

    if (part == 0) {
        debug_StdPrintfDummy("mem:null partition pointer\n");
        return;
    }
    if (strcmp(part->tag, "<PARTITION>____") != 0) {
        debug_StdPrintfDummy("mem:illegal partition pointer\n");
        return;
    }
    if (part->parent != 0 && part != part->parent->child) {
        debug_StdPrintfDummy("mem:not last partition\n");
        return;
    }
    if (part->child != 0) {
        child = part->child;
        while (child != part) {
            if (child->child != 0) {
                child = child->child;
            } else if (strcmp(child->tag, "<PARTITION>____") != 0) {
                debug_StdPrintfDummy("mem:illegal partition pointer\n");
                return;
            } else {
                *(IosMemTag *)child = *(IosMemTag *)" del partition ";
                if (child->next == 0) {
                    child->parent->child = 0;
                    child = child->parent;
                } else {
                    child = child->next;
                }
            }
        }
    }
    if (part->parent != 0) {
        part->parent->child = part->next;
        part->parent->top = part->end;
        part->parent->free = (unsigned int)(part->end - part->parent->start) >> 4;
    }
    *(IosMemTag *)part = *(IosMemTag *)" del partition ";
}

/* set while an allocation is in progress, which the re-entry check tests */
static int mallocBusy = 0; /* derived name */

/* the file and line of the allocation in progress, which the re-entry check
   prints */
static const char *mallocFile; /* derived name */

static int mallocLine; /* derived name */

static void *_iosMallocDebug(IosMemPart *part, int size, const char *file, int line)
{
    char buf[1024];
    /* read only by the DEBUG build's free-list trace at the loop's end */
    IosMemTag tag;
    IosMemNode *node;
    IosMemNode *best;
    IosMemNode *newnode;
    char *name;
    int need;
    int i;
    int len;
    int v;
    int q;
    int n;

    if (mallocBusy != 0) {
        sprintf(buf, "MALLOC: REENTER FOR PARTITION \"%s\" SIZE %d\nBEFORE FILE %s LINE %d",
                part->name, size, mallocFile, mallocLine);
        debug_assertMessage(file, line, buf);
    }
    mallocBusy = 1;
    mallocFile = file;
    mallocLine = line;
    if (part == 0) {
        debug_assertMessage(__FILE__, 555, "IOSMALLOC():\nNULL PARTITION POINTER AT MALLOC\n");
        __assert(__FILE__, 555, "e");
        mallocBusy = 0;
        return 0;
    }
    if (strcmp(part->tag, "<PARTITION>____") != 0) {
        debug_assertMessage(__FILE__, 562, "IOSMALLOC():\nNULL PARTITION POINTER AT MALLOC\n");
        __assert(__FILE__, 562, "e");
        mallocBusy = 0;
        return 0;
    }
    need = (((size + 0xF) & 0xFFFFFFF0) + NODE_SIZE) >> 4;
    for (node = part->head; node != 0; node = node->free_next) {
        if (strcmp(node->tag, "<FREE AREA>____") != 0) {
            debug_StdPrintfDummy("mem:illegal free area pointer\n");
            if (node->prev != 0) {
                debug_StdPrintfDummy("mem: prev block, %08x called at %s\n", node->prev,
                                     node->prev->name);
                debug_StdPrintfDummy("mem:prev magic %s\n", node->prev->tag);
            }
            debug_StdPrintfDummy("mem:cur block, %08x called at %s\n", node, node->name);
            debug_StdPrintfDummy("mem:cur magic %s\n", node->tag);
            if (node->next != 0) {
                debug_StdPrintfDummy("mem: next block, %08x called at %s\n", node->next,
                                     node->next->name);
                debug_StdPrintfDummy("mem:next magic %s\n", node->next->tag);
            }
            debug_StdPrintfDummy("mem:called by %s of line %d\n", file, line);
            mallocBusy = 0;
            debug_assert(__FILE__, 598);
            __assert(__FILE__, 598, "0");
            return 0;
        }
        if (node->size >= need) {
            best = node;
            node = node->free_next;
            if (node != 0) {
                n = 9;
                do {
                    if (node->size >= need && node->size < best->size) {
                        best = node;
                    }
                    node = node->free_next;
                } while (n-- > 0 && node != 0);
            }
            node = best;
            newnode = (IosMemNode *)((char *)node + (need << 4));
            *(IosMemTag *)newnode = *(IosMemTag *)"<FREE AREA>____";
            newnode->prev = node;
            newnode->next = node->next;
            newnode->free_prev = node->free_prev;
            newnode->free_next = node->free_next;
            newnode->size = node->size - need;
            if (node->free_prev == 0) {
                part->head = newnode;
            } else {
                node->free_prev->free_next = newnode;
            }
            if (best->free_next != 0) {
                best->free_next->free_prev = newnode;
            }
            if (best->next != 0) {
                best->next->prev = newnode;
            }
            *(IosMemTag *)node = *(IosMemTag *)"<ALLOC>________";
            name = best->name;
            strncpy(name, file, 15);
            if (strlen(file) < 16) {
                len = strlen(file);
            }
            for (len = strlen(name); len < 15; len++) {
                name[len] = ' ';
            }
            v = line;
            q = v / 10;
            /* the line number in the last four characters of the node name */
            for (i = 3; i >= 0; i--) {
                best->name[i + 11] = v - q * 10 + '0';
                v = q;
                q = v / 10;
            }
            best->part = part;
            best->line = line;
            best->next = newnode;
            best->size = need - NODE_QW;
            best->name[15] = 0;
            debug_StdPrintfDummy("cur: %8p %s\n", best, best->tag);
            debug_StdPrintfDummy("next:%8p %s\n", best->next, best->next->tag);
#ifdef ICO_HEAP_STATS
            ico_heap_stats_alloc(part, part->name, (unsigned int)need << 4);
#endif
#ifdef ICO_HEAP_ASAN
            heapAsanReq(best, (unsigned int)size);
#endif
            mallocBusy = 0;
            return (char *)best + NODE_SIZE;
        }
#ifdef DEBUG
        tag = *(IosMemTag *)node;
        tag.c[15] = 0;
        debug_StdPrintfDummy("mem:skip %s size %d need %d\n", tag.c, node->size, need);
#endif
    }
    mallocBusy = 0;
    return 0;
}

inline void *iosMallocDebug(IosMemPart *part, int size, const char *file, int line)
{
    char buf[1024];
    void *ptr;

    ptr = _iosMallocDebug(part, size, file, line);
    if (ptr == 0) {
        debug_StdPrintfDummy("mem:no memory for %d\n", size);
        debug_StdPrintfDummy("mem:called by %s of line %d\n", file, line);
        sprintf(buf, "MALLOC: NO EMEMORY FOR PARTITION \"%s\"\nSIZE %d BYTES (%1.1fM)\n",
                part->name, size, (float)size / 1024.0f / 1024.0f);
        debug_assertMessage(file, line, buf);
        ICO_BREAK();
        debug_assert(__FILE__, 716);
        __assert(__FILE__, 716, "0");
    }
    return ptr;
}

inline void *iosMallocDebugNoAssert(IosMemPart *part, int size, const char *file, int line)
{
    return _iosMallocDebug(part, size, file, line);
}

void *iosMallocAlignDebug(IosMemPart *part, int size, int align, const char *file, int line)
{
    IosMemAddr ptr;
    int ofs;

    if (align <= 16) {
        return iosMallocDebug(part, size, file, line);
    }
    align = (align + 15) / 16 * 16;
    size += align - 16;
    ptr = (IosMemAddr)iosMallocDebug(part, size, file, line);
    if (ptr % align != 0) {
        ofs = align - ptr % align;
        ptr = ptr + ofs;
        sprintf((char *)ptr - 16, "align%05d", ofs);
    }
    return (void *)ptr;
}

void _iosFreeWithFill(int *ptr, char *file, int line)
{
#ifdef ICO_HOST
    /* the block's header's next pointer: where the block ends */
    int *end = (int *)((IosMemNode *)((char *)ptr - NODE_SIZE))->next;
#else
    int *end = *(int **)((char *)ptr - 0x1C);
#endif
    FlushCache(0);
    iosFree(ptr);
    debug_StdPrintfDummy("IOSFILLFREE %s(%d) %p - %p\n", file, line, ptr, end);
    {
        register int g = (IosMemAddr)ptr < (IosMemAddr)end;
        if (g) {
            do {
                *(unsigned int *)ptr = 0xFFFFFFFFu;
                ptr++;
            } while ((IosMemAddr)ptr < (IosMemAddr)end);
        }
    }
    FlushCache(0);
}

void *iosFree(void *ptr)
{
    char buf[1024];
    IosMemNode *node;
    IosMemNode *next;
    IosMemNode *prev;
    IosMemNode *fn;
    int n;

    debug_StdPrintfDummy("mem:free ");
    if (ptr == 0) {
        debug_StdPrintfDummy("null memory pointer\n");
        ICO_BREAK();
        debug_assertMessage(__FILE__, 820, "IOSFREE(): NULL MEMORY POINTER\n");
        __assert(__FILE__, 820, "e");
        return 0;
    }
    prev = (IosMemNode *)((char *)ptr - 16);
    next = ptr;
    if (strncmp(prev->tag, "align", 5) == 0) {
        n = atoi((char *)ptr - 11);
        *((char *)ptr - 16) = 0;
        next = (IosMemNode *)((char *)prev - (n - 16));
    }
    node = (IosMemNode *)((char *)next - NODE_SIZE);
    if (strcmp(node->tag, "<ALLOC>________") != 0) {
        sprintf(buf, "IOSFREE():\n\tPREV MAGIC: %s\n\t CUR MAGIC: %s\n\tNEXT MAGIC: %s\n",
                node->prev->tag, node->tag, node->next->tag);
        debug_assertMessage(__FILE__, 836, buf);
        __assert(__FILE__, 836, "e");
        return 0;
    }
#ifdef ICO_HEAP_STATS
    ico_heap_stats_free(node->part, (unsigned int)(node->size + NODE_QW) << 4);
#endif
    next = node->next;
    prev = node->prev;
    if (prev != 0) {
        if (strcmp(prev->tag, "<FREE AREA>____") == 0) {
            if (next != 0) {
                if (strcmp(next->tag, "<FREE AREA>____") == 0) {
                    if (prev->free_next == next) {
                        fn = next->free_next;
                        prev->free_next = fn;
                        if (fn != 0) {
                            fn->free_prev = prev;
                        }
                    } else if (next->free_next == prev) {
                        fn = next->free_prev;
                        prev->free_prev = fn;
                        if (fn == 0) {
                            node->part->head = prev;
                        } else {
                            fn->free_next = prev;
                        }
                    } else {
                        fn = next->free_prev;
                        if (fn == 0) {
                            node->part->head = next->free_next;
                        } else {
                            fn->free_next = next->free_next;
                        }
                        if (next->free_next != 0) {
                            next->free_next->free_prev = next->free_prev;
                        }
                    }
                    {
                        int t = prev->size + NODE_QW;
                        t += next->size;
                        prev->next = next->next;
                        prev->size = t;
                    }
                    *(IosMemTag *)next = *(IosMemTag *)" free memory0  ";
                    if (next->next != 0) {
                        next->next->prev = prev;
                    }
                } else if (strcmp(next->tag, "<ALLOC>________") == 0) {
                    prev->next = next;
                    next->prev = prev;
                } else {
                    debug_assertMessage(__FILE__, 905, "IOSFREE(): MEMORY LINK MISS\n");
                    __assert(__FILE__, 905, "e");
                    return 0;
                }
            } else {
                prev->next = 0;
            }
            {
                int t = prev->size;
                t += NODE_QW;
                t += node->size;
                prev->size = t;
            }
            *(IosMemTag *)node = *(IosMemTag *)" free memory1  ";
            goto ret_ptr;
        }
        if (strcmp(prev->tag, "<ALLOC>________") != 0) {
            goto err_3bf;
        }
    }
    if (next == 0) {
        goto tail_node;
    }
    if (strcmp(next->tag, "<ALLOC>________") == 0) {
        node->free_prev = 0;
        node->free_next = node->part->head;
        node->part->head = node;
        if (node->free_next != 0) {
            node->free_next->free_prev = node;
        }
        goto tag_free;
    }
    if (strcmp(next->tag, "<FREE AREA>____") != 0) {
        goto err_3b5;
    }
    fn = next->free_prev;
    if (fn == 0) {
        node->part->head = node;
    } else {
        fn->free_next = node;
    }
    node->free_prev = next->free_prev;
    node->free_next = next->free_next;
    {
        int t = node->size;
        t += NODE_QW;
        t += next->size;
        node->size = t;
    }
    node->next = next->next;
    *(IosMemTag *)next = *(IosMemTag *)" free memory2  ";
    if (next->next != 0) {
        next->next->prev = node;
    }
    if (next->free_next != 0) {
        next->free_next->free_prev = node;
    }
    goto tag_free;
tail_node:
    node->free_prev = 0;
    node->free_next = node->part->head;
    if (node->part->head != 0) {
        node->part->head = node;
        node->free_next->free_prev = node;
    }
    debug_assertMessage(__FILE__, 962, "IOSFREE(): ALLOC NULL\n");
    __assert(__FILE__, 962, "e");
    goto tag_free;
err_3b5:
    debug_assertMessage(__FILE__, 965, "IOSFREE(): MEMORY LINK MISS\n");
    __assert(__FILE__, 965, "e");
    return 0;
tag_free:
    *(IosMemTag *)node = *(IosMemTag *)"<FREE AREA>____";
    goto ret_ptr;
err_3bf:
    debug_assertMessage(__FILE__, 975, "IOSFREE(): ??\n");
    __assert(__FILE__, 975, "e");
ret_ptr:
    return ptr;
}

void iosMallocCheckLeak(IosMemPart *part)
{
    IosMemNode *node;
    IosMemNode *next;
    int found = 0;
    int i;
    int p;

    node = (IosMemNode *)part->start;
    while (node != 0) {
        if (strcmp(node->tag, "<ALLOC>________") != 0 &&
            strcmp(node->tag, "<FREE AREA>____") != 0 &&
            strcmp(node->tag, " free memory   ") != 0) {
            debug_StdPrintfDummy("magic broken :%p\n", node);
            found = 1;
            break;
        }
        next = node->next;
        for (i = 0; i < 12; i++) {}
        node = next;
    }
    if (found == 1) {
        node = (IosMemNode *)part->start;
        while (node != 0) {
            debug_StdPrintfDummy("mem:addr:$%08x ", node);
            if (strcmp(node->tag, "<ALLOC>________") == 0) {
                debug_StdPrintfDummy("ALLOC ");
            } else if (strcmp(node->tag, "<FREE AREA>____") == 0) {
                debug_StdPrintfDummy("FREEAREA ");
            } else if (strcmp(node->tag, " free memory   ") == 0) {
                debug_StdPrintfDummy("DELETED_MEMORY ");
            } else {
                debug_StdPrintfDummy("!!! unrecognized memory block !!!\n");
                return;
            }
            debug_StdPrintfDummy("siz:$%5x ", node->size << 4);
            for (p = 0; p < 12; p++) {
                debug_StdPrintfDummy("%c", node->name[p]);
            }
            debug_StdPrintfDummy("\n");
            node = node->next;
        }
    }
}

#ifdef ICO_HOST

/* the partition's start pointer and a node's next pointer, by offset */
void iosMallocCheckLeak2(__INTPTR_TYPE__ part, int offset)
{
    char *node = *(char **)(part + offset + __builtin_offsetof(IosMemPart, start));
#else
void iosMallocCheckLeak2(int part, int offset)
{
    char *node = *(char **)(part + offset + 0x38);
#endif
    int i;

    debug_StdPrintfDummy("<<< check leak2 >>> %p\n", part);
    if (node == 0) {
        return;
    }
    do {
        node += offset;
        strncpy(nodeName, node + 16, 15);
        nodeName[15] = 0;
        if (strcmp(node, "<ALLOC>________") == 0) {
            debug_StdPrintfDummy("%p:ALLOC %s\n", node - offset, nodeName);
        } else if (strcmp(node, "<FREE AREA>____") == 0) {
            debug_StdPrintfDummy("%p:FREEAREA\n", node - offset);
        } else if (strcmp(node, " free memory   ") == 0) {
            debug_StdPrintfDummy("%p:DELETED_MEMORY\n");
        } else {
            debug_StdPrintfDummy("%p:!!! unrecognized block!!!:%s\n", node - offset, node);
            return;
        }
        for (i = 0; i < 12; i++) {}
#ifdef ICO_HOST
        node = *(char *volatile *)(node + __builtin_offsetof(IosMemNode, next));
#else
        node = *(char *volatile *)(node + 0x24);
#endif
    } while (node != 0);
}

/* the 0x3C-byte node record realloc moves: everything up to the line number */
typedef struct IosMemNodeRec {    /* field names derived */
    char tag[16];                 /* 0x00 */
    char name[16];                /* 0x10 */
    struct IosMemNode *prev;      /* 0x20 */
    struct IosMemNode *next;      /* 0x24 */
    struct IosMemNode *free_prev; /* 0x28 */
    struct IosMemNode *free_next; /* 0x2C */
    struct IosMemPart *part;      /* 0x30 */
    int size;                     /* 0x34 */
    int line;                     /* 0x38 */
} IosMemNodeRec;                  /* derived name */

void *iosReallocDebug(void *ptr, unsigned int size)
{
    char buf[1024];
    IosMemNode *node;
    IosMemNode *nd;
    IosMemNode *p;
    IosMemNode *prev;
    IosMemNode *next;
    int n;
    int d;

    if (ptr == 0) {
        debug_assertMessage(__FILE__, 1182, "IOSREALLOC():\nNULL MEMORY POINTER AT MALLOC\n");
        __assert(__FILE__, 1182, "e");
        return 0;
    }
    next = ptr;
    prev = (IosMemNode *)((char *)ptr - 16);
    if (strncmp(prev->tag, "align", 5) == 0) {
        n = atoi((char *)ptr - 11);
        next = (IosMemNode *)((char *)prev - (n - 16));
        *((char *)ptr - 16) = 0;
    }
    node = (IosMemNode *)((char *)next - NODE_SIZE);
    if (strcmp(node->tag, "<ALLOC>________") != 0) {
        sprintf(buf, "IOSFREE():\n\tPREV MAGIC: %s\n\t CUR MAGIC: %s\n\tNEXT MAGIC: %s\n",
                node->prev->tag, node->tag, node->next->tag);
        debug_assertMessage(__FILE__, 1199, buf);
        __assert(__FILE__, 1199, "e");
        return 0;
    }
    nd = node->next;
    if (strcmp(nd->tag, "<FREE AREA>____") != 0) {
        debug_StdPrintfDummy("mem:realloc; not support yet\n");
        ICO_BREAK();
        return 0;
    }
    n = (size + 0xF) >> 4;
    if (node->size - 0x40 < n) {
        debug_StdPrintfDummy("mem:realloc; not enough memory\n");
        ICO_BREAK();
        return 0;
    }
    d = node->size - n;
    p = (IosMemNode *)((char *)ptr + (n << 4));
    *(IosMemNodeRec *)p = *(IosMemNodeRec *)nd;
    node->size = node->size - d;
#ifdef ICO_HEAP_STATS
    ico_heap_stats_free(node->part, (unsigned int)d << 4);
#endif
#ifdef ICO_HEAP_ASAN
    heapAsanReq(node, (unsigned int)((char *)ptr - ((char *)node + NODE_SIZE)) + size);
#endif
    p->size = p->size + d;
    *(IosMemTag *)nd = *(IosMemTag *)" free memory   ";
    node->next = p;
    if (p->next != 0) {
        p->next->prev = p;
    }
    if (p->free_prev != 0) {
        p->free_prev->free_next = p;
    } else {
        node->part->head = p;
    }
    if (p->free_next != 0) {
        p->free_next->free_prev = p;
    }
    return ptr;
}

#ifdef ICO_HEAP_ASAN

#include <sanitizer/asan_interface.h>

#undef iosMallocInitPartition
#undef iosMallocSetPartition
#undef iosMallocResetPartition
#undef iosMallocClearPartition
#undef iosMallocDebug
#undef iosMallocDebugNoAssert
#undef iosMallocAlignDebug
#undef _iosFreeWithFill
#undef iosFree
#undef iosMallocCheckLeak
#undef iosMallocCheckLeak2
#undef iosReallocDebug

/* the root partitions (ranges not inside another), which hold all others */
#define HEAP_ASAN_ROOTS 8
static IosMemPart *heapAsanRoot[HEAP_ASAN_ROOTS];
static char *heapAsanRootEnd[HEAP_ASAN_ROOTS];
static int heapAsanRoots;
static int heapAsanDepth;
/* ICO_HEAP_ASAN_FREE=1 in the environment also poisons free areas' bodies;
   off by default, since the EE code writes into free memory on purpose in
   places (seki/src/Packet.c's line list end mark, BOOT_DIAG.md) */
static int heapAsanFree = -1;

/* each block's requested bytes + 1, by (node - root) / 16; 0 = unknown */
static unsigned int *heapAsanReqTab[HEAP_ASAN_ROOTS];

static int heapAsanRootOf(const void *p)
{
    int i;
    for (i = 0; i < heapAsanRoots; i++) {
        if ((const char *)p >= (const char *)heapAsanRoot[i] &&
            (const char *)p < heapAsanRootEnd[i]) {
            return i;
        }
    }
    return -1;
}

static void heapAsanReq(void *node, unsigned int bytes)
{
    int r = heapAsanRootOf(node);
    if (r >= 0) {
        heapAsanReqTab[r][((char *)node - (char *)heapAsanRoot[r]) >> 4] = bytes + 1;
    }
}

static void heapAsanOpen(void)
{
    int i;
    if (heapAsanDepth++ != 0) {
        return;
    }
    if (heapAsanFree < 0) {
        heapAsanFree = getenv("ICO_HEAP_ASAN_FREE") != 0 && atoi(getenv("ICO_HEAP_ASAN_FREE")) != 0;
    }
    for (i = 0; i < heapAsanRoots; i++) {
        ASAN_UNPOISON_MEMORY_REGION(heapAsanRoot[i],
                                    (size_t)(heapAsanRootEnd[i] - (char *)heapAsanRoot[i]));
    }
}

static void heapAsanPoisonPart(IosMemPart *part, int r)
{
    IosMemNode *node;
    IosMemNode *next;
    IosMemPart *c;
    char *body;
    int alloc;
    unsigned int len;
    unsigned int req;

    if (strcmp(part->tag, "<PARTITION>____") != 0) {
        return;
    }
    for (node = (IosMemNode *)part->start; node != 0; node = next) {
        if ((char *)node < (char *)heapAsanRoot[r] || (char *)node >= heapAsanRootEnd[r]) {
            break;
        }
        /* everything read from the header before it is poisoned */
        next = node->next;
        body = (char *)node + NODE_SIZE;
        len = (unsigned int)node->size << 4;
        alloc = strcmp(node->tag, "<ALLOC>________") == 0;
        ASAN_POISON_MEMORY_REGION(node, NODE_SIZE);
        if (alloc) {
            req = heapAsanReqTab[r][((char *)node - (char *)heapAsanRoot[r]) >> 4];
            if (req != 0 && req - 1 < len) {
                ASAN_POISON_MEMORY_REGION(body + (req - 1), len - (req - 1));
            }
        } else if (heapAsanFree && body + len <= heapAsanRootEnd[r]) {
            ASAN_POISON_MEMORY_REGION(body, len);
        }
    }
    for (c = part->child; c != 0; c = c->next) {
        heapAsanPoisonPart(c, r);
    }
}

static void heapAsanClose(void)
{
    int i;
    if (--heapAsanDepth != 0) {
        return;
    }
    for (i = 0; i < heapAsanRoots; i++) {
        heapAsanPoisonPart(heapAsanRoot[i], i);
    }
}

IosMemPart *iosMallocInitPartition(IosMemAddr start, IosMemAddr end)
{
    IosMemPart *part;
    int r;

    heapAsanOpen();
    part = heapAsan_iosMallocInitPartition(start, end);
    if (part != 0 && heapAsanRootOf(part) < 0 && heapAsanRoots < HEAP_ASAN_ROOTS) {
        r = heapAsanRoots++;
        heapAsanRoot[r] = part;
        heapAsanRootEnd[r] = part->end;
        heapAsanReqTab[r] = calloc((size_t)(part->end - (char *)part) >> 4, sizeof(unsigned int));
    }
    heapAsanClose();
    return part;
}

IosMemPart *iosMallocSetPartition(IosMemPart *part, int size, int align)
{
    IosMemPart *ret;
    heapAsanOpen();
    ret = heapAsan_iosMallocSetPartition(part, size, align);
    heapAsanClose();
    return ret;
}

IosMemPart *iosMallocResetPartition(IosMemPart *part)
{
    IosMemPart *ret;
    heapAsanOpen();
    ret = heapAsan_iosMallocResetPartition(part);
    heapAsanClose();
    return ret;
}

void iosMallocClearPartition(IosMemPart *part)
{
    heapAsanOpen();
    heapAsan_iosMallocClearPartition(part);
    heapAsanClose();
}

void *iosMallocDebug(IosMemPart *part, int size, const char *file, int line)
{
    void *ret;
    heapAsanOpen();
    ret = heapAsan_iosMallocDebug(part, size, file, line);
    heapAsanClose();
    return ret;
}

void *iosMallocDebugNoAssert(IosMemPart *part, int size, const char *file, int line)
{
    void *ret;
    heapAsanOpen();
    ret = heapAsan_iosMallocDebugNoAssert(part, size, file, line);
    heapAsanClose();
    return ret;
}

void *iosMallocAlignDebug(IosMemPart *part, int size, int align, const char *file, int line)
{
    void *ret;
    heapAsanOpen();
    ret = heapAsan_iosMallocAlignDebug(part, size, align, file, line);
    heapAsanClose();
    return ret;
}

void _iosFreeWithFill(int *ptr, char *file, int line)
{
    heapAsanOpen();
    heapAsan__iosFreeWithFill(ptr, file, line);
    heapAsanClose();
}

void *iosFree(void *ptr)
{
    void *ret;
    heapAsanOpen();
    ret = heapAsan_iosFree(ptr);
    heapAsanClose();
    return ret;
}

void iosMallocCheckLeak(IosMemPart *part)
{
    heapAsanOpen();
    heapAsan_iosMallocCheckLeak(part);
    heapAsanClose();
}

void iosMallocCheckLeak2(__INTPTR_TYPE__ part, int offset)
{
    heapAsanOpen();
    heapAsan_iosMallocCheckLeak2(part, offset);
    heapAsanClose();
}

void *iosReallocDebug(void *ptr, unsigned int size)
{
    void *ret;
    heapAsanOpen();
    ret = heapAsan_iosReallocDebug(ptr, size);
    heapAsanClose();
    return ret;
}

#endif
