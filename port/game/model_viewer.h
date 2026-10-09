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
 * only the model's draws (rd.h rd_SetDrawFilter) over a grey backdrop, and
 * a free camera orbits it.  Triangle goes back to the list, and from the
 * list to the title the way the pause menu's End Game does.
 */
#ifndef PORT_GAME_MODEL_VIEWER_H
#define PORT_GAME_MODEL_VIEWER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MvModel {
    int nameStr;           /* its name, a UiStrId (port/ui/strings.h) */
    int charId;            /* the model (modelData / charFiles row) */
    int kind;              /* the object kind (objKindData row) */
    int label;             /* the layout row (objLayout), -1: the first of kind and model */
    int stage;             /* the host stage */
    int motFirst, motLast; /* the motion-kind block, [first, last) */
    int oriFrom, oriTo;    /* its motion-orient rows, [from, to) */
} MvModel;

extern const MvModel mv_models[];
extern const int mv_modelCount;
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
   KEEP, ALWAYS and NULL) and the motion-orient table's (motionOrient[2468]) */
#define MV_MOTION_KINDS 1145
#define MV_ORIENT_ROWS 2468

/* --- the game side (model_viewer.c, ico_pc only) --------------------------- */

/* common/src/main.c, each Main tick after the pad is read */
void ico_mv_tick(void);
/* Settings > Extras > Models (port/ui/settings.c): the model list's layout,
   built on first use, or -1 */
int ico_mv_models_enter(void);
/* The model list's layout while it is opened from the title (the viewer
   off), or -1 (port/game/title_logo.c hides the logo under it) */
int ico_mv_title_list_layout(void);
/* Settings > Extras > Characters from the title runs in the viewer
   (settings.h UiCharactersHost, registered at the first tick): enter loads
   Ico's model (0, or -1 unless the viewer is off) and the Characters page
   becomes the panel beside it; shown is the character on screen (0 Ico,
   1 Yorda), -1 while a model loads or the viewer leaves, -2 outside
   Characters; switch loads the other one; leave goes back to the title,
   where Settings opens again on Extras (ui_SettingsReopenPage) once the
   title's menu is up (at most 10 s). */
int ico_mv_characters_enter(void);
int ico_mv_characters_shown(void);
void ico_mv_characters_switch(int character);
void ico_mv_characters_leave(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_GAME_MODEL_VIEWER_H */
