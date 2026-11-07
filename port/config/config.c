/*
 * port/config/config.c
 *
 * config.toml and ico-pc.ini as one settings store (config.h). The parsing
 * and writing are host_config.c's.
 */
#include "config.h"
#include "../core/region.h" /* [game] disc and last_disc: the regions' slugs */
#include "host_config.h"
#include "host_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int loaded;
static char toml_file[ICO_PATH_MAX];
static char ini_file[ICO_PATH_MAX];
static char want_toml[ICO_PATH_MAX];
static char want_ini[ICO_PATH_MAX];
static IcoToml *toml;
static IcoIni ini;
/* a setter changed the copy in memory since it was read or saved
   (ico_config_dirty) */
static int dirty;
/* [game] disc's unknown value was logged this load */
static int disc_logged;

void ico_config_reset(const char *toml_path, const char *ini_path)
{
    ico_toml_free(toml);
    toml = NULL;
    loaded = 0;
    dirty = 0;
    disc_logged = 0;
    snprintf(want_toml, sizeof(want_toml), "%s", toml_path != NULL ? toml_path : "");
    snprintf(want_ini, sizeof(want_ini), "%s", ini_path != NULL ? ini_path : "");
}

/* Keys a config.toml may hold that nothing reads: kept in the file (as any
   unknown key is) and logged once a load.  [game] classic_menu_text and
   [game] port_font name a choice of how the game's own words are drawn;
   they always keep their texels. [dev] hand_probe names a log of the
   characters' hand-holding numbers, which the port does not write.
   [video] effect_glow_size named the glow's size above 1x; the glow is
   drawn at the PS2's size at every resolution. */
static const char *const retired_keys[] = {"game.classic_menu_text", "game.port_font",
                                           "dev.hand_probe", "video.effect_glow_size"};

static void log_retired(void)
{
    for (size_t i = 0; i < sizeof(retired_keys) / sizeof(retired_keys[0]); i++) {
        if (ico_toml_has(toml, retired_keys[i])) {
            fprintf(stderr, "config: %s is not used; ignored\n", retired_keys[i]);
        }
    }
}

static void ensure(void)
{
    char dir[ICO_PATH_MAX];

    if (loaded) {
        return;
    }
    loaded = 1;
    if (want_toml[0] != '\0') {
        snprintf(toml_file, sizeof(toml_file), "%s", want_toml);
    } else {
        ico_host_pref_dir(dir, sizeof(dir));
        if (ico_path_join(toml_file, sizeof(toml_file), dir, "config.toml") != 0) {
            fprintf(stderr, "config: the per-user folder's path is too long for config.toml\n");
        }
    }
    if (want_ini[0] != '\0') {
        snprintf(ini_file, sizeof(ini_file), "%s", want_ini);
    } else {
        ico_host_ini_path(ini_file, sizeof(ini_file));
    }
    ico_ini_load_file(&ini, ini_file);
    toml = ico_toml_load(toml_file);
    if (toml == NULL) {
        toml = ico_toml_parse("");
        fprintf(stderr, "config: no %s; defaults\n", toml_file);
    } else {
        long long v = ico_toml_get_int(toml, "version", ICO_CONFIG_VERSION);

        fprintf(stderr, "config: %s (version %lld)\n", toml_file, v);
        if (v > ICO_CONFIG_VERSION) {
            fprintf(stderr, "config: written by a newer version; unknown keys are kept\n");
        }
        log_retired();
    }
}

const char *ico_config_toml_path(void)
{
    ensure();
    return toml_file;
}

const char *ico_config_ini_path(void)
{
    ensure();
    return ini_file;
}

/* the ini's value for a toml path, or NULL */
static const char *ini_value(const char *path)
{
    const char *key = ico_config_ini_key(path);
    const char *v = key != NULL ? ico_ini_get(&ini, key) : NULL;

    return v != NULL && v[0] != '\0' ? v : NULL;
}

