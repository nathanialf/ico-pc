/* rd_hash.h: the per-draw lookups' hash (rd_hash.c): the uniform blocks'
 * cache (rd_replay.c uniformOffset) and the pipeline index (rd_pipeline.c
 * keyHash).  It only picks a slot: a hit is still the bytes compared
 * (memcmp, keyEqual), so another hash moves which entries share a slot,
 * never what a draw reads. */
#ifndef ICO_RD_HASH_H
#define ICO_RD_HASH_H

#include <stddef.h>
#include <stdint.h>

/* The n bytes at p (any alignment), 8 at a time, and seed: a 64-bit
 * multiply-xorshift step a word, the tail's bytes as one zero-padded word,
 * the length and seed in the start value, a final avalanche (MurmurHash3's
 * fmix64 constants); the low 32 bits.  The value depends on the host's byte
 * order (a slot choice only). */
uint32_t rd__hash_words(const void *p, size_t n, uint32_t seed);

#endif
