/*
 * port/input/test/pad_kind_test.c
 *
 * ctest pad_kind: the kind of controller pressed last (pad_kind.c), the
 * button pictures' Auto input.  Every value of SDL 3.4.18's
 * SDL_GamepadType (mirrored in pad_kind.h, checked against SDL by
 * input_sdl.c's _Static_asserts) to its kind, values out of range, and the
 * kind the reported presses leave (keyboard, mouse and touch give
 * PlayStation), with the changes counted once per change.
 */
#include <stdio.h>

#include "input.h"
#include "pad_kind.h"

static int s_fail;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            s_fail++;                                                                              \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
        }                                                                                          \
    } while (0)

int main(void)
{
    static const struct {
        int type, kind;
        const char *name;
    } kTable[] = {
        {ICO_SDLPAD_UNKNOWN, ICO_PAD_KIND_PLAYSTATION, "unknown"},
        {ICO_SDLPAD_STANDARD, ICO_PAD_KIND_PLAYSTATION, "standard"},
        {ICO_SDLPAD_XBOX360, ICO_PAD_KIND_XBOX, "xbox360"},
        {ICO_SDLPAD_XBOXONE, ICO_PAD_KIND_XBOX, "xboxone"},
        {ICO_SDLPAD_PS3, ICO_PAD_KIND_PLAYSTATION, "ps3"},
        {ICO_SDLPAD_PS4, ICO_PAD_KIND_PLAYSTATION, "ps4"},
        {ICO_SDLPAD_PS5, ICO_PAD_KIND_PLAYSTATION, "ps5"},
        {ICO_SDLPAD_NINTENDO_SWITCH_PRO, ICO_PAD_KIND_NINTENDO, "switch_pro"},
        {ICO_SDLPAD_NINTENDO_SWITCH_JOYCON_LEFT, ICO_PAD_KIND_PLAYSTATION, "joycon_left"},
        {ICO_SDLPAD_NINTENDO_SWITCH_JOYCON_RIGHT, ICO_PAD_KIND_PLAYSTATION, "joycon_right"},
        {ICO_SDLPAD_NINTENDO_SWITCH_JOYCON_PAIR, ICO_PAD_KIND_NINTENDO, "joycon_pair"},
        {ICO_SDLPAD_GAMECUBE, ICO_PAD_KIND_PLAYSTATION, "gamecube"},
        {ICO_SDLPAD_STEAM, ICO_PAD_KIND_XBOX, "steam"},
    };

    const int n = (int)(sizeof(kTable) / sizeof(kTable[0]));

    /* SDL_GAMEPAD_TYPE_COUNT values (UNKNOWN .. STEAM), each once, in order */
    CHECK(n == ICO_SDLPAD_COUNT, "%d rows for %d types", n, ICO_SDLPAD_COUNT);
    for (int i = 0; i < n; i++) {
        CHECK(kTable[i].type == i, "row %d is type %d", i, kTable[i].type);
        CHECK(ico_pad_kind_of_sdl_type(kTable[i].type) == kTable[i].kind, "type %s: kind %s",
              kTable[i].name, ico_pad_kind_name(ico_pad_kind_of_sdl_type(kTable[i].type)));
        const char *nm = ico_pad_sdl_type_name(kTable[i].type);
        CHECK(nm[0] == kTable[i].name[0] && nm[1] == kTable[i].name[1], "type %d named %s", i, nm);
    }
    static const int kOut[] = {-1, ICO_SDLPAD_COUNT, 99};
    for (int i = 0; i < 3; i++) {
        CHECK(ico_pad_kind_of_sdl_type(kOut[i]) == ICO_PAD_KIND_PLAYSTATION,
              "type %d out of range: PlayStation", kOut[i]);
        CHECK(ico_pad_sdl_type_name(kOut[i])[0] == '?', "type %d out of range: ?", kOut[i]);
    }
    CHECK(ico_pad_kind_name(ICO_PAD_KIND_XBOX)[0] == 'X' && ico_pad_kind_name(3)[0] == '?',
          "the kind names");

    /* the presses: before any, PlayStation */
    ico_pad_kind_reset();
    CHECK(ico_input_last_pad_kind() == ICO_PAD_KIND_PLAYSTATION && ico_pad_kind_changes() == 0,
          "PlayStation before any press");
    ico_input_note_pad_kind(ICO_PAD_KIND_PLAYSTATION, "keyboard");
    CHECK(ico_pad_kind_changes() == 0, "the keyboard first: no change");
    ico_input_note_pad_kind(ico_pad_kind_of_sdl_type(ICO_SDLPAD_XBOXONE), "xboxone");
    CHECK(ico_input_last_pad_kind() == ICO_PAD_KIND_XBOX && ico_pad_kind_changes() == 1,
          "an Xbox One pad's press: Xbox");
    ico_input_note_pad_kind(ico_pad_kind_of_sdl_type(ICO_SDLPAD_STEAM), "steam");
    CHECK(ico_input_last_pad_kind() == ICO_PAD_KIND_XBOX && ico_pad_kind_changes() == 1,
          "a Steam pad after it: Xbox still, no change");
    ico_input_note_pad_kind(ICO_PAD_KIND_PLAYSTATION, "touch");
    CHECK(ico_input_last_pad_kind() == ICO_PAD_KIND_PLAYSTATION && ico_pad_kind_changes() == 2,
          "a touch: PlayStation");
    ico_input_note_pad_kind(ico_pad_kind_of_sdl_type(ICO_SDLPAD_NINTENDO_SWITCH_PRO), "switch_pro");
    CHECK(ico_input_last_pad_kind() == ICO_PAD_KIND_NINTENDO && ico_pad_kind_changes() == 3,
          "a Switch Pro Controller: Nintendo");
    ico_input_note_pad_kind(ICO_PAD_KIND_PLAYSTATION, "mouse");
    CHECK(ico_input_last_pad_kind() == ICO_PAD_KIND_PLAYSTATION, "a mouse button: PlayStation");
    ico_input_note_pad_kind(ico_pad_kind_of_sdl_type(ICO_SDLPAD_NINTENDO_SWITCH_JOYCON_LEFT),
                            "joycon_left");
    CHECK(ico_input_last_pad_kind() == ICO_PAD_KIND_PLAYSTATION && ico_pad_kind_changes() == 4,
          "a single Joy-Con: PlayStation");
    ico_input_note_pad_kind(7, NULL);
    CHECK(ico_input_last_pad_kind() == ICO_PAD_KIND_PLAYSTATION, "a kind out of range");
    /* 250 changes: the counter counts each (the log writes 8, then every 100th) */
    ico_pad_kind_reset();
    for (int i = 0; i < 250; i++) {
        ico_input_note_pad_kind(i % 2 ? ICO_PAD_KIND_PLAYSTATION : ICO_PAD_KIND_XBOX, "test");
    }
    CHECK(ico_pad_kind_changes() == 250, "%u changes", ico_pad_kind_changes());

    if (s_fail) {
        fprintf(stderr, "pad_kind_test: %d failures\n", s_fail);
        return 1;
    }
    printf("pad_kind_test: ok\n");
    return 0;
}
