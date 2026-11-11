/* rd_internal_interp.h: the interpolation's declarations (rd_interp.c),
 * moved verbatim out of rd_internal.h, which includes this file where they
 * stood (rd_internal.h is at the 256 KiB cap, tools/check_no_rom.sh).
 * Include rd_internal.h, never this file alone. */
/* --------------------------------------------------------- interpolation
 * rd_interp.c.  rd__interp_frame builds, into a frame it owns, the current
 * frame cur with every keyed draw's data blended from its match in prev by
 * alpha (0 = prev's data, 1 = cur's), and the feedback passes held at the
 * tick: a frame that writes FEED128 copies it into FEED_HELD at its head
 * when firstOfTick, and back from FEED_HELD otherwise, and a
 * frame that reads DISPLAY (the motion blur's old frame) does the same with
 * DISPLAY and DISPLAY_HELD, so every present of a tick starts from the same
 * FEED128 and DISPLAY and draws the recorded FIX.  prev NULL, or a
 * frame-level snap (rd__interp_snap), copies cur's data.  The result is
 * valid until the next call; it owns no temporary targets (cur's are
 * used). */
enum {
    RD_SNAP_NONE = 0, /* interpolated */
    RD_SNAP_NO_PREV,  /* no previous frame, or it is not closed */
    RD_SNAP_GAP,      /* frame numbers not consecutive (a frame was discarded) */
    RD_SNAP_KEEP,     /* either frame is a keep (fbKeep) frame */
    RD_SNAP_CUT,      /* rd_camera_cut during cur (RdCamera.cut) */
    RD_SNAP_CAMERA,   /* the camera turned or moved further than a cut threshold */
    RD_SNAP_FADE,     /* either frame fully faded (the fade edge) */
    RD_SNAP_HISTORY,  /* targets recreated after prev (display options changed) */
    RD_SNAP_SIZE,     /* the scene size differs */
    RD_SNAP_WALL,     /* the eye's path from prev to cur crossed the stage
                       * (rd_camera_cut_wall, RdCamera.cut RD_CAMERA_CUT_WALL) */
    RD_SNAP_COUNT
};

/* What made a matched keyed draw snap as mismatched */
enum {
    RD_MISMATCH_SIZE = 0, /* payload too short for its header, vertex or primitive count */
    RD_MISMATCH_MESH,     /* the mesh ids are of different layouts (sameMesh) */
    RD_MISMATCH_STATE,    /* program, code or clip mode (b[0..2]) */
    RD_MISMATCH_HEADER,   /* batch range, bones, stream, layout (RdVuPayload) */
    RD_MISMATCH_TOPOLOGY, /* a shadow volume's triangle counts */
    RD_MISMATCH_COUNT
};

/* Why a keyed draw of cur stayed unmatched (RdInterpStats.unmatchedWhy) */
enum {
    RD_UNMATCHED_ABSENT = 0, /* the previous frame has no draw of its key, type and list */
    RD_UNMATCHED_FEWER,      /* the previous frame drew the key fewer times, every one paired */
    RD_UNMATCHED_UNPLACED,   /* left by the pairing by place while a previous draw stayed free */
    RD_UNMATCHED_PAYLOAD,    /* its payload is out of the frame's range */
    RD_UNMATCHED_COUNT
};

/* Why a frame that blended (no snap, alpha below 1) had no blended camera
 * (rd_interp.c camSetup; RdInterpStats.camOffWhy) */
enum {
    RD_CAM_OFF_NONE = 0,    /* the blended camera on, or the frame not blended (a snap) */
    RD_CAM_OFF_ALPHA_RANGE, /* alpha below 0 or NaN (rd__interp_frame passes neither) */
    RD_CAM_OFF_ALPHA1,      /* alpha 1: nothing blends, the tick's own picture */
    RD_CAM_OFF_NO_CAMERA,   /* a frame without a camera (RdFrame.hasCamera) */
    RD_CAM_OFF_LEGACY,      /* ICO_RD_S2_LEGACY=1 (rd__s2_legacy) */
    RD_CAM_OFF_FAILED,      /* a view not invertible, or the blend refused (not affine, a mirror) */
    RD_CAM_OFF_COUNT
};

