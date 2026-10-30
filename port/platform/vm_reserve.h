/*
 * port/platform/vm_reserve.h
 *
 * Address space at a chosen address: the pinned arena (arena.c) and the
 * fiber pool (fiber.c) sit at fixed addresses so a save state's pointers
 * into them stay valid in the next process (docs/port/PLATFORM.md, "Fibers
 * and the scheduler").  mmap with MAP_FIXED_NOREPLACE on Linux and Android
 * (an older kernel takes the flag as a hint: the result is checked), mmap
 * with the address as a hint on Apple (checked), VirtualAlloc at the
 * address on Windows.
 */
#ifndef ICO_PLATFORM_VM_RESERVE_H
#define ICO_PLATFORM_VM_RESERVE_H

#include <stddef.h>
#include <stdint.h>

/* Reserves `bytes` at exactly `at` (0: anywhere the system likes).
   commit 1: readable, writable and zero filled; 0: no access, nothing
   committed.  NULL when the address is taken or the memory is not
   available (nothing is left mapped). */
void *ico_vm_reserve(uintptr_t at, size_t bytes, int commit);
/* Gives a reservation back (the whole of it). */
void ico_vm_release(void *p, size_t bytes);
/* Makes [p, p+bytes) inside a reservation readable and writable; pages
   never written read as zero.  Returns 0 or -1. */
int ico_vm_commit(void *p, size_t bytes);
/* Back to no access, the pages dropped (they read as zero when committed
   again). */
void ico_vm_decommit(void *p, size_t bytes);
/* The system's page size. */
size_t ico_vm_page_size(void);

#endif /* ICO_PLATFORM_VM_RESERVE_H */
