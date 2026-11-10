/*
 * port/ui/settings_variants.h
 *
 * The game variants' rows (port/game/variants.h) on Options > Gameplay and
 * its Cutscenes and Yorda pages: which options they are, their value text,
 * the note a row shows on the running disc and whether a row shows there
 * (settings_variants.c).  settings.c builds the pages and steps the rows
 * (ico_variant_step); these are its helpers, kept in their own file so
 * settings.c stays under the repository's 256 KiB file cap.  The variants a
 * journey's difficulty holds are the New Game draft's rows
 * (settings_difficulty.h), routed there before these.
 */
#ifndef PORT_UI_SETTINGS_VARIANTS_H
#define PORT_UI_SETTINGS_VARIANTS_H

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when opt is one of the UI_OPT_VAR_* rows of a variant (UI_OPT_VAR_TOUGHNESS
   + IcoVariantId), not the Debug menu row */
int ui__var_opt(int opt);
/* the value text of variant row opt in the port's language */
const char *ui__variant_text(int opt);
/* the note string of variant row opt on the running disc: strId, or the
   disc's own note (the June 2001 demo's toughness, the 2001 discs' No hints
   and No cutscene look-around) */
int ui__variant_note(int opt, int strId);
/* whether variant row opt shows on the running disc: Light sword colour not
   on the North American disc nor the two 2001 previews (no light sword:
   ico_variant_offered), one log line the first time per disc; 1 otherwise */
int ui__variant_shown(int opt);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_SETTINGS_VARIANTS_H */