const char *ico_config_get_string(const char *path, const char *def)
{
    const char *v;

    ensure();
    v = ini_value(path);
    if (v == NULL) {
        v = ico_toml_get(toml, path);
    }
    return v != NULL ? v : def;
}

int ico_config_get_bool(const char *path, int def)
{
    const char *v;

    ensure();
    v = ini_value(path);
    if (v != NULL) {
        if (strcmp(v, "1") == 0 || strcmp(v, "true") == 0) {
            return 1;
        }
        if (strcmp(v, "0") == 0 || strcmp(v, "false") == 0) {
            return 0;
        }
    }
    return ico_toml_get_bool(toml, path, def);
}

long long ico_config_get_int(const char *path, long long def)
{
    const char *v;
    char *end;
    long long n;

    ensure();
    v = ini_value(path);
    if (v != NULL) {
        n = strtoll(v, &end, 10);
        if (*end == '\0') {
            return n;
        }
    }
    return ico_toml_get_int(toml, path, def);
}

double ico_config_get_float(const char *path, double def)
{
    const char *v;
    char *end;
    double d;

    ensure();
    v = ini_value(path);
    if (v != NULL) {
        d = strtod(v, &end);
        if (*end == '\0') {
            return d;
        }
    }
    return ico_toml_get_float(toml, path, def);
}

/* a setter's result; a change marks the copy unsaved */
static int note_set(int r)
{
    if (r == 0) {
        dirty = 1;
    }
    return r;
}

int ico_config_dirty(void)
{
    return dirty;
}

int ico_config_set_string(const char *path, const char *value)
{
    ensure();
    return note_set(ico_toml_set_string(toml, path, value));
}

int ico_config_set_bool(const char *path, int value)
{
    ensure();
    return note_set(ico_toml_set_bool(toml, path, value));
}

int ico_config_set_int(const char *path, long long value)
{
    ensure();
    return note_set(ico_toml_set_int(toml, path, value));
}

int ico_config_set_float(const char *path, double value)
{
    ensure();
    return note_set(ico_toml_set_float(toml, path, value));
}

int ico_config_save(void)
{
    int r = 0;

    ensure();
    if (!ico_toml_has(toml, "version")) {
        r |= ico_toml_set_int(toml, "version", ICO_CONFIG_VERSION);
    }
    if (!ico_toml_has(toml, "paths.iso")) {
        r |= ico_toml_set_string(toml, "paths.iso", "");
    }
    if (!ico_toml_has(toml, "video.preset")) {
        r |= ico_toml_set_string(toml, "video.preset", "original");
    }
    if (!ico_toml_has(toml, "video.vsync")) {
        r |= ico_toml_set_bool(toml, "video.vsync", 1);
    }
    if (!ico_toml_has(toml, "video.window_mode") && !ico_toml_has(toml, "video.fullscreen")) {
        r |= ico_toml_set_string(toml, "video.window_mode", "windowed");
    }
    if (!ico_toml_has(toml, "video.fullscreen")) {
        r |= ico_toml_set_bool(toml, "video.fullscreen", 0);
    }
    if (!ico_toml_has(toml, "audio.enabled")) {
        r |= ico_toml_set_bool(toml, "audio.enabled", 1);
    }
    if (!ico_toml_has(toml, "audio.volume")) {
        r |= ico_toml_set_float(toml, "audio.volume", 1.0);
    }
    if (!ico_toml_has(toml, "audio.music")) {
        r |= ico_toml_set_float(toml, "audio.music", 1.0);
    }
    if (!ico_toml_has(toml, "audio.effects")) {
        r |= ico_toml_set_float(toml, "audio.effects", 1.0);
    }
    if (!ico_toml_has(toml, "audio.output")) {
        r |= ico_toml_set_string(toml, "audio.output", "auto");
    }
    if (!ico_toml_has(toml, "audio.device")) {
        r |= ico_toml_set_string(toml, "audio.device", "");
    }
    if (!ico_toml_has(toml, "game.language")) {
        r |= ico_toml_set_string(toml, "game.language", "auto");
    }
    if (r != 0 || ico_toml_save(toml, toml_file) != 0) {
        fprintf(stderr, "config: cannot write %s\n", toml_file);
        return -1;
    }
    dirty = 0;
    return 0;
}

