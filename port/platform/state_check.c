/*
 * port/platform/state_check.c
 *
 * The program's image range and the typed self-checks a load runs after
 * its relocation (state_reloc.h): the game heap's walk, the scheduler's
 * threads, the kernel's handlers.
 */
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE /* dl_iterate_phdr */
#endif
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#else
#include <link.h>
#endif

#include "kernel_host.h"
#include "sched.h"
#include "state_reloc.h"
/* the allocator's records (the game's header; plain pointers and ints, so
   the port's compile flags lay them out as the game's do) */
#include "../../ico2/fumi/include/memory.h"

/* --- the image ------------------------------------------------------------- */

#if defined(_WIN32)

int ico_state_image_range(IcoStateImage *out)
{
    HMODULE m = NULL;
    const IMAGE_DOS_HEADER *dos;
    const IMAGE_NT_HEADERS *nt;
    const IMAGE_SECTION_HEADER *sec;
    unsigned i;

    memset(out, 0, sizeof *out);
    /* the module this code is in (the program itself on every Windows build) */
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)(const void *)ico_state_image_range, &m) ||
        m == NULL) {
        return -1;
    }
    dos = (const IMAGE_DOS_HEADER *)m;
    nt = (const IMAGE_NT_HEADERS *)((const char *)m + dos->e_lfanew);
    out->lo = (uint64_t)(uintptr_t)m;
    out->hi = out->lo + nt->OptionalHeader.SizeOfImage;
    sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if ((sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) {
            const uint64_t lo = out->lo + sec[i].VirtualAddress;
            const uint64_t hi = lo + sec[i].Misc.VirtualSize;

            if (out->text_lo == 0 || lo < out->text_lo) {
                out->text_lo = lo;
            }
            if (hi > out->text_hi) {
                out->text_hi = hi;
            }
        }
    }
    if (out->text_lo == 0) {
        memset(out, 0, sizeof *out);
        return -1;
    }
    return 0;
}

#elif defined(__APPLE__)

int ico_state_image_range(IcoStateImage *out)
{
    Dl_info info;
    uint32_t i;

    memset(out, 0, sizeof *out);
    if (!dladdr((const void *)ico_state_image_range, &info) || info.dli_fbase == NULL) {
        return -1;
    }
    for (i = 0; i < _dyld_image_count(); i++) {
        const struct mach_header_64 *h = (const struct mach_header_64 *)_dyld_get_image_header(i);
        const struct load_command *c;
        const intptr_t slide = _dyld_get_image_vmaddr_slide(i);
        uint32_t j;

        if ((const void *)h != info.dli_fbase || h->magic != MH_MAGIC_64) {
            continue;
        }
        c = (const struct load_command *)(h + 1);
        for (j = 0; j < h->ncmds; j++) {
            if (c->cmd == LC_SEGMENT_64) {
                const struct segment_command_64 *seg = (const struct segment_command_64 *)c;
                /* __PAGEZERO maps nothing */
                if (seg->vmsize != 0 && seg->initprot != 0) {
                    const uint64_t lo = (uint64_t)(seg->vmaddr + (uint64_t)slide);
                    const uint64_t hi = lo + seg->vmsize;

                    if (out->lo == 0 || lo < out->lo) {
                        out->lo = lo;
                    }
                    if (hi > out->hi) {
                        out->hi = hi;
                    }
                    if (strcmp(seg->segname, SEG_TEXT) == 0) {
                        out->text_lo = lo;
                        out->text_hi = hi;
                    }
                }
            }
            c = (const struct load_command *)((const char *)c + c->cmdsize);
        }
        break;
    }
    if (out->lo == 0 || out->text_lo == 0) {
        memset(out, 0, sizeof *out);
        return -1;
    }
    return 0;
}

#else

typedef struct ImageFind {
    uintptr_t probe; /* an address of this code */
    IcoStateImage *out;
    int found;
} ImageFind;

static int image_phdr(struct dl_phdr_info *info, size_t size, void *user)
{
    ImageFind *f = (ImageFind *)user;
    IcoStateImage im;
    int hit = 0;
    int i;

    (void)size;
    memset(&im, 0, sizeof im);
    for (i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        uint64_t lo, hi;

        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) {
            continue;
        }
        lo = (uint64_t)info->dlpi_addr + (uint64_t)ph->p_vaddr;
        hi = lo + (uint64_t)ph->p_memsz;
        if (im.hi == 0 || lo < im.lo) {
            im.lo = lo;
        }
        if (hi > im.hi) {
            im.hi = hi;
        }
        if ((ph->p_flags & PF_X) != 0) {
            if (im.text_hi == 0 || lo < im.text_lo) {
                im.text_lo = lo;
            }
            if (hi > im.text_hi) {
                im.text_hi = hi;
            }
            if ((uint64_t)f->probe >= lo && (uint64_t)f->probe < hi) {
                hit = 1;
            }
        }
    }
    if (!hit) {
        return 0;
    }
    *f->out = im;
    f->found = 1;
    return 1;
}

