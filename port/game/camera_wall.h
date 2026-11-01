/*
 * port/game/camera_wall.h
 *
 * The blended camera's path through the stage.  With pictures blended between
 * ticks (video_options.h ico_video_interpolate) the presenter lerps the eye
 * from the tick before's place to the tick's (port/render/rd_camera_blend.h),
 * and it snaps to the tick only on a cut, a keep frame, a fade edge or a step
 * past RD_INTERP_CAMERA_MOVE / RD_INTERP_CAMERA_TURN.  The game's cameras have
 * no wall test of their own (designed camera sets and paths), so an eye that
 * moves less than that in a tick but past a wall puts the blended eye behind
 * the wall for the pictures in between: the far side shows for an instant.
 *
 * At each flip (ico2/seki/src/GsBase.c gsbHostCullFlip, the three
 * gsb_UpdateGSSystem copies, so every disc) the step from the eye at the flip
 * before to this one is tested against the stage's collision: the walls and
 * floors of the objects the actors collide with (ico2/fumi/src/fieldCollision.c
 * MakeCollisionDependGObjList's filter), either way through.  A crossing
 * makes the frame closing at that flip a cut (rd.h rd_camera_cut_wall,
 * RD_SNAP_WALL): its pictures are the tick's own, as at the Original frame
 * rate.  The test runs at every flip whatever the Frame rate setting and in
 * the headless build too; it reads the game's memory and writes none of it
 * (no state of the game's depends on it, and it is no part of a save state).
 *
 * Two files:
 *   camera_wall.c        the rule and the segment geometry on plain floats
 *                        (no game header; camera_wall_test)
 *   camera_wall_stage.c  the live check: the stage's collision objects read
 *                        through the game's headers, the rule, the log line
 *                        (camera_wall_stage_test)
 */
#ifndef ICO_PORT_GAME_CAMERA_WALL_H
#define ICO_PORT_GAME_CAMERA_WALL_H

#ifdef __cplusplus
extern "C" {
#endif

/* What the eye's path crossed */
enum {
    ICO_CAMERA_WALL_NONE = 0,
    ICO_CAMERA_WALL_WALL = 1,  /* a wall (FcWallEnt) */
    ICO_CAMERA_WALL_FLOOR = 2, /* a floor or the underside of one (FcFloorEnt) */
};

/* How far past an element's edges a crossing still counts, in the game's
   units (centimetres): the near plane's 2 (GsBase.c vsParam[7]) and as much
   again, so a path that clears a wall's end or a floor's rim by less than the
   near plane is a crossing too */
#define ICO_CAMERA_WALL_RADIUS 4.0f

/* One wall in its object's frame, as fieldCollision.c clip_wall_1 reads an
   FcWallEnt: vertical, measured from its first corner (x, z) along
   (nz, -nx) for `length` units (FcWallEnt.height), its bottom edge from
   bottom[0] (at 0) to bottom[1] (at length) and its top from top[0] to
   top[1] (the corners' heights pt[0..3].y); n = (nx, nz) the unit normal
   (FC_WALL_NORMAL) */
typedef struct IcoCameraWallQuad {
    float x, z;
    float nx, nz;
    float length;
    float bottom[2];
    float top[2];
} IcoCameraWallQuad;

/* One floor in its object's frame, as clip_floor_1 reads an FcFloorEnt:
   the plane n . p + d = 0 and the polygon's corners in x and z (3 or 4,
   FcFloorEnt.nex + 3) */
typedef struct IcoCameraWallFloor {
    float n[3];
    float d;
    int count;
    float x[4];
    float z[4];
} IcoCameraWallFloor;

/* 1 when the segment a -> b crosses the wall's plane (from either side;
   the game's walls are one-sided, ClipPlane counts front to behind only) at
   a point inside the wall widened by r on every edge */
int ico_camera_wall_cross_wall(const float a[3], const float b[3], const IcoCameraWallQuad *w,
                               float r);
/* 1 when the segment a -> b crosses the floor's plane (down through it or
   up through its underside) at a point inside the polygon or within r of
   its rim (in x and z) */
int ico_camera_wall_cross_floor(const float a[3], const float b[3], const IcoCameraWallFloor *f,
                                float r);

/* The stage's collision for the rule: ICO_CAMERA_WALL_WALL or _FLOOR for
   the first element the world segment a -> b crosses (either way, widened
   by r), else ICO_CAMERA_WALL_NONE */
typedef int (*IcoCameraWallQuery)(const float a[3], const float b[3], float r, void *user);

/* The rule's state: the eye at the last flip */
typedef struct IcoCameraWall {
    int valid;
    float eye[3];
} IcoCameraWall;

/* One flip of the rule, with the eye at this flip (world, the inverse
   view's translation).  The query is not asked, and the result is NONE,
   without an eye at the flip before (a stage's first tick, after
   ico_camera_wall_forget), when a cut is already pending (cutPending: the
   game signalled one since the flip before, so the frame snaps anyway),
   while photo mode is open (photo: its free camera is not the game's),
   when the eye did not move, and when it moved further than
   RD_INTERP_CAMERA_MOVE (the presenter snaps on its own, RD_SNAP_CAMERA).
   Otherwise the result is the query's on the eye's path, the flip before's
   eye to this one.  The eye is stored in every case (a non-finite one
   forgets instead); *moved (when not NULL) gets the distance it moved. */
int ico_camera_wall_step(IcoCameraWall *s, const float eye[3], int cutPending, int photo,
                         IcoCameraWallQuery query, void *user, float *moved);

/* The live check (camera_wall_stage.c), from GsBase.c gsbHostCullFlip at
   every flip on the game thread: the rule over the stage's collision
   objects, cutPending as GsBase.c computes it (ico_video_cut_serial since
   the flip before), photo mode from photo_mode.h ico_photo_active, stage
   the game's stage_no for the log line.  The result is the rule's; GsBase.c
   marks the frame closing at the flip as a cut on a crossing.  Logs, with
   pictures blended, "camera: between-tick blend skipped at stage N: the eye
   moved M cm through a wall" (or "a floor"; rate-limited) */
int ico_camera_wall_check(const float *eyeNow, int cutPending, int stage);
/* No eye at the flip before any more (GsBase.c gsbHostCullForget: a
   stage's start, a reset between stages) */
void ico_camera_wall_forget(void);
/* Crossings found since the start, for the tests */
unsigned ico_camera_wall_count(void);

#ifdef __cplusplus
}
#endif

#endif /* ICO_PORT_GAME_CAMERA_WALL_H */
