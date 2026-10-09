/*
 * port/fmv/field_match.h
 *
 * Field matching (inverse telecine) for a film carried in interlaced
 * pictures: a picture whose two fields come from different film frames is
 * combed as decoded, but its own field woven with the other field of the
 * previous or the next picture is the film frame again.  This picks, per
 * picture, which of the three makes the least combed whole picture, for
 * rd_video_field's RD_VIDEO_WEAVE_* fills (the values match).  Luma only,
 * every second column: a few hundred thousand byte compares per picture.
 */
#ifndef ICO_PORT_FMV_FIELD_MATCH_H
#define ICO_PORT_FMV_FIELD_MATCH_H

#include <stdint.h>

enum {
    ICO_FIELD_MATCH_PREV = 1, /* the other rows from the previous picture */
    ICO_FIELD_MATCH_NEXT = 2, /* from the next picture */
    ICO_FIELD_MATCH_CUR = 3   /* the picture's own: as decoded */
};

/* The comb count of the picture woven from keep's rows of one parity
   (keep_odd: the odd rows) and other's rows of the other parity: pixels
   (every second column) whose row steps away from both neighbouring rows
   by more than 10 in the same direction.  Both pictures w x h luma at
   pitch. */
uint32_t ico_field_comb(const uint8_t *keep, const uint8_t *other, uint32_t pitch, uint32_t w,
                        uint32_t h, int keep_odd);
/* The fill for cur's field (keep_odd as above): ICO_FIELD_MATCH_CUR unless
   prev's or next's rows comb clearly less (under half and by more than
   0.1 % of the samples).  prev or next may be NULL. */
int ico_field_match(const uint8_t *prev, const uint8_t *cur, const uint8_t *next, uint32_t pitch,
                    uint32_t w, uint32_t h, int keep_odd);

#endif /* ICO_PORT_FMV_FIELD_MATCH_H */
