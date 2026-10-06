/*
 * port/game/model_viewer_table.c
 *
 * The models of Extras > Models (model_viewer.h) and the motion-kind blocks
 * their animations come from.  Read from a survey of every stage with data
 * (1 to 63, 88, 91, 103 to 105; docs/port/EXTRAS.md, "The table"): each
 * stage was booted, and once it was up every object it had built was
 * listed with its kind, model, layout row, whether it was active, its
 * motion-orient rows and how many of the motions those rows reach the
 * stage held.  A model's host stage is one that builds it active at load,
 * holds the most of its block, and is not stage 88 or 91 (the ending's
 * stages).  Only the numbers are here; the names are the port's strings.
 */
#include "model_viewer.h"

#include "strings.h"

/* motionViewer.c's objMenu, and the opening's guards and horses (the D1_*
   motions between the shadows' block and the queen's) */
const MvBlock mv_blocks[] = {
    {0, 532, 0, 1283},        /* the boy */
    {532, 834, 1283, 2122},   /* the girl */
    {834, 983, 2122, 2407},   /* the shadows */
    {983, 1072, 2122, 2407},  /* the opening's guards and horses */
    {1072, 1134, 2407, 2421}, /* the queen */
    {1134, 1143, 2421, 2467}, /* the bird */
};
const int mv_blockCount = (int)(sizeof(mv_blocks) / sizeof(mv_blocks[0]));

/* name, model, kind, layout row (-1: the first of kind and model), host
   stage, motions [first, last), motion-orient rows [from, to).  Kinds: 1 the
   boy, 2 the girl, 4 a shadow, 6 a demo actor, 14 a weapon, 16 a stone
   couch, 19 a barrel (the bomb, the pot), 22 a floor lever, 32 the bird,
   44 a cage, 48 the queen in a scene */
const MvModel mv_models[] = {
    {UI_STR_MV_ICO, 0, 1, 54, 8, 0, 532, 0, 1283},
    {UI_STR_MV_YORDA, 4, 2, 148, 4, 532, 834, 1283, 2122},
    {UI_STR_MV_QUEEN, 71, 48, 590, 11, 1072, 1134, 2407, 2421},
    {UI_STR_MV_SHADOW, 20, 4, 2464, 46, 834, 983, 2122, 2407},
    {UI_STR_MV_SHADOW_WINGED, 31, 4, 341, 8, 834, 983, 2122, 2407},
    {UI_STR_MV_SHADOW_HORNED, 32, 4, 336, 8, 834, 983, 2122, 2407},
    {UI_STR_MV_SHADOW_HORNED_WINGED, 34, 4, 400, 9, 834, 983, 2122, 2407},
    {UI_STR_MV_SHADOW_BULL, 35, 4, 835, 14, 834, 983, 2122, 2407},
    {UI_STR_MV_SHADOW_BULL_WINGED, 36, 4, 342, 8, 834, 983, 2122, 2407},
    {UI_STR_MV_SHADOW_LARVA, 23, 4, 150, 4, 834, 983, 2122, 2407},
    {UI_STR_MV_SHADOW_BUTTERFLY, 24, 4, 335, 8, 834, 983, 2122, 2407},
    {UI_STR_MV_SHADOW_SLUG, 21, 4, 832, 14, 834, 983, 2122, 2407},
    {UI_STR_MV_SHADOW_BONES, 39, 4, 509, 10, 834, 983, 2122, 2407},
    {UI_STR_MV_BIRD, 1547, 32, 595, 11, 1134, 1143, 2421, 2467},
    {UI_STR_MV_GUARD, 53, 6, 2336, 42, 983, 1005, 2122, 2407},
    {UI_STR_MV_CAGE, 937, 44, 1847, 31, 0, 0, 0, 0},
    {UI_STR_MV_BOMB, 99, 19, 324, 8, 0, 0, 0, 0},
    {UI_STR_MV_POT, 101, 19, 208, 5, 0, 0, 0, 0},
    {UI_STR_MV_BARREL, 92, 19, 280, 7, 0, 0, 0, 0},
    {UI_STR_MV_LEVER, 151, 22, 145, 4, 0, 0, 0, 0},
    {UI_STR_MV_SWORD, 77, 14, 483, 10, 0, 0, 0, 0},
    {UI_STR_MV_MAGIC_SWORD, 79, 14, 2098, 35, 0, 0, 0, 0},
    {UI_STR_MV_COUCH, 160, 16, 327, 8, 0, 0, 0, 0},
};
const int mv_modelCount = (int)(sizeof(mv_models) / sizeof(mv_models[0]));
