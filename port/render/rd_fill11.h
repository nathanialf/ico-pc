/* rd_fill11.h: the crack fill and list 11 in one pass on SCENE, the same
 * bytes (port/render/rd_fill11.c; docs/port/RENDER_POST.md, "The crack fill
 * in list 11's pass").
 *
 * Where nothing reads or writes SCENE's colour, its depth or its snapshot
 * between the place the crack fill is due (rd_crack_fill.c rd__crack_fill_place:
 * a pass of its own there) and list 11's first screen draw on SCENE, the
 * fill's snapshot is taken where it is due (by the swap where allowed, as
 * without this) and its draw is held to that draw, which then begins one
 * pass on SCENE's colour with a scratch depth of SCENE's format and size
 * attached (loaded and stored DONT_CARE: rd_fill11.c's scratch, WORK_BUFFERS.md)
 * instead of SCENE's own depth: the fill first (its pipeline's depth twin,
 * rd__fill11_twin: Z ALWAYS, no Z write), then list 11's draws with their own
 * pipelines, each one a draw that neither tests nor writes the depth (Z test
 * off or ALWAYS, no Z write, no DATE, no PRIM.AA1, no shadow window on
 * SCENE's depth).  So the fill's own pass (SCENE's colour stored) and list
 * 11's colour load go: about 19.5 MB a picture at 2366 x 1080.  Anything else
 * draws the held fill in a pass of its own before it (the same bytes as at
 * its place: nothing between touched SCENE), and a list 11 whose later draws
 * use SCENE's depth, or whose pass would hold a part of the picture only
 * (rd_pass_area.c), keeps today's passes.
 *
 * Off by default in this release (RD_FILL11_DEFAULT 0 in rd_fill11.c: the
 * PowerVR phone's title flickered black with it on, rc14; cause not found).
 * With the default on it would be on where the start-up self-test passed (rd__fill11_selftest: the fill's
 * depth twins draw a known source back unchanged) and the device class is
 * the PowerVR phone's or a desktop one (rd__profile_tile_class 1 or 2, not a
 * tile-based phone GPU: RhiLimits.tilePhone); off on the Arm and Qualcomm
 * phone classes and Apple's in this release (issues 123, 179, 211).
 * ICO_RD_FILL11=0|1 (the tests only, docs/port/TESTING.md; no config key)
 * keeps it off or tries it on any class (the self-test still decides);
 * rd__fill11_force is the tests' switch (-1 the environment's or the
 * class's). */
#ifndef PORT_RENDER_RD_FILL11_H
#define PORT_RENDER_RD_FILL11_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rd_internal.h"

struct Replay;
struct DrawSetup;

/* why a fill due before list 11 keeps its own pass where it is due (the
 * take refused; counted, the 10 s line and --stats) */
enum {
    RD_FILL11_NOT_NEXT,   /* the command it is due before reads or writes SCENE */
    RD_FILL11_NOT_LIST,   /* due after list 11 (the reduction) */
    RD_FILL11_NOT_MSAA,   /* an MSAA epoch wanted */
    RD_FILL11_NOT_BLEND,  /* the exact blend or GS state as shader data */
    RD_FILL11_NOT_SPLIT,  /* the upscaler's split (the display layer reads SCENE) */
    RD_FILL11_NOT_WINDOW, /* a pass window (the tool's --passes) */
    RD_FILL11_NOT_DEFER,  /* a glow paste deferred (rd_late_pass.c) */
    RD_FILL11_NOT_MEMORY, /* no memory for the scratch depth */
    RD_FILL11_NOT_PIPE,   /* the fill's depth twin missing */
    RD_FILL11_NOT_COUNT
};

/* why a held fill was drawn in a pass of its own after all */
enum {
    RD_FILL11_WHY_CMD,    /* a command that reads or writes SCENE */
    RD_FILL11_WHY_DEPTH,  /* SCENE's depth bound, written or DATE */
    RD_FILL11_WHY_BORDER, /* SCENE's border repeat (rd_blur_border.c) */
    RD_FILL11_WHY_AREA,   /* list 11's pass would hold a part of the picture only */
    RD_FILL11_WHY_LATER,  /* a later command of list 11 uses SCENE's depth or is not known */
    RD_FILL11_WHY_DRAW,   /* list 11's first draw itself takes DATE or the stencil */
    RD_FILL11_WHY_SNAP,   /* SCENE's snapshot taken or swapped again */
    RD_FILL11_WHY_END,    /* the walk's end */
    RD_FILL11_WHY_COUNT
};

/* the switch.  rd__fill11_may: the class or ICO_RD_FILL11=1 lets it be
 * tried (the start-up set's twins, the self-test); rd__fill11_on: in force
 * (may, not ICO_RD_FILL11=0, the self-test passed); rd__fill11_test: 0 not
 * run, 1 passed, 2 failed */
