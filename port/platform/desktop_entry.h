/*
 * port/platform/desktop_entry.h
 *
 * Extras > Add to the applications menu (Linux): a desktop entry for the
 * freedesktop menus and the application's icon, written into the user's
 * data folder ($XDG_DATA_HOME, default ~/.local/share).  Pure functions
 * over a base folder and a program path, so the tests run on a fake folder;
 * nothing here is called before the player presses the row.
 *
 * The files are named after the application id the window carries
 * (SDL.app.metadata.identifier, "com.defnf.icopc"): Wayland compositors
 * match a window to its desktop entry by that name.
 */
#ifndef ICO_PLATFORM_DESKTOP_ENTRY_H
#define ICO_PLATFORM_DESKTOP_ENTRY_H

#include <stddef.h>

#define ICO_DESKTOP_APP_ID "com.defnf.icopc"

/* The entry's text for the program at exe into out: the desktop entry
   specification's Exec quoting (the path in double quotes; inside them
   backslash, double quote, backtick and dollar are backslash-escaped and a
   percent sign is doubled).  0, or -1 when exe is NULL or empty, holds a
   control character (a newline would end the line) or the text does not
   fit in size (out is then ""). */
int ico_desktop_entry_text(char *out, size_t size, const char *exe);

/* The data folder as the freedesktop base directory rules give it:
   $XDG_DATA_HOME when it is an absolute path, else $HOME/.local/share.
   0, or -1 (nothing fits, or neither variable is usable; out is then ""). */
int ico_desktop_entry_data_home(char *out, size_t size);

/* Writes <dataHome>/applications/com.defnf.icopc.desktop for the program at
   exe and copies the PNG at pngPath (the current icon, app_icon.h) to
   <dataHome>/icons/hicolor/256x256/apps/com.defnf.icopc.png, making the
   folders; each file goes to a .tmp name and is renamed over the old one.
   A NULL or empty pngPath writes the entry alone (the Icon= name then finds
   no picture; the caller says so).  The picture goes first, so a failure
   there leaves no entry pointing at a missing icon.  0, or -1 (a .tmp is
   never left behind). */
int ico_desktop_entry_install(const char *dataHome, const char *exe, const char *pngPath);

#endif
