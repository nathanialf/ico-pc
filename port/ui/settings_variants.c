/*
 * port/ui/settings_variants.c
 *
 * The game variants' rows (settings_variants.h): one row per IcoVariantId,
 * in its order (UI_OPT_VAR_TOUGHNESS + id): the two weapon rows on
 * Gameplay, the cutscene rows on Gameplay > Cutscenes, the two chain rows on
 * Gameplay > Yorda.  The ten a journey's difficulty holds (toughness, One
 * hit, Never dodge, aggression, No hints, the barrier, the three combat rows,
 * Peaceful shadows) are rows of the New Game draft on Customize's pages since
 * v0.11.3: settings_difficulty.c gives their text and notes from the draft;
 * the texts here read the journey in force (ico_variant_get).  Moved out of
 * settings.c unchanged but for the two weapon rows (Light sword colour,
 * Stick) when they were added in v0.10.5.
 */
#include "settings.h"
#include "settings_variants.h"

#include <stdio.h>

#include "strings.h"
#include "variants.h"
#include "../core/region.h" /* ico_region_cur, ICO_JUN26, ICO_NTSCAUG6JUN26 */

_Static_assert(UI_OPT_VAR_CHAIN_DAMPING - UI_OPT_VAR_TOUGHNESS + 1 == ICO_VARIANT_COUNT &&
                   UI_OPT_VAR_TOUGHNESS + ICO_VARIANT_AGGRESSION == UI_OPT_VAR_AGGRESSION &&
                   UI_OPT_VAR_TOUGHNESS + ICO_VARIANT_BARRIER_HITS == UI_OPT_VAR_BARRIER &&
                   UI_OPT_VAR_TOUGHNESS + ICO_VARIANT_NO_CUTSCENE_LOOK == UI_OPT_VAR_NO_LOOK &&
                   UI_OPT_VAR_TOUGHNESS + ICO_VARIANT_AUTO_GUARD == UI_OPT_VAR_AUTO_GUARD &&
                   UI_OPT_VAR_TOUGHNESS + ICO_VARIANT_ATTACK_TURN == UI_OPT_VAR_ATTACK_TURN &&
                   UI_OPT_VAR_TOUGHNESS + ICO_VARIANT_SHADOWS_WAIT_DOWN ==
                       UI_OPT_VAR_SHADOWS_WAIT_DOWN &&
                   UI_OPT_VAR_TOUGHNESS + ICO_VARIANT_SWORD_COLOUR == UI_OPT_VAR_SWORD_COLOUR &&
                   UI_OPT_VAR_TOUGHNESS + ICO_VARIANT_WEAPON == UI_OPT_VAR_WEAPON &&
                   UI_OPT_VAR_TOUGHNESS + ICO_VARIANT_PEACEFUL_SHADOWS == UI_OPT_VAR_PEACEFUL &&
                   UI_OPT_VAR_TOUGHNESS + ICO_VARIANT_CHAIN_SWING_SPEED == UI_OPT_VAR_CHAIN_SPEED &&
                   UI_OPT_VAR_TOUGHNESS + ICO_VARIANT_CHAIN_SWING_DAMPING ==
                       UI_OPT_VAR_CHAIN_DAMPING,
               "a Settings row for each variant, in variants.h's order");

/* the index of the game's own chain value, 4 (variants.c CHAIN_SHIPPED) */
#define CHAIN_OWN_INDEX 3

int ui__var_opt(int opt)
{
    return opt >= UI_OPT_VAR_TOUGHNESS && opt <= UI_OPT_VAR_CHAIN_DAMPING;
}

