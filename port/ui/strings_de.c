/*
 * port/ui/strings_de.c
 *
 * The port's UI strings in German (strings.h), UTF-8.
 */
#include "strings.h"

const char *const ui_strings_de[UI_STR_COUNT] = {
    [UI_STR_NONE] = "",
    [UI_STR_SETTINGS] = "Einstellungen",
    [UI_STR_SECTION_DISPLAY] = "Anzeige",
    [UI_STR_SECTION_CONTROLS] = "Steuerung",
    [UI_STR_SECTION_GAMEPLAY] = "Spiel",
    [UI_STR_SECTION_LANGUAGE] = "Sprache",
    [UI_STR_SECTION_DEVELOPER] = "Entwicklermodus",
    [UI_STR_BACK] = "Zurück",
    [UI_STR_ON] = "Ein",
    [UI_STR_OFF] = "Aus",
    [UI_STR_OPT_PRESET] = "Voreinstellung",
    [UI_STR_VAL_ORIGINAL] = "Original",
    [UI_STR_VAL_ENHANCED] = "Verbessert",
    [UI_STR_OPT_FULLSCREEN] = "Vollbild",
    [UI_STR_OPT_RESOLUTION] = "Auflösung",
    [UI_STR_OPT_ASPECT] = "Seitenverhältnis",
    [UI_STR_OPT_VSYNC] = "Vertikale Synchronisation",
    [UI_STR_OPT_INTERPOLATION] = "Bildinterpolation",
    [UI_STR_OPT_FILTERING] = "Texturfilterung",
    [UI_STR_OPT_FULL_HEIGHT] = "Bild in voller Höhe",
    [UI_STR_OPT_MIRROR] = "Spiegelmodus",
    [UI_STR_OPT_REMAP] = "Tastenbelegung",
    [UI_STR_OPT_MOUSE_CAMERA] = "Kamera mit der Maus",
    [UI_STR_OPT_INVERT_X] = "Kamera X umkehren",
    [UI_STR_OPT_INVERT_Y] = "Kamera Y umkehren",
    [UI_STR_OPT_VIBRATION] = "Vibration",
    [UI_STR_OPT_STICK_FIX] = "Analogstick-Korrektur",
    [UI_STR_OPT_YORDA] = "Schatten entführen Yorda nie",
    [UI_STR_OPT_YORDA_NOTE] =
        "Für ein entspannteres Spiel. Einige geskriptete Szenen zeigen die Entführung weiterhin.",
    [UI_STR_LANG_EN] = "English",
    [UI_STR_LANG_FR] = "Français",
    [UI_STR_LANG_DE] = "Deutsch",
    [UI_STR_LANG_IT] = "Italiano",
    [UI_STR_LANG_ES] = "Español",
    [UI_STR_OPT_SKIP_BOOT] = "Startbildschirme überspringen",
    [UI_STR_OPT_DEVELOPER_MODE] = "Entwicklermodus",
    [UI_STR_DEVELOPER_NOTE] = "Erfolge sind im Entwicklermodus ausgesetzt",
    [UI_STR_POPUP_TEST_TITLE] = "Testmeldung",
    [UI_STR_POPUP_TEST_BODY] = "Laufzeittext: Éléphant, Größe, señor, città, cœur",
    [UI_STR_ACHIEVEMENT_UNLOCKED] = "Erfolg freigeschaltet",
};