/* the file a first run leaves: every key ico_config_save adds and more
   (the packs, the effects, [photo], Android's driver keys), at their
   defaults, and a line per section */
static const char first_run_text[] =
    "# ico-pc settings. Every key below is at its default; edit and restart.\n"
    "# The Options menu rewrites only the lines it changes, so comments and\n"
    "# keys of your own stay. Keys not listed here keep their defaults.\n"
    "\n"
    "version = 1\n"
    "\n"
    "[paths]\n"
    "# The disc image, a .iso, .chd, .bin or .cue; empty asks on the first start.\n"
    "iso = \"\"\n"
#ifdef __ANDROID__
    "# true keeps the copy of the disc image the first start makes in this\n"
    "# folder (Ico_PAL or Ico_JPN, .iso, .chd or .bin); false deletes it once the game is ready,\n"
    "# which gives its space back.\n"
    "keep_image = false\n"
#endif
    "\n"
    "[video]\n"
    "# \"original\" is the PS2 picture; \"enhanced\" takes resolution,\n"
    "# aspect, texture_filter and full_height as written.\n"
    "preset = \"original\"\n"
    "vsync = true\n"
    "# windowed, borderless (a frameless window that fills the screen) or\n"
    "# fullscreen\n"
    "window_mode = \"windowed\"\n"
    "fullscreen = false\n"
    "# The frames per second shown in the top-left corner of the screen (the\n"
    "# Frame rate counter row); false shows nothing.\n"
    "show_fps = false\n"
    "# Draw the picture at the resolution you chose and shrink it to the game's\n"
    "# own size (the Downsampling row): \"off\" or \"box\".\n"
    "downsample = \"off\"\n"
    "# A PCSX2 texture pack: copy its SCES-50760 folder (SCPS-11003 for the\n"
    "# Japanese disc) into a folder named\n"
    "# textures beside the program or in your user folder. false shows the\n"
    "# game's own textures.\n"
    "texture_pack = true\n"
    "# A model pack: copy its SCES-50760 folder (SCPS-11003 for the Japanese\n"
    "# disc) into a folder named models\n"
    "# beside the program or in your user folder. false shows the game's own\n"
    "# models.\n"
    "model_pack = true\n"
    "# For pack makers: true saves each model part as a glTF file in\n"
    "# models/SCES-50760/dumps (SCPS-11003 for the Japanese disc) in your user\n"
    "# folder. Needs Developer mode.\n"
    "dump_models = false\n"
    "# The three texture pack lines below are switched off by the # in front:\n"
    "# to change one, remove its #, or add the line under [video] when the\n"
    "# file does not have it.\n"
    "# The graphics card memory in MB a texture pack may use (128 to 65536).\n"
    "# Textures past it stay the game's own.\n"
    "# texture_pack_budget_mb = 2048\n"
    "# true reads the pack into memory in the background from the start,\n"
    "# subtitles and menus first, so the game's own do not flash up before\n"
    "# the pack's; false reads each texture when the game first shows it.\n"
    "# texture_pack_precache = true\n"
    "# The computer memory in MB that reading ahead may use: 0 is up to half\n"
    "# of the computer's memory (on Android an eighth, at most 512 MB), or\n"
    "# 128 to 65536. What does not fit is read when the game first shows it.\n"
    "# texture_pack_cache_mb = 0\n"
    "# The game's own picture effects; true keeps each as on the PS2.\n"
    "# Glow: the light bloom and the sun's flare.\n"
    "effect_glow = true\n"
    "# How the glow is made for the frames shown between the game's own:\n"
    "# \"original\" keeps the glow of the game's own frame; \"smooth\" makes it\n"
    "# again for every frame shown (costs more of the graphics card's time).\n"
    "# Only matters above Frame rate Original, with glow on.\n"
    "effect_glow_mode = \"original\"\n"
    "# Depth of field: the blur of far away things.\n"
    "effect_depth_of_field = true\n"
    "# Screen softening: the slight blur that smooths jagged edges.\n"
    "effect_softening = true\n"
    "# Motion blur: the trail moving pictures leave.\n"
    "effect_motion_blur = true\n"
    "# How the trail moves while motion blur is on: \"original\" moves it on with\n"
    "# the game's own frames; \"smooth\" moves it on with every frame shown, the\n"
    "# same length of trail (costs more on phones).\n"
    "effect_motion_blur_mode = \"original\"\n"
    "# Motion blur strength, 10 to 100 in tens: lower values make the trail\n"
    "# fade faster; 100 is the PS2's.\n"
    "effect_motion_blur_strength = 100\n"
    "# Fog: the haze over the distance.\n"
    "effect_fog = true\n"
    "# Cinematic bars: the black bars above and below the picture in cutscenes.\n"
    "effect_cinematic_bars = true\n"
    "# Water reflections: true draws the reflections in water (and the picture\n"
    "# through the queen's barrier) at the game's resolution; false keeps the\n"
    "# PS2's softer ones.\n"
    "effect_water_sharp = false\n"
    "# The colour of the picture, the Picture page's Gamma, Picture brightness,\n"
    "# Contrast and Saturation rows. 50 is the game's own picture; 1 to 100.\n"
    "gamma = 50\n"
    "brightness = 50\n"
    "contrast = 50\n"
    "saturation = 50\n"
    "# Depth for an effects program such as ReShade: true lets its depth\n"
    "# effects see how far away things are (not with the CRT filter).\n"
    "effects_depth = true\n"
