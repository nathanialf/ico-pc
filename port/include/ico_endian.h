/*
 * port/include/ico_endian.h
 *
 * Little-endian reads from a byte buffer (the disc's and the card's
 * structures, the archive, the ELF, the IRX, the movies), whatever the host's
 * order.
 * Included by a relative path ("../include/ico_endian.h") so every target
 * that compiles a user gets it without an include directory.
 */
#ifndef ICO_ENDIAN_H
#define ICO_ENDIAN_H

#include <stdint.h>

static inline uint16_t ico_le16(const uint8_t *p)
{
    return (uint16_t)((unsigned)p[0] | (unsigned)p[1] << 8);
}

static inline uint32_t ico_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

#endif /* ICO_ENDIAN_H */