bool rd__fill11_may(void);
bool rd__fill11_on(void);
int rd__fill11_test(void);
void rd__fill11_force(int on);
/* ICO_RD_FILL11_POISON=1 (the tests only): what this code leaves undefined is
 * made garbage on lavapipe, which keeps a DONT_CARE load's old bytes where a
 * tile-based GPU does not: the merged pass's and the held fill's own pass's
 * colour loaded DONT_CARE are cleared to magenta (every texel must be
 * written by the fill), the scratch depth and stencil cleared to 0.5 and
 * 0x5A (no draw may depend on them) */
bool rd__fill11_poison(void);

/* rd_crack_fill.c rd__crack_fill_init (rd_init) and rd__crack_fill_shutdown
 * (rd_shutdown: the scratch depth destroyed, the self-test forgotten: a
 * later renderer runs its own) */
void rd__fill11_init(void);
void rd__fill11_shutdown(void);

/* The start-up self-test (rd_crack_fill.c rd__crack_swap_selftest's end,
 * once a renderer from rd_fog_selftest; rd_crack_test calls it): a 256 x 2
 * source of every level in every channel drawn through each of the fill's
 * depth twins in a pass with a scratch D32F_S8 depth attached (the swap's
 * copy path, and the discard path over a target loaded with another
 * pattern), read back and compared texel for texel; logs the state line.
 * Run where rd__fill11_may; true when it passed or did not run. */
bool rd__fill11_selftest(void);

/* The fill's key with a depth attachment of D32F_S8, Z ALWAYS and no Z
 * write (the same shader, blend and colour mask) */
RdPipeKeyInt rd__fill11_twin(const RdPipeKeyInt *k);
/* rd_pipeline.c rd__enumerate_reachable_fog: the twins of the four fill
 * keys (FILL, FILL1, each with RGB alone) where rd__fill11_may */
uint32_t rd__fill11_enumerate(RdPipeKeyInt *out, uint32_t max, uint32_t n);

/* The walk.  rd__fill11_take (rd_crack_fill.c rd__crack_fill_late, a fill
 * drawn where it is due before command c of list): whether its draw may be
 * held (the switch, the replay's state, the scratch, the class of c);
 * counted when not (RD_FILL11_NOT_*).  rd__fill11_at (rd__crack_fill_at,
 * after the fill's own step, before every command): a held fill waits past
 * c, is drawn in a pass of its own before it, or lands at c (list 11's first
 * screen draw on SCENE, looked ahead to the pass's end); the merged pass is
 * ended before a command it cannot hold.  rd__fill11_draw (rd_replay.c
 * prepareDrawEx's hook, after the texture is resolved): at the landing
 * draw the pass begun and the fill drawn in it; in the merged pass a draw
 * on SCENE with its depth that uses the depth ends it first.
 * rd__fill11_border (rd_blur_border.c sceneRepeat, before its copies): a
 * held fill drawn before the repeat. */
bool rd__fill11_take(const struct Replay *r, const RdFrame *f, const RdCmd *c, int list,
                     const RdTargetRec *scene);
void rd__fill11_at(struct Replay *r, const RdFrame *f, const RdCmd *c, int list);
void rd__fill11_draw(struct Replay *r, struct DrawSetup *ds);
void rd__fill11_border(struct Replay *r);

/* rd_crack_fill.c: the held fill (taken by rd__crack_fill_late): pending in
 * this replay; drawn into the open pass (the merged one: its depth twin) or
 * in a pass of its own (why RD_FILL11_WHY_*), counted as the fill's own
 * counters count a fill drawn there */
bool rd__crack_fill11_pending(void);
bool rd__crack_fill11_copy(void); /* the swap took its snapshot: it writes every texel */
void rd__crack_fill11_land(struct Replay *r);
void rd__crack_fill11_own(struct Replay *r, int why);

/* the counters: rd__fill11_flushed (a held fill drawn in its own pass),
 * rd__fill11_refused (a fill not held); rd__fill11_block the 10 s line
 * (rd_crack_fill.c rd__crack_fill_block); rd__fill11_stats the replay
 * tool's --stats line after "stats: " (this replay's); rd__fill11_replay the
 * same counts for the tests */
void rd__fill11_flushed(int why);
void rd__fill11_refused(int why);
void rd__fill11_block(void);
void rd__fill11_stats(char *out, size_t size);

typedef struct RdFill11Counts {
    uint32_t landed;  /* fills drawn in list 11's pass */
    uint32_t flushed; /* held fills drawn in a pass of their own */
    uint32_t refused; /* fills not held */
    uint32_t split;   /* merged passes a draw that uses SCENE's depth ended */
    uint32_t why;     /* the last flush's RD_FILL11_WHY_* */
} RdFill11Counts;

void rd__fill11_replay(RdFill11Counts *out);

/* rd_glow_light.c: the light plan leaves command c undrawn (skipped, or its
 * target's colour a lazy clear: no GPU work at c) */
bool rd__glow_light_undrawn(const RdCmd *c);

#endif
