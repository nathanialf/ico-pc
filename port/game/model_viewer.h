/*
 * port/game/model_viewer.h
 *
 * Settings > Extras > Models: a viewer
 * for the game's character and object models, on the game's own motion
 * system.
 *
 * The table (model_viewer_table.c): each model is an object one stage of
 * the game builds when it loads (its host stage), found again by its object
 * kind, its model and, where the stage has several, its layout row.  Its
 * animations are the motion-kind ids [motFirst, motLast) the stage holds
 * (motionTable[id] set, or a static motion: what motionViewer.c's "NO
 * MOTION IN THIS STAGE" test checks), played with the motion-orient rows
 * [oriFrom, oriTo) as the development build's Motion Viewer plays them
 * (sugipon/src/motionViewer.c, objMenu).  An object with no motions has
 * motFirst == motLast.  The numbers were read from a survey of every stage
 * with data; nothing from the disc is
 * in the file but the numbers.
 *
 * The viewer (model_viewer.c, the game side, in ico_pc only): picking a
 * model loads its host stage with ico_mv_active set (options.h): the stage's
 * scripts do not start, every other object is parked, the renderer keeps
 * only the model's draws (rd.h rd_set_draw_filter) over a grey backdrop, and
 * a free camera orbits it.  A motion's sounds and vibration play; its stage
 * animations and particle effects are not entered (frameDependSequence.c
 * playEff).  Triangle goes back to the list, and from the
 * list to the title the way the pause menu's End Game does.
 */
#ifndef PORT_GAME_MODEL_VIEWER_H
#define PORT_GAME_MODEL_VIEWER_H

#include "../core/region.h" /* ico_region_cur: the tables' sizes */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MvModel {
    int nameStr;           /* its name, a UiStrId (port/ui/strings.h) */
    int pal_id;            /* the model: its modelData row on the European disc */
    int jpn_id;            /* and on the Japanese and Korean discs (tools/gen_mv_region.py) */
    int kind;              /* the object kind (objKindData row) */
    int label;             /* the layout row (objLayout), -1: the first of kind and model */
    int stage;             /* the host stage */
    int motFirst, motLast; /* the motion-kind block, [first, last) */
    int oriFrom, oriTo;    /* its motion-orient rows, [from, to) */
    /* a model no stage builds (the early show title art, Extras > Unused
       content): its pack member, read and registered by the viewer once its
       host stage is up (model_viewer_member.c), and the TIM2 member the
       model's material names; NULL for the stages' own models.  Such a row's
       pal_id and jpn_id are -1: its model id is the member's own number in
       the pack's directory (mv_model_id) */
    const char *member;
    const char *texture;
} MvModel;

extern const MvModel mv_models[];
extern const int mv_modelCount;
/* A model's modelData row and its host stage on the running disc
   (the column model_viewer_table.c mv_id_column names for ico_region:
   pal_id or jpn_id; the stage through
   ico_region_stage) */
int mv_model_id(const MvModel *m);
int mv_model_stage(const MvModel *m);
/* The member rows (MvModel.member): their pack (MV_MEMBER_PACK, the
   title's logo pack, which the game loads at every boot and whose E3 2001
   title models nothing in a retail disc draws).  A member row's id is
   PackEnt.id of its member in that pack on the running disc, asked once
   per disc (ico_df_find_member over the program's DATA.DF, the member
   checked to lie in that pack) and kept; -1 when the disc has no such
   member or cannot be read (not kept: asked again).  mv_model_shown(row):
   whether the model list offers the row (every stage row; a member row
   whose id is known); mv_member_first_row: the first member row shown, or
   -1; mv_member_lookups: the member look-ups so far (the tests' count). */
#define MV_MEMBER_PACK "STGLOG.DF"
int mv_model_shown(int row);
int mv_member_first_row(void);
int mv_member_lookups(void);
/* the table's first two rows, the characters Settings > Extras >
   Characters shows (appearance.h ico_appearance_character's numbers) */
#define MV_ROW_ICO 0
#define MV_ROW_YORDA 1

/* The motion-kind blocks of the PAL build's tables, [first, last) with
   their motion-orient rows: motionViewer.c's objMenu (the boy, the girl,
   the shadows, the queen, the bird) and the opening's guards and horses
   between the shadows' and the queen's.  Every table entry's range lies in
   one of them (the model_viewer test). */
typedef struct MvBlock {
    int first, last;
    int oriFrom, oriTo;
} MvBlock;

extern const MvBlock mv_blocks[];
extern const int mv_blockCount;
/* the motion-kind table's size (motionKind[1148]: 1145 motions, then
   KEEP, ALWAYS and NULL) and the motion-orient table's (motionOrient[2468])
   (the running disc's, port/core/region.h: the same on both discs) */
#define MV_MOTION_KINDS (ico_region_cur()->mv_motion_kinds)
/* motionFileManager.c's motionTable[1170], the largest disc's (the June 2001 demo's; its
   header declares it without a size): what the viewer's per-motion arrays are sized by; every
   region's MV_MOTION_KINDS is below it (the model_viewer_effects test) */
#define MV_MOTION_TABLE_SIZE 1170
#define MV_ORIENT_ROWS (ico_region_cur()->mv_orient_rows)

/* model_viewer_motion.c: whether the viewer can play motion `id` on the stage
   it loaded: a plain motion needs its own data loaded (or to be the stage's),
   a node-blend motion needs every partner of its blendMotionKind list loaded.
   *missing (may be NULL) gets the first partner that is not, or -1. */
int ico_mv_motion_playable(int id, int *missing);

/* --- the game side (model_viewer.c, ico_pc only) --------------------------- */

/* common/src/main.c, each Main tick after the pad is read */
void ico_mv_tick(void);
/* Settings > Extras > Models (port/ui/settings.c): the model list's layout,
   built on first use, or -1 */
int ico_mv_models_enter(void);
/* Extras > Unused content's art row (port/game/unused_live.c): the model
   list as ico_mv_models_enter opens it, the cursor on table row `row`; Back
   goes to the Unused content page's art row instead of Extras. -1 as
   ico_mv_models_enter, or for a row the list does not offer */
int ico_mv_models_enter_at(int row);
/* model_viewer_member.c: the member row m's model and texture registered
   from their pack members as the pack loader registers them
   (charFileManager.h ico_char_file_image), then its object built as a
   stage builds a layouted object of its kind (sceneManager.c
   CreateLayoutedGObj), on the host stage once it is up; the object, or
   NULL with a line saying why.  The object found already (a second call on
   the same stage) is returned as it is. */
struct GObj;
struct GObj *mv_member_load(const MvModel *m);
/* The model list's layout while it is opened from the title (the viewer
   off), or -1 (port/game/title_logo.c hides the logo under it) */
int ico_mv_title_list_layout(void);
/* Settings > Extras > Characters from the title runs in the viewer
   (settings.h UiCharactersHost, registered at the first tick): enter loads
   Ico's model (0, or -1 unless the viewer is off) and the Characters page
   becomes the panel beside it; shown is the character on screen (0 Ico,
   1 Yorda), -1 while a model loads or the viewer leaves, -2 outside
   Characters; switch loads the other one; leave goes back to the title,
   where Settings opens again on Extras (ui_settings_reopen_page) once the
   title's menu is up (at most 10 s). */
int ico_mv_characters_enter(void);
int ico_mv_characters_shown(void);
void ico_mv_characters_switch(int character);
void ico_mv_characters_leave(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_GAME_MODEL_VIEWER_H */