const char *ui__variant_text(int opt)
{
    static const int tough[ICO_TOUGHNESS_COUNT] = {UI_STR_VAL_ORIGINAL, UI_STR_VAL_VAR_AMERICAN,
                                                   UI_STR_VAL_VAR_NOT_TOUGHER,
                                                   UI_STR_VAL_VAR_THIRD_JOURNEY};
    static const int agg[ICO_AGGRESSION_COUNT] = {
        UI_STR_VAL_VAR_AGG_OWN, UI_STR_VAL_VAR_AGG_BOLD_PATIENT, UI_STR_VAL_VAR_AGG_BOLD_QUICK,
        UI_STR_VAL_VAR_AGG_WARY_PATIENT, UI_STR_VAL_VAR_AGG_WARY_QUICK};
    const IcoVariantId id = (IcoVariantId)(opt - UI_OPT_VAR_TOUGHNESS);
    const int v = ico_variant_get(id);

    switch (id) {
    case ICO_VARIANT_SHADE_TOUGHNESS:
        return ui_str(tough[v >= 0 && v < ICO_TOUGHNESS_COUNT ? v : 0]);
    case ICO_VARIANT_AGGRESSION:
        return ui_str(agg[v >= 0 && v < ICO_AGGRESSION_COUNT ? v : 0]);
    case ICO_VARIANT_BARRIER_HITS:
        return ui_str(v == 1 ? UI_STR_VAL_VAR_HITS_7 : UI_STR_VAL_VAR_HITS_5);
    case ICO_VARIANT_SWORD_COLOUR:
        /* Red is the Characters page's colour word */
        return ui_str(v == ICO_SWORD_RED ? UI_STR_COLOUR_RED : UI_STR_VAL_ORIGINAL);
    case ICO_VARIANT_WEAPON:
        return ui_str(v == ICO_WEAPON_NET ? UI_STR_VAL_VAR_INSECT_NET : UI_STR_VAL_ORIGINAL);
    case ICO_VARIANT_CHAIN_SWING_SPEED:
    case ICO_VARIANT_CHAIN_SWING_DAMPING:
        /* the game's number, the game's own (4) marked */
        return v == CHAIN_OWN_INDEX ? ui_str(UI_STR_VAL_VAR_CHAIN_ORIGINAL)
                                    : ico_variant_value_name(id, v);
    default:
        return ui_str(v ? UI_STR_ON : UI_STR_OFF);
    }
}

/* PC port: a game variants row's note on the running disc. The June 2001
   demo offers no American toughness (port/game/variants.h
   ico_variant_offered: its Shadows are its own), and the three 2001 discs
   have no hints (variants.c ico_hints_on) and no cutscene look-around (the
   game's debug_VariableInit leaves debug_hand_camera 0 on them), so those
   two rows say they change nothing there */
int ui__variant_note(int opt, int strId)
{
    if (opt == UI_OPT_VAR_TOUGHNESS && ICO_JUN26) {
        return UI_STR_OPT_VAR_TOUGHNESS_NOTE_DEMO;
    }
    /* Third journey explains itself when stepped to */
    if (opt == UI_OPT_VAR_TOUGHNESS &&
        ico_variant_get(ICO_VARIANT_SHADE_TOUGHNESS) == ICO_TOUGHNESS_THIRD_JOURNEY) {
        return UI_STR_OPT_VAR_TOUGHNESS_NOTE_THIRD;
    }
    if (opt == UI_OPT_VAR_NO_HINTS && ICO_NTSCAUG6JUN26) {
        return UI_STR_OPT_VAR_NO_HINTS_NOTE_2001;
    }
    if (opt == UI_OPT_VAR_NO_LOOK && ICO_NTSCAUG6JUN26) {
        return UI_STR_OPT_VAR_NO_LOOK_NOTE_2001;
    }
    return strId;
}

/* PC port: the North American disc and the two 2001 previews have no light
   sword (their weaponKind tables stop at row 7 or 6 and their InitWeaponGeo
   copies make neither kind 8 nor 9), so Light sword colour is hidden there */
int ui__variant_shown(int opt)
{
    static const IcoRegionInfo *logged;

    if (opt != UI_OPT_VAR_SWORD_COLOUR ||
        ico_variant_offered(ICO_VARIANT_SWORD_COLOUR, ICO_SWORD_RED)) {
        return 1;
    }
    if (logged != ico_region_cur()) {
        logged = ico_region_cur();
        fprintf(stderr, "settings: no Light sword colour row on %s (the game has no light sword)\n",
                ico_region_cur()->disc_id);
    }
    return 0;
}
