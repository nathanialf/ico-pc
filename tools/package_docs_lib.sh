# tools/package_docs_lib.sh: the player guides of the release packages
# (docs/*.md that README.md links) and the package's ico-pc.ini, sourced by
# package_win.sh and package_linux.sh. Functions only. BUILDING.md, LEGAL.md and THIRD_PARTY.md
# are developer documents and are not in this list (the Linux package ships
# THIRD_PARTY.md at its root, as before).

pkg_player_docs=(FAQ CONTROLS OPTIONS TEXTURE_PACKS MODEL_PACKS RESHADE ANDROID PORTABLE_MODE TROUBLESHOOTING)

# pkg_stage_docs <repo> <dir>: copies the guides into <dir>/docs/ as they are
# in the repository (README.md is copied the same way), 1 when one is missing
pkg_stage_docs() {
    local src="$1/docs" d="$2/docs" n
    rm -rf "$d"
    mkdir -p "$d"
    for n in "${pkg_player_docs[@]}"; do
        cp "$src/$n.md" "$d/$n.md" || return 1
    done
}

# pkg_write_ini <file> <windows|linux> <iso>: the package's ico-pc.ini, its
# notes in the platform's wording, with the user's iso= value kept
pkg_write_ini() {
    if [[ "$2" == windows ]]; then
        cat > "$1" <<INI
# ico-pc.ini: optional settings, one key=value per line; lines starting
# with # or ; are notes. Everything works without editing this file.
#
# iso: the full path of your disc image of the PAL release (SCES-50760), a
# .iso or a .chd, for example iso=C:\\Games\\Ico_PAL.iso . A file named
# Ico_PAL.iso or Ico_PAL.chd next to the program is used before this line.
# Leave it empty and the first start asks for the image, then saves your
# choice here. The image is read once, to copy the game's data into your
# user folder (%APPDATA%\\ico-pc\\ico-pc); later starts do not need it.
iso=$3

# watchdog: if the game has not started this many seconds after launch, or
# stops responding for twice as long later, the program writes what it was
# doing to logs\\ico-pc.log and closes. 0 turns it off.
watchdog=30

# portable: remove the # in front of the next line to keep your saves,
# settings and the game's data in a folder named userdata next to the
# program, instead of in your user folder. Making an empty userdata folder
# next to the program does the same, and portable=0 turns it off even when
# that folder exists. To move an existing install, close the game and copy
# everything from your user folder into userdata first.
# portable=1

# texture packs: see textures\\SCES-50760\\replacements\\README.txt next to
# the program.
#
# Everything else (display, sound, controls, gameplay, texture packs) is in
# the in-game Options menu, which saves it to config.toml in your user
# folder. With a problem report, send logs\\ico-pc.log from next to the
# program.
INI
    else
        cat > "$1" <<INI
# ico-pc.ini: optional settings, one key=value per line; lines starting
# with # or ; are notes. Everything works without editing this file.
#
# iso: the full path of your disc image of the PAL release (SCES-50760), a
# .iso or a .chd, for example iso=/home/deck/Games/Ico_PAL.iso . A file
# named Ico_PAL.iso or Ico_PAL.chd next to ico_pc is used before this line.
# Leave it empty and the first start asks for the image (the file dialog
# needs zenity), then saves your choice here. The image is read once, to
# copy the game's data into your user folder (~/.local/share/ico-pc/ico-pc/);
# later starts do not need it.
iso=$3

# watchdog: if the game has not started this many seconds after launch, or
# stops responding for twice as long later, the program writes what it was
# doing to logs/ico-pc.log and closes. 0 turns it off.
watchdog=30

# portable: remove the # in front of the next line to keep your saves,
# settings and the game's data in a folder named userdata next to the
# program, instead of in your user folder. Making an empty userdata folder
# next to the program does the same, and portable=0 turns it off even when
# that folder exists. To move an existing install, close the game and copy
# everything from your user folder into userdata first.
# portable=1

# texture packs: see textures/SCES-50760/replacements/README.txt next to
# ico_pc.
#
# Everything else (display, sound, controls, gameplay, texture packs) is in
# the in-game Options menu, which saves it to config.toml in your user
# folder. With a problem report, send logs/ico-pc.log from next to ico_pc.
INI
    fi
}
