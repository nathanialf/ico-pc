/*
 * port/game/appearance.h
 *
 * The characters' colours (Options > Extras > Characters, v0.4.2 package
 * K): nine parts of Ico and Yorda, each Original or one target colour, and
 * the recolour of the characters' CLUTs that the game's texture upload
 * (ico2/seki/src/Texture.c texHostTexture, ICO_RD) applies to a copy of
 * the CLUT before the decode.  No renderer or UI dependency: rd_tex.h is
 * used for its PSM constant and CLUT order only (header-only).
 *
 * Values: 0 is Original; a clothing part takes 1..ICO_APP_COLOURS (the
 * palette below, value = palette index + 1); a skin part takes
 * 1..ICO_APP_TONES (Tone 1, the lightest, to Tone 12, the darkest).
 *
 * config.toml, section [characters], one string per part (absent =
 * "original"; an unknown value reads as Original and is logged once):
 *
 *   ico_skin, ico_poncho_navy, ico_poncho_pink, ico_poncho_light,
 *   ico_poncho_dark, ico_tunic, ico_shorts, yorda_skin, yorda_dress
 *     = "original" | a palette name ("red" ... "black", clothing parts)
 *       | "tone1" ... "tone12" (skin parts)
 *
 * The values are read the first time any function here needs them;
 * ico_appearance_reload (called by ico_opt_reload) forgets them.  The
 * setters write the key into the config in memory (ico_config_set_string);
 * the caller saves the file (ico_config_save, the Settings menu on leaving
 * a page).
 *
 * The palette, in value order (HSL, H degrees, S and L percent):
 *    1 red      0,70,45     9 navy     225,45,20   17 gold    45,70,55
 *    2 crimson  350,65,35  10 blue     215,65,45   18 orange  28,80,52
 *    3 rose     340,55,65  11 sky      200,60,70   19 rust    15,60,35
 *    4 pink     330,60,75  12 teal     180,50,32   20 brown   25,40,25
 *    5 magenta  310,55,45  13 cyan     185,55,55   21 sand    35,35,70
 *    6 plum     290,35,30  14 green    130,45,35   22 white   30,10,92
 *    7 violet   270,45,50  15 moss     90,35,32    23 grey    0,0,55
 *    8 indigo   245,45,30  16 olive    65,40,35    24 black   0,0,8
 * The tones: Tone k (k = 1..12) at t = (k - 1) / 11 is H 30 -> 16,
 * S 45 -> 34, L 85 -> 20 (linear in t).
 */
#ifndef ICO_PORT_GAME_APPEARANCE_H
#define ICO_PORT_GAME_APPEARANCE_H

typedef enum IcoAppPart {
    ICO_APP_ICO_SKIN = 0,
    ICO_APP_ICO_PONCHO_NAVY,
    ICO_APP_ICO_PONCHO_PINK,
    ICO_APP_ICO_PONCHO_LIGHT,
    ICO_APP_ICO_PONCHO_DARK,
    ICO_APP_ICO_TUNIC,
    ICO_APP_ICO_SHORTS,
    ICO_APP_YORDA_SKIN,
    ICO_APP_YORDA_DRESS,
    ICO_APP_PART_COUNT
} IcoAppPart;

/* the clothing palette and the skin ramp (Original not counted) */
#define ICO_APP_COLOURS 24
#define ICO_APP_TONES 12

/* the part's value: 0 Original, else 1..ico_appearance_choices(p) - 1 */
int ico_appearance_get(IcoAppPart p);
/* the number of values of the part, Original included: 1 + ICO_APP_TONES
   for a skin part, 1 + ICO_APP_COLOURS for the others (0 for a bad part) */
int ico_appearance_choices(IcoAppPart p);
/* 1 for ICO_APP_ICO_SKIN and ICO_APP_YORDA_SKIN */
int ico_appearance_is_skin(IcoAppPart p);
/* 0 Ico, 1 Yorda */
int ico_appearance_character(IcoAppPart p);
/* Sets the part (a value out of range is Original) and writes its
   [characters] key; a changed value bumps the serial. */
void ico_appearance_set(IcoAppPart p, int value);
/* The part's colour as the texture holds it, before lighting: the part's
   anchor (its most common original colour) through the same recolour as
   the CLUTs, 0xRRGGBB, 8 bits per channel (0xFF full).  Original: the
   anchor itself. */
unsigned int ico_appearance_swatch(IcoAppPart p);
/* Every part to a random non-Original value from the seed (xorshift32; the
   caller picks the seed, e.g. time(NULL) ^ ico_host_main_ticks()), the
   four poncho groups all different; writes the nine keys, bumps the serial
   once and logs "appearance: randomize (seed N)". */
void ico_appearance_randomize(unsigned int seed);
/* Every part to Original (the nine keys written "original"); bumps the
   serial when something changed. */
void ico_appearance_reset(void);
/* Forget the values (read again from the config on next use) and bump the
   serial. */
void ico_appearance_reload(void);
/* Changes whenever a value may have changed; 0 until the first change.
   Texture.c re-decodes the covered textures when it moves. */
unsigned int ico_appearance_serial(void);
/* 1 when the texture (the game's trimmed name: "b_mantle") has a rule,
   whatever the values; never for the *_l / *_ref layers, the stone
   variants (sg_*), the hair or sekika_boy. */
int ico_appearance_covers(const char *texName);
/* Recolours the texture's CLUT into out (colors entries of 4 bytes, the
   CLUT's own memory order: the GS CSM1 order for 256 colours, as
   rdtex_Csm1Index reads it).  Only RGB is changed; the alpha bytes are
   copied.  Returns 1 when out was written, 0 (out untouched) when every
   part is Original, the texture is not covered, cpsm is not RDTEX_PSMCT32,
   colors is not 1..256, or clut / out is NULL. */
int ico_appearance_recolour(const char *texName, const void *clut, unsigned int colors,
                            unsigned int cpsm, void *out);

#endif /* ICO_PORT_GAME_APPEARANCE_H */
