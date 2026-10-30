/*
 * port/platform/vm_reserve.c
 *
 * Reservations at a chosen address (vm_reserve.h).
 */
#include "vm_reserve.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#else

#include <sys/mman.h>
#include <unistd.h>

/* Linux 4.17; an older kernel ignores the unknown flag and takes the
   address as a hint, which the check below catches. */
#if defined(__linux__) && !defined(MAP_FIXED_NOREPLACE)
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

#endif

void *ico_vm_reserve(uintptr_t at, size_t bytes, int commit)
{
#if defined(_WIN32)
    /* VirtualAlloc memory is zero filled; an address it cannot take fails
       the call (at is 64 KB aligned, the allocation granularity) */
    void *m = VirtualAlloc((void *)at, bytes, MEM_RESERVE | (commit ? MEM_COMMIT : 0),
                           commit ? PAGE_READWRITE : PAGE_NOACCESS);
    if (m != NULL && at != 0 && (uintptr_t)m != at) {
        VirtualFree(m, 0, MEM_RELEASE);
        return NULL;
    }
    return m;
#else
    int prot = commit ? PROT_READ | PROT_WRITE : PROT_NONE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | (commit ? 0 : MAP_NORESERVE);
    void *m;
#if defined(__linux__)
    if (at != 0) {
        flags |= MAP_FIXED_NOREPLACE;
    }
#endif
    m = mmap((void *)at, bytes, prot, flags, -1, 0);
    if (m == MAP_FAILED) {
        return NULL;
    }
    if (at != 0 && (uintptr_t)m != at) {
        munmap(m, bytes);
        return NULL;
    }
    return m;
#endif
}

void ico_vm_release(void *p, size_t bytes)
{
    if (p == NULL) {
        return;
    }
#if defined(_WIN32)
    (void)bytes;
    VirtualFree(p, 0, MEM_RELEASE);
#else
    munmap(p, bytes);
#endif
}

int ico_vm_commit(void *p, size_t bytes)
{
#if defined(_WIN32)
    return VirtualAlloc(p, bytes, MEM_COMMIT, PAGE_READWRITE) != NULL ? 0 : -1;
#else
    return mprotect(p, bytes, PROT_READ | PROT_WRITE) == 0 ? 0 : -1;
#endif
}

void ico_vm_decommit(void *p, size_t bytes)
{
#if defined(_WIN32)
    VirtualFree(p, bytes, MEM_DECOMMIT);
#else
    /* a fresh no-access mapping over the range drops its pages on every
       POSIX system (MADV_DONTNEED does not on Apple); the range is ours, so
       MAP_FIXED replaces nothing else */
    if (mmap(p, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0) ==
        MAP_FAILED) {
        madvise(p, bytes, MADV_DONTNEED);
        mprotect(p, bytes, PROT_NONE);
    }
#endif
}

size_t ico_vm_page_size(void)
{
    static size_t ps;
    if (ps == 0) {
#if defined(_WIN32)
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        ps = si.dwPageSize;
#else
        long v = sysconf(_SC_PAGESIZE);
        ps = v > 0 ? (size_t)v : 4096;
#endif
    }
    return ps;
}
