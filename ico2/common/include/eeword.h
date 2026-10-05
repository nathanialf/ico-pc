/*
 * ico2/common/include/eeword.h
 *
 * EE address words: a 32-bit word in a disc record, or in a record the game
 * keeps at the EE's layout, that holds a heap address.  Port header (package
 * 2C, docs/port/LOADERS.md); the EE build expands every macro to the
 * original cast, so its code is unchanged.
 *
 * The loaders relocate file offsets in place ("p->wcl = (int)p + p->wcl")
 * and the readers cast the word back to a pointer.  A 64-bit host cannot
 * keep an address in 4 bytes, but every address these words hold is a heap
 * address, and the host heap is one block, the EE RAM arena
 * (port/platform/arena.h), in which a block sits at the offset the EE
 * allocator gave it.  So on the host a word holds the address's offset in
 * the arena: the position the host's allocator gave the block in the
 * simulated EE RAM (the EE's number only if the allocator's record sizes
 * agreed with the EE's, which they do not on x64).
 *
 *   ICO_EEWORD(T)    the type of a frozen record's field that holds an
 *                    address: T (the original pointer or int type) on the
 *                    EE, a 32-bit unsigned word on the host.
 *   ICO_EEW(p)       the word for pointer p, as an int: `(int)(p)` on the
 *                    EE.  Null gives 0.  On the host, p must lie in the
 *                    arena (a heap block, or the GObj table); anything else
 *                    traps rather than store a word that cannot be read
 *                    back.
 *   ICO_EEPTR(T, w)  the pointer of type T for word w: `(T)(w)` on the EE.
 *                    0 gives the null pointer.
 *
 * A word is never 0 for a heap address (the EE heap starts at 0x760000), so
 * the code's "== 0" tests on words keep their meaning.
 */
#ifndef EEWORD_H
#define EEWORD_H

#ifdef ICO_HOST

/* port/platform/arena.h; declared here so game headers need not reach the
   platform's include directory */
unsigned char *ico_arena_base(void);
int ico_arena_contains(const void *p, __SIZE_TYPE__ n);

typedef unsigned int IcoEEWord;

#define ICO_EEWORD(T) IcoEEWord

static __inline__ IcoEEWord ico_eew(const void *p)
{
    if (p == 0) {
        return 0;
    }
    if (!ico_arena_contains(p, 0)) {
        __builtin_trap();
    }
    return (IcoEEWord)((const unsigned char *)p - ico_arena_base());
}

static __inline__ void *ico_eeptr(IcoEEWord w)
{
    return w == 0 ? (void *)0 : (void *)(ico_arena_base() + w);
}

#define ICO_EEW(p) ((int)ico_eew(p))
#define ICO_EEPTR(T, w) ((T)ico_eeptr((IcoEEWord)(w)))

#else

#define ICO_EEWORD(T) T
#define ICO_EEW(p) ((int)(p))
#define ICO_EEPTR(T, w) ((T)(w))

#endif

#endif /* EEWORD_H */