#ifdef __ANDROID__
    "# The graphics driver: empty is the phone's own; else the folder of a\n"
    "# driver added in Options > Graphics driver. Used from the next start.\n"
    "gpu_driver = \"\"\n"
    "# The driver that did not start last time (the game went back to the\n"
    "# phone's own); written by the game.\n"
    "gpu_driver_failed = \"\"\n"
#endif
    "\n"
    "[audio]\n"
    "# volume, music and effects are 0.0 to 1.0; output is \"auto\" (the memory\n"
    "# card's choice), \"stereo\" or \"mono\"; device is a device name, empty for\n"
    "# the system's default.\n"
    "enabled = true\n"
    "volume = 1.0\n"
    "music = 1.0\n"
    "effects = 1.0\n"
    "output = \"auto\"\n"
    "device = \"\"\n"
    "\n"
    "[game]\n"
    "# \"auto\" follows the system; or en, fr, de, it, es.\n"
    "language = \"auto\"\n"
    "# A translation by a player of the game's menus and subtitles, shown over\n"
    "# the English game: \"\" for none, \"ru-bobttz\" (Russian, by bob-ttz),\n"
    "# \"ru-cobra838\" (Russian, by cobra838) or \"pt-br\" (Brazilian\n"
    "# Portuguese, by Igor Chagas).\n"
    "text_language = \"\"\n"
    "# \"pal\" (the European disc, SCES-50760) or \"jpn\" (the Japanese disc,\n"
    "# SCPS-11003) pins the disc when both are set up; \"auto\" plays the\n"
    "# disc image given, else the one played last. From the next start.\n"
    "disc = \"auto\"\n"
    "\n"
    "[gameplay]\n"
    "# true: a chain that comes down for Ico keeps its links the right size\n"
    "# the whole way down; false shows it as the PS2 did.\n"
    "natural_chains = false\n"
    "# true: start at the title screen, without the Sony sign and the opening\n"
    "# scene. From the next start.\n"
    "skip_opening = false\n"
    "# \"disc\": Yorda's hand as the disc's own game takes it (keep R1 held on the\n"
    "# European, Japanese, Korean and Asian discs; press once to take it and once\n"
    "# to let go on the North American disc and the 2001 previews); \"hold\": keep\n"
    "# R1 held; \"toggle\": press R1 once to take it, again to let go.\n"
    "hold_type = \"disc\"\n"
    "# How long a shadow takes to pull Yorda into a pit, from when it starts to\n"
    "# sink with her: 4 to 30 (seconds) or \"original\" (the game's own, 15 s).\n"
    "pit_time = \"original\"\n"
    "# How fast shadows walk, run and fly: \"slower\", \"original\", \"faster\" or\n"
    "# \"fastest\". It does not change how often they attack.\n"
    "shadow_speed = \"original\"\n"
    "# true: several shadows may attack at the same time; false: they take\n"
    "# turns, as the original.\n"
    "shadows_together = false\n"
    "# true: running hand in hand for three seconds makes Yorda fall, as she\n"
    "# does after the main gate opens. From the next room.\n"
    "yorda_trips = false\n"
    "# true: a second memory card with ten more saves (the folder memcard2\n"
    "# beside memcard); the game's save and load screens offer both cards.\n"
    "second_memory_card = false\n"
    "\n"
    "[variants]\n"
    "# Options > Gameplay (No hints), Gameplay > Shadows, Combat and\n"
    "# Cutscenes. Every line is the game as the disc\n"
    "# ships it. shade_toughness: \"original\", \"american\" (every Shadow as tough\n"
    "# as on the North American disc, and no tougher on later playthroughs),\n"
    "# \"no_plus\" (no tougher on later playthroughs) or \"third_journey\" (as tough\n"
    "# as on a third playthrough, twice the first journey's: the PAL, Japanese,\n"
    "# Korean and Chinese discs only). aggression: \"own\" (each\n"
    "# Shadow's own), \"bold_patient\", \"bold_quick\", \"wary_patient\" or\n"
    "# \"wary_quick\". barrier_hits (5 or 7, the Queen's barrier) is not written:\n"
    "# without it the disc's own number is used. one_hit, never_dodge,\n"
    "# ignore_cutscene_cameras and peaceful_shadows switch achievements off while\n"
    "# they are on (peaceful_shadows: the Shadows never come for Ico or Yorda).\n"
    "# auto_guard, attack_turn and shadows_wait_down (Combat) are the game as\n"
    "# shipped while true; false makes it harder and keeps achievements.\n"
    "# sword_colour: \"original\" or \"red\" (the light sword's trail is red;\n"
    "# the European, Japanese, Korean and Asian discs). weapon: \"original\" or\n"
    "# \"net\" (every stick is the makers' unused insect net: it is never swung,\n"
    "# cannot be lit and cannot hurt a Shadow). Both from the next room.\n"
    "shade_toughness = \"original\"\n"
    "one_hit = false\n"
    "never_dodge = false\n"
    "aggression = \"own\"\n"
    "no_hints = false\n"
    "no_cutscene_look = false\n"
    "ignore_cutscene_cameras = false\n"
    "auto_guard = true\n"
    "attack_turn = true\n"
    "shadows_wait_down = true\n"
    "sword_colour = \"original\"\n"
    "weapon = \"original\"\n"
    "peaceful_shadows = false\n"
    "\n"
    "[developer]\n"
    "# true: with Developer mode on (Options > Developer), L2 hands Ico to the\n"
    "# game's own debug move: the left stick moves him over the ground, the\n"
    "# right stick up and down, L3 puts him on the floor under him and L2 let go\n"
    "# after that gives him back. Achievements are off while it is on.\n"
    "free_movement = false\n"
    "\n"
    "[photo]\n"
    "# How fast photo mode's camera moves, turns, rolls and zooms (1.0 is normal).\n"
    "stick_speed = 1.0\n"
    "# true swaps up and down on the stick you look around with.\n"
    "invert_y = false\n"
    "# true starts photo mode with its help panel hidden (Square shows it).\n"
    "hide_ui = false\n"
    "# The Film effect photo mode starts with: 0 is off, 1 to 4 are the game's\n"
    "# four, 5 is Deja vu (the d-pad Right button changes it in photo mode).\n"
    "film_effect = 0\n"
    "# The folder for saved pictures, inside your user folder.\n"
    "png_dir = \"screenshots\"\n"
    "\n"
    "# The touch screen buttons you arranged yourself (Options > Controls > Touch\n"
    "# screen > Edit layout writes these lines; nothing is here until you do).\n"
    "# snap = true or false and grid = 2, 4 or 8 (millimetres) go under [touch],\n"
    "# with format = 2 (the editor writes it; without it \"hidden\" and o0 mean\n"
    "# off, as before version 0.8.6). Each posture of the phone (full, split,\n"
    "# tabletop) has its own table, for example [touch.full], with a line per\n"
    "# button: dpad, cross, circle, square, triangle, r1, l1, l2, r2, select,\n"
    "# start, stick or look. A line reads cross = \"r20 b10.5 s1.00 o100\": the\n"
    "# distance in millimetres from the left (l) or right (r) edge and from the\n"
    "# top (t) or bottom (b) edge, the size (s, 0.7 to 3.0) and the opacity (o,\n"
    "# percent). The look area also has its size, w and h. In place of o,\n"
    "# \"hidden\" keeps a button working but does not draw it; \"off\" switches it\n"
    "# off (either alone, or after the place and size). \"default\" or no line\n"
    "# puts a button back. Deleting these lines resets the layout.\n";

