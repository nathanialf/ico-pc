/*
 * port/game/eye_trail.h
 *
 * The shadow creatures' eye trails, kept in the world as well as on the
 * screen, so photo mode can draw them from its camera.
 *
 * Each eye trails a point blur (ico2/sugipon/src/enemyParts.c PointBlur):
 * a strip of num point pairs in screen space.  UpdatePointBlur projects the
 * eye's point through the frame camera (matrixptr +0x80, then +0xC0) once a
 * tick and builds the newest pair across the line to the point before; the
 * older pairs are the earlier ticks' pairs, shifted along.  The strip holds
 * only screen positions, so with the game paused (the update does not run)
 * and photo mode's camera in the matrices (photo_view.h), the eye's model
 * follows the photo camera while the strip stays where the game camera put
 * it.
 *
 * Here each trail's points are kept as the update saw them: the point's
 * world position, the world position of the point it was joined to, its
 * width, and the pair the game wrote.  ico_eye_trail_rebuild repeats the
 * update's arithmetic for every point through the matrices given, which is
 * the game's strip byte for byte when they are the matrices the game used.
 * 64 trails, keyed by the PointBlur's address; a new trail past that takes
 * the slot pushed to longest ago.
 */
#ifndef ICO_PORT_GAME_EYE_TRAIL_H
#define ICO_PORT_GAME_EYE_TRAIL_H

#ifdef __cplusplus
extern "C" {
#endif

#define ICO_EYE_TRAIL_SLOTS 64
#define ICO_EYE_TRAIL_POINTS 10 /* the shadows' eyes (enemy.c InitEnemyEye(10, ...)) */

/* From InitPointBlur: a trail of num points for key, nothing pushed yet;
   any trail key had is forgotten.  num outside 1 .. ICO_EYE_TRAIL_POINTS:
   no trail for key */
void ico_eye_trail_init(const void *key, int num);

/* From UpdatePointBlur, once the strip's newest pair is written: world is
   the point the update projected (4 floats), f its width (added to the
   view-space x), reset the blur's dirty flag (the update then copies the
   newest pair over the whole trail and does not shift it), pair the
   strip's first two vertices (8 words).  Nothing for a key with no trail */
void ico_eye_trail_push(const void *key, const float *world, float f, int reset, const int *pair);

/* key's strip as UpdatePointBlur builds it with view (the +0x80 matrix)
   and screen (the +0xC0 matrix): num * 2 vertices of 4 words into out.
   Returns 1, or 0 with out unspecified when key has no trail of num
   points, num * 2 > cap, a point was joined to one the trail never saw
   (the first update after the init), or strip (the game's strip, num * 2
   vertices) is not the strip the pushes recorded.  The current matrix
   (matrix_stack.c) is as it was on return */
int ico_eye_trail_rebuild(const void *key, int num, const int *strip, const void *view,
                          const void *screen, int *out, int cap);

/* The end of a photo mode session (photo_view.c leave): a summary line of
   the session's rebuilds when there were any; the counts start again */
void ico_eye_trail_photo_end(void);

/* The session's counts: the rebuilds drawn and those that fell back to the
   game's strip (tests) */
void ico_eye_trail_counts(unsigned *rebuilt, unsigned *kept);

/* Forgets every trail and count (tests) */
void ico_eye_trail_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* ICO_PORT_GAME_EYE_TRAIL_H */