int ico_state_image_range(IcoStateImage *out)
{
    ImageFind f;

    memset(out, 0, sizeof *out);
    f.probe = (uintptr_t)(const void *)ico_state_image_range;
    f.out = out;
    f.found = 0;
    dl_iterate_phdr(image_phdr, &f);
    if (!f.found || out->text_hi == 0) {
        memset(out, 0, sizeof *out);
        return -1;
    }
    return 0;
}

#endif

/* --- the heap walk --------------------------------------------------------- */

/* the marks memory.c writes into a record's first 16 bytes (strcmp's NUL
   included) */
static const char MARK_PART[16] = "<PARTITION>____";
static const char MARK_ALLOC[16] = "<ALLOC>________";
static const char MARK_FREE[16] = "<FREE AREA>____";
static const char MARK_DELETED[16] = " free memory   ";

#define HEAP_MAX_DEPTH 16
#define HEAP_MAX_CHILDREN 4096

typedef struct StateHeapWalk {
    uint64_t lo, hi;
    unsigned partitions, nodes;
    unsigned long long max_nodes;
    char *why;
    size_t n;
} StateHeapWalk;

/* a record of `size` bytes at p lies wholly inside the arena, quadword
   aligned (the allocator's records always are) */
static int rec_ok(const StateHeapWalk *w, const void *p, size_t size)
{
    const uint64_t a = (uint64_t)(uintptr_t)p;

    return (a & 15u) == 0 && a >= w->lo && a <= w->hi && w->hi - a >= size;
}

/* a link: 0, or an address inside the arena (its end included) */
static int link_ok(const StateHeapWalk *w, const void *p)
{
    const uint64_t a = (uint64_t)(uintptr_t)p;

    return a == 0 || (a >= w->lo && a <= w->hi);
}

static int fail(StateHeapWalk *w, const char *what, const void *at)
{
    if (w->why != NULL && w->n > 0) {
        snprintf(w->why, w->n, "the game heap: %s at %p", what, at);
    }
    return -1;
}

static int walk_part(StateHeapWalk *w, const IosMemPart *part, int depth)
{
    const IosMemNode *node;
    const IosMemNode *last = NULL;
    const IosMemPart *child;
    unsigned long long count = 0;
    unsigned children = 0;

    if (depth > HEAP_MAX_DEPTH) {
        return fail(w, "partitions nested too deep", part);
    }
    if (!rec_ok(w, part, sizeof *part)) {
        return fail(w, "a partition outside the arena", part);
    }
    if (memcmp(part->tag, MARK_PART, sizeof MARK_PART) != 0) {
        return fail(w, "a partition without its mark", part);
    }
    if (!link_ok(w, part->parent) || !link_ok(w, part->next) || !link_ok(w, part->child) ||
        !link_ok(w, part->start) || !link_ok(w, part->end) || !link_ok(w, part->top) ||
        !link_ok(w, part->head)) {
        return fail(w, "a partition link outside the arena", part);
    }
    if (part->start == NULL || part->end == NULL || part->top == NULL || part->start > part->top ||
        part->top > part->end) {
        return fail(w, "a partition's start, top and end out of order", part);
    }
    w->partitions++;
    for (node = (const IosMemNode *)part->start; node != NULL; node = node->next) {
        if (!rec_ok(w, node, sizeof *node)) {
            return fail(w, "a block header outside the arena", node);
        }
        if (last != NULL && node <= last) {
            return fail(w, "a block chain that goes backwards", node);
        }
        if (++count > w->max_nodes) {
            return fail(w, "a block chain longer than the arena", node);
        }
        if (memcmp(node->tag, MARK_ALLOC, sizeof MARK_ALLOC) == 0) {
            if (!link_ok(w, node->part)) {
                return fail(w, "a block in use whose partition is outside the arena", node);
            }
        } else if (memcmp(node->tag, MARK_FREE, sizeof MARK_FREE) != 0 &&
                   memcmp(node->tag, MARK_DELETED, sizeof MARK_DELETED) != 0) {
            return fail(w, "a block without an allocated or free mark", node);
        }
        if (!link_ok(w, node->prev) || !link_ok(w, node->next) || !link_ok(w, node->free_prev) ||
            !link_ok(w, node->free_next)) {
            return fail(w, "a block link outside the arena", node);
        }
        last = node;
    }
    w->nodes += (unsigned)count;
    /* the free list, which iosMalloc walks from head */
    {
        unsigned long long free_count = 0;

        for (node = part->head; node != NULL; node = node->free_next) {
            if (!rec_ok(w, node, sizeof *node)) {
                return fail(w, "a free block outside the arena", node);
            }
            if (memcmp(node->tag, MARK_FREE, sizeof MARK_FREE) != 0) {
                return fail(w, "a block on the free list without the free mark", node);
            }
            if (++free_count > count + 1) {
                return fail(w, "a free list longer than the block chain", node);
            }
            if (!link_ok(w, node->free_next)) {
                return fail(w, "a free list link outside the arena", node);
            }
        }
    }
    for (child = part->child; child != NULL; child = child->next) {
        if (++children > HEAP_MAX_CHILDREN) {
            return fail(w, "a partition with too many children", part);
        }
        if (!rec_ok(w, child, sizeof *child)) {
            return fail(w, "a child partition outside the arena", child);
        }
        if (child->parent != part) {
            return fail(w, "a child partition whose parent is another", child);
        }
        if (walk_part(w, child, depth + 1) != 0) {
            return -1;
        }
    }
    return 0;
}

