/*
 * port/input/pad_kind.c
 *
 * The kind of the controller pressed last (input.h ico_input_last_pad_kind,
 * pad_kind.h): the mapping of SDL's gamepad types, and the kind the device
 * layer reports at every press.  No SDL: the headless build and the tests
 * link it, and nothing reports there, so the kind stays PlayStation.
 */
#include "pad_kind.h"

#include <stdio.h>

#include "input.h"

int ico_pad_kind_of_sdl_type(int t)
{
    switch (t) {
    case ICO_SDLPAD_XBOX360:
    case ICO_SDLPAD_XBOXONE:
    case ICO_SDLPAD_STEAM:
        return ICO_PAD_KIND_XBOX;
    case ICO_SDLPAD_NINTENDO_SWITCH_PRO:
    case ICO_SDLPAD_NINTENDO_SWITCH_JOYCON_PAIR:
        return ICO_PAD_KIND_NINTENDO;
    default:
        return ICO_PAD_KIND_PLAYSTATION;
    }
}

const char *ico_pad_sdl_type_name(int t)
{
    static const char *const kName[ICO_SDLPAD_COUNT] = {
        "unknown",    "standard",    "xbox360",      "xboxone",     "ps3",      "ps4",  "ps5",
        "switch_pro", "joycon_left", "joycon_right", "joycon_pair", "gamecube", "steam"};
    return t >= 0 && t < ICO_SDLPAD_COUNT ? kName[t] : "?";
}

const char *ico_pad_kind_name(int kind)
{
    static const char *const kName[3] = {"PlayStation", "Xbox", "Nintendo"};
    return kind >= 0 && kind < 3 ? kName[kind] : "?";
}

/* written by the device layer's event and vsync handling, read by the
   menus' drawing: the same thread in the window build (SDL's main thread
   runs the host loop) */
static int s_kind;
static unsigned int s_changes;

void ico_input_note_pad_kind(int kind, const char *from)
{
    if (kind != ICO_PAD_KIND_XBOX && kind != ICO_PAD_KIND_NINTENDO) {
        kind = ICO_PAD_KIND_PLAYSTATION;
    }
    if (kind == s_kind) {
        return;
    }
    const int was = s_kind;
    s_kind = kind;
    s_changes++;
    if (s_changes <= 8 || s_changes % 100 == 0) {
        fprintf(stderr, "input: last pad kind %s (was %s; the press: %s), change %u\n",
                ico_pad_kind_name(kind), ico_pad_kind_name(was), from ? from : "?", s_changes);
    }
}

int ico_input_last_pad_kind(void)
{
    return s_kind;
}

unsigned int ico_pad_kind_changes(void)
{
    return s_changes;
}

void ico_pad_kind_reset(void)
{
    s_kind = ICO_PAD_KIND_PLAYSTATION;
    s_changes = 0;
}
