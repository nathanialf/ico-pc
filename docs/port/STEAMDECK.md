# Linux package and the Steam Deck

`tools/package_linux.sh <label>` builds the Linux package; this note says
what is in it and how to run it on a Steam Deck (SteamOS 3, Desktop Mode to
set it up, then Game Mode) or any x86-64 Linux desktop. The Deck steps
describe what the package is built to do; they have not yet been checked on
a Deck ([`docs/TODO.md`](../TODO.md)).

## What the package holds

`dist/ico-pc-<label>-linux.tar.gz`, one folder `ico-pc-<label>/`:

| file | what |
| --- | --- |
| `ico_pc` | the game, window build (`-DICO_HEADLESS=OFF -DICO_LINK_EXE=ON`, preset `linux-x64`, gcc 14, RelWithDebInfo). Needs no options |
| `libSDL3.so.0` | SDL 3.4.18 (zlib licence), the window, input and audio library. The binary finds it beside itself (`RUNPATH $ORIGIN`) |
| `ico-pc.ini` | optional settings: `iso=` (the disc image path) and `watchdog=30` |
| `ico_pc.map` | link map, to turn a crash address into a function name |
| `README.txt`, `LICENSE`, `NOTICES.txt`, `THIRD_PARTY.md` | the short run guide, the port's MIT licence, the third-party licence texts and the dependency list |

No game data is in it. `tools/package_linux.sh` builds a clean worktree of
`HEAD` (no `baserom/`, so it proves the binary needs none), checks that the
binary needs only `libSDL3.so.0`, libc and libm and that its run path is
`$ORIGIN`, stages `dist/stage/linux/` and archives it. It never runs the game.

### Decisions

- **SDL3 is shipped as a shared library, not linked statically.** The
  toolchain's SDL3 (`tools/fetch_deps.sh`) is a shared build; SDL loads X11,
  ALSA and the file dialog's helper at run time either way, so static linking
  would save one file and cost the user the option of replacing SDL. The
  archive must keep `ico_pc` and `libSDL3.so.0` together.
- **`libvulkan` is the host's.** The renderer loads `libvulkan.so.1` at run time
  through volk (`port/rhi`); the package ships no loader or driver. SteamOS
  has Mesa RADV; desktops need their GPU's Vulkan driver (`vulkaninfo`
  shows one). Without a driver the game stops with a message naming the
  `rhi_vk:` lines of the log.
- **The C library is the host's.** The package is built on Debian 13 (glibc
  2.41); `libSDL3.so.0` needs glibc 2.38 or later and `ico_pc` 2.34.
  SteamOS 3.5 and later, Debian 13 and Ubuntu 24.04 qualify.
- **No `.desktop` file or launcher script.** The Deck's way is a non-Steam
  game (below); a desktop user runs `./ico_pc`.
- **SDL3's video backend is X11** (the toolchain's SDL3 has no Wayland
  backend); Wayland desktops and Gamescope use XWayland.

## Setting it up on a Steam Deck

1. In Desktop Mode, unpack the archive somewhere writable and permanent, for
   example `~/Games/ico-pc-<label>/` (not `/usr`, and not a read-only
   place: `logs/` is written beside `ico_pc`).
2. Copy your own disc image of the PAL release (SCES-50760) to the Deck, as a
   `.iso` file. Put it next to `ico_pc` as `Ico_PAL.iso`, or leave it where
   it is and note its path.
3. Add `ico_pc` to Steam: Steam, Games, Add a Non-Steam Game to My Library,
   Browse, select `ico_pc`, Add Selected Programs. No launch options.
4. Run it. The first run needs the image once:
   - if `Ico_PAL.iso` is beside `ico_pc`, or `iso=` is set in `ico-pc.ini`
     (or `[paths] iso` in `config.toml`), it is used;
   - else a file dialog opens (SDL3's, which on this SDL build runs
     `zenity`; Desktop Mode has it if your image has it installed). Choose
     the `.iso`. The choice is saved to `ico-pc.ini`;
   - else (no dialog helper) the program stops with a message in
     `logs/ico-pc.log` and on the console; set `iso=` in `ico-pc.ini`, or put
     `Ico_PAL.iso` beside `ico_pc`.
   It then checks the image (its SHA-1), shows a small progress window and
   extracts the game's data once (about 870 MB, `ico.o2r`; docs/port/DATA.md).
   Later runs use `ico.o2r` and never open the `.iso`; you can delete it
   afterwards.
5. The game opens in a window and plays at 50 Hz (PAL), or at 60 Hz when
   the Settings menu's video mode says so (`[video] video_mode`), with
   vsync. In Game Mode Gamescope shows it full screen.

The first run's dialog is easier in Desktop Mode: do the first launch there,
then use Game Mode.

## Where things live

| what | where |
| --- | --- |
| pref folder | `$XDG_DATA_HOME/ico-pc/ico-pc/`, normally `~/.local/share/ico-pc/ico-pc/` (`SDL_GetPrefPath("ico-pc", "ico-pc")`, docs/port/CONFIG.md) |
| game data archive | `<pref folder>/ico.o2r` (else beside `ico_pc`) |
| settings | `<pref folder>/config.toml`; `ico-pc.ini` beside `ico_pc` overrides it |
| saves (memory card files) | `<pref folder>/memcard/` (`[paths] saves` moves it) |
| logs | `logs/ico-pc.log` and `logs/trace-*.txt` beside `ico_pc` |

Back up the saves by copying `memcard/`. Uninstall by deleting the program
folder and the pref folder.

## Controller

The Deck's built-in controls and any pad SDL3 recognises work with no setup:
the window build reads SDL's gamepad layer (hotplug, rumble;
docs/port/INPUT.md). In Steam's Game Mode keep the default controller layout
for non-Steam games ("Gamepad"), or whatever sends a standard gamepad
to the window. Keyboard and mouse also work (docs/port/INPUT.md for the
bindings and `config.toml`).

## If it does not start

- `logs/ico-pc.log` says why. `rhi_vk:` lines mean no usable Vulkan driver
  or a missing feature.
- `error while loading shared libraries: libSDL3.so.0`: `ico_pc` and
  `libSDL3.so.0` were separated.
- `version 'GLIBC_2.38' not found`: the host's C library is older than
  Debian 13's; update the OS.
- A crash writes a `CRASH:` block to the log; send it with `ico_pc.map`.