int ico_state_heap_walk(const void *root, uint64_t lo, uint64_t hi, unsigned *partitions,
                        unsigned *nodes, char *why, size_t n)
{
    StateHeapWalk w;
    int rc;

    memset(&w, 0, sizeof w);
    w.lo = lo;
    w.hi = hi;
    w.max_nodes = hi > lo ? (hi - lo) / sizeof(IosMemNode) + 1 : 0;
    w.why = why;
    w.n = n;
    if (why != NULL && n > 0) {
        why[0] = '\0';
    }
    if (root == NULL) {
        rc = fail(&w, "no root partition", root);
    } else if (hi <= lo) {
        rc = fail(&w, "an empty arena", root);
    } else {
        rc = walk_part(&w, (const IosMemPart *)root, 0);
    }
    if (partitions != NULL) {
        *partitions = w.partitions;
    }
    if (nodes != NULL) {
        *nodes = w.nodes;
    }
    return rc;
}

/* --- the self-checks ------------------------------------------------------- */

static int in_text(const IcoStateImage *im, uint64_t a)
{
    return a >= im->text_lo && a < im->text_hi;
}

static int in_image(const IcoStateImage *im, uint64_t a)
{
    return a >= im->lo && a < im->hi;
}

#define CHECK_MAX_HANDLERS 512

int ico_state_reloc_check(IcoStateCheck *c)
{
    static unsigned long long fns[CHECK_MAX_HANDLERS];
    int id;
    int n, i;

    c->partitions = c->nodes = c->threads = c->handlers = 0;
    c->why[0] = '\0';
    if (c->image.text_hi <= c->image.text_lo || c->image.hi <= c->image.lo) {
        snprintf(c->why, sizeof c->why, "the program's image is unknown");
        return -1;
    }
    if (c->heap_root != NULL &&
        ico_state_heap_walk(c->heap_root, c->arena_lo, c->arena_hi, &c->partitions, &c->nodes,
                            c->why, sizeof c->why) != 0) {
        return -1;
    }
    for (id = 1; id < ICO_SCHED_MAX_THREADS; id++) {
        IcoSchedView v;

        if (!ico_sched_view(id, &v)) {
            continue;
        }
        c->threads++;
        if (!in_text(&c->image, (uint64_t)(uintptr_t)v.entry)) {
            snprintf(c->why, sizeof c->why, "thread %d's entry %p is outside the program's code",
                     id, v.entry);
            return -1;
        }
        if (v.note != NULL && !in_image(&c->image, (uint64_t)(uintptr_t)v.note)) {
            snprintf(c->why, sizeof c->why,
                     "thread %d's last kernel call's name %p is outside the program", id,
                     (const void *)v.note);
            return -1;
        }
        if (v.note_caller != NULL && !in_text(&c->image, (uint64_t)(uintptr_t)v.note_caller)) {
            snprintf(c->why, sizeof c->why,
                     "thread %d's last kernel call's caller %p is outside the program's code", id,
                     v.note_caller);
            return -1;
        }
    }
    n = ico_kernel_state_handlers(fns, CHECK_MAX_HANDLERS);
    if (n > CHECK_MAX_HANDLERS) {
        snprintf(c->why, sizeof c->why, "%d interrupt handlers, more than the check reads", n);
        return -1;
    }
    for (i = 0; i < n; i++) {
        if (!in_text(&c->image, fns[i])) {
            snprintf(c->why, sizeof c->why,
                     "an interrupt handler %#llx is outside the program's code", fns[i]);
            return -1;
        }
    }
    c->handlers = (unsigned)n;
    return 0;
}