/* Why a turn over RD_INTERP_TURN_SNAP kept the tick's pose (rd_interp.c
 * rotation continuity; RdInterpStats.turnWhy, the 10 s summary) */
enum {
    RD_TURN_WHY_NONE = 0, /* no memory of the draw: never blended under the snap, or the
                             table full when it was */
    RD_TURN_WHY_OLD,      /* its last memory ended more than RD_INTERP_TURN_GAP ticks before */
    RD_TURN_WHY_POSE,     /* a memory within the gap whose turn does not carry to the pose */
    RD_TURN_WHY_FAR,      /* a memory, the turn further than RD_INTERP_TURN_KEEP from it */
    RD_TURN_WHY_LONG,     /* a memory the long way continues, over RD_INTERP_TURN_LONG */
    RD_TURN_WHY_COUNT
};

typedef struct RdInterpStats {
    uint32_t snap;     /* RD_SNAP_* of the frame */
    uint32_t keyed;    /* keyed draws in cur */
    uint32_t lerped;   /* blended */
    uint32_t missing;  /* no match in prev (snapped) */
    uint32_t mismatch; /* count, topology or mesh differs (snapped) */
    uint32_t jump;     /* moved further than the teleport threshold (snapped) */
    uint32_t morph;    /* mesh draws given a kept or blended vertex stream */
    /* why the mismatched draws snapped (RD_MISMATCH_*), and the mesh
     * draws whose model matrices or bones blended as rotations */
    uint32_t why[RD_MISMATCH_COUNT];
    uint32_t rotated;
    uint32_t turned; /* of them, turning more than 10 degrees in the tick */
    float maxTurn;   /* the largest turn of a model or bone in the tick, degrees */
    /* of them, those with a model or bone turned the long way to continue
     * its turn of the tick before (rotation continuity), and those with one
     * kept at the tick's pose for a turn over RD_INTERP_TURN_SNAP that
     * continued none */
    uint32_t longWay;
    uint32_t turnSnap;
    /* the snapped ones by why (RD_TURN_WHY_*, the draw's first snapped
     * model or bone), and the turns not remembered for want of room */
    uint32_t turnWhy[RD_TURN_WHY_COUNT];
    uint32_t turnFull;
    uint32_t shifted; /* shadow volumes blended with cur's triangles moved by the volume's
                         shift: a topology change, or a prism in one tick only */
    /* VU draws drawn through the rigidly blended camera (matched and
     * not), and of the not-blended ones (unmatched, mismatched, jumped,
     * unkeyed) those re-based */
    uint32_t rebased;
    uint32_t rebasedCur;
    /* lit draws whose previous tick's lights were put in the slots of the
     * current tick's lights they pair with by direction; draws of a key
     * drawn several times paired by their place in the world instead of
     * their order (unpaired new instances included); grids sampling a
     * target whose screen-space STs blended */
    uint32_t lightPaired;
    uint32_t placed;
    uint32_t gridSt;
    /* grids whose STs pool.c's formula (rd.h rd_grid_screen_st) computed
     * through the blended camera instead */
    uint32_t gridStCamera;
    /* the unmatched draws (missing) by RD_UNMATCHED_*; draws of a key drawn
     * several times paired with a free draw of another place (not the
     * nearest of each other) and so drawn as the tick's (counted in jump) */
    uint32_t unmatchedWhy[RD_UNMATCHED_COUNT];
    uint32_t apart;
    /* VU draws of the previous frame alone, their object and part not drawn
     * in the current frame's list and outside its camera's picture: drawn
     * through the blended camera (kept), or not drawn because they could
     * not be placed, their state reached or their block re-based (held) */
    uint32_t prevKept;
    uint32_t prevHeld;
    /* draws projected on the CPU by their tick's camera (RD_SCREEN_FRAME_CAMERA,
     * RD_SHADOW_FRAME_CAMERA) taken through the world onto the blended camera;
     * of them, the screen draws whose S / Q and T / Q moved with their
     * vertices (RD_SCREEN_ST_SHIFT: the ripples) */
    uint32_t reprojected;
    uint32_t stShifted;
    /* The still draws (rd_interp.c stillDraw): blended parts of the world
     * whose object did not move between the ticks while the camera did,
     * each drawn from the tick's block through the blended camera
     * (camCurDraw's block); stillReRounded, of them those whose element-wise
     * blend of camera and model rows (qw 4..27) was not that block in bits
     * (what the blend would have drawn, before the turn).  curWorld: the
     * parts of the world (normal programs, placed through the view) drawn as
     * the tick's through the blended camera (unmatched, snapped or unkeyed)
     * in a frame that blended others.  tickCams: the VU draws marked
     * RD_VU_TICK_CAMERAS */
    uint32_t stillDraws;
    uint32_t stillReRounded;
    uint32_t curWorld;
    uint32_t tickCams;
    /* The mixed picture: in a frame that blended without a blended camera
     * (camOffWhy, RD_CAM_OFF_*), the parts of the world (normal programs'
     * mesh draws placed through the view) that were matched and so drawn
     * from both ticks' blocks blended element by element (at alpha 0 the
     * tick before's block as recorded), worldPrevRaw, and those drawn as
     * the tick's alone (unmatched, snapped or unkeyed: camCurDraw with no
     * camera to re-base them on, the tick's block as recorded),
     * worldCurRaw.  With both above 0 the two kinds stand a tick of camera
     * motion apart in one picture; mixed is then the smaller of the two
     * (the pieces in the minority), 0 otherwise.  Whether such pieces
     * touch is not computed: mixed counts pieces that may open a wedge,
     * not wedges. */
    uint32_t worldPrevRaw;
    uint32_t worldCurRaw;
    uint32_t mixed;
    uint32_t camOffWhy;
    /* VU draws blended whose two payloads differ in size: a card section
     * (rd_mesh.h RdVuCards) at one tick only, the clip mode having changed
     * between REGION and SCISSOR (rd_interp.c blendVu) */
    uint32_t sizeTailBlended;
    /* screen prims of one tick each, faded otherwise (rd_interp.c
     * alphaFades), paired because cur's is prev's in every byte but the
     * key: the same list, command, vertices and state (a layout change
     * that redraws the same full-frame bars under the new layout's keys,
     * issue 139); cur's is drawn whole and prev's not, so the picture
     * under them never shows through a cross-fade (rd_interp.c pairSame) */
    uint32_t screenPaired;
} RdInterpStats;