int ico_config_write_first_run(void)
{
    char tmp[ICO_PATH_MAX + 8];
    FILE *f;
    size_t n = sizeof(first_run_text) - 1;
    int ok;

    ensure();
    if (toml_file[0] == '\0') {
        return -1;
    }
    if (ico_file_exists(toml_file)) {
        return 1;
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp", toml_file);
    f = ico_fopen(tmp, "wb");
    if (f == NULL) {
        fprintf(stderr, "config: cannot write %s\n", toml_file);
        return -1;
    }
    ok = fwrite(first_run_text, 1, n, f) == n;
    ok = ico_fsync(f) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    /* a file that appeared meanwhile is the player's: not replaced */
    if (ok && ico_file_exists(toml_file)) {
        ico_remove(tmp);
        return 1;
    }
    if (!ok || ico_rename_replace(tmp, toml_file) != 0) {
        ico_remove(tmp);
        fprintf(stderr, "config: cannot write %s\n", toml_file);
        return -1;
    }
    fprintf(stderr, "config: wrote %s (first run)\n", toml_file);
    return 0;
}

/* the row of a region slug, or -1 */
static int region_row(const char *slug)
{
    int i;

    for (i = 0; slug != NULL && i < ico_region_count(); i++) {
        if (strcmp(ico_region_at(i)->slug, slug) == 0) {
            return i;
        }
    }
    return -1;
}

int ico_config_disc_pin(void)
{
    const char *v = ico_config_get_string("game.disc", "auto");
    int row = region_row(v);

    if (row < 0 && strcmp(v, "auto") != 0 && !disc_logged) {
        char names[128] = "";
        int i;

        disc_logged = 1;
        for (i = 0; i < ico_region_count(); i++) {
            size_t n = strlen(names);

            snprintf(names + n, sizeof(names) - n, ", %s", ico_region_at(i)->slug);
        }
        fprintf(stderr, "config: [game] disc = \"%s\" is not one of auto%s; auto\n", v, names);
    }
    return row;
}

int ico_config_last_disc(void)
{
    return region_row(ico_config_get_string("game.last_disc", ""));
}

int ico_config_set_last_disc(const char *slug)
{
    if (region_row(slug) < 0) {
        return -1;
    }
    return ico_config_set_string("game.last_disc", slug);
}
