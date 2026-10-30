/*
 * port/platform/state_registry.h   (CONTRACT, v0.9.6)
 *
 * The port's simulation globals a save state must carry (plan.md "156
 * design", Registry).  Each owner registers its blocks once at init, by
 * name; state_host.c copies every block at a save and restores every block
 * at a load, in registration order.  A block's name and size are part of
 * the state's validity (a missing or resized block refuses the load).
 */
#ifndef ICO_PLATFORM_STATE_REGISTRY_H
#define ICO_PLATFORM_STATE_REGISTRY_H

#include <stddef.h>

#define ICO_STATE_SCAN 1u  /* may hold host pointers: the relocation scan runs over it */
#define ICO_STATE_AUDIO 2u /* copied and restored under ico_audio_lock */
#define ICO_STATE_CLOCK 4u /* a counter the load rewinds (main ticks, the pad script's cursor) */
#define ICO_STATE_MAX_ENTRIES 64

/* Registers [p, p+n) under `name` (a string literal; kept by pointer).
   Returns 0, or -1 when the table is full or the name repeats. */
int ico_state_register(const char *name, void *p, size_t n, unsigned flags);

/* The table, for state_host.c and the tests. */
typedef struct IcoStateEntry {
    const char *name;
    void *p;
    size_t n;
    unsigned flags;
} IcoStateEntry;

int ico_state_registry_count(void);
const IcoStateEntry *ico_state_registry_entry(int i);
/* Forgets every entry (tests). */
void ico_state_registry_reset(void);

#endif /* ICO_PLATFORM_STATE_REGISTRY_H */