/* Rotation-aware blending of an affine 4 x 4 (column-major, w row 0 0 0
 * 1): the 3 x 3 polar-decomposed into a rotation (slerped) and a stretch
 * (lerped); the image of pivot (x, y, z; NULL: the origin) lerped, so the
 * blended matrix turns about it.  False (o untouched) when either matrix
 * is not affine, is singular, or the two have opposite handedness.
 * rd_camera_blend.h rdcb_blend_affine, the turn kept for rd_interp.c. */
bool rd__blend_affine(const double *p, const double *c, double t, const double *pivot, double *o);
/* ICO_RD_S2_LEGACY=1 in the environment (a developer A/B switch, read
 * once) turns off the finer blending: rotation-aware blends, the blended
 * camera, the shadow prisms' alignment and the looser state match (the
 * window also takes alpha from the measured present time).  The VU
 * positions are on the 12.4 grid whatever it says. */
bool rd__s2_legacy(void);
/* ICO_RD_VU_OFFGRID=1 in the environment (a developer switch,
 * read once) brings back unquantised VU X and Y (FrameCB g_z.w) with
 * the Enhanced preset on a scaled target: smoother slow motion, but the
 * seams between meshes and against the GIF path open. */
bool rd__vu_off_grid(void);

int rd__interp_snap(const RdFrame *prev, const RdFrame *cur);
/* The CPU-projected draws' re-projection onto the blended camera (on by
 * default; rd_interp.c): off, they blend in screen space (the
 * tests' and the replay tool's before/after switch). */
void rd__interp_set_reproject(bool on);
/* The present clock's alpha before its clamp to [0, 0.999]
 * (rd_present_clock_alpha's (clock - tickAtMs) / tickMs), as its last call
 * computed it; NAN when that call returned before computing one or
 * rd_present has taken it since (each rd_present takes the value the clock
 * noted for it, so a present the clock did not time, the vsync lock's,
 * finds none).  For the alpha histogram and the tests. */
