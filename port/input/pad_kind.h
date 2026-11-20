/*
 * port/input/pad_kind.h
 *
 * Which kind of controller a gamepad is, for the button pictures
 * (port/ui/pad_glyphs.h): SDL's gamepad type mapped to IcoPadKind
 * (input.h) by pad_kind.c, a plain function so its test needs no SDL.
 * IcoSdlPadType mirrors SDL_GamepadType of the vendored SDL 3.4.18
 * (SDL_gamepad.h); input_sdl.c checks every value with _Static_assert.
 */
#ifndef ICO_PORT_INPUT_PAD_KIND_H
#define ICO_PORT_INPUT_PAD_KIND_H

typedef enum IcoSdlPadType {
    ICO_SDLPAD_UNKNOWN = 0,
    ICO_SDLPAD_STANDARD,
    ICO_SDLPAD_XBOX360,
    ICO_SDLPAD_XBOXONE,
    ICO_SDLPAD_PS3,
    ICO_SDLPAD_PS4,
    ICO_SDLPAD_PS5,
    ICO_SDLPAD_NINTENDO_SWITCH_PRO,
    ICO_SDLPAD_NINTENDO_SWITCH_JOYCON_LEFT,
    ICO_SDLPAD_NINTENDO_SWITCH_JOYCON_RIGHT,
    ICO_SDLPAD_NINTENDO_SWITCH_JOYCON_PAIR,
    ICO_SDLPAD_GAMECUBE,
    ICO_SDLPAD_STEAM,
    ICO_SDLPAD_COUNT
} IcoSdlPadType;

/* The kind (IcoPadKind) of a gamepad of SDL type t: Xbox 360, Xbox One and
   Steam (A B X Y in the Xbox places) are Xbox; the Switch Pro Controller
   and a pair of Joy-Cons are Nintendo; everything else (PS3, PS4, PS5,
   STANDARD, UNKNOWN, a single Joy-Con, GameCube, a value out of range)
   is PlayStation. */
int ico_pad_kind_of_sdl_type(int t);
/* "unknown", "standard", "xbox360", ... "steam" (log lines); "?" out of
   range */
const char *ico_pad_sdl_type_name(int t);
/* "PlayStation", "Xbox", "Nintendo"; "?" out of range */
const char *ico_pad_kind_name(int kind);
/* Tests: the kind changes ico_input_note_pad_kind has seen (logged as the
   first 8, then every 100th), and a fresh start (PlayStation, no change) */
unsigned int ico_pad_kind_changes(void);
void ico_pad_kind_reset(void);

#endif /* ICO_PORT_INPUT_PAD_KIND_H */
