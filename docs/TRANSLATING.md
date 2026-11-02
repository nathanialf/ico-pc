# Translating the game

The game can be played in a language its discs do not have, translated by a player. The
translation is built into the program: the words are drawn with the port's own lettering, in the
same look as the menus, over the English version of the European disc's game. No picture of the
game is changed or added.

Translations so far:

| Language in Options | Language | Translated by |
| --- | --- | --- |
| Русский (bob-ttz) | Russian | bob-ttz |
| Русский (cobra838) | Russian | cobra838 |

cobra838's came as C tables in a pull request rather than the two CSV files; it was brought into
the same two files below by hand, with the same checks.

## What a translation covers

- The game's own menus: the title, the memory card screens, the pause menu and the Options words
  the game itself shows. The European disc's menus are already drawn as text by the port, so a
  translation replaces their words.
- The menus the port adds: Options and its pages, the extras, photo mode's help, the messages and
  the achievements.
- The subtitles: 42 lines, each one or two lines on the screen. Yorda's lines are translated for
  the second playthrough, where the game shows them in English; the first playthrough shows her own
  script, which stays.

What stays as the disc has it, because it is a picture rather than words: the "Sony Computer
Entertainment Europe Presents" sign and the copyright line at the start, the logos, the LANGUAGE
and TV headings, the 50/60 Hz notes, the words that come with the title and ending logos, the
button pictures, and the staff roll (the game draws it with a lettering that has only Latin
letters).

A translation works with the European disc (SCES-50760), where Options > Language lists it after
the game's five languages. The Japanese, Korean and Asian discs keep their own words, and the North
American disc and the 2001 previews have no Language choice.

## What a translator sends

Two spreadsheets saved as CSV files (UTF-8), filled in from the two templates. Ask for the
current templates on the [issues page](https://github.com/nathanialf/ico-pc/issues): the
maintainer makes them from the English text, so they list every line the program has at the time.

The subtitles (`ico_subtitles.csv`): one row a line, with the columns `id` (`sub_` and the line's
number on the disc), `type`, `context` (where in the game), `speaker`, `english`, `translation` and
`notes`. Fill in `translation`. A `|` starts the second line; two lines at most, and about 50
letters a line. Lines marked "second play only" are Yorda's words.

The menus (`ico_menu_strings.csv`): one row a text, with the columns `id`, `context` (a row's name,
its value, the note under it, a message, the game's own menus, a place name), `english`,
`translation` and `notes`. Fill in `translation`; a row left empty stays in English. Keep these as
they are:

- `\n`, which starts a new line;
- `%d`, `%s`, `%llu` and the rest of what starts with `%`: the game puts a number or a name there, in
  the same order;
- the length of the notes. A note under a row has room for two lines of about 60 letters each
  (fewer in a language with wide letters); a longer one is kept in English until it is shortened.
  A row's name that is too wide for its place is drawn smaller, down to 60 %.

## Adding a translation

The maintainer does this once for each pair of files, in one change to the code. There is no
loader: the program carries the translation, so nothing is read from a file at run time and nothing
can go missing.

1. Convert the two files into C, once, with a script of the moment (the script is not kept: the
   result is what is reviewed):
   - `port/ui/strings_<language>_<translator>.c`: the table `ui_strings_<language>_<translator>`,
     every id of `port/ui/strings.h` in order, the translation's words, and `NULL` with a comment
     for an empty row and for an id newer than the template; an id is `menu_` and the id's name in
     lower case without `UI_STR_` (`menu_settings` is `UI_STR_SETTINGS`);
   - `port/game/subtitles_<language>_<translator>.c`: the table `ico_subtitles_<language>_<translator>`,
     one `IcoSubtitle` a row in the file's order (the line's number, 1 for a "second play only"
     row, the words with `\n` for the `|`), and its count.
2. Name the language in `port/ui/strings.h`: an `UiLang` after the last translation and before
   `UI_LANG_TABLES` (`UI_LANG_RU_COBRA838` after `UI_LANG_RU_BOBTTZ`), its table's `extern`, and a
   `UI_STR_LANG_*` id at the end of the ids, written in the language itself
   (`"Русский (cobra838)"`) at the end of every table, the eight languages' and the translations'.
3. Wire it in, one entry each, in `UiLang` order:
   - `port/ui/strings.c` (`s_tables`);
   - `port/game/options.c` (`kUiLanguages`: the name the config files take, `ru-cobra838`) and
     `port/game/options.h` (`ICO_OPT_UI_LANGUAGE_COUNT`, and the names in the comments);
   - `port/ui/settings.c` (`menuLanguageName`'s `ids`);
   - `port/game/subtitles.h` (the subtitle table's `extern`), `port/ui/subtitle_text.c`
     (`ui_subtitle_table`'s `case`, and the count in its `_Static_assert`);
   - `port/ui/CMakeLists.txt` (`_ui_string_srcs`, and the subtitle file in `ico_ui_settings` and
     `font_coverage_test`), `port/game/CMakeLists.txt` (`achievements_test`'s list);
   - the tests' lists: `port/ui/test/strings_complete_test.c` (`kNames`, `kTables`, the name check),
     `font_coverage_test.c` (`kLangs`, the subtitle walk), `settings_test.c` (`testNotesFit`'s
     `kLang`), and a copy of `port/ui/test/subtitles_ru_test.c` for the new table (its record list,
     its second-play list), registered beside it and in the `settings_toml` lock;
   - `port/ui/test/font_corpus/<name>.txt`, a copy of `ru-bobttz.txt`.
4. Characters: `tools/subset_font_arimo.py` adds the characters of every translation's tables to
   Arimo's subset when the font has them, then `--check` passes (ctest `font_subset_arimo`); a
   script Arimo does not have (Chinese, Japanese, Korean, Thai, ...) needs a font of its own, as the
   three Noto subsets are made.
5. Credit the translator: [README.md](../README.md) (Special thanks, Translation), the staff roll's
   Translation block (`port/game/credits.c` (`s_lines`), ctest `credits`), the release notes, and
   the table at the top of this page; the language's entry in [OPTIONS.md](OPTIONS.md) (Language).

### What the maintainer checks

- A row whose `english` is not today's English (the English changed after the template was made):
  its translation may say something the English does not say now, so it is `NULL` until it is translated
  again.
- A note longer than two lines at the notes' size, or longer than 255 bytes in UTF-8 (a Cyrillic
  letter is two bytes): `NULL`, and the translator is asked for a shorter one. A row's words are
  held to 255 bytes the same way, a message to 383.
- The `%` codes are the English row's, in the same order (ctest `strings_complete`).
- Every character has a glyph (ctests `font_audit`, `font_coverage`).
- The subtitles: one or two lines of at most 50 characters, the CSV's records and second-play rows
  (the new `subtitles_*` ctest).

The tests to run: `strings_complete`, `font_audit`, `font_coverage`, `font_subset_arimo`, the
translation's `subtitles_*` and its other-disc runs, `settings`, `settings_extra`, `options`,
`jimaku_text`.

## How it works

[GAME_FEATURES.md](port/GAME_FEATURES.md#community-translations) has the mechanism (the Language
row's entry, `[game] text_language`, the subtitle hook) and
[UI.md](port/UI.md#community-translations) the tables and the menu words.