double rd__present_clock_raw(void);
/* The same value taken, NAN after (rd_thread.c rd_present_post hands it to
 * the render thread's rd_present), gap (or NULL) the time since the present
 * before and the tick, 0 after or untimed.  rd_present_clock.c, as are
 * Smooth's k (the clock's tick over its step, else counted) and the step
 * and resets of Smooth's block line. */
double rd__present_clock_take(double gap[2]);
uint32_t rd__present_clock_k(double raw, uint32_t counted);
double rd__present_clock_step_ms(void);
uint32_t rd__present_clock_resets_take(void);
/* F12's blended dump is armed (rd_dump_blended_arm): the present that
 * takes it waits for its end (rd_thread.c) */
bool rd__blended_dump_armed(void);

/* The bins of the alpha histogram (rd_interp.c, the 10 s summary): alpha 0
 * split by the clock's raw value (below 0: clamped from below; anything
 * else, none included: exactly 0), the tenths of 0 < alpha < 0.999, the
 * clock's top 0.999 and 1 (the vsync lock's last slot).  rd__alpha_bin
 * the bin of a present at alpha (already clamped to [0, 1]) whose clock
 * value was raw (NAN: none) */
enum {
    RD_ALPHA_BIN_CLAMPED = 0,
    RD_ALPHA_BIN_ZERO,
    RD_ALPHA_BIN_TENTH0,
    RD_ALPHA_BIN_TOP = RD_ALPHA_BIN_TENTH0 + 10,
    RD_ALPHA_BIN_ONE,
    RD_ALPHA_BIN_COUNT
};

int rd__alpha_bin(float alpha, double raw);
/* One vertex's ST by the RdGridSt formula kind (rd.h; pool.c's statements in
 * double precision): m the grid's model to GS screen (VU qw 16..19), eye the
 * camera's eye in the world (REFLECT), sx and sy the command's f[0] and
 * f[1], pos the vertex and h its wave height (its ST's w).  False (out
 * untouched) for kind NONE or an unknown one, a vertex at w 0 or a result
 * that is not finite. */
bool rd__grid_screen_st(uint8_t kind, const float (*m)[4], const double eye[3], float sx, float sy,
                        const float pos[4], float h, float out[2]);
/* rd_last_present_info's record for rd_end_frame's present of f
 * (interpolation off; rd_present_info.c) */
void rd__note_frame_present(const RdFrame *f);
/* rd_last_present_info's record (rd_present_info.c): a present of cur at
 * alpha (rd_present's, after its clamp), firstOfTick whether it is the
 * tick's first; and the record forgotten (rd_present's reset) */
void rd__present_info_note(const RdFrame *cur, uint32_t firstOfTick, float alpha);
void rd__present_info_reset(void);
const RdFrame *rd__interp_frame(const RdFrame *prev, const RdFrame *cur, float alpha,
                                int firstOfTick, RdInterpStats *stats);
void rd__interp_shutdown(void);

/* Still re-present (rd_interp.c): a between-tick picture that would be the
 * bytes of the tick's last rendered one is shown again (the empty keep
 * frame's replay, no passes) instead of rendered.  rd__interp_frame hashes
 * the frame it builds (rd__interp_frame_hash: ok false when the camera moved,
 * Smooth is on or the switch is off); rd__present_blended replays rf, or
 * shows the picture again when rd__reshow_why says RD_RESHOW_SHOWN.
 * [dev] still_represent = false (ICO_RD_STILL_REPRESENT=0) renders every
 * present. */
enum {
    RD_RESHOW_SHOWN = 0, /* the same bytes: shown again */
    RD_RESHOW_OFF,       /* the switch is off, or Smooth's trail, a video, no device */
    RD_RESHOW_MOVING,    /* no hash: the camera moved */
    RD_RESHOW_FIRST,     /* a tick's first present */
    RD_RESHOW_STALE,     /* no picture of this tick before, or another replay since */
    RD_RESHOW_DIFF,      /* the frame's hash differs from the last rendered one's */
    RD_RESHOW_STATE,     /* settings, targets or textures changed since */
    RD_RESHOW_COUNT
};

typedef struct RdReshowIn {
    bool enabled, smooth, video, hashOk, first, haveRef, stateSame;
    uint32_t number, refNumber;
    uint64_t hash, refHash, replay, refReplay;
} RdReshowIn;

