/*
 * port/input/test/touch_shapes.h
 *
 * The touch tests' screens (touch_test.c, touch_edit_test.c: two
 * executables, one table): phones, a notch inset, foldables open, bent and
 * split, a tablet, monitors, and the rule each should give at Small,
 * Medium and Large.
 */
#ifndef ICO_PORT_INPUT_TEST_TOUCH_SHAPES_H
#define ICO_PORT_INPUT_TEST_TOUCH_SHAPES_H

#include "touch.h"

/* a test screen: its pixels, insets, density and fold, and the rule it
   should give at Small, Medium and Large */
typedef struct Shape {
    uint32_t w, h;
    IcoTouchInsets insets;
    float dpi; /* 0 unknown */
    IcoTouchFold fold;
    int rule[3];
} Shape;

#define NOFOLD {ICO_TOUCH_FOLD_NONE, 0, 0, {0.0f, 0.0f, 0.0f, 0.0f}}
#define FULL3 {ICO_TOUCH_RULE_FULL, ICO_TOUCH_RULE_FULL, ICO_TOUCH_RULE_FULL}

static const Shape s_shapes[] = {
    {1920, 1080, {0, 0, 0, 0}, 0.0f, NOFOLD, FULL3},      /* desktop, density unknown */
    {2340, 1080, {0, 0, 0, 0}, 420.0f, NOFOLD, FULL3},    /* a 19.5:9 phone */
    {2400, 1080, {100, 0, 0, 0}, 450.0f, NOFOLD, FULL3},  /* a notch on the left */
    {2400, 1080, {160, 0, 30, 0}, 450.0f, NOFOLD, FULL3}, /* camera hole, a right inset */
    {2176, 1812, {0, 0, 0, 0}, 420.0f, NOFOLD, FULL3},    /* a foldable open */
    {1812, 2176, {0, 0, 0, 0}, 420.0f, NOFOLD, FULL3},    /* the same, portrait */
    {2560, 1600, {0, 0, 0, 0}, 320.0f, NOFOLD, FULL3},    /* a 16:10 tablet */
    {960, 540, {0, 0, 0, 0}, 0.0f, NOFOLD, FULL3},        /* a small window */
    {3840, 2160, {0, 0, 0, 0}, 96.0f, NOFOLD, FULL3},     /* a 4K monitor */
    {2208,
     1840,
     {0, 0, 0, 0},
     420.0f,
     {ICO_TOUCH_FOLD_VERTICAL, 0, 0, {1104.0f, 0.0f, 0.0f, 1840.0f}},
     {ICO_TOUCH_RULE_SPLIT, ICO_TOUCH_RULE_SPLIT, ICO_TOUCH_RULE_SPLIT}}, /* flat, zero wide */
    {2176,
     1812,
     {0, 0, 0, 0},
     420.0f,
     {ICO_TOUCH_FOLD_HORIZONTAL, 1, 1, {0.0f, 906.0f, 2176.0f, 0.0f}},
     {ICO_TOUCH_RULE_TABLETOP, ICO_TOUCH_RULE_TABLETOP, ICO_TOUCH_RULE_TABLETOP}}, /* bent */
    {2176,
     1812,
     {0, 0, 0, 0},
     420.0f,
     {ICO_TOUCH_FOLD_HORIZONTAL, 0, 0, {0.0f, 906.0f, 2176.0f, 0.0f}},
     /* flat: the crease misses every button until Large puts R1 on it */
     {ICO_TOUCH_RULE_FULL, ICO_TOUCH_RULE_FULL, ICO_TOUCH_RULE_TABLETOP}},
    {2784,
     1800,
     {0, 0, 0, 0},
     401.0f,
     {ICO_TOUCH_FOLD_VERTICAL, 0, 1, {1350.0f, 0.0f, 84.0f, 1800.0f}},
     {ICO_TOUCH_RULE_SPLIT, ICO_TOUCH_RULE_SPLIT, ICO_TOUCH_RULE_SPLIT}}, /* two screens */
};

#define SHAPE_COUNT ((int)(sizeof(s_shapes) / sizeof(s_shapes[0])))

#endif /* ICO_PORT_INPUT_TEST_TOUCH_SHAPES_H */