int rd__reshow_why(const RdReshowIn *in);
uint64_t rd__interp_frame_hash(bool *ok);
bool rd__present_blended(const RdFrame *rf, int first, bool present);
void rd__reshow_block(void);        /* the 10 s summary's line (rd_interp.c presentLog) */
uint64_t rd__reshow_count(int why); /* presents counted so far in this block, per RD_RESHOW_* */
void rd__overlay_reshow_text(const RdFrame *f); /* rd_present.c: lay the deferred text out from f */
void rd__perf_reshown(void); /* rd_perf.c: the next replay is a re-shown present */

/* The glow held between ticks (rd_glow_hold.c): a tick's first present
 * copies the glow its chain drew (WORK3) into WORK3_HELD, and its later
 * presents skip the chain (the mask into WORK2, the reduction into WORK0,
 * the blur and the eye blur) and paste the copy.  rd__glow_hold edits f
 * (rd__interp_frame's frame, after the hash) and says what it did, one of
 * RD_GLOW_*; [dev] glow_redraw = true (ICO_RD_GLOW_REDRAW=1) and Glow between
 * frames: Smooth (RdSettings.glowSmooth) draw the chain at every present; an
 * explicit ICO_RD_GLOW_REDRAW wins over the row. */
enum {
    RD_GLOW_HELD = 0, /* a later present: the chain skipped, the tick's glow pasted */
    RD_GLOW_FIRST,    /* a tick's first present: drawn, and kept */
    RD_GLOW_NONE,     /* the frame draws no glow */
    RD_GLOW_DEV,      /* a later present, the switch on: drawn */
    RD_GLOW_SMOOTH,   /* a later present, Glow between frames: Smooth: drawn */
    RD_GLOW_NO_COPY,  /* a later present without this tick's copy: drawn */
    RD_GLOW_READ,     /* a later present whose other draws read the chain's buffers: drawn */
    RD_GLOW_COUNT
};

int rd__glow_hold(RdFrame *f, int firstOfTick);
const char *rd__glow_hold_text(int why); /* "glow held", "glow redrawn (...)", ... */
int rd__glow_hold_last(void);            /* the last rd__glow_hold's RD_GLOW_* */
void rd__glow_hold_block(void);          /* the 10 s summary's line (rd_interp.c presentLog) */
uint64_t rd__glow_hold_count(int why);   /* presents counted in this block, per RD_GLOW_* */
void rd__perf_glow_held(void);           /* rd_perf.c: the next replay pastes the held glow */
/* rd_interp.c, for rd_glow_hold.c: whether c under st writes or samples the
 * target id; an RDC_COPY of all of from into to (sizeOf's size) inserted at
 * index at of list l of the frame rd__interp_frame builds */
int rd__interp_writes(const RdCmd *c, const RdStateBlock *st, uint32_t id);
int rd__interp_reads(const RdCmd *c, const RdStateBlock *st, uint32_t id);
bool rd__interp_copy_at(int l, uint32_t at, uint32_t from, uint32_t to, uint32_t sizeOf);

/* rd_present.c's capture (rd.h rd_capture_presented): whether one is armed;
 * captureRecord (rd_present.c) copies the output into the capture texture
 * (from rd__present_record, before the overlay); rd__capture_finish reads it
 * back and writes the PNG (after the submit). */
bool rd__capture_armed(void);
void rd__capture_finish(void);
/* A capture this present's list copied, or a state picture it dropped: its
 * finish (rd__capture_finish) has work, so the submit waits for it
 * (rd_submit.c) */
bool rd__capture_pending(void);
/* The thresholds (world units are the game's centimetres) */
#define RD_INTERP_JUMP_WORLD 300.0f  /* an object's or bone's origin, per tick */
#define RD_INTERP_JUMP_SCREEN 256.0f /* GS pixels: screen prims, shadows, grids, particles */
/* RD_INTERP_CAMERA_MOVE and RD_INTERP_CAMERA_TURN, the camera's step and
 * turn per tick past which the pictures snap: rd_camera_blend.h, which the
 * game's cull shares */
/* A model matrix or bone turning further than this in a tick (3600
 * degrees a second at 30 Hz) is a flip, not a motion: it keeps the tick's,
 * unless it continues the turn the same draw (or bone) made in the tick
 * before (rd_interp.c rotation continuity): a turn within
 * RD_INTERP_TURN_KEEP of that one (the distance between the two turns'
 * rotation vectors) blends the way that continues it, the short way or the
 * long way (a wheel turning 190 degrees a tick turns on forward instead of
 * back 170), up to RD_INTERP_TURN_LONG degrees */
#define RD_INTERP_TURN_SNAP 120.0 /* degrees, per tick */
#define RD_INTERP_TURN_KEEP 60.0  /* degrees */
#define RD_INTERP_TURN_LONG 300.0 /* degrees, per tick */
/* A remembered turn also serves a turn that would snap when it ended up to
 * this many ticks before the pair's first (about a quarter second at 30
 * Hz): a pair not blended between (a dropped present, a cut, a fade, a
 * discarded frame) or a draw not drawn for a few ticks keeps its wheel
 * turning on, when the draw's pose is where the turn carried on that many
 * ticks puts it (within RD_INTERP_TURN_KEEP) */
#define RD_INTERP_TURN_GAP 8u /* ticks */
/* In the alignment of two ticks' shadow prisms, a prism left unmatched
 * costs as much as a pair whose top caps lie this far apart (rms, GS pixels)
 * once the volume's shift is taken out; a pair further apart than sqrt(2)
 * times this is cheaper as two unmatched prisms */
#define RD_INTERP_PRISM_GAP 32.0

/* fx_sprite_ps's DrawCB.mode[0] flags (FXF_* in port/shaders/fx_sprite.hlsl) */
enum {
    RD_FXF_TEXTURED = 1,
    RD_FXF_TFX_SHIFT = 1, /* bits 1..2: TEX0.TFX */
    RD_FXF_TCC = 8,
    RD_FXF_LINEAR = 16, /* TEX1.MMAG (the sprites' LOD is K = 0) */
    RD_FXF_CLAMP_S = 32,
    RD_FXF_CLAMP_T = 64,
    RD_FXF_ABE = 128,
    RD_FXF_PABE = 256,
    RD_FXF_FBA = 512,
    RD_FXF_DATE = 1024,
    RD_FXF_DATM = 2048,
    RD_FXF_DST = 4096, /* t2 holds the destination as it was before the sprite */
    /* above 1x: the resampling kernels, the steps in DrawCB.scale[3]
     * (rd_replay.c doBlurSpriteDraw); never set when every target involved
     * is at scale 1 */
    RD_FXF_ABOVE1X = 8192,
    /* fx_frame_ps (rd_fx_plan.c): only the alpha computed, RGB 0 by
     * construction (the glow's mask); the result premultiplied for the
     * fixed-function ONE, ONE blender (RD_BP_PREMUL_ADD) */
    RD_FXF_ALPHA_ONLY = 16384,
    RD_FXF_OUT_PREMUL = 32768,
    /* the reduction at scale 1: the fades fold's steps on its taps
     * (rd_fades_fold.c, fx_sprite.hlsl fx_fold4) */
    RD_FXF_FOLD = 65536,
    /* fx_frame_ps: the glow's reduction with SCENE as t1, WORK2's texels
     * computed as the mask left them (rd_glow_fold.c, fx_sprite.hlsl
     * fx_glow_texel) */
    RD_FXF_GLOW_MASK = 131072,
    /* with RD_FXF_GLOW_MASK: the texels the mask keeps read from t2 (WORK2
     * as the fill and the sun's fans left it), not the fill's colour */
    RD_FXF_GLOW_KEPT = 262144
};

/* The steps at which fx_sprite_ps leaves the GS's own filter above 1x, in
 * 1/32 t1 texel per bound texel (common.hlsli GS_RS_BOX_MIN32 and
 * GS_RS_BSPLINE_MAX32): the box above 2 texels, the B-spline (bilinear
 * only) below half a texel; the packed steps (DrawCB.scale[3]) hold up to
 * RD_FX_STEP_MAX each */
#define RD_FX_BOX_MIN32 64
#define RD_FX_BSPLINE_MAX32 16
#define RD_FX_STEP_MAX 4095
